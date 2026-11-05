#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include "third_party/dav1d/include/dav1d/dav1d.h"

int imgx_decode(const unsigned char* d, int len, unsigned** argb, int* ow, int* oh);

#ifndef IMGX_HOST
long long llabs(long long v) { return v < 0 ? -v : v; }
int prctl(int o, ...) { (void)o; return -1; }
int pthread_attr_init(void* a) { (void)a; return 0; }
int pthread_attr_destroy(void* a) { (void)a; return 0; }
int pthread_attr_setstacksize(void* a, size_t s) { (void)a; (void)s; return 0; }
int pthread_create(void* t, const void* a, void* (*fn)(void*), void* arg) { (void)t; (void)a; (void)fn; (void)arg; return EAGAIN; }
int pthread_join(unsigned long t, void** r) { (void)t; (void)r; return 0; }
int feof(FILE* f) { (void)f; return 1; }
char* strerror(int e) { (void)e; return "error"; }

static void qs_swap(unsigned char* a, unsigned char* b, size_t n) { while (n--) { unsigned char t = *a; *a++ = *b; *b++ = t; } }
static void qs_sift(unsigned char* base, size_t root, size_t n, size_t sz, int (*cmp)(const void*, const void*)) {
    for (;;) {
        size_t c = root * 2 + 1;
        if (c >= n) return;
        if (c + 1 < n && cmp(base + c * sz, base + (c + 1) * sz) < 0) c++;
        if (cmp(base + root * sz, base + c * sz) >= 0) return;
        qs_swap(base + root * sz, base + c * sz, sz);
        root = c;
    }
}
void qsort(void* b, size_t n, size_t sz, int (*cmp)(const void*, const void*)) {
    unsigned char* base = b;
    if (n < 2 || !sz) return;
    if (n <= 12) {
        for (size_t i = 1; i < n; i++) for (size_t j = i; j > 0 && cmp(base + (j - 1) * sz, base + j * sz) > 0; j--) qs_swap(base + (j - 1) * sz, base + j * sz, sz);
        return;
    }
    for (size_t i = n / 2; i-- > 0;) qs_sift(base, i, n, sz, cmp);
    for (size_t e = n - 1; e > 0; e--) { qs_swap(base, base + e * sz, sz); qs_sift(base, 0, e, sz, cmp); }
}
#endif

static void* imgx_jb[5];
static void imgx_j40_trap(void) { __builtin_longjmp(imgx_jb, 1); }
#define J40_ASSERT_HOOK() imgx_j40_trap()
#define J40_IMPLEMENTATION
#define J40_CONFIRM_THAT_THIS_IS_EXPERIMENTAL_AND_POTENTIALLY_UNSAFE
#include "third_party/j40/j40.h"

#define IX_MAXPX (16384L * 16384L)
#define IX_ITEMS 512
#define IX_PROPS 256
#define IX_EXT 8

typedef struct { uint32_t id; uint32_t type; int method; int next; uint64_t off[IX_EXT]; uint64_t len[IX_EXT]; int props[16]; int nprops; uint32_t dimg[256]; int ndimg; uint32_t auxl_to; int has_auxl; } ix_item;
typedef struct { uint32_t type; const unsigned char* p; uint32_t n; } ix_prop;
typedef struct {
    const unsigned char* d; uint64_t len;
    ix_item it[IX_ITEMS]; int nit;
    ix_prop pr[IX_PROPS]; int npr;
    uint32_t primary;
    const unsigned char* idat; uint64_t idatn;
} ix_ctx;

