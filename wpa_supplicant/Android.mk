ifeq ($(WPA_SUPPLICANT_VERSION),VER_0_6_ATHEROS)
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
include $(LOCAL_PATH)/.config

LOCAL_PATH := $(call my-dir)/../..

ifeq ($(TARGET_PRODUCT),$(filter $(TARGET_PRODUCT),GT-I5500))
MY_SUPPLICANT_DIR := wpa_supplicant
else
MY_SUPPLICANT_DIR := wpa_supplicant_ath
endif
PREBUILT_WAPI_LIBS := true

ifdef CONFIG_WAPI
# QSD8k
TARGET_ARCH_V=armv7
# MSM7k
#TARGET_ARCH_V=armv5

ifeq ($(PREBUILT_WAPI_LIBS),true)
include $(CLEAR_VARS)
LOCAL_PREBUILT_LIBS := 	wpa_supplicant_ath/wpa_supplicant/$(TARGET_ARCH_V)/libiwnwai_asue.a \
       		       	wpa_supplicant_ath/wpa_supplicant/$(TARGET_ARCH_V)/libsms4.a \
		       	wpa_supplicant_ath/wpa_supplicant/$(TARGET_ARCH_V)/libecc.a
include $(BUILD_MULTI_PREBUILT)
LOCAL_STATIC_LIBRARIES := libiwnwai_asue libsms4 libecc
else
include $(CLEAR_VARS)
LOCAL_SRC_FILES:= $(MY_SUPPLICANT_DIR)/wpa_supplicant/wapi/ECC2.2-2008/ecc.c \
                  $(MY_SUPPLICANT_DIR)/wpa_supplicant/wapi/ECC2.2-2008/hmac.c
LOCAL_CFLAGS += -DWN_ECC_GCCINT64 -DASUE
LOCAL_MODULE := libecc
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
OBJS_iw = cert.c common.c interface.c wapi.c
LOCAL_SRC_FILES:= $(addprefix $(MY_SUPPLICANT_DIR)/wpa_supplicant/wapi/libiwnwai_asue/,$(OBJS_iw))
LOCAL_MODULE := libiwnwai_asue
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
OBJS_sms = sms4c.c wpi_pcrypt.c
LOCAL_SRC_FILES:=  $(addprefix $(MY_SUPPLICANT_DIR)/wpa_supplicant/wapi/sms4/, $(OBJS_sms))
ifeq ($(TARGET_ARCH), arm)
LOCAL_CFLAGS += -DLE
endif
LOCAL_CFLAGS += -DWN_ECC_GCCINT64 -DASUE
LOCAL_MODULE := libsms4
include $(BUILD_STATIC_LIBRARY)
endif # PREBUILT_WAPI_LIBS
endif # CONFIG_WAPI

WPA_SUPPLICANT := true

include $(CLEAR_VARS)
ifndef CFLAGS
#L_CFLAGS = -MMD -O2 -Wall -g
else
L_CFLAGS :=
endif


ifeq ($(TARGET_ARCH),arm)
L_CFLAGS += -mabi=aapcs-linux
endif 

L_CFLAGS += -DWPA_IGNORE_CONFIG_ERRORS

L_CFLAGS += -Iexternal/$(MY_SUPPLICANT_DIR)/src/
L_CFLAGS += -Iexternal/$(MY_SUPPLICANT_DIR)/src/crypto
L_CFLAGS += -Iexternal/$(MY_SUPPLICANT_DIR)/src/utils
L_CFLAGS += -Iexternal/$(MY_SUPPLICANT_DIR)/src/drivers
L_CFLAGS += -Iexternal/$(MY_SUPPLICANT_DIR)/src/l2_packet
L_CFLAGS += -Iexternal/$(MY_SUPPLICANT_DIR)/src/common
L_CFLAGS += -Iexternal/$(MY_SUPPLICANT_DIR)/src/rsn_supp
L_CFLAGS += -Iexternal/$(MY_SUPPLICANT_DIR)/wpa_supplicant
L_CFLAGS += -Iexternal/openssl/include

ifeq "1.5" "$(PLATFORM_VERSION)"
L_CFLAGS += -DANDROID_CUPCAKE
endif 

ifeq "1.6" "$(PLATFORM_VERSION)"
L_CFLAGS += -DANDROID_DONUT
L_CFLAGS += -Iframeworks/base/cmds/keystore
endif

ifeq ($(PLATFORM_VERSION),$(filter $(PLATFORM_VERSION),2.0 2.1 Eclair 2.1-update1))
L_CFLAGS += -DANDROID_ECLAIR
L_CFLAGS += -Iframeworks/base/cmds/keystore
endif

ifeq ($(PLATFORM_VERSION),$(filter $(PLATFORM_VERSION),2.2 2.2.1))
L_CFLAGS += -DANDROID_ECLAIR -DANDROID_FROYO
L_CFLAGS += -Iframeworks/base/cmds/keystore
endif

ifneq ($(PLATFORM_VERSION),$(filter $(PLATFORM_VERSION),1.6 1.5 2.0 2.1 Eclair 2.1-update1 2.2 2.2.1))
$(error Cannot determinate the android version $(PLATFORM_VERSION))
endif

# To allow non-ASCII characters in SSID
L_CFLAGS += -DWPA_UNICODE_SSID

# OpenSSL is configured without engines on Android
L_CFLAGS += -DOPENSSL_NO_ENGINE

OBJS = wpa_supplicant/config.c
OBJS += src/utils/common.c
OBJS += src/utils/wpa_debug.c
OBJS += src/utils/wpabuf.c
OBJS += src/crypto/md5.c
OBJS += src/crypto/rc4.c
OBJS += src/crypto/md4.c
OBJS += src/crypto/sha1.c
OBJS += src/crypto/des.c
OBJS_p = wpa_supplicant/wpa_passphrase.c
OBJS_p += src/utils/common.c
OBJS_p += src/utils/wpa_debug.c
OBJS_p += src/crypto/md5.c
OBJS_p += src/crypto/md4.c
OBJS_p += src/crypto/sha1.c
OBJS_p += src/crypto/des.c
OBJS_c = wpa_supplicant/wpa_cli.c src/common/wpa_ctrl.c

#-include .config

