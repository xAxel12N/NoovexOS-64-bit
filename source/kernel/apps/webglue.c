#include "include/nvx/base.h"
#include "include/nvx/mm/heap.h"
#include "include/nvx/lib/crypto/nvxtls.h"
#include "include/nvx/net/http.h"
#include "include/nvx/net/tcpip.h"
#include "include/nvx/drivers/net/nic.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/fs/dirtree.h"
#include "include/nvx/gui/wm.h"
#include "include/nvx/net/cookies.h"
#include "include/nvx/apps/browser.h"
#include "include/nvx/apps/nwe_api.h"
#include "include/nvx/apps/webglue.h"
#include "include/nvx/core/media.h"

extern volatile u64 g_ticks;
long nwe_call_stack(long (*fn)(long, void*), long op, void* arg, void* top);

#define ESTACK_SZ (6u * 1024u * 1024u)
static u8* g_estack;
static int g_in_engine;
static int g_web_ok;

const char* WEB_UA = "Mozilla/5.0 (X11; NoovexOS x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36 Noovex/9.82";

int web_init(void) {
    if (g_web_ok) return 1;
    if (!g_estack) g_estack = kmalloc(ESTACK_SZ);
    if (!g_estack) return 0;
    static nwe_req r;
    for (unsigned i = 0; i < sizeof(r); i++) ((u8*)&r)[i] = 0;
    web_call(NWE_OP_INIT, &r);
    g_web_ok = 1;
    return 1;
}

int g_web_failed = 0;
int g_web_aborted = 0;
static unsigned long g_call_t0, g_pump_t;
extern volatile int g_busy_esc;
extern void busy_pump(void);
int nwk_busy(void) {
    unsigned long t = nwk_ms();
    if (t - g_pump_t >= 10) { g_pump_t = t; busy_pump(); }
    if (g_busy_esc && t - g_call_t0 > 1500) return 1;
    return 0;
}
long web_call(int op, nwe_req* r) {
    if (!g_estack) { g_estack = kmalloc(ESTACK_SZ); if (!g_estack) return -1; }
    if (g_in_engine) return nwe_entry(op, r);
    g_in_engine = 1;
    g_call_t0 = nwk_ms(); g_busy_esc = 0;
    long v = nwe_call_stack(nwe_entry, op, r, g_estack + ESTACK_SZ - 64);
    g_in_engine = 0;
    if (v == -1 && op != NWE_OP_INIT) g_web_failed = 1;
    return v;
}
int web_busy(void) { return g_in_engine; }
extern void nwe_fail(const char* why);
int g_web_faults = 0;
static void web_fault_land(void) { nwe_fail("the page crashed the engine (CPU exception); it was stopped safely"); }
int web_fault_redirect(u64* rip, u64* rsp) {
    if (!g_in_engine || !g_estack) return 0;
    u64 lo = (u64)g_estack, hi = lo + ESTACK_SZ, sp = *rsp;
    if (sp >= lo + 0x10000 && sp <= hi) sp -= 2048;
    else if (sp < lo + 0x10000 && sp + 0x100000 > lo) sp = lo + ESTACK_SZ / 2;
    else return 0;
    if (++g_web_faults > 64) return 0;
    *rsp = (sp & ~15ULL) - 8; *rip = (u64)web_fault_land;
    return 1;
}

static char g_con[64][200];
static int g_conn, g_conh;
void web_console_add(const char* s) {
    int i = g_conh % 64;
    int k = 0;
    for (; s[k] && k < 199; k++) g_con[i][k] = s[k] == '\n' ? ' ' : s[k];
    g_con[i][k] = 0;
    g_conh++;
    if (g_conn < 64) g_conn++;
}
int web_console_get(int idx, char* out, int cap) {
    if (idx < 0 || idx >= g_conn) return -1;
    int i = (g_conh - g_conn + idx) % 64;
    int k = 0;
    for (; g_con[i][k] && k < cap - 1; k++) out[k] = g_con[i][k];
    out[k] = 0;
    return 0;
}
int web_console_count(void) { return g_conn; }

