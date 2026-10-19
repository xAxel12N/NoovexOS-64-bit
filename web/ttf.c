#include "web/nwe.h"

typedef unsigned char u8;
typedef struct lig { unsigned short first, n, glyph; unsigned short comp[10]; int cp; } lig;
typedef struct wfont {
    lig* ligs; int nlig;
    const u8* glyf; int glyflen;
    const u8* loca; int localen; int locfmt;
    const u8* hmtx; int nhm;
    int nglyphs;
    short** cache;
} wfont;

static unsigned rd16(const u8* p) { return ((unsigned)p[0] << 8) | p[1]; }
static int rs16(const u8* p) { return (short)rd16(p); }
static unsigned rd32(const u8* p) { return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]; }

int nwk_inflate(const unsigned char* src, int slen, unsigned char* dst, int dcap);
int brotli_decode(const u8* src, int slen, u8* dst, int dcap);
int woff2_to_sfnt(const u8* src, int len, u8** out, int* outlen);

typedef struct { unsigned tag; const u8* p; int len; } tbl;

static int find_tables(const u8* d, int len, tbl* t, int cap) {
    if (len < 12) return -1;
    int n = (int)rd16(d + 4);
    if (12 + n * 16 > len) return -1;
    int k = 0;
    for (int i = 0; i < n && k < cap; i++) {
        const u8* e = d + 12 + i * 16;
        unsigned off = rd32(e + 8), l = rd32(e + 12);
        if (off > (unsigned)len || l > (unsigned)len - off) continue;
        t[k].tag = rd32(e);
        t[k].p = d + off;
        t[k].len = (int)l;
        k++;
    }
    return k;
}
static const tbl* tget(const tbl* t, int n, const char* tag) {
    unsigned v = ((unsigned)tag[0] << 24) | ((unsigned)tag[1] << 16) | ((unsigned)tag[2] << 8) | (unsigned)tag[3];
    for (int i = 0; i < n; i++) if (t[i].tag == v) return &t[i];
    return 0;
}

static u8* woff1_to_sfnt(const u8* d, int len, int* outlen) {
    if (len < 44) return 0;
    int n = (int)rd16(d + 12);
    unsigned total = rd32(d + 16);
    if (n <= 0 || n > 200 || total > (32u << 20) || 44 + n * 20 > len) return 0;
    u8* o = nalloc(total + 16);
    memset(o, 0, total + 16);
    o[0] = d[4]; o[1] = d[5]; o[2] = d[6]; o[3] = d[7];
    o[4] = (u8)(n >> 8); o[5] = (u8)n;
    unsigned off = 12 + (unsigned)n * 16;
    for (int i = 0; i < n; i++) {
        const u8* e = d + 44 + i * 20;
        unsigned tag = rd32(e), toff = rd32(e + 4), clen = rd32(e + 8), olen = rd32(e + 12);
        if (toff > (unsigned)len || clen > (unsigned)len - toff || off + olen > total) return 0;
        u8* de = o + 12 + i * 16;
        de[0] = (u8)(tag >> 24); de[1] = (u8)(tag >> 16); de[2] = (u8)(tag >> 8); de[3] = (u8)tag;
        de[8] = (u8)(off >> 24); de[9] = (u8)(off >> 16); de[10] = (u8)(off >> 8); de[11] = (u8)off;
        de[12] = (u8)(olen >> 24); de[13] = (u8)(olen >> 16); de[14] = (u8)(olen >> 8); de[15] = (u8)olen;
        if (clen == olen) memcpy(o + off, d + toff, olen);
        else if (nwk_inflate(d + toff, (int)clen, o + off, (int)olen) != (int)olen) return 0;
        off = (off + olen + 3) & ~3u;
    }
    *outlen = (int)total;
    return o;
}

static int cmp_g(const void* a, const void* b) { const nwe_glyphrec* x = a; const nwe_glyphrec* y = b; return x->cp - y->cp; }