ifndef CONFIG_OS
ifdef CONFIG_NATIVE_WINDOWS
CONFIG_OS=win32
else
CONFIG_OS=unix
endif
endif

ifdef CONFIG_WAPI
L_CFLAGS += -DCONFIG_WAPI
OBJS += wpa_supplicant/wapi.c
endif



ifeq ($(CONFIG_OS), internal)
L_CFLAGS += -DOS_NO_C_LIB_DEFINES
endif

OBJS += src/utils/os_$(CONFIG_OS).c
OBJS_p += src/utils/os_$(CONFIG_OS).c
OBJS_c += src/utils/os_$(CONFIG_OS).c

ifndef CONFIG_ELOOP
CONFIG_ELOOP=eloop
endif
OBJS += src/utils/$(CONFIG_ELOOP).c


ifdef CONFIG_EAPOL_TEST
L_CFLAGS += -Werror -DEAPOL_TEST
endif

ifndef CONFIG_BACKEND
CONFIG_BACKEND=file
endif

ifeq ($(CONFIG_BACKEND), file)
OBJS += wpa_supplicant/config_file.c
ifndef CONFIG_NO_CONFIG_BLOBS
NEED_BASE64=y
endif
L_CFLAGS += -DCONFIG_BACKEND_FILE
endif

ifeq ($(CONFIG_BACKEND), winreg)
OBJS += wpa_supplicant/config_winreg.c
endif

ifeq ($(CONFIG_BACKEND), none)
OBJS += wpa_supplicant/config_none.c
endif

ifdef CONFIG_NO_CONFIG_WRITE
L_CFLAGS += -DCONFIG_NO_CONFIG_WRITE
endif

ifdef CONFIG_NO_CONFIG_BLOBS
L_CFLAGS += -DCONFIG_NO_CONFIG_BLOBS
endif

ifdef CONFIG_NO_SCAN_PROCESSING
L_CFLAGS += -DCONFIG_NO_SCAN_PROCESSING
endif

ifdef CONFIG_DRIVER_AR6000
L_CFLAGS += -DCONFIG_DRIVER_AR6000
OBJS_d += src/drivers/driver_ar6000.c
CONFIG_WIRELESS_EXTENSION=y
endif

ifdef CONFIG_DRIVER_HOSTAP
L_CFLAGS += -DCONFIG_DRIVER_HOSTAP
OBJS_d += src/drivers/driver_hostap.c
CONFIG_WIRELESS_EXTENSION=y
endif

ifdef CONFIG_DRIVER_WEXT
L_CFLAGS += -DCONFIG_DRIVER_WEXT
CONFIG_WIRELESS_EXTENSION=y
endif

ifdef CONFIG_DRIVER_NL80211
L_CFLAGS += -DCONFIG_DRIVER_NL80211
OBJS_d += src/drivers/driver_nl80211.c
LIBS += libnl
ifdef CONFIG_CLIENT_MLME
OBJS_d += src/drivers/radiotap.c
endif
endif

ifdef CONFIG_DRIVER_PRISM54
L_CFLAGS += -DCONFIG_DRIVER_PRISM54
OBJS_d += src/drivers/driver_prism54.c
CONFIG_WIRELESS_EXTENSION=y
endif

ifdef CONFIG_DRIVER_HERMES
L_CFLAGS += -DCONFIG_DRIVER_HERMES
OBJS_d += src/drivers/driver_hermes.c
CONFIG_WIRELESS_EXTENSION=y
endif

ifdef CONFIG_DRIVER_MADWIFI
L_CFLAGS += -DCONFIG_DRIVER_MADWIFI
OBJS_d += src/drivers/driver_madwifi.c
CONFIG_WIRELESS_EXTENSION=y
endif

ifdef CONFIG_DRIVER_ATMEL
L_CFLAGS += -DCONFIG_DRIVER_ATMEL
OBJS_d += src/drivers/driver_atmel.c
CONFIG_WIRELESS_EXTENSION=y
endif

ifdef CONFIG_DRIVER_NDISWRAPPER
L_CFLAGS += -DCONFIG_DRIVER_NDISWRAPPER
OBJS_d += src/drivers/driver_ndiswrapper.c
CONFIG_WIRELESS_EXTENSION=y
endif

ifdef CONFIG_DRIVER_RALINK
L_CFLAGS += -DCONFIG_DRIVER_RALINK
OBJS_d += src/drivers/driver_ralink.c
endif

ifdef CONFIG_DRIVER_BROADCOM
L_CFLAGS += -DCONFIG_DRIVER_BROADCOM
OBJS_d += src/drivers/driver_broadcom.c
endif

ifdef CONFIG_DRIVER_IPW
L_CFLAGS += -DCONFIG_DRIVER_IPW
OBJS_d += src/drivers/driver_ipw.c
CONFIG_WIRELESS_EXTENSION=y
endif

ifdef CONFIG_DRIVER_BSD
L_CFLAGS += -DCONFIG_DRIVER_BSD
OBJS_d += src/drivers/driver_bsd.c
ifndef CONFIG_L2_PACKET
CONFIG_L2_PACKET=freebsd
endif
endif

ifdef CONFIG_DRIVER_NDIS
L_CFLAGS += -DCONFIG_DRIVER_NDIS
OBJS_d += src/drivers/driver_ndis.c
ifdef CONFIG_NDIS_EVENTS_INTEGRATED
OBJS_d += src/drivers/driver_ndis_.c
endif
ifndef CONFIG_L2_PACKET
CONFIG_L2_PACKET=pcap
endif
CONFIG_WINPCAP=y
ifdef CONFIG_USE_NDISUIO
L_CFLAGS += -DCONFIG_USE_NDISUIO
endif
endif

ifdef CONFIG_DRIVER_WIRED
L_CFLAGS += -DCONFIG_DRIVER_WIRED
OBJS_d += src/drivers/driver_wired.c
endif

ifdef CONFIG_DRIVER_TEST
L_CFLAGS += -DCONFIG_DRIVER_TEST
OBJS_d += src/drivers/driver_test.c
endif

ifdef CONFIG_DRIVER_CUSTOM
L_CFLAGS += -DCONFIG_DRIVER_CUSTOM
endif

