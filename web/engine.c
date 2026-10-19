#include "web/nwe.h"

nwe_doc G;
static void* jb[5];
static char g_failmsg[160];
static int g_inited;
static int g_dead;

void css_reset_all(void);
void js_reset(void);
int js_ready(void);
int js_has_timers(void);
void nwe_pools_forget(void);
void nwe_arena_reset(void);
extern int g_css_hover;

void nwe_fail(const char* why) {
    snprintf(g_failmsg, sizeof(g_failmsg), "%s", why ? why : "failure");
    __builtin_longjmp(jb, 1);
}

static nimg* imgs;
static node* pend_scripts[512];
static int npend;
static node* deferred[256];
static int ndeferred;
static int g_scroll_evt;
static unsigned long g_last_move;
static node* g_down;
static float g_downx, g_downy;
static node* g_selopen;
static float sel_x, sel_y, sel_w, sel_ih;
static int sel_n;
static float find_x, find_y, find_w, find_h;
static int find_on;

nimg* img_get(const char* url) {
    if (!url || !*url) return 0;
    int sv = pool_suspend();
    for (nimg* i = imgs; i; i = i->next) if (!strcmp(i->url, url)) { pool_resume(sv); return i; }
    nimg* i = nzalloc(sizeof(nimg));
    i->url = nstrdup(url);
    i->next = imgs;
    imgs = i;
    pool_resume(sv);
    return i;
}
int img_pending(void) { int n = 0; for (nimg* i = imgs; i; i = i->next) if (i->state == 0) n++; return n; }

static int b64v(int c) { if (c >= 'A' && c <= 'Z') return c - 'A'; if (c >= 'a' && c <= 'z') return c - 'a' + 26; if (c >= '0' && c <= '9') return c - '0' + 52; if (c == '+' || c == '-') return 62; if (c == '/' || c == '_') return 63; return -1; }
unsigned char* data_url(const char* u, int* len, int* is_text) {
    const char* comma = strchr(u, ',');
    if (!comma) return 0;
    int b64 = 0;
    for (const char* p = u; p < comma; p++) if (!strncmp(p, ";base64", 7)) b64 = 1;
    if (is_text) *is_text = strncmp(u, "data:image/", 11) != 0 || !strncmp(u, "data:image/svg", 14);
    const char* s = comma + 1;
    int n = (int)strlen(s);
    unsigned char* out = nalloc((size_t)n + 1);
    int o = 0;
    if (b64) {
        int acc = 0, bits = 0;
        for (int i = 0; i < n; i++) { int v = b64v((unsigned char)s[i]); if (v < 0) continue; acc = (acc << 6) | v; bits += 6; if (bits >= 8) { bits -= 8; out[o++] = (unsigned char)((acc >> bits) & 255); } }
    } else {
        for (int i = 0; i < n; i++) {
            if (s[i] == '%' && i + 2 < n) { int h = 0; for (int k = 1; k <= 2; k++) { char c = s[i + k]; h = h * 16 + (c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : 0); } out[o++] = (unsigned char)h; i += 2; }
            else out[o++] = (unsigned char)s[i];
        }
    }
    out[o] = 0;
    *len = o;
    return out;
}

static void img_set_pixels(nimg* im, unsigned* argb, int w, int h) {
    int maxw = 1600, maxh = 1600;
    int tw = w, th = h;
    if (tw > maxw) { th = (int)((long)th * maxw / tw); tw = maxw; }
    if (th > maxh) { tw = (int)((long)tw * maxh / th); th = maxh; }
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    unsigned* px = malloc((size_t)tw * (size_t)th * 4);
    if (!px) { im->state = 3; return; }
    if (tw == w && th == h) memcpy(px, argb, (size_t)w * (size_t)h * 4);
    else for (int y = 0; y < th; y++) { int sy = (int)((long)y * h / th); for (int x = 0; x < tw; x++) px[y * tw + x] = argb[(long)sy * w + (int)((long)x * w / tw)]; }
    im->px = px;
    im->w = w > maxw || h > maxh ? tw : w;
    im->h = w > maxw || h > maxh ? th : h;
    if (im->w != tw || im->h != th) { im->w = tw; im->h = th; }
    im->state = 2;
}

void svg_paint(node* n, float x, float y, float w, float h, float op, unsigned* fb, int stride, int cx0, int cy0, int cx1, int cy1);
static void img_from_svg(nimg* im, const char* src, int len) {
    node* f = parse_fragment(0, src, len);
    node* svg = 0;
    node* stack[64];
    int sp = 0;
    for (node* c = f->first; c; c = c->next) if (sp < 64) stack[sp++] = c;
    while (sp > 0 && !svg) { node* c = stack[--sp]; if (c->tag == T_SVG) svg = c; else for (node* d = c->first; d; d = d->next) if (sp < 64) stack[sp++] = d; }
    if (!svg) { im->state = 3; return; }
    float w = 0, h = 0;
    const char* aw = attr_get(svg, "width");
    const char* ah = attr_get(svg, "height");
    const char* vb = attr_get(svg, "viewBox");
    float v[4] = { 0, 0, 0, 0 };
    if (vb) { const char* p = vb; for (int i = 0; i < 4; i++) { while (*p == ' ' || *p == ',') p++; v[i] = parse_float(p, &p); } }
    if (aw && !strchr(aw, '%')) w = (float)atof(aw);
    if (ah && !strchr(ah, '%')) h = (float)atof(ah);
    if (w <= 0 && v[2] > 0) w = v[2];
    if (h <= 0 && v[3] > 0) h = v[3];
    if (w <= 0) w = 150;
    if (h <= 0) h = 150;
    if (w > 1600) { h = h * 1600 / w; w = 1600; }
    if (h > 1600) { w = w * 1600 / h; h = 1600; }
    int iw = (int)ceilf(w), ih = (int)ceilf(h);
    if (iw < 1 || ih < 1) { im->state = 3; return; }
    int ss = 2;
    int bw = iw * ss, bh = ih * ss;
    unsigned* wb = nalloc((size_t)bw * (size_t)bh * 4);
    unsigned* bb = nalloc((size_t)bw * (size_t)bh * 4);
    for (int i = 0; i < bw * bh; i++) { wb[i] = 0xFFFFFF; bb[i] = 0x000000; }
    svg->st = 0;
    svg_paint(svg, 0, 0, (float)bw, (float)bh, 1, wb, bw, 0, 0, bw, bh);
    svg_paint(svg, 0, 0, (float)bw, (float)bh, 1, bb, bw, 0, 0, bw, bh);
    unsigned* px = malloc((size_t)iw * (size_t)ih * 4);
    if (!px) { im->state = 3; return; }
    for (int y = 0; y < ih; y++) for (int x = 0; x < iw; x++) {
        int sr = 0, sg = 0, sb = 0, sa = 0;
        for (int dy = 0; dy < ss; dy++) for (int dx = 0; dx < ss; dx++) {
            unsigned a = wb[(y * ss + dy) * bw + x * ss + dx], b = bb[(y * ss + dy) * bw + x * ss + dx];
            int alpha = 255 - (int)(((a >> 8) & 255) - ((b >> 8) & 255));
            if (alpha < 0) alpha = 0;
            if (alpha > 255) alpha = 255;
            sa += alpha;
            if (alpha > 0) { sr += (int)((b >> 16) & 255) * 255 / alpha; sg += (int)((b >> 8) & 255) * 255 / alpha; sb += (int)(b & 255) * 255 / alpha; }
        }
        int n = ss * ss;
        int r = sr / n, g = sg / n, bl = sb / n, al = sa / n;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (bl > 255) bl = 255;
        px[y * iw + x] = ((unsigned)al << 24) | ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)bl;
    }
    nfree(wb);
    nfree(bb);
    im->px = px;
    im->w = iw;
    im->h = ih;
    im->state = 2;
}

static void fire_img_event(nimg* im) {
    if (!js_ready()) return;
    node* stack[2048];
    int sp = 0;
    if (G.doc) stack[sp++] = G.doc;
    while (sp > 0) {
        node* n = stack[--sp];
        if (n->tag == T_IMG && n->img == im) { int p; js_dispatch(n, im->state == 2 ? "load" : "error", 0, 0, 0, 0, &p); }
        for (node* c = n->first; c; c = c->next) if (c->type == NT_ELEM && sp < 2048) stack[sp++] = c;
    }
}

