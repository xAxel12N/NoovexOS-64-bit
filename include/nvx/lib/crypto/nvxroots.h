#ifndef NVX_LIB_CRYPTO_NVXROOTS_H
#define NVX_LIB_CRYPTO_NVXROOTS_H

#include "include/nvx/base.h"
#include "include/nvx/lib/crypto/nvxx509.h"

extern const char* const nvx_root_names[];
int nvx_root_get(int i,x509_anchor* a);

#endif
