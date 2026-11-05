void nwk_webcache_tick(int force);
void web_tick_flush(void) { cookies_flush(0); nwk_webcache_tick(0); }

#define FQ_N 64
typedef struct { char* m[FQ_N]; int len[FQ_N]; int h, t, n; } fqueue;
typedef struct { nwe_req freq; char url[4096]; char* src; int sbx, srclen, state, x, y, w, h, lw, lh, vx0, vy0, vx1, vy1; unsigned int* buf; int bufcap, dirty, inited; fqueue qd, qu; char why[200]; char* pbody; int plen, closereq; } fslot;
#define NFS 5
#define NFS_IF 4
#define PSLOT 4
static fslot g_fs[NFS];
static fslot* F = &g_fs[0];
static struct { char url[512]; char why[200]; } g_ffail[8]; static int g_ffail_n;
static void ffail_note(const char* url, const char* why) { int i = g_ffail_n++ % 8; int k = 0; for (; url[k] && k < 511; k++) g_ffail[i].url[k] = url[k]; g_ffail[i].url[k] = 0; for (k = 0; why[k] && k < 199; k++) g_ffail[i].why[k] = why[k]; g_ffail[i].why[k] = 0; }
static void frame_fail_why(const char* why) {
    int k = 0; for (; why[k] && k < 199; k++) F->why[k] = why[k]; F->why[k] = 0;
    const char* u = F->src ? "about:srcdoc" : F->url;
    ffail_note(u, F->why);
    char m[800], *p = m; const char* t = "Embedded page failed: "; while (*t) *p++ = *t++;
    for (int i = 0; u[i] && i < 400; i++) *p++ = u[i];
    *p++ = ':'; *p++ = ' '; for (int i = 0; F->why[i]; i++) *p++ = F->why[i]; *p = 0;
    nwk_error(m);
}
static int g_fsel;
static int g_kfocus = -1;
#define g_freq (F->freq)
#define g_frame_url (F->url)
#define g_frame_src (F->src)
#define g_frame_srclen (F->srclen)
#define g_frame_state (F->state)
#define g_frame_x (F->x)
#define g_frame_y (F->y)
#define g_frame_w (F->w)
#define g_frame_h (F->h)
#define g_frame_lw (F->lw)
#define g_frame_lh (F->lh)
#define g_frame_vx0 (F->vx0)
#define g_frame_vy0 (F->vy0)
#define g_frame_vx1 (F->vx1)
#define g_frame_vy1 (F->vy1)
#define g_frame_buf (F->buf)
#define g_frame_bufcap (F->bufcap)
#define g_frame_dirty (F->dirty)
#define g_frame_inited (F->inited)
#define g_q_down (F->qd)
#define g_q_up (F->qu)
#define FSEL() (F = &g_fs[g_fsel])
void nwk_frame_fail(const char* why) { FSEL(); if (F->state) frame_fail_why(why && why[0] ? why : "Could not load the page"); }

static void fq_clear(fqueue* q) { while (q->n > 0) { kfree(q->m[q->h]); q->h = (q->h + 1) % FQ_N; q->n--; } q->h = q->t = 0; }
static int fq_push(fqueue* q, const char* m, int len) {
    if (len < 0 || len > 262143 || q->n >= FQ_N) return -1;
    char* c = kmalloc((unsigned long)len + 1);
    if (!c) return -1;
    for (int i = 0; i < len; i++) c[i] = m[i];
    c[len] = 0;
    q->m[q->t] = c; q->len[q->t] = len; q->t = (q->t + 1) % FQ_N; q->n++;
    return len;
}
static int fq_pop(fqueue* q, char* out, int cap) {
    if (q->n <= 0) return -1;
    int len = q->len[q->h];
    if (len > cap) len = cap;
    for (int i = 0; i < len; i++) out[i] = q->m[q->h][i];
    kfree(q->m[q->h]); q->h = (q->h + 1) % FQ_N; q->n--;
    return len;
}

