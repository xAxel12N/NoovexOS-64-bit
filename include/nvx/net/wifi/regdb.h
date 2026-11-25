#ifndef NVX_NET_WIFI_REGDB_H
#define NVX_NET_WIFI_REGDB_H
#include "include/nvx/base.h"
#define REGDB_DFS 1u
#define REGDB_NOIR 2u
#define REGDB_NOOUTDOOR 4u
#define REGDB_AUTOBW 8u
#define REGDB_NOINDOOR 16u
struct regdb_rule { u16 lo2, hi2; u16 bw; signed char dbm; u8 fl; };
struct regdb_country { char cc[3]; u8 dfsreg; u16 first; u8 n; };
int regdb_ch_freq(u8 ch);
int regdb_freq_flags(int mhz,int bw);
int regdb_freq_max_dbm(int mhz);
int regdb_ch_allowed(u8 ch);
int regdb_max_dbm(u8 ch);
int regdb_is_dfs(u8 ch);
int regdb_no_ir(u8 ch);
int regdb_probe_ok(u8 ch);
int regdb_dfs_region(void);
const char* regdb_country(void);
const char* regdb_setting(void);
int regdb_set_country(const char* cc);
void regdb_note_ie(const u8* bssid,const u8* cc);
int regdb_enabled(void);
void regdb_enable(int on);
int regdb_filter(u8* ch,int n);
void regdb_scan_result(int filtered,int found);
#endif