int img_load_one(void) {
    nimg* im = 0;
    for (nimg* i = imgs; i; i = i->next) if (i->state == 0) im = i;
    if (!im) return 0;
    im->state = 1;
    int sv = pool_suspend();
    unsigned char* data = 0;
    int len = 0;
    int istext = 0;
    char* tofree = 0;
    if (istarts(im->url, "data:")) { data = data_url(im->url, &len, &istext); if (strncmp(im->url, "data:image/svg", 14) == 0) istext = 2; }
    else {
        nwk_resp r;
        memset(&r, 0, sizeof(r));
        char st[200];
        snprintf(st, sizeof(st), "Loading image %.150s", im->url);
        nwk_status(st);
        if (nwk_fetch(im->url, "GET", 0, 0, 0, G.url, &r) >= 0 && r.status >= 200 && r.status < 300 && r.body) {
            data = (unsigned char*)r.body;
            len = r.len;
            tofree = r.body;
            if (strstr(r.ctype, "svg") || (len > 5 && (!memcmp(data, "<svg", 4) || !memcmp(data, "<?xml", 5)))) istext = 2;
        }
    }
    if (data && len > 0) {
        if (istext == 2) img_from_svg(im, (const char*)data, len);
        else {
            unsigned* argb = 0;
            int w = 0, h = 0;
            if (nwk_image(data, len, &argb, &w, &h) == 0 && argb && w > 0 && h > 0) { img_set_pixels(im, argb, w, h); nwk_free(argb); }
            else im->state = 3;
        }
    } else im->state = 3;
    if (tofree) nwk_free(tofree);
    pool_resume(sv);
    fire_img_event(im);
    return 1;
}

static const char* srcset_pick(const char* ss, char* out, int cap) {
    const char* best = 0;
    int bestl = 0;
    float bestv = -1;
    const char* p = ss;
    float want = 700;
    while (*p) {
        while (*p && (is_ws((unsigned char)*p) || *p == ',')) p++;
        if (!*p) break;
        const char* us = p;
        while (*p && !is_ws((unsigned char)*p) && !(*p == ',' && (p[1] == ' ' || p[1] == 0))) p++;
        int ul = (int)(p - us);
        while (*p && is_ws((unsigned char)*p)) p++;
        float v = 1;
        int isw = 0;
        if (*p && *p != ',') { v = parse_float(p, &p); if (*p == 'w') isw = 1; while (*p && *p != ',') p++; }
        float score = isw ? (v >= want ? 10000 - v : v) : (v == 1 ? 20000 : 10000 - v);
        if (score > bestv) { bestv = score; best = us; bestl = ul; }
    }
    if (!best) return 0;
    if (bestl >= cap) bestl = cap - 1;
    memcpy(out, best, (size_t)bestl);
    out[bestl] = 0;
    return out;
}

static int placeholder_src(const char* s) { return !s || !*s || istarts(s, "data:image/gif") || (istarts(s, "data:image/svg") && strlen(s) < 400) || strstr(s, "placeholder") || strstr(s, "blank.gif") || strstr(s, "spacer.gif") || strstr(s, "pixel.gif"); }

static const char* choose_src(node* n, char* buf, int cap) {
    if (n->parent && n->parent->tag == T_PICTURE) {
        for (node* s = n->parent->first; s && s != n; s = s->next) {
            if (s->tag != T_SOURCE) continue;
            const char* ty = attr_get(s, "type");
            if (ty && !strstr(ty, "jpeg") && !strstr(ty, "jpg") && !strstr(ty, "png") && !strstr(ty, "webp") && !strstr(ty, "gif")) continue;
            const char* m = attr_get(s, "media");
            if (m && !css_media_match(m, (int)strlen(m))) continue;
            const char* ss = attr_get(s, "srcset");
            if (!ss) ss = attr_get(s, "data-srcset");
            if (ss && srcset_pick(ss, buf, cap)) return buf;
        }
    }
    const char* src = attr_get(n, "src");
    if (placeholder_src(src)) {
        static const char* alts[] = { "data-src", "data-lazy-src", "data-original", "data-lazy", "data-url", "data-hi-res-src", "data-full-src", 0 };
        for (int i = 0; alts[i]; i++) { const char* v = attr_get(n, alts[i]); if (v && *v) return v; }
        const char* dss = attr_get(n, "data-srcset");
        if (dss && srcset_pick(dss, buf, cap)) return buf;
    }
    const char* ss = attr_get(n, "srcset");
    if (ss && *ss && (placeholder_src(src) || 1)) { if (srcset_pick(ss, buf, cap)) { if (!src || placeholder_src(src) || strstr(buf, "http") || 1) return buf; } }
    return src;
}

static void request_img(node* n) {
    char buf[1024];
    const char* s = choose_src(n, buf, sizeof(buf));
    if (!s || !*s) { n->img = 0; return; }
    char full[2048];
    url_resolve(G.base, s, full, sizeof(full));
    n->img = img_get(full);
}

static void scan_images(node* root) {
    node* stack[4096];
    int sp = 0;
    stack[sp++] = root;
    while (sp > 0) {
        node* n = stack[--sp];
        if (n->type == NT_ELEM) {
            if (n->tag == T_IMG) request_img(n);
            else if (n->tag == T_INPUT && attr_get(n, "type") && ieq(attr_get(n, "type"), "image")) request_img(n);
        }
        for (node* c = n->first; c; c = c->next) if (c->type == NT_ELEM && sp < 4096) stack[sp++] = c;
    }
}

char* eng_fetch_text(const char* url, int* len) {
    *len = 0;
    int sv = pool_suspend();
    if (istarts(url, "data:")) { int l; int t; char* d = (char*)data_url(url, &l, &t); *len = d ? l : 0; pool_resume(sv); return d; }
    nwk_resp r;
    memset(&r, 0, sizeof(r));
    char st[200];
    snprintf(st, sizeof(st), "Loading %.170s", url);
    nwk_status(st);
    if (nwk_fetch(url, "GET", 0, 0, 0, G.url, &r) < 0 || !r.body || r.status < 200 || r.status >= 400) { if (r.body) nwk_free(r.body); pool_resume(sv); return 0; }
    char* t = nalloc((size_t)r.len + 1);
    memcpy(t, r.body, (size_t)r.len);
    t[r.len] = 0;
    *len = r.len;
    nwk_free(r.body);
    pool_resume(sv);
    return t;
}

static int script_kind(node* s) {
    const char* ty = attr_get(s, "type");
    if (attr_get(s, "nomodule")) return 0;
    if (!ty || !*ty) return 1;
    if (ieq(ty, "module")) return 2;
    if (strstr(ty, "javascript") || strstr(ty, "ecmascript") || ieq(ty, "text/jsx") || ieq(ty, "application/x-javascript")) return 1;
    return 0;
}

static void run_script_node(node* s) {
    if (s->flags & NF_SCRIPTDONE) return;
    s->flags |= NF_SCRIPTDONE;
    int k = script_kind(s);
    if (!k) return;
    const char* src = attr_get(s, "src");
    node* save = G.cur_script;
    G.cur_script = s;
    int ok = 1;
    if (src && *src) {
        char full[2048];
        url_resolve(G.base, src, full, sizeof(full));
        int len = 0;
        char* t = eng_fetch_text(full, &len);
        if (t) js_run_script(t, len, full, k == 2);
        else ok = 0;
        G.cur_script = save;
        int p;
        js_dispatch(s, ok ? "load" : "error", 0, 0, 0, 0, &p);
    } else {
        sbuf b = { 0, 0, 0 };
        text_content(s, &b);
        if (b.n) js_run_script(b.s, b.n, G.url, k == 2);
        G.cur_script = save;
    }
}

void eng_script_parsed(node* s) {
    int k = script_kind(s);
    if (!k) { s->flags |= NF_SCRIPTDONE; return; }
    const char* src = attr_get(s, "src");
    if (k == 2 || (src && (attr_get(s, "defer") || attr_get(s, "async")))) { if (ndeferred < 256) deferred[ndeferred++] = s; return; }
    run_script_node(s);
}