static long frame_call(int op, nwe_req* r) {
    if (!g_estack) { { g_estack = kmalloc(ESTACK_SZ); extern int mm_guard_range_bottom(void*, unsigned long); if (g_estack) mm_guard_range_bottom(g_estack, ESTACK_SZ); } if (!g_estack) return -1; }
    long v;
    int sl = (int)(F - g_fs);
    long (*ent)(long, void*) = g_fent[sl >= 0 && sl < NFS ? sl : 0];
    if (g_web_nest) return -3;
    if (g_in_engine) { int sv = g_in_frame; g_in_frame = sl + 1; v = ent(op, r); g_in_frame = sv; }
    else {
        g_in_engine = 1; g_in_frame = sl + 1;
        g_call_t0 = nwk_ms(); g_call_s0 = g_susp_total; g_busy_esc = 0;
        v = nwe_call_stack(ent, op, r, g_estack + ESTACK_SZ - 64);
        g_in_frame = 0; g_in_engine = 0;
    }
    if (v == -1 && op != NWE_OP_INIT) { g_frame_state = 3; if (!F->why[0]) frame_fail_why("The embedded page stopped (page engine error)"); }
    return v;
}
static int src_match(const char* tok, int tl, const char* url, const char* self_url) {
    purl u; if (parse_url(url, &u) < 0) return 0;
    if (tl == 1 && tok[0] == '*') return 1;
    if (tl == 6 && ieqn(tok, "'self'", 6)) { char a[300], b2[300]; return org_of(url, a, sizeof a) && org_of(self_url, b2, sizeof b2) && hc_seq(a, b2); }
    if (tok[tl - 1] == ':') { return (tl == 6 && ieqn(tok, "https:", 6) && u.secure) || (tl == 5 && ieqn(tok, "http:", 5)); }
    const char* h = tok; int hl = tl; int sch = -1;
    if (hl > 8 && ieqn(h, "https://", 8)) { sch = 1; h += 8; hl -= 8; } else if (hl > 7 && ieqn(h, "http://", 7)) { sch = 0; h += 7; hl -= 7; }
    int e = 0; while (e < hl && h[e] != '/' && h[e] != ':') e++;
    if (sch == 1 && !u.secure) return 0;
    int ul = sl(u.host);
    if (e > 2 && h[0] == '*' && h[1] == '.') { int sfx = e - 1; if (ul < sfx) return 0; return ieqn(u.host + ul - sfx, h + 1, sfx); }
    return e == ul && ieqn(u.host, h, e);
}
extern int g_web_csp;
static const char* g_frame_refuse_why = "";
static int frame_refused(const char* furl, const char* top) {
    if (!g_web_csp) return 0;
    char m[700], *p;
    const char* fa = 0;
    for (const char* q = g_doc_csp; *q; q++) { if ((q == g_doc_csp || q[-1] == ';' || q[-1] == ' ') && ieqn(q, "frame-ancestors", 15)) { fa = q + 15; break; } }
    if (fa) {
        int ok = 0, any = 0;
        const char* q = fa;
        while (*q && *q != ';') { while (*q == ' ') q++; const char* t = q; while (*q && *q != ' ' && *q != ';') q++; int tl = (int)(q - t); if (!tl) continue; any = 1; if (tl == 6 && ieqn(t, "'none'", 6)) { ok = 0; break; } if (src_match(t, tl, top, furl)) { ok = 1; break; } }
        (void)any;
        if (ok) return 0;
        p = m; p = put(p, "Refused to frame '"); { int k = 0; while (furl[k] && k < 250) *p++ = furl[k++]; } p = put(p, "' because an ancestor violates the following Content Security Policy directive: \"frame-ancestors"); { const char* t = fa; int k = 0; while (*t && *t != ';' && k < 200) { *p++ = *t++; k++; } } p = put(p, "\"."); *p = 0; nwk_error(m);
        g_frame_refuse_why = "Refused by the site (Content-Security-Policy frame-ancestors)";
        return 1;
    }
    if (g_doc_xfo[0]) {
        int deny = ieqn(g_doc_xfo, "deny", 4), same = ieqn(g_doc_xfo, "sameorigin", 10);
        char a[300], b2[300];
        if (deny || (same && !(org_of(furl, a, sizeof a) && org_of(top, b2, sizeof b2) && hc_seq(a, b2)))) {
            p = m; p = put(p, "Refused to display '"); { int k = 0; while (furl[k] && k < 250) *p++ = furl[k++]; } p = put(p, "' in a frame because it set 'X-Frame-Options' to '"); p = put(p, deny ? "deny" : "sameorigin"); p = put(p, "'."); *p = 0; nwk_error(m);
            g_frame_refuse_why = "Refused by the site (X-Frame-Options)";
            return 1;
        }
    }
    return 0;
}
static void frq_zero(void) { for (unsigned k = 0; k < sizeof(g_freq); k++) ((u8*)&g_freq)[k] = 0; }