static uint32_t be16(const unsigned char* p) { return ((uint32_t)p[0] << 8) | p[1]; }
static uint32_t be32(const unsigned char* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint64_t ben(const unsigned char* p, int n) { uint64_t v = 0; for (int i = 0; i < n; i++) v = (v << 8) | p[i]; return v; }
#define FCC(a, b, c, e) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(e))

static int box_next(const unsigned char* p, const unsigned char* end, uint32_t* type, const unsigned char** body, const unsigned char** next) {
    if (end - p < 8) return 0;
    uint64_t sz = be32(p);
    *type = be32(p + 4);
    int hl = 8;
    if (sz == 1) { if (end - p < 16) return 0; sz = ben(p + 8, 8); hl = 16; }
    else if (sz == 0) sz = (uint64_t)(end - p);
    if (sz < (uint64_t)hl || sz > (uint64_t)(end - p)) return 0;
    *body = p + hl;
    *next = p + sz;
    return 1;
}

static ix_item* item_get(ix_ctx* c, uint32_t id, int create) {
    for (int i = 0; i < c->nit; i++) if (c->it[i].id == id) return &c->it[i];
    if (!create || c->nit >= IX_ITEMS) return 0;
    ix_item* t = &c->it[c->nit++];
    memset(t, 0, sizeof(*t));
    t->id = id;
    return t;
}

static void parse_iinf(ix_ctx* c, const unsigned char* p, const unsigned char* e) {
    if (e - p < 6) return;
    int v = p[0];
    p += 4;
    p += v == 0 ? 2 : 4;
    uint32_t t; const unsigned char *b, *n;
    while (box_next(p, e, &t, &b, &n)) {
        if (t == FCC('i', 'n', 'f', 'e') && n - b >= 4) {
            int iv = b[0];
            const unsigned char* q = b + 4;
            if (iv >= 2) {
                uint32_t id;
                if (iv == 2) { if (n - q < 8) goto nx; id = be16(q); q += 2; }
                else { if (n - q < 10) goto nx; id = be32(q); q += 4; }
                q += 2;
                ix_item* it = item_get(c, id, 1);
                if (it) it->type = be32(q);
            }
        }
nx:
        p = n;
    }
}

static void parse_iloc(ix_ctx* c, const unsigned char* p, const unsigned char* e) {
    if (e - p < 8) return;
    int v = p[0];
    p += 4;
    int os = p[0] >> 4, ls = p[0] & 15, bs = p[1] >> 4, is = (v == 1 || v == 2) ? (p[1] & 15) : 0;
    p += 2;
    uint32_t cnt;
    if (v < 2) { cnt = be16(p); p += 2; } else { cnt = be32(p); p += 4; }
    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t id;
        if (v < 2) { if (e - p < 2) return; id = be16(p); p += 2; } else { if (e - p < 4) return; id = be32(p); p += 4; }
        int method = 0;
        if (v == 1 || v == 2) { if (e - p < 2) return; method = be16(p) & 15; p += 2; }
        if (e - p < 2 + bs + 2) return;
        p += 2;
        uint64_t base = ben(p, bs); p += bs;
        uint32_t ne = be16(p); p += 2;
        ix_item* it = item_get(c, id, 1);
        for (uint32_t k = 0; k < ne; k++) {
            if (e - p < is + os + ls) return;
            p += is;
            uint64_t off = ben(p, os); p += os;
            uint64_t l = ben(p, ls); p += ls;
            if (it && it->next < IX_EXT) { it->off[it->next] = base + off; it->len[it->next] = l; it->next++; }
        }
        if (it) it->method = method;
    }
}

static void parse_iprp(ix_ctx* c, const unsigned char* p, const unsigned char* e) {
    uint32_t t; const unsigned char *b, *n;
    while (box_next(p, e, &t, &b, &n)) {
        if (t == FCC('i', 'p', 'c', 'o')) {
            uint32_t t2; const unsigned char *b2, *n2; const unsigned char* q = b;
            while (box_next(q, n, &t2, &b2, &n2)) { if (c->npr < IX_PROPS) { c->pr[c->npr].type = t2; c->pr[c->npr].p = b2; c->pr[c->npr].n = (uint32_t)(n2 - b2); c->npr++; } q = n2; }
        } else if (t == FCC('i', 'p', 'm', 'a') && n - b >= 8) {
            int v = b[0], fl = b[3];
            const unsigned char* q = b + 4;
            uint32_t cnt = be32(q); q += 4;
            for (uint32_t i = 0; i < cnt; i++) {
                uint32_t id;
                if (v < 1) { if (n - q < 3) break; id = be16(q); q += 2; } else { if (n - q < 5) break; id = be32(q); q += 4; }
                int na = *q++;
                ix_item* it = item_get(c, id, 1);
                for (int k = 0; k < na; k++) {
                    int idx;
                    if (fl & 1) { if (n - q < 2) return; idx = (int)(be16(q) & 0x7FFF); q += 2; } else { if (n - q < 1) return; idx = q[0] & 0x7F; q++; }
                    if (it && idx > 0 && it->nprops < 16) it->props[it->nprops++] = idx - 1;
                }
            }
        }
        p = n;
    }
}