ifdef CONFIG_DRIVER_OSX
L_CFLAGS += -DCONFIG_DRIVER_OSX
OBJS_d += src/drivers/driver_osx.c
LDFLAGS += -framework CoreFoundation
LDFLAGS += -F/System/Library/PrivateFrameworks -framework Apple80211
endif

ifdef CONFIG_DRIVER_PS3
L_CFLAGS += -DCONFIG_DRIVER_PS3 -m64
OBJS_d += src/drivers/driver_ps3.c
LDFLAGS += -m64
endif

ifdef CONFIG_DRIVER_IPHONE
L_CFLAGS += -DCONFIG_DRIVER_IPHONE
OBJS_d += src/drivers/driver_iphone.c
OBJS_d += src/drivers/MobileApple80211.c
LIBS += -framework CoreFoundation
endif

ifdef CONFIG_DRIVER_ROBOSWITCH
L_CFLAGS += -DCONFIG_DRIVER_ROBOSWITCH
OBJS_d += src/drivers/driver_roboswitch.c
endif

ifndef CONFIG_L2_PACKET
CONFIG_L2_PACKET=linux
endif

OBJS_l2 += src/l2_packet/l2_packet_$(CONFIG_L2_PACKET).c

ifeq ($(CONFIG_L2_PACKET), pcap)
ifdef CONFIG_WINPCAP
L_CFLAGS += -DCONFIG_WINPCAP
LIBS += libwpcap libpacket
LIBS_w += libwpcap
else
LIBS += libdnet libpcap
endif
endif

ifeq ($(CONFIG_L2_PACKET), winpcap)
LIBS += libwpcap libpacket
LIBS_w += -lwpcap
endif

ifeq ($(CONFIG_L2_PACKET), freebsd)
LIBS += libpcap
endif

ifdef CONFIG_EAP_TLS
# EAP-TLS
ifeq ($(CONFIG_EAP_TLS), dyn)
L_CFLAGS += -DEAP_TLS_DYNAMIC
EAPDYN += src/eap_peer/eap_tls.so
else
L_CFLAGS += -DEAP_TLS
OBJS += src/eap_peer/eap_tls.c
OBJS_h += src/eap_server/eap_tls.c
endif
TLS_FUNCS=y
CONFIG_IEEE8021X_EAPOL=y
endif

ifdef CONFIG_EAP_PEAP
# EAP-PEAP
ifeq ($(CONFIG_EAP_PEAP), dyn)
L_CFLAGS += -DEAP_PEAP_DYNAMIC
EAPDYN += src/eap_peer/eap_peap.so
else
L_CFLAGS += -DEAP_PEAP
OBJS += src/eap_peer/eap_peap.c
OBJS += src/eap_common/eap_peap_common.c
OBJS_h += src/eap_server/eap_peap.c
endif
TLS_FUNCS=y
CONFIG_IEEE8021X_EAPOL=y
endif

ifdef CONFIG_EAP_TTLS
# EAP-TTLS
ifeq ($(CONFIG_EAP_TTLS), dyn)
L_CFLAGS += -DEAP_TTLS_DYNAMIC
EAPDYN += src/eap_peer/eap_ttls.so
else
L_CFLAGS += -DEAP_TTLS
OBJS += src/eap_peer/eap_ttls.c
OBJS_h += src/eap_server/eap_ttls.c
endif
MS_FUNCS=y
TLS_FUNCS=y
CHAP=y
CONFIG_IEEE8021X_EAPOL=y
endif

ifdef CONFIG_EAP_MD5
# EAP-MD5
ifeq ($(CONFIG_EAP_MD5), dyn)
L_CFLAGS += -DEAP_MD5_DYNAMIC
EAPDYN += src/eap_peer/eap_md5.so
else
L_CFLAGS += -DEAP_MD5
OBJS += src/eap_peer/eap_md5.c
OBJS_h += src/eap_server/eap_md5.c
endif
CHAP=y
CONFIG_IEEE8021X_EAPOL=y
endif

# backwards compatibility for old spelling
ifdef CONFIG_MSCHAPV2
ifndef CONFIG_EAP_MSCHAPV2
CONFIG_EAP_MSCHAPV2=y
endif
endif

ifdef CONFIG_EAP_MSCHAPV2
# EAP-MSCHAPv2
ifeq ($(CONFIG_EAP_MSCHAPV2), dyn)
L_CFLAGS += -DEAP_MSCHAPv2_DYNAMIC
EAPDYN += src/eap_peer/eap_mschapv2.so
EAPDYN += src/eap_peer/mschapv2.so
else
L_CFLAGS += -DEAP_MSCHAPv2
OBJS += src/eap_peer/eap_mschapv2.c
OBJS += src/eap_peer/mschapv2.c
OBJS_h += src/eap_server/eap_mschapv2.c
endif
MS_FUNCS=y
CONFIG_IEEE8021X_EAPOL=y
endif

ifdef CONFIG_EAP_GTC
# EAP-GTC
ifeq ($(CONFIG_EAP_GTC), dyn)
L_CFLAGS += -DEAP_GTC_DYNAMIC
EAPDYN += src/eap_peer/eap_gtc.so
else
L_CFLAGS += -DEAP_GTC
OBJS += src/eap_peer/eap_gtc.c
OBJS_h += src/eap_server/eap_gtc.c
endif
CONFIG_IEEE8021X_EAPOL=y
endif

ifdef CONFIG_EAP_OTP
# EAP-OTP
ifeq ($(CONFIG_EAP_OTP), dyn)
L_CFLAGS += -DEAP_OTP_DYNAMIC
EAPDYN += src/eap_peer/eap_otp.so
else
L_CFLAGS += -DEAP_OTP
OBJS += src/eap_peer/eap_otp.c
endif
CONFIG_IEEE8021X_EAPOL=y
endif

ifdef CONFIG_EAP_SIM
# EAP-SIM
ifeq ($(CONFIG_EAP_SIM), dyn)
L_CFLAGS += -DEAP_SIM_DYNAMIC
EAPDYN += src/eap_peer/eap_sim.so
else
L_CFLAGS += -DEAP_SIM
OBJS += src/eap_peer/eap_sim.c
OBJS_h += src/eap_server/eap_sim.c
endif
CONFIG_IEEE8021X_EAPOL=y
CONFIG_EAP_SIM_COMMON=y
endif

