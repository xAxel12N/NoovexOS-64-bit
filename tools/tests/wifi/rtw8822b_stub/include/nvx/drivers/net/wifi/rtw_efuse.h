#ifndef NVX_DRIVERS_WIFI_RTW_EFUSE_H
#define NVX_DRIVERS_WIFI_RTW_EFUSE_H
#include "include/nvx/base.h"
u8 rtw_r8(u32 m,u32 o); u16 rtw_r16(u32 m,u32 o); u32 rtw_r32(u32 m,u32 o);
void rtw_w8(u32 m,u32 o,u8 v); void rtw_w16(u32 m,u32 o,u16 v); void rtw_w32(u32 m,u32 o,u32 v);
void rtw_delay_us(u32 us);
int rtw_apply_cond(unsigned int m,const unsigned int* t,int n,unsigned char sz,const char* lbl,int verify);
#endif
