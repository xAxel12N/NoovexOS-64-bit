#include "web/nwe.h"

typedef struct pchunk { struct pchunk* next; size_t cap, used; unsigned char* mem; } pchunk;
static pchunk* pools[4];
static pchunk* pcur[4];
int g_pool;
static void* pool_alloc(int w, size_t n) {
    n = (n + 15) & ~(size_t)15;
    pchunk* c = pcur[w];
    while (c && c->used + n > c->cap) { c = c->next; if (c) { c->used = 0; pcur[w] = c; } }
    if (!c) {
        size_t cap = n > (4u << 20) ? n : (4u << 20);
        pchunk* nc = malloc(sizeof(pchunk));
        if (!nc) nwe_fail("out of memory");
        nc->mem = malloc(cap);
        if (!nc->mem) nwe_fail("out of memory");
        nc->cap = cap;
        nc->used = 0;
        nc->next = 0;
        if (!pools[w]) pools[w] = nc;
        else { pchunk* t = pools[w]; while (t->next) t = t->next; t->next = nc; }
        pcur[w] = nc;
        c = nc;
    }
    void* p = c->mem + c->used;
    c->used += n;
    return p;
}
void pool_reset(int w) { pcur[w] = pools[w]; if (pools[w]) pools[w]->used = 0; }
int pool_suspend(void) { int v = g_pool; g_pool = 0; return v; }
void pool_resume(int v) { g_pool = v; }
static int in_pool(void* p) {
    unsigned char* u = p;
    for (int w = 1; w < 4; w++) for (pchunk* c = pools[w]; c; c = c->next) if (u >= c->mem && u < c->mem + c->cap) return 1;
    return 0;
}
void nwe_pools_forget(void) { for (int i = 0; i < 4; i++) { pools[i] = 0; pcur[i] = 0; } g_pool = 0; }
void nfree(void* p) { if (!p || in_pool(p)) return; free(p); }
void* nalloc(size_t n) {
    if (g_pool) return pool_alloc(g_pool, n ? n : 1);
    void* p = malloc(n ? n : 1);
    if (!p) nwe_fail("out of memory");
    return p;
}
void* nzalloc(size_t n) {
    void* p = nalloc(n);
    memset(p, 0, n);
    return p;
}
char* nstrndup(const char* s, int n) {
    char* r = nalloc((size_t)n + 1);
    memcpy(r, s, (size_t)n);
    r[n] = 0;
    return r;
}
char* nstrdup(const char* s) { return nstrndup(s ? s : "", (int)strlen(s ? s : "")); }

static void sb_grow(sbuf* b, int need) {
    if (b->n + need + 1 <= b->cap) return;
    int nc = b->cap ? b->cap * 2 : 64;
    while (nc < b->n + need + 1) nc *= 2;
    char* ns = nalloc((size_t)nc);
    if (b->s) memcpy(ns, b->s, (size_t)b->n);
    if (b->s) nfree(b->s);
    b->s = ns;
    b->cap = nc;
}
void sb_putc(sbuf* b, int c) { sb_grow(b, 1); b->s[b->n++] = (char)c; b->s[b->n] = 0; }
void sb_putn(sbuf* b, const char* s, int n) { if (n <= 0) { sb_grow(b, 0); b->s[b->n] = 0; return; } sb_grow(b, n); memcpy(b->s + b->n, s, (size_t)n); b->n += n; b->s[b->n] = 0; }
void sb_puts(sbuf* b, const char* s) { sb_putn(b, s, (int)strlen(s)); }
void sb_putu8(sbuf* b, int cp) {
    if (cp < 0x80) sb_putc(b, cp);
    else if (cp < 0x800) { sb_putc(b, 0xC0 | (cp >> 6)); sb_putc(b, 0x80 | (cp & 63)); }
    else if (cp < 0x10000) { sb_putc(b, 0xE0 | (cp >> 12)); sb_putc(b, 0x80 | ((cp >> 6) & 63)); sb_putc(b, 0x80 | (cp & 63)); }
    else { sb_putc(b, 0xF0 | (cp >> 18)); sb_putc(b, 0x80 | ((cp >> 12) & 63)); sb_putc(b, 0x80 | ((cp >> 6) & 63)); sb_putc(b, 0x80 | (cp & 63)); }
}
char* sb_str(sbuf* b) { if (!b->s) sb_grow(b, 0), b->s[0] = 0; return b->s; }

