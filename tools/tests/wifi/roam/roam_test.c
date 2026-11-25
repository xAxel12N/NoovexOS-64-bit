#include <stdio.h>
#include <string.h>
#include "include/nvx/base.h"
#include "net/wifi/wifi_roam.c"
int g_wifi_bgscan=1;
volatile u64 g_ticks; int g_wm_state=WM_CONNECTED, g_wm_5g=1, g_wm_portal, g_wifi_rssi=-60, g_apn; u8 g_wm_bssid[6]={2,0,0,0,0,1}; u8 g_wm_ch=6; u64 g_wm_since=1;
scan_ap g_aps[24]; char g_wifi_conn[33]="home"; u32 g_wnd_tx_frames; int g_dhcp_run;
static int busy, nslice, njoin, joinr, slicech[64], nsc; static scan_ap air[8]; static int nair;
char* scopy(char* p,const char* s){ while(*s)*p++=*s++; *p=0; return p; }
char* udec(char* p,u32 v){ return p+sprintf(p,"%u",v); }
char* hx(char* p,u32 v,int dg){ return p+sprintf(p,"%0*x",dg,v); }
void klog(const char* s){ printf("  log: %s\n",s); }
int dhcp_async_running(void){ return g_dhcp_run; }
int wifi_scan_busy(void){ return busy>0; }
int wifi_scan_slice(const u8* ch,int n){ if(busy)return -1; nslice++; for(int i=0;i<n;i++){ if(nsc<64)slicech[nsc++]=ch[i]; for(int a=0;a<nair;a++) if(air[a].ch==ch[i]){ int k=-1; for(int j=0;j<g_apn;j++) if(!memcmp(g_aps[j].bssid,air[a].bssid,6))k=j; if(k<0)k=g_apn++; g_aps[k]=air[a]; g_aps[k].last=g_ticks; } } busy=2; return 0; }
int wifi_roam_join(const scan_ap* ap){ njoin++; if(joinr==0){ memcpy(g_wm_bssid,ap->bssid,6); g_wm_ch=ap->ch; g_wm_since=g_ticks; } else g_wm_state=WM_IDLE; return joinr; }
static int fails;
#define CHK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
static void run(u64 t){ u64 end=g_ticks+t; while(g_ticks<end){ g_ticks+=20; if(busy)busy--; wifi_roam_tick(); } }
static scan_ap mk(u8 id,u8 ch,int rssi,const char* ss,u8 sec,u8 akm){ scan_ap a; memset(&a,0,sizeof a); a.bssid[0]=2; a.bssid[5]=id; a.ch=ch; a.rssi=(signed char)rssi; strcpy(a.ssid,ss); a.sec=sec; a.akm=akm; return a; }
int main(void){
    scan_ap c[3]; c[0]=mk(2,1,-65,"home",2,2); c[1]=mk(3,1,-55,"home",2,2); c[2]=mk(4,1,-50,"home",2,2);
    CHK(wifi_roam_pick(c,3,-60,8)==2); CHK(wifi_roam_pick(c,3,-58,8)==2); CHK(wifi_roam_pick(c,3,-57,8)<0); CHK(wifi_roam_pick(c,3,-41,8)<0); CHK(wifi_roam_pick(c,3,-127,8)<0); CHK(wifi_roam_pick(c,2,-63,8)==1); CHK(wifi_roam_pick(c,2,-62,8)<0);
    g_aps[0]=mk(1,6,-60,"home",2,2); g_apn=1;
    air[0]=mk(2,36,-55,"home",2,2); air[1]=mk(3,11,-40,"other",2,2); air[2]=mk(4,44,-40,"home",0,0); air[3]=mk(5,149,-40,"home",2,8); nair=4;
    g_ticks=2000; run(30000);
    CHK(nslice>0); CHK(njoin==0); int seen6=0; for(int i=0;i<nsc;i++) if(slicech[i]==6)seen6=1; CHK(!seen6); CHK(nsc>=36);
    int sl=nslice; g_dhcp_run=1; run(40000); CHK(nslice==sl); g_dhcp_run=0;
    air[0].rssi=-50; g_wifi_rssi=-75; run(4000); CHK(njoin==1); CHK(g_wm_bssid[5]==2); CHK(g_wm_ch==36); CHK(g_wifi_roams==1);
    g_wm_bssid[5]=1; g_wm_ch=6; g_wm_since=g_ticks; g_wifi_rssi=-80; air[0]=mk(7,40,-50,"home",2,2); air[1]=mk(8,48,-50,"home",2,2); air[2]=mk(9,52,-50,"home",2,2); nair=3; joinr=3;
    for(int r=0;r<6&&g_wifi_roam;r++){ g_wm_state=WM_CONNECTED; g_wm_since=g_ticks; run(8000); }
    CHK(g_wifi_roam==0); CHK(g_wifi_roam_fails==3); int nj=njoin; g_wm_state=WM_CONNECTED; run(20000); CHK(njoin==nj);
    wifi_roam_set(1); CHK(g_wifi_roam==1);
    char b[640]; wifi_roam_text(b,sizeof b); CHK(strstr(b,"Roaming on")!=0);
    sl=nslice; g_wm_state=WM_IDLE; run(20000); CHK(nslice==sl);
    if(fails){ printf("roam: %d failures\n",fails); return 1; } printf("roam: all ok\n"); return 0; }
