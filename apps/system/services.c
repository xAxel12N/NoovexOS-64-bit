#include "include/nvx/base.h"
#include "include/drivers.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/net/stack/tcpip.h"
#include "include/nvx/sys/linux/lxsock.h"
#include "include/nvx/net/http/cookies.h"

extern volatile u64 g_ticks;
extern u32 my_ip;
extern void klog(const char*);
extern void toast(const char*);
extern u64 k_realtime_ms(void);
extern void k_realtime_set(u64 ms);
extern void write_clock(int hh, int mm, int ss, int yr, int mo, int dy);
extern void nwk_net_step(void);
extern void nwk_webcache_tick(int force);
extern int nwk_net_active(long owner);
extern int dl_active(void);
extern void dl_tick(void);
extern int g_acb_pct, g_acb_n, g_acb_charging, g_lid_open;
extern u32 dns_ip;
unsigned long long nwk_us(void);

typedef struct { const char* id; const char* name; const char* desc; int required, on; u32 every; u64 last, runs, us; void (*tick)(void); char st[72]; } svc_t;

static void s_net(void);
static void s_cache(void);
static void s_cookies(void);
static void s_power(void);
static void s_clock(void);
static void s_dl(void);
static void s_wifi(void);

static svc_t S[] = {
    { "net", "Web requests", "Runs page requests in the background so pages never wait", 1, 1, 1, 0, 0, 0, s_net, "" },
    { "downloads", "Downloads", "Keeps downloads going while you browse", 1, 1, 2, 0, 0, 0, s_dl, "" },
    { "webcache", "Web cache writer", "Saves the web cache to the disk in small steps", 0, 1, 10, 0, 0, 0, s_cache, "" },
    { "cookies", "Cookie and site data saving", "Writes sign-ins and site storage to the disk", 0, 1, 100, 0, 0, 0, s_cookies, "" },
    { "clock", "Clock sync", "Sets the clock from internet time servers every 6 hours", 0, 1, 100, 0, 0, 0, s_clock, "" },
    { "power", "Battery, lid and power button", "Low-battery warnings, lid and power button", 1, 1, 50, 0, 0, 0, s_power, "" },
    { "wifi", "Wi-Fi status", "Watches the Wi-Fi connection", 0, 1, 200, 0, 0, 0, s_wifi, "" },
};
#define SVC_N ((int)(sizeof(S) / sizeof(S[0])))

static void st_set(svc_t* s, const char* a, const char* b) { char* p = s->st; p = scopy(p, a); if (b) p = scopy(p, b); *p = 0; }
static svc_t* svc_find(const char* id) { for (int i = 0; i < SVC_N; i++) { const char* a = S[i].id; const char* b = id; while (*a && *a == *b) { a++; b++; } if (!*a && !*b) return &S[i]; } return 0; }

static void s_net(void) { nwk_net_step(); int n = nwk_net_active(0); char b[16], *p = b; p = udec(p, (u32)n); *p = 0; st_set(&S[0], b, n == 1 ? " request open" : " requests open"); }
static void s_cache(void) { nwk_webcache_tick(0); st_set(&S[2], "Idle", 0); }
static void s_cookies(void) { cookies_flush(0); st_set(&S[3], "Saved when changed", 0); }
static void s_power(void) {
    if (g_acb_n && g_acb_pct >= 0) { char b[48], *p = b; p = scopy(p, "Battery "); p = udec(p, (u32)g_acb_pct); p = scopy(p, g_acb_charging ? "%, charging" : "%"); *p = 0; st_set(&S[5], b, g_lid_open ? ", lid open" : ""); }
    else st_set(&S[5], "No battery; power button watched", 0);
}
static void s_dl(void) { dl_tick(); int n = dl_active(); if (n) { char b[16], *p = b; p = udec(p, (u32)n); *p = 0; st_set(&S[1], b, " active"); } else st_set(&S[1], "None active", 0); }
static void s_wifi(void) {
    extern char g_wifi_conn[33]; extern int nic_up(void);
    if (g_wifi_conn[0]) st_set(&S[6], "Connected to ", g_wifi_conn);
    else st_set(&S[6], nic_up() ? "Not used (wired network)" : "Not connected", 0);
}

