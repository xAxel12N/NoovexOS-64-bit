int fs_save_bin(const char* name, const u8* data, int len, int folder);

extern volatile u64 g_ticks;
long nwe_call_stack(long (*fn)(long, void*), long op, void* arg, void* top);

#define ESTACK_SZ (16u * 1024u * 1024u)
static u8* g_estack;
static int g_in_engine;
static int g_web_ok;

const char* WEB_UA = "Mozilla/5.0 (X11; NoovexOS x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36 Noovex/10.60";

int web_init(void) {
    if (g_web_ok) return 1;
    if (!g_estack) { g_estack = kmalloc(ESTACK_SZ); extern int mm_guard_range_bottom(void*, unsigned long); if (g_estack) mm_guard_range_bottom(g_estack, ESTACK_SZ); }
    if (!g_estack) return 0;
    static nwe_req r;
    for (unsigned i = 0; i < sizeof(r); i++) ((u8*)&r)[i] = 0;
    web_call(NWE_OP_INIT, &r);
    g_web_ok = 1;
    return 1;
}

extern nwk_netstat g_netstat;
nwk_slow g_nwk_slow;
int g_web_failed = 0;
int g_web_aborted = 0;
static unsigned long g_call_t0, g_pump_t, g_call_s0;
extern volatile int g_busy_esc;
extern void busy_pump(void);
int g_web_pump_ok, g_web_nest;
static unsigned long g_susp_total;
unsigned long nwk_ms_active(void) { return nwk_ms() - g_susp_total; }
int web_nested(void) { return g_web_nest; }
int nwk_busy(void) {
    unsigned long t = nwk_ms();
    if (g_web_pump_ok && g_in_engine && !g_web_nest) {
        if (t - g_pump_t >= 30) {
            extern void desk_nested_step(void); extern u64 hr_ns(void);
            u64 a = hr_ns(); unsigned long m0 = nwk_ms();
            g_web_nest = 1; desk_nested_step(); g_web_nest = 0;
            g_susp_total += nwk_ms() - m0; g_pump_t = nwk_ms();
            g_netstat.pump_us += (hr_ns() - a) / 1000ull; g_netstat.pumps++;
        }
    } else if (t - g_pump_t >= 10) { g_pump_t = t; extern u64 hr_ns(void); u64 a = hr_ns(); busy_pump(); g_netstat.pump_us += (hr_ns() - a) / 1000ull; g_netstat.pumps++; }
    if (g_busy_esc && t - g_call_t0 > 1500) return 1;
    return 0;
}
long web_call(int op, nwe_req* r) {
    if (!g_estack) { { g_estack = kmalloc(ESTACK_SZ); extern int mm_guard_range_bottom(void*, unsigned long); if (g_estack) mm_guard_range_bottom(g_estack, ESTACK_SZ); } if (!g_estack) return -1; }
    if (g_web_nest) return -3;
    if (g_in_engine) return nwe_entry(op, r);
    g_in_engine = 1;
    g_call_t0 = nwk_ms(); g_call_s0 = g_susp_total; g_busy_esc = 0;
    long v = nwe_call_stack(nwe_entry, op, r, g_estack + ESTACK_SZ - 64);
    g_in_engine = 0;
    if (v == -1 && op != NWE_OP_INIT) g_web_failed = 1;
    return v;
}
int web_busy(void) { return g_in_engine; }
unsigned long web_busy_ms(void) { return g_in_engine ? nwk_ms() - g_call_t0 - (g_susp_total - g_call_s0) : 0; }
void* nwe_stack_top(void) { return g_estack ? (void*)(g_estack + ESTACK_SZ) : 0; }
extern void nwe_fail(const char* why);
int g_web_faults = 0;
extern void nwe2_fail(const char* why);
extern void nwe3_fail(const char* why);
extern void nwe4_fail(const char* why);
extern void nwe5_fail(const char* why);
extern void nwe6_fail(const char* why);
long nwe2_entry(long op, void* req);
long nwe3_entry(long op, void* req);
long nwe4_entry(long op, void* req);
long nwe5_entry(long op, void* req);
long nwe6_entry(long op, void* req);
static void (*const g_ffn[5])(const char*) = { nwe2_fail, nwe3_fail, nwe4_fail, nwe5_fail, nwe6_fail };
static long (*const g_fent[5])(long, void*) = { nwe2_entry, nwe3_entry, nwe4_entry, nwe5_entry, nwe6_entry };
static int g_in_frame;
static void web_fault_land(void) { if (g_in_frame >= 1 && g_in_frame <= 5) g_ffn[g_in_frame - 1](g_in_frame == 5 ? "the pop-up window crashed the engine (CPU exception); it was stopped safely" : "the embedded page crashed the engine (CPU exception); it was stopped safely"); else nwe_fail("the page crashed the engine (CPU exception); it was stopped safely"); }
int web_fault_redirect(u64* rip, u64* rsp) {
    if (!g_in_engine || !g_estack || g_web_nest) return 0;
    { char b[80]; const char* h = "0123456789abcdef"; char* q = b; const char* t = "web fault rip="; while (*t) *q++ = *t++; for (int i = 15; i >= 0; i--) *q++ = h[(*rip >> (i * 4)) & 15]; t = " rsp="; while (*t) *q++ = *t++; for (int i = 15; i >= 0; i--) *q++ = h[(*rsp >> (i * 4)) & 15]; *q = 0; klog(b); }
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
#define ERR_N 64
static char g_err[ERR_N][1200];
static unsigned long g_err_t[ERR_N];
static int g_errh, g_errn;
void nwk_error(const char* s) {
    int i = g_errh % ERR_N, k = 0;
    for (; s[k] && k < 1199; k++) g_err[i][k] = (s[k] == '\n' || s[k] == '\r') ? ' ' : s[k];
    g_err[i][k] = 0; g_err_t[i] = nwk_ms(); g_errh++; if (g_errn < ERR_N) g_errn++;
    web_console_add(s); klog(s);
}
int web_err_count(void) { return g_errn; }
int web_err_total(void) { return g_errh; }
void nwk_error_mark(const char* url) { char b[1200]; int k = 0; const char* p = "Page "; while (*p) b[k++] = *p++; for (int i = 0; url[i] && k < 1199; i++) b[k++] = url[i]; b[k] = 0; int i = g_errh % ERR_N; for (int j = 0; j <= k; j++) g_err[i][j] = b[j]; g_err_t[i] = nwk_ms(); g_errh++; if (g_errn < ERR_N) g_errn++; }
int web_err_get(int idx, char* out, int cap, unsigned long* t) {
    if (idx < 0 || idx >= g_errn) return -1;
    int i = (g_errh - g_errn + idx) % ERR_N, k = 0;
    for (; g_err[i][k] && k < cap - 1; k++) out[k] = g_err[i][k];
    out[k] = 0; if (t) *t = g_err_t[i]; return k;
}
void nwk_alert(const char* s) { toast(s); web_console_add(s); }
unsigned long nwk_ms(void) { return (unsigned long)(g_ticks * 10ULL); }
extern char g_clip[]; extern int g_clip_len;
int nwk_clip_get(char* out, int cap) { int n = g_clip_len; if (n > cap - 1) n = cap - 1; if (n < 0) n = 0; for (int i = 0; i < n; i++) out[i] = g_clip[i]; out[n] = 0; return n; }
void nwk_clip_set(const char* s, int n) { if (n > 65535) n = 65535; if (n < 0) n = 0; for (int i = 0; i < n; i++) g_clip[i] = s[i]; g_clip[n] = 0; g_clip_len = n; }
unsigned long long nwk_us(void) { extern u64 hr_ns(void); return hr_ns() / 1000ULL; }
long long nwk_epoch_ms(void) { return (long long)k_realtime_ms(); }
static long long tz_days(int y, int m, int d) { y -= m <= 2; long long era = (y >= 0 ? y : y - 399) / 400; unsigned yoe = (unsigned)(y - era * 400); unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1; unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy; return era * 146097 + (long long)doe - 719468; }
static int tz_year(long long days) { long long z = days + 719468; long long era = (z >= 0 ? z : z - 146096) / 146097; unsigned doe = (unsigned)(z - era * 146097); unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; long long y = (long long)yoe + era * 400; unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100); unsigned mp = (5 * doy + 2) / 153; unsigned m = mp + (mp < 10 ? 3 : (unsigned)-9); return (int)(y + (m <= 2)); }
static long long tz_nth_sun(int y, int m, int n) { long long d = tz_days(y, m, 1); int w = (int)((d + 4) % 7); if (w < 0) w += 7; long long first = d + ((7 - w) % 7); return first + 7 * (n - 1); }
static long long tz_last_sun(int y, int m) { long long d = tz_days(y, m + 1 > 12 ? 1 : m + 1, 1) - 1; if (m == 12) d = tz_days(y + 1, 1, 1) - 1; int w = (int)((d + 4) % 7); if (w < 0) w += 7; return d - w; }
int nwk_tz_index(void) { extern int nvx_tz_index(void); return nvx_tz_index(); }
int nwk_tz_min(void) {
    extern int nvx_tz_index(void);
    static const short base[7] = { -480, -300, 0, 60, 180, 330, 540 };
    int i = nvx_tz_index(); if (i < 0 || i > 6) return 0;
    long long ms = (long long)k_realtime_ms(); long long s = ms / 1000; int y = tz_year(s / 86400);
    if (i == 2 || i == 3) { long long a = tz_last_sun(y, 3) * 86400 + 3600, b = tz_last_sun(y, 10) * 86400 + 3600; return base[i] + (s >= a && s < b ? 60 : 0); }
    if (i == 0 || i == 1) { long long a = tz_nth_sun(y, 3, 2) * 86400 + 7200 - base[i] * 60, b = tz_nth_sun(y, 11, 1) * 86400 + 7200 - (base[i] + 60) * 60; return base[i] + (s >= a && s < b ? 60 : 0); }
    return base[i];
}
void nwk_pump(void) {}
void* nwk_alloc_arena(unsigned long n) { extern unsigned long long heap_cur, heap_top; extern int mm_guard(u64); if (n > (32ul << 20) && heap_top > heap_cur && heap_top - heap_cur < (unsigned long long)n + (320ull << 20)) return 0; u8* p = kmalloc(n + 8192); if (!p) return 0; u64 g = ((u64)(p + n) + 4095ull) & ~4095ull; if (g + 4096 <= (u64)(p + n + 8192)) mm_guard(g); return p; }
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