extern const unsigned char _binary_emoji32_bin_start[], _binary_emoji32_bin_end[];
static int emo_find(int cp) {
    const unsigned char* d = _binary_emoji32_bin_start;
    long sz = _binary_emoji32_bin_end - _binary_emoji32_bin_start;
    if (sz < 12 || d[0] != 'N' || d[1] != 'V' || d[2] != 'E' || d[3] != 'M') return -1;
    unsigned n = (unsigned)d[4] | ((unsigned)d[5] << 8) | ((unsigned)d[6] << 16) | ((unsigned)d[7] << 24);
    if (12 + (long)n * 12 > sz) return -1;
    int lo = 0, hi = (int)n - 1;
    while (lo <= hi) { int m = (lo + hi) / 2; const unsigned char* e = d + 12 + m * 12; int c = (int)((unsigned)e[0] | ((unsigned)e[1] << 8) | ((unsigned)e[2] << 16) | ((unsigned)e[3] << 24)); if (c == cp) return m; if (c < cp) lo = m + 1; else hi = m - 1; }
    return -1;
}
int nwk_emoji_has(int cp) { return emo_find(cp) >= 0; }
int nwk_emoji(int cp, unsigned int** argb, int* w, int* h) {
    int i = emo_find(cp);
    *argb = 0;
    if (i < 0) return -1;
    const unsigned char* d = _binary_emoji32_bin_start;
    long sz = _binary_emoji32_bin_end - _binary_emoji32_bin_start;
    unsigned n = (unsigned)d[4] | ((unsigned)d[5] << 8) | ((unsigned)d[6] << 16) | ((unsigned)d[7] << 24);
    const unsigned char* e = d + 12 + i * 12;
    unsigned off = (unsigned)e[4] | ((unsigned)e[5] << 8) | ((unsigned)e[6] << 16) | ((unsigned)e[7] << 24);
    unsigned len = (unsigned)e[8] | ((unsigned)e[9] << 8) | ((unsigned)e[10] << 16) | ((unsigned)e[11] << 24);
    long base = 12 + (long)n * 12;
    if (base + (long)off + (long)len > sz) return -1;
    return nwk_image(d + base + off, (int)len, argb, w, h);
}

#define CC_ALLOC(n) kmalloc(n)
#define CC_FREE(p) kfree(p)

#define CC_N 96
#define CC_MAX (40ul << 20)
typedef struct { unsigned long long key; int srclen, len; unsigned char* p; unsigned long use; } ccent;
static ccent g_cc[CC_N];
static unsigned long g_cc_total, g_cc_tick;
int nwk_code_get(unsigned long long key, int srclen, const unsigned char** data, int* len) {
    wc_load();
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
            g_wc_dirty = 1;
            return;
        }
        if (lru < 0) return;
        CC_FREE(g_cc[lru].p); g_cc_total -= (unsigned long)g_cc[lru].len; g_cc[lru].p = 0;
    }
}

