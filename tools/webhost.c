#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "include/nvx/apps/nwe_api.h"

void nwe_arena_init(void) {}
void nwe_arena_reset(void) {}
size_t nwe_arena_used(void) { return 0; }
size_t nwe_arena_size(void) { return 512u << 20; }
void* nwk_alloc_arena(unsigned long n) { return malloc(n); }
#define CC_ALLOC(n) malloc(n)
#define CC_FREE(p) free(p)

#define CC_N 96
#define CC_MAX (40ul << 20)
typedef struct { unsigned long long key; int srclen, len; unsigned char* p; unsigned long use; } ccent;
static ccent g_cc[CC_N];
static unsigned long g_cc_total, g_cc_tick;
int nwk_code_get(unsigned long long key, int srclen, const unsigned char** data, int* len) {
    for (int i = 0; i < CC_N; i++) if (g_cc[i].p && g_cc[i].key == key && g_cc[i].srclen == srclen) { g_cc[i].use = ++g_cc_tick; *data = g_cc[i].p; *len = g_cc[i].len; return 1; }
    return 0;
}
void nwk_code_put(unsigned long long key, int srclen, const unsigned char* data, int len) {
    if (len <= 0 || (unsigned long)len > CC_MAX / 2) return;
    for (int i = 0; i < CC_N; i++) if (g_cc[i].p && g_cc[i].key == key && g_cc[i].srclen == srclen) return;
    for (;;) {
        int fr = -1, lru = -1;
        for (int i = 0; i < CC_N; i++) { if (!g_cc[i].p) { if (fr < 0) fr = i; } else if (lru < 0 || g_cc[i].use < g_cc[lru].use) lru = i; }
        if (fr >= 0 && g_cc_total + (unsigned long)len <= CC_MAX) {
            unsigned char* m = CC_ALLOC((unsigned long)len);
            if (!m) return;
            for (int k = 0; k < len; k++) m[k] = data[k];
            g_cc[fr].key = key; g_cc[fr].srclen = srclen; g_cc[fr].len = len; g_cc[fr].p = m; g_cc[fr].use = ++g_cc_tick;
            g_cc_total += (unsigned long)len;
            return;
        }
        if (lru < 0) return;
        CC_FREE(g_cc[lru].p); g_cc_total -= (unsigned long)g_cc[lru].len; g_cc[lru].p = 0;
    }
}

int nwk_ws_open(const char* url, const char* protocols, const char* origin) { (void)url; (void)protocols; (void)origin; return -1; }
int nwk_ws_send(int id, const unsigned char* data, int len, int binary) { (void)id; (void)data; (void)len; (void)binary; return -1; }
int nwk_ws_recv(int id, unsigned char* out, int cap, int* binary) { (void)id; (void)out; (void)cap; (void)binary; return -3; }
int nwk_ws_info(int id, int* code, char* proto, int pcap, char* reason, int rcap) { (void)id; (void)code; (void)proto; (void)pcap; (void)reason; (void)rcap; return -1; }
void nwk_ws_close(int id, int code, int forget) { (void)id; (void)code; (void)forget; }
void nwk_ws_close_all(void) {}
int nwk_frame_status(const char* url, const char* srcdoc, char* out, int cap) { (void)url; (void)srcdoc; if (out && cap) out[0] = 0; return 0; }
int nwk_frame_paint(const char* url, const char* srcdoc, int x, int y, int w, int h, int cx0, int cy0, int cx1, int cy1, unsigned int* fb, int stride) { (void)url; (void)srcdoc; (void)x; (void)y; (void)w; (void)h; (void)cx0; (void)cy0; (void)cx1; (void)cy1; (void)fb; (void)stride; return 0; }
int nwk_frame_post(const char* msg, int len) { (void)msg; (void)len; return -1; }
int nwk_frame_recv(char* out, int cap) { (void)out; (void)cap; return -1; }
int nwk_frame_is(void) { return 0; }
void nwk_frame_reset(void) {}
void nwk_frame_open(const char* url) { (void)url; }
void nwk_frame_sandbox(int flags) { (void)flags; }
void nwk_notify(const char* t, const char* b) { fprintf(stderr, "[notify] %s: %s\n", t, b); }
void nwk_badge(int n) { fprintf(stderr, "[badge] %d\n", n); }
int nwk_mods(void) { const char* e = getenv("WH_MODS"); return e ? atoi(e) : 0; }
int nwk_doc_policy(char* csp, int cap, char* ro, int rcap) { const char* e = getenv("WH_CSP"); const char* r = getenv("WH_CSPRO"); snprintf(csp, (size_t)cap, "%s", e ? e : ""); snprintf(ro, (size_t)rcap, "%s", r ? r : ""); return csp[0] || ro[0]; }
void nwk_frame_open_doc(const char* html) { (void)html; }

