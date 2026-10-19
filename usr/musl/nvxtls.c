#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nvxtls.h"

struct dbuf { unsigned char* p; size_t n, cap; };
static void dbuf_add(void* ctx, const void* d, size_t n){
    struct dbuf* b = ctx;
    if(b->n + n > b->cap){ size_t c = b->cap ? b->cap * 2 : 1024; while(c < b->n + n)c *= 2; unsigned char* q = realloc(b->p, c); if(!q)return; b->p = q; b->cap = c; }
    memcpy(b->p + b->n, d, n); b->n += n;
}
static int der_get(const unsigned char* p, size_t len, size_t* off, int* tag, const unsigned char** val, size_t* vlen){
    if(*off + 2 > len)return -1;
    int t = p[*off]; size_t l = p[*off + 1], h = 2;
    if(l & 0x80){ int n = (int)(l & 0x7f); if(n < 1 || n > 4 || *off + 2 + (size_t)n > len)return -1; l = 0; for(int i = 0; i < n; i++)l = (l << 8) | p[*off + 2 + i]; h = 2 + (size_t)n; }
    if(*off + h + l > len)return -1;
    *tag = t; *val = p + *off + h; *vlen = l; *off += h + l; return 0;
}
static int spki_to_pkey(const unsigned char* d, size_t n, br_x509_pkey* pk){
    static const unsigned char RSA[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x01}, EC[] = {0x2a,0x86,0x48,0xce,0x3d,0x02,0x01};
    static const unsigned char P256[] = {0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07}, P384[] = {0x2b,0x81,0x04,0x00,0x22}, P521[] = {0x2b,0x81,0x04,0x00,0x23};
    size_t o = 0, o2 = 0, o3 = 0; int t; const unsigned char *v, *alg, *bits, *oid; size_t vl, al, bl, ol;
    if(der_get(d, n, &o, &t, &v, &vl) || t != 0x30)return -1;
    if(der_get(v, vl, &o2, &t, &alg, &al) || t != 0x30)return -1;
    if(der_get(v, vl, &o2, &t, &bits, &bl) || t != 0x03 || bl < 2 || bits[0] != 0)return -1;
    bits++; bl--;
    if(der_get(alg, al, &o3, &t, &oid, &ol) || t != 0x06)return -1;
    if(ol == sizeof RSA && !memcmp(oid, RSA, ol)){
        size_t o4 = 0, o5 = 0; const unsigned char *sq, *nn, *ee; size_t sl, nl, el;
        if(der_get(bits, bl, &o4, &t, &sq, &sl) || t != 0x30)return -1;
        if(der_get(sq, sl, &o5, &t, &nn, &nl) || t != 0x02)return -1;
        if(der_get(sq, sl, &o5, &t, &ee, &el) || t != 0x02)return -1;
        while(nl > 1 && !nn[0]){ nn++; nl--; }
        while(el > 1 && !ee[0]){ ee++; el--; }
        unsigned char* m = malloc(nl + el); if(!m)return -1;
        memcpy(m, nn, nl); memcpy(m + nl, ee, el);
        pk->key_type = BR_KEYTYPE_RSA; pk->key.rsa.n = m; pk->key.rsa.nlen = nl; pk->key.rsa.e = m + nl; pk->key.rsa.elen = el;
        return 0;
    }
    if(ol == sizeof EC && !memcmp(oid, EC, ol)){
        const unsigned char* cv; size_t cl; int curve;
        if(der_get(alg, al, &o3, &t, &cv, &cl) || t != 0x06)return -1;
        if(cl == sizeof P256 && !memcmp(cv, P256, cl))curve = BR_EC_secp256r1;
        else if(cl == sizeof P384 && !memcmp(cv, P384, cl))curve = BR_EC_secp384r1;
        else if(cl == sizeof P521 && !memcmp(cv, P521, cl))curve = BR_EC_secp521r1;
        else return -1;
        unsigned char* m = malloc(bl); if(!m)return -1;
        memcpy(m, bits, bl);
        pk->key_type = BR_KEYTYPE_EC; pk->key.ec.curve = curve; pk->key.ec.q = m; pk->key.ec.qlen = bl;
        return 0;
    }
    return -1;
}
static br_x509_trust_anchor* anchor_slot(nvx_anchors* a){
    if(a->n == a->cap){ size_t c = a->cap ? a->cap * 2 : 128; br_x509_trust_anchor* q = realloc(a->v, c * sizeof *q); if(!q)return NULL; a->v = q; a->cap = c; }
    br_x509_trust_anchor* t = &a->v[a->n]; memset(t, 0, sizeof *t); return t;
}
static int pem_each(const char* path, int (*cb)(void*, const char*, const unsigned char*, size_t), void* ctx){
    FILE* f = fopen(path, "rb"); if(!f)return 0;
    struct dbuf file = {0}; unsigned char tmp[4096]; size_t r;
    while((r = fread(tmp, 1, sizeof tmp, f)) > 0)dbuf_add(&file, tmp, r);
    fclose(f);
    dbuf_add(&file, "\n", 1);
    br_pem_decoder_context pc; br_pem_decoder_init(&pc);
    struct dbuf der = {0}; char name[128] = {0}; int inobj = 0, count = 0; size_t off = 0;
    while(off < file.n){
        off += br_pem_decoder_push(&pc, file.p + off, file.n - off);
        switch(br_pem_decoder_event(&pc)){
        case BR_PEM_BEGIN_OBJ: snprintf(name, sizeof name, "%s", br_pem_decoder_name(&pc)); der.n = 0; br_pem_decoder_setdest(&pc, dbuf_add, &der); inobj = 1; break;
        case BR_PEM_END_OBJ: if(inobj && der.n){ if(cb(ctx, name, der.p, der.n) == 0)count++; } inobj = 0; break;
        case BR_PEM_ERROR: free(der.p); free(file.p); return -1;
        }
    }
    free(der.p); free(file.p);
    return count;
}
struct roots_ctx { nvx_anchors* a; unsigned char* dn; size_t dnlen; };
static int roots_obj(void* c, const char* name, const unsigned char* d, size_t n){
    struct roots_ctx* rc = c;
    if(!strcmp(name, "TRUST ANCHOR NAME")){ free(rc->dn); rc->dn = malloc(n); if(!rc->dn)return -1; memcpy(rc->dn, d, n); rc->dnlen = n; return 1; }
    if(!strcmp(name, "PUBLIC KEY") && rc->dn){
        br_x509_trust_anchor* t = anchor_slot(rc->a); if(!t)return -1;
        if(spki_to_pkey(d, n, &t->pkey)){ free(rc->dn); rc->dn = NULL; return -1; }
        t->dn.data = rc->dn; t->dn.len = rc->dnlen; t->flags = BR_X509_TA_CA; rc->dn = NULL; rc->a->n++;
        return 0;
    }
    return -1;
}
static int cert_obj(void* c, const char* name, const unsigned char* d, size_t n){
    nvx_anchors* a = c;
    if(strcmp(name, "CERTIFICATE") && strcmp(name, "X509 CERTIFICATE"))return -1;
    struct dbuf dn = {0}; br_x509_decoder_context dc;
    br_x509_decoder_init(&dc, dbuf_add, &dn);
    br_x509_decoder_push(&dc, d, n);
    const br_x509_pkey* pk = br_x509_decoder_get_pkey(&dc);
    if(!pk || br_x509_decoder_last_error(&dc)){ free(dn.p); return -1; }
    br_x509_trust_anchor* t = anchor_slot(a); if(!t){ free(dn.p); return -1; }
    t->dn.data = dn.p; t->dn.len = dn.n; t->flags = br_x509_decoder_isCA(&dc) ? BR_X509_TA_CA : 0;
    t->pkey = *pk;
    if(pk->key_type == BR_KEYTYPE_RSA){
        unsigned char* m = malloc(pk->key.rsa.nlen + pk->key.rsa.elen); if(!m){ free(dn.p); return -1; }
        memcpy(m, pk->key.rsa.n, pk->key.rsa.nlen); memcpy(m + pk->key.rsa.nlen, pk->key.rsa.e, pk->key.rsa.elen);
        t->pkey.key.rsa.n = m; t->pkey.key.rsa.e = m + pk->key.rsa.nlen;
    } else {
        unsigned char* m = malloc(pk->key.ec.qlen); if(!m){ free(dn.p); return -1; }
        memcpy(m, pk->key.ec.q, pk->key.ec.qlen); t->pkey.key.ec.q = m;
    }
    a->n++;
    return 0;
}
int nvx_anchors_load(nvx_anchors* a, const char* roots_path, const char* local_ca_path){
    memset(a, 0, sizeof *a);
    struct roots_ctx rc = { a, NULL, 0 };
    int r = roots_path ? pem_each(roots_path, roots_obj, &rc) : 0;
    free(rc.dn);
    int l = local_ca_path ? pem_each(local_ca_path, cert_obj, a) : 0;
    if(r < 0 && l <= 0)return -1;
    return (int)a->n;
}
void nvx_anchors_free(nvx_anchors* a){
    for(size_t i = 0; i < a->n; i++){
        free(a->v[i].dn.data);
        if(a->v[i].pkey.key_type == BR_KEYTYPE_RSA)free(a->v[i].pkey.key.rsa.n); else free(a->v[i].pkey.key.ec.q);
    }
    free(a->v); memset(a, 0, sizeof *a);
}
const char* nvx_tls_error(int err){
    switch(err){
    case BR_ERR_OK: return "no error";
    case BR_ERR_BAD_VERSION: return "the server does not support TLS 1.0 to 1.2";
    case BR_ERR_BAD_CIPHER_SUITE: return "no common cipher suite";
    case BR_ERR_UNEXPECTED: return "unexpected message from the server";
    case BR_ERR_BAD_SIGNATURE: return "bad signature from the server";
    case BR_ERR_IO: return "connection closed";
    case BR_ERR_X509_EXPIRED: return "certificate expired or not yet valid";
    case BR_ERR_X509_BAD_SERVER_NAME: return "certificate is for another name";
    case BR_ERR_X509_NOT_TRUSTED: return "certificate chain ends at an unknown root";
    case BR_ERR_X509_UNSUPPORTED: return "unsupported certificate feature";
    case BR_ERR_X509_BAD_SIGNATURE: return "bad certificate signature";
    case BR_ERR_X509_CRITICAL_EXTENSION: return "unsupported critical extension";
    case BR_ERR_X509_NOT_CA: return "issuer is not a CA";
    case BR_ERR_X509_FORBIDDEN_KEY_USAGE: return "key usage forbids this use";
    case BR_ERR_X509_WEAK_PUBLIC_KEY: return "public key too weak";
    case BR_ERR_X509_TIME_UNKNOWN: return "the clock is not set";
    }
    if(err >= BR_ERR_RECV_FATAL_ALERT)return "the server sent a fatal alert";
    if(err >= BR_ERR_SEND_FATAL_ALERT)return "TLS failure";
    return "TLS or certificate error";
}