static void parse_iref(ix_ctx* c, const unsigned char* p, const unsigned char* e) {
    if (e - p < 4) return;
    int v = p[0];
    p += 4;
    uint32_t t; const unsigned char *b, *n;
    while (box_next(p, e, &t, &b, &n)) {
        const unsigned char* q = b;
        int w = v == 0 ? 2 : 4;
        if (n - q >= w + 2) {
            uint32_t from = (uint32_t)ben(q, w); q += w;
            uint32_t rc = be16(q); q += 2;
            ix_item* it = item_get(c, from, 1);
            for (uint32_t k = 0; k < rc && n - q >= w; k++) {
                uint32_t to = (uint32_t)ben(q, w); q += w;
                if (!it) continue;
                if (t == FCC('d', 'i', 'm', 'g') && it->ndimg < 256) it->dimg[it->ndimg++] = to;
                else if (t == FCC('a', 'u', 'x', 'l') && !it->has_auxl) { it->auxl_to = to; it->has_auxl = 1; }
            }
        }
        p = n;
    }
}

static int parse_meta(ix_ctx* c, const unsigned char* p, const unsigned char* e) {
    if (e - p < 4) return -1;
    p += 4;
    uint32_t t; const unsigned char *b, *n;
    while (box_next(p, e, &t, &b, &n)) {
        if (t == FCC('p', 'i', 't', 'm') && n - b >= 6) c->primary = b[0] == 0 ? be16(b + 4) : (n - b >= 8 ? be32(b + 4) : 0);
        else if (t == FCC('i', 'i', 'n', 'f')) parse_iinf(c, b, n);
        else if (t == FCC('i', 'l', 'o', 'c')) parse_iloc(c, b, n);
        else if (t == FCC('i', 'p', 'r', 'p')) parse_iprp(c, b, n);
        else if (t == FCC('i', 'r', 'e', 'f')) parse_iref(c, b, n);
        else if (t == FCC('i', 'd', 'a', 't')) { c->idat = b; c->idatn = (uint64_t)(n - b); }
        p = n;
    }
    return c->primary ? 0 : -1;
}

static const ix_prop* item_prop(ix_ctx* c, ix_item* it, uint32_t type) {
    for (int i = 0; i < it->nprops; i++) { int k = it->props[i]; if (k >= 0 && k < c->npr && c->pr[k].type == type) return &c->pr[k]; }
    return 0;
}

static unsigned char* item_data(ix_ctx* c, ix_item* it, size_t* outn) {
    uint64_t tot = 0;
    for (int i = 0; i < it->next; i++) tot += it->len[i];
    if (!it->next || tot == 0 || tot > (256u << 20)) return 0;
    unsigned char* o = malloc((size_t)tot);
    if (!o) return 0;
    uint64_t at = 0;
    for (int i = 0; i < it->next; i++) {
        const unsigned char* src; uint64_t lim;
        if (it->method == 1) { src = c->idat; lim = c->idatn; } else if (it->method == 0) { src = c->d; lim = c->len; } else { free(o); return 0; }
        if (!src || it->off[i] > lim || it->len[i] > lim - it->off[i]) { free(o); return 0; }
        memcpy(o + at, src + it->off[i], (size_t)it->len[i]);
        at += it->len[i];
    }
    *outn = (size_t)tot;
    return o;
}

typedef struct { int w, h, bpc, layout, full, mtrx; uint8_t* y; uint8_t* u; uint8_t* v; ptrdiff_t ys, cs; Dav1dPicture pic; } ix_av1;