ifdef CONFIG_EAP_LEAP
# EAP-LEAP
ifeq ($(CONFIG_EAP_LEAP), dyn)
L_CFLAGS += -DEAP_LEAP_DYNAMIC
EAPDYN += src/eap_peer/eap_leap.so
else
L_CFLAGS += -DEAP_LEAP
OBJS += src/eap_peer/eap_leap.c
endif
MS_FUNCS=y
CONFIG_IEEE8021X_EAPOL=y
endif

ifdef CONFIG_EAP_PSK
# EAP-PSK
ifeq ($(CONFIG_EAP_PSK), dyn)
L_CFLAGS += -DEAP_PSK_DYNAMIC
EAPDYN += src/eap_peer/eap_psk.so
else
L_CFLAGS += -DEAP_PSK
OBJS += src/eap_peer/eap_psk.c src/eap_common/eap_psk_common.c
OBJS_h += src/eap_server/eap_psk.c
endif
CONFIG_IEEE8021X_EAPOL=y
NEED_AES=y
endif

ifdef CONFIG_EAP_AKA
# EAP-AKA
ifeq ($(CONFIG_EAP_AKA), dyn)
L_CFLAGS += -DEAP_AKA_DYNAMIC
EAPDYN += src/eap_peer/eap_aka.so
else
L_CFLAGS += -DEAP_AKA
OBJS += src/eap_peer/eap_aka.c
OBJS_h += src/eap_server/eap_aka.c
endif
CONFIG_IEEE8021X_EAPOL=y
CONFIG_EAP_SIM_COMMON=y
endif

ifdef CONFIG_EAP_AKA_PRIME
# EAP-AKA'
ifeq ($(CONFIG_EAP_AKA_PRIME), dyn)
L_CFLAGS += -DEAP_AKA_PRIME_DYNAMIC
else
L_CFLAGS += -DEAP_AKA_PRIME
endif
NEED_SHA256=y
endif

ifdef CONFIG_EAP_SIM_COMMON
OBJS += src/eap_common/eap_sim_common.c
OBJS_h += src/eap_server/eap_sim_db.c
NEED_AES=y
NEED_FIPS186_2_PRF=y
endif

ifdef CONFIG_EAP_FAST
# EAP-FAST
ifeq ($(CONFIG_EAP_FAST), dyn)
L_CFLAGS += -DEAP_FAST_DYNAMIC
EAPDYN += src/eap_peer/eap_fast.so
EAPDYN += src/eap_common/eap_fast_common.c
else
L_CFLAGS += -DEAP_FAST
OBJS += src/eap_peer/eap_fast.c src/eap_peer/eap_fast_pac.c
OBJS += src/eap_common/eap_fast_common.c
OBJS_h += src/eap_server/eap_fast.c
endif
TLS_FUNCS=y
CONFIG_IEEE8021X_EAPOL=y
NEED_T_PRF=y
endif

ifdef CONFIG_EAP_PAX
# EAP-PAX
ifeq ($(CONFIG_EAP_PAX), dyn)
L_CFLAGS += -DEAP_PAX_DYNAMIC
EAPDYN += src/eap_peer/eap_pax.so
else
L_CFLAGS += -DEAP_PAX
OBJS += src/eap_peer/eap_pax.c src/eap_common/eap_pax_common.c
OBJS_h += src/eap_server/eap_pax.c
endif
CONFIG_IEEE8021X_EAPOL=y
endif

ifdef CONFIG_EAP_SAKE
# EAP-SAKE
ifeq ($(CONFIG_EAP_SAKE), dyn)
L_CFLAGS += -DEAP_SAKE_DYNAMIC
EAPDYN += src/eap_peer/eap_sake.so
else
L_CFLAGS += -DEAP_SAKE
OBJS += src/eap_peer/eap_sake.c src/eap_common/eap_sake_common.c
OBJS_h += src/eap_server/eap_sake.c
endif
CONFIG_IEEE8021X_EAPOL=y
endif

ifdef CONFIG_EAP_GPSK
# EAP-GPSK
ifeq ($(CONFIG_EAP_GPSK), dyn)
L_CFLAGS += -DEAP_GPSK_DYNAMIC
EAPDYN += src/eap_peer/eap_gpsk.so
else
L_CFLAGS += -DEAP_GPSK
OBJS += src/eap_peer/eap_gpsk.c src/eap_common/eap_gpsk_common.c
OBJS_h += src/eap_server/eap_gpsk.c
endif
CONFIG_IEEE8021X_EAPOL=y
ifdef CONFIG_EAP_GPSK_SHA256
CFLAGS += -DEAP_GPSK_SHA256
endif
NEED_SHA256=y
endif

ifdef CONFIG_WPS
# EAP-WSC
L_CFLAGS += -DCONFIG_WPS -DEAP_WSC
OBJS += wpa_supplicant/wps_supplicant.c
OBJS += src/utils/uuid.c
OBJS += src/eap_peer/eap_wsc.c src/eap_common/eap_wsc_common.c
OBJS += src/wps/wps.c
OBJS += src/wps/wps_common.c
OBJS += src/wps/wps_attr_parse.c
OBJS += src/wps/wps_attr_build.c
OBJS += src/wps/wps_attr_process.c
OBJS += src/wps/wps_dev_attr.c
OBJS += src/wps/wps_enrollee.c
OBJS += src/wps/wps_registrar.c
OBJS_h += src/eap_server/eap_wsc.c
CONFIG_IEEE8021X_EAPOL=y
NEED_DH_GROUPS=y
NEED_SHA256=y
NEED_BASE64=y
NEED_CRYPTO=y
NEED_80211_COMMON=y

ifdef CONFIG_WPS_UPNP
L_CFLAGS += -DCONFIG_WPS_UPNP
OBJS += src/wps/wps_upnp.c
OBJS += src/wps/wps_upnp_ssdp.c
OBJS += src/wps/wps_upnp_web.c
OBJS += src/wps/wps_upnp_event.c
OBJS += src/wps/httpread.c
endif

endif

