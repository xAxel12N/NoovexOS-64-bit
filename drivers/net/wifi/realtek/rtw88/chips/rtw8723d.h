/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation */
#ifndef RTW8723D_CHIP_H
#define RTW8723D_CHIP_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/net/wifi/rtw_chip.h"

extern const rtw_chip rtw8723d_chip;
extern u32 rtw8723d_lck_fail, rtw8723d_fw_fail, rtw8723d_trx_fail;
int  rtw8723d_trx_init(u32 m);
extern u32 rtw8723d_iqk_runs, rtw8723d_iqk_fail;
extern int rtw8723d_iqk_res[8];
int  rtw8723d_iqk_similar(int res[4][8], int c1, int c2);
int  rtw8723d_fw_download(u32 m, const u8* fw, long sz);
int  rtw8723d_efuse_parse(const u8* map, int len, rtw_efuse_info* o);
u32  rtw8723d_rf_read(u32 m, int path, u32 addr);
void rtw8723d_txpwr_calc(u8 ch, u8* cck, u8* ofdm, u8* ht20);
void rtw8723d_set_tx_power(u32 m, u8 ch);
int rtw8723d_txpwr_limit(int regd, u8 ch, int bw, u8 cc, int rs);
extern u8 rtw8723d_txpwr_used[4];
void rtw8723d_spur_cal(u32 m, u8 ch);
extern u32 rtw8723d_notch_on, rtw8723d_pt_runs, rtw8723d_pt_iqk;
extern int rtw8723d_iqk_done;
extern u8 rtw8723d_pt_avg;
extern int8_t rtw8723d_pt_delta, rtw8723d_pt_rem_ofdm, rtw8723d_pt_rem_cck;
void rtw8723d_pt_init(void);
int  rtw8723d_pt_limit_ofdm(u8 rate);
void rtw8723d_pwr_track(u32 m);
void rtw8723d_iqk_regs(u32 m, int put);
void rtw8723d_coex_init(u32 m);

#endif
