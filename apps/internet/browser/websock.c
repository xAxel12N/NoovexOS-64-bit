#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/lib/crypto/nvxtls.h"
#include "include/nvx/lib/crypto/nvxwpa.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/net/http/cookies.h"
#include "include/nvx/net/stack/tcpip.h"
#include "include/nvx/sys/linux/lxsock.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/apps/nwe_api.h"

#define LXS_BLOCK (-1000)
#define WS_N 8
#define WS_RXMAX (8u * 1024u * 1024u)
#define WS_QMAX 256

extern volatile u64 g_ticks;
extern void (*g_wait_pump)(void);
extern const char* WEB_UA;

typedef struct wsmsg { struct wsmsg* next; int op; int len; u8 data[]; } wsmsg;
typedef struct {
    int used, state, sock, tls, closing_code;
    tls_io* io;
    tls_sess ss;
    u8* rx; u32 rxn, rxcap;
    u8* frag; u32 fragn, fragcap; int fragop;
    wsmsg *qh, *qt; int qn;
    char proto[64];
    char reason[96];
} wsconn;

static wsconn g_ws[WS_N];

static int w_len(const char* s) { int n = 0; while (s && s[n]) n++; return n; }
static char* w_cpy(char* d, const char* s) { while (s && *s) *d++ = *s++; *d = 0; return d; }
static int w_ieqn(const char* a, const char* b, int n) { for (int i = 0; i < n; i++) { char x = a[i], y = b[i]; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) return 0; if (!x) return 1; } return 1; }

