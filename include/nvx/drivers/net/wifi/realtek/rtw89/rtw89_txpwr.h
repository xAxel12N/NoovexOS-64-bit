/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * rtw89 RTL8852B TX power: by-rate, rate offsets, regulatory limit and RU limit pages, BB power reference; RX DCK at channel switch.
 * Derived from Linux drivers/net/wireless/realtek/rtw89 (phy.c, phy.h, core.h, regd.c, rtw8852b_common.c, rtw8852b_rfk.c, rtw8852b_table.c, reg.h;
 * GPL-2.0 OR BSD-3-Clause, used under BSD-3-Clause).
 * Copyright(c) 2019-2024  Realtek Corporation
 * Licence text: drivers/net/wifi/realtek/rtw89/LICENCE.BSD-3-Clause
 */
#ifndef NVX_DRIVERS_NET_WIFI_REALTEK_RTW89_TXPWR_H
#define NVX_DRIVERS_NET_WIFI_REALTEK_RTW89_TXPWR_H
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_pci.h"

#define RTW89_TXP_WW 0
#define RTW89_TXP_ETSI 1
#define RTW89_TXP_UK 14
#define RTW89_TXP_NREGD 19
#define R_AX_PWR_RATE_CTRL 0xD200
#define R_AX_PWR_RATE_OFST_CTRL 0xD204
#define R_AX_PWR_BY_RATE 0xD2C0
#define R_AX_PWR_LMT 0xD2EC
#define R_AX_PWR_RU_LMT 0xD33C
#define RTW89_TXP_LMT_PAGE 40
#define RTW89_TXP_RU_PAGE 24

struct rtw89_byr { signed char cck[4], ofdm[8], mcs[2][12], hedcm[2][4], offset[5]; };
struct rtw89_txpwr_state { u8 ch, regd, ok; u32 applies; signed char lmt[2][RTW89_TXP_LMT_PAGE], ru[2][RTW89_TXP_RU_PAGE]; struct rtw89_byr byr; u8 rfk_on; const char* rfk_off; };
extern struct rtw89_txpwr_state g_r89tx;
void rtw8852b_byr_load(struct rtw89_byr b[2]);
u8 rtw89_txpwr_regd_of(const char* cc,u8 band);
u8 rtw89_txpwr_regd(u8 band);
signed char rtw8852b_txpwr_limit(u8 regd,u8 band,u8 bw,u8 ntx,u8 rs,u8 bf,u8 ch);
signed char rtw8852b_txpwr_limit_ru(u8 regd,u8 band,u8 ru,u8 ntx,u8 ch);
void rtw8852b_fill_limit(u8 regd,u8 ch,u8 ntx,signed char out[RTW89_TXP_LMT_PAGE]);
void rtw8852b_fill_limit_ru(u8 regd,u8 ch,u8 ntx,signed char out[RTW89_TXP_RU_PAGE]);
u32 rtw8852b_txpwr_ref_val(int ref,u16 dec);
int rtw8852b_set_txpwr(struct rtw89_softc* sc,u8 ch);
int rtw8852b_rx_dck(struct rtw89_softc* sc);
int rtw8852b_rfk_channel(struct rtw89_softc* sc,u8 ch);
#endif