static int ck_state, ck_srv, ck_sock = -1;
static u64 ck_next = 1500, ck_t0, ck_ok_at;
static long long ck_last_off;
static const char* NTP_SRV[4] = { "pool.ntp.org", "time.google.com", "time.cloudflare.com", 0 };
static char ck_custom[64];
extern int parse_ip(const char* s, u32* out);
static void ck_close(void) { if (ck_sock >= 0) lxs_release(ck_sock); ck_sock = -1; }
static void ck_civil(long long days, int* y, int* m, int* d) {
    long long z = days + 719468; long long era = (z >= 0 ? z : z - 146096) / 146097; unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; long long yy = (long long)yoe + era * 400; unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153; unsigned dd = doy - (153 * mp + 2) / 5 + 1; unsigned mm = mp < 10 ? mp + 3 : mp - 9; *y = (int)(yy + (mm <= 2)); *m = (int)mm; *d = (int)dd;
}
void clock_sync_now(void) { if (ck_state == 0) ck_next = g_ticks; }
void clock_sync_server(const char* h) { int i = 0; for (; h && h[i] && h[i] != ' ' && i < 63; i++) ck_custom[i] = h[i]; ck_custom[i] = 0; NTP_SRV[3] = ck_custom[0] ? ck_custom : 0; ck_srv = NTP_SRV[3] ? 3 : 0; clock_sync_now(); }
static int ck_dsock = -1;
static u16 ck_dtx;
static u64 ck_dt0;
static u32 ck_ip;
static void ck_dclose(void) { if (ck_dsock >= 0) lxs_release(ck_dsock); ck_dsock = -1; }
static int ck_dns_send(const char* host) {
    u8 q[300]; int qp = 12;
    ck_dtx = (u16)(g_ticks * 2654435761u >> 7); if (!ck_dtx) ck_dtx = 0x5a5a;
    for (int i = 0; i < 12; i++) q[i] = 0;
    q[0] = (u8)(ck_dtx >> 8); q[1] = (u8)ck_dtx; q[2] = 1; q[5] = 1;
    const char* p = host;
    while (*p) { const char* st = p; int l = 0; while (*p && *p != '.') { p++; l++; } if (l > 63 || qp + l + 6 > 300) return -1; q[qp++] = (u8)l; for (int i = 0; i < l; i++) q[qp++] = (u8)st[i]; if (*p == '.') p++; }
    q[qp++] = 0; q[qp++] = 0; q[qp++] = 1; q[qp++] = 0; q[qp++] = 1;
    ck_dclose();
    ck_dsock = lxs_new(2);
    if (ck_dsock < 0 || lxs_connect(ck_dsock, dns_ip, 53, 1) != 0) { ck_dclose(); return -1; }
    if (lxs_send(ck_dsock, q, qp, 0, 0, 0) != qp) { ck_dclose(); return -1; }
    ck_dt0 = g_ticks;
    return 0;
}
static int ck_dns_poll(u32* ip) {
    u8 r[512];
    int n = lxs_recv(ck_dsock, r, sizeof r, 0, 0, 0);
    if (n < 12) return g_ticks - ck_dt0 > 300 ? -1 : 0;
    if (((r[0] << 8) | r[1]) != ck_dtx || !(r[2] & 0x80)) return 0;
    int an = (r[6] << 8) | r[7], off = 12;
    while (off < n && r[off]) { if ((r[off] & 0xC0) == 0xC0) { off += 1; break; } off += r[off] + 1; }
    off += 5;
    for (int a = 0; a < an && off + 10 <= n; a++) {
        while (off < n && r[off]) { if ((r[off] & 0xC0) == 0xC0) { off += 1; break; } off += r[off] + 1; }
        off += 1;
        if (off + 10 > n) break;
        int type = (r[off] << 8) | r[off + 1], rdl = (r[off + 8] << 8) | r[off + 9];
        off += 10;
        if (off + rdl > n) break;
        if (type == 1 && rdl == 4) { *ip = ((u32)r[off] << 24) | ((u32)r[off + 1] << 16) | ((u32)r[off + 2] << 8) | r[off + 3]; return 1; }
        off += rdl;
    }
    return -1;
}
static void ck_ntp_send(svc_t* s, u32 ip) {
    (void)s;
    ck_close();
    ck_sock = lxs_new(2);
    if (ck_sock < 0 || lxs_connect(ck_sock, ip, 123, 1) != 0) { ck_close(); ck_state = 0; S[4].every = 100; ck_next = g_ticks + 3000; return; }
    u8 pk[48]; for (int i = 0; i < 48; i++) pk[i] = 0; pk[0] = 0x23;
    u64 now = k_realtime_ms(); u64 ns = now / 1000ull + 2208988800ull; pk[40] = (u8)(ns >> 24); pk[41] = (u8)(ns >> 16); pk[42] = (u8)(ns >> 8); pk[43] = (u8)ns;
    if (lxs_send(ck_sock, pk, 48, 0, 0, 0) != 48) { ck_close(); ck_state = 0; S[4].every = 100; ck_next = g_ticks + 3000; return; }
    ck_t0 = g_ticks; ck_state = 1; S[4].every = 1;
}
static void s_clock(void) {
    svc_t* s = svc_find("clock");
    if (ck_state == 0) {
        if (g_ticks < ck_next) return;
        if (!my_ip) { st_set(s, "Waiting for the network", 0); ck_next = g_ticks + 1000; return; }
        if (!NTP_SRV[ck_srv]) ck_srv = 0;
        ck_ip = 0;
        if (parse_ip(NTP_SRV[ck_srv], &ck_ip)) { ck_ntp_send(s, ck_ip); return; }
        if (!dns_ip || ck_dns_send(NTP_SRV[ck_srv]) != 0) { ck_srv = (ck_srv + 1) % 3; ck_next = g_ticks + 3000; st_set(s, "Time server not found, retrying", 0); return; }
        ck_state = 2; S[4].every = 1;
        return;
    }
    if (ck_state == 2) {
        int r = ck_dns_poll(&ck_ip);
        if (r == 0) return;
        ck_dclose();
        if (r < 0) { ck_state = 0; S[4].every = 100; ck_srv = (ck_srv + 1) % 3; ck_next = g_ticks + 3000; st_set(s, "Time server not found, retrying", 0); return; }
        ck_ntp_send(s, ck_ip);
        return;
    }
    u8 rb[96];
    int k = lxs_recv(ck_sock, rb, sizeof rb, 0, 0, 0);
    if (k < 48) {
        if (g_ticks - ck_t0 > 300) { ck_close(); ck_state = 0; S[4].every = 100; ck_srv = (ck_srv + 1) % 3; ck_next = g_ticks + 1000; st_set(s, "No answer from the time server, retrying", 0); }
        return;
    }
    ck_close(); ck_state = 0; S[4].every = 100;
    int mode = rb[0] & 7, stratum = rb[1];
    u64 secs = ((u64)rb[40] << 24) | ((u64)rb[41] << 16) | ((u64)rb[42] << 8) | rb[43];
    u64 frac = ((u64)rb[44] << 24) | ((u64)rb[45] << 16) | ((u64)rb[46] << 8) | rb[47];
    if (mode != 4 || stratum < 1 || stratum > 15 || secs < 2208988800ull + 1700000000ull) { ck_srv = (ck_srv + 1) % 3; ck_next = g_ticks + 3000; st_set(s, "Bad answer from the time server", 0); return; }
    u64 rtt = (g_ticks - ck_t0) * 10ull;
    u64 ms = (secs - 2208988800ull) * 1000ull + (frac * 1000ull >> 32) + rtt / 2;
    long long off = (long long)ms - (long long)k_realtime_ms();
    ck_last_off = off;
    if (off > 1500 || off < -1500) {
        k_realtime_set(ms);
        u64 sec = ms / 1000ull; int y, m, d; ck_civil((long long)(sec / 86400ull), &y, &m, &d); u64 r = sec % 86400ull;
        write_clock((int)(r / 3600), (int)(r / 60 % 60), (int)(r % 60), y, m, d);
        char b[96], *p = b; p = scopy(p, "clock: set from "); p = scopy(p, NTP_SRV[ck_srv]); p = scopy(p, ", was off by "); long long a = off < 0 ? -off : off; p = udec(p, (u32)(a / 1000)); p = scopy(p, " s"); *p = 0; klog(b);
    }
    ck_ok_at = g_ticks;
    { char b[72], *p = b; p = scopy(p, "In sync with "); p = scopy(p, NTP_SRV[ck_srv]); *p = 0; st_set(s, b, 0); }
    ck_next = g_ticks + 6ull * 3600ull * 100ull;
}