static int cmap_build(const u8* cm, int len, int nglyphs, nwe_glyphrec* out, int cap, const u8* hmtx, int nhm, int hmlen) {
    if (len < 4) return 0;
    int nt = (int)rd16(cm + 2);
    const u8* best = 0;
    int bestfmt = 0;
    for (int i = 0; i < nt && 4 + i * 8 + 8 <= len; i++) {
        const u8* e = cm + 4 + i * 8;
        unsigned pid = rd16(e), eid = rd16(e + 2), off = rd32(e + 4);
        if (off + 4 > (unsigned)len) continue;
        const u8* st = cm + off;
        int fmt = (int)rd16(st);
        int ok = (pid == 3 && (eid == 1 || eid == 10)) || pid == 0;
        if (pid == 3 && eid == 0) ok = 1;
        if (!ok) continue;
        if (fmt == 12 && bestfmt != 12) { best = st; bestfmt = 12; }
        else if (fmt == 4 && !bestfmt) { best = st; bestfmt = 4; }
    }
    if (!best) return 0;
    int n = 0;
    const u8* end = cm + len;
#define ADDG(c, g) do { if (n < cap && (g) > 0 && (g) < nglyphs) { int gi_ = (g); int ai_ = gi_ < nhm ? gi_ : nhm - 1; int adv_ = (4 * ai_ + 2 <= hmlen) ? (int)rd16(hmtx + 4 * ai_) : 0; out[n].cp = (c); out[n].adv = adv_; out[n].off = gi_; n++; } } while (0)
    if (bestfmt == 4) {
        int segx2 = (int)rd16(best + 6);
        const u8* ends = best + 14;
        const u8* starts = ends + segx2 + 2;
        const u8* deltas = starts + segx2;
        const u8* ranges = deltas + segx2;
        if (ranges + segx2 > end) return 0;
        for (int s = 0; s < segx2 / 2; s++) {
            unsigned e = rd16(ends + s * 2), st = rd16(starts + s * 2);
            int dl = rs16(deltas + s * 2);
            unsigned ro = rd16(ranges + s * 2);
            if (st == 0xFFFF) break;
            for (unsigned c = st; c <= e && n < cap; c++) {
                int g;
                if (!ro) g = (int)((c + (unsigned)dl) & 0xFFFF);
                else {
                    const u8* gp = ranges + s * 2 + ro + (c - st) * 2;
                    if (gp + 2 > end) break;
                    g = (int)rd16(gp);
                    if (g) g = (g + dl) & 0xFFFF;
                }
                ADDG((int)c, g);
                if (c == 0xFFFF) break;
            }
        }
    } else {
        unsigned ng = rd32(best + 12);
        for (unsigned i = 0; i < ng && best + 16 + i * 12 + 12 <= end; i++) {
            const u8* e = best + 16 + i * 12;
            unsigned a = rd32(e), b = rd32(e + 4), g = rd32(e + 8);
            if (b > 0x10FFFF || a > b) continue;
            for (unsigned c = a; c <= b && n < cap; c++) ADDG((int)c, (int)(g + (c - a)));
        }
    }
#undef ADDG
    msort(out, n, sizeof(nwe_glyphrec), cmp_g);
    int k = 0;
    for (int i = 0; i < n; i++) if (!k || out[k - 1].cp != out[i].cp) out[k++] = out[i];
    return k;
}

typedef struct { short* d; int n, cap; } sv;
static void sp(sv* v, int x) { if (v->n < v->cap) v->d[v->n++] = (short)(x < -32767 ? -32767 : x > 32767 ? 32767 : x); }

static int glyph_range(wfont* f, int g, unsigned* a, unsigned* b) {
    if (g < 0 || g >= f->nglyphs) return 0;
    if (f->locfmt == 0) { if ((g + 1) * 2 + 2 > f->localen) return 0; *a = rd16(f->loca + g * 2) * 2u; *b = rd16(f->loca + g * 2 + 2) * 2u; }
    else { if ((g + 1) * 4 + 4 > f->localen) return 0; *a = rd32(f->loca + g * 4); *b = rd32(f->loca + g * 4 + 4); }
    return *a < *b && *b <= (unsigned)f->glyflen;
}

