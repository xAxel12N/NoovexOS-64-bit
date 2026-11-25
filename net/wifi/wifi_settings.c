#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/sysfeat.h"
#include "include/nvx/net/wifi/wifi_settings.h"
#include "include/nvx/net/wifi/wifi_ps.h"
#include "include/nvx/net/wifi/wifi_mgr.h"
#include "include/nvx/net/wifi/wifi_roam.h"

extern int g_ampdu_on, g_ampdu_tx_on, g_ampdu_tx_auto, g_ampdu_tx_hard, g_wps_mode, g_wps_off, g_wifi_ra, g_rtw_ra_dead, g_rtw_ra_fail;
extern int g_rtw_txpwr_tbl, g_rtw_txpwr_dead, g_rtw_iqk_en, g_wifi_sae_fallback, g_wifi_sae_ext, g_wifi_sae_fail, g_rtw_bw_mode, g_wm_5g, g_wm_auto;
extern volatile u64 g_ticks;
void ampdu_reset(void); void ampdu_tx_reset(u64 now); void wnd_delba_tx(void); void rtw_txpwr_set_mode(int);
int regdb_set_country(const char*); const char* regdb_country(void); const char* regdb_setting(void); void regdb_enable(int); int regdb_enabled(void);

int g_wifi_bgscan=1;
static struct featguard wf_g[4]={ {"rtw89 (RTL8852BE/CE)","feat-rtw89.cfg",1,0,1,0,0}, {"MT7925","feat-mt7925.cfg",1,0,3,0,0}, {"RTL8723DE","feat-rtw8723.cfg",1,0,3,0,0}, {"MT7921AU USB","feat-mt7921u.cfg",1,0,3,0,0} };
static const char* wf_why[4];
static struct featguard wf_bw={"Wi-Fi 40/80 MHz","feat-wifibw.cfg",1,0,3,0,0}, wf_cx={"Wi-Fi/Bluetooth coex","feat-wificoex.cfg",1,0,3,0,0};
static struct featguard* wfg(int f){ return f==WSET_RTW89?&wf_g[0]:f==WSET_MT7925?&wf_g[1]:f==WSET_RTW8723?&wf_g[2]:f==WSET_MT7921U?&wf_g[3]:f==WSET_BW?&wf_bw:f==WSET_COEX?&wf_cx:0; }
static int wsx_idx(int f){ return f==WSET_RTW89?0:f==WSET_MT7925?1:f==WSET_RTW8723?2:f==WSET_MT7921U?3:-1; }
int wifi_feat_on(int f){ struct featguard* g=wfg(f); if(!g)return 1; fg_boot(g); return g->on&&!g->dead; }
int wifi_feat_begin(int f){ struct featguard* g=wfg(f); return g?fg_begin(g):1; }
void wifi_feat_ok(int f){ struct featguard* g=wfg(f); if(g)fg_ok(g); }
void wifi_feat_fail(int f,const char* why){ struct featguard* g=wfg(f); if(!g)return; fg_fail(g,why); if(f==WSET_BW&&g->dead){ g_rtw_bw_dead=1; } if(f==WSET_COEX&&g->dead)g_rtw_coex_en=0; }
void wifi_feat_unsup(int f,const char* why){ struct featguard* g=wfg(f); int i=wsx_idx(f); if(!g||i<0)return; fg_ok(g);
    if(!wf_why[i]){ wf_why[i]=why?why:"bring-up not finished"; char m[160]; int n=0; const char* s[3]={g->name,": not supported yet - ",wf_why[i]}; for(int k=0;k<3;k++)for(const char* q=s[k];*q&&n<158;q++)m[n++]=*q; m[n]=0; klog(m); } }
