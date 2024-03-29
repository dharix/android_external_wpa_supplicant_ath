/*
 * WPA Supplicant - driver interaction with generic Linux Wireless Extensions
 * Copyright (c) 2003-2007, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 *
 * This file implements a driver interface for the Linux Wireless Extensions.
 * When used with WE-18 or newer, this interface can be used as-is with number
 * of drivers. In addition to this, some of the common functions in this file
 * can be used by other driver interface implementations that use generic WE
 * ioctls, but require private ioctls for some of the functionality.
 */

#include "includes.h"
#include <sys/ioctl.h>
#include <net/if_arp.h>

#include "wireless_copy.h"
#include "common.h"
#include "driver.h"
#include "eloop.h"
#include "priv_netlink.h"
#include "driver_wext.h"
#include "ieee802_11_defs.h"
#include "wpa_common.h"


static int wpa_driver_wext_flush_pmkid(void *priv);
static int wpa_driver_wext_get_range(void *priv);
static void wpa_driver_wext_finish_drv_init(struct wpa_driver_wext_data *drv);
static int wpa_driver_wext_disassociate(void *priv, const u8 *addr,
					int reason_code);
#ifdef ANDROID
#include "wpa_ctrl.h"
typedef enum {
        POWER_MODE_AUTO,
        POWER_MODE_ACTIVE
} POWER_MODE;
static int wpa_driver_atheros_pwr_mode(struct wpa_driver_wext_data *drv, int mode)
{
	struct iwreq iwr;
	os_memset(&iwr, 0, sizeof(iwr));
	os_strncpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	if (mode == POWER_MODE_AUTO)
		iwr.u.power.disabled = 0;
	else if (mode == POWER_MODE_ACTIVE)
		iwr.u.power.disabled = 1;
	else
		return -1;
	if (ioctl(drv->ioctl_sock, SIOCSIWPOWER, &iwr) < 0) {
		wpa_printf(MSG_DEBUG, "drv_wext: failed to control power\n");
	}
	return 0;
}
#define AR6000_IOCTL_WMI_SET_CHANNELPARAMS   (SIOCIWFIRSTPRIV+16)
#define AR6000_IOCTL_EXTENDED                (SIOCIWFIRSTPRIV+31)
#define AR6000_XIOCTRL_WMI_SET_WLAN_STATE                       35
typedef enum {
    WLAN_DISABLED,
    WLAN_ENABLED
} AR6000_WLAN_STATE;
static int wpa_driver_atheros_wlan_ctrl(struct wpa_driver_wext_data *drv, int enable)
{
	struct ifreq ifr;
	char buf[16];

	os_memset(&ifr, 0, sizeof(ifr));
	os_memset(buf, 0, sizeof(buf));

	os_strncpy(ifr.ifr_name, drv->ifname, IFNAMSIZ);
	((int *)buf)[0] = AR6000_XIOCTRL_WMI_SET_WLAN_STATE;
	ifr.ifr_data = buf;

	if (enable)
		((int *)buf)[1] = WLAN_ENABLED;
	else
		((int *)buf)[1] = WLAN_DISABLED;

	if (ioctl(drv->ioctl_sock, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
		return -1;
	}
	return 0;
}

static uint16_t wmic_ieee2freq(int chan)
{
	/* channel 14? */
	if (chan == 14) {
		return 2484;
	}
    if (chan < 14) {    /* 0-13 */
        return (2407 + (chan * 5));
    }
    return (5000 + (chan*5));
}

 typedef struct {
	 uint8_t	reserved1;
	 uint8_t	scanParam;              /* set if enable scan */
	 uint8_t	phyMode;                /* see WMI_PHY_MODE */
	 uint8_t	numChannels;            /* how many channels follow */
	 uint16_t	channelList[1];         /* channels in Mhz */
} STRUCT_PACKED WMI_CHANNEL_PARAMS_CMD;

typedef enum {
    WMI_DEFAULT_MODE = 0x0,
    WMI_11A_MODE  = 0x1,
    WMI_11G_MODE  = 0x2,
    WMI_11AG_MODE = 0x3,
    WMI_11B_MODE  = 0x4,
    WMI_11GONLY_MODE = 0x5,
    WMI_11GHT20_MODE = 0x6,
} WMI_PHY_MODE;

static int wpa_driver_atheros_cfg_chan(struct wpa_driver_wext_data *drv, int num)
{
	struct ifreq ifr;
	char buf[256];
	WMI_CHANNEL_PARAMS_CMD *chParamCmd = (WMI_CHANNEL_PARAMS_CMD *)buf;
	int i;
	uint16_t *clist;

	os_memset(&ifr, 0, sizeof(ifr));
	os_strncpy(ifr.ifr_name, drv->ifname, IFNAMSIZ);
	chParamCmd->phyMode = WMI_11G_MODE;
	clist = chParamCmd->channelList;
	chParamCmd->numChannels = num;
	chParamCmd->scanParam = 1;

	for (i = 0; i < num; i++)
		clist[i] = wmic_ieee2freq(i + 1);

	ifr.ifr_data = (void *)chParamCmd;

	if (ioctl(drv->ioctl_sock, AR6000_IOCTL_WMI_SET_CHANNELPARAMS, &ifr) < 0) {
		return -1;
	}
	return 0;
}
#endif // ANDROID
static int wpa_driver_wext_send_oper_ifla(struct wpa_driver_wext_data *drv,
					  int linkmode, int operstate)
{
	struct {
		struct nlmsghdr hdr;
		struct ifinfomsg ifinfo;
		char opts[16];
	} req;
	struct rtattr *rta;
	static int nl_seq;
	ssize_t ret;

	os_memset(&req, 0, sizeof(req));

	req.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.hdr.nlmsg_type = RTM_SETLINK;
	req.hdr.nlmsg_flags = NLM_F_REQUEST;
	req.hdr.nlmsg_seq = ++nl_seq;
	req.hdr.nlmsg_pid = 0;

	req.ifinfo.ifi_family = AF_UNSPEC;
	req.ifinfo.ifi_type = 0;
	req.ifinfo.ifi_index = drv->ifindex;
	req.ifinfo.ifi_flags = 0;
	req.ifinfo.ifi_change = 0;

	if (linkmode != -1) {
		rta = (struct rtattr *)
			((char *) &req + NLMSG_ALIGN(req.hdr.nlmsg_len));
		rta->rta_type = IFLA_LINKMODE;
		rta->rta_len = RTA_LENGTH(sizeof(char));
		*((char *) RTA_DATA(rta)) = linkmode;
		req.hdr.nlmsg_len = NLMSG_ALIGN(req.hdr.nlmsg_len) +
			RTA_LENGTH(sizeof(char));
	}
	if (operstate != -1) {
		rta = (struct rtattr *)
			((char *) &req + NLMSG_ALIGN(req.hdr.nlmsg_len));
		rta->rta_type = IFLA_OPERSTATE;
		rta->rta_len = RTA_LENGTH(sizeof(char));
		*((char *) RTA_DATA(rta)) = operstate;
		req.hdr.nlmsg_len = NLMSG_ALIGN(req.hdr.nlmsg_len) +
			RTA_LENGTH(sizeof(char));
	}

	wpa_printf(MSG_DEBUG, "WEXT: Operstate: linkmode=%d, operstate=%d",
		   linkmode, operstate);

	ret = send(drv->event_sock, &req, req.hdr.nlmsg_len, 0);
	if (ret < 0) {
		wpa_printf(MSG_DEBUG, "WEXT: Sending operstate IFLA failed: "
			   "%s (assume operstate is not supported)",
			   strerror(errno));
	}

	return ret < 0 ? -1 : 0;
}


int wpa_driver_wext_set_auth_param(struct wpa_driver_wext_data *drv,
				   int idx, u32 value)
{
	struct iwreq iwr;
	int ret = 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.param.flags = idx & IW_AUTH_INDEX;
	iwr.u.param.value = value;

	if (ioctl(drv->ioctl_sock, SIOCSIWAUTH, &iwr) < 0) {
		if (errno != EOPNOTSUPP) {
			wpa_printf(MSG_DEBUG, "WEXT: SIOCSIWAUTH(param %d "
				   "value 0x%x) failed: %s)",
				   idx, value, strerror(errno));
		}
		ret = errno == EOPNOTSUPP ? -2 : -1;
	}

	return ret;
}


/**
 * wpa_driver_wext_get_bssid - Get BSSID, SIOCGIWAP
 * @priv: Pointer to private wext data from wpa_driver_wext_init()
 * @bssid: Buffer for BSSID
 * Returns: 0 on success, -1 on failure
 */
int wpa_driver_wext_get_bssid(void *priv, u8 *bssid)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iwreq iwr;
	int ret = 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);

	if (ioctl(drv->ioctl_sock, SIOCGIWAP, &iwr) < 0) {
		perror("ioctl[SIOCGIWAP]");
		ret = -1;
	}
	os_memcpy(bssid, iwr.u.ap_addr.sa_data, ETH_ALEN);

	return ret;
}


/**
 * wpa_driver_wext_set_bssid - Set BSSID, SIOCSIWAP
 * @priv: Pointer to private wext data from wpa_driver_wext_init()
 * @bssid: BSSID
 * Returns: 0 on success, -1 on failure
 */
int wpa_driver_wext_set_bssid(void *priv, const u8 *bssid)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iwreq iwr;
	int ret = 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.ap_addr.sa_family = ARPHRD_ETHER;
	if (bssid)
		os_memcpy(iwr.u.ap_addr.sa_data, bssid, ETH_ALEN);
	else
		os_memset(iwr.u.ap_addr.sa_data, 0, ETH_ALEN);

	if (ioctl(drv->ioctl_sock, SIOCSIWAP, &iwr) < 0) {
		perror("ioctl[SIOCSIWAP]");
		ret = -1;
	}

	return ret;
}


/**
 * wpa_driver_wext_get_ssid - Get SSID, SIOCGIWESSID
 * @priv: Pointer to private wext data from wpa_driver_wext_init()
 * @ssid: Buffer for the SSID; must be at least 32 bytes long
 * Returns: SSID length on success, -1 on failure
 */
int wpa_driver_wext_get_ssid(void *priv, u8 *ssid)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iwreq iwr;
	int ret = 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.essid.pointer = (caddr_t) ssid;
	iwr.u.essid.length = 32;

	if (ioctl(drv->ioctl_sock, SIOCGIWESSID, &iwr) < 0) {
		perror("ioctl[SIOCGIWESSID]");
		ret = -1;
	} else {
		ret = iwr.u.essid.length;
		if (ret > 32)
			ret = 32;
		/* Some drivers include nul termination in the SSID, so let's
		 * remove it here before further processing. WE-21 changes this
		 * to explicitly require the length _not_ to include nul
		 * termination. */
		if (ret > 0 && ssid[ret - 1] == '\0' &&
		    drv->we_version_compiled < 21)
			ret--;
	}

	return ret;
}


/**
 * wpa_driver_wext_set_ssid - Set SSID, SIOCSIWESSID
 * @priv: Pointer to private wext data from wpa_driver_wext_init()
 * @ssid: SSID
 * @ssid_len: Length of SSID (0..32)
 * Returns: 0 on success, -1 on failure
 */
int wpa_driver_wext_set_ssid(void *priv, const u8 *ssid, size_t ssid_len)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iwreq iwr;
	int ret = 0;
	char buf[33];

	if (ssid_len > 32)
		return -1;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	/* flags: 1 = ESSID is active, 0 = not (promiscuous) */
	iwr.u.essid.flags = (ssid_len != 0);
	os_memset(buf, 0, sizeof(buf));
	os_memcpy(buf, ssid, ssid_len);
	iwr.u.essid.pointer = (caddr_t) buf;
	if (drv->we_version_compiled < 21) {
		/* For historic reasons, set SSID length to include one extra
		 * character, C string nul termination, even though SSID is
		 * really an octet string that should not be presented as a C
		 * string. Some Linux drivers decrement the length by one and
		 * can thus end up missing the last octet of the SSID if the
		 * length is not incremented here. WE-21 changes this to
		 * explicitly require the length _not_ to include nul
		 * termination. */
		if (ssid_len)
			ssid_len++;
	}
	iwr.u.essid.length = ssid_len;

	if (ioctl(drv->ioctl_sock, SIOCSIWESSID, &iwr) < 0) {
		perror("ioctl[SIOCSIWESSID]");
		ret = -1;
	}

	return ret;
}


