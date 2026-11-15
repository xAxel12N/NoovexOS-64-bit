/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */
#ifndef RTW8822C_CHIP_H
#define RTW8822C_CHIP_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/net/wifi/rtw_chip.h"

#define RTW8822C_TXFF_SIZE    262144u
#define RTW8822C_FW_RXFF_SIZE 12288u
#define RTW8822C_DMA_EXTRA  0
#define RTW8822C_DMA_LOW    1
#define RTW8822C_DMA_NORMAL 2
#define RTW8822C_DMA_HIGH   3

enum { PWR_WRITE = 1, PWR_POLL = 2, PWR_END = 0 };
typedef struct rtw8822c_pwr_cmd { u16 off; u8 mask; u8 val; u8 cmd; } rtw8822c_pwr_cmd;

extern const rtw_chip rtw8822c_chip;
extern const rtw_chip rtw8822c_chip_c82f;

u32  rtw8822c_rf_read(u32 m, int path, u32 addr, u32 mask);
void rtw8822c_rf_write(u32 m, int path, u32 addr, u32 mask, u32 data);
int  rtw8822c_load_rf(u32 m, int path, const unsigned int* t, int n);
void rtw8822c_dac_cal(u32 m);
int  rtw8822c_do_iqk(u32 m);
void rtw8822c_chan_params(u8 ch, u8 bw, u8* center, u8* pri_idx);
void rtw8822c_set_channel_ex(u32 m, u8 center, u8 bw, u8 pri_idx);
u8   rtw8822c_ch_group(u8 ch, u8 rate);
u8   rtw8822c_txpwr_index(int path, u8 ch, u8 bw, u8 rate);

#endif
