#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/apps/system/settings.h"
#include "include/nvx/net/wifi/wifi_mgr.h"
#include "include/nvx/net/wifi/wifi_netdev.h"
#include "include/nvx/net/wifi/wifi_roam.h"
#include "include/nvx/net/wifi/wifi_settings.h"

extern volatile u64 g_ticks;
int g_wifi_roam=1, g_wifi_roams=0, g_wifi_roam_fails=0;
static scan_ap rm_c[ROAM_MAXC]; static int rm_n=0;
static u8 rm_bad[4][6]; static int rm_badn=0;
static u8 rm_chs[40]; static int rm_nch=0, rm_pos=-1, rm_inslice=0, rm_refok=0, rm_fail=0;
static u8 rm_rsec, rm_rakm; static u64 rm_since=0, rm_next=0, rm_slot=0, rm_st=0, rm_last=0; static u32 rm_tx=0;
static const u8 RM2[13]={1,6,11,2,3,4,5,7,8,9,10,12,13};
static const u8 RM5[24]={36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,149,153,157,161,165};

static int rm_meq(const u8* a,const u8* b){ for(int i=0;i<6;i++) if(a[i]!=b[i])return 0; return 1; }
static int rm_seq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
static int rm_isbad(const u8* b){ for(int i=0;i<rm_badn;i++) if(rm_meq(rm_bad[i],b))return 1; return 0; }

int wifi_roam_pick(const scan_ap* c,int n,int cur,int delta){
    int best=-1; if(cur<=-127)return -1;
    for(int i=0;i<n;i++){ if(c[i].rssi<=-127||c[i].rssi<cur+delta)continue; if(best<0||c[i].rssi>c[best].rssi)best=i; }
    return best; }

void wifi_roam_set(int on){ g_wifi_roam=on?1:0; rm_fail=0; rm_pos=-1; rm_next=0; if(on){ rm_badn=0; klog("wifi: roaming on"); } else klog("wifi: roaming off"); }

static void rm_reset(void){ rm_n=0; rm_pos=-1; rm_inslice=0; rm_refok=0; rm_since=g_wm_since; rm_next=g_wm_since+1500; rm_slot=0; }

static void rm_ref(void){ for(int i=0;i<g_apn;i++) if(rm_meq(g_aps[i].bssid,g_wm_bssid)){ rm_rsec=g_aps[i].sec; rm_rakm=g_aps[i].akm; rm_refok=1; return; } }

static void rm_chlist(void){ rm_nch=0; for(int i=0;i<13;i++) if(RM2[i]!=g_wm_ch)rm_chs[rm_nch++]=RM2[i]; if(g_wm_5g)for(int i=0;i<24;i++) if(RM5[i]!=g_wm_ch)rm_chs[rm_nch++]=RM5[i]; }

static void rm_harvest(void){
    if(!rm_refok){ rm_ref(); if(!rm_refok)return; }
    for(int i=0;i<g_apn;i++){ const scan_ap* a=&g_aps[i]; if(a->last<rm_st||a->hidden||!a->ssid[0]||!rm_seq(a->ssid,g_wifi_conn))continue;
        if(rm_meq(a->bssid,g_wm_bssid)||rm_isbad(a->bssid)||a->sec!=rm_rsec||(rm_rakm&&!(a->akm&rm_rakm)))continue;
        int k=-1; for(int j=0;j<rm_n;j++) if(rm_meq(rm_c[j].bssid,a->bssid))k=j;
        if(k<0){ if(rm_n<ROAM_MAXC)k=rm_n++; else { k=0; for(int j=1;j<rm_n;j++) if(rm_c[j].last<rm_c[k].last)k=j; } }
        rm_c[k]=*a; }
    for(int j=0;j<rm_n;) if(g_ticks-rm_c[j].last>6000)rm_c[j]=rm_c[--rm_n]; else j++; }

