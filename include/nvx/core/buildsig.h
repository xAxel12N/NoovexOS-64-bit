#ifndef NVX_CORE_BUILDSIG_H
#define NVX_CORE_BUILDSIG_H

#include "include/nvx/base.h"
#include "include/nvx/lib/crypto/nvxcrypto.h"

struct nvx_g_buildsig_t {
    char     magic[8];       /* "NVX64SIG" */
    char     version[16];    /* e.g. "8.38" */
    unsigned int  build_epoch;    /* unix time, patched post-link */
    unsigned int  image_size;     /* flat image size, patched post-link */
    unsigned char sha256[32];     /* SHA-256 of the image (with these 32 bytes zeroed), patched */
    char     builder[16];    /* "NoovexOS build" */
    unsigned long long uefi_entry; /* physical addr of uefi_entry, patched post-link */
    unsigned char sha256_full[32];
    unsigned long long bss_end;
};

extern char __bss_end[];
extern __attribute__((aligned(64), section(".sig"))) volatile const struct nvx_g_buildsig_t g_buildsig;

#endif
