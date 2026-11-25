/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * rtw89 RTL8852B/RTL8852C channel switch (MAC, BB and RF parts of set_channel, 20 MHz).
 * Derived from Linux drivers/net/wireless/realtek/rtw89 (rtw8852b.c, rtw8852b_common.c, rtw8852b_rfk.c, rtw8852c.c, rtw8852c_rfk.c, phy.c, reg.h;
 * GPL-2.0 OR BSD-3-Clause, used under BSD-3-Clause).
 * Copyright(c) 2019-2024  Realtek Corporation
 * Licence text: drivers/net/wifi/realtek/rtw89/LICENCE.BSD-3-Clause
 */
#ifndef NVX_DRIVERS_NET_WIFI_REALTEK_RTW89_CHAN_H
#define NVX_DRIVERS_NET_WIFI_REALTEK_RTW89_CHAN_H
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_pci.h"

#define RTW89_INV_RF_DATA 0xFFFFFFFFu
#define R_SWSI_READ_ADDR_V1 0x0378
#define B_SWSI_R_BUSY_V1 (1u<<25)
#define B_SWSI_R_DATA_DONE_V1 (1u<<26)
#define RR_RSV1 0x05
#define RR_CFGCH 0x18
#define RR_APK 0x19
#define RR_RXBB2 0x8f
#define RR_CFGCH_V1 0x10018
#define RR_LUTWA 0x33
#define RR_LUTWD0 0x3f
#define RR_POW 0xa0
#define RR_SX 0xaf
#define RR_LDO 0xb1
#define RR_LPF 0xb7
#define RR_SYNFB 0xc5
#define RR_LCKST 0xcf
#define RR_LCK_TRG 0xd3
#define RR_MMD 0xd5
#define RR_SYNLUT 0xdd
#define RR_LUTWE2 0xee

struct rtw89_chan_state { u8 ch, band, ok; u32 switches, lck_timeouts, lck_fix, rf_err; };
extern struct rtw89_chan_state g_r89ch;
u32 rtw89_read_rf(struct rtw89_softc* sc,int path,u32 addr,u32 mask);
u8 rtw89_encode_chan_idx(u8 ch);
u8 rtw8852b_sco_mapping(u8 ch);
int rtw8852b_set_channel(struct rtw89_softc* sc,u8 ch);
int rtw8852c_set_channel(struct rtw89_softc* sc,u8 ch);
void rtw8852c_bb_sethw(struct rtw89_softc* sc);
u16 rtw89_chan_freq(u8 ch);
u32 rtw8852c_spur_freq(u8 ch);
int rtw8852c_csi_tone(u8 ch);
void rtw8852c_nbi_tone(u8 ch,int* tone,int* frac);
int rtw89_set_channel(struct rtw89_softc* sc,u8 ch);
#endif