/**
 * wpa_driver_wext_set_freq - Set frequency/channel, SIOCSIWFREQ
 * @priv: Pointer to private wext data from wpa_driver_wext_init()
 * @freq: Frequency in MHz
 * Returns: 0 on success, -1 on failure
 */
int wpa_driver_wext_set_freq(void *priv, int freq)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iwreq iwr;
	int ret = 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.freq.m = freq * 100000;
	iwr.u.freq.e = 1;

	if (ioctl(drv->ioctl_sock, SIOCSIWFREQ, &iwr) < 0) {
		perror("ioctl[SIOCSIWFREQ]");
		ret = -1;
	}

	return ret;
}


static void
wpa_driver_wext_event_wireless_custom(struct wpa_driver_wext_data *drv, void *ctx, char *custom)
{
	union wpa_event_data data;

	wpa_printf(MSG_MSGDUMP, "WEXT: Custom wireless event: '%s'",
		   custom);

	os_memset(&data, 0, sizeof(data));
	/* Host AP driver */
	if (os_strncmp(custom, "MLME-MICHAELMICFAILURE.indication", 33) == 0) {
		data.michael_mic_failure.unicast =
			os_strstr(custom, " unicast ") != NULL;
		/* TODO: parse parameters(?) */
		wpa_supplicant_event(ctx, EVENT_MICHAEL_MIC_FAILURE, &data);
	} else if (os_strncmp(custom, "ASSOCINFO(ReqIEs=", 17) == 0) {
		char *spos;
		int bytes;

		spos = custom + 17;

		bytes = strspn(spos, "0123456789abcdefABCDEF");
		if (!bytes || (bytes & 1))
			return;
		bytes /= 2;

		data.assoc_info.req_ies = os_malloc(bytes);
		if (data.assoc_info.req_ies == NULL)
			return;

		data.assoc_info.req_ies_len = bytes;
		hexstr2bin(spos, data.assoc_info.req_ies, bytes);

		spos += bytes * 2;

		data.assoc_info.resp_ies = NULL;
		data.assoc_info.resp_ies_len = 0;

		if (os_strncmp(spos, " RespIEs=", 9) == 0) {
			spos += 9;

			bytes = strspn(spos, "0123456789abcdefABCDEF");
			if (!bytes || (bytes & 1))
				goto done;
			bytes /= 2;

			data.assoc_info.resp_ies = os_malloc(bytes);
			if (data.assoc_info.resp_ies == NULL)
				goto done;

			data.assoc_info.resp_ies_len = bytes;
			hexstr2bin(spos, data.assoc_info.resp_ies, bytes);
		}

		wpa_supplicant_event(ctx, EVENT_ASSOCINFO, &data);

	done:
		os_free(data.assoc_info.resp_ies);
		os_free(data.assoc_info.req_ies);
//Atheros
	} else if (strncmp(custom, "PRE-AUTH", 8) == 0) {
		u8 res;
		int bytes;
		char *spos;

		spos = custom + 8;

		bytes = strspn(spos, "0123456789abcdefABCDEF");
		if (!bytes || (bytes & 1))
			return;
		bytes /= 2;

		if (bytes < 8)
			return;

		hexstr2bin(spos, data.pmkid_candidate.bssid, ETH_ALEN);
		spos += ETH_ALEN * 2;
		hexstr2bin(spos, &res, 1);
		data.pmkid_candidate.index = res;
		spos += 2;
		hexstr2bin(spos, &res, 1);
		data.pmkid_candidate.preauth = res;
		wpa_supplicant_event(ctx, EVENT_PMKID_CANDIDATE, &data);
	} else if (strncmp(custom, "ASSOCRESPIE=", 12) == 0) {
		int bytes;
		char *spos;

		spos = custom + 12;
		bytes = strspn(spos, "0123456789abcdefABCDEF");
		if (!bytes || (bytes & 1))
			return;
		bytes /= 2;

		free(drv->assoc_resp_ies);
		drv->assoc_resp_ies = malloc(bytes);
		if (drv->assoc_resp_ies == NULL) {
			drv->assoc_resp_ies_len = 0;
			return;
		}
		hexstr2bin(spos, drv->assoc_resp_ies, bytes);
		drv->assoc_resp_ies_len = bytes;
//Atheros
#ifdef CONFIG_PEERKEY
	} else if (os_strncmp(custom, "STKSTART.request=", 17) == 0) {
		if (hwaddr_aton(custom + 17, data.stkstart.peer)) {
			wpa_printf(MSG_DEBUG, "WEXT: unrecognized "
				   "STKSTART.request '%s'", custom + 17);
			return;
		}
		wpa_supplicant_event(ctx, EVENT_STKSTART, &data);
#endif /* CONFIG_PEERKEY */
#ifdef ANDROID
#ifdef ANDROID_ECLAIR
	} else if (os_strncmp(custom, "STOP", 4) == 0) {
		wpa_msg(ctx, MSG_INFO, WPA_EVENT_DRIVER_STATE "STOPPED");
	} else if (os_strncmp(custom, "START", 5) == 0) {
		wpa_msg(ctx, MSG_INFO, WPA_EVENT_DRIVER_STATE "STARTED");
#endif
#endif // ANDROID && ECLAIR
	}
}


static int wpa_driver_wext_event_wireless_michaelmicfailure(
	void *ctx, const char *ev, size_t len)
{
	const struct iw_michaelmicfailure *mic;
	union wpa_event_data data;

	if (len < sizeof(*mic))
		return -1;

	mic = (const struct iw_michaelmicfailure *) ev;

	wpa_printf(MSG_DEBUG, "Michael MIC failure wireless event: "
		   "flags=0x%x src_addr=" MACSTR, mic->flags,
		   MAC2STR(mic->src_addr.sa_data));

	os_memset(&data, 0, sizeof(data));
	data.michael_mic_failure.unicast = !(mic->flags & IW_MICFAILURE_GROUP);
	wpa_supplicant_event(ctx, EVENT_MICHAEL_MIC_FAILURE, &data);

	return 0;
}


static int wpa_driver_wext_event_wireless_pmkidcand(
	struct wpa_driver_wext_data *drv, const char *ev, size_t len)
{
	const struct iw_pmkid_cand *cand;
	union wpa_event_data data;
	const u8 *addr;

	if (len < sizeof(*cand))
		return -1;

	cand = (const struct iw_pmkid_cand *) ev;
	addr = (const u8 *) cand->bssid.sa_data;

	wpa_printf(MSG_DEBUG, "PMKID candidate wireless event: "
		   "flags=0x%x index=%d bssid=" MACSTR, cand->flags,
		   cand->index, MAC2STR(addr));

	os_memset(&data, 0, sizeof(data));
	os_memcpy(data.pmkid_candidate.bssid, addr, ETH_ALEN);
	data.pmkid_candidate.index = cand->index;
	data.pmkid_candidate.preauth = cand->flags & IW_PMKID_CAND_PREAUTH;
	wpa_supplicant_event(drv->ctx, EVENT_PMKID_CANDIDATE, &data);

	return 0;
}


static int wpa_driver_wext_event_wireless_assocreqie(
	struct wpa_driver_wext_data *drv, const char *ev, int len)
{
	if (len < 0)
		return -1;

	wpa_hexdump(MSG_DEBUG, "AssocReq IE wireless event", (const u8 *) ev,
		    len);
	os_free(drv->assoc_req_ies);
	drv->assoc_req_ies = os_malloc(len);
	if (drv->assoc_req_ies == NULL) {
		drv->assoc_req_ies_len = 0;
		return -1;
	}
	os_memcpy(drv->assoc_req_ies, ev, len);
	drv->assoc_req_ies_len = len;

	return 0;
}


static int wpa_driver_wext_event_wireless_assocrespie(
	struct wpa_driver_wext_data *drv, const char *ev, int len)
{
	if (len < 0)
		return -1;

	wpa_hexdump(MSG_DEBUG, "AssocResp IE wireless event", (const u8 *) ev,
		    len);
	os_free(drv->assoc_resp_ies);
	drv->assoc_resp_ies = os_malloc(len);
	if (drv->assoc_resp_ies == NULL) {
		drv->assoc_resp_ies_len = 0;
		return -1;
	}
	os_memcpy(drv->assoc_resp_ies, ev, len);
	drv->assoc_resp_ies_len = len;

	return 0;
}


static void wpa_driver_wext_event_assoc_ies(struct wpa_driver_wext_data *drv)
{
	union wpa_event_data data;

	if (drv->assoc_req_ies == NULL && drv->assoc_resp_ies == NULL)
		return;

	os_memset(&data, 0, sizeof(data));
	if (drv->assoc_req_ies) {
		data.assoc_info.req_ies = drv->assoc_req_ies;
		drv->assoc_req_ies = NULL;
		data.assoc_info.req_ies_len = drv->assoc_req_ies_len;
	}
	if (drv->assoc_resp_ies) {
		data.assoc_info.resp_ies = drv->assoc_resp_ies;
		drv->assoc_resp_ies = NULL;
		data.assoc_info.resp_ies_len = drv->assoc_resp_ies_len;
	}

	wpa_supplicant_event(drv->ctx, EVENT_ASSOCINFO, &data);

	os_free(data.assoc_info.req_ies);
	os_free(data.assoc_info.resp_ies);
}


static void wpa_driver_wext_event_wireless(struct wpa_driver_wext_data *drv,
					   void *ctx, char *data, int len)
{
	struct iw_event iwe_buf, *iwe = &iwe_buf;
	char *pos, *end, *custom, *buf;

	pos = data;
	end = data + len;

	while (pos + IW_EV_LCP_LEN <= end) {
		/* Event data may be unaligned, so make a local, aligned copy
		 * before processing. */
		os_memcpy(&iwe_buf, pos, IW_EV_LCP_LEN);
		if (iwe->cmd != IWEVGENIE) {
			wpa_printf(MSG_DEBUG, "Wireless event: cmd=0x%x len=%d",
				   iwe->cmd, iwe->len);
		}
		if (iwe->len <= IW_EV_LCP_LEN)
			return;

		custom = pos + IW_EV_POINT_LEN;
		if (drv->we_version_compiled > 18 &&
		    (iwe->cmd == IWEVMICHAELMICFAILURE ||
		     iwe->cmd == IWEVCUSTOM ||
		     iwe->cmd == IWEVASSOCREQIE ||
		     iwe->cmd == IWEVASSOCRESPIE ||
		     iwe->cmd == IWEVPMKIDCAND)) {
			/* WE-19 removed the pointer from struct iw_point */
			char *dpos = (char *) &iwe_buf.u.data.length;
			int dlen = dpos - (char *) &iwe_buf;
			os_memcpy(dpos, pos + IW_EV_LCP_LEN,
				  sizeof(struct iw_event) - dlen);
		} else {
			os_memcpy(&iwe_buf, pos, sizeof(struct iw_event));
			custom += IW_EV_POINT_OFF;
		}

		switch (iwe->cmd) {
		case SIOCGIWAP:
			wpa_printf(MSG_DEBUG, "Wireless event: new AP: "
				   MACSTR,
				   MAC2STR((u8 *) iwe->u.ap_addr.sa_data));
			if (is_zero_ether_addr(
				    (const u8 *) iwe->u.ap_addr.sa_data) ||
			    os_memcmp(iwe->u.ap_addr.sa_data,
				      "\x44\x44\x44\x44\x44\x44", ETH_ALEN) ==
			    0) {
				os_free(drv->assoc_req_ies);
				drv->assoc_req_ies = NULL;
				os_free(drv->assoc_resp_ies);
				drv->assoc_resp_ies = NULL;
				wpa_supplicant_event(ctx, EVENT_DISASSOC,
						     NULL);

			} else {
				wpa_driver_wext_event_assoc_ies(drv);
				wpa_supplicant_event(ctx, EVENT_ASSOC, NULL);
			}
			break;
		case IWEVMICHAELMICFAILURE:
			if (custom + iwe->u.data.length > end) {
				wpa_printf(MSG_DEBUG, "WEXT: Invalid "
					   "IWEVMICHAELMICFAILURE length");
				return;
			}
			wpa_driver_wext_event_wireless_michaelmicfailure(
				ctx, custom, iwe->u.data.length);
			break;
		case IWEVCUSTOM:
			if (custom + iwe->u.data.length > end) {
				wpa_printf(MSG_DEBUG, "WEXT: Invalid "
					   "IWEVCUSTOM length");
				return;
			}
			buf = os_malloc(iwe->u.data.length + 1);
			if (buf == NULL)
				return;
			os_memcpy(buf, custom, iwe->u.data.length);
			buf[iwe->u.data.length] = '\0';
#ifdef ANDROID
			if (os_strncmp(buf, "HOST_ASLEEP=", 12) == 0) {
				drv->host_asleep = (os_strncmp(buf+12, "asleep", 6)==0);
			}
#endif
			wpa_driver_wext_event_wireless_custom(drv, ctx, buf);
			os_free(buf);
			break;
		case SIOCGIWSCAN:
			drv->scan_complete_events = 1;
			eloop_cancel_timeout(wpa_driver_wext_scan_timeout,
					     drv, ctx);
			wpa_supplicant_event(ctx, EVENT_SCAN_RESULTS, NULL);
			break;
		case IWEVASSOCREQIE:
			if (custom + iwe->u.data.length > end) {
				wpa_printf(MSG_DEBUG, "WEXT: Invalid "
					   "IWEVASSOCREQIE length");
				return;
			}
			wpa_driver_wext_event_wireless_assocreqie(
				drv, custom, iwe->u.data.length);
			break;
		case IWEVASSOCRESPIE:
			if (custom + iwe->u.data.length > end) {
				wpa_printf(MSG_DEBUG, "WEXT: Invalid "
					   "IWEVASSOCRESPIE length");
				return;
			}
			wpa_driver_wext_event_wireless_assocrespie(
				drv, custom, iwe->u.data.length);
			break;
		case IWEVPMKIDCAND:
			if (custom + iwe->u.data.length > end) {
				wpa_printf(MSG_DEBUG, "WEXT: Invalid "
					   "IWEVPMKIDCAND length");
				return;
			}
			wpa_driver_wext_event_wireless_pmkidcand(
				drv, custom, iwe->u.data.length);
			break;
		}

		pos += iwe->len;
	}
}


