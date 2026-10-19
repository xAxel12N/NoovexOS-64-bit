#ifndef NVX_NET_WIFI_WPA2_H
#define NVX_NET_WIFI_WPA2_H

#include "include/nvx/base.h"

extern u8 g_ptk[48];
extern u8 g_pmk[32];
extern int g_wpa_ok;

void rtw_tx_data_eapol(u32 m,const u8* bssid,const u8* mac,const u8* eapol,int elen);
int rtw_rx_wait_eapol(u32 m,const u8* bssid,u8* out,int maxout,int ms);
void wpa_derive_ptk(const u8* pmk,const u8* aa,const u8* spa,const u8* anonce,const u8* snonce,u8* ptk);
int wpa_rsn_ie(u8* b);

#endif