void nwk_log(const char* s) { web_console_add(s); klog(s); }
void nwk_alert(const char* s) { toast(s); web_console_add(s); }
unsigned long nwk_ms(void) { return (unsigned long)(g_ticks * 10ULL); }
long long nwk_epoch_ms(void) { return (long long)k_realtime_ms(); }
int nwk_tz_min(void) { return 0; }
void nwk_pump(void) {}
void* nwk_alloc_arena(unsigned long n) { extern unsigned long long heap_cur, heap_top; if (n > (32ul << 20) && heap_top > heap_cur && heap_top - heap_cur < (unsigned long long)n + (320ull << 20)) return 0; return kmalloc(n); }
void nwk_free(void* p) { kfree(p); }
int nwk_inflate(const unsigned char* src, int slen, unsigned char* dst, int dcap) { if (slen < 3) return -1; int r = pz_inflate_raw(src + 2, slen - 2, dst, dcap); return r; }
int nwk_media_new(const char* url, int split, int w, int h, long long start_ms, int paused) {
    int id = media_new();
    if (id < 0) return -1;
    if (media_start(id, url, split, w, h, start_ms, paused) < 0) { media_free(id); return -1; }
    return id;
}
void nwk_media_cmd(int id, int cmd, long long a, long long b) { media_cmd(id, cmd, a, b); }
int nwk_media_status(int id, long long* v) {
    struct media_status st;
    if (media_get_status(id, &st) < 0) return -1;
    v[0] = st.cur_ms; v[1] = st.dur_ms; v[2] = st.flags; v[3] = st.vw; v[4] = st.vh; v[5] = st.buffered_pct;
    return 0;
}
int nwk_media_append(int id, int stream, const unsigned char* d, int len) { return media_append(id, stream, d, len); }
void nwk_media_end(int id, int stream) { media_end_stream(id, stream); }
int nwk_media_restart(int id, long long start_ms, int keep) { return media_restart(id, start_ms, keep); }
void nwk_media_free(int id) { media_free(id); }
long long nwk_media_len(int id, int stream) { return (long long)media_stream_len(id, stream); }
const unsigned* nwk_media_frame(int id, int* w, int* h, unsigned long long* seq) { u64 s = 0; const u32* f = media_frame(id, w, h, &s); if (seq) *seq = s; return (const unsigned*)f; }
int nwk_cookie_get(const char* url, int forjs, char* out, int cap) { return cookie_header(url, forjs, out, cap); }
void nwk_cookie_set(const char* url, const char* line, int fromjs) { cookie_set(url, line, fromjs); }
int nwk_store_get(const char* origin, int session, const char* key, char* out, int cap) { return webstore_get(origin, session, key, out, cap); }
void nwk_store_set(const char* origin, int session, const char* key, const char* val) { webstore_set(origin, session, key, val); }
void nwk_store_clear(const char* origin, int session) { webstore_clear(origin, session); }
int nwk_store_key(const char* origin, int session, int idx, char* out, int cap) { return webstore_key(origin, session, idx, out, cap); }
int nwk_store_len(const char* origin, int session) { return webstore_len(origin, session); }