static void outline(wfont* f, int g, float m[6], sv* v, int depth) {
    unsigned a, b;
    if (depth > 6 || !glyph_range(f, g, &a, &b)) return;
    const u8* p = f->glyf + a;
    const u8* end = f->glyf + b;
    if (end - p < 10) return;
    int nc = rs16(p);
    p += 10;
    if (nc >= 0) {
        if (p + nc * 2 + 2 > end) return;
        const u8* ep = p;
        int npts = nc ? (int)rd16(ep + (nc - 1) * 2) + 1 : 0;
        p += nc * 2;
        int il = (int)rd16(p);
        p += 2 + il;
        if (p > end || npts > 4000) return;
        static u8 fl[4000];
        int k = 0;
        while (k < npts && p < end) { u8 c = *p++; fl[k++] = c; if (c & 8) { if (p >= end) return; int r = *p++; while (r-- > 0 && k < npts) fl[k++] = c; } }
        if (k < npts) return;
        static int xs[4000], ys[4000];
        int x = 0, y = 0;
        for (int i = 0; i < npts; i++) {
            u8 c = fl[i];
            if (c & 2) { if (p >= end) return; int dx = *p++; x += (c & 16) ? dx : -dx; }
            else if (!(c & 16)) { if (p + 2 > end) return; x += rs16(p); p += 2; }
            xs[i] = x;
        }
        for (int i = 0; i < npts; i++) {
            u8 c = fl[i];
            if (c & 4) { if (p >= end) return; int dy = *p++; y += (c & 32) ? dy : -dy; }
            else if (!(c & 32)) { if (p + 2 > end) return; y += rs16(p); p += 2; }
            ys[i] = y;
        }
        int st = 0;
        for (int ci = 0; ci < nc; ci++) {
            int en = (int)rd16(ep + ci * 2);
            if (en < st || en >= npts) return;
            int cnt = en - st + 1;
            int first = -1;
            for (int i = 0; i < cnt; i++) if (fl[st + i] & 1) { first = i; break; }
            float sx, sy;
            #define TX(X, Y) (m[0] * (X) + m[2] * (Y) + m[4])
            #define TY(X, Y) (m[1] * (X) + m[3] * (Y) + m[5])
            if (first < 0) { float ax = (xs[st] + xs[st + cnt - 1]) / 2.0f, ay = (ys[st] + ys[st + cnt - 1]) / 2.0f; sx = ax; sy = ay; first = 0; sp(v, 0); sp(v, (int)TX(sx, sy)); sp(v, (int)TY(sx, sy)); }
            else { sx = (float)xs[st + first]; sy = (float)ys[st + first]; sp(v, 0); sp(v, (int)TX(sx, sy)); sp(v, (int)TY(sx, sy)); }
            int allon = 1;
            for (int i = 0; i < cnt; i++) if (!(fl[st + i] & 1)) allon = 0;
            int startoff = (fl[st + first] & 1) ? first + 1 : first;
            float cx = 0, cy = 0;
            int pend = 0;
            for (int j = 0; j < cnt; j++) {
                int i = st + (startoff + j) % cnt;
                float px2 = (float)xs[i], py2 = (float)ys[i];
                if (fl[i] & 1) {
                    if (pend) { sp(v, 2); sp(v, (int)TX(cx, cy)); sp(v, (int)TY(cx, cy)); sp(v, (int)TX(px2, py2)); sp(v, (int)TY(px2, py2)); pend = 0; }
                    else { sp(v, 1); sp(v, (int)TX(px2, py2)); sp(v, (int)TY(px2, py2)); }
                } else {
                    if (pend) { float mx = (cx + px2) / 2, my = (cy + py2) / 2; sp(v, 2); sp(v, (int)TX(cx, cy)); sp(v, (int)TY(cx, cy)); sp(v, (int)TX(mx, my)); sp(v, (int)TY(mx, my)); }
                    cx = px2; cy = py2; pend = 1;
                }
            }
            if (pend) { sp(v, 2); sp(v, (int)TX(cx, cy)); sp(v, (int)TY(cx, cy)); sp(v, (int)TX(sx, sy)); sp(v, (int)TY(sx, sy)); }
            (void)allon;
            sp(v, 4);
            st = en + 1;
            #undef TX
            #undef TY
        }
        return;
    }
    for (int guard = 0; guard < 64; guard++) {
        if (p + 4 > end) return;
        unsigned fl = rd16(p), gi = rd16(p + 2);
        p += 4;
        float dx, dy;
        if (fl & 1) { if (p + 4 > end) return; dx = (float)rs16(p); dy = (float)rs16(p + 2); p += 4; }
        else { if (p + 2 > end) return; dx = (float)(signed char)p[0]; dy = (float)(signed char)p[1]; p += 2; }
        float t[6] = { 1, 0, 0, 1, 0, 0 };
        if (fl & 8) { if (p + 2 > end) return; t[0] = t[3] = rs16(p) / 16384.0f; p += 2; }
        else if (fl & 0x40) { if (p + 4 > end) return; t[0] = rs16(p) / 16384.0f; t[3] = rs16(p + 2) / 16384.0f; p += 4; }
        else if (fl & 0x80) { if (p + 8 > end) return; t[0] = rs16(p) / 16384.0f; t[1] = rs16(p + 2) / 16384.0f; t[2] = rs16(p + 4) / 16384.0f; t[3] = rs16(p + 6) / 16384.0f; p += 8; }
        if (!(fl & 2)) { dx = 0; dy = 0; }
        t[4] = dx; t[5] = dy;
        float c[6];
        c[0] = m[0] * t[0] + m[2] * t[1];
        c[1] = m[1] * t[0] + m[3] * t[1];
        c[2] = m[0] * t[2] + m[2] * t[3];
        c[3] = m[1] * t[2] + m[3] * t[3];
        c[4] = m[0] * t[4] + m[2] * t[5] + m[4];
        c[5] = m[1] * t[4] + m[3] * t[5] + m[5];
        outline(f, (int)gi, c, v, depth + 1);
        if (!(fl & 0x20)) break;
    }
}