static void subtree(node* n, void (*fn)(node*)) {
    node* stack[2048];
    int sp = 0;
    stack[sp++] = n;
    while (sp > 0) {
        node* c = stack[--sp];
        fn(c);
        for (node* d = c->last; d; d = d->prev) if (d->type == NT_ELEM && sp < 2048) stack[sp++] = d;
    }
}
static void on_ins(node* c) {
    if (c->type != NT_ELEM) return;
    if (c->tag == T_SCRIPT && !(c->flags & NF_SCRIPTDONE) && !G.parsing) { if (npend < 512) pend_scripts[npend++] = c; }
    else if (c->tag == T_STYLE || c->tag == T_LINK) css_bump();
    else if (c->tag == T_IMG || (c->tag == T_INPUT && attr_get(c, "type") && ieq(attr_get(c, "type"), "image"))) request_img(c);
    else if (c->tag == T_SOURCE && c->parent && c->parent->tag == T_PICTURE) { for (node* q = c->parent->first; q; q = q->next) if (q->tag == T_IMG) request_img(q); }
}
static int in_composed_doc(node* n) {
    for (int guard = 0; n && guard < 100000; guard++) {
        if (n == G.doc) return 1;
        if (!n->parent && n->host) n = n->host;
        else n = n->parent;
    }
    return 0;
}
void eng_node_inserted(node* n) {
    if (!n || !G.doc || !in_composed_doc(n)) return;
    subtree(n, on_ins);
    G.dirty_style = 1;
}
static void on_rem(node* c) { if (c->tag == T_STYLE || c->tag == T_LINK) css_bump(); }
void eng_node_removed(node* n) {
    if (G.focus && node_contains(n, G.focus)) G.focus = 0;
    if (G.hover && node_contains(n, G.hover)) G.hover = 0;
    if (G.active && node_contains(n, G.active)) G.active = 0;
    if (g_selopen && node_contains(n, g_selopen)) g_selopen = 0;
    subtree(n, on_rem);
    G.dirty_style = 1;
}

void eng_invalidate(int what) { if (what & 1) G.dirty_style = 1; if (what & 2) G.dirty_layout = 1; G.dirty_paint = 1; }

static void apply_scroll_offsets(node* n) {
    if (n->box && n->scrolly > 0) {
        box* b = n->box;
        float inner = b->h - b->b[0] - b->b[2];
        float total = b->contenth + b->p[0] + b->p[2];
        float mx = total - inner;
        if (mx < 0) mx = 0;
        if (n->scrolly > mx) n->scrolly = mx;
        b->sy = n->scrolly;
    }
    for (node* c = n->first; c; c = c->next) if (c->type == NT_ELEM) apply_scroll_offsets(c);
}

void eng_layout_if_dirty(void) {
    if (!G.doc) return;
    if (G.dirty_style) {
        style_all(G.doc, G.vw, G.vh);
        G.dirty_style = 0;
        G.dirty_layout = 1;
    }
    if (G.dirty_layout) {
        layout_all(G.vw, G.vh);
        G.dirty_layout = 0;
        apply_scroll_offsets(G.doc);
        float mx = g_doch - G.vh;
        if (mx < 0) mx = 0;
        if (G.scroll > mx) G.scroll = mx;
        G.dirty_paint = 1;
    }
}

void eng_scroll_to(float y) {
    eng_layout_if_dirty();
    float mx = g_doch - G.vh;
    if (mx < 0) mx = 0;
    if (y > mx) y = mx;
    if (y < 0) y = 0;
    if (y != G.scroll) { G.scroll = y; g_scroll_evt = 1; G.dirty_paint = 1; }
}

static int same_doc(const char* a, const char* b) {
    int la = (int)(strchr(a, '#') ? strchr(a, '#') - a : (long)strlen(a));
    int lb = (int)(strchr(b, '#') ? strchr(b, '#') - b : (long)strlen(b));
    return la == lb && !strncmp(a, b, (size_t)la);
}
static void scroll_to_anchor(const char* frag) {
    if (!frag || !*frag) { eng_scroll_to(0); return; }
    char dec[256];
    int o = 0;
    for (int i = 0; frag[i] && o < 255; i++) {
        if (frag[i] == '%' && frag[i + 1] && frag[i + 2]) { char h[3] = { frag[i + 1], frag[i + 2], 0 }; dec[o++] = (char)strtol(h, 0, 16); i += 2; }
        else dec[o++] = frag[i];
    }
    dec[o] = 0;
    node* t = find_id(G.doc, dec);
    if (!t) {
        node* stack[4096];
        int sp = 0;
        stack[sp++] = G.doc;
        while (sp > 0 && !t) { node* n = stack[--sp]; if (n->tag == T_A && attr_get(n, "name") && !strcmp(attr_get(n, "name"), dec)) t = n; for (node* c = n->first; c; c = c->next) if (c->type == NT_ELEM && sp < 4096) stack[sp++] = c; }
    }
    if (!t) return;
    float x, y, w, h;
    eng_layout_if_dirty();
    box_rect(t, &x, &y, &w, &h);
    eng_scroll_to(y);
}

void eng_navigate(const char* url, const char* method, const char* body, int blen) {
    if (istarts(url, "javascript:")) {
        sbuf b = { 0, 0, 0 };
        const char* p = url + 11;
        while (*p) { if (*p == '%' && p[1] && p[2]) { char h[3] = { p[1], p[2], 0 }; sb_putc(&b, (int)strtol(h, 0, 16)); p += 3; } else sb_putc(&b, *p++); }
        if (b.n) js_run_script(b.s, b.n, "javascript:", 0);
        return;
    }
    if (!strcmp(method, "GET") && strchr(url, '#') && same_doc(url, G.url)) {
        snprintf(G.url, sizeof(G.url), "%s", url);
        scroll_to_anchor(strchr(url, '#') + 1);
        int p;
        if (js_ready()) js_dispatch(G.doc, "hashchange", 0, 0, 0, 0, &p);
        return;
    }
    if (G.nav_pending) return;
    G.nav_pending = 1;
    snprintf(G.nav, sizeof(G.nav), "%s", url);
    snprintf(G.navmethod, sizeof(G.navmethod), "%s", method);
    G.navbody = 0;
    G.navblen = 0;
    if (body && blen > 0) { int sv = pool_suspend(); G.navbody = nalloc((size_t)blen + 1); memcpy(G.navbody, body, (size_t)blen); G.navbody[blen] = 0; G.navblen = blen; pool_resume(sv); }
}

static void urlenc(sbuf* b, const char* s) {
    static const char* hx = "0123456789ABCDEF";
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '*') sb_putc(b, c);
        else if (c == ' ') sb_putc(b, '+');
        else if (c == '\n') sb_puts(b, "%0D%0A");
        else { sb_putc(b, '%'); sb_putc(b, hx[c >> 4]); sb_putc(b, hx[c & 15]); }
    }
}
static const char* ctl_value(node* n, sbuf* tmp) {
    if (n->tag == T_SELECT) {
        int idx = 0, want = n->selidx;
        node* first = 0;
        node* sel = 0;
        node* stack[256];
        int sp = 0;
        for (node* c = n->last; c; c = c->prev) if (sp < 256) stack[sp++] = c;
        while (sp > 0) { node* o = stack[--sp]; if (o->tag == T_OPTGROUP) { for (node* q = o->last; q; q = q->prev) if (sp < 256) stack[sp++] = q; continue; } if (o->tag != T_OPTION) continue; if (!first) first = o; if (want >= 0 ? idx == want : (attr_get(o, "selected") != 0 && !sel)) sel = o; idx++; }
        if (!sel && want == -1) sel = first;
        if (!sel) return 0;
        const char* v = attr_get(sel, "value");
        if (v) return v;
        text_content(sel, tmp);
        return tmp->s ? tmp->s : "";
    }
    if (n->value) return n->value;
    if (n->tag == T_TEXTAREA) { text_content(n, tmp); return tmp->s ? tmp->s : ""; }
    const char* v = attr_get(n, "value");
    return v ? v : "";
}

