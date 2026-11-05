#ifndef NVX_NET_WIFI_EAP_H
#define NVX_NET_WIFI_EAP_H
#include "include/nvx/base.h"
typedef int (*eap_tx_fn)(void* ctx,const u8* eap,int len);
typedef int (*eap_rx_fn)(void* ctx,u8* eap,int cap);
extern int g_eap_stage; extern const char* g_eap_why; extern u8 g_eap_pin[32], g_eap_pin_new[32]; extern int g_eap_pin_set;
void md4_hash(const u8* m,int len,u8 out[16]);
void des_encrypt(const u8 key8[8],const u8 in[8],u8 out[8]);
void mschap_challenge_hash(const u8 peer[16],const u8 auth[16],const char* user,u8 out[8]);
void mschap_pw_hash(const char* pw,u8 out[16]);
void mschap_nt_response(const u8 auth[16],const u8 peer[16],const char* user,const char* pw,u8 out[24]);
void mschap_auth_response(const char* pw,const u8 nt[24],const u8 peer[16],const u8 auth[16],const char* user,char out[43]);
int eap_peap_run(eap_tx_fn tx,eap_rx_fn rx,void* ctx,const char* user,const char* pass,u8 msk[64]);
#endif
