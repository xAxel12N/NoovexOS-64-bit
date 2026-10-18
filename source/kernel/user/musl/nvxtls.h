#ifndef NVXTLS_USER_H
#define NVXTLS_USER_H

#include <stddef.h>
#include "bearssl.h"

typedef struct {
    br_x509_trust_anchor* v;
    size_t n, cap;
} nvx_anchors;

int nvx_anchors_load(nvx_anchors* a, const char* roots_path, const char* local_ca_path);
void nvx_anchors_free(nvx_anchors* a);
const char* nvx_tls_error(int err);

#endif