void eng_form_submit(node* form, node* submitter) {
    sbuf b = { 0, 0, 0 };
    node* stack[2048];
    int sp = 0;
    for (node* c = form->last; c; c = c->prev) if (sp < 2048) stack[sp++] = c;
    int first = 1;
    while (sp > 0) {
        node* n = stack[--sp];
        if (n->type != NT_ELEM) continue;
        for (node* d = n->last; d; d = d->prev) if (sp < 2048) stack[sp++] = d;
        if (n->tag != T_INPUT && n->tag != T_SELECT && n->tag != T_TEXTAREA && n->tag != T_BUTTON) continue;
        const char* nm = attr_get(n, "name");
        if (!nm || !*nm || attr_get(n, "disabled")) continue;
        const char* ty = attr_get(n, "type");
        if (n->tag == T_INPUT && ty) {
            if ((ieq(ty, "checkbox") || ieq(ty, "radio")) && !n->checked) continue;
            if ((ieq(ty, "submit") || ieq(ty, "image") || ieq(ty, "button") || ieq(ty, "reset")) && n != submitter) continue;
            if (ieq(ty, "file")) continue;
        }
        if (n->tag == T_BUTTON && n != submitter) continue;
        sbuf tmp = { 0, 0, 0 };
        const char* v = ctl_value(n, &tmp);
        if (!v) continue;
        if (n->tag == T_INPUT && ty && (ieq(ty, "checkbox") || ieq(ty, "radio")) && !attr_get(n, "value")) v = "on";
        if (!first) sb_putc(&b, '&');
        first = 0;
        urlenc(&b, nm);
        sb_putc(&b, '=');
        urlenc(&b, v);
    }
    const char* action = submitter && attr_get(submitter, "formaction") ? attr_get(submitter, "formaction") : attr_get(form, "action");
    const char* method = submitter && attr_get(submitter, "formmethod") ? attr_get(submitter, "formmethod") : attr_get(form, "method");
    char full[2048];
    url_resolve(G.base, action && *action ? action : G.url, full, sizeof(full));
    if (method && ieq(method, "post")) { eng_navigate(full, "POST", b.s ? b.s : "", b.n); return; }
    char* q = strchr(full, '?');
    if (q) *q = 0;
    char* h = strchr(full, '#');
    if (h) *h = 0;
    char nu[2048];
    snprintf(nu, sizeof(nu), "%s?%s", full, b.s ? b.s : "");
    eng_navigate(nu, "GET", 0, 0);
}

static int is_text_input(node* n) {
    if (!n) return 0;
    if (n->tag == T_TEXTAREA) return 1;
    if (n->tag != T_INPUT) return 0;
    const char* t = attr_get(n, "type");
    if (!t) return 1;
    return ieq(t, "text") || ieq(t, "password") || ieq(t, "email") || ieq(t, "search") || ieq(t, "url") || ieq(t, "tel") || ieq(t, "number") || ieq(t, "date") || ieq(t, "time") || ieq(t, "datetime-local") || ieq(t, "month") || ieq(t, "week") || !*t;
}
node* g_ce_node;
int g_ce_off;
node* edit_host(node* n) {
    for (node* q = n; q && q->type == NT_ELEM; q = q->parent) {
        const char* v = attr_get(q, "contenteditable");
        if (v) { if (ieq(v, "false")) return 0; return q; }
    }
    return 0;
}
static int is_edit_host(node* n) { return n && n->type == NT_ELEM && edit_host(n) == n && attr_get(n, "contenteditable"); }
static void ce_place(node* host, float x, float y) {
    eng_layout_if_dirty();
    frag* f = 0;
    hit_test(x, y + G.scroll, &f);
    if (f && f->kind == FK_TEXT && f->b && f->b->n && f->b->n->type == NT_TEXT && node_contains(host, f->b->n)) {
        node* tn = f->b->n;
        if (tn->text && f->s >= tn->text && f->s <= tn->text + tn->tlen) {
            float rel = x - f->x;
            int i = 0, best = f->n;
            float acc = 0;
            while (i < f->n) { int cp; int k = u8dec(f->s + i, f->n - i, &cp); if (k <= 0) break; float aw = font_adv(f->st->face, f->st->fsize, cp) + f->st->lspace; if (acc + aw / 2 > rel) { best = i; break; } acc += aw; i += k; }
            js_ce_call("__ceSet", tn, (int)(f->s - tn->text) + best);
            return;
        }
    }
    js_ce_call("__ceSet", host, -1);
}
static int focusable(node* n) {
    if (!n || n->type != NT_ELEM) return 0;
    if (attr_get(n, "disabled")) return 0;
    if (n->tag == T_INPUT) { const char* t = attr_get(n, "type"); return !(t && ieq(t, "hidden")); }
    if (n->tag == T_TEXTAREA || n->tag == T_SELECT || n->tag == T_BUTTON) return 1;
    if (is_edit_host(n)) return 1;
    if (n->tag == T_A && attr_get(n, "href")) return 1;
    const char* ti = attr_get(n, "tabindex");
    if (ti && atoi(ti) >= 0) return 1;
    return 0;
}

static void set_focus(node* n) {
    if (G.focus == n) return;
    node* old = G.focus;
    G.focus = n;
    int p;
    if (old) { js_dispatch(old, "blur", 0, 0, 0, 0, &p); js_dispatch(old, "focusout", 1, 0, 0, 0, &p); if (old->dirtyval && is_text_input(old)) js_dispatch(old, "change", 1, 0, 0, 0, &p); }
    if (n && is_edit_host(n) && (!g_ce_node || !(g_ce_node == n || node_contains(n, g_ce_node)))) { G.focus = n; js_ce_call("__ceSet", n, 0); }
    if (n) {
        if (is_text_input(n)) {
            if (!n->value) {
                sbuf t = { 0, 0, 0 };
                const char* v = ctl_value(n, &t);
                int l = v ? (int)strlen(v) : 0;
                n->value = nalloc((size_t)l + 1);
                if (l) memcpy(n->value, v, (size_t)l);
                n->value[l] = 0;
                n->vlen = l;
                n->vcap = l + 1;
            }
            n->caret = n->vlen;
        }
        js_dispatch(n, "focus", 0, 0, 0, 0, &p);
        js_dispatch(n, "focusin", 1, 0, 0, 0, &p);
    }
    G.dirty_style = 1;
}

static node* elem_of(box* b, frag* f) {
    node* n = b ? b->n : 0;
    (void)f;
    while (n && n->type != NT_ELEM) n = n->parent;
    return n;
}

static node* hit(float x, float y) {
    eng_layout_if_dirty();
    frag* f = 0;
    box* b = hit_test(x, y + G.scroll, &f);
    node* n = elem_of(b, f);
    while (n && n->st && n->st->pevents) n = n->parent;
    return n;
}

static void toggle_check(node* n) {
    const char* ty = attr_get(n, "type");
    int radio = ty && ieq(ty, "radio");
    if (radio) {
        if (n->checked) return;
        const char* nm = attr_get(n, "name");
        if (nm) {
            node* root = n->parent;
            while (root && root->tag != T_FORM && root->parent) root = root->parent;
            node* stack[2048];
            int sp = 0;
            stack[sp++] = root ? root : G.doc;
            while (sp > 0) { node* q = stack[--sp]; if (q->tag == T_INPUT && q != n && attr_get(q, "name") && !strcmp(attr_get(q, "name"), nm)) { q->checked = 0; q->flags |= NF_INLINEDIRTY; } for (node* d = q->first; d; d = d->next) if (d->type == NT_ELEM && sp < 2048) stack[sp++] = d; }
        }
        n->checked = 1;
    } else n->checked = !n->checked;
    n->flags |= NF_INLINEDIRTY;
    int p;
    js_dispatch(n, "input", 1, 0, 0, 0, &p);
    js_dispatch(n, "change", 1, 0, 0, 0, &p);
    G.dirty_style = 1;
}

static node* form_of_node(node* n) { for (node* p = n; p; p = p->parent) if (p->tag == T_FORM) return p; const char* f = attr_get(n, "form"); return f ? find_id(G.doc, f) : 0; }

static void submit_via(node* form, node* submitter) {
    int prevented = 0;
    js_dispatch(form, "submit", 1, 0, 0, 0, &prevented);
    if (!prevented) eng_form_submit(form, submitter);
}

