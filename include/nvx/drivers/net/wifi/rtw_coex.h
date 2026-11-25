#pragma once
#include "include/nvx/base.h"
#define C2H_BT_INFO 0x09
extern int g_rtw_coex_nolps;
void rtw_coex_c2h(const u8* p,int len);
void rtw_coex_bt_scan(int on);
int rtw_coex_active(void);
void rtw_coex_reset(void);
void rtw_coex_tick(u32 m,int conn,int connecting,int scanning,u8 ch);
char* rtw_coex_status(char* p);
