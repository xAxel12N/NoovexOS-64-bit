/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * rtw89 station mode: role/join/address CAM/security CAM H2C commands, auth/assoc exchange and the wifi_hw entry points.
 * Derived from Linux drivers/net/wireless/realtek/rtw89 (fw.c, fw.h, cam.c, cam.h, core.h; GPL-2.0 OR BSD-3-Clause, used under BSD-3-Clause).
 * Copyright(c) 2019-2022  Realtek Corporation
 * Licence text: drivers/net/wifi/realtek/rtw89/LICENCE.BSD-3-Clause
 */
#ifndef NVX_DRIVERS_NET_WIFI_REALTEK_RTW89_STA_H
#define NVX_DRIVERS_NET_WIFI_REALTEK_RTW89_STA_H
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_core.h"

#define H2C_CL_MAC_SEC_CAM 0xA
#define H2C_FUNC_MAC_SEC_UPD 0x1
#define H2C_CL_MAC_FR_EXCHG 0x5
#define H2C_FUNC_MAC_CCTLINFO_UD 0x2
#define H2C_FUNC_MAC_CCTLINFO_UD_V1 0xA
#define H2C_CL_MAC_ADDR_CAM_UPDATE 0x6
#define H2C_FUNC_MAC_ADDR_CAM_UPD 0x0
#define H2C_CL_MAC_MEDIA_RPT 0x8
#define H2C_FUNC_MAC_JOININFO 0x0
#define H2C_FUNC_MAC_FWROLE_MAINTAIN 0x4
#define RTW89_ADDR_CAM_LEN 60
#define RTW89_SEC_CAM_H2C_LEN 24
#define RTW89_CMC_TBL_LEN 68
#define RTW89_ADDR_CAM_ENT_SIZE 0x40
#define RTW89_BSSID_CAM_ENT_SIZE 0x08
#define RTW89_SEC_CAM_LEN 20
enum { RTW89_NET_TYPE_NO_LINK=0, RTW89_NET_TYPE_INFRA=2 };
enum { RTW89_WIFI_ROLE_STATION=1 };
enum { RTW89_ROLE_CREATE=0, RTW89_ROLE_REMOVE=1, RTW89_ROLE_TYPE_CHANGE=2, RTW89_ROLE_INFO_CHANGE=3, RTW89_ROLE_CON_DISCONN=4 };
enum { RTW89_SEC_KEY_TYPE_NONE=0, RTW89_SEC_KEY_TYPE_CCMP128=6, RTW89_SEC_KEY_TYPE_BIP_CCMP128=10 };
enum { RTW89_ADDR_CAM_SEC_NONE=0, RTW89_ADDR_CAM_SEC_NORMAL=2 };
enum { R89S_IDLE=0, R89S_AUTH, R89S_ASSOC, R89S_UP };

struct rtw89_sta {
    int state; u8 bssid[6], me[6], ch, macid, aid_ok; u16 aid, seq; int auth_st, assoc_st; u32 joins, deauths;
    u8 sec_mode, sec_valid, sec_ent[7], sec_keyid[7]; int hwcrypt; const char* why; int vif; u32 vif_inits;
};
extern struct rtw89_sta g_r89s;
extern int (*rtw89_chan_hook)(struct rtw89_softc* sc,u8 ch);
extern int (*rtw89_tx_hook)(struct rtw89_softc* sc,const u8* frame,u32 len,int mgmt,u8 rate,u8 macid);
u32 rtw89_build_role_maintain(u8* o,u8 macid,u8 upd);
u32 rtw89_build_join_info(u8* o,u8 macid,int disconn,u8 net_type);
u32 rtw89_build_addr_cam(u8* o,const struct rtw89_sta* s,u8 net_type,int valid);
u32 rtw89_build_sec_cam(u8* o,u8 idx,u8 type,const u8* key);
u32 rtw89_build_cmac_default(u8* o,u8 macid);
u32 rtw89_build_auth(u8* f,const struct rtw89_sta* s);
u32 rtw89_build_assoc(u8* f,u32 cap,const struct rtw89_sta* s,const char* ssid,u16 capab);
int rtw89_sta_rx_mgmt(const u8* f,int len);
void rtw89_sta_rx(const u8* frame,int len,int rssi);
int rtw89_sta_join(struct rtw89_softc* sc,const u8* bssid,u8 ch,const char* ssid,u8 akm,u8 ht);
void rtw89_sta_leave(struct rtw89_softc* sc);
int rtw89_sta_tx(struct rtw89_softc* sc,const u8* f,int len);
int rtw89_sta_set_key(struct rtw89_softc* sc,int idx,int type,const u8* key,int len,int pairwise);
void rtw89_sta_poll(struct rtw89_softc* sc);
int rtw89_sta_vif_init(struct rtw89_softc* sc);
#endif
