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
int tls_get(tls_io*io,const char*host,int hl,const u8 rnd[64], const char*req,int reqlen,u8*out,int maxout);

#endif
