#ifndef NVX_NET_WIFI_WPA3_H
#define NVX_NET_WIFI_WPA3_H
#include "include/nvx/base.h"
typedef struct { u8 pwe[65], rnd[32], scalar[32], elem[65], pscalar[32], pelem[65], kck[32], pmk[32], pmkid[16]; } sae_ctx;
extern int g_wpa_akm;
void aes_cmac(const u8 key[16],const u8* m,int len,u8 out[16]);
void wpa_mic(const u8* frame,int len,u8 out[32]);
int wpa_key_ver(void);
void kdf_sha256(const u8* key,int klen,const char* label,const u8* ctx,int clen,u8* out,int bits);
void wpa_derive_ptk_akm(const u8* pmk,const u8* aa,const u8* spa,const u8* an,const u8* sn,u8* ptk);
int sae_derive_pwe(const u8* a1,const u8* a2,const char* pw,u8 pwe[65]);
int sae_commit(sae_ctx* s,const u8* own,const u8* peer,const char* pw);
int sae_process_commit(sae_ctx* s,const u8* ps,const u8* pe);
void sae_confirm(const sae_ctx* s,u16 sc,u8 out[32]);
int sae_check_confirm(const sae_ctx* s,u16 psc,const u8* c);
#endif