ifdef CONFIG_EAP_IKEV2
# EAP-IKEv2
ifeq ($(CONFIG_EAP_IKEV2), dyn)
L_CFLAGS += -DEAP_IKEV2_DYNAMIC
EAPDYN += src/eap_peer/eap_ikev2.so src/eap_peer/ikev2.c
EAPDYN += src/eap_common/eap_ikev2_common.c src/eap_common/ikev2_common.c
else
L_CFLAGS += -DEAP_IKEV2
OBJS += src/eap_peer/eap_ikev2.c src/eap_peer/ikev2.c
OBJS += src/eap_common/eap_ikev2_common.c src/eap_common/ikev2_common.c
OBJS_h += src/eap_server/eap_ikev2.c
OBJS_h += src/eap_server/ikev2.c
endif
CONFIG_IEEE8021X_EAPOL=y
NEED_DH_GROUPS=y
NEED_DH_GROUPS_ALL=y
endif

ifdef CONFIG_EAP_VENDOR_TEST
ifeq ($(CONFIG_EAP_VENDOR_TEST), dyn)
L_CFLAGS += -DEAP_VENDOR_TEST_DYNAMIC
EAPDYN += src/eap_peer/eap_vendor_test.so
else
L_CFLAGS += -DEAP_VENDOR_TEST
OBJS += src/eap_peer/eap_vendor_test.c
OBJS_h += src/eap_server/eap_vendor_test.c
endif
CONFIG_IEEE8021X_EAPOL=y
endif

ifdef CONFIG_EAP_TNC
# EAP-TNC
L_CFLAGS += -DEAP_TNC
OBJS += src/eap_peer/eap_tnc.c
OBJS += src/eap_peer/tncc.c
NEED_BASE64=y
ifndef CONFIG_NATIVE_WINDOWS
ifndef CONFIG_DRIVER_BSD
LIBS += libdl
endif
endif
endif

ifdef CONFIG_IEEE8021X_EAPOL
# IEEE 802.1X/EAPOL state machines (e.g., for RADIUS authentication)
L_CFLAGS += -DIEEE8021X_EAPOL
OBJS += src/eapol_supp/eapol_supp_sm.c src/eap_peer/eap.c src/eap_common/eap_common.c src/eap_peer/eap_methods.c
ifdef CONFIG_DYNAMIC_EAP_METHODS
L_CFLAGS += -DCONFIG_DYNAMIC_EAP_METHODS
LIBS_w += -ldl -rdynamic
endif
endif

ifdef CONFIG_EAP_SERVER
L_CFLAGS += -DEAP_SERVER
OBJS_h += src/eap_server/eap.c
OBJS_h += src/eap_server/eap_identity.c
OBJS_h += src/eap_server/eap_methods.c
endif

ifdef CONFIG_RADIUS_CLIENT
OBJS_h += src/utils/ip_addr.c
OBJS_h += src/radius/radius.c
OBJS_h += src/radius/radius_client.c
endif

ifdef CONFIG_AUTHENTICATOR
OBJS_h += ../hostapd/eapol_sm.c
OBJS_h += ../hostapd/ieee802_1x.c
endif

ifdef CONFIG_WPA_AUTHENTICATOR
OBJS_h += ../hostapd/wpa.c
OBJS_h += ../hostapd/wpa_auth_ie.c
ifdef CONFIG_IEEE80211R
OBJS_h += ../hostapd/wpa_ft.c
endif
ifdef CONFIG_PEERKEY
OBJS_h += ../hostapd/peerkey.c
endif
endif

ifdef CONFIG_PCSC
# PC/SC interface for smartcards (USIM, GSM SIM)
L_CFLAGS += -DPCSC_FUNCS -I/usr/include/PCSC
OBJS += src/utils/pcsc_funcs.c
# -lpthread may not be needed depending on how pcsc-lite was configured
ifdef CONFIG_NATIVE_WINDOWS
#Once MinGW gets support for WinScard, -lwinscard could be used instead of the
#dynamic symbol loading that is now used in pcsc_funcs.c
#LIBS += -lwinscard
else
LIBS += libpcsclite libpthread
endif
endif

ifdef CONFIG_SIM_SIMULATOR
L_CFLAGS += -DCONFIG_SIM_SIMULATOR
NEED_MILENAGE=y
endif

ifdef CONFIG_USIM_SIMULATOR
L_CFLAGS += -DCONFIG_USIM_SIMULATOR
NEED_MILENAGE=y
endif

ifdef NEED_MILENAGE
OBJS += src/hlr_auc_gw/milenage.c
endif

ifndef CONFIG_TLS
CONFIG_TLS=openssl
endif

ifeq ($(CONFIG_TLS), internal)
ifndef CONFIG_CRYPTO
CONFIG_CRYPTO=internal
endif
endif
ifeq ($(CONFIG_CRYPTO), libtomcrypt)
L_CFLAGS += -DCONFIG_INTERNAL_X509
endif
ifeq ($(CONFIG_CRYPTO), internal)
L_CFLAGS += -DCONFIG_INTERNAL_X509
endif