static char* readfile(const char* p, int* len) {
    FILE* f = fopen(p, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n < 0 || n > (1L << 30)) { fclose(f); return 0; }
    fseek(f, 0, SEEK_SET);
    char* b = malloc((size_t)n + 1);
    if (!b) { fclose(f); return 0; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return 0; }
    b[n] = 0;
    fclose(f);
    *len = (int)n;
    return b;
}

static unsigned long long g_wh_net;
void nwk_prefetch(const char** urls, int n, const char* referer) { (void)referer; for (int i = 0; i < n; i++) fprintf(stderr, "[prefetch] %s\n", urls[i]); }
void nwk_prefetch_more(const char** urls, int n, const char* referer) { (void)referer; for (int i = 0; i < n; i++) fprintf(stderr, "[prefetch+] %s\n", urls[i]); }
unsigned long long nwk_net_us(void) { return g_wh_net; }
const char* g_nwk_xhdr;
nwk_slow g_nwk_slow;
int g_nwk_kind;
static int wh_http(const char* url, const char* method, const char* ctype, const char* body, int blen, nwk_resp* r) {
    const char* h = url + 7; const char* sl = strchr(h, '/'); char host[256]; int hl = sl ? (int)(sl - h) : (int)strlen(h); if (hl > 255) hl = 255; memcpy(host, h, (size_t)hl); host[hl] = 0;
    int port = 80; char* c = strchr(host, ':'); if (c) { *c = 0; port = atoi(c + 1); }
    int fd = socket(AF_INET, SOCK_STREAM, 0); struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port); a.sin_addr.s_addr = inet_addr(strcmp(host, "localhost") ? host : "127.0.0.1");
    if (fd < 0 || connect(fd, (struct sockaddr*)&a, sizeof a)) { if (fd >= 0) close(fd); return -2; }
    size_t cap = 16384 + (size_t)(blen > 0 ? blen : 0); char* q = malloc(cap); int n = snprintf(q, cap, "%s %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\nAccept-Encoding: identity\r\n", method && method[0] ? method : "GET", sl ? sl : "/", host, port);
    if (g_nwk_kind == 2 && g_nwk_xhdr) n += snprintf(q + n, cap - (size_t)n, "%s", g_nwk_xhdr);
    if (body && blen >= 0 && method && (method[0] == 'P' || method[0] == 'D')) n += snprintf(q + n, cap - (size_t)n, "Content-Type: %s\r\nContent-Length: %d\r\n", ctype && ctype[0] ? ctype : "application/x-www-form-urlencoded", blen);
    n += snprintf(q + n, cap - (size_t)n, "\r\n"); if (body && blen > 0) { memcpy(q + n, body, (size_t)blen); n += blen; }
    if (write(fd, q, (size_t)n) != n) { close(fd); free(q); return -2; } free(q);
    size_t bc = 1 << 16, bn = 0; char* b = malloc(bc); for (;;) { if (bn + 4096 > bc) { bc *= 2; b = realloc(b, bc); } ssize_t k = read(fd, b + bn, 4096); if (k <= 0) break; bn += (size_t)k; } close(fd);
    b = realloc(b, bn + 1); b[bn] = 0;
    char* he = strstr(b, "\r\n\r\n"); if (!he) { free(b); return -5; }
    r->status = atoi(strchr(b, ' ') ? strchr(b, ' ') + 1 : "0");
    char* hs = strstr(b, "\r\n") + 2; int hlen = (int)(he - hs + 2); r->hdrs = malloc((size_t)hlen + 1); memcpy(r->hdrs, hs, (size_t)hlen); r->hdrs[hlen] = 0; r->hlen = hlen;
    char* ct = strcasestr(b, "\r\ncontent-type:"); if (ct && ct < he) { ct += 15; while (*ct == ' ') ct++; int k = 0; while (ct[k] && ct[k] != '\r' && k < 95) { r->ctype[k] = ct[k]; k++; } r->ctype[k] = 0; }
    char* d = he + 4; int dl = (int)(bn - (size_t)(d - b));
    char* te = strcasestr(b, "\r\ntransfer-encoding: chunked");
    char* out = malloc((size_t)dl + 1); int on = 0;
    if (te && te < he) { char* p = d; char* e = d + dl; while (p < e) { int sz = (int)strtol(p, 0, 16); char* nl = strstr(p, "\r\n"); if (!nl || sz <= 0) break; p = nl + 2; if (p + sz > e) sz = (int)(e - p); memcpy(out + on, p, (size_t)sz); on += sz; p += sz + 2; } }
    else { memcpy(out, d, (size_t)dl); on = dl; }
    out[on] = 0; r->body = out; r->len = on; free(b);
    return 0;
}
int nwk_fetch(const char* url, const char* method, const char* ctype, const char* body, int blen, const char* referer, nwk_resp* r) {
    (void)ctype; (void)body; (void)blen; (void)referer;
    memset(r, 0, sizeof(*r));
    if (!strncmp(url, "http://127.0.0.1:", 17) || !strncmp(url, "http://localhost:", 17)) { snprintf(r->final_url, sizeof(r->final_url), "%s", url); fprintf(stderr, "[fetch] %s %s\n", method, url); return wh_http(url, method, ctype, body, blen, r); }
    snprintf(r->final_url, sizeof(r->final_url), "%s", url);
    fprintf(stderr, "[fetch] %s %s\n", method, url);
    const char* fp;
    if (!strncmp(url, "file://", 7)) fp = url + 7;
    else if (!strncmp(url, "http://wh.test/", 15)) fp = url + 14;
    else { r->status = 0; return -1; }
    char pth[4096]; snprintf(pth, sizeof(pth), "%s", fp); char* q = strchr(pth, '?'); if (q) *q = 0; q = strchr(pth, '#'); if (q) *q = 0;
    int len;
    char* d = readfile(pth, &len);
    if (!d) { r->status = 404; return 0; }
    r->body = d;
    r->len = len;
    r->status = 200;
    snprintf(r->ctype, sizeof(r->ctype), "%s", strstr(pth, ".wasm") ? "application/wasm" : strstr(pth, ".css") ? "text/css" : strstr(pth, ".json") ? "application/json" : strstr(pth, ".js") ? "application/javascript" : "text/html");
    return 0;
}
void nwk_free(void* p) { free(p); }
#include <pthread.h>
typedef struct { int used, id, state, stage; long owner; char* url; char* meth; char* ctype; char* body; int blen; nwk_resp r; int rc; pthread_t th; } whreq;
static whreq g_whq[64];
static int g_whq_next = 1;
static pthread_mutex_t g_whq_mu = PTHREAD_MUTEX_INITIALIZER;
static void* whq_run(void* a) { whreq* q = a; nwk_resp r; int rc = nwk_fetch(q->url, q->meth, q->ctype, q->body, q->blen, 0, &r); pthread_mutex_lock(&g_whq_mu); q->r = r; q->rc = rc; q->state = 2; pthread_mutex_unlock(&g_whq_mu); return 0; }
void nwk_net_keep(int id) { (void)id; }
int nwk_net_start(long owner, const char* url, const char* method, const char* ctype, const char* body, int blen, const char* xhdr, const char* referer) {
    (void)xhdr; (void)referer;
    if (getenv("WH_SYNCNET")) return 0;
    for (int i = 0; i < 64; i++) if (!g_whq[i].used) {
        whreq* q = &g_whq[i]; memset(q, 0, sizeof(*q));
        q->used = 1; q->id = g_whq_next++; q->owner = owner; q->url = strdup(url); q->meth = strdup(method ? method : "GET"); q->ctype = ctype ? strdup(ctype) : 0;
        if (body && blen > 0) { q->body = malloc((size_t)blen); memcpy(q->body, body, (size_t)blen); q->blen = blen; }
        q->state = 1;
        pthread_create(&q->th, 0, whq_run, q);
        return q->id;
    }
    return 0;
}
static void whq_free(whreq* q) { pthread_join(q->th, 0); free(q->url); free(q->meth); free(q->ctype); free(q->body); if (q->r.body) free(q->r.body); memset(q, 0, sizeof(*q)); }
int nwk_net_event(long owner, nwk_netev* ev) {
    memset(ev, 0, sizeof(*ev));
    for (int i = 0; i < 64; i++) {
        whreq* q = &g_whq[i];
        pthread_mutex_lock(&g_whq_mu); int st = q->state; pthread_mutex_unlock(&g_whq_mu);
        if (!q->used || q->owner != owner || st != 2) continue;
        ev->id = q->id;
        if (q->stage == 3) { whq_free(q); continue; }
        if (q->rc < 0) { ev->type = 3; ev->ok = 0; q->stage = 3; return 1; }
        if (q->stage == 0) { q->stage = 1; ev->type = 1; ev->status = q->r.status; ev->ctype = q->r.ctype; ev->url = q->r.final_url; ev->hdrs = ""; return 1; }
        if (q->stage == 1) { q->stage = 2; if (q->r.len > 0) { ev->type = 2; ev->data = (const unsigned char*)q->r.body; ev->dlen = q->r.len; return 1; } }
        q->stage = 3; ev->type = 3; ev->ok = 1; return 1;
    }
    return 0;
}
void nwk_net_abort(long owner, int id) { for (int i = 0; i < 64; i++) if (g_whq[i].used && g_whq[i].owner == owner && g_whq[i].id == id) { pthread_mutex_lock(&g_whq_mu); int st = g_whq[i].state; pthread_mutex_unlock(&g_whq_mu); if (st == 2) whq_free(&g_whq[i]); else g_whq[i].owner = -1; } }
void nwk_net_cancel_owner(long owner) { for (int i = 0; i < 64; i++) if (g_whq[i].used && g_whq[i].owner == owner) g_whq[i].owner = -1; }
int nwk_net_active(long owner) { int n = 0; for (int i = 0; i < 64; i++) if (g_whq[i].used && g_whq[i].owner == owner) n++; return n; }
int nwk_image(const unsigned char* data, int len, unsigned int** argb, int* w, int* h) {
    (void)data; (void)len;
    *w = 64; *h = 48;
    unsigned* p = malloc(64 * 48 * 4);
    for (int i = 0; i < 64 * 48; i++) p[i] = 0xFF000000u | (unsigned)((i % 64) * 4 << 16) | (unsigned)((i / 64) * 5 << 8) | 0x80;
    *argb = p;
    return 0;
}

