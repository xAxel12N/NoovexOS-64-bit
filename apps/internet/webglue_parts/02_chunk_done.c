static int chunk_done(const u8* b, int n, int* pos) {
    int i = *pos;
    for (;;) {
        int j = i, sz = 0, any = 0;
        while (j < n) { int c = b[j]; int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; if (v < 0) break; sz = sz * 16 + v; any = 1; j++; if (sz > (1 << 28)) return -1; }
        while (j < n && b[j] != '\n') j++;
        if (j >= n) return 0;
        j++;
        if (!any) return -1;
        if (sz == 0) { for (;;) { int e = j; while (e < n && b[e] != '\n') e++; if (e >= n) return 0; int blank = e == j || (e == j + 1 && b[j] == '\r'); j = e + 1; if (blank) return j; } }
        if (j + sz + 2 > n) { *pos = i; return 0; }
        j += sz; if (b[j] == '\r') j++; if (j < n && b[j] == '\n') j++;
        i = j; *pos = i;
    }
}
static int http_send(pconn* p, const char* req, int rl) { return p->secure ? tls_sess_write(&p->io, &p->ss, (const u8*)req, rl) >= 0 : pc_wr(p, (const u8*)req, rl) == rl; }
void (*g_nwk_stream)(const char* html, int len, const char* url);
int g_nwk_offline;
static const char* g_stream_url;
static u8 *g_sbuf1, *g_sbuf2;
#define SBUF_SZ (2u << 20)
static int dechunk(u8* b, int n);
static void stream_try(int bo, int n, int chunked, int gz, unsigned long long tfirst, int* shots, int* lastn) {
    if (*shots >= 2 || !g_stream_url) return;
    unsigned long long el = nus() - tfirst;
    if (el < (*shots ? 1500000ULL : 300000ULL) || n - bo < 1024 || n - *lastn < 2048) return;
    if (!g_sbuf1) g_sbuf1 = kmalloc(SBUF_SZ);
    if (!g_sbuf2) g_sbuf2 = kmalloc(SBUF_SZ);
    if (!g_sbuf1 || !g_sbuf2) return;
    int len = n - bo; if (len > (int)SBUF_SZ - 1) len = (int)SBUF_SZ - 1;
    for (int i = 0; i < len; i++) g_sbuf1[i] = g_rbuf[bo + i];
    if (chunked) len = dechunk(g_sbuf1, len);
    const u8* d = g_sbuf1;
    if (gz) {
        extern int pz_inflate_part(const unsigned char*, int, unsigned char*, int);
        if (len < 18 || g_sbuf1[0] != 0x1F || g_sbuf1[1] != 0x8B) return;
        int flg = g_sbuf1[3], q = 10;
        if (flg & 4) { if (q + 2 > len) return; q += 2 + (g_sbuf1[q] | (g_sbuf1[q + 1] << 8)); }
        if (flg & 8) { while (q < len && g_sbuf1[q]) q++; q++; }
        if (flg & 16) { while (q < len && g_sbuf1[q]) q++; q++; }
        if (flg & 2) q += 2;
        if (q >= len) return;
        int dl = pz_inflate_part(g_sbuf1 + q, len - q, g_sbuf2, (int)SBUF_SZ - 1);
        if (dl <= 0) return;
        d = g_sbuf2; len = dl;
    }
    if (len < 2048) return;
    (*shots)++; *lastn = n;
    void (*cb)(const char*, int, const char*) = g_nwk_stream;
    const char* su = g_stream_url;
    g_nwk_stream = 0;
    g_nwk_offline = 1;
    cb((const char*)d, len, su);
    g_nwk_offline = 0;
    g_nwk_stream = cb;
}
static int http_recv(pconn* p, int head, int* outn, int* keep_out, unsigned long long t0) {
    int n = 0, he = -1, bo = 0, done = 0, keep = 1, clen = -1, chunked = 0, cpos = 0, until_close = 0;
    int sv = 0, sgz = 0, shots = 0, slast = 0;
    unsigned long long tfirst = 0;
    while (n < (int)RBUF_SZ - 1) {
        int k = pc_read(p, g_rbuf + n, (int)RBUF_SZ - 1 - n);
        if (k <= 0) { if (until_close && n > 0) done = 1; keep = 0; break; }
        if (!tfirst) tfirst = nus();
        n += k;
        if (he < 0) {
            he = find_hdr_end(g_rbuf, n);
            if (he < 0) continue;
            bo = g_rbuf[he] == '\n' ? he + 1 : he + 2;
            int status = 0; { int i = 0; while (i < n && g_rbuf[i] != ' ') i++; i++; while (i < n && g_rbuf[i] >= '0' && g_rbuf[i] <= '9') status = status * 10 + (g_rbuf[i++] - '0'); }
            if (status == 100) { for (int i = bo; i < n; i++) g_rbuf[i - bo] = g_rbuf[i]; n -= bo; he = -1; continue; }
            char v[96];
            if (hdr_get(g_rbuf, he, "connection", v, sizeof(v)) && ieqn(v, "close", 5)) keep = 0;
            if (n < 9 || g_rbuf[5] != '1' || g_rbuf[7] == '0') { if (!(hdr_get(g_rbuf, he, "connection", v, sizeof(v)) && ieqn(v, "keep-alive", 10))) keep = 0; }
            if (head || status == 204 || status == 304 || (status >= 100 && status < 200)) { done = 1; n = bo; break; }
            if (hdr_get(g_rbuf, he, "transfer-encoding", v, sizeof(v))) { for (int i = 0; v[i]; i++) if (ieqn(v + i, "chunked", 7)) chunked = 1; }
            if (!chunked && hdr_get(g_rbuf, he, "content-length", v, sizeof(v))) { long L = 0; int i = 0; while (v[i] == ' ') i++; while (v[i] >= '0' && v[i] <= '9') { L = L * 10 + (v[i] - '0'); i++; } clen = L > (long)RBUF_SZ ? (int)RBUF_SZ : (int)L; }
            if (!chunked && clen < 0) { until_close = 1; keep = 0; }
            cpos = bo;
            if (g_nwk_kind == 1 && g_nwk_stream && status == 200 && hdr_get(g_rbuf, he, "content-type", v, sizeof(v))) { for (int i = 0; v[i]; i++) if (ieqn(v + i, "html", 4)) sv = 1; if (sv && hdr_get(g_rbuf, he, "content-encoding", v, sizeof(v))) { if (ieqn(v, "gzip", 4) || ieqn(v, "x-gzip", 6)) sgz = 1; else sv = 0; } }
        }
        if (sv && g_nwk_stream && !done) stream_try(bo, n, chunked, sgz, tfirst, &shots, &slast);
        if (chunked) { int r = chunk_done(g_rbuf, n, &cpos); if (r < 0) { keep = 0; until_close = 1; chunked = 0; } else if (r > 0) { n = r; done = 1; break; } }
        else if (clen >= 0 && n - bo >= clen) { n = bo + clen; done = 1; break; }
    }
    unsigned long long tend = nus();
    if (tfirst) { g_netstat.wait_us += tfirst - t0; g_netstat.body_us += tend - tfirst; }
    *outn = n; *keep_out = done && keep;
    return done;
}
static int http_once(purl* u, const char* req, int rl, int* outn, int head) {
    *outn = 0;
    for (int attempt = 0; attempt < 2; attempt++) {
        int err = 0;
        pconn* p = pc_get(u, attempt > 0, &err);
        if (!p) return err;
        unsigned long long t0 = nus();
        if (!http_send(p, req, rl)) { pc_close(p); if (attempt == 0) continue; return -3; }
        int n = 0, keep = 0;
        int dn = http_recv(p, head, &n, &keep, t0);
        if (n == 0 && p->reused && attempt == 0) { pc_close(p); continue; }
        if (!dn && n > 0 && !head) { pc_close(p); if (attempt == 0) { netlog("download cut off, retrying"); continue; } *outn = 0; return -4; }
        g_netstat.requests++; g_netstat.bytes += (unsigned long long)n;
        if (keep) { p->last = g_ticks; pc_put(p); } else pc_close(p);
        *outn = n;
        return n > 0 ? 0 : -3;
    }
    return -3;
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
        while (r < n) { u8 c = b[r]; int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1; if (d < 0) break; if (sz > (1 << 26)) { sz = -1; break; } sz = sz * 16 + d; r++; any = 1; }
        while (r < n && b[r] != '\n') r++;
        r++;
        if (!any || sz <= 0 || r > n) break;
        if (sz > n - r) sz = n - r;
        for (int i = 0; i < sz; i++) b[w++] = b[r + i];
        r += sz;
        if (r < n && b[r] == '\r') r++;
        if (r < n && b[r] == '\n') r++;
    }
    return w;
}

