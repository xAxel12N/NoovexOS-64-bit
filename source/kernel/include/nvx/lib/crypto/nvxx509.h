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

#endif