static unsigned char* g_emo_bin; static long g_emo_sz;
static void emo_load(void) { static int tried; if (tried) return; tried = 1; const char* p = getenv("WH_EMOJI"); if (!p) return; int l = 0; g_emo_bin = (unsigned char*)readfile(p, &l); g_emo_sz = l; }
static int emo_find(int cp) { emo_load(); const unsigned char* d = g_emo_bin; if (!d || g_emo_sz < 12) return -1; unsigned n; memcpy(&n, d + 4, 4); int lo = 0, hi = (int)n - 1; while (lo <= hi) { int m = (lo + hi) / 2; int c; memcpy(&c, d + 12 + m * 12, 4); if (c == cp) return m; if (c < cp) lo = m + 1; else hi = m - 1; } return -1; }
int nwk_emoji_has(int cp) { return emo_find(cp) >= 0; }
int nwk_emoji(int cp, unsigned int** argb, int* w, int* h) { if (emo_find(cp) < 0) return -1; *w = 32; *h = 32; unsigned* p = malloc(32 * 32 * 4); for (int i = 0; i < 32 * 32; i++) { int x = i % 32 - 16, y = i / 32 - 16; p[i] = x * x + y * y < 200 ? 0xFFF5C518u : 0; } *argb = p; return 0; }
static char cookies[4096];
int nwk_cookie_get(const char* url, int forjs, char* out, int cap) { (void)url; (void)forjs; snprintf(out, (size_t)cap, "%s", cookies); return 0; }
void nwk_cookie_set(const char* url, const char* line, int fromjs) { (void)url; (void)fromjs; const char* semi = strchr(line, ';'); int l = semi ? (int)(semi - line) : (int)strlen(line); if (cookies[0]) strcat(cookies, "; "); strncat(cookies, line, (size_t)l); }
static char skeys[64][128], svals[64][1024];
static int ns;
int nwk_store_get(const char* origin, int session, const char* key, char* out, int cap) { (void)origin; (void)session; for (int i = 0; i < ns; i++) if (!strcmp(skeys[i], key)) { snprintf(out, (size_t)cap, "%s", svals[i]); return 0; } return -1; }
void nwk_store_set(const char* origin, int session, const char* key, const char* val) { (void)origin; (void)session; for (int i = 0; i < ns; i++) if (!strcmp(skeys[i], key)) { if (!val) { skeys[i][0] = 0; } else snprintf(svals[i], 1024, "%s", val); return; } if (val && ns < 64) { snprintf(skeys[ns], 128, "%s", key); snprintf(svals[ns], 1024, "%s", val); ns++; } }
void nwk_store_clear(const char* origin, int session) { (void)origin; (void)session; ns = 0; }
int nwk_store_key(const char* origin, int session, int idx, char* out, int cap) { (void)origin; (void)session; if (idx < 0 || idx >= ns) return -1; snprintf(out, (size_t)cap, "%s", skeys[idx]); return 0; }
int nwk_store_len(const char* origin, int session) { (void)origin; (void)session; return ns; }
void nwk_log(const char* s) { fprintf(stderr, "[log] %s\n", s); }
void nwk_error(const char* s) { fprintf(stderr, "[error] %s\n", s); }
static char g_wh_pend[12288];
void nwk_pending_set(const char* s, int len) { if (len > 12287) len = 12287; memcpy(g_wh_pend, s, (size_t)len); g_wh_pend[len] = 0; if (getenv("WH_PEND")) fprintf(stderr, "[pending]\n%s\n", g_wh_pend); }
int nwk_frame_info(void) { return 0; }
void nwk_frame_fail(const char* why) { (void)why; }
int nwk_popup_open(const char* url, int w, int h) { (void)url; (void)w; (void)h; return 0; }
int nwk_popup_info(void) { return 0; }
void nwk_popup_close(void) {}
static int wh_perm(void) { const char* e = getenv("WH_PERM"); return e ? (e[0] == 'a' ? 1 : 2) : 0; }
int nwk_perm_get(const char* o, const char* n) { (void)o; (void)n; return wh_perm(); }
int nwk_perm_ask(const char* o, const char* n) { (void)o; (void)n; int p = wh_perm(); return p ? -p : -2; }
int nwk_perm_poll(int id) { (void)id; return 2; }
int nwk_tz_index(void) { return -1; }
void nwk_frame_slot(int s) { (void)s; }
unsigned long nwk_ms(void);
unsigned long nwk_ms_active(void) { return nwk_ms(); }
int nwk_battery(int* pct, int* charging, int* mins) { const char* e = getenv("WH_BAT"); if (!e) { *pct = -1; *charging = 1; *mins = -1; return 0; } *pct = atoi(e); *charging = strchr(e, 'c') != 0; *mins = 90; return 1; }
void nwk_frame_switch(void) {}
void nwk_status(const char* s) { (void)s; }
void nwk_alert(const char* s) { fprintf(stderr, "[alert] %s\n", s); }
int nwk_busy(void) { return 0; }
unsigned long nwk_ms(void) { struct timeval tv; gettimeofday(&tv, 0); return (unsigned long)(tv.tv_sec * 1000 + tv.tv_usec / 1000); }
static char wh_clip[65536]; static int wh_clipn;
int nwk_clip_get(char* out, int cap) { int n = wh_clipn < cap - 1 ? wh_clipn : cap - 1; memcpy(out, wh_clip, (size_t)n); out[n] = 0; return n; }
void nwk_clip_set(const char* s, int n) { if (n > 65535) n = 65535; memcpy(wh_clip, s, (size_t)n); wh_clip[n] = 0; wh_clipn = n; fprintf(stderr, "[clip] %.*s\n", n, s); }
unsigned long long nwk_us(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (unsigned long long)ts.tv_sec * 1000000ULL + (unsigned long long)ts.tv_nsec / 1000ULL; }
int g_web_csp = 1;
long long nwk_epoch_ms(void) { struct timeval tv; gettimeofday(&tv, 0); return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000; }
int nwk_tz_min(void) { return 0; }
void nwk_pump(void) {}
void* nwe_stack_top(void) { return 0; }
#include <zlib.h>
int nwk_inflate(const unsigned char* src, int slen, unsigned char* dst, int dcap) { if (slen < 3) return -1; z_stream z; memset(&z, 0, sizeof(z)); if (inflateInit2(&z, -15) != Z_OK) return -1; z.next_in = (Bytef*)src + 2; z.avail_in = (uInt)(slen - 2); z.next_out = dst; z.avail_out = (uInt)dcap; int r = inflate(&z, Z_FINISH); int n = (int)z.total_out; inflateEnd(&z); if (r != Z_STREAM_END) return -1; return n; }
static struct { int used, playing; long long cur, base; unsigned long t0; long long app[2]; int ended[2]; unsigned fr[160 * 90]; unsigned long long seq; } HM[8];
int nwk_media_new(const char* url, int split, int w, int h, long long start_ms, int paused) {
    for (int i = 0; i < 8; i++) if (!HM[i].used) { memset(&HM[i], 0, sizeof HM[i]); HM[i].used = 1; HM[i].playing = !paused; HM[i].base = start_ms; HM[i].t0 = nwk_ms(); for (int k = 0; k < 160 * 90; k++) HM[i].fr[k] = 0xFF2060C0u + (unsigned)(k % 160); HM[i].seq = 1; fprintf(stderr, "[media] new %d url='%s' split=%d %dx%d start=%lld paused=%d\n", i, url, split, w, h, start_ms, paused); return i; }
    return -1;
}
static long long hm_cur(int id) { return HM[id].playing ? HM[id].base + (long long)(nwk_ms() - HM[id].t0) : HM[id].base; }
void nwk_media_cmd(int id, int cmd, long long a, long long b) { fprintf(stderr, "[media] cmd %d %d %lld %lld\n", id, cmd, a, b); if (cmd == 1 && !HM[id].playing) { HM[id].t0 = nwk_ms(); HM[id].playing = 1; } if (cmd == 2 && HM[id].playing) { HM[id].base = hm_cur(id); HM[id].playing = 0; } if (cmd == 3) { HM[id].base = a; HM[id].t0 = nwk_ms(); } }
int nwk_media_status(int id, long long* v) { if (id < 0 || id >= 8 || !HM[id].used) return -1; long long c = hm_cur(id); unsigned fl = 128 | 16 | 32; if (c >= 10000) { c = 10000; fl |= 4; } else fl |= HM[id].playing ? 1 : 2; v[0] = c; v[1] = 10000; v[2] = fl; v[3] = 160; v[4] = 90; v[5] = 50; HM[id].seq++; return 0; }
int nwk_media_append(int id, int stream, const unsigned char* d, int len) { HM[id].app[stream] += len; fprintf(stderr, "[media] append %d s%d %d bytes (total %lld) first=%02x%02x%02x%02x%02x%02x%02x%02x\n", id, stream, len, HM[id].app[stream], d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]); return 0; }
void nwk_media_end(int id, int stream) { fprintf(stderr, "[media] end %d s%d\n", id, stream); }
int nwk_media_restart(int id, long long start_ms, int keep) { fprintf(stderr, "[media] restart %d at %lld keep=%d\n", id, start_ms, keep); HM[id].base = start_ms; HM[id].t0 = nwk_ms(); return 0; }
void nwk_media_free(int id) { if (id >= 0 && id < 8) { HM[id].used = 0; fprintf(stderr, "[media] free %d\n", id); } }
long long nwk_media_len(int id, int stream) { return HM[id].app[stream]; }
const unsigned* nwk_media_frame(int id, int* w, int* h, unsigned long long* seq) { if (id < 0 || id >= 8 || !HM[id].used) return 0; *w = 160; *h = 90; if (seq) *seq = HM[id].seq; return HM[id].fr; }

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: webhost in.html out.ppm [w] [h] [maxh] [script]\n"); return 1; }
    int w = argc > 3 ? atoi(argv[3]) : 1000, h = argc > 4 ? atoi(argv[4]) : 700, maxh = argc > 5 ? atoi(argv[5]) : 2400;
    int len;
    char* html = readfile(argv[1], &len);
    if (!html) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    char url[2048];
    char abs[4096];
    if (!realpath(argv[1], abs)) snprintf(abs, sizeof(abs), "%s", argv[1]);
    snprintf(url, sizeof(url), getenv("WH_HTTP") ? "http://wh.test%s" : "file://%s", abs);
    static nwe_req r;
    memset(&r, 0, sizeof(r));
    nwe_entry(NWE_OP_INIT, &r);
    unsigned long t0 = nwk_ms();
    memset(&r, 0, sizeof(r));
    r.url = url; r.data = html; r.len = len; r.w = w; r.h = h;
    if (getenv("WH_TWICE")) { nwe_entry(NWE_OP_LOAD, &r); fprintf(stderr, "load1 %lums\n", nwk_ms() - t0); t0 = nwk_ms(); memset(&r, 0, sizeof(r)); r.url = url; r.data = html; r.len = len; r.w = w; r.h = h; }
    nwe_entry(NWE_OP_LOAD, &r);
    fprintf(stderr, "load %lums title='%s' doch=%d nav='%s'\n", nwk_ms() - t0, r.title, r.doch, r.nav);
    if (getenv("WH_PERF")) { static nwe_req pr; memset(&pr, 0, sizeof(pr)); nwe_entry(NWE_OP_PERF, &pr); nwe_perf pf; memcpy(&pf, pr.out, sizeof(pf)); fprintf(stderr, "perf js=%llu compile=%llu style=%llu layout=%llu paint=%llu scripts=%llu load=%llu\n", pf.js_us/1000, pf.compile_us/1000, pf.style_us/1000, pf.layout_us/1000, pf.paint_us/1000, pf.scripts, pf.load_us/1000); { extern int g_part_lay_ok, g_part_lay_fail; extern int g_part_stats[3]; extern unsigned long long g_lay_full_us, g_lay_part_us; extern int g_lay_full_n; fprintf(stderr, "partial layout ok=%d fail=%d style partial full=%d lay=%d paint=%d; full layouts %d avg %llu us, partial avg %llu us\n", g_part_lay_ok, g_part_lay_fail, g_part_stats[0], g_part_stats[1], g_part_stats[2], g_lay_full_n, g_lay_full_n ? g_lay_full_us / g_lay_full_n : 0, g_part_lay_ok ? g_lay_part_us / g_part_lay_ok : 0); } }
    for (int rl = getenv("WH_RELOAD") ? atoi(getenv("WH_RELOAD")) : 0; rl > 0; rl--) { for (int i = 0; i < 5; i++) { memset(&r, 0, sizeof(r)); nwe_entry(NWE_OP_TICK, &r); } t0 = nwk_ms(); memset(&r, 0, sizeof(r)); r.url = url; r.data = html; r.len = len; r.w = w; r.h = h; nwe_entry(NWE_OP_LOAD, &r); fprintf(stderr, "reload %lums title='%s' doch=%d\n", nwk_ms() - t0, r.title, r.doch); }
    for (int i = 0; i < 20; i++) { memset(&r, 0, sizeof(r)); nwe_entry(NWE_OP_TICK, &r); }
    if (getenv("WH_PRE")) { memset(&r, 0, sizeof(r)); r.data = getenv("WH_PRE"); nwe_entry(NWE_OP_EVAL, &r); fprintf(stderr, "pre: %s\n", r.out); }
    if (getenv("WH_MS")) { unsigned long t0 = nwk_ms(), lim = (unsigned long)atol(getenv("WH_MS")); while (nwk_ms() - t0 < lim) { memset(&r, 0, sizeof(r)); nwe_entry(NWE_OP_TICK, &r); struct timespec ts = { 0, 10000000 }; nanosleep(&ts, 0); } }
    if (getenv("WH_ACT")) { char* a = strdup(getenv("WH_ACT")); for (char* t = strtok(a, ";"); t; t = strtok(0, ";")) {
            if (t[0] == 'c') { int x = 0, y = 0; sscanf(t + 1, "%d %d", &x, &y); memset(&r, 0, sizeof(r)); r.x = x; r.y = y; r.btn = 1; nwe_entry(NWE_OP_MOVE, &r); memset(&r, 0, sizeof(r)); r.x = x; r.y = y; r.btn = 1; nwe_entry(NWE_OP_DOWN, &r); memset(&r, 0, sizeof(r)); r.x = x; r.y = y; r.btn = 1; nwe_entry(NWE_OP_UP, &r); }
            else if (t[0] == 't') { for (char* q = t + 2; *q; q++) { memset(&r, 0, sizeof(r)); r.key = (unsigned char)*q; nwe_entry(NWE_OP_KEY, &r); } }
            else if (t[0] == 'k') { memset(&r, 0, sizeof(r)); r.key = atoi(t + 1); nwe_entry(NWE_OP_KEY, &r); }
            else if (t[0] == 'd' || t[0] == 'm' || t[0] == 'u' || t[0] == 'r' || t[0] == 'D') {
                int x = 0, y = 0; sscanf(t + 1, "%d %d", &x, &y);
                if (t[0] == 'd') { memset(&r, 0, sizeof(r)); r.x = x; r.y = y; r.btn = 1; r.key = 1; nwe_entry(NWE_OP_DOWN, &r); }
                else if (t[0] == 'm') { memset(&r, 0, sizeof(r)); r.x = x; r.y = y; r.btn = 1; nwe_entry(NWE_OP_MOVE, &r); }
                else if (t[0] == 'u') { memset(&r, 0, sizeof(r)); r.x = x; r.y = y; r.btn = 1; r.key = 1; nwe_entry(NWE_OP_UP, &r); }
                else if (t[0] == 'r') { memset(&r, 0, sizeof(r)); r.x = x; r.y = y; r.btn = 2; nwe_entry(NWE_OP_DOWN, &r); memset(&r, 0, sizeof(r)); r.x = x; r.y = y; r.btn = 2; nwe_entry(NWE_OP_UP, &r); fprintf(stderr, "[ctx] prevented=%d\n", r.ret); }
                else { for (int c = 1; c <= 2; c++) { memset(&r, 0, sizeof(r)); r.x = x; r.y = y; r.btn = 1; r.key = c; nwe_entry(NWE_OP_DOWN, &r); memset(&r, 0, sizeof(r)); r.x = x; r.y = y; r.btn = 1; r.key = c; nwe_entry(NWE_OP_UP, &r); } }
            }
            else if (t[0] == 'F') { int x = 0, y = 0; sscanf(t + 1, "%d %d", &x, &y); memset(&r, 0, sizeof(r)); r.x = x; r.y = y; r.url = "notes.txt"; r.data = "hello file"; r.len = 10; nwe_entry(NWE_OP_DROPFILE, &r); }
            else if (t[0] == 'w') { int x = 0, y = 0, d = 0, e = 0; sscanf(t + 1, "%d %d %d %d", &x, &y, &d, &e); memset(&r, 0, sizeof(r)); r.x = x; r.y = y; r.key = d; r.btn = e; nwe_entry(NWE_OP_WHEEL, &r); }
            for (int i = 0; i < 3; i++) { memset(&r, 0, sizeof(r)); nwe_entry(NWE_OP_TICK, &r); } } }
    if (argc > 6) {
        memset(&r, 0, sizeof(r));
        r.data = argv[6];
        nwe_entry(NWE_OP_EVAL, &r);
        fprintf(stderr, "eval: %s\n", r.out);
        for (int i = 0; i < 5; i++) { memset(&r, 0, sizeof(r)); nwe_entry(NWE_OP_TICK, &r); }
    }
    memset(&r, 0, sizeof(r));
    r.w = w; r.h = h;
    nwe_entry(NWE_OP_LAYOUT, &r);
    int doch = r.doch;
    int H = doch < maxh ? doch : maxh;
    if (H < h) H = h;
    unsigned* fb = calloc((size_t)w * (size_t)H, 4);
    t0 = nwk_ms();
    for (int y = 0; y < H; y += h) {
        memset(&r, 0, sizeof(r));
        r.y = y;
        nwe_entry(NWE_OP_SCROLL, &r);
        int sc = r.scroll;
        unsigned* tmp = calloc((size_t)w * (size_t)h, 4);
        memset(&r, 0, sizeof(r));
        r.fb = tmp; r.stride = w; r.fbw = w; r.fbh = h; r.x = 0; r.y = 0; r.w = w; r.h = h;
        nwe_entry(NWE_OP_PAINT, &r);
        for (int yy = 0; yy < h; yy++) { int dy = sc + yy; if (dy >= y && dy < H) memcpy(fb + (size_t)dy * w, tmp + (size_t)yy * w, (size_t)w * 4); }
        free(tmp);
        memset(&r, 0, sizeof(r));
        r.y = -100000;
        nwe_entry(NWE_OP_SCROLL, &r);
    }
    fprintf(stderr, "paint %lums size %dx%d\n", nwk_ms() - t0, w, H);
    FILE* f = fopen(argv[2], "wb");
    fprintf(f, "P6 %d %d 255\n", w, H);
    for (size_t i = 0; i < (size_t)w * H; i++) { unsigned c = fb[i]; fputc((c >> 16) & 255, f); fputc((c >> 8) & 255, f); fputc(c & 255, f); }
    fclose(f);
    return 0;
}

