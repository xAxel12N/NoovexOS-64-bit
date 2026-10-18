#include "web/nwe.h"

typedef unsigned char u8;
int brotli_decode(const u8* src, int slen, u8* dst, int dcap);

static const char* const KT[63] = { "cmap", "head", "hhea", "hmtx", "maxp", "name", "OS/2", "post", "cvt ", "fpgm", "glyf", "loca", "prep", "CFF ", "VORG", "EBDT", "EBLC", "gasp", "hdmx", "kern", "LTSH", "PCLT", "VDMX", "vhea", "vmtx", "BASE", "GDEF", "GPOS", "GSUB", "EBSC", "JSTF", "MATH", "CBDT", "CBLC", "COLR", "CPAL", "SVG ", "sbix", "acnt", "avar", "bdat", "bloc", "bsln", "cvar", "fdsc", "feat", "fmtx", "fvar", "gvar", "hsty", "just", "lcar", "mort", "morx", "opbd", "prop", "trak", "Zapf", "Silf", "Glat", "Gloc", "Feat", "Sill" };

typedef struct { const u8* p; int n, i, err; } rd;
static unsigned g8(rd* r) { if (r->i >= r->n) { r->err = 1; return 0; } return r->p[r->i++]; }
static unsigned g16(rd* r) { unsigned a = g8(r); return (a << 8) | g8(r); }
static unsigned g32(rd* r) { unsigned a = g16(r); return (a << 16) | g16(r); }
static unsigned gb128(rd* r) {
    unsigned v = 0;
    for (int i = 0; i < 5; i++) {
        unsigned b = g8(r);
        if (i == 0 && b == 0x80) { r->err = 1; return 0; }
        if (v & 0xFE000000u) { r->err = 1; return 0; }
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) return v;
    }
    r->err = 1;
    return 0;
}
static unsigned g255(rd* r) {
    unsigned c = g8(r);
    if (c == 253) return g16(r);
    if (c == 255) return g8(r) + 253;
    if (c == 254) return g8(r) + 506;
    return c;
}

typedef struct { u8* p; int n, cap, err; } wb;
static void w8(wb* w, unsigned v) { if (w->n >= w->cap) { w->err = 1; return; } w->p[w->n++] = (u8)v; }
static void w16(wb* w, unsigned v) { w8(w, v >> 8); w8(w, v); }
static void w32(wb* w, unsigned v) { w16(w, v >> 16); w16(w, v); }

static int ws(int f, int v) { return (f & 1) ? v : -v; }