static void w_wait_tick(void) { u64 t0 = g_ticks; while (g_ticks == t0) { if (g_wait_pump) g_wait_pump(); cpu_idle(); } }

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int b64enc(const u8* in, int n, char* out) {
    int o = 0;
    for (int i = 0; i < n; i += 3) {
        u32 v = (u32)in[i] << 16 | (i + 1 < n ? (u32)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = i + 1 < n ? B64[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? B64[v & 63] : '=';
    }
    out[o] = 0;
    return o;
}

typedef struct { int sock; int blocking; int dead; u64 deadline; } wsio;
static wsio g_wsio[WS_N];
static tls_io g_wstls[WS_N];

static int w_rd(void* c, u8* b, int n) {
    wsio* x = c;
    if (x->dead) return 0;
    for (;;) {
        int r = lxs_recv(x->sock, b, n, 0, 0, 0);
        if (r > 0) return r;
        if (r == 0 || (r != LXS_BLOCK)) { x->dead = 1; return 0; }
        if (!x->blocking) return 0;
        if (g_ticks > x->deadline) { x->dead = 1; return 0; }
        w_wait_tick();
    }
}
static int w_wr(void* c, const u8* b, int n) {
    wsio* x = c;
    int off = 0;
    u64 dl = g_ticks + 1000;
    while (off < n) {
        int r = lxs_send(x->sock, b + off, n - off, 0, 0, 0);
        if (r > 0) { off += r; continue; }
        if (r != LXS_BLOCK || g_ticks > dl) { x->dead = 1; return -1; }
        w_wait_tick();
    }
    return n;
}

static int w_send_raw(wsconn* w, int id, const u8* b, int n) {
    if (w->tls) return tls_sess_write(w->io, &w->ss, b, n) < 0 ? -1 : n;
    return w_wr(&g_wsio[id], b, n);
}
static int w_recv_raw(wsconn* w, int id, u8* b, int n) {
    if (w->tls) {
        int r = tls_sess_read(w->io, &w->ss, b, n);
        if (r < 0 || (r == 0 && g_wsio[id].dead)) return -1;
        return r;
    }
    int r = w_rd(&g_wsio[id], b, n);
    if (r == 0 && g_wsio[id].dead) return -1;
    return r;
}

static void q_push(wsconn* w, int op, const u8* d, u32 n) {
    if (w->qn >= WS_QMAX) return;
    wsmsg* m = kmalloc(sizeof(wsmsg) + n + 1);
    if (!m) return;
    m->next = 0;
    m->op = op;
    m->len = (int)n;
    for (u32 i = 0; i < n; i++) m->data[i] = d[i];
    m->data[n] = 0;
    if (w->qt) w->qt->next = m; else w->qh = m;
    w->qt = m;
    w->qn++;
}

static int w_frame(wsconn* w, int id, int op, const u8* d, u32 n) {
    u8 h[14];
    int hl = 0;
    h[hl++] = (u8)(0x80 | (op & 15));
    if (n < 126) h[hl++] = (u8)(0x80 | n);
    else if (n < 65536) { h[hl++] = 0x80 | 126; h[hl++] = (u8)(n >> 8); h[hl++] = (u8)n; }
    else { h[hl++] = 0x80 | 127; for (int i = 7; i >= 0; i--) h[hl++] = (u8)((u64)n >> (i * 8)); }
    u8 mk[4];
    entropy_get(mk, 4);
    for (int i = 0; i < 4; i++) h[hl++] = mk[i];
    u8* buf = kmalloc((unsigned long)hl + n);
    if (!buf) return -1;
    for (int i = 0; i < hl; i++) buf[i] = h[i];
    for (u32 i = 0; i < n; i++) buf[hl + i] = d[i] ^ mk[i & 3];
    int r = w_send_raw(w, id, buf, hl + (int)n);
    kfree(buf);
    return r < 0 ? -1 : 0;
}

static void w_free(wsconn* w) {
    while (w->qh) { wsmsg* m = w->qh; w->qh = m->next; kfree(m); }
    w->qt = 0;
    w->qn = 0;
    if (w->rx) kfree(w->rx);
    if (w->frag) kfree(w->frag);
    w->rx = w->frag = 0;
    w->rxn = w->rxcap = w->fragn = w->fragcap = 0;
    tls_sess_wipe(&w->ss);
}

static void w_dead(wsconn* w, int id, int code) {
    if (w->state == 3) return;
    w->state = 3;
    if (!w->closing_code) w->closing_code = code;
    if (w->sock >= 0) { lxs_release(w->sock); w->sock = -1; }
    g_wsio[id].dead = 1;
}

static int rx_grow(u8** b, u32* cap, u32 need) {
    if (need <= *cap) return 1;
    if (need > WS_RXMAX) return 0;
    u32 nc = *cap ? *cap : 8192;
    while (nc < need) nc *= 2;
    if (nc > WS_RXMAX) nc = WS_RXMAX;
    u8* nb = kmalloc(nc);
    if (!nb) return 0;
    for (u32 i = 0; i < *cap && *b; i++) nb[i] = (*b)[i];
    if (*b) kfree(*b);
    *b = nb;
    *cap = nc;
    return 1;
}

static void w_parse(wsconn* w, int id) {
    for (;;) {
        if (w->rxn < 2) return;
        u8* p = w->rx;
        int fin = p[0] >> 7, op = p[0] & 15, masked = p[1] >> 7;
        u64 len = p[1] & 127;
        u32 hl = 2;
        if (len == 126) { if (w->rxn < 4) return; len = (u64)p[2] << 8 | p[3]; hl = 4; }
        else if (len == 127) { if (w->rxn < 10) return; len = 0; for (int i = 0; i < 8; i++) len = len << 8 | p[2 + i]; hl = 10; }
        if (masked) hl += 4;
        if (len > WS_RXMAX) { w_dead(w, id, 1009); return; }
        if (w->rxn < hl + len) {
            if (!rx_grow(&w->rx, &w->rxcap, hl + (u32)len)) w_dead(w, id, 1009);
            return;
        }
        u8* d = p + hl;
        if (masked) { u8* mk = p + hl - 4; for (u64 i = 0; i < len; i++) d[i] ^= mk[i & 3]; }
        if (op == 8) {
            int code = len >= 2 ? (d[0] << 8 | d[1]) : 1005;
            if (w->state == 1) { u8 cb[2] = { (u8)(code >> 8), (u8)code }; w_frame(w, id, 8, cb, len >= 2 ? 2 : 0); }
            w->closing_code = code;
            int rl = 0;
            for (u64 i = 2; i < len && rl < 95; i++) w->reason[rl++] = (char)d[i];
            w->reason[rl] = 0;
            w_dead(w, id, code);
            return;
        } else if (op == 9) {
            w_frame(w, id, 10, d, (u32)len);
        } else if (op == 10) {
        } else if (op == 0 || op == 1 || op == 2) {
            if (op != 0) { w->fragop = op; w->fragn = 0; }
            if (!rx_grow(&w->frag, &w->fragcap, w->fragn + (u32)len + 1)) { w_dead(w, id, 1009); return; }
            for (u64 i = 0; i < len; i++) w->frag[w->fragn + i] = d[i];
            w->fragn += (u32)len;
            if (fin) { q_push(w, w->fragop ? w->fragop : 1, w->frag, w->fragn); w->fragn = 0; w->fragop = 0; }
        } else { w_dead(w, id, 1002); return; }
        u32 used = hl + (u32)len;
        for (u32 i = used; i < w->rxn; i++) w->rx[i - used] = w->rx[i];
        w->rxn -= used;
    }
}

static void w_pump(int id) {
    wsconn* w = &g_ws[id];
    if (!w->used || w->state != 1) return;
    g_wsio[id].blocking = 0;
    for (int k = 0; k < 64; k++) {
        if (!rx_grow(&w->rx, &w->rxcap, w->rxn + 16384)) { w_dead(w, id, 1009); return; }
        int r = w_recv_raw(w, id, w->rx + w->rxn, 16384);
        if (r < 0) { w_parse(w, id); w_dead(w, id, 1006); return; }
        if (r == 0) break;
        w->rxn += (u32)r;
    }
    w_parse(w, id);
}

static int url_split(const char* url, int* tls, char* host, int hcap, u16* port, char* path, int pcap) {
    const char* p = url;
    if (w_ieqn(p, "wss://", 6)) { *tls = 1; p += 6; *port = 443; }
    else if (w_ieqn(p, "ws://", 5)) { *tls = 0; p += 5; *port = 80; }
    else if (w_ieqn(p, "https://", 8)) { *tls = 1; p += 8; *port = 443; }
    else if (w_ieqn(p, "http://", 7)) { *tls = 0; p += 7; *port = 80; }
    else return 0;
    int h = 0;
    while (*p && *p != '/' && *p != ':' && *p != '?' && *p != '#' && h < hcap - 1) host[h++] = *p++;
    host[h] = 0;
    if (!h) return 0;
    if (*p == ':') { p++; u32 v = 0; while (*p >= '0' && *p <= '9') { v = v * 10 + (u32)(*p - '0'); if (v > 65535) return 0; p++; } if (!v) return 0; *port = (u16)v; }
    int o = 0;
    if (*p != '/') path[o++] = '/';
    while (*p && *p != '#' && o < pcap - 1) path[o++] = *p++;
    path[o] = 0;
    return 1;
}

int nwk_ws_open(const char* url, const char* protocols, const char* origin) {
    int id = -1;
    for (int i = 0; i < WS_N; i++) if (!g_ws[i].used) { id = i; break; }
    if (id < 0) return -1;
    wsconn* w = &g_ws[id];
    for (unsigned k = 0; k < sizeof(*w); k++) ((u8*)w)[k] = 0;
    w->sock = -1;
    char host[256], path[1024];
    u16 port;
    int tls;
    if (!url_split(url, &tls, host, sizeof host, &port, path, sizeof path)) return -1;
    u32 ip;
    if (!parse_ip(host, &ip)) { if (dns_query(host, &ip) <= 0) { klog("websocket: DNS lookup failed"); return -1; } }
    int s = lxs_new(1);
    if (s < 0) return -1;
    w->used = 1;
    w->sock = s;
    w->tls = tls;
    wsio* x = &g_wsio[id];
    x->sock = s;
    x->dead = 0;
    x->blocking = 1;
    x->deadline = g_ticks + 1500;
    int r = lxs_connect(s, ip, port, 0);
    while (r == LXS_BLOCK && g_ticks < x->deadline) { w_wait_tick(); r = lxs_connect(s, ip, port, 0); }
    if (r != 0) { klog("websocket: connect failed"); w_dead(w, id, 1006); w_free(w); w->used = 0; return -1; }
    if (tls) {
        w->io = &g_wstls[id];
        w->io->wr = w_wr;
        w->io->rd = w_rd;
        w->io->ctx = x;
        w->io->rlen = 0;
        w->io->rpos = 0;
        u8 ent[64];
        entropy_get(ent, 64);
        int hr = tls_connect_sess(w->io, host, w_len(host), ent, &w->ss);
        secure_zero(ent, 64);
        if (hr != 0 || !w->ss.ok) { klog("websocket: TLS handshake failed"); w_dead(w, id, 1015); w_free(w); w->used = 0; return -1; }
    }
    u8 key[16];
    entropy_get(key, 16);
    char k64[32];
    b64enc(key, 16, k64);
    char* req = kmalloc(6144 + 16384);
    if (!req) { w_dead(w, id, 1006); w_free(w); w->used = 0; return -1; }
    char* q = req;
    q = w_cpy(q, "GET ");
    q = w_cpy(q, path);
    q = w_cpy(q, " HTTP/1.1\r\nHost: ");
    q = w_cpy(q, host);
    if ((tls && port != 443) || (!tls && port != 80)) { char pb[8]; int pn = 0; u32 v = port; char t[8]; int tn = 0; while (v) { t[tn++] = (char)('0' + v % 10); v /= 10; } *q++ = ':'; while (tn) pb[pn++] = t[--tn]; pb[pn] = 0; q = w_cpy(q, pb); }
    q = w_cpy(q, "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: ");
    q = w_cpy(q, k64);
    q = w_cpy(q, "\r\nUser-Agent: ");
    q = w_cpy(q, WEB_UA);
    if (origin && origin[0] && w_len(origin) < 512) { q = w_cpy(q, "\r\nOrigin: "); q = w_cpy(q, origin); }
    if (protocols && protocols[0] && w_len(protocols) < 512) { q = w_cpy(q, "\r\nSec-WebSocket-Protocol: "); q = w_cpy(q, protocols); }
    {
        char hu[1400];
        char* h = w_cpy(hu, tls ? "https://" : "http://");
        h = w_cpy(h, host);
        int pl = w_len(path);
        if (pl < 1000) w_cpy(h, path);
        static char ck[16384];
        int cn = cookie_header(hu, 0, ck, sizeof ck);
        if (cn > 0 && cn < (int)sizeof ck) { ck[cn] = 0; q = w_cpy(q, "\r\nCookie: "); q = w_cpy(q, ck); }
    }
    q = w_cpy(q, "\r\n\r\n");
    int ok = w_send_raw(w, id, (const u8*)req, (int)(q - req)) > 0;
    kfree(req);
    if (!ok) { w_dead(w, id, 1006); w_free(w); w->used = 0; return -1; }
    if (!rx_grow(&w->rx, &w->rxcap, 16384)) { w_dead(w, id, 1006); w_free(w); w->used = 0; return -1; }
    int he = -1;
    x->blocking = 1;
    x->deadline = g_ticks + 1500;
    while (he < 0) {
        if (w->rxn + 4096 > w->rxcap && !rx_grow(&w->rx, &w->rxcap, w->rxn + 4096)) break;
        int n = w_recv_raw(w, id, w->rx + w->rxn, 4096);
        if (n < 0) break;
        if (n == 0) { if (g_ticks > x->deadline) break; w_wait_tick(); continue; }
        w->rxn += (u32)n;
        for (u32 i = 0; i + 3 < w->rxn; i++) if (w->rx[i] == '\r' && w->rx[i + 1] == '\n' && w->rx[i + 2] == '\r' && w->rx[i + 3] == '\n') { he = (int)i + 4; break; }
        if (w->rxn > 65536) break;
    }
    if (he < 0) { klog("websocket: no handshake reply"); w_dead(w, id, 1006); w_free(w); w->used = 0; return -1; }
    char* hd = (char*)w->rx;
    int good = he > 12 && w_ieqn(hd, "HTTP/1.1 101", 12);
    char want[64];
    {
        char cat[80];
        char* c = w_cpy(cat, k64);
        w_cpy(c, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
        u8 dg[20];
        sha1_hash((const u8*)cat, (u32)w_len(cat), dg);
        b64enc(dg, 20, want);
    }
    int acc = 0;
    for (int i = 0; i < he; i++) {
        if (i == 0 || hd[i - 1] == '\n') {
            if (w_ieqn(hd + i, "sec-websocket-accept:", 21)) {
                int j = i + 21;
                while (hd[j] == ' ') j++;
                int wl = w_len(want);
                acc = 1;
                for (int t = 0; t < wl; t++) if (hd[j + t] != want[t]) acc = 0;
            } else if (w_ieqn(hd + i, "sec-websocket-protocol:", 23)) {
                int j = i + 23, o = 0;
                while (hd[j] == ' ') j++;
                while (j < he && hd[j] != '\r' && o < 63) w->proto[o++] = hd[j++];
                w->proto[o] = 0;
            } else if (w_ieqn(hd + i, "set-cookie:", 11)) {
                int j = i + 11, o = 0;
                char line[1024];
                while (hd[j] == ' ') j++;
                while (j < he && hd[j] != '\r' && o < 1023) line[o++] = hd[j++];
                line[o] = 0;
                char hu[300];
                char* h = w_cpy(hu, tls ? "https://" : "http://");
                w_cpy(h, host);
                cookie_set(hu, line, 0);
            }
        }
    }
    if (!good || !acc) { klog(good ? "websocket: bad Sec-WebSocket-Accept" : "websocket: server did not switch protocols"); w_dead(w, id, 1006); w_free(w); w->used = 0; return -1; }
    for (u32 i = (u32)he; i < w->rxn; i++) w->rx[i - he] = w->rx[i];
    w->rxn -= (u32)he;
    w->state = 1;
    x->blocking = 0;
    w_parse(w, id);
    klog("websocket: connected");
    return id;
}

int nwk_ws_send(int id, const unsigned char* data, int len, int binary) {
    if (id < 0 || id >= WS_N || !g_ws[id].used || g_ws[id].state != 1 || len < 0) return -1;
    wsconn* w = &g_ws[id];
    if (w_frame(w, id, binary ? 2 : 1, data, (u32)len) < 0) { w_dead(w, id, 1006); return -1; }
    return len;
}

int nwk_ws_recv(int id, unsigned char* out, int cap, int* binary) {
    if (id < 0 || id >= WS_N || !g_ws[id].used) return -3;
    wsconn* w = &g_ws[id];
    w_pump(id);
    if (!w->qh) return w->state == 3 ? -2 : -1;
    wsmsg* m = w->qh;
    if (m->len > cap) return -4 - m->len;
    for (int i = 0; i < m->len; i++) out[i] = m->data[i];
    if (binary) *binary = m->op == 2;
    int n = m->len;
    w->qh = m->next;
    if (!w->qh) w->qt = 0;
    w->qn--;
    kfree(m);
    return n;
}

int nwk_ws_info(int id, int* code, char* proto, int pcap, char* reason, int rcap) {
    if (id < 0 || id >= WS_N || !g_ws[id].used) return -1;
    wsconn* w = &g_ws[id];
    if (code) *code = w->closing_code;
    if (proto && pcap > 0) { int i = 0; for (; w->proto[i] && i < pcap - 1; i++) proto[i] = w->proto[i]; proto[i] = 0; }
    if (reason && rcap > 0) { int i = 0; for (; w->reason[i] && i < rcap - 1; i++) reason[i] = w->reason[i]; reason[i] = 0; }
    return w->state;
}

void nwk_ws_close(int id, int code, int forget) {
    if (id < 0 || id >= WS_N || !g_ws[id].used) return;
    wsconn* w = &g_ws[id];
    if (w->state == 1) {
        u8 cb[2] = { (u8)(code >> 8), (u8)code };
        w_frame(w, id, 8, cb, code ? 2 : 0);
        w->state = 2;
        u64 dl = g_ticks + 100;
        g_wsio[id].blocking = 0;
        while (g_ticks < dl && w->state == 2) {
            if (!rx_grow(&w->rx, &w->rxcap, w->rxn + 4096)) break;
            int r = w_recv_raw(w, id, w->rx + w->rxn, 4096);
            if (r < 0) break;
            if (r > 0) { w->rxn += (u32)r; w_parse(w, id); continue; }
            w_wait_tick();
        }
        w->closing_code = code ? code : 1005;
        w_dead(w, id, w->closing_code);
    }
    if (forget) { w_dead(w, id, code); w_free(w); w->used = 0; }
}

void nwk_ws_close_all(void) {
    for (int i = 0; i < WS_N; i++) if (g_ws[i].used) nwk_ws_close(i, 1001, 1);
}