typedef struct whm { struct whm* next; int wid, type, len; unsigned char* d; } whm;
typedef struct { whm *h, *t; } whq;
static whq wk_inq, wk_outq;
static pthread_mutex_t wk_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wk_cv = PTHREAD_COND_INITIALIZER;
static int wk_started, wk_nid = 1;
static unsigned long long wk_due;
static unsigned char wk_termf[4096];
long wkx_entry(long op, void* arg);
static unsigned long long wh_now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull; }
static void q_push(whq* q, int wid, int type, const void* a, int na, const void* b, int nb) {
    whm* m = malloc(sizeof(whm)); m->next = 0; m->wid = wid; m->type = type; m->len = na + nb; m->d = malloc((size_t)m->len + 1);
    if (na) memcpy(m->d, a, (size_t)na);
    if (nb) memcpy(m->d + na, b, (size_t)nb);
    if (q->t) q->t->next = m; else q->h = m;
    q->t = m;
}
static int q_pop(whq* q, int* wid, int* type, void* buf, int cap) {
    whm* m = q->h;
    if (!m) return -1;
    if (m->len > cap) return -m->len - 4;
    memcpy(buf, m->d, (size_t)m->len);
    *wid = m->wid; *type = m->type;
    q->h = m->next; if (!q->h) q->t = 0;
    int n = m->len; free(m->d); free(m);
    return n;
}
static void* wk_thread(void* a) {
    (void)a;
    for (;;) {
        pthread_mutex_lock(&wk_mu);
        while (!wk_inq.h && (!wk_due || wh_now_ms() < wk_due)) {
            struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_nsec += 2000000; if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
            pthread_cond_timedwait(&wk_cv, &wk_mu, &ts);
        }
        pthread_mutex_unlock(&wk_mu);
        long r = wkx_entry(1, 0);
        pthread_mutex_lock(&wk_mu);
        wk_due = r < 0 ? 0 : wh_now_ms() + (unsigned long long)r;
        pthread_mutex_unlock(&wk_mu);
    }
    return 0;
}
int nwk_wk_ok(void) {
    const char* e = getenv("WH_WK");
    if (e && *e == '0') return 0;
    if (!wk_started) { pthread_t t; wk_started = 1; pthread_create(&t, 0, wk_thread, 0); }
    return 1;
}
int nwk_wk_new(void) { if (!nwk_wk_ok()) return -1; int id = wk_nid++; wk_termf[id % 4096] = 0; return id; }
int nwk_wk_send(int wid, int type, const void* d, int n) {
    if (!wk_started) return -1;
    pthread_mutex_lock(&wk_mu);
    if (type == 3) wk_termf[wid % 4096] = 1;
    if (type == 5) memset(wk_termf, 1, sizeof(wk_termf));
    q_push(&wk_inq, wid, type, d, n, 0, 0);
    pthread_cond_signal(&wk_cv);
    pthread_mutex_unlock(&wk_mu);
    return 0;
}
int nwk_wk_recv(int* wid, int* type, void* buf, int cap) { pthread_mutex_lock(&wk_mu); int r = q_pop(&wk_outq, wid, type, buf, cap); pthread_mutex_unlock(&wk_mu); return r; }
int nwk_wk_busy(void) { pthread_mutex_lock(&wk_mu); int r = wk_started && (wk_inq.h || wk_outq.h || wk_due); pthread_mutex_unlock(&wk_mu); return r; }
int wk_k_recv(int* wid, int* type, void* buf, int cap) { pthread_mutex_lock(&wk_mu); int r = q_pop(&wk_inq, wid, type, buf, cap); pthread_mutex_unlock(&wk_mu); return r; }
int wk_k_pending(void) { pthread_mutex_lock(&wk_mu); int r = wk_inq.h != 0; pthread_mutex_unlock(&wk_mu); return r; }
int wk_k_send(int wid, int type, const void* d, int n, const void* d2, int n2) { pthread_mutex_lock(&wk_mu); q_push(&wk_outq, wid, type, d, n, d2, n2); pthread_mutex_unlock(&wk_mu); return 0; }
int wk_k_term(int wid) { return wk_termf[wid % 4096]; }
unsigned long wk_k_ms(void) { return (unsigned long)wh_now_ms(); }
