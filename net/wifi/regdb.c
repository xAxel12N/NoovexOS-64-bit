// SPDX-License-Identifier: GPL-3.0-or-later
// Regulatory data: wireless-regdb db.txt, Copyright (c) 2008 Luis R. Rodriguez, Johannes Berg, Michael Green, ISC licence (see net/wifi/NOTICE)
#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/net/wifi/regdb.h"
#include "include/nvx/net/wifi/regdb_data.h"
#define REGDB_NC (int)(sizeof(g_regdb_ctry)/sizeof(g_regdb_ctry[0]))
static char g_rd_user[3]="SE"; static int g_rd_auto=1, g_rd_on=1, g_rd_fail=0, g_rd_ci=-2;
static struct { u8 bssid[6]; char cc[2]; } g_rd_seen[8]; static int g_rd_ns=0;
static int rd_find(const char* cc){ for(int i=0;i<REGDB_NC;i++) if(g_regdb_ctry[i].cc[0]==cc[0]&&g_regdb_ctry[i].cc[1]==cc[1])return i; return -1; }
static int rd_ap_ci(void){
    extern int g_wm_state; extern const u8* wnd_bssid(void); if(g_wm_state!=3)return -1; const u8* bs=wnd_bssid();
    for(int i=0;i<g_rd_ns;i++){ int eq=1; for(int k=0;k<6;k++) if(g_rd_seen[i].bssid[k]!=bs[k])eq=0; if(eq)return rd_find(g_rd_seen[i].cc); } return -1; }
static int rd_ci(void){
    if(g_rd_auto){ int a=rd_ap_ci(); if(a>=0){ if(a!=g_rd_ci){ g_rd_ci=a; char m[48]="wifi: country XX from the access point"; m[14]=g_regdb_ctry[a].cc[0]; m[15]=g_regdb_ctry[a].cc[1]; klog(m); } return a; } }
    int u=rd_find(g_rd_user); if(u<0)u=rd_find("00"); g_rd_ci=u; return u; }
const char* regdb_setting(void){ return g_rd_auto?"--":g_rd_user; }
const char* regdb_country(void){ int c=rd_ci(); return c<0?"00":g_regdb_ctry[c].cc; }
int regdb_dfs_region(void){ int c=rd_ci(); return c<0?0:g_regdb_ctry[c].dfsreg; }
int regdb_set_country(const char* cc){
    if(!cc)return -1;
    if((cc[0]=='-'&&cc[1]=='-'&&!cc[2])||((cc[0]|32)=='a'&&(cc[1]|32)=='u'&&(cc[2]|32)=='t'&&(cc[3]|32)=='o'&&!cc[4])){ g_rd_auto=1; return 0; }
    char u[3]={ (char)(cc[0]>='a'&&cc[0]<='z'?cc[0]-32:cc[0]), (char)(cc[1]>='a'&&cc[1]<='z'?cc[1]-32:cc[1]), 0 };
    if(!cc[1]||cc[2]||rd_find(u)<0)return -1;
    g_rd_user[0]=u[0]; g_rd_user[1]=u[1]; g_rd_auto=0; return 0; }
void regdb_note_ie(const u8* bssid,const u8* cc){
    if(!bssid||!cc||cc[0]<'A'||cc[0]>'Z'||cc[1]<'A'||cc[1]>'Z')return;
    int i=0;
    for(;i<g_rd_ns;i++){ int eq=1; for(int k=0;k<6;k++) if(g_rd_seen[i].bssid[k]!=bssid[k])eq=0; if(eq)break; }
    if(i==g_rd_ns){ if(g_rd_ns<8)g_rd_ns++; else { for(int j=1;j<8;j++)g_rd_seen[j-1]=g_rd_seen[j]; i=7; } for(int k=0;k<6;k++)g_rd_seen[i].bssid[k]=bssid[k]; }
    g_rd_seen[i].cc[0]=(char)cc[0]; g_rd_seen[i].cc[1]=(char)cc[1]; }
int regdb_ch_freq(u8 ch){ if(!ch)return 0; if(ch<14)return 2407+5*ch; if(ch==14)return 2484; return 5000+5*ch; }
static const struct regdb_rule* rd_rule(int mhz,int bw){
    int c=rd_ci(); if(c<0||mhz<=0)return 0; if(bw<20)bw=20; int lo=2*mhz-bw, hi=2*mhz+bw; const struct regdb_country* k=&g_regdb_ctry[c];
    for(int i=0;i<k->n;i++){ const struct regdb_rule* r=&g_regdb_rules[g_regdb_idx[k->first+i]]; if(lo>=r->lo2&&hi<=r->hi2&&(bw<=r->bw||(r->fl&REGDB_AUTOBW)))return r; }
    return 0; }
int regdb_freq_flags(int mhz,int bw){ const struct regdb_rule* r=rd_rule(mhz,bw); return r?r->fl:-1; }
int regdb_freq_max_dbm(int mhz){ const struct regdb_rule* r=rd_rule(mhz,20); return r?r->dbm:0; }
int regdb_enabled(void){ return g_rd_on; }
void regdb_enable(int on){ g_rd_on=on?1:0; g_rd_fail=0; }
int regdb_ch_allowed(u8 ch){ return !g_rd_on||rd_rule(regdb_ch_freq(ch),20)!=0; }
int regdb_max_dbm(u8 ch){ return regdb_freq_max_dbm(regdb_ch_freq(ch)); }
int regdb_is_dfs(u8 ch){ int f=regdb_freq_flags(regdb_ch_freq(ch),20); return f>=0&&(f&REGDB_DFS); }
int regdb_no_ir(u8 ch){ int f=regdb_freq_flags(regdb_ch_freq(ch),20); return f<0||(f&REGDB_NOIR); }
int regdb_probe_ok(u8 ch){ if(!g_rd_on)return 1; int f=regdb_freq_flags(regdb_ch_freq(ch),20); return f>=0&&!(f&(REGDB_DFS|REGDB_NOIR)); }
int regdb_filter(u8* ch,int n){ if(!g_rd_on)return n; int o=0; for(int i=0;i<n;i++) if(regdb_ch_allowed(ch[i]))ch[o++]=ch[i]; return o; }
void regdb_scan_result(int filtered,int found){
    if(!g_rd_on||filtered<=0)return;
    if(found>0){ g_rd_fail=0; return; }
    if(++g_rd_fail>=3){ g_rd_on=0; g_rd_fail=0; klog("wifi: regdb channel filter turned off after 3 empty scans (wifi regdb on to retry)"); } }
