#ifndef NVX_NET_WIFI_WIFI_PS_H
#define NVX_NET_WIFI_WIFI_PS_H
#include "include/nvx/base.h"
enum { WPS_OFF=0, WPS_ON, WPS_AUTO };
#define WPS_MAXFAIL 3
extern int g_wps_mode, g_wps_off, g_wps_tim, g_wps_aid;
extern u32 g_wps_enter, g_wps_wake_tim, g_wps_miss, g_wps_fail;
int wps_allowed(int bat_present,int bat_charging);
int wps_null(u8* f,const u8* bssid,const u8* mac,int pm);
int wps_tim(const u8* ies,int n,int aid);
void wps_rx_beacon(const u8* fr,int len);
int wps_miss(void);
void wps_reset(void);
int wnd_tx_null(int pm);
const char* wps_mode_name(void);
#endif