static void wpa_driver_wext_event_link(struct wpa_driver_wext_data *drv,
				       void *ctx, char *buf, size_t len,
				       int del)
{
	union wpa_event_data event;

	os_memset(&event, 0, sizeof(event));
	if (len > sizeof(event.interface_status.ifname))
		len = sizeof(event.interface_status.ifname) - 1;
	os_memcpy(event.interface_status.ifname, buf, len);
	event.interface_status.ievent = del ? EVENT_INTERFACE_REMOVED :
		EVENT_INTERFACE_ADDED;

	wpa_printf(MSG_DEBUG, "RTM_%sLINK, IFLA_IFNAME: Interface '%s' %s",
		   del ? "DEL" : "NEW",
		   event.interface_status.ifname,
		   del ? "removed" : "added");

	if (os_strcmp(drv->ifname, event.interface_status.ifname) == 0) {
		if (del)
			drv->if_removed = 1;
		else
			drv->if_removed = 0;
	}

	wpa_supplicant_event(ctx, EVENT_INTERFACE_STATUS, &event);
}


static int wpa_driver_wext_own_ifname(struct wpa_driver_wext_data *drv,
				      struct nlmsghdr *h)
{
	struct ifinfomsg *ifi;
	int attrlen, nlmsg_len, rta_len;
	struct rtattr *attr;

	ifi = NLMSG_DATA(h);

	nlmsg_len = NLMSG_ALIGN(sizeof(struct ifinfomsg));

	attrlen = h->nlmsg_len - nlmsg_len;
	if (attrlen < 0)
		return 0;

	attr = (struct rtattr *) (((char *) ifi) + nlmsg_len);

	rta_len = RTA_ALIGN(sizeof(struct rtattr));
	while (RTA_OK(attr, attrlen)) {
		if (attr->rta_type == IFLA_IFNAME) {
			if (os_strcmp(((char *) attr) + rta_len, drv->ifname)
			    == 0)
				return 1;
			else
				break;
		}
		attr = RTA_NEXT(attr, attrlen);
	}

	return 0;
}


static int wpa_driver_wext_own_ifindex(struct wpa_driver_wext_data *drv,
				       int ifindex, struct nlmsghdr *h)
{
	if (drv->ifindex == ifindex || drv->ifindex2 == ifindex)
		return 1;

	if (drv->if_removed && wpa_driver_wext_own_ifname(drv, h)) {
		drv->ifindex = if_nametoindex(drv->ifname);
		wpa_printf(MSG_DEBUG, "WEXT: Update ifindex for a removed "
			   "interface");
		wpa_driver_wext_finish_drv_init(drv);
		return 1;
	}

	return 0;
}


static void wpa_driver_wext_event_rtm_newlink(struct wpa_driver_wext_data *drv,
					      void *ctx, struct nlmsghdr *h,
					      size_t len)
{
	struct ifinfomsg *ifi;
	int attrlen, nlmsg_len, rta_len;
	struct rtattr * attr;

	if (len < sizeof(*ifi))
		return;

	ifi = NLMSG_DATA(h);

	if (!wpa_driver_wext_own_ifindex(drv, ifi->ifi_index, h)) {
		wpa_printf(MSG_DEBUG, "Ignore event for foreign ifindex %d",
			   ifi->ifi_index);
		return;
	}
	if (ifi->ifi_change & IFF_UP)
		wpa_printf(MSG_DEBUG, "RTM_NEWLINK: operstate=%d ifi_flags=0x%x "
		   "(%s%s%s%s)",
		   drv->operstate, ifi->ifi_flags,
		   (ifi->ifi_flags & IFF_UP) ? "[UP]" : "",
		   (ifi->ifi_flags & IFF_RUNNING) ? "[RUNNING]" : "",
		   (ifi->ifi_flags & IFF_LOWER_UP) ? "[LOWER_UP]" : "",
		   (ifi->ifi_flags & IFF_DORMANT) ? "[DORMANT]" : "");
	/*
	 * Some drivers send the association event before the operup event--in
	 * this case, lifting operstate in wpa_driver_wext_set_operstate()
	 * fails. This will hit us when wpa_supplicant does not need to do
	 * IEEE 802.1X authentication
	 */
	if (drv->operstate == 1 &&
	    (ifi->ifi_flags & (IFF_LOWER_UP | IFF_DORMANT)) == IFF_LOWER_UP &&
	    !(ifi->ifi_flags & IFF_RUNNING))
		wpa_driver_wext_send_oper_ifla(drv, -1, IF_OPER_UP);

	nlmsg_len = NLMSG_ALIGN(sizeof(struct ifinfomsg));

	attrlen = h->nlmsg_len - nlmsg_len;
	if (attrlen < 0)
		return;

	attr = (struct rtattr *) (((char *) ifi) + nlmsg_len);

	rta_len = RTA_ALIGN(sizeof(struct rtattr));
	while (RTA_OK(attr, attrlen)) {
		if (attr->rta_type == IFLA_WIRELESS) {
			wpa_driver_wext_event_wireless(
				drv, ctx, ((char *) attr) + rta_len,
				attr->rta_len - rta_len);
		} else if (attr->rta_type == IFLA_IFNAME) {
			if (ifi->ifi_change & IFF_UP) {
				wpa_driver_wext_event_link(drv, ctx,
						   ((char *) attr) + rta_len,
						   attr->rta_len - rta_len, 0);
			}
		}
		attr = RTA_NEXT(attr, attrlen);
	}
}


static void wpa_driver_wext_event_rtm_dellink(struct wpa_driver_wext_data *drv,
					      void *ctx, struct nlmsghdr *h,
					      size_t len)
{
	struct ifinfomsg *ifi;
	int attrlen, nlmsg_len, rta_len;
	struct rtattr * attr;

	if (len < sizeof(*ifi))
		return;

	ifi = NLMSG_DATA(h);

	nlmsg_len = NLMSG_ALIGN(sizeof(struct ifinfomsg));

	attrlen = h->nlmsg_len - nlmsg_len;
	if (attrlen < 0)
		return;

	attr = (struct rtattr *) (((char *) ifi) + nlmsg_len);

	rta_len = RTA_ALIGN(sizeof(struct rtattr));
	while (RTA_OK(attr, attrlen)) {
		if (attr->rta_type == IFLA_IFNAME) {
			wpa_driver_wext_event_link(drv,  ctx,
						   ((char *) attr) + rta_len,
						   attr->rta_len - rta_len, 1);
		}
		attr = RTA_NEXT(attr, attrlen);
	}
}


static void wpa_driver_wext_event_receive(int sock, void *eloop_ctx,
					  void *sock_ctx)
{
	char buf[8192];
	int left;
	struct sockaddr_nl from;
	socklen_t fromlen;
	struct nlmsghdr *h;
	int max_events = 10;

try_again:
	fromlen = sizeof(from);
	left = recvfrom(sock, buf, sizeof(buf), MSG_DONTWAIT,
			(struct sockaddr *) &from, &fromlen);
	if (left < 0) {
		if (errno != EINTR && errno != EAGAIN)
			perror("recvfrom(netlink)");
		return;
	}

	h = (struct nlmsghdr *) buf;
	while (left >= (int) sizeof(*h)) {
		int len, plen;

		len = h->nlmsg_len;
		plen = len - sizeof(*h);
		if (len > left || plen < 0) {
			wpa_printf(MSG_DEBUG, "Malformed netlink message: "
				   "len=%d left=%d plen=%d",
				   len, left, plen);
			break;
		}

		switch (h->nlmsg_type) {
		case RTM_NEWLINK:
			wpa_driver_wext_event_rtm_newlink(eloop_ctx, sock_ctx,
							  h, plen);
			break;
		case RTM_DELLINK:
			wpa_driver_wext_event_rtm_dellink(eloop_ctx, sock_ctx,
							  h, plen);
			break;
		}

		len = NLMSG_ALIGN(len);
		left -= len;
		h = (struct nlmsghdr *) ((char *) h + len);
	}

	if (left > 0) {
		wpa_printf(MSG_DEBUG, "%d extra bytes in the end of netlink "
			   "message", left);
	}

	if (--max_events > 0) {
		/*
		 * Try to receive all events in one eloop call in order to
		 * limit race condition on cases where AssocInfo event, Assoc
		 * event, and EAPOL frames are received more or less at the
		 * same time. We want to process the event messages first
		 * before starting EAPOL processing.
		 */
		goto try_again;
	}
}


static int wpa_driver_wext_get_ifflags_ifname(struct wpa_driver_wext_data *drv,
					      const char *ifname, int *flags)
{
	struct ifreq ifr;

	os_memset(&ifr, 0, sizeof(ifr));
	os_strlcpy(ifr.ifr_name, ifname, IFNAMSIZ);
	if (ioctl(drv->ioctl_sock, SIOCGIFFLAGS, (caddr_t) &ifr) < 0) {
		perror("ioctl[SIOCGIFFLAGS]");
		return -1;
	}
	*flags = ifr.ifr_flags & 0xffff;
	return 0;
}


/**
 * wpa_driver_wext_get_ifflags - Get interface flags (SIOCGIFFLAGS)
 * @drv: driver_wext private data
 * @flags: Pointer to returned flags value
 * Returns: 0 on success, -1 on failure
 */
int wpa_driver_wext_get_ifflags(struct wpa_driver_wext_data *drv, int *flags)
{
	return wpa_driver_wext_get_ifflags_ifname(drv, drv->ifname, flags);
}


static int wpa_driver_wext_set_ifflags_ifname(struct wpa_driver_wext_data *drv,
					      const char *ifname, int flags)
{
	struct ifreq ifr;

	os_memset(&ifr, 0, sizeof(ifr));
	os_strlcpy(ifr.ifr_name, ifname, IFNAMSIZ);
	ifr.ifr_flags = flags & 0xffff;
	if (ioctl(drv->ioctl_sock, SIOCSIFFLAGS, (caddr_t) &ifr) < 0) {
		perror("SIOCSIFFLAGS");
		return -1;
	}
	return 0;
}