ifdef TLS_FUNCS
# Shared TLS functions (needed for EAP_TLS, EAP_PEAP, EAP_TTLS, and EAP_FAST)
L_CFLAGS += -DEAP_TLS_FUNCS
OBJS += src/eap_peer/eap_tls_common.c
OBJS_h += src/eap_server/eap_tls_common.c
NEED_TLS_PRF=y
ifeq ($(CONFIG_TLS), openssl)
L_CFLAGS += -DEAP_TLS_OPENSSL
OBJS += src/crypto/tls_openssl.c
LIBS += libssl libcrypto
LIBS_p += -libcrypto
endif
ifeq ($(CONFIG_TLS), gnutls)
OBJS += src/crypto/tls_gnutls.c
LIBS += libgnutls libgcrypt libgpg-error
LIBS_p += libgcrypt
ifdef CONFIG_GNUTLS_EXTRA
L_CFLAGS += -DCONFIG_GNUTLS_EXTRA
LIBS += libgnutls-extra
endif
endif
ifeq ($(CONFIG_TLS), schannel)
OBJS += src/crypto/tls_schannel.c
endif
ifeq ($(CONFIG_TLS), internal)
OBJS += src/crypto/tls_internal.c
OBJS += src/tls/tlsv1_common.c src/tls/tlsv1_record.c
OBJS += src/tls/tlsv1_cred.c src/tls/tlsv1_client.c
OBJS += src/tls/tlsv1_client_write.c src/tls/tlsv1_client_read.c
OBJS += src/tls/asn1.c src/tls/rsa.c src/tls/x509v3.c
OBJS_p += src/tls/asn1.c src/tls/rsa.c
OBJS_p += src/crypto/rc4.c src/crypto/aes_wrap.c src/crypto/aes.c
NEED_BASE64=y
NEED_TLS_PRF=y
L_CFLAGS += -DCONFIG_TLS_INTERNAL
L_CFLAGS += -DCONFIG_TLS_INTERNAL_CLIENT
ifeq ($(CONFIG_CRYPTO), internal)
endif
ifeq ($(CONFIG_CRYPTO), libtomcrypt)
LIBS += libtomcrypt libtfm
LIBS_p += -ltomcrypt -ltfm
endif
endif
ifeq ($(CONFIG_TLS), none)
OBJS += src/crypto/tls_none.c
L_CFLAGS += -DEAP_TLS_NONE
CONFIG_INTERNAL_AES=y
CONFIG_INTERNAL_SHA1=y
CONFIG_INTERNAL_MD5=y
CONFIG_INTERNAL_SHA256=y
endif
ifdef CONFIG_SMARTCARD
ifndef CONFIG_NATIVE_WINDOWS
ifneq ($(CONFIG_L2_PACKET), freebsd)
LIBS += libdl
endif
endif
endif
NEED_CRYPTO=y
else
OBJS += src/crypto/tls_none.c
endif

ifdef CONFIG_PKCS12
L_CFLAGS += -DPKCS12_FUNCS
endif

ifdef CONFIG_SMARTCARD
L_CFLAGS += -DCONFIG_SMARTCARD
endif

ifdef MS_FUNCS
OBJS += src/crypto/ms_funcs.c
NEED_CRYPTO=y
endif

ifdef CHAP
OBJS += src/eap_common/chap.c
endif

ifdef NEED_CRYPTO
ifndef TLS_FUNCS
ifeq ($(CONFIG_TLS), openssl)
LIBS += libcrypto
LIBS_p += libcrypto
endif
ifeq ($(CONFIG_TLS), gnutls)
LIBS += libgcrypt
LIBS_p += libgcrypt
endif
ifeq ($(CONFIG_TLS), schannel)
endif
ifeq ($(CONFIG_TLS), internal)
ifeq ($(CONFIG_CRYPTO), libtomcrypt)
LIBS += libtomcrypt libtfm
LIBS_p += libtomcrypt libtfm
endif
endif
endif
ifeq ($(CONFIG_TLS), openssl)
OBJS += src/crypto/crypto_openssl.c
OBJS_p += src/crypto/crypto_openssl.c
CONFIG_INTERNAL_SHA256=y
endif
ifeq ($(CONFIG_TLS), gnutls)
OBJS += src/crypto/crypto_gnutls.c
OBJS_p += src/crypto/crypto_gnutls.c
CONFIG_INTERNAL_SHA256=y
endif
ifeq ($(CONFIG_TLS), schannel)
OBJS += src/crypto/crypto_cryptoapi.c
OBJS_p += src/crypto/crypto_cryptoapi.c
CONFIG_INTERNAL_SHA256=y
endif
ifeq ($(CONFIG_TLS), internal)
ifeq ($(CONFIG_CRYPTO), libtomcrypt)
OBJS += src/crypto/crypto_libtomcrypt.c
OBJS_p += src/crypto/crypto_libtomcrypt.c
CONFIG_INTERNAL_SHA256=y
endif
ifeq ($(CONFIG_CRYPTO), internal)
OBJS += src/crypto/crypto_internal.c src/tls/bignum.c
OBJS_p += src/crypto/crypto_internal.c src/tls/bignum.c
L_CFLAGS += -DCONFIG_CRYPTO_INTERNAL
ifdef CONFIG_INTERNAL_LIBTOMMATH
L_CFLAGS += -DCONFIG_INTERNAL_LIBTOMMATH
ifdef CONFIG_INTERNAL_LIBTOMMATH_FAST
L_CFLAGS += -DLTM_FAST
endif
else
LIBS += libtommath
LIBS_p += libtommath
endif
CONFIG_INTERNAL_AES=y
CONFIG_INTERNAL_DES=y
CONFIG_INTERNAL_SHA1=y
CONFIG_INTERNAL_MD4=y
CONFIG_INTERNAL_MD5=y
CONFIG_INTERNAL_SHA256=y
endif
ifeq ($(CONFIG_CRYPTO), cryptoapi)
OBJS += src/crypto/crypto_cryptoapi.c
OBJS_p += src/crypto/crypto_cryptoapi.c
L_CFLAGS += -DCONFIG_CRYPTO_CRYPTOAPI
CONFIG_INTERNAL_SHA256=y
endif
endif
ifeq ($(CONFIG_TLS), none)
OBJS += src/crypto/crypto_none.c
OBJS_p += src/crypto/crypto_none.c
CONFIG_INTERNAL_SHA256=y
endif
else
CONFIG_INTERNAL_AES=y
CONFIG_INTERNAL_SHA1=y
CONFIG_INTERNAL_MD5=y
endif

ifdef CONFIG_INTERNAL_AES
L_CFLAGS += -DINTERNAL_AES
endif
ifdef CONFIG_INTERNAL_SHA1
L_CFLAGS += -DINTERNAL_SHA1
endif
ifdef CONFIG_INTERNAL_SHA256
L_CFLAGS += -DINTERNAL_SHA256
endif
ifdef CONFIG_INTERNAL_MD5
L_CFLAGS += -DINTERNAL_MD5
endif
ifdef CONFIG_INTERNAL_MD4
L_CFLAGS += -DINTERNAL_MD4
endif
ifdef CONFIG_INTERNAL_DES
L_CFLAGS += -DINTERNAL_DES
endif

ifdef CONFIG_IEEE80211R
NEED_SHA256=y
endif

ifdef CONFIG_IEEE80211W
L_CFLAGS += -DCONFIG_IEEE80211W
NEED_SHA256=y
endif