static void default_action(node* t) {
    for (node* n = t; n && n->type == NT_ELEM; n = n->parent) {
        if ((n->tag == T_A || n->tag == T_AREA) && attr_get(n, "href")) {
            const char* h = attr_get(n, "href");
            char full[2048];
            url_resolve(G.base, h, full, sizeof(full));
            eng_navigate(full, "GET", 0, 0);
            return;
        }
        if (n->tag == T_INPUT) {
            const char* ty = attr_get(n, "type");
            if (attr_get(n, "disabled")) return;
            if (ty && (ieq(ty, "checkbox") || ieq(ty, "radio"))) { toggle_check(n); return; }
            if (ty && (ieq(ty, "submit") || ieq(ty, "image"))) { node* f = form_of_node(n); if (f) submit_via(f, n); return; }
            if (ty && ieq(ty, "reset")) return;
            return;
        }
        if (n->tag == T_BUTTON) {
            if (attr_get(n, "disabled")) return;
            const char* ty = attr_get(n, "type");
            if (!ty || ieq(ty, "submit")) { node* f = form_of_node(n); if (f) submit_via(f, n); }
            return;
        }
        if (n->tag == T_LABEL) {
            node* c = 0;
            const char* f = attr_get(n, "for");
            if (f) c = find_id(G.doc, f);
            if (!c) { node* stack[256]; int sp = 0; for (node* d = n->first; d; d = d->next) if (sp < 256) stack[sp++] = d; while (sp > 0 && !c) { node* q = stack[--sp]; if (q->tag == T_INPUT || q->tag == T_SELECT || q->tag == T_TEXTAREA || q->tag == T_BUTTON) c = q; for (node* d = q->first; d; d = d->next) if (sp < 256) stack[sp++] = d; } }
            if (c && c != t && !node_contains(c, t)) {
                const char* ty = attr_get(c, "type");
                if (c->tag == T_INPUT && ty && (ieq(ty, "checkbox") || ieq(ty, "radio"))) { int p; js_dispatch(c, "click", 1, 0, 0, 0, &p); if (!p) toggle_check(c); }
                else set_focus(c);
            }
            return;
        }
        if (n->tag == T_SUMMARY && n->parent && n->parent->tag == T_DETAILS) {
            node* d = n->parent;
            if (attr_get(d, "open")) attr_del(d, "open"); else attr_set(d, "open", "");
            css_bump();
            G.dirty_style = 1;
            int p;
            js_dispatch(d, "toggle", 0, 0, 0, 0, &p);
            return;
        }
        if (n->tag == T_SELECT) {
            if (attr_get(n, "disabled")) return;
            g_selopen = g_selopen == n ? 0 : n;
            G.dirty_paint = 1;
            return;
        }
    }
}

void eng_synthetic_click(node* n) {
    int p = 0;
    js_dispatch(n, "click", 1, 0, 0, 0, &p);
    if (!p) default_action(n);
}

static void text_insert(node* n, const char* s, int len) {
    const char* ml = attr_get(n, "maxlength");
    if (ml && atoi(ml) >= 0 && n->vlen + len > atoi(ml)) return;
    if (n->vlen + len + 1 > n->vcap) {
        int nc = (n->vlen + len + 1) * 2;
        char* nv = nalloc((size_t)nc);
        if (n->value) memcpy(nv, n->value, (size_t)n->vlen);
        n->value = nv;
        n->vcap = nc;
    }
    if (n->caret > n->vlen) n->caret = n->vlen;
    memmove(n->value + n->caret + len, n->value + n->caret, (size_t)(n->vlen - n->caret));
    memcpy(n->value + n->caret, s, (size_t)len);
    n->vlen += len;
    n->caret += len;
    n->value[n->vlen] = 0;
    n->dirtyval = 1;
}
static int u8prev(const char* s, int i) { if (i <= 0) return 0; i--; while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--; return i; }
static int u8next(const char* s, int n, int i) { if (i >= n) return n; i++; while (i < n && ((unsigned char)s[i] & 0xC0) == 0x80) i++; return i; }

static void focus_next(int back) {
    node* list[4096];
    int cnt = 0;
    node* stack[4096];
    int sp = 0;
    stack[sp++] = G.doc;
    while (sp > 0) { node* q = stack[--sp]; if (focusable(q) && q->st && q->st->display != D_NONE && cnt < 4096) list[cnt++] = q; for (node* d = q->last; d; d = d->prev) if (d->type == NT_ELEM && sp < 4096) stack[sp++] = d; }
    if (!cnt) return;
    int cur = -1;
    for (int i = 0; i < cnt; i++) if (list[i] == G.focus) cur = i;
    int nx = back ? (cur <= 0 ? cnt - 1 : cur - 1) : (cur + 1) % cnt;
    set_focus(list[nx]);
    float x, y, w, h;
    eng_layout_if_dirty();
    box_rect(list[nx], &x, &y, &w, &h);
    if (y < G.scroll || y + h > G.scroll + G.vh) eng_scroll_to(y - G.vh / 3);
}

static int key_event(int k) {
    if (k == 13) k = '\n';
    node* tgt = G.focus ? G.focus : doc_body();
    if (!tgt) return 0;
    int prevented = 0;
    js_dispatch(tgt, "keydown", 1, 0, 0, k, &prevented);
    if (prevented) return 1;
    if (k >= 32 && k != 127 && k < 0x110000) { js_dispatch(tgt, "keypress", 1, 0, 0, k, &prevented); if (prevented) return 1; }
    node* n = G.focus;
    if (n && is_edit_host(n) && !g_selopen) {
        if (k == '\t') { focus_next(0); return 1; }
        if (k == 27) { set_focus(0); return 1; }
        js_ce_call("__ceKey", n, k);
        G.dirty_style = 1;
        G.dirty_paint = 1;
        return 1;
    }
    if (g_selopen) {
        node* s = g_selopen;
        int cnt = 0;
        for (node* o = s->first; o; o = o->next) if (o->tag == T_OPTION) cnt++;
        int cur = s->selidx >= 0 ? s->selidx : 0;
        if (k == NWE_K_UP && cur > 0) cur--;
        else if (k == NWE_K_DOWN && cur < cnt - 1) cur++;
        else if (k == '\n' || k == 27 || k == ' ') { g_selopen = 0; int p; js_dispatch(s, "input", 1, 0, 0, 0, &p); js_dispatch(s, "change", 1, 0, 0, 0, &p); G.dirty_paint = 1; return 1; }
        s->selidx = cur;
        G.dirty_paint = 1;
        return 1;
    }
    if (n && is_text_input(n) && !attr_get(n, "readonly") && !attr_get(n, "disabled")) {
        int ta = n->tag == T_TEXTAREA;
        int changed = 0;
        if (k == '\b' || k == 127 || (k >= 32 && k < 0x110000 && k != 127) || (k == '\n' && ta)) { int bp = 0; js_dispatch(n, "beforeinput", 1, 0, 0, k, &bp); if (bp) { G.dirty_paint = 1; return 1; } }
        if (k == '\n') {
            if (ta) { text_insert(n, "\n", 1); changed = 1; }
            else {
                node* f = form_of_node(n);
                if (f) {
                    node* sub = 0;
                    node* stack[2048];
                    int sp = 0;
                    stack[sp++] = f;
                    while (sp > 0 && !sub) { node* q = stack[--sp]; if ((q->tag == T_BUTTON && (!attr_get(q, "type") || ieq(attr_get(q, "type"), "submit"))) || (q->tag == T_INPUT && attr_get(q, "type") && (ieq(attr_get(q, "type"), "submit") || ieq(attr_get(q, "type"), "image")))) sub = q; for (node* d = q->last; d; d = d->prev) if (d->type == NT_ELEM && sp < 2048) stack[sp++] = d; }
                    if (sub) { int p; js_dispatch(sub, "click", 1, 0, 0, 0, &p); if (!p) default_action(sub); }
                    else submit_via(f, 0);
                }
                return 1;
            }
        } else if (k == '\b') {
            if (n->caret > 0 && n->value) { int p = u8prev(n->value, n->caret); memmove(n->value + p, n->value + n->caret, (size_t)(n->vlen - n->caret + 1)); n->vlen -= n->caret - p; n->caret = p; changed = 1; n->dirtyval = 1; }
        } else if (k == 127) {
            if (n->value && n->caret < n->vlen) { int q = u8next(n->value, n->vlen, n->caret); memmove(n->value + n->caret, n->value + q, (size_t)(n->vlen - q + 1)); n->vlen -= q - n->caret; changed = 1; n->dirtyval = 1; }
        } else if (k == NWE_K_LEFT) { if (n->value) n->caret = u8prev(n->value, n->caret); }
        else if (k == NWE_K_RIGHT) { if (n->value) n->caret = u8next(n->value, n->vlen, n->caret); }
        else if (k == NWE_K_HOME || (k == NWE_K_UP && !ta)) n->caret = 0;
        else if (k == NWE_K_END || (k == NWE_K_DOWN && !ta)) n->caret = n->vlen;
        else if (k == '\t') { focus_next(0); return 1; }
        else if (k == 27) { set_focus(0); return 1; }
        else if (k >= 32 && k < 0x110000) {
            sbuf b = { 0, 0, 0 };
            sb_putu8(&b, k);
            text_insert(n, b.s, b.n);
            changed = 1;
        }
        if (changed) { int p; js_dispatch(n, "input", 1, 0, 0, k, &p); }
        G.dirty_style = 1;
        G.dirty_paint = 1;
        return 1;
    }
    if (k == '\t') { focus_next(0); return 1; }
    if (k == 27) { if (G.focus) set_focus(0); find_on = 0; return 1; }
    if (k == '\n' && G.focus) { eng_synthetic_click(G.focus); return 1; }
    if (k == ' ' && G.focus && (G.focus->tag == T_BUTTON || G.focus->tag == T_INPUT)) { eng_synthetic_click(G.focus); return 1; }
    float line = 48;
    switch (k) {
    case NWE_K_UP: eng_scroll_to(G.scroll - line); return 1;
    case NWE_K_DOWN: eng_scroll_to(G.scroll + line); return 1;
    case NWE_K_PGUP: eng_scroll_to(G.scroll - G.vh * 0.9f); return 1;
    case NWE_K_PGDN: case ' ': eng_scroll_to(G.scroll + G.vh * 0.9f); return 1;
    case NWE_K_HOME: eng_scroll_to(0); return 1;
    case NWE_K_END: eng_scroll_to(1e9f); return 1;
    }
    return 0;
}

