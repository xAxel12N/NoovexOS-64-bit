#ifndef NVX_NET_WIFI_WIFI_HW_H
#define NVX_NET_WIFI_WIFI_HW_H
#include "include/nvx/base.h"
#define WIFI_HW_MAX 4
#define WIFI_HW_NATIVE 1u
#define WIFI_HW_RXDEC 2u
#define WIFI_HW_TXENC 4u
#define WIFI_HW_SAE 8u
enum { WIFI_KEY_NONE=0, WIFI_KEY_CCMP=4, WIFI_KEY_BIP=6 };
struct wifi_hw_ops {
    const char* name;
    u32 flags;
    int (*init)(void* ctx);
    int (*scan_start)(void* ctx,const u8* ch,int n);
    int (*scan_done)(void* ctx);
    int (*join)(void* ctx,const u8* bssid,u8 ch,const char* ssid,u8 akm,u8 ht);
    void (*leave)(void* ctx);
    int (*tx)(void* ctx,const u8* frame,int len);
    int (*set_key)(void* ctx,int idx,int type,const u8* key,int len,int pairwise);
    int (*get_rssi)(void* ctx);
    void (*poll)(void* ctx);
    int (*get_mac)(void* ctx,u8* mac);
    int (*tx_eapol)(void* ctx,const u8* bssid,const u8* eapol,int len);
};
int wifi_hw_register(const struct wifi_hw_ops* ops,void* ctx);
void wifi_hw_rx(const u8* frame,int len,int rssi);
void wifi_hw_rx_dec(const u8* frame,int len,int rssi,int decrypted);
int wifi_hw_rx_pop_dec(u8* out,int max,int* rssi,int* decrypted);
const struct wifi_hw_ops* wifi_hw_ops_get(void);
void* wifi_hw_ctx(void);
int wifi_hw_generic(void);
int wifi_hw_init(void);
int wifi_hw_scan_start(const u8* ch,int n);
int wifi_hw_scan_done(void);
int wifi_hw_join(const u8* bssid,u8 ch,const char* ssid,u8 akm,u8 ht);
void wifi_hw_leave(void);
int wifi_hw_tx(const u8* frame,int len);
int wifi_hw_set_key(int idx,int type,const u8* key,int len,int pairwise);
int wifi_hw_rssi(void);
void wifi_hw_poll(void);
int wifi_hw_mac(u8* mac);
int wifi_hw_rx_pop(u8* out,int max,int* rssi);
int wifi_scan_parse(const u8* fr,int flen,u8 ch,int rssi,int onch);
#endif
