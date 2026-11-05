#ifndef NVX_LIB_CRYPTO_NVXX509_H
#define NVX_LIB_CRYPTO_NVXX509_H

#include "include/nvx/base.h"

typedef struct { const u8* subj; int subjlen; const u8* spki; int spkilen; } x509_anchor;
#define X509_MAX_EXTRA 8

extern int g_x509_nextra;

int x509_verify_chain(const u8* const* certs,const int* lens,int n,const char* host,i64 now);
int x509_pem_next(const char** pp,u8* out,int cap);
int x509_add_anchor_der(const u8* d,int len);
int x509_leaf_cv(const u8* leaf,int len,int scheme,const u8* sig,int siglen,const u8* thash);
const char* x509_reason(void);
int x509_leaf_sig12(const u8* leaf,int len,int scheme,const u8* data,int dlen,const u8* sig,int siglen);
int x509_p256_ecdh(const u8 priv[32],const u8* peer,int plen,u8* out);
int p256_mul(const u8 k32[32],const u8* pt,u8 out[65]);
int p256_add(const u8 a[65],const u8 b[65],u8 out[65]);
void p256_neg(u8 pt[65]);
int p256_y_from_x(const u8 x32[32],u8 y32[32]);
void p256_modn_add(const u8 a[32],const u8 b[32],u8 out[32]);
void p256_modn(u8 a[32]);
void p256_prime(u8 out[32]);
void p256_order(u8 out[32]);

#endif