static box* scroll_box_at(float x, float y) {
    eng_layout_if_dirty();
    frag* f;
    box* b = hit_test(x, y + G.scroll, &f);
    for (; b; b = b->parent) {
        if (!b->st || !b->parent) continue;
        if (b->st->ovy == OV_AUTO || b->st->ovy == OV_SCROLL) {
            float inner = b->h - b->b[0] - b->b[2];
            float total = b->contenth + b->p[0] + b->p[2];
            if (total > inner + 1) return b;
        }
    }
    return 0;
}

static int do_wheel(float x, float y, int dy) {
    box* b = scroll_box_at(x, y);
    float d = (float)dy;
    if (b) {
        float inner = b->h - b->b[0] - b->b[2];
        float total = b->contenth + b->p[0] + b->p[2];
        float ns = b->sy + d;
        if (ns < 0) ns = 0;
        if (ns > total - inner) ns = total - inner;
        if (ns != b->sy) { b->sy = ns; if (b->n) b->n->scrolly = ns; G.dirty_paint = 1; return 1; }
    }
    eng_scroll_to(G.scroll + d);
    return 1;
}

static void paint_select_overlay(unsigned* fb, int stride, int fbw, int fbh, int vx, int vy, int vw, int vh) {
    if (!g_selopen || !g_selopen->box) return;
    box* b = g_selopen->box;
    style* s = g_selopen->st;
    node* opts[256];
    int n = 0;
    for (node* o = g_selopen->first; o && n < 256; o = o->next) {
        if (o->tag == T_OPTION) opts[n++] = o;
        else if (o->tag == T_OPTGROUP) for (node* q = o->first; q && n < 256; q = q->next) if (q->tag == T_OPTION) opts[n++] = q;
    }
    sel_n = n;
    float ih = s->fsize * 1.5f + 4;
    float x = b->x + vx, y = b->y + b->h + vy - G.scroll;
    if (b->st->position == P_FIXED) y += G.scroll;
    float w = b->w;
    float maxw = w;
    for (int i = 0; i < n; i++) { sbuf t = { 0, 0, 0 }; text_content(opts[i], &t); float tw = text_width(s->face, s->fsize, t.s ? t.s : "", t.n, 0) + 20; if (tw > maxw) maxw = tw; }
    w = maxw;
    float h = ih * n;
    if (y + h > vy + vh && y - b->h - h > vy) y = b->y + vy - G.scroll - h;
    sel_x = x - vx; sel_y = y - vy; sel_w = w; sel_ih = ih;
    int cx1 = vx + vw < fbw ? vx + vw : fbw, cy1 = vy + vh < fbh ? vy + vh : fbh;
    fill_rect_a(fb, stride, vx, vy, cx1, cy1, x + 2, y + 3, w, h, 0x40000000);
    fill_rect_a(fb, stride, vx, vy, cx1, cy1, x, y, w, h, 0xFF767676);
    fill_rect_a(fb, stride, vx, vy, cx1, cy1, x + 1, y + 1, w - 2, h - 2, 0xFFFFFFFF);
    int cur = g_selopen->selidx;
    for (int i = 0; i < n; i++) {
        if (i == cur || (cur < 0 && attr_get(opts[i], "selected"))) fill_rect_a(fb, stride, vx, vy, cx1, cy1, x + 1, y + 1 + i * ih, w - 2, ih, 0xFF1E6FD9);
        sbuf t = { 0, 0, 0 };
        text_content(opts[i], &t);
        fontm m;
        font_metrics(s->face, s->fsize, &m);
        unsigned col = (i == cur) ? 0xFFFFFFFF : (attr_get(opts[i], "disabled") ? 0xFF999999 : 0xFF000000);
        font_draw(fb, stride, vx, vy, cx1, cy1, s->face, s->fsize, x + 8, y + i * ih + (ih - (m.ascent + m.descent)) / 2 + m.ascent, t.s ? t.s : "", t.n, col, 0);
    }
}

static int sel_overlay_click(float x, float y) {
    if (!g_selopen) return 0;
    if (x >= sel_x && x < sel_x + sel_w && y >= sel_y && y < sel_y + sel_ih * sel_n) {
        int i = (int)((y - sel_y) / sel_ih);
        g_selopen->selidx = i;
        node* s = g_selopen;
        g_selopen = 0;
        int p;
        js_dispatch(s, "input", 1, 0, 0, 0, &p);
        js_dispatch(s, "change", 1, 0, 0, 0, &p);
        G.dirty_style = 1;
        return 1;
    }
    return 0;
}

static int find_text(const char* q, int next) {
    find_on = 0;
    if (!q || !*q || !g_root) return 0;
    int ql = (int)strlen(q);
    box* stack[4096];
    int sp = 0;
    stack[sp++] = g_root;
    float best_y = 1e30f, first_y = 1e30f;
    frag* best = 0;
    frag* firstf = 0;
    int count = 0;
    float start = next ? G.scroll + 1 : G.scroll - 1;
    while (sp > 0) {
        box* b = stack[--sp];
        for (int i = 0; i < b->nfr; i++) {
            frag* f = &b->fr[i];
            if (f->kind != FK_TEXT) continue;
            for (int k = 0; k + ql <= f->n; k++) if (ieqn(f->s + k, q, ql)) {
                count++;
                if (f->y < first_y) { first_y = f->y; firstf = f; }
                if (f->y > start && f->y < best_y) { best_y = f->y; best = f; }
                break;
            }
        }
        for (box* c = b->first; c; c = c->next) if (sp < 4096) stack[sp++] = c;
    }
    if (!best) best = firstf;
    if (best) {
        find_x = best->x; find_y = best->y; find_w = best->w; find_h = best->h; find_on = 1;
        eng_scroll_to(best->y - G.vh / 3);
    }
    G.dirty_paint = 1;
    return count;
}

static void reset_engine(void) {
    wm_reset_all();
    { extern void anim_reset(void); anim_reset(); }
    ttf_reset();
    { extern void webfont_reset(void); webfont_reset(); }
    nwe_arena_reset();
    nwe_pools_forget();
    font_cache_reset();
    css_reset_all();
    js_reset();
    imgs = 0;
    npend = 0;
    ndeferred = 0;
    g_selopen = 0;
    g_down = 0;
    find_on = 0;
    g_root = 0;
    g_doch = 0;
    memset(&G, 0, sizeof(G));
}