void* nwk_alloc_arena2(unsigned long n) { if (n > (256ul << 20)) return 0; return nwk_alloc_arena(n); }
void* nwk_alloc_arena3(unsigned long n) { if (n > (128ul << 20)) return 0; return nwk_alloc_arena(n); }
int nwk_battery(int* pct, int* charging, int* mins) { extern int g_acb_n, g_acb_pct, g_acb_charging, g_acb_mins, g_ac_online; if (!g_acb_n || g_acb_pct < 0) { *pct = -1; *charging = 1; *mins = -1; return 0; } *pct = g_acb_pct; *charging = g_acb_charging || g_ac_online == 1; *mins = g_acb_mins; return 1; }
void nwk_frame_slot(int s) { g_fsel = s >= 0 && s < NFS ? s : 0; FSEL(); }
void nwk_frame_slot2(int s) { (void)s; }
void nwk_frame_noop(void) {}
void nwk_frame_noop2(void) {}
int nwk_frame_paint_nested(const char* url, const char* srcdoc, int x, int y, int w, int h, int cx0, int cy0, int cx1, int cy1, unsigned int* fb, int stride) { (void)url; (void)srcdoc; (void)x; (void)y; (void)w; (void)h; (void)cx0; (void)cy0; (void)cx1; (void)cy1; (void)fb; (void)stride; return 0; }
int nwk_frame_is(void) { return 0; }
int nwk_frame_is2(void) { return 1; }
int nwk_frame_post(const char* msg, int len) { FSEL(); return fq_push(&g_q_down, msg, len); }
int nwk_frame_is_popup(void) { return 2; }
int nwk_frame_post_up2(const char* msg, int len) { return fq_push(&g_fs[0].qu, msg, len); }
int nwk_frame_post_up3(const char* msg, int len) { return fq_push(&g_fs[1].qu, msg, len); }
int nwk_frame_post_up4(const char* msg, int len) { return fq_push(&g_fs[2].qu, msg, len); }
int nwk_frame_post_up5(const char* msg, int len) { return fq_push(&g_fs[3].qu, msg, len); }
int nwk_frame_post_up6(const char* msg, int len) { return fq_push(&g_fs[4].qu, msg, len); }
int nwk_frame_recv(char* out, int cap) { FSEL(); return fq_pop(&g_q_up, out, cap); }
int nwk_frame_recv_down2(char* out, int cap) { return fq_pop(&g_fs[0].qd, out, cap); }
int nwk_frame_recv_down3(char* out, int cap) { return fq_pop(&g_fs[1].qd, out, cap); }
int nwk_frame_recv_down4(char* out, int cap) { return fq_pop(&g_fs[2].qd, out, cap); }
int nwk_frame_recv_down5(char* out, int cap) { return fq_pop(&g_fs[3].qd, out, cap); }
int nwk_frame_recv_down6(char* out, int cap) { return fq_pop(&g_fs[4].qd, out, cap); }

void nwk_frame_open2(const char* url) { (void)url; }
void nwk_frame_reset(void);
int nwk_frame_info(void) { FSEL(); return g_frame_state; }
int nwk_frame_info2(void) { return 0; }
static void frame_reset_one(void);
void nwk_frame_switch(void) { FSEL(); frame_reset_one(); }
void nwk_frame_switch2(void) {}
static char g_pend[12288]; static int g_pend_n; static unsigned long g_pend_t;
void nwk_pending_set(const char* s, int len) { if (len > (int)sizeof(g_pend) - 1) len = (int)sizeof(g_pend) - 1; for (int i = 0; i < len; i++) g_pend[i] = s[i]; g_pend[len] = 0; g_pend_n = len; g_pend_t = nwk_ms(); }
void nwk_pending_set2(const char* s, int len) { (void)s; (void)len; }
const char* web_pending_get(unsigned long* t) { if (t) *t = g_pend_t; return g_pend_n ? g_pend : 0; }
void web_pending_clear(void) { g_pend_n = 0; g_pend[0] = 0; }
void nwk_frame_open_doc2(const char* html) { (void)html; }
void nwk_frame_open_doc(const char* html) {
    FSEL();
    if (!html || g_frame_state != 0) return;
    int sl = 0; while (html[sl]) sl++;
    char* c = kmalloc((unsigned long)sl + 1); if (!c) return;
    for (int i = 0; i <= sl; i++) c[i] = html[i];
    if (g_frame_src) kfree(g_frame_src);
    g_frame_src = c; g_frame_srclen = sl;
    const char* ab = "about:srcdoc"; int i = 0; for (; ab[i]; i++) g_frame_url[i] = ab[i]; g_frame_url[i] = 0;
    g_frame_w = 300; g_frame_h = 150;
    g_frame_state = 1;
}
void nwk_frame_sandbox(int flags) { FSEL(); F->sbx = flags; }
void nwk_notify(const char* t, const char* b) { extern void notify(const char*, const char*); char tt[64], bb[160]; int i = 0; for (; t && t[i] && i < 63; i++) tt[i] = t[i]; tt[i] = 0; i = 0; for (; b && b[i] && i < 159; i++) bb[i] = b[i]; bb[i] = 0; notify(tt[0] ? tt : "Web page", bb); }
int g_web_badge;
void nwk_badge(int n) { g_web_badge = n < 0 ? 0 : n > 9999 ? 9999 : n; }
int nwk_mods(void) { return (kdown[0x1D] ? 1 : 0) | (kdown[0x2A] || kdown[0x36] ? 2 : 0) | (kdown[0x38] ? 4 : 0); }
void nwk_frame_open(const char* url) {
    FSEL();
    if (!url || !url[0] || g_frame_state != 0) return;
    int kl = 0; while (url[kl]) kl++;
    if (kl >= (int)sizeof(g_frame_url)) return;
    for (int i = 0; i <= kl; i++) g_frame_url[i] = url[i];
    g_frame_w = 300; g_frame_h = 150;
    g_frame_state = 1;
}
static void frame_reset_one(void) {
    g_frame_state = 0;
    F->sbx = 0;
    g_frame_url[0] = 0;
    if (g_frame_src) { kfree(g_frame_src); g_frame_src = 0; }
    g_frame_srclen = 0;
    g_frame_w = g_frame_h = 0;
    g_frame_vx1 = g_frame_vx0 = 0;
    fq_clear(&g_q_down);
    fq_clear(&g_q_up);
    if (F->pbody) { kfree(F->pbody); F->pbody = 0; } F->plen = 0; F->closereq = 0;
    if (g_kfocus == (int)(F - g_fs)) g_kfocus = -1;
}
void nwk_frame_reset(void) { for (int i = NFS - 1; i >= 0; i--) { F = &g_fs[i]; frame_reset_one(); } g_fsel = 0; F = &g_fs[0]; }

