static int frame_paint_cur(int x, int y, int w, int h, int cx0, int cy0, int cx1, int cy1, unsigned int* fb, int stride) {
    g_frame_x = x; g_frame_y = y; g_frame_w = w; g_frame_h = h;
    int vx0 = x > cx0 ? x : cx0, vy0 = y > cy0 ? y : cy0, vx1 = x + w < cx1 ? x + w : cx1, vy1 = y + h < cy1 ? y + h : cy1;
    g_frame_vx0 = vx0; g_frame_vy0 = vy0; g_frame_vx1 = vx1; g_frame_vy1 = vy1;
    if (g_frame_state != 2) return 0;
    if (vx0 >= vx1 || vy0 >= vy1) return 1;
    if (w != g_frame_lw || h != g_frame_lh) { frq_zero(); g_freq.w = w; g_freq.h = h; frame_call(NWE_OP_LAYOUT, &g_freq); g_frame_lw = w; g_frame_lh = h; if (g_frame_state != 2) return 0; }
    int need = w * h;
    if (need > g_frame_bufcap) { if (g_frame_buf) kfree(g_frame_buf); g_frame_buf = kmalloc((unsigned long)need * 4); g_frame_bufcap = g_frame_buf ? need : 0; if (!g_frame_buf) return 0; }
    for (int i = 0; i < need; i++) g_frame_buf[i] = 0xFFFFFF;
    frq_zero();
    g_freq.fb = g_frame_buf; g_freq.stride = w; g_freq.fbw = w; g_freq.fbh = h; g_freq.x = 0; g_freq.y = 0; g_freq.w = w; g_freq.h = h;
    if (frame_call(NWE_OP_PAINT, &g_freq) < 0 || g_frame_state != 2) return 0;
    for (int yy = vy0; yy < vy1; yy++) {
        unsigned int* d = fb + (u64)yy * (u64)stride;
        const unsigned int* sr = g_frame_buf + (u64)(yy - y) * (u64)w - x;
        for (int xx = vx0; xx < vx1; xx++) d[xx] = sr[xx];
    }
    g_frame_dirty = 0;
    return 1;
}

static void frame_take_nav(void) {
    int i = 0; for (; g_freq.nav[i] && i < 4095; i++) g_frame_url[i] = g_freq.nav[i]; g_frame_url[i] = 0;
    if (g_frame_src) { kfree(g_frame_src); g_frame_src = 0; }
    if (F->pbody) { kfree(F->pbody); F->pbody = 0; } F->plen = 0;
    if (g_freq.navmethod[0] == 'P' && g_freq.navbody && g_freq.navblen >= 0) { F->pbody = kmalloc((unsigned long)g_freq.navblen + 1); if (F->pbody) { for (int k = 0; k < g_freq.navblen; k++) F->pbody[k] = g_freq.navbody[k]; F->pbody[g_freq.navblen] = 0; F->plen = g_freq.navblen; } }
    g_frame_state = 1;
}
static int frame_tick_one(void) {
    if (g_frame_state == 1) {
        g_frame_state = 4; F->why[0] = 0;
        nwk_status("Loading embedded page...");
        const char* data; int len; static char furl[4096]; nwk_resp rr; int fetched = 0;
        for (unsigned k = 0; k < sizeof(rr); k++) ((u8*)&rr)[k] = 0;
        if (g_frame_src) { data = g_frame_src; len = g_frame_srclen; { const char* ab = "about:srcdoc"; int i = 0; for (; ab[i]; i++) furl[i] = ab[i]; furl[i] = 0; } }
        else {
            { extern const char* browser_cur_url(void); g_doc_hdr = 1; int frc = nwk_fetch(g_frame_url, F->pbody ? "POST" : "GET", F->pbody ? "application/x-www-form-urlencoded" : 0, F->pbody, F->plen, browser_cur_url(), &rr); g_doc_hdr = 0; if (F->pbody) { kfree(F->pbody); F->pbody = 0; F->plen = 0; } if (frc < 0 || !rr.body) { const char* w = frc == -100 ? "Could not load: name lookup failed" : frc == -2 ? "Could not load: could not connect" : frc == -5 ? "Could not load: secure connection failed" : frc == -3 ? "Could not load: no answer from the server" : "Could not load the page"; frame_fail_why(w); g_frame_state = 3; return 1; }
              if (F != &g_fs[PSLOT] && frame_refused(rr.final_url[0] ? rr.final_url : g_frame_url, browser_cur_url())) { { const char* w = g_frame_refuse_why; int k = 0; for (; w[k] && k < 199; k++) F->why[k] = w[k]; F->why[k] = 0; ffail_note(g_frame_url, F->why); } nwk_free(rr.body); g_doc_csp[0] = 0; g_doc_cspro[0] = 0; g_frame_state = 3; return 1; } }
            data = rr.body; len = rr.len; fetched = 1;
            int i = 0; const char* fu = rr.final_url[0] ? rr.final_url : g_frame_url; for (; fu[i] && i < 4095; i++) furl[i] = fu[i]; furl[i] = 0;
        }
        if (!g_frame_inited) { frq_zero(); frame_call(NWE_OP_INIT, &g_freq); g_frame_inited = 1; }
        frq_zero();
        g_freq.url = furl; g_freq.data = data; g_freq.len = len; g_freq.w = g_frame_w > 0 ? g_frame_w : 300; g_freq.h = g_frame_h > 0 ? g_frame_h : 150;
        if (F->sbx) g_freq.key = (int)(0x5B000000u | (unsigned)(F->sbx & 0xFFFF));
        g_frame_state = 2;
        frame_call(NWE_OP_LOAD, &g_freq);
        g_frame_lw = g_freq.w; g_frame_lh = g_freq.h;
        if (fetched) nwk_free(rr.body);
        if (g_frame_state == 2 && g_freq.nav[0]) frame_take_nav();
        nwk_status("Done");
        return 1;
    }
    if (g_frame_state == 2) {
        frq_zero();
        frame_call(NWE_OP_TICK, &g_freq);
        int d = g_freq.ret || g_freq.dirty || g_frame_dirty || g_q_up.n > 0;
        if (g_frame_state == 2 && g_freq.nav[0]) { frame_take_nav(); d = 1; }
        return d;
    }
    return 0;
}