/**
 * wpa_driver_wext_set_ifflags - Set interface flags (SIOCSIFFLAGS)
 * @drv: driver_wext private data
 * @flags: New value for flags
 * Returns: 0 on success, -1 on failure
 */
int wpa_driver_wext_set_ifflags(struct wpa_driver_wext_data *drv, int flags)
{
	return wpa_driver_wext_set_ifflags_ifname(drv, drv->ifname, flags);
}


/**
 * wpa_driver_wext_init - Initialize WE driver interface
 * @ctx: context to be used when calling wpa_supplicant functions,
 * e.g., wpa_supplicant_event()
 * @ifname: interface name, e.g., wlan0
 * Returns: Pointer to private data, %NULL on failure
 */
void * wpa_driver_wext_init(void *ctx, const char *ifname)
{
	int s;
	struct sockaddr_nl local;
	struct wpa_driver_wext_data *drv;

	drv = os_zalloc(sizeof(*drv));
	if (drv == NULL)
		return NULL;
	drv->ctx = ctx;
	os_strlcpy(drv->ifname, ifname, sizeof(drv->ifname));
#ifdef ANDROID
	drv->scan_channels = 11;
#endif

	drv->ioctl_sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (drv->ioctl_sock < 0) {
		perror("socket(PF_INET,SOCK_DGRAM)");
		os_free(drv);
		return NULL;
	}

	s = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (s < 0) {
		perror("socket(PF_NETLINK,SOCK_RAW,NETLINK_ROUTE)");
		close(drv->ioctl_sock);
		os_free(drv);
		return NULL;
	}

	os_memset(&local, 0, sizeof(local));
	local.nl_family = AF_NETLINK;
	local.nl_groups = RTMGRP_LINK;
	if (bind(s, (struct sockaddr *) &local, sizeof(local)) < 0) {
		perror("bind(netlink)");
		close(s);
		close(drv->ioctl_sock);
		os_free(drv);
		return NULL;
	}

	eloop_register_read_sock(s, wpa_driver_wext_event_receive, drv, ctx);
	drv->event_sock = s;

	drv->mlme_sock = -1;

	wpa_driver_wext_finish_drv_init(drv);

	return drv;
}


static void wpa_driver_wext_finish_drv_init(struct wpa_driver_wext_data *drv)
{
	int flags;

	if (wpa_driver_wext_get_ifflags(drv, &flags) != 0)
		printf("Could not get interface '%s' flags\n", drv->ifname);
	else if (!(flags & IFF_UP)) {
		if (wpa_driver_wext_set_ifflags(drv, flags | IFF_UP) != 0) {
			printf("Could not set interface '%s' UP\n",
			       drv->ifname);
		} else {
			/*
			 * Wait some time to allow driver to initialize before
			 * starting configuring the driver. This seems to be
			 * needed at least some drivers that load firmware etc.
			 * when the interface is set up.
			 */
			wpa_printf(MSG_DEBUG, "Interface %s set UP - waiting "
				   "a second for the driver to complete "
				   "initialization", drv->ifname);
			sleep(1);
		}
	}

	/*
	 * Make sure that the driver does not have any obsolete PMKID entries.
	 */
	wpa_driver_wext_flush_pmkid(drv);

	if (wpa_driver_wext_set_mode(drv, 0) < 0) {
		printf("Could not configure driver to use managed mode\n");
	}

	wpa_driver_wext_get_range(drv);

	drv->ifindex = if_nametoindex(drv->ifname);

	if (os_strncmp(drv->ifname, "wlan", 4) == 0) {
		/*
		 * Host AP driver may use both wlan# and wifi# interface in
		 * wireless events. Since some of the versions included WE-18
		 * support, let's add the alternative ifindex also from
		 * driver_wext.c for the time being. This may be removed at
		 * some point once it is believed that old versions of the
		 * driver are not in use anymore.
		 */
		char ifname2[IFNAMSIZ + 1];
		os_strlcpy(ifname2, drv->ifname, sizeof(ifname2));
		os_memcpy(ifname2, "wifi", 4);
		wpa_driver_wext_alternative_ifindex(drv, ifname2);
	}

	wpa_driver_wext_send_oper_ifla(drv, 1, IF_OPER_DORMANT);
}


/**
 * wpa_driver_wext_deinit - Deinitialize WE driver interface
 * @priv: Pointer to private wext data from wpa_driver_wext_init()
 *
 * Shut down driver interface and processing of driver events. Free
 * private data buffer if one was allocated in wpa_driver_wext_init().
 */
void wpa_driver_wext_deinit(void *priv)
{
	struct wpa_driver_wext_data *drv = priv;
	int flags;

	eloop_cancel_timeout(wpa_driver_wext_scan_timeout, drv, drv->ctx);

	/*
	 * Clear possibly configured driver parameters in order to make it
	 * easier to use the driver after wpa_supplicant has been terminated.
	 */
	(void) wpa_driver_wext_set_bssid(drv,
					 (u8 *) "\x00\x00\x00\x00\x00\x00");

	wpa_driver_wext_send_oper_ifla(priv, 0, IF_OPER_UP);

	eloop_unregister_read_sock(drv->event_sock);
	if (drv->mlme_sock >= 0)
		eloop_unregister_read_sock(drv->mlme_sock);

	if (wpa_driver_wext_get_ifflags(drv, &flags) == 0)
		(void) wpa_driver_wext_set_ifflags(drv, flags & ~IFF_UP);

	close(drv->event_sock);
	close(drv->ioctl_sock);
	if (drv->mlme_sock >= 0)
		close(drv->mlme_sock);
	os_free(drv->assoc_req_ies);
	os_free(drv->assoc_resp_ies);
	os_free(drv);
}


/**
 * wpa_driver_wext_scan_timeout - Scan timeout to report scan completion
 * @eloop_ctx: Unused
 * @timeout_ctx: ctx argument given to wpa_driver_wext_init()
 *
 * This function can be used as registered timeout when starting a scan to
 * generate a scan completed event if the driver does not report this.
 */
void wpa_driver_wext_scan_timeout(void *eloop_ctx, void *timeout_ctx)
{
	wpa_printf(MSG_DEBUG, "Scan timeout - try to get results");
	wpa_supplicant_event(timeout_ctx, EVENT_SCAN_RESULTS, NULL);
}


/**
 * wpa_driver_wext_scan - Request the driver to initiate scan
 * @priv: Pointer to private wext data from wpa_driver_wext_init()
 * @ssid: Specific SSID to scan for (ProbeReq) or %NULL to scan for
 *	all SSIDs (either active scan with broadcast SSID or passive
 *	scan
 * @ssid_len: Length of the SSID
 * Returns: 0 on success, -1 on failure
 */
int wpa_driver_wext_scan(void *priv, const u8 *ssid, size_t ssid_len)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iwreq iwr;
	int ret = 0, timeout;
	struct iw_scan_req req;

	if (ssid_len > IW_ESSID_MAX_SIZE) {
		wpa_printf(MSG_DEBUG, "%s: too long SSID (%lu)",
			   __FUNCTION__, (unsigned long) ssid_len);
		return -1;
	}

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);

	if (ssid && ssid_len) {
		os_memset(&req, 0, sizeof(req));
		req.essid_len = ssid_len;
		req.bssid.sa_family = ARPHRD_ETHER;
		os_memset(req.bssid.sa_data, 0xff, ETH_ALEN);
		os_memcpy(req.essid, ssid, ssid_len);
		iwr.u.data.pointer = (caddr_t) &req;
		iwr.u.data.length = sizeof(req);
		iwr.u.data.flags = IW_SCAN_THIS_ESSID;
	}

	if (ioctl(drv->ioctl_sock, SIOCSIWSCAN, &iwr) < 0) {
		perror("ioctl[SIOCSIWSCAN]");
		return -1;
	}

	/* Not all drivers generate "scan completed" wireless event, so try to
	 * read results after a timeout. */
	timeout = 5;
#if 0
	if (drv->scan_complete_events) {
		/*
		 * The driver seems to deliver SIOCGIWSCAN events to notify
		 * when scan is complete, so use longer timeout to avoid race
		 * conditions with scanning and following association request.
		 */
		timeout = 30;
 	}
#endif
	wpa_printf(MSG_DEBUG, "Scan requested (ret=%d) - scan timeout %d "
		   "seconds", ret, timeout);
	eloop_cancel_timeout(wpa_driver_wext_scan_timeout, drv, drv->ctx);
	eloop_register_timeout(timeout, 0, wpa_driver_wext_scan_timeout, drv,
			       drv->ctx);

	return ret;
}


static u8 * wpa_driver_wext_giwscan(struct wpa_driver_wext_data *drv,
				    size_t *len)
{
	struct iwreq iwr;
	u8 *res_buf;
	size_t res_buf_len;

	res_buf_len = IW_SCAN_MAX_DATA;
	for (;;) {
		res_buf = os_malloc(res_buf_len);
		if (res_buf == NULL)
			return NULL;
		os_memset(&iwr, 0, sizeof(iwr));
		os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
		iwr.u.data.pointer = res_buf;
		iwr.u.data.length = res_buf_len;

		if (ioctl(drv->ioctl_sock, SIOCGIWSCAN, &iwr) == 0)
			break;

		if (errno == E2BIG && res_buf_len < 65535) {
			os_free(res_buf);
			res_buf = NULL;
			res_buf_len *= 2;
			if (res_buf_len > 65535)
				res_buf_len = 65535; /* 16-bit length field */
			wpa_printf(MSG_DEBUG, "Scan results did not fit - "
				   "trying larger buffer (%lu bytes)",
				   (unsigned long) res_buf_len);
		} else {
			perror("ioctl[SIOCGIWSCAN]");
			os_free(res_buf);
			return NULL;
		}
	}

	if (iwr.u.data.length > res_buf_len) {
		os_free(res_buf);
		return NULL;
	}
	*len = iwr.u.data.length;

	return res_buf;
}


/*
 * Data structure for collecting WEXT scan results. This is needed to allow
 * the various methods of reporting IEs to be combined into a single IE buffer.
 */
struct wext_scan_data {
	struct wpa_scan_res res;
	u8 *ie;
	size_t ie_len;
	u8 ssid[32];
	size_t ssid_len;
	int maxrate;
};


static void wext_get_scan_mode(struct iw_event *iwe,
			       struct wext_scan_data *res)
{
	if (iwe->u.mode == IW_MODE_ADHOC)
		res->res.caps |= IEEE80211_CAP_IBSS;
	else if (iwe->u.mode == IW_MODE_MASTER || iwe->u.mode == IW_MODE_INFRA)
		res->res.caps |= IEEE80211_CAP_ESS;
}


static void wext_get_scan_ssid(struct iw_event *iwe,
			       struct wext_scan_data *res, char *custom,
			       char *end)
{
	int ssid_len = iwe->u.essid.length;
	if (custom + ssid_len > end)
		return;
	if (iwe->u.essid.flags &&
	    ssid_len > 0 &&
	    ssid_len <= IW_ESSID_MAX_SIZE) {
		os_memcpy(res->ssid, custom, ssid_len);
		res->ssid_len = ssid_len;
	}
}


static void wext_get_scan_freq(struct iw_event *iwe,
			       struct wext_scan_data *res)
{
	int divi = 1000000, i;

	if (iwe->u.freq.e == 0) {
		/*
		 * Some drivers do not report frequency, but a channel.
		 * Try to map this to frequency by assuming they are using
		 * IEEE 802.11b/g.  But don't overwrite a previously parsed
		 * frequency if the driver sends both frequency and channel,
		 * since the driver may be sending an A-band channel that we
		 * don't handle here.
		 */

		if (res->res.freq)
			return;

		if (iwe->u.freq.m >= 1 && iwe->u.freq.m <= 13) {
			res->res.freq = 2407 + 5 * iwe->u.freq.m;
			return;
		} else if (iwe->u.freq.m == 14) {
			res->res.freq = 2484;
			return;
		}
	}

	if (iwe->u.freq.e > 6) {
		wpa_printf(MSG_DEBUG, "Invalid freq in scan results (BSSID="
			   MACSTR " m=%d e=%d)",
			   MAC2STR(res->res.bssid), iwe->u.freq.m,
			   iwe->u.freq.e);
		return;
	}

