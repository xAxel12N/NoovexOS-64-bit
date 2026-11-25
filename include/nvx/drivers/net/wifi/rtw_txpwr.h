#ifndef NVX_DRIVERS_WIFI_RTW_TXPWR_H
#define NVX_DRIVERS_WIFI_RTW_TXPWR_H
#include "include/nvx/base.h"
#define RTW_TP_NRATE 64
#define RTW_TP_NRS 6
#define RTW_TP_NREGD 13
#define RTW_TP_REGD_ETSI 2
#define RTW_TP_REGD_WW 12
#define RTW_TP_REGD_CN 9
#define RTW_TP_MAXFAIL 3
#define RTW_TP_BW40 1
#define RTW_TP_BW80 2
extern int g_rtw_txpwr_bw; extern u8 g_rtw_txpwr_pri, g_rtw_txpwr_c40;
void rtw_txpwr_bases(const u8* e,u8 cc,int bw,int* cck,int* of,int* ht);
extern int g_rtw_txpwr_tbl, g_rtw_txpwr_dead, g_rtw_txpwr_regd, g_rtw_txpwr_fails, g_rtw_txpwr_used;
extern u8 g_rtw_txpwr_idx[RTW_TP_NRATE];
void rtw_txpwr_tables_init(void);
int rtw_txpwr_limit(int regd,u8 ch,int bw,int rs);
int rtw_txpwr_index(u8 ch,int r,int cck,int ofdm,int b20);
int rtw_txpwr_on(void);
int rtw_txpwr_compute(u8 ch,int cck,int ofdm,int b20);
void rtw_txpwr_write(u32 m,u8 ch);
void rtw_txpwr_kill(const char* why);
void rtw_txpwr_join(int r);
void rtw_txpwr_set_mode(int table);
int rtw_txpwr_set_regd(const char* n);
const char* rtw_txpwr_regd_name(void);
int rtw_txpwr_regd_now(void);
#endif
