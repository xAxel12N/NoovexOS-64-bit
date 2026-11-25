#ifndef NVX_NET_WIFI_WPA_SM_H
#define NVX_NET_WIFI_WPA_SM_H
#include "include/nvx/base.h"
struct wpa_io {
    void* ctx;
    void (*tx_eapol)(void* ctx,const u8* eapol,int len);
    int (*rx_eapol)(void* ctx,u8* out,int max,int ms);
    int (*set_key)(void* ctx,int idx,int type,const u8* key,int len,int pairwise);
    int (*sae_tx)(void* ctx,const u8* f,int len,int phase,int round);
    int (*rx_mgmt)(void* ctx,u8 want,u8* body,int* blen,int ms);
    void (*sae_phase_ok)(void* ctx,int phase);
    u16* seq;
    u32 wnd_m;
    int qos;
};
extern int g_gtk_state, g_gtk_keyid, g_gtk_len, g_wifi_rekeys, g_wifi_ptk_rekey, g_wifi_prefer_sae;
const char* wpa_gtk_status(void);
int wpa_sm_select(u8 sec,u8 grp,u8 akm,u8 pmf,int eap,int* sae);
void wpa_sm_pmk(const char* ssid,const char* pass,int sae,int eap);
int wpa_sm_sae(const struct wpa_io* io,const u8* bssid,const u8* mac,const char* pass);
int wpa_sm_eap(const struct wpa_io* io,const char* user,const char* pass);
int wpa_sm_4way(const struct wpa_io* io,const u8* aa,const u8* spa);
void wpa_sm_stash_clear(void);
int wpa_rx_eapol(const u8* eap,int el);
int wpa_rx_eapol_p(const u8* eap,int el,int prot);
int wifi_gen_join_secure(const u8* bssid,u8 ch,const char* ssid,u8 sec,u8 grp,u8 akm,u8 pmf,u8 ht,const char* user,const char* pass);
int wifi_hw_assoc_ie(u8* out,int cap);
extern int g_wifi_sae_ext, g_wifi_sae_fail, g_igtk_state, g_igtk_keyid;
int wifi_gen_sae_active(void);
int wifi_gen_sae_auth(const struct wpa_io* io);
#endif