	for (i = 0; i < iwe->u.freq.e; i++)
		divi /= 10;
	res->res.freq = iwe->u.freq.m / divi;
}


static void wext_get_scan_qual(struct iw_event *iwe,
			       struct wext_scan_data *res)
{
	res->res.qual = iwe->u.qual.qual;
	res->res.noise = iwe->u.qual.noise;
	res->res.level = iwe->u.qual.level;
#ifdef ANDROID
	res->res.level = iwe->u.qual.level - 256 + 10;
#endif
}


static void wext_get_scan_encode(struct iw_event *iwe,
				 struct wext_scan_data *res)
{
	if (!(iwe->u.data.flags & IW_ENCODE_DISABLED))
		res->res.caps |= IEEE80211_CAP_PRIVACY;
}


static void wext_get_scan_rate(struct iw_event *iwe,
			       struct wext_scan_data *res, char *pos,
			       char *end)
{
	int maxrate;
	char *custom = pos + IW_EV_LCP_LEN;
	struct iw_param p;
	size_t clen;

	clen = iwe->len;
	if (custom + clen > end)
		return;
	maxrate = 0;
	while (((ssize_t) clen) >= (ssize_t) sizeof(struct iw_param)) {
		/* Note: may be misaligned, make a local, aligned copy */
		os_memcpy(&p, custom, sizeof(struct iw_param));
		if (p.value > maxrate)
			maxrate = p.value;
		clen -= sizeof(struct iw_param);
		custom += sizeof(struct iw_param);
	}

	/* Convert the maxrate from WE-style (b/s units) to
	 * 802.11 rates (500000 b/s units).
	 */
	res->maxrate = maxrate / 500000;
}


static void wext_get_scan_iwevgenie(struct iw_event *iwe,
				    struct wext_scan_data *res, char *custom,
				    char *end)
{
	char *genie, *gpos, *gend;
	u8 *tmp;

	if (iwe->u.data.length == 0)
		return;

	gpos = genie = custom;
	gend = genie + iwe->u.data.length;
	if (gend > end) {
		wpa_printf(MSG_INFO, "IWEVGENIE overflow");
		return;
	}

	tmp = os_realloc(res->ie, res->ie_len + gend - gpos);
	if (tmp == NULL)
		return;
	os_memcpy(tmp + res->ie_len, gpos, gend - gpos);
	res->ie = tmp;
	res->ie_len += gend - gpos;
}


static void wext_get_scan_custom(struct iw_event *iwe,
				 struct wext_scan_data *res, char *custom,
				 char *end)
{
	size_t clen;
	u8 *tmp;

	clen = iwe->u.data.length;
	if (custom + clen > end)
		return;

	if (clen > 7 && os_strncmp(custom, "wpa_ie=", 7) == 0) {
		char *spos;
		int bytes;
		spos = custom + 7;
		bytes = custom + clen - spos;
		if (bytes & 1 || bytes == 0)
			return;
		bytes /= 2;
		tmp = os_realloc(res->ie, res->ie_len + bytes);
		if (tmp == NULL)
			return;
		hexstr2bin(spos, tmp + res->ie_len, bytes);
		res->ie = tmp;
		res->ie_len += bytes;
	} else if (clen > 7 && os_strncmp(custom, "rsn_ie=", 7) == 0) {
		char *spos;
		int bytes;
		spos = custom + 7;
		bytes = custom + clen - spos;
		if (bytes & 1 || bytes == 0)
			return;
		bytes /= 2;
		tmp = os_realloc(res->ie, res->ie_len + bytes);
		if (tmp == NULL)
			return;
		hexstr2bin(spos, tmp + res->ie_len, bytes);
		res->ie = tmp;
		res->ie_len += bytes;
	} else if (clen > 4 && os_strncmp(custom, "tsf=", 4) == 0) {
		char *spos;
		int bytes;
		u8 bin[8];
		spos = custom + 4;
		bytes = custom + clen - spos;
		if (bytes != 16) {
			wpa_printf(MSG_INFO, "Invalid TSF length (%d)", bytes);
			return;
		}
		bytes /= 2;
		hexstr2bin(spos, bin, bytes);
		res->res.tsf += WPA_GET_BE64(bin);
	}
#ifdef CONFIG_WAPI
	else if (clen > 8 && os_strncmp(custom, "wapi_ie=", 8) == 0 ) {
		wpa_msg(NULL, MSG_DEBUG, "[get wapi ie]\n"); /* Dm: */
		char *spos;
		/* u8 bin[8]; */
		int bytes;
		spos = custom + 8;
		bytes = custom + clen - spos;
		if (bytes & 1)
			return;
		bytes /= 2;
		if (bytes > SSID_MAX_WAPI_IE_LEN) {
			wpa_printf(MSG_INFO, "Too long WAPI IE "
					"(%d)", bytes);

			return;
		}

		tmp = os_realloc(res->ie, res->ie_len + bytes);
		if (tmp == NULL)
			return;
		hexstr2bin(spos, tmp + res->ie_len, bytes);
		res->ie = tmp;
		res->ie_len += bytes;
		/*hexstr2bin(spos,bin,bytes);*/
		/*results[ap_num].wapi_ie_len = bytes;*/
		/*wpa_hexdump_ascii(MSG_DEBUG, "wapi ie", (u8 *)bin, bin[1]);*/
	}
#endif

}


static int wext_19_iw_point(struct wpa_driver_wext_data *drv, u16 cmd)
{
	return drv->we_version_compiled > 18 &&
		(cmd == SIOCGIWESSID || cmd == SIOCGIWENCODE ||
		 cmd == IWEVGENIE || cmd == IWEVCUSTOM);
}


static void wpa_driver_wext_add_scan_entry(struct wpa_scan_results *res,
					   struct wext_scan_data *data)
{
	struct wpa_scan_res **tmp;
	struct wpa_scan_res *r;
	size_t extra_len;
	u8 *pos, *end, *ssid_ie = NULL, *rate_ie = NULL;

	/* Figure out whether we need to fake any IEs */
	pos = data->ie;
	end = pos + data->ie_len;
	while (pos && pos + 1 < end) {
		if (pos + 2 + pos[1] > end)
			break;
		if (pos[0] == WLAN_EID_SSID)
			ssid_ie = pos;
		else if (pos[0] == WLAN_EID_SUPP_RATES)
			rate_ie = pos;
		else if (pos[0] == WLAN_EID_EXT_SUPP_RATES)
			rate_ie = pos;
		pos += 2 + pos[1];
	}

	extra_len = 0;
	if (ssid_ie == NULL)
		extra_len += 2 + data->ssid_len;
	if (rate_ie == NULL && data->maxrate)
		extra_len += 3;

	r = os_zalloc(sizeof(*r) + extra_len + data->ie_len);
	if (r == NULL)
		return;
	os_memcpy(r, &data->res, sizeof(*r));
	r->ie_len = extra_len + data->ie_len;
	pos = (u8 *) (r + 1);
	if (ssid_ie == NULL) {
		/*
		 * Generate a fake SSID IE since the driver did not report
		 * a full IE list.
		 */
		*pos++ = WLAN_EID_SSID;
		*pos++ = data->ssid_len;
		os_memcpy(pos, data->ssid, data->ssid_len);
		pos += data->ssid_len;
	}
	if (rate_ie == NULL && data->maxrate) {
		/*
		 * Generate a fake Supported Rates IE since the driver did not
		 * report a full IE list.
		 */
		*pos++ = WLAN_EID_SUPP_RATES;
		*pos++ = 1;
		*pos++ = data->maxrate;
	}
	if (data->ie)
		os_memcpy(pos, data->ie, data->ie_len);

	tmp = os_realloc(res->res,
			 (res->num + 1) * sizeof(struct wpa_scan_res *));
	if (tmp == NULL) {
		os_free(r);
		return;
	}
	tmp[res->num++] = r;
	res->res = tmp;
}


/**
 * wpa_driver_wext_get_scan_results - Fetch the latest scan results
 * @priv: Pointer to private wext data from wpa_driver_wext_init()
 * Returns: Scan results on success, -1 on failure
 */
struct wpa_scan_results * wpa_driver_wext_get_scan_results(void *priv)
{
	struct wpa_driver_wext_data *drv = priv;
	size_t ap_num = 0, len;
	int first;
	u8 *res_buf;
	struct iw_event iwe_buf, *iwe = &iwe_buf;
	char *pos, *end, *custom;
	struct wpa_scan_results *res;
	struct wext_scan_data data;

	res_buf = wpa_driver_wext_giwscan(drv, &len);
	if (res_buf == NULL)
		return NULL;

	ap_num = 0;
	first = 1;

	res = os_zalloc(sizeof(*res));
	if (res == NULL) {
		os_free(res_buf);
		return NULL;
	}

	pos = (char *) res_buf;
	end = (char *) res_buf + len;
	os_memset(&data, 0, sizeof(data));

	while (pos + IW_EV_LCP_LEN <= end) {
		/* Event data may be unaligned, so make a local, aligned copy
		 * before processing. */
		os_memcpy(&iwe_buf, pos, IW_EV_LCP_LEN);
		if (iwe->len <= IW_EV_LCP_LEN)
			break;

		custom = pos + IW_EV_POINT_LEN;
		if (wext_19_iw_point(drv, iwe->cmd)) {
			/* WE-19 removed the pointer from struct iw_point */
			char *dpos = (char *) &iwe_buf.u.data.length;
			int dlen = dpos - (char *) &iwe_buf;
			os_memcpy(dpos, pos + IW_EV_LCP_LEN,
				  sizeof(struct iw_event) - dlen);
		} else {
			os_memcpy(&iwe_buf, pos, sizeof(struct iw_event));
			custom += IW_EV_POINT_OFF;
		}

		switch (iwe->cmd) {
		case SIOCGIWAP:
			if (!first)
				wpa_driver_wext_add_scan_entry(res, &data);
			first = 0;
			os_free(data.ie);
			os_memset(&data, 0, sizeof(data));
			os_memcpy(data.res.bssid,
				  iwe->u.ap_addr.sa_data, ETH_ALEN);
			break;
		case SIOCGIWMODE:
			wext_get_scan_mode(iwe, &data);
			break;
		case SIOCGIWESSID:
			wext_get_scan_ssid(iwe, &data, custom, end);
			break;
		case SIOCGIWFREQ:
			wext_get_scan_freq(iwe, &data);
			break;
		case IWEVQUAL:
			wext_get_scan_qual(iwe, &data);
			break;
		case SIOCGIWENCODE:
			wext_get_scan_encode(iwe, &data);
			break;
		case SIOCGIWRATE:
			wext_get_scan_rate(iwe, &data, pos, end);
			break;
		case IWEVGENIE:
			wext_get_scan_iwevgenie(iwe, &data, custom, end);
			break;
		case IWEVCUSTOM:
			wext_get_scan_custom(iwe, &data, custom, end);
			break;
		}

		pos += iwe->len;
	}
	os_free(res_buf);
	res_buf = NULL;
	if (!first)
		wpa_driver_wext_add_scan_entry(res, &data);
	os_free(data.ie);

	wpa_printf(MSG_DEBUG, "Received %lu bytes of scan results (%lu BSSes)",
		   (unsigned long) len, (unsigned long) res->num);

	return res;
}


static int wpa_driver_wext_get_range(void *priv)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iw_range *range;
	struct iwreq iwr;
	int minlen;
	size_t buflen;

	/*
	 * Use larger buffer than struct iw_range in order to allow the
	 * structure to grow in the future.
	 */
	buflen = sizeof(struct iw_range) + 500;
	range = os_zalloc(buflen);
	if (range == NULL)
		return -1;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.data.pointer = (caddr_t) range;
	iwr.u.data.length = buflen;

	minlen = ((char *) &range->enc_capa) - (char *) range +
		sizeof(range->enc_capa);
#ifdef CONFIG_WAPI
	while (1)
	{
		if (ioctl(drv->ioctl_sock, SIOCGIWRANGE, &iwr) >= 0) {
			break;
		} else {
			sleep(1);
			wpa_printf(MSG_ERROR, "ioctl[SIOCGIWRANGE]");
		}
	}