static void title_from_doc(void) {
    node* h = doc_head();
    G.title[0] = 0;
    node* t = 0;
    if (h) for (node* c = h->first; c; c = c->next) if (c->tag == T_TITLE) { t = c; break; }
    if (!t) { node* stack[1024]; int sp = 0; stack[sp++] = G.doc; while (sp > 0 && !t) { node* n = stack[--sp]; if (n->tag == T_TITLE && (!n->parent || n->parent->tag != T_SVG)) t = n; for (node* c = n->first; c; c = c->next) if (c->type == NT_ELEM && sp < 1024) stack[sp++] = c; } }
    if (!t) return;
    sbuf b = { 0, 0, 0 };
    text_content(t, &b);
    int o = 0, sp = 1;
    for (int i = 0; i < b.n && o < (int)sizeof(G.title) - 1; i++) { char c = b.s[i]; if (is_ws((unsigned char)c)) { if (!sp) { G.title[o++] = ' '; sp = 1; } } else { G.title[o++] = c; sp = 0; } }
    while (o > 0 && G.title[o - 1] == ' ') o--;
    G.title[o] = 0;
}

static void do_load(nwe_req* r) {
    float vw = (float)(r->w > 0 ? r->w : 800), vh = (float)(r->h > 0 ? r->h : 600);
    reset_engine();
    G.vw = vw;
    G.vh = vh;
    G.t0 = nwk_ms();
    snprintf(G.url, sizeof(G.url), "%s", r->url ? r->url : "about:blank");
    snprintf(G.base, sizeof(G.base), "%s", G.url);
    url_origin(G.url, G.origin, sizeof(G.origin));
    if (!strcmp(G.url, "about:crash")) *(volatile int*)0x8000000000000000ULL = 1;
    G.doc = node_new(NT_DOC);
    css_bump();
    js_init();
    const char* src = r->data ? r->data : "";
    int len = r->len;
    sbuf wrapped = { 0, 0, 0 };
    if (r->btn == 1) {
        sb_puts(&wrapped, "<html><head></head><body><pre style=\"white-space:pre-wrap;word-wrap:break-word\">");
        for (int i = 0; i < len; i++) { char c = src[i]; if (c == '<') sb_puts(&wrapped, "&lt;"); else if (c == '&') sb_puts(&wrapped, "&amp;"); else sb_putc(&wrapped, c); }
        sb_puts(&wrapped, "</pre></body></html>");
        src = wrapped.s;
        len = wrapped.n;
    }
    for (int i = 0; i + 5 < len && i < 8192; i++) {
        if (src[i] == '<' && ieqn(src + i + 1, "base", 4) && is_ws((unsigned char)src[i + 5])) {
            const char* e = memchr(src + i, '>', (size_t)(len - i));
            if (!e) break;
            const char* h = src + i;
            while (h < e && !ieqn(h, "href", 4)) h++;
            if (h < e) {
                h += 4;
                while (h < e && (*h == '=' || is_ws((unsigned char)*h))) h++;
                char q = (*h == '"' || *h == '\'') ? *h++ : 0;
                const char* he = h;
                while (he < e && (q ? *he != q : !is_ws((unsigned char)*he))) he++;
                char tmp[1024];
                int l = (int)(he - h) < 1023 ? (int)(he - h) : 1023;
                memcpy(tmp, h, (size_t)l);
                tmp[l] = 0;
                url_resolve(G.url, tmp, G.base, sizeof(G.base));
            }
            break;
        }
    }
    G.loaded = 0;
    parse_html(G.doc, src, len, 1);
    title_from_doc();
    scan_images(G.doc);
    G.loaded = 1;
    for (int i = 0; i < ndeferred; i++) run_script_node(deferred[i]);
    ndeferred = 0;
    G.dirty_style = 1;
    G.loaded = 2;
    js_fire_load();
    for (int i = 0; i < npend && i < 512; i++) run_script_node(pend_scripts[i]);
    npend = 0;
    title_from_doc();
    eng_layout_if_dirty();
    const char* h = strchr(G.url, '#');
    if (h && h[1]) scroll_to_anchor(h + 1);
    G.dirty_paint = 1;
}

static void blank_doc(void) {
    reset_engine();
    G.vw = 800;
    G.vh = 600;
    G.t0 = nwk_ms();
    snprintf(G.url, sizeof(G.url), "about:blank");
    snprintf(G.base, sizeof(G.base), "about:blank");
    snprintf(G.origin, sizeof(G.origin), "null");
    G.doc = node_new(NT_DOC);
    js_init();
    parse_html(G.doc, "<html><body></body></html>", 26, 0);
}

static void fill_nav(nwe_req* r) {
    if (!G.nav_pending) { r->nav[0] = 0; return; }
    snprintf(r->nav, sizeof(r->nav), "%s", G.nav);
    snprintf(r->navmethod, sizeof(r->navmethod), "%s", G.navmethod);
    r->navbody = G.navbody;
    r->navblen = G.navblen;
    G.nav_pending = 0;
}

static void fill_info(nwe_req* r) {
    snprintf(r->title, sizeof(r->title), "%s", G.title);
    r->scroll = (int)G.scroll;
    r->doch = (int)g_doch;
    r->viewh = (int)G.vh;
    r->dirty = G.dirty_paint || G.dirty_layout || G.dirty_style;
}

static void hover_info(nwe_req* r, node* n) {
    r->href[0] = 0;
    r->cursor = 0;
    for (node* p = n; p && p->type == NT_ELEM; p = p->parent) {
        if ((p->tag == T_A || p->tag == T_AREA) && attr_get(p, "href")) { url_resolve(G.base, attr_get(p, "href"), r->href, sizeof(r->href)); r->cursor = 1; return; }
        if (p->tag == T_BUTTON || p->tag == T_SELECT || p->tag == T_LABEL || p->tag == T_SUMMARY || (p->tag == T_INPUT && !is_text_input(p))) { r->cursor = 1; return; }
        if (is_text_input(p)) { r->cursor = 2; return; }
        if (p->st && p->st->cursor == 1) { r->cursor = 1; return; }
        if (p->st && p->st->cursor == 2) { r->cursor = 2; return; }
    }
}