int u8dec(const char* s, int n, int* cp) {
    const unsigned char* u = (const unsigned char*)s;
    if (n <= 0) { *cp = 0; return 0; }
    if (u[0] < 0x80) { *cp = u[0]; return 1; }
    if ((u[0] & 0xE0) == 0xC0 && n >= 2 && (u[1] & 0xC0) == 0x80) { *cp = ((u[0] & 31) << 6) | (u[1] & 63); return 2; }
    if ((u[0] & 0xF0) == 0xE0 && n >= 3 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) { *cp = ((u[0] & 15) << 12) | ((u[1] & 63) << 6) | (u[2] & 63); return 3; }
    if ((u[0] & 0xF8) == 0xF0 && n >= 4 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80 && (u[3] & 0xC0) == 0x80) { *cp = ((u[0] & 7) << 18) | ((u[1] & 63) << 12) | ((u[2] & 63) << 6) | (u[3] & 63); return 4; }
    *cp = u[0] >= 0xA0 ? u[0] : 0xFFFD;
    return 1;
}
int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int ieq(const char* a, const char* b) { while (*a && *b) { if (lc((unsigned char)*a) != lc((unsigned char)*b)) return 0; a++; b++; } return *a == *b; }
int ieqn(const char* a, const char* b, int n) { for (int i = 0; i < n; i++) { if (lc((unsigned char)a[i]) != lc((unsigned char)b[i])) return 0; if (!a[i]) return 1; } return 1; }
int istarts(const char* s, const char* p) { while (*p) { if (lc((unsigned char)*s) != lc((unsigned char)*p)) return 0; s++; p++; } return 1; }
int is_ws(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

float parse_float(const char* s, const char** end) {
    const char* p = s;
    double sign = 1, v = 0;
    if (*p == '-') { sign = -1; p++; } else if (*p == '+') p++;
    int any = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; any = 1; }
    if (*p == '.' && p[1] >= '0' && p[1] <= '9') {
        p++;
        double f = 0.1;
        while (*p >= '0' && *p <= '9') { v += (*p - '0') * f; f *= 0.1; p++; any = 1; }
    }
    if (any && (*p == 'e' || *p == 'E') && ((p[1] >= '0' && p[1] <= '9') || ((p[1] == '-' || p[1] == '+') && p[2] >= '0' && p[2] <= '9'))) {
        p++;
        int es = 1, e = 0;
        if (*p == '-') { es = -1; p++; } else if (*p == '+') p++;
        while (*p >= '0' && *p <= '9') { e = e * 10 + (*p - '0'); p++; }
        v *= pow(10.0, es * e);
    }
    if (end) *end = any ? p : s;
    return (float)(sign * v);
}

static int scheme_len(const char* u) {
    int i = 0;
    if (!((u[0] >= 'a' && u[0] <= 'z') || (u[0] >= 'A' && u[0] <= 'Z'))) return 0;
    while (u[i] && ((u[i] >= 'a' && u[i] <= 'z') || (u[i] >= 'A' && u[i] <= 'Z') || (u[i] >= '0' && u[i] <= '9') || u[i] == '+' || u[i] == '-' || u[i] == '.')) i++;
    return u[i] == ':' ? i : 0;
}

static void norm_path(char* path) {
    char out[2048];
    int segs[128], ns = 0, o = 0;
    const char* p = path;
    char* q = strchr(path, '?');
    char* h = strchr(path, '#');
    char* tail = q ? q : h;
    char tailbuf[1024] = "";
    if (tail) { strncpy(tailbuf, tail, 1023); tailbuf[1023] = 0; *tail = 0; }
    out[o++] = '/';
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char* s = p;
        while (*p && *p != '/') p++;
        int n = (int)(p - s);
        if (n == 1 && s[0] == '.') { if (!*p && o > 0 && out[o - 1] != '/') out[o++] = '/'; continue; }
        if (n == 2 && s[0] == '.' && s[1] == '.') {
            if (ns > 0) o = segs[--ns];
            else o = 1;
            continue;
        }
        if (ns < 128) segs[ns++] = o;
        if (o + n + 2 >= (int)sizeof(out)) break;
        memcpy(out + o, s, (size_t)n);
        o += n;
        if (*p == '/') out[o++] = '/';
    }
    out[o] = 0;
    int tl = (int)strlen(tailbuf);
    if (o + tl < 2047) memcpy(out + o, tailbuf, (size_t)tl + 1);
    strcpy(path, out);
}