static int av1_decode(const unsigned char* data, size_t n, ix_av1* out) {
    Dav1dSettings s;
    dav1d_default_settings(&s);
    s.n_threads = 1;
    s.max_frame_delay = 1;
    s.all_layers = 0;
    s.logger.callback = 0;
    Dav1dContext* ctx = 0;
    if (dav1d_open(&ctx, &s) < 0 || !ctx) return -1;
    Dav1dData dd;
    memset(&dd, 0, sizeof(dd));
    uint8_t* buf = dav1d_data_create(&dd, n);
    if (!buf) { dav1d_close(&ctx); return -1; }
    memcpy(buf, data, n);
    memset(&out->pic, 0, sizeof(out->pic));
    int got = 0;
    for (int guard = 0; guard < 64 && !got; guard++) {
        if (dd.sz > 0) {
            int r = dav1d_send_data(ctx, &dd);
            if (r < 0 && r != DAV1D_ERR(EAGAIN)) break;
        }
        int r = dav1d_get_picture(ctx, &out->pic);
        if (r == 0) { got = 1; break; }
        if (r != DAV1D_ERR(EAGAIN)) break;
        if (dd.sz == 0) {
            r = dav1d_get_picture(ctx, &out->pic);
            if (r == 0) got = 1;
            else if (r != DAV1D_ERR(EAGAIN)) break;
        }
    }
    if (dd.sz > 0) dav1d_data_unref(&dd);
    dav1d_close(&ctx);
    if (!got) return -1;
    Dav1dPicture* p = &out->pic;
    out->w = p->p.w; out->h = p->p.h; out->bpc = p->p.bpc; out->layout = p->p.layout;
    out->y = p->data[0]; out->u = p->data[1]; out->v = p->data[2]; out->ys = p->stride[0]; out->cs = p->stride[1];
    out->full = p->seq_hdr ? p->seq_hdr->color_range : 0;
    out->mtrx = p->seq_hdr ? (int)p->seq_hdr->mtrx : 2;
    if (out->w <= 0 || out->h <= 0 || (long)out->w * out->h > IX_MAXPX) { dav1d_picture_unref(p); return -1; }
    return 0;
}

static inline int samp(const uint8_t* row, int x, int hi) { return hi ? ((const uint16_t*)row)[x] : row[x]; }
static inline int clamp8(float v) { return v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5f); }

static float chroma_at(const uint8_t* plane, ptrdiff_t stride, int cw, int ch, int x, int y, int sx, int sy, int hi) {
    int x0, x1, y0, y1;
    float fx, fy;
    if (sx) { if (x & 1) { x0 = x >> 1; x1 = x0 + 1; fx = 0.25f; } else { x1 = x >> 1; x0 = x1 - 1; fx = 0.75f; } } else { x0 = x1 = x; fx = 0; }
    if (sy) { if (y & 1) { y0 = y >> 1; y1 = y0 + 1; fy = 0.25f; } else { y1 = y >> 1; y0 = y1 - 1; fy = 0.75f; } } else { y0 = y1 = y; fy = 0; }
    if (x0 < 0) x0 = 0;
    if (x1 >= cw) x1 = cw - 1;
    if (y0 < 0) y0 = 0;
    if (y1 >= ch) y1 = ch - 1;
    const uint8_t* r0 = plane + (ptrdiff_t)y0 * stride;
    const uint8_t* r1 = plane + (ptrdiff_t)y1 * stride;
    float a = (float)samp(r0, x0, hi) * (1 - fx) + (float)samp(r0, x1, hi) * fx;
    float b = (float)samp(r1, x0, hi) * (1 - fx) + (float)samp(r1, x1, hi) * fx;
    return a * (1 - fy) + b * fy;
}