ifdef NEED_SHA256
OBJS += src/crypto/sha256.c
L_CFLAGS += -DNEED_SHA256
endif

ifdef CONFIG_WIRELESS_EXTENSION
L_CFLAGS += -DCONFIG_WIRELESS_EXTENSION
OBJS_d += src/drivers/driver_wext.c
endif

ifdef CONFIG_CTRL_IFACE
ifeq ($(CONFIG_CTRL_IFACE), y)
ifdef CONFIG_NATIVE_WINDOWS
CONFIG_CTRL_IFACE=named_pipe
else
CONFIG_CTRL_IFACE=unix
endif
endif
L_CFLAGS += -DCONFIG_CTRL_IFACE
ifeq ($(CONFIG_CTRL_IFACE), unix)
L_CFLAGS += -DCONFIG_CTRL_IFACE_UNIX
endif
ifeq ($(CONFIG_CTRL_IFACE), udp)
L_CFLAGS += -DCONFIG_CTRL_IFACE_UDP
endif
ifeq ($(CONFIG_CTRL_IFACE), named_pipe)
L_CFLAGS += -DCONFIG_CTRL_IFACE_NAMED_PIPE
endif
OBJS += wpa_supplicant/ctrl_iface.c wpa_supplicant/ctrl_iface_$(CONFIG_CTRL_IFACE).c
endif

ifdef CONFIG_CTRL_IFACE_DBUS
L_CFLAGS += -DCONFIG_CTRL_IFACE_DBUS -DDBUS_API_SUBJECT_TO_CHANGE
OBJS += wpa_supplicant/ctrl_iface_dbus.c wpa_supplicant/ctrl_iface_dbus_handlers.c wpa_supplicant/dbus_dict_helpers.c
ifndef DBUS_LIBS
DBUS_LIBS := $(shell pkg-config --libs dbus-1)
endif
LIBS += $(DBUS_LIBS)
ifndef DBUS_INCLUDE
DBUS_INCLUDE := $(shell pkg-config --cflags dbus-1)
endif
dbus_version=$(subst ., ,$(shell pkg-config --modversion dbus-1))
DBUS_VERSION_MAJOR=$(word 1,$(dbus_version))
DBUS_VERSION_MINOR=$(word 2,$(dbus_version))
ifeq ($(DBUS_VERSION_MAJOR),)
DBUS_VERSION_MAJOR=0
endif
ifeq ($(DBUS_VERSION_MINOR),)
DBUS_VERSION_MINOR=0
endif
DBUS_INCLUDE += -DDBUS_VERSION_MAJOR=$(DBUS_VERSION_MAJOR)
DBUS_INCLUDE += -DDBUS_VERSION_MINOR=$(DBUS_VERSION_MINOR)
L_CFLAGS += $(DBUS_INCLUDE)
endif

ifdef CONFIG_READLINE
L_CFLAGS += -DCONFIG_READLINE
LIBS_c += -lncurses -lreadline
endif

ifdef CONFIG_NATIVE_WINDOWS
L_CFLAGS += -DCONFIG_NATIVE_WINDOWS
LIBS += libws2_32 libgdi32 libcrypt32
LIBS_c += -lws2_32
LIBS_p += -lws2_32 -lgdi32
ifeq ($(CONFIG_CRYPTO), cryptoapi)
LIBS_p += -lcrypt32
endif
endif

ifdef CONFIG_NO_STDOUT_DEBUG
L_CFLAGS += -DCONFIG_NO_STDOUT_DEBUG
ifndef CONFIG_CTRL_IFACE
L_CFLAGS += -DCONFIG_NO_WPA_MSG
endif
endif

ifdef CONFIG_IPV6
# for eapol_test only
L_CFLAGS += -DCONFIG_IPV6
endif

ifdef CONFIG_PEERKEY
L_CFLAGS += -DCONFIG_PEERKEY
endif

ifdef CONFIG_IEEE80211R
L_CFLAGS += -DCONFIG_IEEE80211R
OBJS += src/rsn_supp/wpa_ft.c
endif

ifndef CONFIG_NO_WPA
OBJS += src/rsn_supp/wpa.c
OBJS += src/rsn_supp/preauth.c
OBJS += src/rsn_supp/pmksa_cache.c
OBJS += src/rsn_supp/peerkey.c
OBJS += src/rsn_supp/wpa_ie.c
OBJS += src/common/wpa_common.c
NEED_AES=y
else
L_CFLAGS += -DCONFIG_NO_WPA -DCONFIG_NO_WPA2
endif

ifdef CONFIG_NO_WPA2
L_CFLAGS += -DCONFIG_NO_WPA2
endif

ifdef CONFIG_NO_WPA_PASSPHRASE
L_CFLAGS += -DCONFIG_NO_PBKDF2
endif

ifdef CONFIG_NO_AES_EXTRAS
L_CFLAGS += -DCONFIG_NO_AES_WRAP
L_CFLAGS += -DCONFIG_NO_AES_CTR -DCONFIG_NO_AES_OMAC1
L_CFLAGS += -DCONFIG_NO_AES_EAX -DCONFIG_NO_AES_CBC
L_CFLAGS += -DCONFIG_NO_AES_ENCRYPT
L_CFLAGS += -DCONFIG_NO_AES_ENCRYPT_BLOCK
endif

ifdef NEED_AES
OBJS += src/crypto/aes_wrap.c src/crypto/aes.c
endif

ifdef NEED_DH_GROUPS
OBJS += src/crypto/dh_groups.c
ifdef NEED_DH_GROUPS_ALL
L_CFLAGS += -DALL_DH_GROUPS
endif
endif

ifndef NEED_FIPS186_2_PRF
L_CFLAGS += -DCONFIG_NO_FIPS186_2_PRF
endif

ifndef NEED_T_PRF
L_CFLAGS += -DCONFIG_NO_T_PRF
endif

ifndef NEED_TLS_PRF
L_CFLAGS += -DCONFIG_NO_TLS_PRF
endif

ifdef NEED_BASE64
OBJS += src/utils/base64.c
endif

ifdef CONFIG_CLIENT_MLME
OBJS += wpa_supplicant/mlme.c src/common/ieee802_11_common.c
L_CFLAGS += -DCONFIG_CLIENT_MLME
endif