typedef struct { int secure; char host[256]; int port; char path[4096]; } purl;
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
    while (*u && *u != '#' && k < 4095) { char c = *u++; if (c == ' ') { if (k < 4093) { o->path[k++] = '%'; o->path[k++] = '2'; o->path[k++] = '0'; } } else o->path[k++] = c; }
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

static int find_hdr_end(const u8* b, int n);
static int hdr_get(const u8* b, int he, const char* name, char* out, int cap);
#define PC_N 32
typedef struct { int used, sock, secure, port; char host[256]; u64 last; int reused, busy; tls_io io; tls_sess ss; } pconn;
static pconn* g_pc;
nwk_netstat g_netstat;
static unsigned long long nus(void) { extern u64 hr_ns(void); return hr_ns() / 1000ULL; }
static u64 g_pc_tmo = 2000; static int g_pc_abort;
struct nfib; static struct nfib* g_nf_cur; static void nf_yield(void); static void nf_step_wait(void);
static void pc_wait(void) { if (g_nf_cur) { nf_yield(); return; } lxs_pump(); nf_step_wait(); if (nwk_busy() && g_in_engine) g_pc_abort = 1; u16 a = pit_read(); while ((u16)(a - pit_read()) < 120) {} }
static int pc_wr(void* c, const u8* b, int n) {
    pconn* p = (pconn*)c; int off = 0; u64 t0 = g_ticks;
    while (off < n) { int k = lxs_send(p->sock, b + off, n - off, 0, 0, 0); if (k > 0) { off += k; t0 = g_ticks; continue; } if (k != -1000) return -1; if (g_ticks - t0 > g_pc_tmo || g_pc_abort) return -1; pc_wait(); }
    return n;
}
extern int g_nwk_kind;
static u64 nf_tmo(void);
static u64 pc_rd_limit(void) { if (g_nf_cur) return nf_tmo(); if (g_pc_tmo != 2000) return g_pc_tmo; if (g_nwk_kind == 2) return 30000; if (g_nwk_kind == 1) return 12000; return g_pc_tmo; }
static int pc_rd(void* c, u8* b, int n) {
    pconn* p = (pconn*)c; u64 t0 = g_ticks, ka = g_ticks, lim = pc_rd_limit();
    for (;;) { int k = lxs_recv(p->sock, b, n, 0, 0, 0); if (k > 0) return k; if (k != -1000) return 0; if (g_ticks - t0 > lim || g_pc_abort) return 0; if (g_ticks - ka >= 4500) { lxs_keepalive(p->sock); ka = g_ticks; } pc_wait(); }
}
static int pc_streq(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void pc_close(pconn* p) { p->busy = 0; if (!p->used) return; if (p->secure) tls_sess_wipe(&p->ss); lxs_release(p->sock); p->used = 0; }
static void pc_put(pconn* p) { p->busy = 0; }
static int pc_alive(pconn* p) { if (!p->used) return 0; int e = lxs_poll(p->sock); if (e & (0x10 | 0x2000 | 8)) return 0; if (g_lxs[p->sock].st != LXS_EST) return 0; if (g_lxs[p->sock].rxn > 0) return 0; if (g_ticks - p->last > 5000) return 0; return 1; }
void nwk_conn_reset(void) { if (!g_pc) return; for (int i = 0; i < PC_N; i++) pc_close(&g_pc[i]); }
static pconn* pc_get(purl* u, int fresh, int* err) {
    if (!g_pc) { g_pc = kmalloc(sizeof(pconn) * PC_N); if (!g_pc) { *err = -1; return 0; } for (unsigned i = 0; i < sizeof(pconn) * PC_N; i++) ((u8*)g_pc)[i] = 0; }
    for (int i = 0; i < PC_N; i++) { pconn* p = &g_pc[i]; if (!p->used || p->busy) continue; if (!pc_alive(p)) { pc_close(p); continue; } }
    if (!fresh) for (int i = 0; i < PC_N; i++) { pconn* p = &g_pc[i]; if (p->used && !p->busy && p->port == u->port && p->secure == u->secure && pc_streq(p->host, u->host)) { p->reused = 1; p->busy = 1; g_netstat.reused++; return p; } }
    int slot = -1; u64 old = ~0ull;
    for (int i = 0; i < PC_N; i++) { if (!g_pc[i].used && !g_pc[i].busy) { slot = i; break; } if (!g_pc[i].busy && g_pc[i].last < old) { old = g_pc[i].last; slot = i; } }
    if (slot < 0) { *err = -2; return 0; }
    pconn* p = &g_pc[slot]; pc_close(p); p->busy = 1;
    unsigned long long t0 = nus();
    u32 ip;
    if (!parse_ip(u->host, &ip)) { if (dns_query(u->host, &ip) <= 0) { p->busy = 0; *err = -100; return 0; } }
    unsigned long long t1 = nus(); g_netstat.dns_us += t1 - t0;
    lxs_pump(); lxs_pump();
    int sk = lxs_new(1); if (sk < 0) { p->busy = 0; *err = -2; return 0; }
    int r = lxs_connect(sk, ip, (u16)u->port, 1); u64 tw = g_ticks;
    u64 ts = g_ticks;
    int nsyn = 0;
    while (r == -115 || r == -114) { if (g_ticks - tw > (g_pc_tmo < 1500 ? g_pc_tmo : 1500) || g_pc_abort) break; pc_wait(); if (g_ticks - ts >= 25 && g_ticks - tw < 300) { lxs_resyn(sk); ts = g_ticks; nsyn++; } r = lxs_connect(sk, ip, (u16)u->port, 1); }
    if (r != 0) { lxs_release(sk); p->busy = 0; *err = -2; return 0; }
    unsigned long long t2 = nus(); g_netstat.connect_us += t2 - t1;
    p->used = 1; p->busy = 1; p->sock = sk; p->secure = u->secure; p->port = u->port; p->reused = 0; p->last = g_ticks;
    int h = 0; for (; u->host[h] && h < 255; h++) p->host[h] = u->host[h]; p->host[h] = 0;
    if (u->secure) {
        p->io.wr = pc_wr; p->io.rd = pc_rd; p->io.ctx = p; p->io.rlen = 0; p->io.rpos = 0;
        u8 ent[64]; entropy_get(ent, 64);
        int tr = tls_connect_sess(&p->io, u->host, sl(u->host), ent, &p->ss);
        secure_zero(ent, 64);
        if (tr != 0) { char b[160], *q = b; q = put(q, "TLS failed: "); q = put(q, g_tls_why ? g_tls_why : "?"); *q = 0; netlog(b); pc_close(p); *err = -5; return 0; }
        g_netstat.tls_us += nus() - t2; g_netstat.handshakes++;
    }
    g_netstat.conns++;
    { unsigned long long t3 = nus(); char b[64], *q = b; int k = 0; while (u->host[k] && k < 22) { *q++ = u->host[k]; k++; } q = put(q, " d"); q = putu(q, (u32)((t1 - t0) / 1000)); q = put(q, " t"); q = putu(q, (u32)((t2 - t1) / 1000)); if (u->secure) { q = put(q, " s"); q = putu(q, (u32)((t3 - t2) / 1000)); } if (nsyn) { q = put(q, " r"); q = putu(q, (u32)nsyn); } *q = 0; netlog(b);
      if ((t3 - t0) / 1000 > g_nwk_slow.ms) { g_nwk_slow.ms = (u32)((t3 - t0) / 1000); g_nwk_slow.dns = (u32)((t1 - t0) / 1000); g_nwk_slow.tcp = (u32)((t2 - t1) / 1000); g_nwk_slow.tls = (u32)((t3 - t2) / 1000); k = 0; while (u->host[k] && k < 47) { g_nwk_slow.host[k] = u->host[k]; k++; } g_nwk_slow.host[k] = 0; }
      g_netstat.syn_resends += (u32)nsyn; }
    return p;
}
static int pc_read(pconn* p, u8* b, int n) { if (p->secure) { int k = tls_sess_read(&p->io, &p->ss, b, n); return k < 0 ? 0 : k; } return pc_rd(p, b, n); }
