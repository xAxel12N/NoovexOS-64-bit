#ifndef NVX_LIB_CRYPTO_NVXTLS_H
#define NVX_LIB_CRYPTO_NVXTLS_H

#include "include/nvx/base.h"

typedef struct {
    int (*wr)(void*ctx,const u8*,int);
    int (*rd)(void*ctx,u8*,int);
    void* ctx;
    u8 rbuf[18432]; int rlen, rpos;
} tls_io;
extern tls_io g_tls_io;

extern const u8 SB[256];
extern int g_tls_insecure;
extern const char* g_tls_why;

u8 xtime(u8 x);
void aes128_expand(const u8 key[16],u8 rk[176]);
void aes128_enc(const u8 rk[176],const u8 in[16],u8 out[16]);
extern volatile int g_net_rx;
int tls_get(tls_io*io,const char*host,int hl,const u8 rnd[64], const char*req,int reqlen,u8*out,int maxout);
typedef struct { u16 cipher; u8 ckey[32], civ[12], skey[32], siv[12], csec[32], ssec[32]; u64 cseq, sseq; int ok, klen, v12; } tls_sess;
int tls_connect_sess(tls_io*io,const char*host,int hl,const u8 rnd[64],tls_sess*ss);
int tls_sess_write(tls_io*io,tls_sess*ss,const u8*data,int len);
int tls_sess_read(tls_io*io,tls_sess*ss,u8*out,int max);
void tls_sess_wipe(tls_sess*ss);
extern int g_tls_only12, g_tls_pin; extern u8 g_tls_leaf[32]; extern u8 g_tls12_ms[48], g_tls12_crnd[32], g_tls12_srnd[32];
void tls12_prf(const u8*sec,int sl,const char*label,const u8*seed,int seedl,u8*out,int outl);

#endif
