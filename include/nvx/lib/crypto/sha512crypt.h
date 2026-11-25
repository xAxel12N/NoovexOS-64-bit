#ifndef NVX_LIB_CRYPTO_SHA512CRYPT_H
#define NVX_LIB_CRYPTO_SHA512CRYPT_H

#include "include/nvx/base.h"

int sha512_crypt(const char* key,const char* setting,char* out,int cap);
int sha512_crypt_check(const char* key,const char* hash);
void sha512_crypt_salt(char* out,int n);
u32 sha512_crypt_rounds(const char* hash);
#define SHA512_CRYPT_ROUNDS 100000u

#endif
