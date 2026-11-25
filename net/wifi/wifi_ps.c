#include "include/nvx/base.h"
#include "include/nvx/net/wifi/wifi_ps.h"
int g_wps_mode=WPS_ON, g_wps_off=0, g_wps_tim=0, g_wps_aid=0;
u32 g_wps_enter=0, g_wps_wake_tim=0, g_wps_miss=0, g_wps_fail=0;
int wps_allowed(int bat_present,int bat_charging){ if(g_wps_off||g_wps_mode==WPS_OFF)return 0; if(g_wps_mode==WPS_ON)return 1; return bat_present&&!bat_charging; }
int wps_null(u8* f,const u8* bssid,const u8* mac,int pm){ f[0]=0x48; f[1]=(u8)(0x01|(pm?0x10:0)); f[2]=f[3]=0;
    for(int i=0;i<6;i++){ f[4+i]=bssid[i]; f[10+i]=mac[i]; f[16+i]=bssid[i]; } f[22]=f[23]=0; return 24; }
int wps_tim(const u8* ies,int n,int aid){
    for(int o=0;o+2<=n;){ int id=ies[o], l=ies[o+1]; if(o+2+l>n)return 0;
        if(id==5){ if(l<4)return 0; const u8* t=ies+o+2; int r=(t[0]==0&&(t[2]&1))?2:0; if(aid<=0||aid>2007)return r;
            int n1=t[2]&0xFE, b=aid>>3; if(b<n1||b>=n1+l-3)return r; return (t[3+b-n1]>>(aid&7))&1?1:r; }
        o+=2+l; }
    return 0; }
void wps_rx_beacon(const u8* fr,int len){ if(len<36)return; int r=wps_tim(fr+36,len-36,g_wps_aid); if(r==1)g_wps_tim=1; }
int wps_miss(void){ g_wps_miss++; if(++g_wps_fail>=WPS_MAXFAIL&&!g_wps_off){ g_wps_off=1; return 1; } return 0; }
void wps_reset(void){ g_wps_tim=0; g_wps_aid=0; }
const char* wps_mode_name(void){ return g_wps_off?"off (disabled after missed beacons or link problems)":g_wps_mode==WPS_ON?"on":g_wps_mode==WPS_OFF?"off":"auto (battery only)"; }