ifndef CONFIG_MAIN
CONFIG_MAIN=main
endif

ifdef CONFIG_DEBUG_FILE
L_CFLAGS += -DCONFIG_DEBUG_FILE
endif

ifdef CONFIG_DELAYED_MIC_ERROR_REPORT
L_CFLAGS += -DCONFIG_DELAYED_MIC_ERROR_REPORT
endif

OBJS += src/drivers/scan_helpers.c

OBJS_wpa_rm := wpa_supplicant/ctrl_iface.c wpa_supplicant/mlme.c wpa_supplicant/ctrl_iface_unix.c
OBJS_wpa := $(filter-out $(OBJS_wpa_rm),$(OBJS)) $(OBJS_h) tests/test_wpa.c
ifdef CONFIG_AUTHENTICATOR
OBJS_wpa += tests/link_test.c
endif
OBJS_wpa += $(OBJS_l2)
OBJS += wpa_supplicant/wpa_supplicant.c wpa_supplicant/events.c wpa_supplicant/blacklist.c wpa_supplicant/wpas_glue.c wpa_supplicant/scan.c
OBJS_t := $(OBJS) $(OBJS_l2) wpa_supplicant/eapol_test.c src/radius/radius.c src/radius/radius_client.c
OBJS_t += src/utils/ip_addr.c
OBJS_t2 := $(OBJS) $(OBJS_l2) wpa_supplicant/preauth_test.c
OBJS += wpa_supplicant/$(CONFIG_MAIN).c

ifdef CONFIG_PRIVSEP
OBJS_priv += $(OBJS_d) src/drivers/drivers.c src/drivers/scan_helpers.c
OBJS_priv += $(OBJS_l2)
OBJS_priv += src/utils/os_$(CONFIG_OS).c
OBJS_priv += src/utils/$(CONFIG_ELOOP).c
OBJS_priv += src/utils/common.c
OBJS_priv += src/utils/wpa_debug.c
OBJS_priv += src/utils/wpabuf.c
OBJS_priv += wpa_supplicant/wpa_priv.c
ifdef CONFIG_DRIVER_TEST
OBJS_priv += src/crypto/sha1.c
OBJS_priv += src/crypto/md5.c
ifeq ($(CONFIG_TLS), openssl)
OBJS_priv += src/crypto/crypto_openssl.c
endif
ifeq ($(CONFIG_TLS), gnutls)
OBJS_priv += src/crypto/crypto_gnutls.c
endif
ifeq ($(CONFIG_TLS), internal)
ifeq ($(CONFIG_CRYPTO), libtomcrypt)
OBJS_priv += src/crypto/crypto_libtomcrypt.c
else
OBJS_priv += src/crypto/crypto_internal.c
endif
endif
endif # CONFIG_DRIVER_TEST
OBJS += src/l2_packet/l2_packet_privsep.c
OBJS += src/drivers/driver_privsep.c
EXTRA_progs += wpa_priv
else
OBJS += $(OBJS_d) src/drivers/drivers.c
OBJS += $(OBJS_l2)
endif

ifdef CONFIG_NDIS_EVENTS_INTEGRATED
L_CFLAGS += -DCONFIG_NDIS_EVENTS_INTEGRATED
OBJS += src/drivers/ndis_events.c
EXTRALIBS += -loleaut32 -lole32 -luuid
ifdef PLATFORMSDKLIB
EXTRALIBS += $(PLATFORMSDKLIB)/WbemUuid.Lib
else
EXTRALIBS += WbemUuid.Lib
endif
endif

########################

include $(CLEAR_VARS)
LOCAL_MODULE := wpa_cli
LOCAL_MODULE_TAGS := debug
LOCAL_SHARED_LIBRARIES := libc
LOCAL_SHARED_LIBRARIES += libcutils
LOCAL_CFLAGS := $(L_CFLAGS)
LOCAL_SRC_FILES := $(addprefix $(MY_SUPPLICANT_DIR)/,$(OBJS_c))
LOCAL_C_INCLUDES := $(INCLUDES)
include $(BUILD_EXECUTABLE)

####################################
#WPA SUPPLICANT FOR ANDROID#
include $(CLEAR_VARS)
LOCAL_MODULE := wpa_supplicant
#ifdef CONFIG_DRIVER_CUSTOM
#LOCAL_STATIC_LIBRARIES := libCustomWifi libWifiApi
#endif
ifdef CONFIG_WAPI
#LOCAL_STATIC_LIBRARIES := libiwnwai_asue libsms4 libecc
LOCAL_STATIC_LIBRARIES := libiwnwai_asue
LOCAL_STATIC_LIBRARIES += libsms4
LOCAL_STATIC_LIBRARIES += libecc
 
endif
LOCAL_SHARED_LIBRARIES := $(LIBS) #libc libcutils libcrypto 
LOCAL_SHARED_LIBRARIES += libcutils
LOCAL_CFLAGS := $(L_CFLAGS)
LOCAL_SRC_FILES := $(addprefix $(MY_SUPPLICANT_DIR)/,$(OBJS))
LOCAL_C_INCLUDES := $(INCLUDES)
include $(BUILD_EXECUTABLE)

####################################

include $(CLEAR_VARS)
LOCAL_MODULE = libwpa_client
LOCAL_CFLAGS = $(L_CFLAGS)
LOCAL_SRC_FILES = $(MY_SUPPLICANT_DIR)/src/common/wpa_ctrl.c $(MY_SUPPLICANT_DIR)/src/utils/os_unix.c
LOCAL_C_INCLUDES = $(INCLUDES)
LOCAL_SHARED_LIBRARIES := libcutils
LOCAL_COPY_HEADERS_TO := libwpa_client
LOCAL_COPY_HEADERS := $(MY_SUPPLICANT_DIR)/src/common/wpa_ctrl.h
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := wpa_supplicant.conf
LOCAL_MODULE_TAGS := user
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT)/etc/wifi
LOCAL_SRC_FILES := $(MY_SUPPLICANT_DIR)/wpa_supplicant/android.conf
include $(BUILD_PREBUILT)

L_CFLAGS :=
LIBS :=
OBJS_p :=

endif # VER_0_6_ATHEROS