void url_resolve(const char* base, const char* rel, char* out, int cap) {
    char buf[2048];
    while (*rel == ' ' || *rel == '\t' || *rel == '\n' || *rel == '\r') rel++;
    int rl = (int)strlen(rel);
    while (rl > 0 && (rel[rl - 1] == ' ' || rel[rl - 1] == '\n' || rel[rl - 1] == '\r' || rel[rl - 1] == '\t')) rl--;
    char r[1800];
    if (rl > 1799) rl = 1799;
    memcpy(r, rel, (size_t)rl);
    r[rl] = 0;
    if (scheme_len(r)) { snprintf(out, (size_t)cap, "%s", r); return; }
    int bs = scheme_len(base);
    if (!bs) { snprintf(out, (size_t)cap, "%s", r); return; }
    if (r[0] == '/' && r[1] == '/') { snprintf(out, (size_t)cap, "%.*s:%s", bs, base, r); return; }
    const char* hs = base + bs + 1;
    if (hs[0] == '/' && hs[1] == '/') hs += 2;
    const char* pe = hs;
    while (*pe && *pe != '/' && *pe != '?' && *pe != '#') pe++;
    int prefix = (int)(pe - base);
    if (!r[0]) { snprintf(out, (size_t)cap, "%s", base); char* h = strchr(out, '#'); if (h) *h = 0; return; }
    if (r[0] == '#') {
        snprintf(buf, sizeof(buf), "%s", base);
        char* h = strchr(buf, '#');
        if (h) *h = 0;
        snprintf(out, (size_t)cap, "%s%s", buf, r);
        return;
    }
    if (r[0] == '?') {
        snprintf(buf, sizeof(buf), "%s", base);
        char* q = strchr(buf + prefix, '?');
        if (!q) q = strchr(buf + prefix, '#');
        if (q) *q = 0;
        snprintf(out, (size_t)cap, "%s%s", buf, r);
        return;
    }
    char path[2048];
    if (r[0] == '/') snprintf(path, sizeof(path), "%s", r);
    else {
        const char* bp = pe;
        int bl = 0;
        while (bp[bl] && bp[bl] != '?' && bp[bl] != '#') bl++;
        int ls = -1;
        for (int i = 0; i < bl; i++) if (bp[i] == '/') ls = i;
        if (ls < 0) snprintf(path, sizeof(path), "/%s", r);
        else snprintf(path, sizeof(path), "%.*s%s", ls + 1, bp, r);
    }
    norm_path(path);
    snprintf(out, (size_t)cap, "%.*s%s", prefix, base, path);
}

void url_origin(const char* url, char* out, int cap) {
    int bs = scheme_len(url);
    if (!bs) { snprintf(out, (size_t)cap, "null"); return; }
    const char* hs = url + bs + 1;
    if (hs[0] == '/' && hs[1] == '/') hs += 2;
    const char* pe = hs;
    while (*pe && *pe != '/' && *pe != '?' && *pe != '#') pe++;
    snprintf(out, (size_t)cap, "%.*s", (int)(pe - url), url);
}

static void msort_rec(char* a, char* t, int n, int sz, int (*cmp)(const void*, const void*)) {
    if (n < 2) return;
    if (n < 8) {
        for (int i = 1; i < n; i++) {
            memcpy(t, a + i * sz, (size_t)sz);
            int j = i - 1;
            while (j >= 0 && cmp(a + j * sz, t) > 0) { memcpy(a + (j + 1) * sz, a + j * sz, (size_t)sz); j--; }
            memcpy(a + (j + 1) * sz, t, (size_t)sz);
        }
        return;
    }
    int h = n / 2;
    msort_rec(a, t, h, sz, cmp);
    msort_rec(a + h * sz, t, n - h, sz, cmp);
    int i = 0, j = h, k = 0;
    while (i < h && j < n) {
        if (cmp(a + j * sz, a + i * sz) < 0) { memcpy(t + k * sz, a + j * sz, (size_t)sz); j++; }
        else { memcpy(t + k * sz, a + i * sz, (size_t)sz); i++; }
        k++;
    }
    while (i < h) { memcpy(t + k * sz, a + i * sz, (size_t)sz); i++; k++; }
    while (j < n) { memcpy(t + k * sz, a + j * sz, (size_t)sz); j++; k++; }
    memcpy(a, t, (size_t)n * (size_t)sz);
}
void msort(void* base, int n, int sz, int (*cmp)(const void*, const void*)) {
    if (n < 2) return;
    char* t = nalloc((size_t)n * (size_t)sz);
    msort_rec(base, t, n, sz, cmp);
    nfree(t);
}
