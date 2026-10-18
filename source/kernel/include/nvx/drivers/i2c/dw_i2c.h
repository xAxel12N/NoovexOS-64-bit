#ifndef NVX_DRIVERS_I2C_DW_I2C_H
#define NVX_DRIVERS_I2C_DW_I2C_H

#include "include/nvx/base.h"

typedef struct { u64 base; int ok; u32 txd, rxd; int fast; u32 last_abort; u32 xfers, errs; } dwi2c;

extern u16 g_dw_ss[3], g_dw_fs[3];
extern u32 g_dw_clk_mhz, g_dw_hold;
int dw_present(u64 base);
int dw_init(dwi2c* c,u64 base,u32 speed);
int dw_xfer(dwi2c* c,u16 addr,const u8* w,int wn,u8* r,int rn);

#endif