static int rebuild_glyf(const u8* t, int tl, u8** gout, int* glen, u8** lout, int* llen, short** xmins, int* nglyph) {
    rd h = { t, tl, 0, 0 };
    g16(&h);
    unsigned opt = g16(&h);
    int ng = (int)g16(&h);
    g16(&h);
    unsigned sz[7];
    for (int i = 0; i < 7; i++) sz[i] = g32(&h);
    if (h.err) return -1;
    unsigned off = (unsigned)h.i;
    rd st[7];
    for (int i = 0; i < 7; i++) {
        if (off + sz[i] > (unsigned)tl || off + sz[i] < off) return -1;
        st[i].p = t + off; st[i].n = (int)sz[i]; st[i].i = 0; st[i].err = 0;
        off += sz[i];
    }
    (void)opt;
    rd* nc = &st[0]; rd* np = &st[1]; rd* fl = &st[2]; rd* gl = &st[3]; rd* cs = &st[4]; rd* bb = &st[5]; rd* ins = &st[6];
    int bml = ((ng + 31) >> 5) << 2;
    if (bb->n < bml) return -1;
    const u8* bmap = bb->p;
    bb->i = bml;
    int cap = tl * 4 + ng * 64 + 4096;
    wb g = { nalloc((size_t)cap), 0, cap, 0 };
    u8* loca = nalloc((size_t)(ng + 1) * 4);
    short* xm = nalloc(sizeof(short) * (size_t)(ng + 1));
    static unsigned short ends[16384];
    static int xs[65536], ys[65536];
    static u8 on[65536];
    for (int gi = 0; gi < ng; gi++) {
        loca[gi * 4] = (u8)(g.n >> 24); loca[gi * 4 + 1] = (u8)(g.n >> 16); loca[gi * 4 + 2] = (u8)(g.n >> 8); loca[gi * 4 + 3] = (u8)g.n;
        xm[gi] = 0;
        int ncont = (short)g16(nc);
        int hasbb = (bmap[gi >> 3] >> (7 - (gi & 7))) & 1;
        if (ncont == 0) { if (hasbb) return -1; continue; }
        if (ncont > 0) {
            if (ncont > 16384) return -1;
            int tot = 0;
            for (int c = 0; c < ncont; c++) { int n = (int)g255(np); tot += n; if (tot > 65536) return -1; ends[c] = (unsigned short)(tot - 1); }
            int x = 0, y = 0;
            for (int i = 0; i < tot; i++) {
                int f = (int)g8(fl);
                on[i] = !(f & 0x80);
                f &= 0x7F;
                int dx, dy;
                const u8* in = gl->p + gl->i;
                int nb = f < 84 ? 1 : f < 120 ? 2 : f < 124 ? 3 : 4;
                if (gl->i + nb > gl->n) return -1;
                if (f < 10) { dx = 0; dy = ws(f, ((f & 14) << 7) + in[0]); }
                else if (f < 20) { dx = ws(f, (((f - 10) & 14) << 7) + in[0]); dy = 0; }
                else if (f < 84) { int b0 = f - 20, b1 = in[0]; dx = ws(f, 1 + (b0 & 0x30) + (b1 >> 4)); dy = ws(f >> 1, 1 + ((b0 & 0x0C) << 2) + (b1 & 0x0F)); }
                else if (f < 120) { int b0 = f - 84; dx = ws(f, 1 + ((b0 / 12) << 8) + in[0]); dy = ws(f >> 1, 1 + (((b0 % 12) >> 2) << 8) + in[1]); }
                else if (f < 124) { int b2 = in[1]; dx = ws(f, (in[0] << 4) + (b2 >> 4)); dy = ws(f >> 1, ((b2 & 0x0F) << 8) + in[2]); }
                else { dx = ws(f, (in[0] << 8) + in[1]); dy = ws(f >> 1, (in[2] << 8) + in[3]); }
                gl->i += nb;
                x += dx; y += dy;
                xs[i] = x; ys[i] = y;
            }
            int il = (int)g255(gl);
            if (ins->i + il > ins->n) return -1;
            int x0, y0, x1, y1;
            if (hasbb) { x0 = (short)g16(bb); y0 = (short)g16(bb); x1 = (short)g16(bb); y1 = (short)g16(bb); }
            else { x0 = y0 = 32767; x1 = y1 = -32768; for (int i = 0; i < tot; i++) { if (xs[i] < x0) x0 = xs[i]; if (xs[i] > x1) x1 = xs[i]; if (ys[i] < y0) y0 = ys[i]; if (ys[i] > y1) y1 = ys[i]; } if (!tot) x0 = y0 = x1 = y1 = 0; }
            xm[gi] = (short)x0;
            w16(&g, (unsigned)ncont); w16(&g, (unsigned)(x0 & 0xFFFF)); w16(&g, (unsigned)(y0 & 0xFFFF)); w16(&g, (unsigned)(x1 & 0xFFFF)); w16(&g, (unsigned)(y1 & 0xFFFF));
            for (int c = 0; c < ncont; c++) w16(&g, ends[c]);
            w16(&g, (unsigned)il);
            for (int i = 0; i < il; i++) w8(&g, ins->p[ins->i + i]);
            ins->i += il;
            for (int i = 0; i < tot; i++) w8(&g, on[i] ? 1 : 0);
            int px = 0;
            for (int i = 0; i < tot; i++) { w16(&g, (unsigned)((xs[i] - px) & 0xFFFF)); px = xs[i]; }
            int py = 0;
            for (int i = 0; i < tot; i++) { w16(&g, (unsigned)((ys[i] - py) & 0xFFFF)); py = ys[i]; }
        } else {
            if (!hasbb) return -1;
            int x0 = (short)g16(bb), y0 = (short)g16(bb), x1 = (short)g16(bb), y1 = (short)g16(bb);
            xm[gi] = (short)x0;
            w16(&g, 0xFFFF); w16(&g, (unsigned)(x0 & 0xFFFF)); w16(&g, (unsigned)(y0 & 0xFFFF)); w16(&g, (unsigned)(x1 & 0xFFFF)); w16(&g, (unsigned)(y1 & 0xFFFF));
            int hasins = 0;
            for (int guard = 0; guard < 4096; guard++) {
                unsigned f = g16(cs);
                unsigned gid = g16(cs);
                w16(&g, f); w16(&g, gid);
                int n = (f & 1) ? 4 : 2;
                if (f & 8) n += 2; else if (f & 0x40) n += 4; else if (f & 0x80) n += 8;
                for (int i = 0; i < n; i++) w8(&g, g8(cs));
                if (f & 0x100) hasins = 1;
                if (!(f & 0x20)) break;
                if (cs->err) return -1;
            }
            if (hasins) {
                int il = (int)g255(gl);
                if (ins->i + il > ins->n) return -1;
                w16(&g, (unsigned)il);
                for (int i = 0; i < il; i++) w8(&g, ins->p[ins->i + i]);
                ins->i += il;
            }
        }
        while (g.n & 3) w8(&g, 0);
        if (g.err) return -1;
    }
    loca[ng * 4] = (u8)(g.n >> 24); loca[ng * 4 + 1] = (u8)(g.n >> 16); loca[ng * 4 + 2] = (u8)(g.n >> 8); loca[ng * 4 + 3] = (u8)g.n;
    for (int i = 0; i < 7; i++) if (st[i].err) return -1;
    *gout = g.p; *glen = g.n;
    *lout = loca; *llen = (ng + 1) * 4;
    *xmins = xm;
    *nglyph = ng;
    return 0;
}