static int g_svc_inited;
static void svc_init(void) {
    if (g_svc_inited) return;
    g_svc_inited = 1;
    for (int i = 0; i < SVC_N; i++) st_set(&S[i], "Starting", 0);
}
extern u32 g_svc_off;
u32 g_svc_off;
void svc_apply_saved(void) { for (int i = 0; i < SVC_N; i++) if (!S[i].required) S[i].on = !(g_svc_off & (1u << i)); }
void svc_tick(void) {
    svc_init();
    static int applied;
    if (!applied) { applied = 1; svc_apply_saved(); }
    for (int i = 0; i < SVC_N; i++) {
        svc_t* s = &S[i];
        if (!s->on) continue;
        if (g_ticks - s->last < s->every) continue;
        s->last = g_ticks;
        unsigned long long t = nwk_us();
        s->tick();
        s->us += nwk_us() - t; s->runs++;
    }
}
int svc_count(void) { return SVC_N; }
u64 svc_us(int i) { return (i >= 0 && i < SVC_N) ? S[i].us : 0; }
u32 svc_every(int i) { return (i >= 0 && i < SVC_N) ? S[i].every : 0; }
u64 svc_runs(int i) { return (i >= 0 && i < SVC_N) ? S[i].runs : 0; }
int svc_info(int i, const char** name, const char** desc, const char** status, int* on, int* required) {
    if (i < 0 || i >= SVC_N) return 0;
    *name = S[i].name; *desc = S[i].desc; *status = S[i].on ? S[i].st : "Stopped"; *on = S[i].on; *required = S[i].required;
    return 1;
}
int svc_set(int i, int on) {
    if (i < 0 || i >= SVC_N || S[i].required) return 0;
    S[i].on = on ? 1 : 0;
    if (on) g_svc_off &= ~(1u << i); else g_svc_off |= 1u << i;
    if (!on && i == 4) { ck_close(); ck_dclose(); ck_state = 0; S[4].every = 100; }
    extern void power_prefs_save(void); power_prefs_save();
    return 1;
}
int svc_index(const char* id) { for (int i = 0; i < SVC_N; i++) { const char* a = S[i].id; const char* b = id; while (*a && *a == *b) { a++; b++; } if (!*a && (!*b || *b == ' ')) return i; } return -1; }
const char* svc_id(int i) { return (i >= 0 && i < SVC_N) ? S[i].id : ""; }
void svc_list(char* out, int cap) {
    char* p = out; char* e = out + cap - 200;
    for (int i = 0; i < SVC_N && p < e; i++) {
        svc_t* s = &S[i];
        p = scopy(p, s->on ? "  running  " : "  stopped  "); p = scopy(p, s->id);
        int l = 0; while (s->id[l]) l++; while (l++ < 11) *p++ = ' ';
        p = scopy(p, s->name); p = scopy(p, s->required ? " (always on)" : ""); p = scopy(p, "\n             "); p = scopy(p, s->on ? s->st : "Stopped"); p = scopy(p, "\n");
    }
    *p = 0;
}