static int wset_ok(int f){ return f>=0&&f<WSET_COUNT; }
static const char* const WSX_NM[WSET_COUNT]={"Receive aggregation (A-MPDU)","Send aggregation (A-MPDU)","Power saving","Roaming","Background scan","Fast reconnect","TX power","Rate adaptation","Calibration (IQK)","Country","Country rules","WPA3 to WPA2 fallback","WPA3 SAE on host-auth adapters","Channel width","Wi-Fi/Bluetooth coexistence","Realtek Wi-Fi 6 (rtw89)","MediaTek MT7925","Realtek RTL8723DE","5 GHz","Auto-connect","MediaTek MT7921AU USB"};
static const signed char WSX_MAX[WSET_COUNT]={1,1,2,1,1,1,1,2,1,0,1,1,1,3,1,1,1,1,1,1,1};
static const signed char WSX_DEF[WSET_COUNT]={1,1,1,1,1,1,1,1,1,0,1,0,1,0,1,1,1,1,1,1,1};
const char* wifi_set_name(int f){ return wset_ok(f)?WSX_NM[f]:""; }
int wifi_set_kind(int f){ return f==WSET_COUNTRY?WSET_KIND_COUNTRY:(f==WSET_PS||f==WSET_RA||f==WSET_BW||f==WSET_TXPWR)?WSET_KIND_CHOICE:WSET_KIND_BOOL; }
int wifi_set_max(int f){ return wset_ok(f)?WSX_MAX[f]:-1; }
int wifi_set_default(int f){ return wset_ok(f)?WSX_DEF[f]:-1; }
const char* wifi_set_choice(int f,int v){
    static const char* const PS[3]={"off","on","auto (battery only)"}, *RA[3]={"fixed","firmware","firmware, basic rates"}, *BW[4]={"auto","20 MHz","40 MHz","80 MHz"}, *TP[2]={"legacy","per-rate table"};
    if(f==WSET_PS&&v>=0&&v<3)return PS[v]; if(f==WSET_RA&&v>=0&&v<3)return RA[v]; if(f==WSET_BW&&v>=0&&v<4)return BW[v]; if(f==WSET_TXPWR&&v>=0&&v<2)return TP[v];
    return v?"on":"off"; }
int wifi_set_get(int f){
    switch(f){
    case WSET_AMPDU_RX: return g_ampdu_on?1:0;
    case WSET_AMPDU_TX: return g_ampdu_tx_on?1:0;
    case WSET_PS: return g_wps_mode==WPS_OFF?0:g_wps_mode==WPS_ON?1:2;
    case WSET_ROAM: return g_wifi_roam?1:0;
    case WSET_BGSCAN: return g_wifi_bgscan?1:0;
    case WSET_FAST: return wifi_fast_state()?1:0;
    case WSET_TXPWR: return g_rtw_txpwr_tbl?1:0;
    case WSET_RA: return !g_wifi_ra?0:g_rtw_ra_dead?2:1;
    case WSET_IQK: return g_rtw_iqk_en?1:0;
    case WSET_COUNTRY: { const char* s=regdb_setting(); if(!s||s[0]=='-')return 0; const char* c=regdb_country(); return WSET_CC(c[0],c[1]); }
    case WSET_REGDB: return regdb_enabled()?1:0;
    case WSET_SAE_FB: return g_wifi_sae_fallback?1:0;
    case WSET_SAE: return g_wifi_sae_ext?1:0;
    case WSET_BW: return g_rtw_bw_mode&3;
    case WSET_COEX: fg_boot(&wf_cx); return wf_cx.on&&g_rtw_coex_en?1:0;
    case WSET_RTW89: case WSET_MT7925: case WSET_RTW8723: case WSET_MT7921U: { struct featguard* g=wfg(f); fg_boot(g); return g->on; }
    case WSET_5G: return g_wm_5g?1:0;
    case WSET_AUTO: return g_wm_auto?1:0; }
    return -1; }