#endif
	if (ioctl(drv->ioctl_sock, SIOCGIWRANGE, &iwr) < 0) {
		perror("ioctl[SIOCGIWRANGE]");
		os_free(range);
		return -1;
	} else if (iwr.u.data.length >= minlen &&
		   range->we_version_compiled >= 18) {
		wpa_printf(MSG_DEBUG, "SIOCGIWRANGE: WE(compiled)=%d "
			   "WE(source)=%d enc_capa=0x%x",
			   range->we_version_compiled,
			   range->we_version_source,
			   range->enc_capa);
		drv->has_capability = 1;
		drv->we_version_compiled = range->we_version_compiled;
		if (range->enc_capa & IW_ENC_CAPA_WPA) {
			drv->capa.key_mgmt |= WPA_DRIVER_CAPA_KEY_MGMT_WPA |
				WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK;
		}
		if (range->enc_capa & IW_ENC_CAPA_WPA2) {
			drv->capa.key_mgmt |= WPA_DRIVER_CAPA_KEY_MGMT_WPA2 |
				WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK;
		}
		drv->capa.enc |= WPA_DRIVER_CAPA_ENC_WEP40 |
			WPA_DRIVER_CAPA_ENC_WEP104;
		if (range->enc_capa & IW_ENC_CAPA_CIPHER_TKIP)
			drv->capa.enc |= WPA_DRIVER_CAPA_ENC_TKIP;
		if (range->enc_capa & IW_ENC_CAPA_CIPHER_CCMP)
			drv->capa.enc |= WPA_DRIVER_CAPA_ENC_CCMP;
		if (range->enc_capa & IW_ENC_CAPA_4WAY_HANDSHAKE)
			drv->capa.flags |= WPA_DRIVER_FLAGS_4WAY_HANDSHAKE;

		wpa_printf(MSG_DEBUG, "  capabilities: key_mgmt 0x%x enc 0x%x "
			   "flags 0x%x",
			   drv->capa.key_mgmt, drv->capa.enc, drv->capa.flags);
	} else {
		wpa_printf(MSG_DEBUG, "SIOCGIWRANGE: too old (short) data - "
			   "assuming WPA is not supported");
	}

	os_free(range);
	return 0;
}


static int wpa_driver_wext_set_wpa(void *priv, int enabled)
{
	struct wpa_driver_wext_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);

	return wpa_driver_wext_set_auth_param(drv, IW_AUTH_WPA_ENABLED,
					      enabled);
}


static int wpa_driver_wext_set_psk(struct wpa_driver_wext_data *drv,
				   const u8 *psk)
{
	struct iw_encode_ext *ext;
	struct iwreq iwr;
	int ret;

	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);

	if (!(drv->capa.flags & WPA_DRIVER_FLAGS_4WAY_HANDSHAKE))
		return 0;

	if (!psk)
		return 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);

	ext = os_zalloc(sizeof(*ext) + PMK_LEN);
	if (ext == NULL)
		return -1;

	iwr.u.encoding.pointer = (caddr_t) ext;
	iwr.u.encoding.length = sizeof(*ext) + PMK_LEN;
	ext->key_len = PMK_LEN;
	os_memcpy(&ext->key, psk, ext->key_len);
	ext->alg = IW_ENCODE_ALG_PMK;

	ret = ioctl(drv->ioctl_sock, SIOCSIWENCODEEXT, &iwr);
	if (ret < 0)
		perror("ioctl[SIOCSIWENCODEEXT] PMK");
	os_free(ext);

	return ret;
}


static int wpa_driver_wext_set_key_ext(void *priv, wpa_alg alg,
				       const u8 *addr, int key_idx,
				       int set_tx, const u8 *seq,
				       size_t seq_len,
				       const u8 *key, size_t key_len)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iwreq iwr;
	int ret = 0;
	struct iw_encode_ext *ext;


#ifdef CONFIG_WAPI
	if (alg == WPA_ALG_SMS4) {
    	if (seq_len > IW_ENCODE_SEQ_MAX_SIZE * 2) {
        	printf("%s: Invalid seq_len %lu\n",
            		__FUNCTION__, (unsigned long)seq_len);
            return -1;
    	}
    } else
#endif

	if (seq_len > IW_ENCODE_SEQ_MAX_SIZE) {
		wpa_printf(MSG_DEBUG, "%s: Invalid seq_len %lu",
			   __FUNCTION__, (unsigned long) seq_len);
		return -1;
	}

	ext = os_zalloc(sizeof(*ext) + key_len);
	if (ext == NULL)
		return -1;
	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.encoding.flags = key_idx + 1;
	iwr.u.encoding.flags |= IW_ENCODE_TEMP;
	if (alg == WPA_ALG_NONE)
		iwr.u.encoding.flags |= IW_ENCODE_DISABLED;
	iwr.u.encoding.pointer = (caddr_t) ext;
	iwr.u.encoding.length = sizeof(*ext) + key_len;

	if (addr == NULL ||
	    os_memcmp(addr, "\xff\xff\xff\xff\xff\xff", ETH_ALEN) == 0)
		ext->ext_flags |= IW_ENCODE_EXT_GROUP_KEY;
	if (set_tx)
		ext->ext_flags |= IW_ENCODE_EXT_SET_TX_KEY;

	ext->addr.sa_family = ARPHRD_ETHER;
	if (addr)
		os_memcpy(ext->addr.sa_data, addr, ETH_ALEN);
	else
		os_memset(ext->addr.sa_data, 0xff, ETH_ALEN);
	if (key && key_len) {
		os_memcpy(ext + 1, key, key_len);
		ext->key_len = key_len;
	}
	switch (alg) {
	case WPA_ALG_NONE:
		ext->alg = IW_ENCODE_ALG_NONE;
		break;
	case WPA_ALG_WEP:
		ext->alg = IW_ENCODE_ALG_WEP;
		break;
	case WPA_ALG_TKIP:
		ext->alg = IW_ENCODE_ALG_TKIP;
		break;
	case WPA_ALG_CCMP:
		ext->alg = IW_ENCODE_ALG_CCMP;
		break;
	case WPA_ALG_PMK:
		ext->alg = IW_ENCODE_ALG_PMK;
		break;
#ifdef CONFIG_IEEE80211W
	case WPA_ALG_IGTK:
		ext->alg = IW_ENCODE_ALG_AES_CMAC;
		break;
#endif /* CONFIG_IEEE80211W */

#ifdef CONFIG_WAPI
	case WPA_ALG_SMS4:
        ext->alg = IW_ENCODE_ALG_SMS4;
        break;
#endif

	default:
		wpa_printf(MSG_DEBUG, "%s: Unknown algorithm %d",
			   __FUNCTION__, alg);
		os_free(ext);
		return -1;
	}

	if (seq && seq_len) {

#ifdef CONFIG_WAPI
   		if (alg == WPA_ALG_SMS4) {
                	os_memcpy(ext->tx_seq, seq, seq_len);
                	wpa_hexdump(MSG_DEBUG, "seq", seq, seq_len);
            	} else {
#endif
		ext->ext_flags |= IW_ENCODE_EXT_RX_SEQ_VALID;
		os_memcpy(ext->rx_seq, seq, seq_len);

#ifdef CONFIG_WAPI
		}
#endif
	}
	if (ioctl(drv->ioctl_sock, SIOCSIWENCODEEXT, &iwr) < 0) {
		ret = errno == EOPNOTSUPP ? -2 : -1;
		if (errno == ENODEV) {
			/*
			 * ndiswrapper seems to be returning incorrect error
			 * code.. */
			ret = -2;
		}

		perror("ioctl[SIOCSIWENCODEEXT]");
	}

	os_free(ext);
	return ret;
}


/**
 * wpa_driver_wext_set_key - Configure encryption key
 * @priv: Pointer to private wext data from wpa_driver_wext_init()
 * @priv: Private driver interface data
 * @alg: Encryption algorithm (%WPA_ALG_NONE, %WPA_ALG_WEP,
 *	%WPA_ALG_TKIP, %WPA_ALG_CCMP); %WPA_ALG_NONE clears the key.
 * @addr: Address of the peer STA or ff:ff:ff:ff:ff:ff for
 *	broadcast/default keys
 * @key_idx: key index (0..3), usually 0 for unicast keys
 * @set_tx: Configure this key as the default Tx key (only used when
 *	driver does not support separate unicast/individual key
 * @seq: Sequence number/packet number, seq_len octets, the next
 *	packet number to be used for in replay protection; configured
 *	for Rx keys (in most cases, this is only used with broadcast
 *	keys and set to zero for unicast keys)
 * @seq_len: Length of the seq, depends on the algorithm:
 *	TKIP: 6 octets, CCMP: 6 octets
 * @key: Key buffer; TKIP: 16-byte temporal key, 8-byte Tx Mic key,
 *	8-byte Rx Mic Key
 * @key_len: Length of the key buffer in octets (WEP: 5 or 13,
 *	TKIP: 32, CCMP: 16)
 * Returns: 0 on success, -1 on failure
 *
 * This function uses SIOCSIWENCODEEXT by default, but tries to use
 * SIOCSIWENCODE if the extended ioctl fails when configuring a WEP key.
 */
int wpa_driver_wext_set_key(void *priv, wpa_alg alg,
			    const u8 *addr, int key_idx,
			    int set_tx, const u8 *seq, size_t seq_len,
			    const u8 *key, size_t key_len)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iwreq iwr;
	int ret = 0;

	wpa_printf(MSG_DEBUG, "%s: alg=%d key_idx=%d set_tx=%d seq_len=%lu "
		   "key_len=%lu",
		   __FUNCTION__, alg, key_idx, set_tx,
		   (unsigned long) seq_len, (unsigned long) key_len);

	ret = wpa_driver_wext_set_key_ext(drv, alg, addr, key_idx, set_tx,
					  seq, seq_len, key, key_len);
	if (ret == 0)
		return 0;

	if (ret == -2 &&
	    (alg == WPA_ALG_NONE || alg == WPA_ALG_WEP)) {
		wpa_printf(MSG_DEBUG, "Driver did not support "
			   "SIOCSIWENCODEEXT, trying SIOCSIWENCODE");
		ret = 0;
	} else {
		wpa_printf(MSG_DEBUG, "Driver did not support "
			   "SIOCSIWENCODEEXT");
		return ret;
	}

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.encoding.flags = key_idx + 1;
	iwr.u.encoding.flags |= IW_ENCODE_TEMP;
	if (alg == WPA_ALG_NONE)
		iwr.u.encoding.flags |= IW_ENCODE_DISABLED;
	iwr.u.encoding.pointer = (caddr_t) key;
	iwr.u.encoding.length = key_len;

	if (ioctl(drv->ioctl_sock, SIOCSIWENCODE, &iwr) < 0) {
		perror("ioctl[SIOCSIWENCODE]");
		ret = -1;
	}

	if (set_tx && alg != WPA_ALG_NONE) {
		os_memset(&iwr, 0, sizeof(iwr));
		os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
		iwr.u.encoding.flags = key_idx + 1;
		iwr.u.encoding.flags |= IW_ENCODE_TEMP;
		iwr.u.encoding.pointer = (caddr_t) NULL;
		iwr.u.encoding.length = 0;
		if (ioctl(drv->ioctl_sock, SIOCSIWENCODE, &iwr) < 0) {
			perror("ioctl[SIOCSIWENCODE] (set_tx)");
			ret = -1;
		}
	}

	return ret;
}


static int wpa_driver_wext_set_countermeasures(void *priv,
					       int enabled)
{
	struct wpa_driver_wext_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
	return wpa_driver_wext_set_auth_param(drv,
					      IW_AUTH_TKIP_COUNTERMEASURES,
					      enabled);
}


static int wpa_driver_wext_set_drop_unencrypted(void *priv,
						int enabled)
{
	struct wpa_driver_wext_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
	drv->use_crypt = enabled;
	return wpa_driver_wext_set_auth_param(drv, IW_AUTH_DROP_UNENCRYPTED,
					      enabled);
}