static void yuv_to_argb(ix_av1* a, int mtrx, int full, unsigned* dst, int dstride, int dx, int dy, int maxw, int maxh) {
    int hi = a->bpc > 8;
    float mx = (float)((1 << a->bpc) - 1);
    float yoff = full ? 0 : 16.0f * (1 << (a->bpc - 8));
    float yscale = full ? 255.0f / mx : 255.0f / (219.0f * (1 << (a->bpc - 8)));
    float coff = (float)(1 << (a->bpc - 1));
    float cscale = full ? 255.0f / mx : 255.0f / (224.0f * (1 << (a->bpc - 8)));
    float kr = 0.299f, kb = 0.114f;
    if (mtrx == 1) { kr = 0.2126f; kb = 0.0722f; }
    else if (mtrx == 9 || mtrx == 10) { kr = 0.2627f; kb = 0.0593f; }
    else if (mtrx == 4) { kr = 0.30f; kb = 0.11f; }
    else if (mtrx == 7) { kr = 0.212f; kb = 0.087f; }
    float kg = 1.0f - kr - kb;
    float rv = 2.0f * (1.0f - kr), bu = 2.0f * (1.0f - kb), gu = -2.0f * kb * (1.0f - kb) / kg, gv = -2.0f * kr * (1.0f - kr) / kg;
    int sx = a->layout == DAV1D_PIXEL_LAYOUT_I420 || a->layout == DAV1D_PIXEL_LAYOUT_I422;
    int sy = a->layout == DAV1D_PIXEL_LAYOUT_I420;
    int mono = a->layout == DAV1D_PIXEL_LAYOUT_I400;
    int cw = (a->w + sx) >> sx, ch = (a->h + sy) >> sy;
    for (int y = 0; y < a->h; y++) {
        int oy = dy + y;
        if (oy < 0 || oy >= maxh) continue;
        const uint8_t* yr = a->y + (ptrdiff_t)y * a->ys;
        unsigned* o = dst + (ptrdiff_t)oy * dstride;
        for (int x = 0; x < a->w; x++) {
            int ox = dx + x;
            if (ox < 0 || ox >= maxw) continue;
            float Y = ((float)samp(yr, x, hi) - yoff) * yscale;
            int r, g, b;
            if (mono) { r = g = b = clamp8(Y); }
            else {
                float u0 = chroma_at(a->u, a->cs, cw, ch, x, y, sx, sy, hi), v0 = chroma_at(a->v, a->cs, cw, ch, x, y, sx, sy, hi);
                float U = (u0 - coff) * cscale;
                float V = (v0 - coff) * cscale;
                if (mtrx == 0) { g = clamp8(Y); b = clamp8((u0 - yoff) * yscale); r = clamp8((v0 - yoff) * yscale); }
                else { r = clamp8(Y + rv * V); g = clamp8(Y + gu * U + gv * V); b = clamp8(Y + bu * U); }
            }
            o[ox] = 0xFF000000u | ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
        }
    }
}

static void alpha_apply(ix_av1* a, unsigned* dst, int dstride, int dx, int dy, int maxw, int maxh) {
    int hi = a->bpc > 8;
    float mx = (float)((1 << a->bpc) - 1);
    float off = a->full ? 0 : 16.0f * (1 << (a->bpc - 8));
    float sc = a->full ? 255.0f / mx : 255.0f / (219.0f * (1 << (a->bpc - 8)));
    for (int y = 0; y < a->h; y++) {
        int oy = dy + y;
        if (oy < 0 || oy >= maxh) continue;
        const uint8_t* yr = a->y + (ptrdiff_t)y * a->ys;
        unsigned* o = dst + (ptrdiff_t)oy * dstride;
        for (int x = 0; x < a->w; x++) {
            int ox = dx + x;
            if (ox < 0 || ox >= maxw) continue;
            unsigned al = (unsigned)clamp8(((float)samp(yr, x, hi) - off) * sc);
            o[ox] = (o[ox] & 0x00FFFFFFu) | (al << 24);
        }
    }
}

static void color_of(ix_ctx* c, ix_item* it, int* mtrx, int* full) {
    const ix_prop* pc = item_prop(c, it, FCC('c', 'o', 'l', 'r'));
    if (pc && pc->n >= 11 && be32(pc->p) == FCC('n', 'c', 'l', 'x')) { *mtrx = (int)be16(pc->p + 8); *full = pc->p[10] >> 7; }
    if (*mtrx == 2) *mtrx = 6;
}

static int item_decode_into(ix_ctx* c, ix_item* it, unsigned* dst, int dw, int dh, int dx, int dy, int is_alpha) {
    if (it->type != FCC('a', 'v', '0', '1')) return -1;
    size_t n = 0;
    unsigned char* data = item_data(c, it, &n);
    if (!data) return -1;
    ix_av1 a;
    int r = av1_decode(data, n, &a);
    free(data);
    if (r < 0) return -1;
    if (is_alpha) alpha_apply(&a, dst, dw, dx, dy, dw, dh);
    else { int m = a.mtrx, f = a.full; color_of(c, it, &m, &f); yuv_to_argb(&a, m, f, dst, dw, dx, dy, dw, dh); }
    dav1d_picture_unref(&a.pic);
    return 0;
}

