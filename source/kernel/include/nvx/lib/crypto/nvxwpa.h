#ifndef NVX_LIB_CRYPTO_NVXWPA_H
#define NVX_LIB_CRYPTO_NVXWPA_H

#include "include/nvx/base.h"

extern void (*g_wpa_pump)(void);

void sha1_hash(const u8* msg,u32 len,u8 out[20]);
void hmac_sha1(const u8* key,u32 klen,const u8* msg,u32 mlen,u8 out[20]);
void pbkdf2_sha1(const u8* pw,u32 pwlen,const u8* salt,u32 saltlen,u32 iter,u8* out,u32 outlen);
void wpa_prf(const u8* key,u32 klen,const char* label,const u8* data,u32 dlen,u8* out,u32 outlen);
void aes_ccm_auth_encrypt(const u8 key[16],const u8 nonce[13], const u8* aad,int aadlen, u8* data,int dlen,u8 mic[8]);
int aes_ccm_decrypt_verify(const u8 key[16],const u8 nonce[13], const u8* aad,int aadlen, u8* data,int dlen,const u8 mic[8]);
int aes_key_unwrap(const u8 kek[16],const u8* in,int inlen,u8* out);

#endif