static int lc_(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int ieqn(const char* a, const char* b, int n) { for (int i = 0; i < n; i++) { if (lc_((u8)a[i]) != lc_((u8)b[i])) return 0; if (!a[i]) return 1; } return 1; }
static int sl(const char* s) { int n = 0; while (s[n]) n++; return n; }
static char* put(char* p, const char* s) { while (*s) *p++ = *s++; return p; }
static char* putn(char* p, const char* s, int n) { for (int i = 0; i < n; i++) *p++ = s[i]; return p; }
static char* putu(char* p, unsigned v) { char t[12]; int n = 0; if (!v) t[n++] = '0'; while (v) { t[n++] = (char)('0' + v % 10); v /= 10; } while (n) *p++ = t[--n]; return p; }

typedef struct { int secure; char host[256]; int port; char path[2048]; } purl;
static int parse_url(const char* u, purl* o) {
    o->secure = 0;
    o->port = 80;
    if (ieqn(u, "https://", 8)) { o->secure = 1; o->port = 443; u += 8; }
    else if (ieqn(u, "http://", 7)) u += 7;
    else return -1;
    int h = 0;
    while (*u && *u != '/' && *u != '?' && *u != '#' && *u != ':' && h < 255) o->host[h++] = (char)lc_((u8)*u++);
    o->host[h] = 0;
    if (*u == ':') { u++; int p = 0; while (*u >= '0' && *u <= '9') p = p * 10 + (*u++ - '0'); if (p > 0 && p < 65536) o->port = p; }
    while (*u && *u != '/' && *u != '?' && *u != '#') u++;
    int k = 0;
    if (*u != '/') o->path[k++] = '/';
    while (*u && *u != '#' && k < 2047) { char c = *u++; if (c == ' ') { if (k < 2045) { o->path[k++] = '%'; o->path[k++] = '2'; o->path[k++] = '0'; } } else o->path[k++] = c; }
    o->path[k] = 0;
    return h > 0 ? 0 : -1;
}
static void resolve(const char* base, const char* rel, char* out, int cap) {
    int n = 0;
    while (*rel == ' ') rel++;
    for (const char* p = rel; *p && *p != '/' && *p != '?'; p++) if (*p == ':' ) { for (int i = 0; rel[i] && n < cap - 1; i++) out[n++] = rel[i]; out[n] = 0; return; }
    const char* sch = base;
    int schl = 0;
    while (sch[schl] && sch[schl] != ':') schl++;
    if (rel[0] == '/' && rel[1] == '/') { for (int i = 0; i <= schl && n < cap - 1; i++) out[n++] = base[i]; for (int i = 0; rel[i] && n < cap - 1; i++) out[n++] = rel[i]; out[n] = 0; return; }
    const char* hs = base + schl + 1;
    while (*hs == '/') hs++;
    const char* he = hs;
    while (*he && *he != '/' && *he != '?' && *he != '#') he++;
    for (const char* p = base; p < he && n < cap - 1; p++) out[n++] = *p;
    if (rel[0] == '/') { for (int i = 0; rel[i] && n < cap - 1; i++) out[n++] = rel[i]; out[n] = 0; return; }
    const char* pe = he;
    while (*pe && *pe != '?' && *pe != '#') pe++;
    const char* ls = he;
    for (const char* p = he; p < pe; p++) if (*p == '/') ls = p;
    if (ls == he && (*he != '/')) { if (n < cap - 1) out[n++] = '/'; }
    else for (const char* p = he; p <= ls && n < cap - 1; p++) out[n++] = *p;
    for (int i = 0; rel[i] && n < cap - 1; i++) out[n++] = rel[i];
    out[n] = 0;
}

static u8* g_rbuf;
static u8* g_dbuf;
#define RBUF_SZ (12u * 1024u * 1024u)
#define DBUF_SZ (24u * 1024u * 1024u)

static int http_once(purl* u, const char* req, int rl, int* outn) {
    u32 ip;
    *outn = 0;
    if (!parse_ip(u->host, &ip)) { if (dns_query(u->host, &ip) <= 0) return -100; }
    if (!tcps_open(ip, (u16)u->port)) return -2;
    int n;
    if (u->secure) {
        u8 ent[64];
        entropy_get(ent, 64);
        tls_io* io = &g_tls_io;
        io->wr = tcps_wr; io->rd = tcps_rd; io->ctx = 0; io->rlen = 0; io->rpos = 0;
        n = tls_get(io, u->host, sl(u->host), ent, req, rl, g_rbuf, (int)RBUF_SZ - 1);
        secure_zero(ent, 64);
        if (n < 0) { char b[160], *p = b; p = put(p, "TLS failed: "); p = put(p, g_tls_why ? g_tls_why : "?"); *p = 0; netlog(b); return -5; }
    } else {
        tcps_wr(0, (const u8*)req, rl);
        n = 0;
        for (;;) { int k = tcps_rd(0, g_rbuf + n, (int)RBUF_SZ - 1 - n); if (k <= 0) break; n += k; if (n >= (int)RBUF_SZ - 1) break; }
    }
    *outn = n;
    return n > 0 ? 0 : -3;
}

static int find_hdr_end(const u8* b, int n) { for (int i = 0; i + 3 < n; i++) if (b[i] == '\r' && b[i + 1] == '\n' && b[i + 2] == '\r' && b[i + 3] == '\n') return i + 2; for (int i = 0; i + 1 < n; i++) if (b[i] == '\n' && b[i + 1] == '\n') return i + 1; return -1; }
static int hdr_get(const u8* b, int he, const char* name, char* out, int cap) {
    int nl = sl(name);
    for (int i = 0; i < he; i++) {
        if (i && b[i - 1] != '\n') continue;
        if (i + nl < he && ieqn((const char*)b + i, name, nl) && b[i + nl] == ':') {
            int j = i + nl + 1;
            while (j < he && (b[j] == ' ' || b[j] == '\t')) j++;
            int k = 0;
            while (j < he && b[j] != '\r' && b[j] != '\n' && k < cap - 1) out[k++] = (char)b[j++];
            out[k] = 0;
            return 1;
        }
    }
    out[0] = 0;
    return 0;
}
static int dechunk(u8* b, int n) {
    int r = 0, w = 0;
    while (r < n) {
        int sz = 0, any = 0;
        while (r < n) { u8 c = b[r]; int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1; if (d < 0) break; sz = sz * 16 + d; r++; any = 1; }
        while (r < n && b[r] != '\n') r++;
        r++;
        if (!any || sz == 0) break;
        if (sz > n - r) sz = n - r;
        for (int i = 0; i < sz; i++) b[w++] = b[r + i];
        r += sz;
        if (r < n && b[r] == '\r') r++;
        if (r < n && b[r] == '\n') r++;
    }
    return w;
}

unsigned long g_fetch_ms = 0; int g_fetch_n = 0;
static int nwk_fetch_real(const char* url, const char* method, const char* ctype, const char* body, int blen, const char* referer, nwk_resp* r);
int nwk_fetch(const char* url, const char* method, const char* ctype, const char* body, int blen, const char* referer, nwk_resp* r) {
    unsigned long t = nwk_ms(); int rc = nwk_fetch_real(url, method, ctype, body, blen, referer, r); g_fetch_ms += nwk_ms() - t; g_fetch_n++; return rc;
}
static int nwk_fetch_real(const char* url, const char* method, const char* ctype, const char* body, int blen, const char* referer, nwk_resp* r) {
    for (unsigned i = 0; i < sizeof(*r); i++) ((u8*)r)[i] = 0;
    if (!g_rbuf) g_rbuf = kmalloc(RBUF_SZ);
    if (!g_dbuf) g_dbuf = kmalloc(DBUF_SZ);
    if (!g_rbuf || !g_dbuf) return -1;
    static char cur[2048];
    static char meth[16];
    int k = 0;
    for (; url[k] && k < 2047; k++) cur[k] = url[k];
    cur[k] = 0;
    for (k = 0; method && method[k] && k < 15; k++) meth[k] = method[k];
    meth[k] = 0;
    if (!meth[0]) { meth[0] = 'G'; meth[1] = 'E'; meth[2] = 'T'; meth[3] = 0; }
    const char* b = body;
    int bl = blen;
    for (int hop = 0; hop < 8; hop++) {
        static purl u;
        if (parse_url(cur, &u) < 0) return -1;
        int reqcap = 8192 + 4096 + bl;
        char* req = kmalloc((unsigned long)reqcap);
        if (!req) return -1;
        char* p = req;
        p = put(p, meth); *p++ = ' '; p = put(p, u.path); p = put(p, " HTTP/1.1\r\nHost: "); p = put(p, u.host);
        if ((u.secure && u.port != 443) || (!u.secure && u.port != 80)) { *p++ = ':'; p = putu(p, (unsigned)u.port); }
        p = put(p, "\r\nUser-Agent: "); p = put(p, WEB_UA);
        p = put(p, "\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/png,image/jpeg,*/*;q=0.8\r\nAccept-Language: en-US,en;q=0.9\r\nAccept-Encoding: gzip\r\nConnection: close\r\n");
        static char ck[6144];
        if (cookie_header(cur, 0, ck, sizeof(ck)) > 0) { p = put(p, "Cookie: "); p = put(p, ck); p = put(p, "\r\n"); }
        if (referer && referer[0] && (ieqn(referer, "http", 4))) { p = put(p, "Referer: "); int rl = sl(referer); if (rl > 1500) rl = 1500; p = putn(p, referer, rl); p = put(p, "\r\n"); }
        if (b && bl >= 0 && (meth[0] == 'P' || meth[0] == 'D')) {
            p = put(p, "Content-Type: "); p = put(p, ctype && ctype[0] ? ctype : "application/x-www-form-urlencoded"); p = put(p, "\r\n");
            p = put(p, "Content-Length: "); p = putu(p, (unsigned)bl); p = put(p, "\r\n");
        }
        if (u.secure == 0 && 0) {}
        p = put(p, "\r\n");
        if (b && bl > 0 && (meth[0] == 'P' || meth[0] == 'D')) p = putn(p, b, bl);
        int n;
        int rc = http_once(&u, req, (int)(p - req), &n);
        kfree(req);
        if (rc < 0) return rc;
        int he = find_hdr_end(g_rbuf, n);
        if (he < 0) he = n;
        int status = 0;
        { int i = 0; while (i < n && g_rbuf[i] != ' ') i++; i++; while (i < n && g_rbuf[i] >= '0' && g_rbuf[i] <= '9') status = status * 10 + (g_rbuf[i++] - '0'); }
        for (int i = 0; i < he; i++) {
            if (i && g_rbuf[i - 1] != '\n') continue;
            if (i + 11 < he && ieqn((const char*)g_rbuf + i, "set-cookie:", 11)) {
                int j = i + 11;
                while (j < he && g_rbuf[j] == ' ') j++;
                static char line[2048];
                int q = 0;
                while (j < he && g_rbuf[j] != '\r' && g_rbuf[j] != '\n' && q < 2047) line[q++] = (char)g_rbuf[j++];
                line[q] = 0;
                cookie_set(cur, line, 0);
            }
        }
        if ((status == 301 || status == 302 || status == 303 || status == 307 || status == 308) && hop < 7) {
            static char loc[2048];
            if (hdr_get(g_rbuf, he, "location", loc, sizeof(loc)) && loc[0]) {
                static char nu[2048];
                resolve(cur, loc, nu, sizeof(nu));
                int i = 0;
                for (; nu[i] && i < 2047; i++) cur[i] = nu[i];
                cur[i] = 0;
                if (status == 303 || ((status == 301 || status == 302) && meth[0] == 'P')) { meth[0] = 'G'; meth[1] = 'E'; meth[2] = 'T'; meth[3] = 0; b = 0; bl = 0; }
                continue;
            }
        }
        int bo = he + 2;
        if (g_rbuf[he] == '\n') bo = he + 1;
        if (bo > n) bo = n;
        int len = n - bo;
        u8* data = g_rbuf + bo;
        static char te[96], ce[96];
        if (hdr_get(g_rbuf, he, "transfer-encoding", te, sizeof(te))) { for (int i = 0; te[i]; i++) if (ieqn(te + i, "chunked", 7)) { len = dechunk(data, len); break; } }
        hdr_get(g_rbuf, he, "content-type", r->ctype, sizeof(r->ctype));
        if (hdr_get(g_rbuf, he, "content-encoding", ce, sizeof(ce))) {
            if (ieqn(ce, "gzip", 4) || ieqn(ce, "x-gzip", 6)) { int dl = gunzip(data, len, g_dbuf, (int)DBUF_SZ - 1); if (dl >= 0) { data = g_dbuf; len = dl; } }
            else if (ieqn(ce, "deflate", 7)) { int dl = pz_inflate_raw(data + 2, len - 2, g_dbuf, (int)DBUF_SZ - 1); if (dl < 0) dl = pz_inflate_raw(data, len, g_dbuf, (int)DBUF_SZ - 1); if (dl >= 0) { data = g_dbuf; len = dl; } }
        }
        if (len < 0) len = 0;
        r->body = kmalloc((unsigned long)len + 1);
        if (!r->body) return -1;
        for (int i = 0; i < len; i++) r->body[i] = (char)data[i];
        r->body[len] = 0;
        r->len = len;
        r->status = status;
        int i = 0;
        for (; cur[i] && i < (int)sizeof(r->final_url) - 1; i++) r->final_url[i] = cur[i];
        r->final_url[i] = 0;
        cookies_flush(0);
        return 0;
    }
    return -6;
}

void web_tick_flush(void) { cookies_flush(0); }
