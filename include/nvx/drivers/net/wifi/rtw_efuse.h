#ifndef NVX_DRIVERS_WIFI_RTW_EFUSE_H
#define NVX_DRIVERS_WIFI_RTW_EFUSE_H

#include "include/nvx/base.h"

#define RTW_EFUSE_PHY_SZ   1024
#define RTW_EFUSE_LOG_SZ   768
#define RTW_EFUSE_PROT_SZ  96
typedef struct { u32 off; u8 sz; u32 val; } rtw_regw;
typedef struct { unsigned int off; unsigned char sz; unsigned int val; } rtw_bent;

extern u8 rtw_phymap[RTW_EFUSE_PHY_SZ];
extern u8 rtw_logmap[RTW_EFUSE_LOG_SZ];
extern int rtw_efuse_valid;
extern rtw_regw rtw_mactbl[512];
extern const rtw_bent rtw_bmac[138];
extern const rtw_bent rtw_bbb[1669];
extern const rtw_bent rtw_bagc[386];
extern const rtw_bent rtw_brf[586];
extern const unsigned int rtw_cmac[], rtw_cbb[], rtw_cagc[], rtw_cagc_btg2[], rtw_crf[];
extern const int rtw_cmac_n, rtw_cbb_n, rtw_cagc_n, rtw_cagc_btg2_n, rtw_crf_n;
extern int g_rtw_cut, g_rtw_pkg, g_rtw_rfe, g_rtw_cond_set;
void rtw_cond_setup(unsigned int m);
int rtw_apply_cond(unsigned int m,const unsigned int* t,int n,unsigned char sz,const char* lbl,int verify);

u8 rtw_r8 (u32 m,u32 o);
u16 rtw_r16(u32 m,u32 o);
u32 rtw_r32(u32 m,u32 o);
void rtw_w8 (u32 m,u32 o,u8 v);
void rtw_w16(u32 m,u32 o,u16 v);
void rtw_w32(u32 m,u32 o,u32 v);
int rtw_efuse_dump_phy(u32 m,u8* map,int size);
int rtw_efuse_logical(const u8* phy,int physize,int protect,u8* log,int logsize);
void rtw_hexline(const u8* p,int off,int n,char* out);
int rtw_tbl_parse(const char* txt,int len);
u32 rtw_rdsz(u32 m,u32 o,u8 sz);
void rtw_delay_us(u32 us);
void rtw_rf_write_a(u32 m,u32 addr,u32 data);
int rtw_efuse_phy_sz(void);
int rtw_efuse_log_sz(void);
int rtw_efuse_prot_sz(void);

#endif