unsigned long g_fetch_ms = 0; int g_fetch_n = 0;
int g_http_mode = 0;
static long g_last_maxage; static int g_last_nostore, g_last_setcookie;
#define HC_N 256
#define HC_MAX (48ul << 20)
typedef struct { char* url; char* body; int len, status, maxage; char ctype[96]; char final_url[4096]; char etag[128]; char lm[64]; unsigned long t, use; } hcent;
static char g_last_etag[128], g_last_lm[64]; static int g_reval_i = -1, g_wc_dirty, g_wc_loaded; static unsigned long g_wc_saved;
static unsigned long ep_s(void) { return (unsigned long)(k_realtime_ms() / 1000ull); }
static void wc_load(void);
static hcent g_hc[HC_N];
static unsigned long g_hc_total, g_hc_tick;
static int hc_seq(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int hc_ends(const char* u, const char* e) { int ul = sl(u), el = sl(e); const char* q = u; int n = ul; for (int i = 0; i < ul; i++) if (u[i] == '?' || u[i] == '#') { n = i; break; } (void)q; return n >= el && ieqn(u + n - el, e, el); }
static int hc_has(const char* h, const char* n) { int nl = sl(n); for (int i = 0; h[i]; i++) if (ieqn(h + i, n, nl)) return 1; return 0; }
static int hc_static(const char* u, const char* ct) {
    static const char* ex[] = { ".js", ".mjs", ".css", ".woff2", ".woff", ".ttf", ".otf", ".png", ".jpg", ".jpeg", ".gif", ".webp", ".svg", ".ico", ".avif" };
    for (unsigned i = 0; i < sizeof(ex) / sizeof(ex[0]); i++) if (hc_ends(u, ex[i])) return 1;
    if (ct && (hc_has(ct, "javascript") || hc_has(ct, "text/css") || hc_has(ct, "font/") || hc_has(ct, "image/"))) return 1;
    return 0;
}
static void hc_drop(int i) { if (!g_hc[i].url) return; kfree(g_hc[i].url); kfree(g_hc[i].body); g_hc_total -= (unsigned long)g_hc[i].len; g_hc[i].url = 0; }
static void hc_store(const char* url, nwk_resp* r, long maxage) {
    if (r->len < 0 || (unsigned long)r->len > HC_MAX / 4) return;
    for (int i = 0; i < HC_N; i++) if (g_hc[i].url && hc_seq(g_hc[i].url, url)) hc_drop(i);
    for (;;) {
        int fr = -1, lru = -1;
        for (int i = 0; i < HC_N; i++) { if (!g_hc[i].url) { if (fr < 0) fr = i; } else if (lru < 0 || g_hc[i].use < g_hc[lru].use) lru = i; }
        if (fr >= 0 && g_hc_total + (unsigned long)r->len <= HC_MAX) {
            hcent* e = &g_hc[fr];
            int ul = sl(url);
            e->url = kmalloc((unsigned long)ul + 1); e->body = kmalloc((unsigned long)r->len + 1);
            if (!e->url || !e->body) { if (e->url) kfree(e->url); if (e->body) kfree(e->body); e->url = 0; return; }
            for (int k = 0; k <= ul; k++) e->url[k] = url[k];
            for (int k = 0; k < r->len; k++) e->body[k] = r->body[k];
            e->body[r->len] = 0; e->len = r->len; e->status = r->status; e->maxage = (int)maxage;
            for (int k = 0; k < 96; k++) e->ctype[k] = r->ctype[k];
            for (int k = 0; k < 4096; k++) e->final_url[k] = r->final_url[k];
            for (int k = 0; k < 128; k++) e->etag[k] = g_last_etag[k];
            for (int k = 0; k < 64; k++) e->lm[k] = g_last_lm[k];
            e->t = ep_s(); e->use = ++g_hc_tick;
            if (hc_static(url, r->ctype)) g_wc_dirty = 1;
            g_hc_total += (unsigned long)r->len;
            return;
        }
        if (lru < 0) return;
        hc_drop(lru);
    }
}
static int hc_lookup(const char* url, nwk_resp* r) {
    unsigned long now = ep_s();
    wc_load();
    g_reval_i = -1;
    for (int i = 0; i < HC_N; i++) {
        hcent* e = &g_hc[i];
        if (!e->url || !hc_seq(e->url, url)) continue;
        unsigned long age = now >= e->t ? now - e->t : 0;
        int ok = g_http_mode == 1 ? age < 600 : (e->maxage > 0 && age < (unsigned long)e->maxage);
        if (!ok) { if (e->etag[0] || e->lm[0]) g_reval_i = i; return 0; }
        for (unsigned k = 0; k < sizeof(*r); k++) ((u8*)r)[k] = 0;
        r->body = kmalloc((unsigned long)e->len + 1);
        if (!r->body) return 0;
        for (int k = 0; k < e->len; k++) r->body[k] = e->body[k];
        r->body[e->len] = 0; r->len = e->len; r->status = e->status;
        for (int k = 0; k < 96; k++) r->ctype[k] = e->ctype[k];
        for (int k = 0; k < 4096; k++) r->final_url[k] = e->final_url[k];
        e->use = ++g_hc_tick;
        return 1;
    }
    return 0;
}
void nwk_http_cache_clear(void) { for (int i = 0; i < HC_N; i++) hc_drop(i); }
static int nwk_fetch_real(const char* url, const char* method, const char* ctype, const char* body, int blen, const char* referer, nwk_resp* r);
static int nwk_fetch_t(const char* url, const char* method, const char* ctype, const char* body, int blen, const char* referer, nwk_resp* r);
static char* req_head(char* p, purl* u, const char* cur, const char* meth, const char* referer);
#define PF_N 32
typedef struct { char* url; char* body; int len, status, maxage, nostore, setcookie; char ctype[96]; char etag[128]; char lm[64]; unsigned long t; } pfent;
static pfent g_pf[PF_N];
static void pf_drop(pfent* e) { if (e->url) kfree(e->url); if (e->body) kfree(e->body); e->url = 0; e->body = 0; }
static int pf_take(const char* url, nwk_resp* r) {
    for (int i = 0; i < PF_N; i++) {
        pfent* e = &g_pf[i];
        if (!e->url || !hc_seq(e->url, url)) continue;
        if (nwk_ms() - e->t > 60000) { pf_drop(e); return 0; }
        for (unsigned k = 0; k < sizeof(*r); k++) ((u8*)r)[k] = 0;
        r->body = e->body; e->body = 0; r->len = e->len; r->status = e->status;
        for (int k = 0; k < 95 && e->ctype[k]; k++) r->ctype[k] = e->ctype[k];
        int k = 0; for (; url[k] && k < (int)sizeof(r->final_url) - 1; k++) r->final_url[k] = url[k]; r->final_url[k] = 0;
        g_last_maxage = e->maxage; g_last_nostore = e->nostore; g_last_setcookie = e->setcookie;
        for (int q = 0; q < 128; q++) g_last_etag[q] = e->etag[q];
        for (int q = 0; q < 64; q++) g_last_lm[q] = e->lm[q];
        pf_drop(e);
        g_netstat.prefetched++;
        return 1;
    }
    return 0;
}
static void pf_store(const char* url, int n) {
    int he = find_hdr_end(g_rbuf, n);
    if (he < 0) return;
    int status = 0; { int i = 0; while (i < n && g_rbuf[i] != ' ') i++; i++; while (i < n && g_rbuf[i] >= '0' && g_rbuf[i] <= '9') status = status * 10 + (g_rbuf[i++] - '0'); }
    int setck = 0;
    for (int i = 0; i < he; i++) {
        if (i && g_rbuf[i - 1] != '\n') continue;
        if (i + 11 < he && ieqn((const char*)g_rbuf + i, "set-cookie:", 11)) {
            int j = i + 11; while (j < he && g_rbuf[j] == ' ') j++;
            static char line[4096]; int q = 0;
            while (j < he && g_rbuf[j] != '\r' && g_rbuf[j] != '\n' && q < 4095) line[q++] = (char)g_rbuf[j++];
            line[q] = 0; cookie_set(url, line, 0); setck = 1;
        }
    }
    if (status < 200 || status >= 300) return;
    int bo = g_rbuf[he] == '\n' ? he + 1 : he + 2; if (bo > n) bo = n;
    int len = n - bo; u8* data = g_rbuf + bo;
    char te[96], ce[96];
    if (hdr_get(g_rbuf, he, "transfer-encoding", te, sizeof(te))) { for (int i = 0; te[i]; i++) if (ieqn(te + i, "chunked", 7)) { len = dechunk(data, len); break; } }
    if (hdr_get(g_rbuf, he, "content-encoding", ce, sizeof(ce))) {
        if (ieqn(ce, "gzip", 4) || ieqn(ce, "x-gzip", 6)) { int dl = gunzip(data, len, g_dbuf, (int)DBUF_SZ - 1); if (dl < 0) return; data = g_dbuf; len = dl; }
        else if (ieqn(ce, "deflate", 7)) { int dl = pz_inflate_raw(data + 2, len - 2, g_dbuf, (int)DBUF_SZ - 1); if (dl < 0) dl = pz_inflate_raw(data, len, g_dbuf, (int)DBUF_SZ - 1); if (dl < 0) return; data = g_dbuf; len = dl; }
        else if (!ieqn(ce, "identity", 8)) return;
    }
    if (len < 0) return;
    int slot = -1; for (int i = 0; i < PF_N; i++) if (!g_pf[i].url) { slot = i; break; }
    if (slot < 0) { unsigned long old = ~0ul; for (int i = 0; i < PF_N; i++) if (g_pf[i].t < old) { old = g_pf[i].t; slot = i; } if (slot < 0) return; pf_drop(&g_pf[slot]); }
    pfent* e = &g_pf[slot];
    int ul = sl(url);
    e->url = kmalloc((unsigned long)ul + 1); e->body = kmalloc((unsigned long)len + 1);
    if (!e->url || !e->body) { pf_drop(e); return; }
    for (int k = 0; k <= ul; k++) e->url[k] = url[k];
    for (int k = 0; k < len; k++) e->body[k] = (char)data[k];
    e->body[len] = 0; e->len = len; e->status = status; e->setcookie = setck; e->t = nwk_ms();
    if (!hdr_get(g_rbuf, he, "content-type", e->ctype, sizeof(e->ctype))) e->ctype[0] = 0;
    if (!hdr_get(g_rbuf, he, "etag", e->etag, sizeof(e->etag))) e->etag[0] = 0;
    if (!hdr_get(g_rbuf, he, "last-modified", e->lm, sizeof(e->lm))) e->lm[0] = 0;
    e->maxage = -1; e->nostore = 0;
    { char cc[160]; if (hdr_get(g_rbuf, he, "cache-control", cc, sizeof(cc))) { for (int i = 0; cc[i]; i++) { if (ieqn(cc + i, "no-store", 8)) e->nostore = 1; if (ieqn(cc + i, "max-age=", 8)) { long v = 0; int j = i + 8; while (cc[j] >= '0' && cc[j] <= '9') { v = v * 10 + (cc[j] - '0'); j++; } e->maxage = v; } } if (ieqn(cc, "no-cache", 8) && e->maxage < 0) e->maxage = 0; } }
}
static int hc_has_url(const char* url) { for (int i = 0; i < HC_N; i++) if (g_hc[i].url && hc_seq(g_hc[i].url, url)) return 1; return 0; }
static int g_pf_keep;
static int pf_has(const char* url) { for (int i = 0; i < PF_N; i++) if (g_pf[i].url && hc_seq(g_pf[i].url, url)) return 1; return 0; }
void nwk_prefetch(const char** urls, int n, const char* referer);
void nwk_prefetch_more(const char** urls, int n, const char* referer) { g_pf_keep = 1; nwk_prefetch(urls, n, referer); g_pf_keep = 0; }
void nwk_prefetch(const char** urls, int n, const char* referer) {
    if (!g_pf_keep) for (int i = 0; i < PF_N; i++) pf_drop(&g_pf[i]);
    if (!g_rbuf) g_rbuf = kmalloc(RBUF_SZ);
    if (!g_dbuf) g_dbuf = kmalloc(DBUF_SZ);
    if (!g_rbuf || !g_dbuf) return;
    wc_load();
    unsigned long long t0 = nus();
    u64 svt = g_pc_tmo; g_pc_tmo = 400;
    static char bad[8][256]; int nbad = 0;
    struct { pconn* p; int idx; unsigned long long ts; } inf[PF_N];
    static char req[65536];
    int doneu[PF_N]; for (int i = 0; i < PF_N; i++) doneu[i] = 0;
    if (n > PF_N) n = PF_N;
    for (int wave = 0; wave < 8; wave++) {
        int ni = 0;
        for (int i = 0; i < n && ni < 6; i++) {
            if (doneu[i]) continue;
            if (g_http_mode != 2 && hc_has_url(urls[i])) { doneu[i] = 1; continue; }
            if (g_pf_keep && pf_has(urls[i])) { doneu[i] = 1; continue; }
            static purl u;
            if (parse_url(urls[i], &u) < 0) { doneu[i] = 1; continue; }
            int same = 0; for (int k = 0; k < ni; k++) if (inf[k].p->port == u.port && inf[k].p->secure == u.secure && pc_streq(inf[k].p->host, u.host)) same++;
            if (same >= 4) continue;
            int skip = 0; for (int b = 0; b < nbad; b++) if (pc_streq(bad[b], u.host)) skip = 1;
            if (skip) { doneu[i] = 1; continue; }
            int err = 0;
            pconn* p = pc_get(&u, 0, &err);
            doneu[i] = 1;
            if (!p) { if (nbad < 8) { int k2 = 0; for (; u.host[k2] && k2 < 255; k2++) bad[nbad][k2] = u.host[k2]; bad[nbad][k2] = 0; nbad++; } continue; }
            char* q = req_head(req, &u, urls[i], "GET", referer);
            q = put(q, "\r\n");
            unsigned long long ts = nus();
            if (!http_send(p, req, (int)(q - req))) { pc_close(p); continue; }
            p->busy = 1;
            inf[ni].p = p; inf[ni].idx = i; inf[ni].ts = ts; ni++;
        }
        if (!ni) break;
        for (int k = 0; k < ni; k++) {
            pconn* p = inf[k].p;
            int nn = 0, keep = 0;
            int dn = http_recv(p, 0, &nn, &keep, inf[k].ts);
            p->busy = 0;
            if (nn > 0 && dn) { g_netstat.requests++; g_netstat.bytes += (unsigned long long)nn; pf_store(urls[inf[k].idx], nn); }
            else if (nn > 0) keep = 0;
            if (keep) p->last = g_ticks; else pc_close(p);
        }
    }
    g_pc_tmo = svt; g_pc_abort = 0;
    g_netstat.fetch_us += nus() - t0;
    cookies_flush(0);
}