static int item_size(ix_ctx* c, ix_item* it, int* w, int* h) {
    if (it->type == FCC('g', 'r', 'i', 'd')) {
        size_t n = 0;
        unsigned char* g = item_data(c, it, &n);
        if (!g || n < 8) { free(g); return -1; }
        int big = g[1] & 1;
        if (big && n < 12) { free(g); return -1; }
        *w = big ? (int)be32(g + 4) : (int)be16(g + 4);
        *h = big ? (int)be32(g + 8) : (int)be16(g + 6);
        free(g);
        return 0;
    }
    const ix_prop* ps = item_prop(c, it, FCC('i', 's', 'p', 'e'));
    if (!ps || ps->n < 12) return -1;
    *w = (int)be32(ps->p + 4);
    *h = (int)be32(ps->p + 8);
    return 0;
}

static int decode_item(ix_ctx* c, ix_item* it, unsigned* dst, int w, int h, int is_alpha) {
    if (it->type != FCC('g', 'r', 'i', 'd')) return item_decode_into(c, it, dst, w, h, 0, 0, is_alpha);
    size_t n = 0;
    unsigned char* g = item_data(c, it, &n);
    if (!g || n < 8) { free(g); return -1; }
    int rows = g[2] + 1, cols = g[3] + 1;
    free(g);
    if (it->ndimg < rows * cols) return -1;
    ix_item* t0 = item_get(c, it->dimg[0], 0);
    int tw = 0, th = 0;
    if (!t0 || item_size(c, t0, &tw, &th) < 0 || tw <= 0 || th <= 0) return -1;
    int ok = 0;
    for (int r = 0; r < rows; r++) for (int k = 0; k < cols; k++) {
        ix_item* ti = item_get(c, it->dimg[r * cols + k], 0);
        if (ti && item_decode_into(c, ti, dst, w, h, k * tw, r * th, is_alpha) == 0) ok++;
    }
    return ok ? 0 : -1;
}

static int is_alpha_aux(ix_ctx* c, ix_item* it) {
    const ix_prop* a = item_prop(c, it, FCC('a', 'u', 'x', 'C'));
    if (!a || a->n < 5) return 0;
    const char* s = (const char*)a->p + 4;
    int l = (int)a->n - 4;
    static const char* k1 = "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha";
    static const char* k2 = "urn:mpeg:hevc:2015:auxid:1";
    int l1 = (int)strlen(k1), l2 = (int)strlen(k2);
    return (l >= l1 && !memcmp(s, k1, (size_t)l1)) || (l >= l2 && !memcmp(s, k2, (size_t)l2));
}

static unsigned* transform(ix_ctx* c, ix_item* it, unsigned* px, int* w, int* h) {
    const ix_prop* ir = item_prop(c, it, FCC('i', 'r', 'o', 't'));
    const ix_prop* im = item_prop(c, it, FCC('i', 'm', 'i', 'r'));
    int rot = ir && ir->n >= 1 ? (ir->p[0] & 3) : 0;
    if (rot) {
        int W = *w, H = *h, nw = rot == 2 ? W : H, nh = rot == 2 ? H : W;
        unsigned* o = malloc((size_t)W * (size_t)H * 4);
        if (o) {
            for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
                int X, Y;
                if (rot == 1) { X = y; Y = W - 1 - x; }
                else if (rot == 2) { X = W - 1 - x; Y = H - 1 - y; }
                else { X = H - 1 - y; Y = x; }
                o[(size_t)Y * nw + X] = px[(size_t)y * W + x];
            }
            free(px); px = o; *w = nw; *h = nh;
        }
    }
    if (im && im->n >= 1) {
        int axis = im->p[0] & 1, W = *w, H = *h;
        if (!axis) { for (int y = 0; y < H; y++) for (int x = 0; x < W / 2; x++) { unsigned t = px[(size_t)y * W + x]; px[(size_t)y * W + x] = px[(size_t)y * W + W - 1 - x]; px[(size_t)y * W + W - 1 - x] = t; } }
        else { for (int y = 0; y < H / 2; y++) for (int x = 0; x < W; x++) { unsigned t = px[(size_t)y * W + x]; px[(size_t)y * W + x] = px[(size_t)(H - 1 - y) * W + x]; px[(size_t)(H - 1 - y) * W + x] = t; } }
    }
    return px;
}