static void rm_log(const char* a,const scan_ap* c,int cur){ char b[160],*p=b; p=scopy(p,"wifi: roam "); p=scopy(p,a); p=scopy(p," "); for(int i=0;i<6;i++){ p=hx(p,c->bssid[i],2); if(i<5)*p++=':'; }
    p=scopy(p," ch "); p=udec(p,c->ch); p=scopy(p," -"); p=udec(p,(u32)(-c->rssi)); p=scopy(p," dBm, current -"); p=udec(p,(u32)(cur<0?-cur:0)); p=scopy(p," dBm"); *p=0; klog(b); }

static void rm_decide(void){
    if(g_ticks-rm_last<3000||g_ticks-g_wm_since<1500)return;
    int cur=g_wifi_rssi; if(cur<=-127||cur>=ROAM_WEAK)return; int k=wifi_roam_pick(rm_c,rm_n,cur,ROAM_DELTA); if(k<0)return;
    scan_ap c=rm_c[k]; rm_log("to",&c,cur); rm_last=g_ticks;
    int r=wifi_roam_join(&c);
    if(r==0){ g_wifi_roams++; rm_fail=0; rm_log("done",&c,cur); return; }
    g_wifi_roam_fails++; rm_fail++; if(rm_badn<4){ for(int i=0;i<6;i++)rm_bad[rm_badn][i]=c.bssid[i]; rm_badn++; }
    rm_log("failed",&c,cur);
    if(rm_fail>=3){ g_wifi_roam=0; klog("wifi: roaming turned off for this session after 3 failed roams (wifi roam on to retry)"); } }

void wifi_roam_tick(void){
    if(!g_wifi_roam||!g_wifi_bgscan||g_wm_state!=WM_CONNECTED)return;
    if(rm_since!=g_wm_since)rm_reset();
    if(rm_inslice){ if(wifi_scan_busy())return; rm_inslice=0; rm_harvest(); rm_decide(); return; }
    if(g_wm_portal||wifi_scan_busy())return;
    { extern int dhcp_async_running(void); if(dhcp_async_running())return; }
    int weak=g_wifi_rssi>-127&&g_wifi_rssi<ROAM_WEAK;
    if(rm_pos<0){ if(g_ticks<rm_next)return; rm_chlist(); if(!rm_nch){ rm_next=g_ticks+18000; return; } rm_pos=0; rm_slot=g_ticks; rm_tx=g_wnd_tx_frames; }
    if(g_ticks<rm_slot)return;
    if(!weak&&g_wnd_tx_frames-rm_tx>100){ rm_tx=g_wnd_tx_frames; rm_slot=g_ticks+200; return; }
    rm_tx=g_wnd_tx_frames;
    int n=rm_nch-rm_pos; if(n>2)n=2;
    rm_st=g_ticks; if(wifi_scan_slice(rm_chs+rm_pos,n)==0)rm_inslice=1;
    rm_pos+=n; rm_slot=g_ticks+(weak?150:300);
    if(rm_pos>=rm_nch){ rm_pos=-1; rm_next=g_ticks+(weak?2000:18000); } }

int wifi_roam_text(char* o,int cap){ char* p=o; char* e=o+cap-80;
    p=scopy(p,"Roaming "); p=scopy(p,g_wifi_roam?"on":"off"); p=scopy(p,", roams "); p=udec(p,(u32)g_wifi_roams); p=scopy(p,", failed "); p=udec(p,(u32)g_wifi_roam_fails); *p++='\n';
    for(int i=0;i<rm_n&&p<e;i++){ p=scopy(p,"  "); for(int k=0;k<6;k++){ p=hx(p,rm_c[i].bssid[k],2); if(k<5)*p++=':'; } p=scopy(p,"  ch "); p=udec(p,rm_c[i].ch); p=scopy(p,"  -"); p=udec(p,(u32)(-rm_c[i].rssi)); p=scopy(p," dBm\n"); }
    *p=0; return (int)(p-o); }
