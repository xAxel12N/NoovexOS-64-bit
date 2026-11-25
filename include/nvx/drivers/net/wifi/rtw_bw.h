#pragma once
#include "include/nvx/base.h"
enum { RTW_BWM_AUTO = 0, RTW_BWM_20 = 1, RTW_BWM_40 = 2, RTW_BWM_80 = 3 };
enum { RTW_SC_DONT_CARE = 0, RTW_SC_20_UPPER = 1, RTW_SC_20_LOWER = 2, RTW_SC_20_UPMOST = 3, RTW_SC_20_LOWEST = 4, RTW_SC_40_UPPER = 9, RTW_SC_40_LOWER = 10 };
#define RTW_BW_PEERS 24
#define RTW_BW_LOW_RPTS 8
#define RTW_RATEID_ARFR1_AC_1SS 10
#define RTW_BW_VERIFY_T 1000
#define RTW_BW_VERIFY_RX 3
enum { RTW_BWD_NONE = 0, RTW_BWD_NARROW = 1, RTW_BWD_OK = 2, RTW_BWD_REJOIN = 3 };
typedef struct { u8 bssid[6]; u8 ht40, sco, vht, vw, vc0, bad; } rtw_bw_peer;
typedef struct { u8 cch, pidx, cch40, bw; } rtw_bw_ch;
extern int g_rtw_bw_mode, g_rtw_bw_falls, g_rtw_bw_low;
extern u8 g_rtw_bw_cur, g_rtw_bw_pidx, g_rtw_bw_cch, g_rtw_bw_vc0, g_rtw_bw_bssid[6];
extern signed char g_rtw_bw_sec;
extern u8 g_rtw_bw_join, g_rtw_bw_jch, g_rtw_bw_drop;
extern int g_rtw_bw_live, g_rtw_bw_rejoin;
extern rtw_bw_peer g_rtw_bw_peers[RTW_BW_PEERS];
int  rtw_bw_calc(u8 pri, u8 bw, int sec, u8 vc0, rtw_bw_ch* o);
u32  rtw_bw_adcclk(u32 old, u8 bw, u8 pidx);
u32  rtw_bw_rf18(u8 bw);
u8   rtw_bw_data_sc(u8 bw, u8 pidx);
u32  rtw_bw_rfmod(u32 old, u8 bw);
void rtw_bw_dfir(u8 bw, u32* acbb, u32* acbbrx, u32* txdfir, u32* chfir);
void rtw_bw_note(const u8* bssid, const u8* ies, int len);
rtw_bw_peer* rtw_bw_peer_of(const u8* bssid);
u8   rtw_bw_pick(const u8* bssid, u8 pri, int wide_chip);
void rtw_bw_join_start(const u8* bssid, u8 pri, int wide);
u8   rtw_bw_chan(u8 ch);
void rtw_bw_leave(void);
void rtw_bw_join_done(const u8* bssid, int rc);
int  rtw_bw_ht_cap(u8* cap_lo);
int  rtw_bw_vht_ie(u8* f);
void rtw_bw_ra(u8* rid, u32* mask, u32* w0);
void rtw_bw_ra_report(u8 rate);
const char* rtw_bw_name(u8 bw);
char* rtw_bw_status(char* p);
int  rtw_bw_action(u8* f, const u8* bssid, const u8* sa, int vht);
int  rtw_bw_drop_step(u64 now, u32 rx);
void rtw_bw_narrowed(u64 now, u32 rx);
u8   rtw_bw_sgi(u8 bw, const u8* ies, int il, u8 sgi20);
int  rtw_bw_tick(u32 m, u64 now, u32 rx);