static int wpa_driver_wext_mlme(struct wpa_driver_wext_data *drv,
				const u8 *addr, int cmd, int reason_code)
{
	struct iwreq iwr;
	struct iw_mlme mlme;
	int ret = 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	os_memset(&mlme, 0, sizeof(mlme));
	mlme.cmd = cmd;
	mlme.reason_code = reason_code;
	mlme.addr.sa_family = ARPHRD_ETHER;
	os_memcpy(mlme.addr.sa_data, addr, ETH_ALEN);
	iwr.u.data.pointer = (caddr_t) &mlme;
	iwr.u.data.length = sizeof(mlme);

	wpa_printf(MSG_DEBUG, "issue SIOCMLME to the driver=================================\n");

	if (ioctl(drv->ioctl_sock, SIOCSIWMLME, &iwr) < 0) {
		perror("ioctl[SIOCSIWMLME]");
		ret = -1;
	}

	return ret;
}


static int wpa_driver_wext_deauthenticate(void *priv, const u8 *addr,
					  int reason_code)
{
	struct wpa_driver_wext_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
	return wpa_driver_wext_mlme(drv, addr, IW_MLME_DEAUTH, reason_code);
}

#ifdef CONFIG_WAPI
static int wpa_driver_wext_set_gen_ie(void *priv, const u8 *ie,
				      size_t ie_len);
#endif

static int wpa_driver_wext_disassociate(void *priv, const u8 *addr,
					int reason_code)
{
	struct wpa_driver_wext_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s () +", __FUNCTION__);
#ifdef CONFIG_WAPI
        int temp = 0;
#endif

	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
#ifdef CONFIG_WAPI
        /* Clear APP IE */
        wpa_driver_wext_set_gen_ie(drv, (const u8 *)&temp, 0);
#endif
	return wpa_driver_wext_mlme(drv, addr, IW_MLME_DISASSOC,
				    reason_code);
}


static int wpa_driver_wext_set_gen_ie(void *priv, const u8 *ie,
				      size_t ie_len)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iwreq iwr;
	int ret = 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.data.pointer = (caddr_t) ie;
	iwr.u.data.length = ie_len;

	if (ioctl(drv->ioctl_sock, SIOCSIWGENIE, &iwr) < 0) {
		perror("ioctl[SIOCSIWGENIE]");
		ret = -1;
	}

	return ret;
}


int wpa_driver_wext_cipher2wext(int cipher)
{
	switch (cipher) {
	case CIPHER_NONE:
		return IW_AUTH_CIPHER_NONE;
	case CIPHER_WEP40:
		return IW_AUTH_CIPHER_WEP40;
	case CIPHER_TKIP:
		return IW_AUTH_CIPHER_TKIP;
	case CIPHER_CCMP:
		return IW_AUTH_CIPHER_CCMP;
	case CIPHER_WEP104:
		return IW_AUTH_CIPHER_WEP104;
#ifdef CONFIG_WAPI
	case CIPHER_SMS4:
		return IW_AUTH_CIPHER_SMS4;
#endif
	default:
		return 0;
	}
}


int wpa_driver_wext_keymgmt2wext(int keymgmt)
{
	switch (keymgmt) {
	case KEY_MGMT_802_1X:
	case KEY_MGMT_802_1X_NO_WPA:
		return IW_AUTH_KEY_MGMT_802_1X;
	case KEY_MGMT_PSK:
		return IW_AUTH_KEY_MGMT_PSK;
	default:
		return 0;
	}
}


static int
wpa_driver_wext_auth_alg_fallback(struct wpa_driver_wext_data *drv,
				  struct wpa_driver_associate_params *params)
{
	struct iwreq iwr;
	int ret = 0;

	wpa_printf(MSG_DEBUG, "WEXT: Driver did not support "
		   "SIOCSIWAUTH for AUTH_ALG, trying SIOCSIWENCODE");

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	/* Just changing mode, not actual keys */
	iwr.u.encoding.flags = 0;
	iwr.u.encoding.pointer = (caddr_t) NULL;
	iwr.u.encoding.length = 0;

	/*
	 * Note: IW_ENCODE_{OPEN,RESTRICTED} can be interpreted to mean two
	 * different things. Here they are used to indicate Open System vs.
	 * Shared Key authentication algorithm. However, some drivers may use
	 * them to select between open/restricted WEP encrypted (open = allow
	 * both unencrypted and encrypted frames; restricted = only allow
	 * encrypted frames).
	 */

	if (!drv->use_crypt) {
		iwr.u.encoding.flags |= IW_ENCODE_DISABLED;
	} else {
		if (params->auth_alg & AUTH_ALG_OPEN_SYSTEM)
			iwr.u.encoding.flags |= IW_ENCODE_OPEN;
		if (params->auth_alg & AUTH_ALG_SHARED_KEY)
			iwr.u.encoding.flags |= IW_ENCODE_RESTRICTED;
	}

	if (ioctl(drv->ioctl_sock, SIOCSIWENCODE, &iwr) < 0) {
		perror("ioctl[SIOCSIWENCODE]");
		ret = -1;
	}

	return ret;
}


int wpa_driver_wext_associate(void *priv,
			      struct wpa_driver_associate_params *params)
{
	struct wpa_driver_wext_data *drv = priv;
	int ret = 0;
	int allow_unencrypted_eapol;
	int value;

	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);

	do {

		struct ifreq ifr;
		os_memset(&ifr, 0, sizeof(ifr));
		os_strncpy(ifr.ifr_name, drv->ifname, IFNAMSIZ);
		if(ioctl(drv->ioctl_sock, SIOCGIFFLAGS, &ifr)==0 && !(ifr.ifr_flags & IFF_UP)) {
			/* reset the interface in order to received eapol */
			ifr.ifr_flags |= IFF_UP;
			ioctl(drv->ioctl_sock, SIOCSIFFLAGS, &ifr);
		}
		/*
		 * If the driver did not support SIOCSIWAUTH, fallback to
		 * SIOCSIWENCODE here.
		 */
		if (drv->auth_alg_fallback &&
			(ret=wpa_driver_wext_auth_alg_fallback(drv, params)) < 0)
			break;

		if (!params->bssid &&
			(ret=wpa_driver_wext_set_bssid(drv, NULL)) < 0)
			break;

		/* TODO: should consider getting wpa version and cipher/key_mgmt suites
		 * from configuration, not from here, where only the selected suite is
		 * available */
		if ((ret=wpa_driver_wext_set_gen_ie(drv, params->wpa_ie, params->wpa_ie_len))
			< 0)
			break;

		if (params->wpa_ie == NULL || params->wpa_ie_len == 0)
			value = IW_AUTH_WPA_VERSION_DISABLED;
		else if (params->wpa_ie[0] == WLAN_EID_RSN)
			value = IW_AUTH_WPA_VERSION_WPA2;
		else if (RSN_SELECTOR_GET(&params->wpa_ie[2]) == WPA_OUI_TYPE)
			value = IW_AUTH_WPA_VERSION_WPA;
		else
			value = IW_AUTH_WPA_VERSION_DISABLED;

		if ((ret=wpa_driver_wext_set_auth_param(drv,
                                IW_AUTH_WPA_VERSION, value)) < 0)
			break;
#ifdef CONFIG_WAPI
		if (params->pairwise_suite!=CIPHER_SMS4 && params->group_suite!=CIPHER_SMS4)
#endif
		{
			value = wpa_driver_wext_cipher2wext(params->pairwise_suite);
			if ((ret=wpa_driver_wext_set_auth_param(drv,
							   IW_AUTH_CIPHER_PAIRWISE, value)) < 0)
				break;
			value = wpa_driver_wext_cipher2wext(params->group_suite);
			if ((ret=wpa_driver_wext_set_auth_param(drv,
							   IW_AUTH_CIPHER_GROUP, value)) < 0)
				break;
		}
		value = wpa_driver_wext_keymgmt2wext(params->key_mgmt_suite);
		if ((ret=wpa_driver_wext_set_auth_param(drv,
						   IW_AUTH_KEY_MGMT, value)) < 0)
			break;
		value = params->key_mgmt_suite != KEY_MGMT_NONE ||
			params->pairwise_suite != CIPHER_NONE ||
			params->group_suite != CIPHER_NONE ||
			params->wpa_ie_len;
		if ((ret=wpa_driver_wext_set_auth_param(drv,
						   IW_AUTH_PRIVACY_INVOKED, value)) < 0)
			break;

		/* Allow unencrypted EAPOL messages even if pairwise keys are set when
		 * not using WPA. IEEE 802.1X specifies that these frames are not
		 * encrypted, but WPA encrypts them when pairwise keys are in use. */
		if (params->key_mgmt_suite == KEY_MGMT_802_1X ||
			params->key_mgmt_suite == KEY_MGMT_PSK)
			allow_unencrypted_eapol = 0;
		else
			allow_unencrypted_eapol = 1;

		if ((ret=wpa_driver_wext_set_psk(drv, params->psk)) < 0)
			break;
		if ((ret=wpa_driver_wext_set_auth_param(drv,
						   IW_AUTH_RX_UNENCRYPTED_EAPOL,
						   allow_unencrypted_eapol)) < 0)
			break;
#ifdef CONFIG_IEEE80211W
		switch (params->mgmt_frame_protection) {
		case NO_MGMT_FRAME_PROTECTION:
			value = IW_AUTH_MFP_DISABLED;
			break;
		case MGMT_FRAME_PROTECTION_OPTIONAL:
			value = IW_AUTH_MFP_OPTIONAL;
			break;
		case MGMT_FRAME_PROTECTION_REQUIRED:
			value = IW_AUTH_MFP_REQUIRED;
			break;
		};
		if ((ret=wpa_driver_wext_set_auth_param(drv, IW_AUTH_MFP, value)) < 0)
			break;
#endif /* CONFIG_IEEE80211W */
		if (params->freq && (ret=wpa_driver_wext_set_freq(drv, params->freq)) < 0)
			break;
		if ((ret=wpa_driver_wext_set_ssid(drv, params->ssid, params->ssid_len)) < 0)
			break;
		if (params->bssid &&
			(ret=wpa_driver_wext_set_bssid(drv, params->bssid)) < 0)
			break;
	} while (0);

	return ret;
}


static int wpa_driver_wext_set_auth_alg(void *priv, int auth_alg)
{
	struct wpa_driver_wext_data *drv = priv;
	int algs = 0, res;

	if (auth_alg & AUTH_ALG_OPEN_SYSTEM)
		algs |= IW_AUTH_ALG_OPEN_SYSTEM;
	if (auth_alg & AUTH_ALG_SHARED_KEY)
		algs |= IW_AUTH_ALG_SHARED_KEY;
	if (auth_alg & AUTH_ALG_LEAP)
		algs |= IW_AUTH_ALG_LEAP;
	if (algs == 0) {
		/* at least one algorithm should be set */
		algs = IW_AUTH_ALG_OPEN_SYSTEM;
	}

	res = wpa_driver_wext_set_auth_param(drv, IW_AUTH_80211_AUTH_ALG,
					     algs);
	drv->auth_alg_fallback = res == -2;
	return res;
}


/**
 * wpa_driver_wext_set_mode - Set wireless mode (infra/adhoc), SIOCSIWMODE
 * @priv: Pointer to private wext data from wpa_driver_wext_init()
 * @mode: 0 = infra/BSS (associate with an AP), 1 = adhoc/IBSS
 * Returns: 0 on success, -1 on failure
 */
int wpa_driver_wext_set_mode(void *priv, int mode)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iwreq iwr;
	int ret = -1, flags;
	unsigned int new_mode = mode ? IW_MODE_ADHOC : IW_MODE_INFRA;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.mode = new_mode;
	if (ioctl(drv->ioctl_sock, SIOCSIWMODE, &iwr) == 0) {
		ret = 0;
		goto done;
	}

	if (errno != EBUSY) {
		perror("ioctl[SIOCSIWMODE]");
		goto done;
	}

	/* mac80211 doesn't allow mode changes while the device is up, so if
	 * the device isn't in the mode we're about to change to, take device
	 * down, try to set the mode again, and bring it back up.
	 */
	if (ioctl(drv->ioctl_sock, SIOCGIWMODE, &iwr) < 0) {
		perror("ioctl[SIOCGIWMODE]");
		goto done;
	}

	if (iwr.u.mode == new_mode) {
		ret = 0;
		goto done;
	}

	if (wpa_driver_wext_get_ifflags(drv, &flags) == 0) {
		(void) wpa_driver_wext_set_ifflags(drv, flags & ~IFF_UP);

		/* Try to set the mode again while the interface is down */
		iwr.u.mode = new_mode;
		if (ioctl(drv->ioctl_sock, SIOCSIWMODE, &iwr) < 0)
			perror("ioctl[SIOCSIWMODE]");
		else
			ret = 0;

		/* Ignore return value of get_ifflags to ensure that the device
		 * is always up like it was before this function was called.
		 */
		(void) wpa_driver_wext_get_ifflags(drv, &flags);
		(void) wpa_driver_wext_set_ifflags(drv, flags | IFF_UP);
	}

