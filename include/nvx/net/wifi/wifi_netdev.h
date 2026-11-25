#ifndef NVX_NET_WIFI_WIFI_NETDEV_H
#define NVX_NET_WIFI_WIFI_NETDEV_H

#include "include/nvx/base.h"

enum { NETDEV_ETH=0, NETDEV_WIFI=1 };

extern int g_wnd_have_gtk , g_wnd_gtk_id;
extern u32 g_wnd_rx_frames , g_wnd_rx_drop , g_wnd_tx_frames, g_wnd_rx_uc;
static inline int wnd_ap_uc(const u8* fr,const u8* me,const u8* ap){ if((fr[0]&0x0C)!=0x08||(fr[0]&0x40)||(fr[1]&3)!=2)return 0; for(int i=0;i<6;i++)if(fr[4+i]!=me[i]||fr[10+i]!=ap[i])return 0; return 1; }

void wnd_setup(u32 m,const u8* bssid,const u8* mac,const u8* tk,int qos);
void wnd_set_gtk(const u8* gtk,int keyid);
void netdev_set(int d);
void wnd_setup_open(u32 m,const u8* bssid,const u8* mac,int qos);
void wnd_down(void);
int wnd_is_up(void);
const u8* wnd_bssid(void);
extern u64 g_wnd_beacon_t; extern int g_wnd_lost, g_wnd_beacons, g_wnd_open; extern u32 g_wnd_bcn_int;

#endif