static int cov_index(const u8* c, const u8* end, unsigned g) {
    if (c + 4 > end) return -1;
    unsigned fmt = rd16(c), n = rd16(c + 2);
    if (fmt == 1) { for (unsigned i = 0; i < n && c + 6 + i * 2 <= end; i++) if (rd16(c + 4 + i * 2) == g) return (int)i; return -1; }
    if (fmt == 2) { for (unsigned i = 0; i < n && c + 4 + i * 6 + 6 <= end; i++) { const u8* r = c + 4 + i * 6; unsigned a = rd16(r), b = rd16(r + 2); if (g >= a && g <= b) return (int)(rd16(r + 4) + g - a); } return -1; }
    return -1;
}
static void cov_list(const u8* c, const u8* end, unsigned* out, int* n, int cap) {
    *n = 0;
    if (c + 4 > end) return;
    unsigned fmt = rd16(c), cnt = rd16(c + 2);
    if (fmt == 1) { for (unsigned i = 0; i < cnt && *n < cap && c + 6 + i * 2 <= end; i++) out[(*n)++] = rd16(c + 4 + i * 2); }
    else if (fmt == 2) { for (unsigned i = 0; i < cnt && c + 4 + i * 6 + 6 <= end; i++) { const u8* r = c + 4 + i * 6; unsigned a = rd16(r), b = rd16(r + 2), si = rd16(r + 4); for (unsigned g = a; g <= b && *n < cap; g++) { int idx = (int)(si + g - a); if (idx < cap) { out[idx] = g; if (idx + 1 > *n) *n = idx + 1; } } } }
}
static int parse_gsub(const u8* gs, int len, lig* out, int cap) {
    if (len < 10) return 0;
    const u8* end = gs + len;
    const u8* fl = gs + rd16(gs + 6);
    const u8* ll = gs + rd16(gs + 8);
    if (fl + 2 > end || ll + 2 > end) return 0;
    u8 want[4096];
    memset(want, 0, sizeof want);
    unsigned nf = rd16(fl);
    for (unsigned i = 0; i < nf && fl + 2 + i * 6 + 6 <= end; i++) {
        const u8* r = fl + 2 + i * 6;
        unsigned tag = rd32(r);
        if (tag != 0x6C696761u && tag != 0x726C6967u && tag != 0x636C6967u) continue;
        const u8* ft = fl + rd16(r + 4);
        if (ft + 4 > end) continue;
        unsigned nl = rd16(ft + 2);
        for (unsigned k = 0; k < nl && ft + 4 + k * 2 + 2 <= end; k++) { unsigned li = rd16(ft + 4 + k * 2); if (li < 4096) want[li] = 1; }
    }
    int n = 0;
    unsigned nlk = rd16(ll);
    static unsigned cov[8192];
    for (unsigned li = 0; li < nlk && li < 4096; li++) {
        if (!want[li] || ll + 2 + li * 2 + 2 > end) continue;
        const u8* lk = ll + rd16(ll + 2 + li * 2);
        if (lk + 6 > end) continue;
        unsigned type = rd16(lk), nsub = rd16(lk + 4);
        for (unsigned si = 0; si < nsub && lk + 6 + si * 2 + 2 <= end; si++) {
            const u8* st = lk + rd16(lk + 6 + si * 2);
            unsigned t = type;
            if (t == 7) { if (st + 8 > end) continue; t = rd16(st + 2); st = st + rd32(st + 4); }
            if (t != 4 || st + 6 > end || rd16(st) != 1) continue;
            int nc = 0;
            cov_list(st + rd16(st + 2), end, cov, &nc, 8192);
            unsigned nset = rd16(st + 4);
            for (unsigned k = 0; k < nset && (int)k < nc && st + 6 + k * 2 + 2 <= end; k++) {
                const u8* ls = st + rd16(st + 6 + k * 2);
                if (ls + 2 > end) continue;
                unsigned nlg = rd16(ls);
                for (unsigned q = 0; q < nlg && ls + 2 + q * 2 + 2 <= end && n < cap; q++) {
                    const u8* lg = ls + rd16(ls + 2 + q * 2);
                    if (lg + 4 > end) continue;
                    unsigned cc = rd16(lg + 2);
                    if (cc < 2 || cc > 11 || lg + 4 + (cc - 1) * 2 > end) continue;
                    lig* L = &out[n++];
                    L->first = (unsigned short)cov[k];
                    L->glyph = (unsigned short)rd16(lg);
                    L->n = (unsigned short)(cc - 1);
                    for (unsigned c = 0; c < cc - 1; c++) L->comp[c] = (unsigned short)rd16(lg + 4 + c * 2);
                }
            }
        }
    }
    (void)cov_index;
    return n;
}

