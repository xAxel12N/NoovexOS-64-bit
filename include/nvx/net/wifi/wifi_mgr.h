#ifndef NVX_NET_WIFI_WIFI_MGR_H
#define NVX_NET_WIFI_WIFI_MGR_H
#include "include/nvx/base.h"
#define WIFI_MAXNET 16
enum { WM_OFF=0, WM_IDLE, WM_CONNECTING, WM_CONNECTED };
typedef struct { char ssid[33], pass[65], user[65]; int prio, hidden, autoc, eap, pinset; u8 pin[32]; } wnet;
extern wnet g_wnets[WIFI_MAXNET]; extern int g_wnet_n;
extern int g_wm_state, g_wm_auto, g_wm_5g, g_wm_ps, g_wm_lps, g_wm_portal, g_wm_cur, g_wm_reason, g_wm_up;
extern u8 g_wm_bssid[6]; extern u8 g_wm_ch; extern u64 g_wm_since;
void wifi_saved_load(void);
void wifi_saved_save(void);
int wifi_saved_find(const char* ssid);
int wifi_saved_add(const char* ssid,const char* pass,const char* user,int eap,int hidden);
int wifi_saved_forget(const char* ssid);
void wifi_saved_prio(const char* ssid,int delta);
int wifi_up(void);
void wifi_radio_off(void);
void wifi_radio_on(void);
int wifi_scan_start(int full);
int wifi_scan_busy(void);
void wifi_scan_blocking(int full);
int wifi_connect_ap(int idx,const char* pass,const char* user,int save);
int wifi_connect_ssid(const char* ssid,const char* pass,const char* user,int hidden);
void wifi_disconnect(void);
void wifi_mgr_tick(void);
int wifi_powersave(int on);
const char* wifi_state_name(void);
int wifi_signal_bars(int rssi);
int wifi_rate_mbps(void);
void wm_status_set(const char* a);
#endif