int wifi_set_set(int f,int v){
    if(!wset_ok(f))return -1;
    if(f==WSET_COUNTRY){ char cc[3]; if(!v){ char a[5]={'a','u','t','o',0}; if(regdb_set_country(a)<0)return -1; wifi_saved_save(); return 0; }
        cc[0]=(char)((v>>8)&0xFF); cc[1]=(char)(v&0xFF); cc[2]=0; if(regdb_set_country(cc)<0)return -1; wifi_saved_save(); return 0; }
    if(v<0||v>WSX_MAX[f])return -1;
    switch(f){
    case WSET_AMPDU_RX: g_ampdu_on=v; if(!v)ampdu_reset(); break;
    case WSET_AMPDU_TX: if(v){ if(!g_ampdu_tx_on)ampdu_tx_reset(g_ticks); g_ampdu_tx_on=1; g_ampdu_tx_auto=0; g_ampdu_tx_hard=0; } else { g_ampdu_tx_on=0; wnd_delba_tx(); } break;
    case WSET_PS: wifi_powersave_mode(v==0?WPS_OFF:v==1?WPS_ON:WPS_AUTO); break;
    case WSET_ROAM: wifi_roam_set(v); break;
    case WSET_BGSCAN: g_wifi_bgscan=v; break;
    case WSET_FAST: wifi_fast_set(v); break;
    case WSET_TXPWR: rtw_txpwr_set_mode(v); break;
    case WSET_RA: if(!v)g_wifi_ra=0; else { g_wifi_ra=1; g_rtw_ra_dead=v==2; g_rtw_ra_fail=0; } break;
    case WSET_IQK: g_rtw_iqk_en=v; break;
    case WSET_REGDB: regdb_enable(v); break;
    case WSET_SAE_FB: g_wifi_sae_fallback=v; break;
    case WSET_SAE: g_wifi_sae_ext=v; if(v)g_wifi_sae_fail=0; break;
    case WSET_BW: g_rtw_bw_mode=v; if(v==0){ fg_enable(&wf_bw,1); g_rtw_bw_dead=0; } break;
    case WSET_COEX: fg_enable(&wf_cx,v); g_rtw_coex_en=v; break;
    case WSET_RTW89: case WSET_MT7925: case WSET_RTW8723: case WSET_MT7921U: fg_enable(wfg(f),v); if(v)wf_why[wsx_idx(f)]=0; break;
    case WSET_5G: g_wm_5g=v; wifi_saved_save(); break;
    case WSET_AUTO: g_wm_auto=v; wifi_saved_save(); break; }
    return 0; }
int wifi_set_state(int f){
    if(!wset_ok(f))return WSET_ST_OFF;
    int i=wsx_idx(f); struct featguard* g=wfg(f);
    if(g){ fg_boot(g); if(!g->on)return WSET_ST_OFF; if(g->dead)return WSET_ST_FAILED; if(i>=0&&wf_why[i])return WSET_ST_UNSUPPORTED; }
    if(f==WSET_AMPDU_TX&&!g_ampdu_tx_on&&g_ampdu_tx_auto)return WSET_ST_FAILED;
    if(f==WSET_PS&&g_wps_off)return WSET_ST_FAILED;
    if(f==WSET_FAST&&wifi_fast_state()==2)return WSET_ST_FAILED;
    if(f==WSET_TXPWR&&g_rtw_txpwr_tbl&&g_rtw_txpwr_dead)return WSET_ST_FAILED;
    if(f==WSET_BW&&g_rtw_bw_dead)return WSET_ST_FAILED;
    if(f==WSET_COUNTRY)return WSET_ST_ON;
    return wifi_set_get(f)?WSET_ST_ON:WSET_ST_OFF; }
static char* wsx_cp(char* p,char* e,const char* s){ while(s&&*s&&p<e)*p++=*s++; *p=0; return p; }
const char* wifi_set_text(int f){
    static char b[WSET_COUNT][160]; if(!wset_ok(f))return "";
    char* p=b[f]; char* e=b[f]+159; int v=wifi_set_get(f), st=wifi_set_state(f);
    p=wsx_cp(p,e,WSX_NM[f]); p=wsx_cp(p,e,": ");
    if(f==WSET_COUNTRY){ if(!v)p=wsx_cp(p,e,"auto (from the access point)"); else { char c[3]={(char)(v>>8),(char)v,0}; p=wsx_cp(p,e,c); } return b[f]; }
    p=wsx_cp(p,e,wifi_set_choice(f,v));
    if(st==WSET_ST_FAILED)p=wsx_cp(p,e," - off for this session after failures");
    else if(st==WSET_ST_UNSUPPORTED){ p=wsx_cp(p,e," - not supported yet: "); p=wsx_cp(p,e,wf_why[wsx_idx(f)]); }
    if(f==WSET_BW&&!g_rtw_bw_dead)p=wsx_cp(p,e," (RTL8821CE, from the next connection)");
    return b[f]; }
int wifi_feat_report(char* o,int cap){
    char* p=o; char* e=o+cap-1; if(cap<1)return 0; *p=0;
    for(int f=WSET_RTW89;f<WSET_COUNT;f++){ if(wsx_idx(f)<0)continue; int st=wifi_set_state(f); if(st!=WSET_ST_UNSUPPORTED&&st!=WSET_ST_FAILED)continue; p=wsx_cp(p,e,"  "); p=wsx_cp(p,e,wifi_set_text(f)); p=wsx_cp(p,e,"\n"); }
    return (int)(p-o); }
