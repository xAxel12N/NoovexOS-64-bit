#ifndef NVX_LIB_CRYPTO_NVXCRYPTO2_H
#define NVX_LIB_CRYPTO_NVXCRYPTO2_H

#include "include/nvx/base.h"

typedef struct { u64 h[8]; u64 len; u8 buf[128]; int n; } sha512_ctx;
void sha512_init(sha512_ctx* c);
void sha512_update(sha512_ctx* c,const u8* d,u64 len);
void sha512_final(sha512_ctx* c,u8* out,int outlen);
void sha512_hash(const u8* d,int len,u8 out[64]);
void sha384_hash(const u8* d,int len,u8 out[48]);
void aes256_expand(const u8 key[32],u8 rk[240]);
void aes256_enc(const u8 rk[240],const u8 in[16],u8 out[16]);
void csprng_bytes(u8* buf,int len);

#endif
