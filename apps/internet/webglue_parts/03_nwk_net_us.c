unsigned long long nwk_net_us(void) { return g_netstat.fetch_us; }
int nwk_fetch(const char* url, const char* method, const char* ctype, const char* body, int blen, const char* referer, nwk_resp* r) {
    unsigned long long t0 = nus();
    g_pc_abort = 0;
    int rc = nwk_fetch_t(url, method, ctype, body, blen, referer, r);
    g_netstat.fetch_us += nus() - t0;
    if (!g_nwk_offline && (rc < 0 || (r && r->status >= 400))) {
        char b[1200], *q = b; int k = 0;
        if (rc < 0) { q = put(q, rc == -100 ? "Name lookup failed: " : rc == -2 ? "Could not connect: " : rc == -5 ? "Secure connection failed: " : rc == -4 ? "Download cut off (retried once): " : rc == -3 ? "No answer from server: " : "Request failed: "); }
        else { q = put(q, "HTTP "); q = putu(q, (unsigned)r->status); q = put(q, ": "); }
        q = put(q, method && method[0] ? method : "GET"); q = put(q, " ");
        while (url[k] && k < 1100) *q++ = url[k++];
        if (method && (method[0] == 'P' || method[0] == 'D')) { q = put(q, " (body "); q = putu(q, (unsigned)(body && blen > 0 ? blen : 0)); q = put(q, " bytes)"); }
        *q = 0; nwk_error(b);
    }
    return rc;
}
static char g_doc_csp[4096], g_doc_cspro[4096], g_doc_xfo[64];
int g_doc_hdr;
int nwk_doc_policy(char* csp, int cap, char* ro, int rcap) {
    int i = 0; for (; g_doc_csp[i] && i < cap - 1; i++) csp[i] = g_doc_csp[i]; csp[i] = 0;
    i = 0; for (; g_doc_cspro[i] && i < rcap - 1; i++) ro[i] = g_doc_cspro[i]; ro[i] = 0;
    g_doc_csp[0] = 0; g_doc_cspro[0] = 0;
    return csp[0] || ro[0];
}
static int nwk_fetch_t(const char* url, const char* method, const char* ctype, const char* body, int blen, const char* referer, nwk_resp* r) {
    if (g_nwk_kind == 1 || g_doc_hdr) { g_doc_csp[0] = 0; g_doc_cspro[0] = 0; g_doc_xfo[0] = 0; }
    unsigned long t = nwk_ms();
    int get = (!method || !method[0] || ieqn(method, "GET", 4)) && !(body && blen > 0);
    int mode = g_http_mode;
    if (mode == 1) g_http_mode = 0;
    if (g_nwk_offline) { if (get && hc_lookup(url, r)) { g_netstat.cache_hits++; return 0; } return -1; }
    if (get && mode != 2) { int sv = g_http_mode; g_http_mode = mode; int hit = hc_lookup(url, r); g_http_mode = sv; if (hit) { g_netstat.cache_hits++; g_fetch_ms += nwk_ms() - t; g_fetch_n++; return 0; } }
    g_last_maxage = -1; g_last_nostore = 0; g_last_setcookie = 0; g_last_etag[0] = 0; g_last_lm[0] = 0;
    if (!get || mode == 2) g_reval_i = -1;
    int ri = g_reval_i;
    int rc = (get && pf_take(url, r)) ? 0 : nwk_fetch_real(url, method, ctype, body, blen, referer, r);
    g_reval_i = -1;
    if (rc == 0 && r->status == 304 && ri >= 0 && g_hc[ri].url) {
        hcent* e = &g_hc[ri];
        if (r->body) kfree(r->body);
        r->body = kmalloc((unsigned long)e->len + 1);
        if (!r->body) return -1;
        for (int k = 0; k < e->len; k++) r->body[k] = e->body[k];
        r->body[e->len] = 0; r->len = e->len; r->status = e->status;
        for (int k = 0; k < 96; k++) r->ctype[k] = e->ctype[k];
        e->t = ep_s(); if (g_last_maxage > 0) e->maxage = g_last_maxage > 86400 ? 86400 : (int)g_last_maxage; e->use = ++g_hc_tick;
        g_netstat.cache_hits++;
        g_fetch_ms += nwk_ms() - t; g_fetch_n++;
        return 0;
    }
    if (get && rc == 0 && r->body && r->status == 200 && !g_last_nostore && !g_last_setcookie) {
        long ma = g_last_maxage;
        if (ma < 0 && hc_static(r->final_url[0] ? r->final_url : url, r->ctype)) ma = 600;
        if (ma > 86400 * 7) ma = 86400 * 7;
        if (ma > 0 || !hc_static(url, r->ctype)) hc_store(url, r, ma > 0 ? ma : 0);
    }
    g_fetch_ms += nwk_ms() - t; g_fetch_n++;
    return rc;
}
const char* g_nwk_xhdr;
int g_nwk_kind;
static int xh_forbidden(const char* k, int n) {
    static const char* bad[] = { "host", "connection", "content-length", "content-type", "cookie", "cookie2", "accept-encoding", "accept-charset", "referer", "origin", "te", "trailer", "transfer-encoding", "upgrade", "keep-alive", "date", "expect", "via", "dnt", "user-agent", "access-control-request-headers", "access-control-request-method", "permissions-policy", 0 };
    if (n >= 4 && ieqn(k, "sec-", 4)) return 1;
    if (n >= 6 && ieqn(k, "proxy-", 6)) return 1;
    for (int i = 0; bad[i]; i++) { int l = sl(bad[i]); if (l == n && ieqn(k, bad[i], n)) return 1; }
    return 0;
}
static int xh_has(const char* name) {
    const char* h = g_nwk_xhdr; if (!h) return 0; int nl = sl(name);
    for (const char* p = h; *p;) { const char* c = p; while (*c && *c != ':' && *c != '\n') c++; if ((int)(c - p) == nl && ieqn(p, name, nl)) return 1; while (*p && *p != '\n') p++; if (*p) p++; }
    return 0;
}
static int org_of(const char* url, char* out, int cap) {
    purl t; if (!url || parse_url(url, &t) < 0) return 0;
    char* p = out; char* e = out + cap - 16;
    p = put(p, t.secure ? "https://" : "http://"); for (int i = 0; t.host[i] && p < e; i++) *p++ = t.host[i];
    if ((t.secure && t.port != 443) || (!t.secure && t.port != 80)) { *p++ = ':'; p = putu(p, (unsigned)t.port); }
    *p = 0; return 1;
}
static const char* reg_dom(const char* h) { int n = sl(h), dots = 0; for (int i = n - 1; i >= 0; i--) if (h[i] == '.') { dots++; if (dots == 2) return h + i + 1; } return h; }
static const char* fetch_site(purl* u, const char* referer) {
    purl r; if (!referer || !referer[0] || !ieqn(referer, "http", 4) || parse_url(referer, &r) < 0) return "none";
    if (r.secure == u->secure && r.port == u->port && hc_seq(r.host, u->host)) return "same-origin";
    if (hc_seq(reg_dom(r.host), reg_dom(u->host))) return "same-site";
    return "cross-site";
}
static char* req_head(char* p, purl* u, const char* cur, const char* meth, const char* referer) {
    p = put(p, meth); *p++ = ' '; p = put(p, u->path); p = put(p, " HTTP/1.1\r\nHost: "); p = put(p, u->host);
    if ((u->secure && u->port != 443) || (!u->secure && u->port != 80)) { *p++ = ':'; p = putu(p, (unsigned)u->port); }
    p = put(p, "\r\nUser-Agent: "); p = put(p, WEB_UA);
    if (g_nwk_kind == 2) { p = put(p, "\r\n"); if (!xh_has("accept")) p = put(p, "Accept: */*\r\n"); if (!xh_has("accept-language")) p = put(p, "Accept-Language: en-US,en;q=0.9\r\n"); p = put(p, "Accept-Encoding: gzip\r\nConnection: keep-alive\r\n"); }
    else p = put(p, "\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/jxl,image/webp,image/png,image/jpeg,*/*;q=0.8\r\nAccept-Language: en-US,en;q=0.9\r\nAccept-Encoding: gzip\r\nConnection: keep-alive\r\n");
    if (g_nwk_kind == 1 || g_nwk_kind == 2) {
        const char* site = fetch_site(u, referer);
        if (g_nwk_kind == 1) { p = put(p, "Sec-Fetch-Site: "); p = put(p, site); p = put(p, "\r\nSec-Fetch-Mode: navigate\r\nSec-Fetch-Dest: document\r\nSec-Fetch-User: ?1\r\nUpgrade-Insecure-Requests: 1\r\n"); }
        else { p = put(p, "Sec-Fetch-Site: "); p = put(p, site[0] == 'n' ? "same-origin" : site); p = put(p, "\r\nSec-Fetch-Mode: cors\r\nSec-Fetch-Dest: empty\r\n"); }
        int post = !(ieqn(meth, "GET", 4) || ieqn(meth, "HEAD", 5));
        int cross = !hc_seq(site, "same-origin") && !hc_seq(site, "none");
        static char og[300];
        if ((post || (g_nwk_kind == 2 && cross)) && !hc_seq(site, "none") && org_of(referer, og, sizeof og)) { p = put(p, "Origin: "); p = put(p, og); p = put(p, "\r\n"); }
    }
    if (g_nwk_kind == 2 && g_nwk_xhdr) {
        const char* h = g_nwk_xhdr; int budget = 6000;
        for (const char* q = h; *q && budget > 0;) {
            const char* le = q; while (*le && *le != '\n') le++;
            const char* c = q; while (c < le && *c != ':') c++;
            int kn = (int)(c - q);
            if (c < le && kn > 0 && kn < 128 && !xh_forbidden(q, kn)) {
                int ok = 1; for (const char* t = q; t < c; t++) { char ch = *t; if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.')) { ok = 0; break; } }
                const char* v = c + 1; while (v < le && *v == ' ') v++;
                const char* ve = le; while (ve > v && (ve[-1] == '\r' || ve[-1] == ' ')) ve--;
                for (const char* t = v; t < ve; t++) if (*t == '\r' || *t == 0) { ok = 0; break; }
                if (ok && (ve - v) < 4000) { p = putn(p, q, kn); p = put(p, ": "); p = putn(p, v, (int)(ve - v)); p = put(p, "\r\n"); budget -= kn + (int)(ve - v) + 4; }
            }
            q = *le ? le + 1 : le;
        }
    }
    static char ck[16384];
    { const char* fs2 = fetch_site(u, referer); int safe = ieqn(meth, "GET", 4) || ieqn(meth, "HEAD", 5) || ieqn(meth, "OPTIONS", 8);
      ck[0] = 0; cookie_header2(cur, 0, ck, sizeof(ck), fs2[0] == 'c', g_nwk_kind == 1, safe); }
    if (ck[0]) { p = put(p, "Cookie: "); p = put(p, ck); p = put(p, "\r\n"); }
    if (referer && referer[0] && (ieqn(referer, "http", 4))) { p = put(p, "Referer: "); int rl = sl(referer); if (rl > 1500) rl = 1500; p = putn(p, referer, rl); p = put(p, "\r\n"); }
    return p;
}
static int nwk_fetch_real(const char* url, const char* method, const char* ctype, const char* body, int blen, const char* referer, nwk_resp* r) {
    for (unsigned i = 0; i < sizeof(*r); i++) ((u8*)r)[i] = 0;
    if (!g_rbuf) g_rbuf = kmalloc(RBUF_SZ);
    if (!g_dbuf) g_dbuf = kmalloc(DBUF_SZ);
    if (!g_rbuf || !g_dbuf) return -1;
    static char cur[4096];
    static char meth[16];
    int k = 0;
    for (; url[k] && k < 4095; k++) cur[k] = url[k];
    cur[k] = 0;
    for (k = 0; method && method[k] && k < 15; k++) meth[k] = method[k];
    meth[k] = 0;
    if (!meth[0]) { meth[0] = 'G'; meth[1] = 'E'; meth[2] = 'T'; meth[3] = 0; }
    const char* b = body;
    int bl = blen;
    for (int hop = 0; hop <= 40; hop++) {
        static purl u;
        if (parse_url(cur, &u) < 0) return -1;
        int reqcap = 65536 + bl;
        char* req = kmalloc((unsigned long)reqcap);
        if (!req) return -1;
        char* p = req_head(req, &u, cur, meth, referer);
        if (meth[0] == 'P' || meth[0] == 'D') {
            int hb = b && bl > 0;
            if (ctype && ctype[0]) { p = put(p, "Content-Type: "); p = put(p, ctype); p = put(p, "\r\n"); }
            p = put(p, "Content-Length: "); p = putu(p, (unsigned)(hb ? bl : 0)); p = put(p, "\r\n");
        }
        if (hop == 0 && g_reval_i >= 0 && g_hc[g_reval_i].url) { hcent* e = &g_hc[g_reval_i]; if (e->etag[0]) { p = put(p, "If-None-Match: "); p = put(p, e->etag); p = put(p, "\r\n"); } if (e->lm[0]) { p = put(p, "If-Modified-Since: "); p = put(p, e->lm); p = put(p, "\r\n"); } }
        p = put(p, "\r\n");
        if (b && bl > 0 && (meth[0] == 'P' || meth[0] == 'D')) p = putn(p, b, bl);
        int n;
        g_stream_url = cur;
        int rc = http_once(&u, req, (int)(p - req), &n, meth[0] == 'H');
        g_stream_url = 0;
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
                static char line[4096];
                int q = 0;
                while (j < he && g_rbuf[j] != '\r' && g_rbuf[j] != '\n' && q < 4095) line[q++] = (char)g_rbuf[j++];
                line[q] = 0;
                cookie_set(cur, line, 0);
            }
        }
        if ((status == 301 || status == 302 || status == 303 || status == 307 || status == 308) && hop < 40) {
            static char loc[4096];
            if (hdr_get(g_rbuf, he, "location", loc, sizeof(loc)) && loc[0]) {
                static char nu[4096];
                resolve(cur, loc, nu, sizeof(nu));
                int i = 0;
                for (; nu[i] && i < 4095; i++) cur[i] = nu[i];
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
        { static char cc[160]; if (hdr_get(g_rbuf, he, "cache-control", cc, sizeof(cc))) { for (int i = 0; cc[i]; i++) { if (ieqn(cc + i, "no-store", 8)) g_last_nostore = 1; if (ieqn(cc + i, "max-age=", 8)) { long v = 0; int j = i + 8; while (cc[j] >= '0' && cc[j] <= '9') { v = v * 10 + (cc[j] - '0'); j++; } g_last_maxage = v; } } if (ieqn(cc, "no-cache", 8) && g_last_maxage < 0) g_last_maxage = 0; } }
        { static char sc[8]; if (hdr_get(g_rbuf, he, "set-cookie", sc, sizeof(sc))) g_last_setcookie = 1; }
        if (!hdr_get(g_rbuf, he, "etag", g_last_etag, sizeof(g_last_etag))) g_last_etag[0] = 0;
        if (!hdr_get(g_rbuf, he, "last-modified", g_last_lm, sizeof(g_last_lm))) g_last_lm[0] = 0;
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
        if (g_nwk_kind == 1 || g_doc_hdr) { if (!hdr_get(g_rbuf, he, "content-security-policy", g_doc_csp, sizeof(g_doc_csp))) g_doc_csp[0] = 0; if (!hdr_get(g_rbuf, he, "content-security-policy-report-only", g_doc_cspro, sizeof(g_doc_cspro))) g_doc_cspro[0] = 0; if (!hdr_get(g_rbuf, he, "x-frame-options", g_doc_xfo, sizeof(g_doc_xfo))) g_doc_xfo[0] = 0; }
        if (g_nwk_kind == 2) { int hs = 0; while (hs < he && g_rbuf[hs] != '\n') hs++; if (hs < he) hs++; int hl = he - hs; if (hl < 0) hl = 0; if (hl > 32768) hl = 32768; r->hdrs = kmalloc((unsigned long)hl + 1); if (r->hdrs) { for (int q = 0; q < hl; q++) r->hdrs[q] = (char)g_rbuf[hs + q]; r->hdrs[hl] = 0; r->hlen = hl; } }
        int i = 0;
        for (; cur[i] && i < (int)sizeof(r->final_url) - 1; i++) r->final_url[i] = cur[i];
        r->final_url[i] = 0;
        cookies_flush(0);
        return 0;
    }
    return -6;
}

extern struct tls_scratch* g_tls_scr;
unsigned long tls_scratch_size(void);
extern int pz_inflate_part(const unsigned char*, int, unsigned char*, int);
void nf_swap(u64* save, u64 to);

#define NF_N 16
#define NF_STK (512u * 1024u)
#define NQ_N 96
#define NQ_MAXBODY (256 << 20)
#define NQ_PER_HOST 6

typedef struct nreq {
    int used, id, state, cancel, orphan, keep;
    long owner;
    char* url; char* meth; char* ctype; char* body; int blen; char* xhdr; char* referer;
    char host[256];
    u8* raw; int rawn, rawcap;
    u8* bod; int bodn, bodcap;
    int status, gz, ok, err, hdr_ready, hdr_sent, done_sent, delivered;
    char* hdrs; int hlen; char ctyp[96]; char* final_url;
    u8* dec; int decn, deccap; u64 dec_t; int dec_full, ctrail;
} nreq;
struct nfib { u64 rsp; u8* stk; struct tls_scratch* scr; nreq* q; int live; };
static struct nfib g_nf[NF_N];
static nreq g_nq[NQ_N];
static u64 g_nf_main_rsp;
static int g_nq_next = 1, g_nf_stepping, g_nf_live;
static unsigned g_nf_prog;

static void nq_cp(u8* d, const u8* s, int n) { for (int i = 0; i < n; i++) d[i] = s[i]; }
static char* nq_dup(const char* s, int n) { if (!s) return 0; if (n < 0) n = sl(s); char* d = kmalloc((unsigned long)n + 1); if (!d) return 0; nq_cp((u8*)d, (const u8*)s, n); d[n] = 0; return d; }
static int nq_grow(u8** b, int* cap, int need) {
    if (need <= *cap) return 1;
    if (need > NQ_MAXBODY + (1 << 20)) return 0;
    int nc = *cap ? *cap : 65536;
    while (nc < need) nc *= 2;
    u8* n = kmalloc((unsigned long)nc);
    if (!n) return 0;
    if (*b) { nq_cp(n, *b, *cap < need ? *cap : need); kfree(*b); }
    *b = n; *cap = nc;
    return 1;
}
static void nq_free(nreq* q) {
    char** ps[] = { &q->url, &q->meth, &q->ctype, &q->body, &q->xhdr, &q->referer, &q->hdrs, &q->final_url };
    for (unsigned i = 0; i < sizeof(ps) / sizeof(ps[0]); i++) if (*ps[i]) { kfree(*ps[i]); *ps[i] = 0; }
    if (q->raw) kfree(q->raw);
    if (q->bod) kfree(q->bod);
    if (q->dec) kfree(q->dec);
    for (unsigned i = 0; i < sizeof(*q); i++) ((u8*)q)[i] = 0;
}

static u64 nf_tmo(void) { return 30000; }
static void nf_yield(void) { struct nfib* f = g_nf_cur; nf_swap(&f->rsp, g_nf_main_rsp); }

static int nf_body(nreq* q, int* cpos, int chunked, int clen, int* fin) {
    if (!chunked) {
        int n = q->rawn - *cpos;
        if (n > 0) {
            if (!nq_grow(&q->bod, &q->bodcap, q->bodn + n + 1)) return -1;
            nq_cp(q->bod + q->bodn, q->raw + *cpos, n); q->bodn += n;
        }
        q->rawn = 0; *cpos = 0;
        if (clen >= 0 && q->bodn >= clen) { q->bodn = clen; *fin = 1; }
        return 0;
    }
    int i = *cpos;
    for (;;) {
        if (q->ctrail) {
            for (;;) { int e = i; while (e < q->rawn && q->raw[e] != '\n') e++; if (e >= q->rawn) goto stop; int blank = e == i || (e == i + 1 && q->raw[i] == '\r'); i = e + 1; if (blank) { *fin = 1; goto stop; } }
        }
        while (i < q->rawn && (q->raw[i] == '\r' || q->raw[i] == '\n')) i++;
        int j = i, sz = 0, any = 0;
        while (j < q->rawn) { int c = q->raw[j]; int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; if (v < 0) break; sz = sz * 16 + v; any = 1; j++; if (sz > NQ_MAXBODY) return -1; }
        while (j < q->rawn && q->raw[j] != '\n') j++;
        if (j >= q->rawn) break;
        j++;
        if (!any) return -1;
        if (sz == 0) { i = j; q->ctrail = 1; continue; }
        if (j + sz > q->rawn) break;
        if (!nq_grow(&q->bod, &q->bodcap, q->bodn + sz + 1)) return -1;
        nq_cp(q->bod + q->bodn, q->raw + j, sz); q->bodn += sz;
        i = j + sz;
    }
stop:
    if (i > 0) { int left = q->rawn - i; if (left > 0) for (int k = 0; k < left; k++) q->raw[k] = q->raw[i + k]; q->rawn = left > 0 ? left : 0; }
    *cpos = 0;
    return 0;
}

