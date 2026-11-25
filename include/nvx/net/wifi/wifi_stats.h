#ifndef NVX_NET_WIFI_WIFI_STATS_H
#define NVX_NET_WIFI_WIFI_STATS_H
#include "include/nvx/base.h"
#define WST_HIST 120
struct wifi_stats { const char* drv; const char* ssid; int conn, ch, bw, rssi, noise, ba; u32 rate_kbps, rx, tx, rxdrop, txdrop, retry; };
extern u32 g_wst_retry, g_wst_rate_kbps; extern int g_wst_noise, g_wst_bw;
u32 wst_mbps_x100(u64 bytes,u32 ms);
int wst_retry_pct(u32 retry,u32 tx);
const char* wst_hint(u32 mx100,u32 link_mbps,int rssi,int retry_pct);
int wifi_stats_fmt(char* o,int cap,const struct wifi_stats* s);
int wifi_speed_fmt(char* o,int cap,const char* what,u64 bytes,u32 ms,u32 link_mbps,int rssi,int retry_pct);
int wst_graph_y(int rssi,int h);
void wifi_stats_get(struct wifi_stats* s);
void wifi_stats_sample(void);
int wifi_stats_hist(signed char* out,int max);
int wifi_speedtest(char* o,int cap,const char* host,const char* path,int up);
#endif