static long dispatch(long op, nwe_req* r) {
    if (op == NWE_OP_INIT) { nwe_arena_init(); g_inited = 1; return 0; }
    if (!g_inited) { nwe_arena_init(); g_inited = 1; }
    if (op == NWE_OP_LOAD) { g_dead = 0; do_load(r); fill_info(r); fill_nav(r); return 0; }
    if (op == NWE_OP_EVAL) {
        if (!G.doc || g_dead) { blank_doc(); g_dead = 0; }
        r->ret = js_eval_expr(r->data ? r->data : "", r->out, sizeof(r->out));
        fill_nav(r);
        return r->ret;
    }
    if (!G.doc || g_dead) { r->ret = 0; r->nav[0] = 0; return 0; }
    switch (op) {
    case NWE_OP_LAYOUT: {
        float nw = (float)r->w, nh = (float)r->h;
        if (nw != G.vw || nh != G.vh) {
            G.vw = nw; G.vh = nh;
            G.dirty_style = 1;
            int p;
            if (js_ready()) js_dispatch(G.doc, "resize", 0, 0, 0, 0, &p);
        }
        eng_layout_if_dirty();
        fill_info(r);
        break;
    }
    case NWE_OP_PAINT:
        eng_layout_if_dirty();
        paint_all(r->fb, r->stride, r->fbw, r->fbh, r->x, r->y, r->w, r->h, G.scroll);
        if (find_on) fill_rect_a(r->fb, r->stride, r->x, r->y, r->x + r->w, r->y + r->h, find_x + r->x - 1, find_y + r->y - G.scroll - 1, find_w + 2, find_h + 2, 0x60FFC400);
        paint_select_overlay(r->fb, r->stride, r->fbw, r->fbh, r->x, r->y, r->w, r->h);
        if (G.focus && g_ce_node && is_edit_host(G.focus) && (g_ce_node == G.focus || node_contains(G.focus, g_ce_node))) {
            float cx, cy, ch;
            if (caret_pos(g_ce_node, g_ce_off, &cx, &cy, &ch)) {
                int px = r->x + (int)cx, py = r->y + (int)(cy - G.scroll);
                unsigned col = G.focus->st ? (G.focus->st->color | 0xFF000000u) : 0xFF000000u;
                for (int yy = py; yy < py + (int)ch; yy++) {
                    if (yy < r->y || yy >= r->y + r->h || yy >= r->fbh) continue;
                    for (int xx = px; xx < px + 2; xx++) if (xx >= r->x && xx < r->x + r->w && xx < r->fbw) r->fb[(size_t)yy * (size_t)r->stride + (size_t)xx] = col;
                }
            }
        }
        G.dirty_paint = 0;
        fill_info(r);
        break;
    case NWE_OP_SCROLL: eng_scroll_to(G.scroll + (float)r->y); fill_info(r); break;
    case NWE_OP_WHEEL: do_wheel((float)r->x, (float)r->y, r->key); fill_info(r); break;
    case NWE_OP_MOVE: {
        node* n = hit((float)r->x, (float)r->y);
        hover_info(r, n);
        if (n != G.hover) {
            node* old = G.hover;
            G.hover = n;
            if (g_css_hover) G.dirty_style = 1;
            int p;
            if (old) js_dispatch(old, "mouseout", 1, (float)r->x, (float)r->y, 0, &p);
            if (n) js_dispatch(n, "mouseover", 1, (float)r->x, (float)r->y, 0, &p);
        }
        unsigned long now = nwk_ms();
        if (n && now - g_last_move > 60) { g_last_move = now; int p; js_dispatch(n, "mousemove", 1, (float)r->x, (float)r->y, 0, &p); }
        fill_info(r);
        break;
    }
    case NWE_OP_DOWN: {
        if (g_selopen) { if (sel_overlay_click((float)r->x, (float)r->y)) { g_down = 0; fill_info(r); break; } }
        node* n = hit((float)r->x, (float)r->y);
        g_down = n;
        g_downx = (float)r->x;
        g_downy = (float)r->y;
        G.active = n;
        if (g_css_hover) G.dirty_style = 1;
        int p = 0;
        if (n) { js_dispatch(n, "pointerdown", 1, (float)r->x, (float)r->y, 0, &p); js_dispatch(n, "mousedown", 1, (float)r->x, (float)r->y, 0, &p); }
        node* f = n;
        while (f && !focusable(f)) f = f->parent;
        if (!p) set_focus(f && (is_text_input(f) || f->tag == T_SELECT || f->tag == T_BUTTON || f->tag == T_INPUT || f->tag == T_A || attr_get(f, "tabindex") || is_edit_host(f)) ? f : 0);
        if (!p && f && is_edit_host(f) && G.focus == f) ce_place(f, (float)r->x, (float)r->y);
        if (g_selopen && g_selopen != f) { g_selopen = 0; G.dirty_paint = 1; }
        if (f && is_text_input(f) && f->box && f->value) {
            float bx = f->box->x + f->box->b[3] + f->box->p[3];
            float rel = (float)r->x - bx;
            int best = f->vlen;
            float acc = 0;
            int i = 0;
            while (i < f->vlen) { int cp; int k = u8dec(f->value + i, f->vlen - i, &cp); float aw = font_adv(f->st->face, f->st->fsize, cp); if (acc + aw / 2 > rel) { best = i; break; } acc += aw; i += k; }
            if (f->tag != T_TEXTAREA) f->caret = best;
        }
        fill_info(r);
        break;
    }
    case NWE_OP_UP: {
        node* n = hit((float)r->x, (float)r->y);
        int p = 0;
        if (n) { js_dispatch(n, "pointerup", 1, (float)r->x, (float)r->y, 0, &p); js_dispatch(n, "mouseup", 1, (float)r->x, (float)r->y, 0, &p); }
        G.active = 0;
        if (g_css_hover) G.dirty_style = 1;
        if (n && g_down && (n == g_down || node_contains(g_down, n) || node_contains(n, g_down))) {
            node* t = node_contains(g_down, n) ? n : g_down;
            if (fabsf((float)r->x - g_downx) < 6 && fabsf((float)r->y - g_downy) < 6) {
                p = 0;
                js_dispatch(t, "click", 1, (float)r->x, (float)r->y, 0, &p);
                node* mn = t;
                while (mn && mn->tag != T_VIDEO && mn->tag != T_AUDIO) mn = mn->parent;
                int mc = !p && mn ? wm_click(mn, (float)r->x, (float)r->y) : -100;
                if (mc != -100) { js_ce_call("__mediaUI", mn, mc); G.dirty_paint = 1; }
                else if (!p) default_action(t);
            }
        }
        g_down = 0;
        hover_info(r, n);
        fill_info(r);
        break;
    }
    case NWE_OP_KEY: r->ret = key_event(r->key); fill_info(r); break;
    case NWE_OP_TICK: {
        int any = js_tick();
        if (npend > 0) {
            int n = npend;
            node* list[512];
            memcpy(list, pend_scripts, sizeof(node*) * (size_t)n);
            npend = 0;
            for (int i = 0; i < n; i++) run_script_node(list[i]);
            any = 1;
        }
        unsigned long t0 = nwk_ms();
        int loaded = 0;
        while (img_pending() && nwk_ms() - t0 < 60 && loaded < 4) { img_load_one(); loaded++; if (G.nav_pending) break; }
        if (loaded) { G.dirty_layout = 1; any = 1; if (!img_pending()) nwk_status("Done"); }
        if (g_scroll_evt) { g_scroll_evt = 0; int p; if (js_ready()) js_dispatch(G.doc, "scroll", 0, 0, 0, 0, &p); }
        if (G.dirty_style || G.dirty_layout) { eng_layout_if_dirty(); any = 1; }
        { int wt = wm_tick(); if (wt & 2) { G.dirty_layout = 1; eng_layout_if_dirty(); } if (wt) any = 1; }
        {
            extern int anim_tick(void);
            extern int anim_events(node** ns, const char** ts, int cap);
            static unsigned long last_af;
            unsigned long nowa = nwk_ms();
            if (nowa - last_af >= 33) {
                last_af = nowa;
                int af = anim_tick();
                if (af & 2) { G.dirty_layout = 1; eng_layout_if_dirty(); }
                if (af) any = 1;
            }
            node* evn[64];
            const char* evt[64];
            int ne = anim_events(evn, evt, 64);
            for (int i = 0; i < ne; i++) { int p; if (js_ready()) js_dispatch(evn[i], evt[i], 1, 0, 0, 0, &p); }
            if (ne) any = 1;
        }
        if (any) G.dirty_paint = 1;
        r->ret = G.dirty_paint || (g_selopen != 0) || (G.focus && is_text_input(G.focus));
        fill_info(r);
        r->dirty = r->ret;
        break;
    }
    case NWE_OP_FIND: r->ret = find_text(r->data, r->key); fill_info(r); break;
    case NWE_OP_INFO: fill_info(r); break;
    case NWE_OP_MEDIA: {
        if (G.dirty_paint || G.dirty_style || G.dirty_layout) { r->ret = -1; break; }
        int rx = 0, ry = 0, rw = 0, rh = 0;
        r->ret = wm_fast(r->fb, r->stride, &rx, &ry, &rw, &rh);
        r->x = rx; r->y = ry; r->w = rw; r->h = rh;
        break;
    }
    }
    fill_nav(r);
    return r->ret;
}

int g_nwe_user_stop;
void nwe_check_stop(void) {
    static unsigned c;
    if ((++c & 127) == 0 && nwk_busy()) { g_nwe_user_stop = 0; nwe_fail("the page stopped responding and was stopped with Esc"); }
}
long nwe_entry(long op, void* req) {
    nwe_req* r = req;
    static int depth;
    if (depth > 0) { if (op == NWE_OP_PAINT || op == NWE_OP_INFO) { r->ret = -2; return -2; } }
    depth++;
    int sv = g_pool;
    if (__builtin_setjmp(jb)) {
        depth = 0;
        g_pool = 0;
        g_dead = 1;
        char m[220];
        snprintf(m, sizeof(m), "Page engine stopped: %s", g_failmsg);
        nwk_log(m);
        nwk_status(m);
        r->ret = -1;
        r->nav[0] = 0;
        return -1;
    }
    long rv = dispatch(op, r);
    if (g_nwe_user_stop) { g_nwe_user_stop = 0; nwe_fail("the page stopped responding and was stopped with Esc"); }
    g_pool = sv;
    depth--;
    return rv;
}