done:
	return ret;
}


static int wpa_driver_wext_pmksa(struct wpa_driver_wext_data *drv,
				 u32 cmd, const u8 *bssid, const u8 *pmkid)
{
	struct iwreq iwr;
	struct iw_pmksa pmksa;
	int ret = 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	os_memset(&pmksa, 0, sizeof(pmksa));
	pmksa.cmd = cmd;
	pmksa.bssid.sa_family = ARPHRD_ETHER;
	if (bssid)
		os_memcpy(pmksa.bssid.sa_data, bssid, ETH_ALEN);
	if (pmkid)
		os_memcpy(pmksa.pmkid, pmkid, IW_PMKID_LEN);
	iwr.u.data.pointer = (caddr_t) &pmksa;
	iwr.u.data.length = sizeof(pmksa);

	if (ioctl(drv->ioctl_sock, SIOCSIWPMKSA, &iwr) < 0) {
		if (errno != EOPNOTSUPP)
			perror("ioctl[SIOCSIWPMKSA]");
		ret = -1;
	}

	return ret;
}


static int wpa_driver_wext_add_pmkid(void *priv, const u8 *bssid,
				     const u8 *pmkid)
{
	struct wpa_driver_wext_data *drv = priv;
	return wpa_driver_wext_pmksa(drv, IW_PMKSA_ADD, bssid, pmkid);
}


static int wpa_driver_wext_remove_pmkid(void *priv, const u8 *bssid,
		 			const u8 *pmkid)
{
	struct wpa_driver_wext_data *drv = priv;
	return wpa_driver_wext_pmksa(drv, IW_PMKSA_REMOVE, bssid, pmkid);
}


static int wpa_driver_wext_flush_pmkid(void *priv)
{
	struct wpa_driver_wext_data *drv = priv;
	return wpa_driver_wext_pmksa(drv, IW_PMKSA_FLUSH, NULL, NULL);
}


int wpa_driver_wext_get_capa(void *priv, struct wpa_driver_capa *capa)
{
	struct wpa_driver_wext_data *drv = priv;
	if (!drv->has_capability)
		return -1;
	os_memcpy(capa, &drv->capa, sizeof(*capa));
	return 0;
}


int wpa_driver_wext_alternative_ifindex(struct wpa_driver_wext_data *drv,
					const char *ifname)
{
	if (ifname == NULL) {
		drv->ifindex2 = -1;
		return 0;
	}

	drv->ifindex2 = if_nametoindex(ifname);
	if (drv->ifindex2 <= 0)
		return -1;

	wpa_printf(MSG_DEBUG, "Added alternative ifindex %d (%s) for "
		   "wireless events", drv->ifindex2, ifname);

	return 0;
}


int wpa_driver_wext_set_operstate(void *priv, int state)
{
	struct wpa_driver_wext_data *drv = priv;

	wpa_printf(MSG_DEBUG, "%s: operstate %d->%d (%s)",
		   __func__, drv->operstate, state, state ? "UP" : "DORMANT");
	drv->operstate = state;
	return wpa_driver_wext_send_oper_ifla(
		drv, -1, state ? IF_OPER_UP : IF_OPER_DORMANT);
}


int wpa_driver_wext_get_version(struct wpa_driver_wext_data *drv)
{
	return drv->we_version_compiled;
}
#ifdef ANDROID

/**
     * driver_cmd - execute driver-specific command
     * @priv: private driver interface data from init()
     * @cmd: command to execute
     * @buf: return buffer
     * @buf_len: buffer length
	 *
     * Returns: 0 for "OK" reply, >0 for reply_len on success,
     * -1 on failure
	 *
	 */
int wpa_driver_wext_driver_cmd(void *priv, char *cmd, char *buf, size_t buf_len)
{
	struct wpa_driver_wext_data *drv = priv;

	if (drv->host_asleep) {
		return 0; /* just return due to system suspend */
	}

	if (os_strcmp(cmd, "RSSI")==0 || os_strcasecmp(cmd, "RSSI-APPROX") == 0) {
		int rssi = 255;
		struct iwreq iwr;
		struct iw_statistics stats;
		os_memset(&iwr, 0, sizeof(iwr));
		os_memset(&stats, 0, sizeof(stats));
		os_strncpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
		iwr.u.data.pointer = (caddr_t) &stats;
		iwr.u.data.length = sizeof(struct iw_statistics);
		iwr.u.data.flags = 1;             /* Clear updated flag */
		if (ioctl(drv->ioctl_sock, SIOCGIWSTATS, &iwr) >= 0) {
			rssi = stats.qual.qual;
		}
		if (rssi == 255)
			rssi = -200;
		else
			rssi += (161 - 256);
		return os_snprintf(buf, buf_len, "SSID rssi %d\n", rssi);
	} else if (os_strcmp(cmd, "LINKSPEED")==0) {
		struct iwreq iwr;
		os_memset(&iwr, 0, sizeof(iwr));
		os_strncpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
		if (ioctl(drv->ioctl_sock, SIOCGIWRATE, &iwr) == 0) {
			unsigned int speed_kbps = iwr.u.param.value / 1000000;
			if ((!iwr.u.param.fixed)) {
				return os_snprintf(buf, buf_len, "LinkSpeed %u\n", speed_kbps);
			}
		}
		return -1;
	} else if (os_strcmp(cmd, "MACADDR")==0) {
		// reply comes back in the form "Macaddr = XX.XX.XX.XX.XX.XX" where XX
		struct ifreq ifr;
		os_memset(&ifr, 0, sizeof(ifr));
		os_strncpy(ifr.ifr_name, drv->ifname, IFNAMSIZ);
		if(ioctl(drv->ioctl_sock, SIOCGIFHWADDR, &ifr)==0) {
			char *mac = ifr.ifr_hwaddr.sa_data;
			return os_snprintf(buf, buf_len, "Macaddr = %02X:%02X:%02X:%02X:%02X:%02X\n",
						mac[0], mac[1], mac[2],
						mac[3], mac[4], mac[5]);
		}
	} else if (os_strcmp(cmd, "SCAN-ACTIVE")==0) {
		return 0; /* unsupport function */
	} else if (os_strcmp(cmd, "SCAN-PASSIVE")==0) {
		return 0; /* unsupport function */
	} else if (os_strcmp(cmd, "START")==0) {
		if (!wpa_driver_atheros_wlan_ctrl(drv, 1)) {
			wpa_msg(drv->ctx, MSG_INFO, WPA_EVENT_DRIVER_STATE "STARTED");
		} else {
			wpa_printf(MSG_DEBUG, "Fail to start WLAN");
		}
		return 0;
	} else if (os_strcmp(cmd, "STOP")==0) {
		if (!wpa_driver_atheros_wlan_ctrl(drv, 0)) {
			wpa_msg(drv->ctx, MSG_INFO, WPA_EVENT_DRIVER_STATE "STOPPED");
		} else {
			wpa_printf(MSG_DEBUG, "Fail to stop WLAN");
		}
		return 0;
	} else if (os_strncmp(cmd, "POWERMODE ", 10)==0) {
		int mode;
		if (sscanf(cmd, "%*s %d", &mode) == 1) {
			return wpa_driver_atheros_pwr_mode(drv, mode);
		}
		return -1;
	} else if (os_strcmp(cmd, "SCAN-CHANNELS")==0) {
		// reply comes back in the form "Scan-Channels = X" where X is the number of channels
		int val = drv->scan_channels;
		return os_snprintf(buf, buf_len, "Scan-Channels = %d\n", val);
	} else if (os_strncmp(cmd, "SCAN-CHANNELS ", 14)==0) {
		int chan;
		if (sscanf(cmd, "%*s %d", &chan) != 1)
			return -1;
		if ((chan == 11) || (chan == 13) || (chan == 14)) {
			if (!wpa_driver_atheros_cfg_chan(drv, chan)) {
				drv->scan_channels = chan;
				return 0;
			}
		}
		return -1;
	} else if (os_strncmp(cmd, "BTCOEXMODE ", 11)==0) {
		int mode;
		if (sscanf(cmd, "%*s %d", &mode)==1) {
			/* 
			 * Android disable BT-COEX when obtaining dhcp packet except there is headset is connected 
			 * It enable the BT-COEX after dhcp process is finished
			 * We ignore since we have our way to do bt-coex during dhcp obtaining.
			 */
			switch (mode) {			
			case 1: /* Disable*/
				break;
			case 0: /* Enable */
				/* fall through */
			case 2: /* Sense*/
				/* fall through */
			default:
				break;
			}
			return 0; /* ignore it */
		}
	} else if (os_strncmp(cmd, "RXFILTER-ADD ", 13)==0) {
		return 0; /* ignore it */
	} else if (os_strncmp(cmd, "RXFILTER-REMOVE ", 16)==0) {
		return 0; /* ignoret it */
	} else if (os_strcmp(cmd, "RXFILTER-START")==0) {
		int flags;
		if (wpa_driver_wext_get_ifflags(drv, &flags) == 0) {	
			return wpa_driver_wext_set_ifflags(drv, flags & ~IFF_MULTICAST);
		}
	} else if (os_strcmp(cmd, "RXFILTER-STOP")==0) {
		int flags;
		if (wpa_driver_wext_get_ifflags(drv, &flags) == 0) {	
			return wpa_driver_wext_set_ifflags(drv, flags | IFF_MULTICAST);
		}
	}

	return -1;
}

#endif // ANDROID

#ifdef CONFIG_WAPI
static int wpa_driver_wext_set_wapi(void * priv, int enabled)
{
    struct wpa_driver_wext_data *drv = priv;
    int ret = 0;
    wpa_printf(MSG_DEBUG, "%s: enabled=%d\n", __func__, enabled);
#define IW_AUTH_WAPI_ENABLED     0x20
    if (enabled) {
        ret = wpa_driver_wext_set_auth_param(drv, IW_AUTH_WAPI_ENABLED, 1);
    } else {
        ret = wpa_driver_wext_set_auth_param(drv, IW_AUTH_WAPI_ENABLED, 0);
    }

    return ret;
}
#endif

const struct wpa_driver_ops wpa_driver_wext_ops = {
	.name = "wext",
	.desc = "Linux wireless extensions (generic)",
	.get_bssid = wpa_driver_wext_get_bssid,
	.get_ssid = wpa_driver_wext_get_ssid,
	.set_wpa = wpa_driver_wext_set_wpa,
	.set_key = wpa_driver_wext_set_key,
	.set_countermeasures = wpa_driver_wext_set_countermeasures,
	.set_drop_unencrypted = wpa_driver_wext_set_drop_unencrypted,
	.scan = wpa_driver_wext_scan,
	.get_scan_results2 = wpa_driver_wext_get_scan_results,
	.deauthenticate = wpa_driver_wext_deauthenticate,
	.disassociate = wpa_driver_wext_disassociate,
	.set_mode = wpa_driver_wext_set_mode,
	.associate = wpa_driver_wext_associate,
	.set_auth_alg = wpa_driver_wext_set_auth_alg,
	.init = wpa_driver_wext_init,
	.deinit = wpa_driver_wext_deinit,
	.add_pmkid = wpa_driver_wext_add_pmkid,
	.remove_pmkid = wpa_driver_wext_remove_pmkid,
	.flush_pmkid = wpa_driver_wext_flush_pmkid,
	.get_capa = wpa_driver_wext_get_capa,
	.set_operstate = wpa_driver_wext_set_operstate,
#ifdef CONFIG_WAPI
	.set_wpa_ie = wpa_driver_wext_set_gen_ie,
	.set_wapi = wpa_driver_wext_set_wapi,
#endif
#ifdef ANDROID
	.driver_cmd = wpa_driver_wext_driver_cmd,
#endif

};