#define WF_MAX 32
static wfont g_wf[WF_MAX];
static nwe_facerec g_dyn[WF_MAX];
static char g_dynname[WF_MAX][32];
static int g_ndyn;

void ttf_reset(void) { g_ndyn = 0; memset(g_wf, 0, sizeof g_wf); }

const nwe_facerec* nwe_face(int face) {
    if (face < nwe_nfaces) return &nwe_faces[face < 0 ? 0 : face];
    int i = face - nwe_nfaces;
    if (i >= g_ndyn) return &nwe_faces[0];
    return &g_dyn[i];
}
int nwe_face_count(void) { return nwe_nfaces + g_ndyn; }

const short* ttf_outline(int face, int g) {
    int i = face - nwe_nfaces;
    if (i < 0 || i >= g_ndyn) return 0;
    wfont* f = &g_wf[i];
    if (g < 0 || g >= f->nglyphs) return 0;
    if (f->cache[g]) return f->cache[g];
    int sv0 = pool_suspend();
    static short buf[60000];
    sv v = { buf, 0, 59990 };
    float m[6] = { 1, 0, 0, 1, 0, 0 };
    outline(f, g, m, &v, 0);
    sp(&v, 5);
    short* o = nalloc(sizeof(short) * (size_t)v.n);
    memcpy(o, buf, sizeof(short) * (size_t)v.n);
    f->cache[g] = o;
    pool_resume(sv0);
    return o;
}

