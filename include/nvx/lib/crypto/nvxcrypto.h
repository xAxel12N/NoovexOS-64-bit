#ifndef NVX_LIB_CRYPTO_NVXCRYPTO_H
#define NVX_LIB_CRYPTO_NVXCRYPTO_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"

typedef struct { u32 h[8]; u64 len; u8 buf[64]; int n; } sha256;

void sha256_init(sha256*c);
void sha256_update(sha256*c,const u8*p,int len);
void sha256_final(sha256*c,u8*out);
void sha256_hash(const u8*p,int len,u8*out);
void hmac_sha256(const u8*key,int klen,const u8*msg,int mlen,u8*out);
void hkdf_extract(const u8*salt,int slen,const u8*ikm,int ilen,u8*prk);
void hkdf_label(const u8*secret,const char*label,const u8*ctx,int ctxlen,u8*out,int L);
void chacha_block(const u32 in[16],u32 out[16]);
void aead_seal(const u8 key[32],const u8 nonce[12],const u8*aad,int aadlen, const u8*pt,int ptlen,u8*ct,u8 tag[16]);
int aead_open(const u8 key[32],const u8 nonce[12],const u8*aad,int aadlen, const u8*ct,int ctlen,const u8 tag[16],u8*pt);
void x25519(u8*out,const u8*scalar,const u8*point);

#endif