static int rebuild_hmtx(const u8* t, int tl, int ng, int nhm, const short* xm, u8** out, int* olen) {
    rd r = { t, tl, 0, 0 };
    unsigned fl = g8(&r);
    if (nhm < 1 || nhm > ng) return -1;
    int n = 4 * nhm + 2 * (ng - nhm);
    u8* o = nalloc((size_t)n);
    unsigned short* adv = nalloc(sizeof(unsigned short) * (size_t)nhm);
    for (int i = 0; i < nhm; i++) adv[i] = (unsigned short)g16(&r);
    for (int i = 0; i < nhm; i++) {
        int lsb = (fl & 1) ? (xm ? xm[i] : 0) : (short)g16(&r);
        o[i * 4] = (u8)(adv[i] >> 8); o[i * 4 + 1] = (u8)adv[i]; o[i * 4 + 2] = (u8)(lsb >> 8); o[i * 4 + 3] = (u8)lsb;
    }
    for (int i = nhm; i < ng; i++) {
        int lsb = (fl & 2) ? (xm ? xm[i] : 0) : (short)g16(&r);
        o[4 * nhm + (i - nhm) * 2] = (u8)(lsb >> 8); o[4 * nhm + (i - nhm) * 2 + 1] = (u8)lsb;
    }
    if (r.err) return -1;
    *out = o;
    *olen = n;
    return 0;
}