static int gid_of(const nwe_facerec* r, int cp) {
    int lo = 0, hi = r->n - 1;
    while (lo <= hi) { int m = (lo + hi) / 2; if (r->g[m].cp == cp) return r->g[m].off; if (r->g[m].cp < cp) lo = m + 1; else hi = m - 1; }
    return -1;
}
static int put8(char* o, int cap, int k, int cp) {
    if (cp < 0x80) { if (k + 1 > cap) return k; o[k++] = (char)cp; }
    else if (cp < 0x800) { if (k + 2 > cap) return k; o[k++] = (char)(0xC0 | (cp >> 6)); o[k++] = (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { if (k + 3 > cap) return k; o[k++] = (char)(0xE0 | (cp >> 12)); o[k++] = (char)(0x80 | ((cp >> 6) & 63)); o[k++] = (char)(0x80 | (cp & 63)); }
    else { if (k + 4 > cap) return k; o[k++] = (char)(0xF0 | (cp >> 18)); o[k++] = (char)(0x80 | ((cp >> 12) & 63)); o[k++] = (char)(0x80 | ((cp >> 6) & 63)); o[k++] = (char)(0x80 | (cp & 63)); }
    return k;
}
int ttf_shape(int face, const char* s, int n, char* out, int cap) {
    int fi = face - nwe_nfaces;
    if (fi < 0 || fi >= g_ndyn || !g_wf[fi].nlig || n <= 1) return -1;
    wfont* f = &g_wf[fi];
    const nwe_facerec* r = &g_dyn[fi];
    int cps[256], gid[256], bpos[257];
    int m = 0, i = 0;
    while (i < n && m < 256) { int cp; int k = u8dec(s + i, n - i, &cp); if (k <= 0) break; bpos[m] = i; cps[m] = cp; gid[m] = gid_of(r, cp); m++; i += k; }
    if (i < n) return -1;
    bpos[m] = n;
    int o = 0, changed = 0;
    for (int p = 0; p < m;) {
        int best = -1, blen = 0;
        int lo = 0, hi = f->nlig;
        while (lo < hi) { int md = (lo + hi) / 2; if (f->ligs[md].first < gid[p]) lo = md + 1; else hi = md; }
        if (gid[p] >= 0) for (int L = lo; L < f->nlig; L++) {
            lig* lg = &f->ligs[L];
            if (lg->first != gid[p]) { if (lg->first > gid[p]) break; continue; }
            if (p + lg->n >= m || lg->n + 1 <= blen) continue;
            int ok = 1;
            for (int c = 0; c < lg->n; c++) if (gid[p + 1 + c] != lg->comp[c]) { ok = 0; break; }
            if (ok) { best = L; blen = lg->n + 1; }
        }
        if (best >= 0) { o = put8(out, cap, o, f->ligs[best].cp); p += blen; changed = 1; }
        else { int l = bpos[p + 1] - bpos[p]; if (o + l > cap) return -1; memcpy(out + o, s + bpos[p], (size_t)l); o += l; p++; }
    }
    return changed ? o : -1;
}

int ttf_load(const u8* data, int len, const char* name) {
    if (g_ndyn >= WF_MAX || len < 12) return -1;
    int sv0 = pool_suspend();
    const u8* d = data;
    int dl = len;
    unsigned sig = rd32(d);
    if (sig == 0x774F4646u) { int ol = 0; u8* o = woff1_to_sfnt(d, dl, &ol); if (!o) { pool_resume(sv0); return -1; } d = o; dl = ol; }
    else if (sig == 0x774F4632u) { u8* o = 0; int ol = 0; if (woff2_to_sfnt(d, dl, &o, &ol) < 0) { pool_resume(sv0); return -1; } d = o; dl = ol; }
    else { u8* c = nalloc((size_t)dl); memcpy(c, d, (size_t)dl); d = c; }
    sig = rd32(d);
    if (sig != 0x00010000u && sig != 0x74727565u) { pool_resume(sv0); return -1; }
    tbl t[64];
    int nt = find_tables(d, dl, t, 64);
    const tbl *head = tget(t, nt, "head"), *hhea = tget(t, nt, "hhea"), *hmtx = tget(t, nt, "hmtx"), *maxp = tget(t, nt, "maxp"), *loca = tget(t, nt, "loca"), *glyf = tget(t, nt, "glyf"), *cmap = tget(t, nt, "cmap"), *os2 = tget(t, nt, "OS/2");
    if (!head || !hhea || !hmtx || !maxp || !loca || !glyf || !cmap || head->len < 54 || hhea->len < 36 || maxp->len < 6) { pool_resume(sv0); return -1; }
    int i = g_ndyn;
    wfont* f = &g_wf[i];
    memset(f, 0, sizeof *f);
    f->glyf = glyf->p; f->glyflen = glyf->len;
    f->loca = loca->p; f->localen = loca->len;
    f->locfmt = rs16(head->p + 50);
    f->nglyphs = (int)rd16(maxp->p + 4);
    f->nhm = (int)rd16(hhea->p + 34);
    if (f->nhm < 1) f->nhm = 1;
    f->hmtx = hmtx->p;
    f->cache = nalloc(sizeof(short*) * (size_t)(f->nglyphs + 1));
    memset(f->cache, 0, sizeof(short*) * (size_t)(f->nglyphs + 1));
    int cap = 20000;
    nwe_glyphrec* g = nalloc(sizeof(nwe_glyphrec) * (size_t)(cap + 6000));
    int ng = cmap_build(cmap->p, cmap->len, f->nglyphs, g, cap, hmtx->p, f->nhm, hmtx->len);
    const tbl* gsub = tget(t, nt, "GSUB");
    if (gsub) {
        f->ligs = nalloc(sizeof(lig) * 6000);
        f->nlig = parse_gsub(gsub->p, gsub->len, f->ligs, 6000);
        for (int a = 1; a < f->nlig; a++) for (int b2 = a; b2 > 0 && f->ligs[b2].first < f->ligs[b2 - 1].first; b2--) { lig tl = f->ligs[b2]; f->ligs[b2] = f->ligs[b2 - 1]; f->ligs[b2 - 1] = tl; }
        for (int k = 0; k < f->nlig; k++) {
            lig* L = &f->ligs[k];
            L->cp = 0xF0000 + k;
            int gi = L->glyph < f->nglyphs ? L->glyph : 0;
            int ai = gi < f->nhm ? gi : f->nhm - 1;
            g[ng].cp = L->cp; g[ng].adv = 4 * ai + 2 <= hmtx->len ? (int)rd16(hmtx->p + 4 * ai) : 0; g[ng].off = gi; ng++;
        }
        msort(g, ng, sizeof(nwe_glyphrec), cmp_g);
    }
    nwe_facerec* r = &g_dyn[i];
    memset(r, 0, sizeof *r);
    snprintf(g_dynname[i], sizeof g_dynname[i], "%s", name ? name : "webfont");
    r->name = g_dynname[i];
    r->upm = (int)rd16(head->p + 18);
    if (r->upm <= 0) r->upm = 1000;
    int asc = rs16(hhea->p + 4), desc = -rs16(hhea->p + 6), gap = rs16(hhea->p + 8);
    if (os2 && os2->len >= 78) { int wa = (int)rd16(os2->p + 74), wd = (int)rd16(os2->p + 76); if (wa > 0) { int g2 = asc + desc + gap - wa - wd; asc = wa; desc = wd; gap = g2 > 0 ? g2 : 0; } }
    r->asc = asc; r->desc = desc; r->gap = gap;
    r->n = ng;
    r->g = g;
    r->d = 0;
    g_ndyn++;
    pool_resume(sv0);
    return nwe_nfaces + i;
}
