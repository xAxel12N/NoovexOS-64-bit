#pragma once
#include "include/nvx/base.h"
#define H2C_MEDIA_STATUS 0x01u
#define H2C_RA_INFO 0x40u
#define C2H_RA_RPT 0x0C
#define RTW_C2H_RA_RPT_SIZE 7
#define RTW_RA_MACID 0
#define RTW_RA_RPT_STALE 500u
enum { RTW_RATEID_BGN_40M_2SS, RTW_RATEID_BGN_40M_1SS, RTW_RATEID_BGN_20M_2SS, RTW_RATEID_BGN_20M_1SS, RTW_RATEID_GN_N2SS, RTW_RATEID_GN_N1SS, RTW_RATEID_BG, RTW_RATEID_G, RTW_RATEID_B_20M };
typedef struct { u8 rate, sgi, bw, ratio, seq; u32 rate100k, n, c2h_n; u64 t; } rtw_ra_rpt;
extern volatile u64 g_ticks;
extern int g_wifi_ra, g_wifi_band5, g_rtw_ra_dead, g_rtw_ra_fail;
extern rtw_ra_rpt g_rtw_ra_rpt;
int rtw_h2c_cmd(u32 m,u32 w0,u32 w1);
void rtw_ra_peer_ies(const u8* ies,int len);
void rtw_ra_ies_restore(void);
int rtw_ra_build(int band5,int ht,const u8* rates,int nr,const u8* ies,int il,u32* mask_out,u8* sgi_out);
int rtw_ra_fw_setup(u32 m,int ht,const u8* rates,int nr,u32* mask_out);
u32 rtw_ra_desc_rate100k(u8 rate,int sgi,int bw);
void rtw_c2h_rx(const u8* c,int len);
int rtw_ra_fw_mbps(void);
char* rtw_ra_rate_name(char* p,u8 rate);
char* rtw_ra_status(char* p);