static int avif_decode(const unsigned char* d, int len, unsigned** argb, int* ow, int* oh) {
    ix_ctx* c = calloc(1, sizeof(ix_ctx));
    if (!c) return -2;
    c->d = d; c->len = (uint64_t)len;
    const unsigned char* p = d; const unsigned char* e = d + len;
    uint32_t t; const unsigned char *b, *n;
    int have = -1;
    while (box_next(p, e, &t, &b, &n)) { if (t == FCC('m', 'e', 't', 'a')) have = parse_meta(c, b, n); p = n; }
    if (have < 0) { free(c); return -2; }
    ix_item* pi = item_get(c, c->primary, 0);
    int w = 0, h = 0;
    if (!pi || item_size(c, pi, &w, &h) < 0 || w <= 0 || h <= 0 || (long)w * h > IX_MAXPX) { free(c); return -2; }
    unsigned* px = malloc((size_t)w * (size_t)h * 4);
    if (!px) { free(c); return -2; }
    for (long i = 0; i < (long)w * h; i++) px[i] = 0xFF000000u;
    if (decode_item(c, pi, px, w, h, 0) < 0) { free(px); free(c); return -2; }
    for (int i = 0; i < c->nit; i++) {
        ix_item* ai = &c->it[i];
        if (ai->has_auxl && ai->auxl_to == pi->id && is_alpha_aux(c, ai)) { decode_item(c, ai, px, w, h, 1); break; }
    }
    px = transform(c, pi, px, &w, &h);
    free(c);
    *argb = px; *ow = w; *oh = h;
    return 0;
}

static int jxl_decode(const unsigned char* d, int len, unsigned** argb, int* ow, int* oh) {
    static j40_image img;
    memset(&img, 0, sizeof(img));
    unsigned char* copy = malloc((size_t)len);
    if (!copy) return -2;
    memcpy(copy, d, (size_t)len);
    static volatile int rc;
    rc = -2;
    if (__builtin_setjmp(imgx_jb)) return -2;
    if (j40_from_memory(&img, copy, (size_t)len, free) == 0 && j40_output_format(&img, J40_RGBA, J40_U8X4) == 0 && j40_next_frame(&img)) {
        j40_frame fr = j40_current_frame(&img);
        j40_pixels_u8x4 pix = j40_frame_pixels_u8x4(&fr, J40_RGBA);
        if (!j40_error(&img) && pix.width > 0 && pix.height > 0 && (long)pix.width * pix.height <= IX_MAXPX) {
            unsigned* o = malloc((size_t)pix.width * (size_t)pix.height * 4);
            if (o) {
                for (int y = 0; y < pix.height; y++) {
                    const j40_u8x4* row = j40_row_u8x4(pix, y);
                    for (int x = 0; x < pix.width; x++) { const uint8_t* q = row[x]; o[(size_t)y * pix.width + x] = ((unsigned)q[3] << 24) | ((unsigned)q[0] << 16) | ((unsigned)q[1] << 8) | q[2]; }
                }
                *argb = o; *ow = pix.width; *oh = pix.height; rc = 0;
            }
        }
    }
    j40_free(&img);
    return rc;
}

static int is_avif(const unsigned char* d, int len) {
    if (len < 16 || be32(d + 4) != FCC('f', 't', 'y', 'p')) return 0;
    uint32_t sz = be32(d);
    if (sz < 16 || sz > (uint32_t)len) return 0;
    for (uint32_t o = 8; o + 4 <= sz; o += 4) { if (o == 12) continue; uint32_t br = be32(d + o); if (br == FCC('a', 'v', 'i', 'f') || br == FCC('a', 'v', 'i', 's')) return 1; }
    return 0;
}

static int is_jxl(const unsigned char* d, int len) {
    if (len >= 2 && d[0] == 0xFF && d[1] == 0x0A) return 1;
    static const unsigned char sig[12] = { 0, 0, 0, 0x0C, 'J', 'X', 'L', ' ', 0x0D, 0x0A, 0x87, 0x0A };
    return len >= 12 && !memcmp(d, sig, 12);
}

int imgx_decode(const unsigned char* d, int len, unsigned** argb, int* ow, int* oh) {
    *argb = 0; *ow = 0; *oh = 0;
    if (!d || len < 2) return -1;
    if (is_avif(d, len)) return avif_decode(d, len, argb, ow, oh);
    if (is_jxl(d, len)) return jxl_decode(d, len, argb, ow, oh);
    return -1;
}