int nwk_frame_status(const char* url, const char* srcdoc, char* out, int cap) {
    const char* key = (url && url[0]) ? url : "about:srcdoc";
    int kl = 0; while (key[kl]) kl++;
    if (out && cap > 0) out[0] = 0;
    for (int sx = 0; sx < NFS_IF; sx++) {
        fslot* S = &g_fs[sx]; if (S->state == 0) continue;
        int i = 0; while (i <= kl && S->url[i] == key[i]) i++; if (i <= kl) continue;
        if (!(url && url[0]) && srcdoc && S->src) { int j = 0; while (srcdoc[j] && S->src[j] == srcdoc[j]) j++; if (srcdoc[j] != S->src[j]) continue; }
        if (out && cap > 0) { int k = 0; for (; S->why[k] && k < cap - 1; k++) out[k] = S->why[k]; out[k] = 0; }
        if (S->why[0] && S->state != 2) return 3;
        return S->state == 4 ? 1 : S->state;
    }
    for (int i = 0; i < 8; i++) { int k = 0; while (k <= kl && k < 511 && g_ffail[i].url[k] == key[k]) k++; if (k > kl && g_ffail[i].why[0]) { if (out && cap > 0) { int j = 0; for (; g_ffail[i].why[j] && j < cap - 1; j++) out[j] = g_ffail[i].why[j]; out[j] = 0; } return 3; } }
    return 0;
}
static int frame_paint_cur(int x, int y, int w, int h, int cx0, int cy0, int cx1, int cy1, unsigned int* fb, int stride);
int nwk_frame_paint(const char* url, const char* srcdoc, int x, int y, int w, int h, int cx0, int cy0, int cx1, int cy1, unsigned int* fb, int stride) {
    if (w < 16 || h < 16 || w > 4096 || h > 4096) return 0;
    const char* key = (url && url[0]) ? url : "about:srcdoc";
    int kl = 0; while (key[kl]) kl++;
    if (kl >= (int)sizeof(g_fs[0].url)) return 0;
    F = &g_fs[0];
    int isdoc = !(url && url[0]) && srcdoc;
    for (int sx = 0; sx < NFS_IF; sx++) { if (g_fs[sx].state == 0) continue; int i = 0; while (i <= kl && g_fs[sx].url[i] == key[i]) i++; if (i > kl) { if (isdoc && g_fs[sx].src) { int j = 0; while (srcdoc[j] && g_fs[sx].src[j] == srcdoc[j]) j++; if (srcdoc[j] != g_fs[sx].src[j]) continue; } F = &g_fs[sx]; break; } }
    if (g_frame_state == 0) {
        for (int i = 0; i <= kl; i++) g_frame_url[i] = key[i];
        if (!(url && url[0]) && srcdoc) {
            int sl = 0; while (srcdoc[sl]) sl++;
            g_frame_src = kmalloc((unsigned long)sl + 1);
            if (!g_frame_src) return 0;
            for (int i = 0; i <= sl; i++) g_frame_src[i] = srcdoc[i];
            g_frame_srclen = sl;
        }
        g_frame_state = 1;
    }
    for (int i = 0; i <= kl; i++) if (g_frame_url[i] != key[i]) return 0;
    return frame_paint_cur(x, y, w, h, cx0, cy0, cx1, cy1, fb, stride);
}
