#ifndef NVX_NET_WIFI_WIFI_NETDEV_H
#define NVX_NET_WIFI_WIFI_NETDEV_H

#include "include/nvx/base.h"

enum { NETDEV_ETH=0, NETDEV_WIFI=1 };

extern int g_wnd_have_gtk , g_wnd_gtk_id;
extern u32 g_wnd_rx_frames , g_wnd_rx_drop , g_wnd_tx_frames;

void wnd_setup(u32 m,const u8* bssid,const u8* mac,const u8* tk,int qos);
void wnd_set_gtk(const u8* gtk,int keyid);
void netdev_set(int d);

#endif