int frame_tick(void) { int d = 0; for (int i = 0; i < NFS; i++) { F = &g_fs[i]; if (frame_tick_one()) d = 1; } FSEL(); return d; }

static int g_hit_slot;
int frame_hit(int px, int py) {
    for (int i = 0; i < NFS_IF; i++) { fslot* q = &g_fs[i]; if (q->state == 2 && px >= q->vx0 && px < q->vx1 && py >= q->vy0 && py < q->vy1) { g_hit_slot = i; return 1; } }
    return 0;
}

static void slot_mouse(int sl, int op, int px, int py) {
    F = &g_fs[sl];
    if (g_frame_state != 2) { FSEL(); return; }
    g_kfocus = sl;
    frq_zero();
    g_freq.x = px - g_frame_x; g_freq.y = py - g_frame_y; g_freq.btn = 1;
    frame_call(op, &g_freq);
    if (g_frame_state == 2 && g_freq.nav[0]) frame_take_nav();
    g_frame_dirty = 1;
    FSEL();
}
void frame_mouse(int op, int px, int py) { slot_mouse(g_hit_slot, op, px, py); }
int frame_focus(void) { return g_kfocus; }
void frame_blur(void) { g_kfocus = -1; }
int frame_key(int kv) {
    int sl = g_kfocus; if (sl < 0 || sl >= NFS) return 0;
    F = &g_fs[sl];
    if (g_frame_state != 2) { g_kfocus = -1; FSEL(); return 0; }
    frq_zero(); g_freq.key = kv;
    frame_call(NWE_OP_KEY, &g_freq);
    if (g_frame_state == 2 && g_freq.nav[0]) frame_take_nav();
    g_frame_dirty = 1; FSEL(); return 1;
}
static int popup_in(int px, int py) { fslot* q = &g_fs[PSLOT]; return q->state == 2 && px >= q->vx0 && px < q->vx1 && py >= q->vy0 && py < q->vy1; }
int frame_wheel(int px, int py, int dy, int dx) {
    int sl = -1;
    if (popup_in(px, py)) sl = PSLOT; else if (frame_hit(px, py)) sl = g_hit_slot;
    if (sl < 0) return 0;
    F = &g_fs[sl];
    frq_zero(); g_freq.x = px - g_frame_x; g_freq.y = py - g_frame_y; g_freq.key = dy; g_freq.btn = dx;
    frame_call(NWE_OP_WHEEL, &g_freq);
    g_frame_dirty = 1; FSEL(); return 1;
}
int nwk_popup_open(const char* url, int w, int h) {
    if (!url || !url[0]) return 0;
    int kl = 0; while (url[kl]) kl++;
    if (kl >= (int)sizeof(g_fs[0].url)) return 0;
    F = &g_fs[PSLOT]; frame_reset_one();
    for (int i = 0; i <= kl; i++) g_frame_url[i] = url[i];
    if (w < 240) w = 240; if (w > 1600) w = 1600; if (h < 200) h = 200; if (h > 1200) h = 1200;
    g_frame_w = w; g_frame_h = h; g_frame_lw = 0; g_frame_lh = 0;
    g_frame_state = 1; g_kfocus = PSLOT;
    FSEL(); return 1;
}
int nwk_popup_open2(const char* url, int w, int h) { (void)url; (void)w; (void)h; return 0; }
int nwk_popup_info(void) { fslot* q = &g_fs[PSLOT]; if (q->closereq) return 0; return q->state == 4 ? 1 : q->state; }
int nwk_popup_info2(void) { return 0; }
void nwk_popup_close(void) { F = &g_fs[PSLOT]; frame_reset_one(); FSEL(); }
void nwk_popup_close_self(void) { g_fs[PSLOT].closereq = 1; }
int popup_state(void) { fslot* q = &g_fs[PSLOT]; if (q->closereq) { F = q; frame_reset_one(); FSEL(); return 0; } return q->state; }
const char* popup_url(void) { return g_fs[PSLOT].url; }
void popup_size(int* w, int* h) { *w = g_fs[PSLOT].w; *h = g_fs[PSLOT].h; }
int popup_why(char* out, int cap) { int k = 0; for (; g_fs[PSLOT].why[k] && k < cap - 1; k++) out[k] = g_fs[PSLOT].why[k]; out[k] = 0; return k; }
int popup_paint(int x, int y, int w, int h, int cx0, int cy0, int cx1, int cy1, unsigned int* fb, int stride) {
    if (w < 16 || h < 16 || w > 4096 || h > 4096) return 0;
    F = &g_fs[PSLOT];
    if (g_frame_state == 0) { FSEL(); return 0; }
    int r = frame_paint_cur(x, y, w, h, cx0, cy0, cx1, cy1, fb, stride);
    FSEL(); return r;
}
int popup_hit(int px, int py) { return popup_in(px, py); }
void popup_mouse(int op, int px, int py) { slot_mouse(PSLOT, op, px, py); }
void popup_close(void) { nwk_popup_close(); }