int woff2_to_sfnt(const u8* src, int len, u8** out, int* outlen) {
    rd r = { src, len, 0, 0 };
    if (g32(&r) != 0x774F4632u) return -1;
    unsigned flavor = g32(&r);
    g32(&r);
    int nt = (int)g16(&r);
    g16(&r);
    unsigned total = g32(&r);
    unsigned comp = g32(&r);
    r.i = 48;
    if (r.err || nt <= 0 || nt > 128 || flavor == 0x74746366u) return -1;
    unsigned tag[128], olen2[128], tlen[128];
    int xf[128];
    unsigned sum = 0;
    for (int i = 0; i < nt; i++) {
        unsigned f = g8(&r);
        if ((f & 63) == 63) tag[i] = g32(&r);
        else { const char* k = KT[f & 63]; tag[i] = ((unsigned)k[0] << 24) | ((unsigned)k[1] << 16) | ((unsigned)k[2] << 8) | (unsigned)k[3]; }
        int ver = (int)(f >> 6);
        olen2[i] = gb128(&r);
        int isgl = tag[i] == 0x676C7966u || tag[i] == 0x6C6F6361u;
        xf[i] = isgl ? ver == 0 : ver != 0;
        tlen[i] = xf[i] ? gb128(&r) : olen2[i];
        sum += tlen[i];
        if (r.err) return -1;
    }
    if ((unsigned)r.i + comp > (unsigned)len || sum > (64u << 20)) return -1;
    u8* data = nalloc(sum + 16);
    if (brotli_decode(src + r.i, (int)comp, data, (int)sum) != (int)sum) return -1;
    const u8* tp[128];
    unsigned off = 0;
    for (int i = 0; i < nt; i++) { tp[i] = data + off; off += tlen[i]; }
    int gi = -1, li = -1, hi = -1, hhi = -1, mi = -1, hdi = -1;
    for (int i = 0; i < nt; i++) {
        if (tag[i] == 0x676C7966u) gi = i;
        else if (tag[i] == 0x6C6F6361u) li = i;
        else if (tag[i] == 0x686D7478u) hi = i;
        else if (tag[i] == 0x68686561u) hhi = i;
        else if (tag[i] == 0x6D617870u) mi = i;
        else if (tag[i] == 0x68656164u) hdi = i;
    }
    const u8* np[128];
    unsigned nl[128];
    for (int i = 0; i < nt; i++) { np[i] = tp[i]; nl[i] = tlen[i]; }
    short* xm = 0;
    int ng = 0;
    if (gi >= 0 && xf[gi]) {
        u8 *g, *l;
        int gl2, ll;
        if (rebuild_glyf(tp[gi], (int)tlen[gi], &g, &gl2, &l, &ll, &xm, &ng) < 0) return -1;
        np[gi] = g; nl[gi] = (unsigned)gl2;
        if (li >= 0) { np[li] = l; nl[li] = (unsigned)ll; }
    }
    if (hi >= 0 && xf[hi]) {
        if (hhi < 0 || mi < 0 || tlen[hhi] < 36 || tlen[mi] < 6) return -1;
        int nhm = (tp[hhi][34] << 8) | tp[hhi][35];
        int ngl = (tp[mi][4] << 8) | tp[mi][5];
        u8* o;
        int ol;
        if (rebuild_hmtx(tp[hi], (int)tlen[hi], ngl, nhm, xm, &o, &ol) < 0) return -1;
        np[hi] = o; nl[hi] = (unsigned)ol;
    }
    u8* headcopy = 0;
    if (hdi >= 0 && gi >= 0 && xf[gi] && nl[hdi] >= 54) { headcopy = nalloc(nl[hdi]); memcpy(headcopy, np[hdi], nl[hdi]); headcopy[50] = 0; headcopy[51] = 1; np[hdi] = headcopy; }
    unsigned outsz = 12 + 16 * (unsigned)nt;
    for (int i = 0; i < nt; i++) outsz += (nl[i] + 3) & ~3u;
    (void)total;
    u8* o = nalloc(outsz);
    memset(o, 0, outsz);
    wb w = { o, 0, (int)outsz, 0 };
    w32(&w, flavor);
    w16(&w, (unsigned)nt);
    w16(&w, 0); w16(&w, 0); w16(&w, 0);
    unsigned doff = 12 + 16 * (unsigned)nt;
    for (int i = 0; i < nt; i++) {
        w32(&w, tag[i]); w32(&w, 0); w32(&w, doff); w32(&w, nl[i]);
        memcpy(o + doff, np[i], nl[i]);
        doff += (nl[i] + 3) & ~3u;
    }
    *out = o;
    *outlen = (int)outsz;
    return 0;
}
