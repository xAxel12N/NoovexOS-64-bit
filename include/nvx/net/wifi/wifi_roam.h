#ifndef NVX_NET_WIFI_WIFI_ROAM_H
#define NVX_NET_WIFI_WIFI_ROAM_H
#include "include/nvx/base.h"
#include "include/nvx/drivers/net/wifi/rtw8821ce.h"
#define ROAM_MAXC 8
#define ROAM_DELTA 8
#define ROAM_WEAK (-70)
extern int g_wifi_roam, g_wifi_roams, g_wifi_roam_fails;
int wifi_scan_slice(const u8* ch,int n);
int wifi_roam_join(const scan_ap* ap);
void wifi_roam_tick(void);
void wifi_roam_set(int on);
int wifi_roam_pick(const scan_ap* c,int n,int cur,int delta);
int wifi_roam_text(char* o,int cap);
#endif