#define WC_NAME "webcache.bin"
#define WC_CAP (28ul << 20)
static const char WC_STAMP[] = "NVWC3 " __DATE__ " " __TIME__;
typedef struct { u8* b; unsigned long n, cap; } wbuf;
static int wb_put(wbuf* w, const void* d, unsigned long n) { if (w->n + n > w->cap) return 0; const u8* s = (const u8*)d; for (unsigned long i = 0; i < n; i++) w->b[w->n + i] = s[i]; w->n += n; return 1; }
static int wb_u32(wbuf* w, u32 v) { u8 t[4] = { (u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24) }; return wb_put(w, t, 4); }
static int wb_str(wbuf* w, const char* s) { u32 l = (u32)sl(s); return wb_u32(w, l) && wb_put(w, s, l); }
static u32 rd_u32(const u8* p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static unsigned long long wc_hash(const u8* p, unsigned long n) { unsigned long long h = 1469598103934665603ull; for (unsigned long i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; } return h; }
static int g_wc_slot = -1, g_wc_writing;
int fs_store_nosync(const char* name, const u8* data, int len, int folder);
int fs_sync_step(int k, int maxblk);
static void wc_load(void) {
    if (g_wc_loaded) return;
    g_wc_loaded = 1;
    extern int disk_ok; if (!disk_ok) return;
    int slot = -1;
    for (int i = 0; i < FS_N; i++) if (FS[i].used && pc_streq(FS[i].name, WC_NAME)) { slot = i; break; }
    if (slot < 0) return;
    const u8* d = (const u8*)fsd(slot); unsigned long n = (unsigned long)FS[slot].len, o = 0;
    unsigned long ml = (unsigned long)sl(WC_STAMP);
    if (n < ml + 12) return;
    for (unsigned long i = 0; i < ml; i++) if (d[i] != (u8)WC_STAMP[i]) return;
    { unsigned long pl = rd_u32(d + ml); unsigned long long hv = (unsigned long long)rd_u32(d + ml + 4) | ((unsigned long long)rd_u32(d + ml + 8) << 32);
      if (pl != n - ml - 12 || wc_hash(d + ml + 12, pl) != hv) { nwk_log("webcache: saved cache incomplete, ignored"); return; } }
    o = ml + 12;
    unsigned long now = ep_s();
    while (o + 5 <= n) {
        u8 ty = d[o++];
        if (ty == 1) {
            if (o + 16 > n) break;
            unsigned long long key = (unsigned long long)rd_u32(d + o) | ((unsigned long long)rd_u32(d + o + 4) << 32); int srclen = (int)rd_u32(d + o + 8); u32 len = rd_u32(d + o + 12); o += 16;
            if (o + len > n) break;
            nwk_code_put(key, srclen, d + o, (int)len); o += len;
        } else if (ty == 2) {
            const char* f[5]; u32 fl[5];
            int bad = 0;
            for (int k = 0; k < 5; k++) { if (o + 4 > n) { bad = 1; break; } fl[k] = rd_u32(d + o); o += 4; if (o + fl[k] > n) { bad = 1; break; } f[k] = (const char*)d + o; o += fl[k]; }
            if (bad || o + 16 > n) break;
            int status = (int)rd_u32(d + o), maxage = (int)rd_u32(d + o + 4); unsigned long t = rd_u32(d + o + 8); u32 len = rd_u32(d + o + 12); o += 16;
            if (o + len > n) break;
            const u8* body = d + o; o += len;
            if (fl[0] == 0 || fl[0] > 4000 || len > HC_MAX / 4) continue;
            if (maxage > 0 && now >= t && now - t >= (unsigned long)maxage && !fl[3] && !fl[4]) continue;
            int fr = -1; for (int i = 0; i < HC_N; i++) if (!g_hc[i].url) { fr = i; break; }
            if (fr < 0 || g_hc_total + len > HC_MAX) continue;
            hcent* e = &g_hc[fr];
            e->url = kmalloc(fl[0] + 1); e->body = kmalloc(len + 1);
            if (!e->url || !e->body) { if (e->url) kfree(e->url); if (e->body) kfree(e->body); e->url = 0; break; }
            for (u32 k = 0; k < fl[0]; k++) e->url[k] = f[0][k]; e->url[fl[0]] = 0;
            for (u32 k = 0; k < len; k++) e->body[k] = (char)body[k]; e->body[len] = 0;
            char* dst[4] = { e->ctype, e->final_url, e->etag, e->lm }; int cap[4] = { 96, 4096, 128, 64 };
            for (int q = 0; q < 4; q++) { u32 L = fl[q + 1]; if ((int)L > cap[q] - 1) L = (u32)cap[q] - 1; for (u32 k = 0; k < L; k++) dst[q][k] = f[q + 1][k]; dst[q][L] = 0; }
            e->len = (int)len; e->status = status; e->maxage = maxage; e->t = t; e->use = ++g_hc_tick; g_hc_total += len;
            g_netstat.disk_hits++;
        } else break;
    }
    g_wc_dirty = 0;
}
static void wc_save(void) {
    extern int disk_ok; if (!disk_ok) return;
    wbuf w; w.cap = WC_CAP + 65536; w.n = 0; w.b = kmalloc(w.cap); if (!w.b) return;
    wb_put(&w, WC_STAMP, (unsigned long)sl(WC_STAMP));
    unsigned long hdr = w.n; wb_u32(&w, 0); wb_u32(&w, 0); wb_u32(&w, 0);
    unsigned long used = 0;
    int ord[CC_N]; int no = 0;
    for (int i = 0; i < CC_N; i++) if (g_cc[i].p) ord[no++] = i;
    for (int a = 1; a < no; a++) { int v = ord[a], b = a; while (b > 0 && g_cc[ord[b - 1]].use < g_cc[v].use) { ord[b] = ord[b - 1]; b--; } ord[b] = v; }
    for (int q = 0; q < no; q++) { ccent* c = &g_cc[ord[q]]; if (used + (unsigned long)c->len > WC_CAP / 2) continue; u8 ty = 1; if (!wb_put(&w, &ty, 1) || !wb_u32(&w, (u32)c->key) || !wb_u32(&w, (u32)(c->key >> 32)) || !wb_u32(&w, (u32)c->srclen) || !wb_u32(&w, (u32)c->len) || !wb_put(&w, c->p, (unsigned long)c->len)) break; used += (unsigned long)c->len; }
    int ho[HC_N]; int nh = 0;
    for (int i = 0; i < HC_N; i++) if (g_hc[i].url && hc_static(g_hc[i].url, g_hc[i].ctype) && (g_hc[i].maxage > 0 || g_hc[i].etag[0] || g_hc[i].lm[0])) ho[nh++] = i;
    for (int a = 1; a < nh; a++) { int v = ho[a], b = a; while (b > 0 && g_hc[ho[b - 1]].use < g_hc[v].use) { ho[b] = ho[b - 1]; b--; } ho[b] = v; }
    for (int q = 0; q < nh; q++) { hcent* e = &g_hc[ho[q]]; if (used + (unsigned long)e->len > WC_CAP) continue; u8 ty = 2;
        if (!wb_put(&w, &ty, 1) || !wb_str(&w, e->url) || !wb_str(&w, e->ctype) || !wb_str(&w, e->final_url) || !wb_str(&w, e->etag) || !wb_str(&w, e->lm) || !wb_u32(&w, (u32)e->status) || !wb_u32(&w, (u32)e->maxage) || !wb_u32(&w, (u32)e->t) || !wb_u32(&w, (u32)e->len) || !wb_put(&w, e->body, (unsigned long)e->len)) break;
        used += (unsigned long)e->len; }
    { unsigned long pl = w.n - hdr - 12; unsigned long long hv = wc_hash(w.b + hdr + 12, pl); u32 v[3] = { (u32)pl, (u32)hv, (u32)(hv >> 32) }; for (int q = 0; q < 3; q++) for (int k = 0; k < 4; k++) w.b[hdr + (unsigned long)q * 4 + (unsigned long)k] = (u8)(v[q] >> (k * 8)); }
    g_wc_slot = fs_store_nosync(WC_NAME, w.b, (int)w.n, 0);
    kfree(w.b);
    g_wc_dirty = 0;
    g_wc_saved = nwk_ms();
    g_wc_writing = g_wc_slot >= 0;
}
static void wc_write_step(int force) {
    if (!g_wc_writing) return;
    if (force) { extern volatile int g_frz_hold; int h = g_frz_hold; g_frz_hold = 1; int r; do r = fs_sync_step(g_wc_slot, 1 << 20); while (r == 0); g_frz_hold = h; g_wc_writing = 0; return; }
    unsigned long t0 = nwk_ms(); int r;
    do r = fs_sync_step(g_wc_slot, 32); while (r == 0 && nwk_ms() - t0 < 12);
    if (r != 0) g_wc_writing = 0;
    if (r < 0) nwk_log("webcache: could not save the cache to the disk");
}
void nwk_webcache_tick(int force) {
    if (g_in_engine) return;
    if (g_wc_writing) { wc_write_step(force); if (!force) return; }
    if (!g_wc_dirty || !g_wc_loaded) return;
    if (!force && nwk_ms() - g_wc_saved < 60000) return;
    wc_save();
    wc_write_step(force);
}
void nwk_webcache_clear(void) { for (int i = 0; i < CC_N; i++) if (g_cc[i].p) { CC_FREE(g_cc[i].p); g_cc[i].p = 0; } g_cc_total = 0; g_wc_loaded = 1; g_wc_dirty = 0; g_wc_writing = 0; extern int disk_ok; if (disk_ok) fs_save_bin(WC_NAME, (const u8*)"", 0, 0); }
