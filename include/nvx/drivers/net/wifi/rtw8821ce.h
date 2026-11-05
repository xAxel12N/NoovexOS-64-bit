#ifndef NVX_DRIVERS_WIFI_RTW8821CE_H
#define NVX_DRIVERS_WIFI_RTW8821CE_H

#include "include/nvx/base.h"

typedef struct { u8 bssid[6]; char ssid[33]; u8 ch; int seen; u8 sec; signed char rssi; u8 ht; u8 akm; u8 pmf; u8 wmm; u8 hidden; u64 last; u8 grp; } scan_ap;
enum { AKM_PSK=1, AKM_SAE=2, AKM_8021X=4, AKM_PSK256=8 };

extern int g_dmem_ck ,g_imem_ck ,g_emem_ck ,g_dmem_sf ,g_dmem_if ,g_imem_sf ,g_imem_if;
extern int rtw_mac_trx_ok;
extern u8* g_rxring;
extern u8* g_rxbuf;
extern u32 g_rxnum;
extern u32 g_rxidx;
extern int g_chquiet;
extern scan_ap g_aps[24];
extern int g_apn;
extern u8* g_bering;
extern u32 g_benum;
extern u32 g_beidx;
extern u8* g_mgring;
extern u32 g_mgnum;
extern u32 g_mgidx;
extern int g_probe_txok;
extern u32 g_probe_hwb , g_probe_hwa;
extern u8 g_eap_stash[512];
extern int g_eap_stashlen;
extern u16 g_seq;

int rtw_apply_one(u32 m,u32 off,u8 sz,u32 v);
int rtw_iddma_block(u32 m,u32 src,u32 dst,u32 len,int first);
int rtw_bcnq_stage(u32 m,const u8* payload,u32 plen);
int rtw_fw_download_iddma(u32 m,const u8* fw,long fwsz);
int rtw_mac_trx_init(u32 m);
u32 rtw_rf_read_a(u32 m,u32 addr);
void rtw_set_channel_2g(u32 m,u8 ch);
void rtw_set_txpwr(u32 m,u8 ch);
void rtw_txdesc_rate(u8* pkt,int mgmt);
int rtw_rx_rssi(const u8* base);
void rtw_ra_connect(u32 m,int ht,const u8* rates,int nrates);
void rtw_ra_disconnect(u32 m);
extern int g_wifi_txpwr, g_wifi_band5, g_wifi_ra, g_wifi_rssi;
extern u8 g_wifi_curch;
int rtw_rx_enable(u32 m);
int rtw_rx_ring_up(u32 m);
int rtw_rx_hw_wp(u32 m);
int rtw_scan_drain(u32 m,u8 ch);
int rtw_rx_poll_beacon(u32 m,char* ssid_out,int* rssi_out,u8* bssid_out);
int rtw_tx_probe(u32 m,int* got_resp,char* resp_ssid);
int rtw_tx_mgmt(u32 m,const u8* frame,int fl);
int rtw_rx_wait_mgmt(u32 m,u8 want_fc,const u8* from_bssid,u8* body_out,int* body_len,int ms);
int rtw_assoc(u32 m,const scan_ap* ap,const char* ssid);
int rtw_assoc_ies(u8* f,const scan_ap* ap);
extern int g_wifi_assoc_qos, g_wifi_assoc_ht, g_wifi_ht_enable;

#endif
