#include "web/nwe.h"

typedef struct gent {
    int face, sq, cp;
    int w, h, ox, oy;
    unsigned char* a;
    struct gent* next;
} gent;

#define GHASH 4096
static gent* gh[GHASH];
static short* advidx[64];

void font_cache_reset(void) {
    memset(gh, 0, sizeof(gh));
    memset(advidx, 0, sizeof(advidx));
}

static int gfind(int face, int cp) {
    const nwe_facerec* f = nwe_face(face);
    if (cp < 0x500) {
        if (!advidx[face]) {
            int sv = pool_suspend();
            advidx[face] = nalloc(sizeof(short) * 0x500);
            pool_resume(sv);
            for (int i = 0; i < 0x500; i++) advidx[face][i] = -1;
            for (int i = 0; i < f->n; i++) if (f->g[i].cp < 0x500) advidx[face][f->g[i].cp] = (short)i;
        }
        return advidx[face][cp];
    }
    int lo = 0, hi = f->n - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (f->g[m].cp == cp) return m;
        if (f->g[m].cp < cp) lo = m + 1; else hi = m - 1;
    }
    return -1;
}

static int map_cp(int* facep, int cp) {
    int face = *facep;
    if (cp == 0xA0) cp = ' ';
    int g = gfind(face, cp);
    if (g >= 0) return g;
    if (cp == 0x2028 || cp == 0x200B || cp == 0xFEFF || cp == 0x200D || cp == 0x200C || cp == 0xAD || (cp >= 0xFE00 && cp <= 0xFE0F)) return -2;
    int fb = face >= nwe_nfaces ? 0 : (face == 1 || face == 3 || face == 5 || face == 8) ? 1 : 0;
    if (fb != face) { g = gfind(fb, cp); if (g >= 0) { *facep = fb; return g; } }
    if (fb == 1) { g = gfind(0, cp); if (g >= 0) { *facep = 0; return g; } }
    return gfind(face, '?');
}

void font_metrics(int face, float size, fontm* m) {
    const nwe_facerec* f = nwe_face(face);
    float sc = size / (float)f->upm;
    m->face = face;
    m->size = size;
    m->ascent = (int)ceilf(f->asc * sc);
    m->descent = (int)ceilf(f->desc * sc);
    m->lineh = (int)ceilf((f->asc + f->desc + f->gap) * sc);
    m->xh = size * 0.52f;
}

float font_adv(int face, float size, int cp) {
    if (cp == '\t') cp = ' ';
    int g = map_cp(&face, cp);
    const nwe_facerec* f = nwe_face(face);
    if (g == -2) return 0;
    if (g < 0) return size * 0.5f;
    return f->g[g].adv * size / (float)f->upm;
}

float text_width(int face, float size, const char* s, int n, float ls) {
    float w = 0;
    char shp[1024];
    if (face >= nwe_nfaces && n < 1000) { int k = ttf_shape(face, s, n, shp, (int)sizeof shp); if (k >= 0) { s = shp; n = k; } }
    int i = 0;
    while (i < n) {
        int cp;
        int k = u8dec(s + i, n - i, &cp);
        if (k <= 0) break;
        i += k;
        w += font_adv(face, size, cp) + ls;
    }
    return w;
}

void nwe_acc_line(float* acc, int w, int h, float x0, float y0, float x1, float y1) {
    if (y0 == y1) return;
    float dir = 1;
    if (y0 > y1) { dir = -1; float t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    float dxdy = (x1 - x0) / (y1 - y0);
    float x = x0;
    if (y0 < 0) x -= y0 * dxdy;
    int ys = y0 < 0 ? 0 : (int)y0;
    int ye = (int)ceilf(y1);
    if (ye > h) ye = h;
    for (int y = ys; y < ye; y++) {
        int ls = y * w;
        float ya = y > y0 ? (float)y : y0;
        float yb = (float)(y + 1) < y1 ? (float)(y + 1) : y1;
        float dy = yb - ya;
        float xn = x + dxdy * dy;
        float d = dy * dir;
        float xa = x < xn ? x : xn, xb = x < xn ? xn : x;
        if (xa < 0) xa = 0;
        if (xb < 0) xb = 0;
        float xaf = floorf(xa);
        int xai = (int)xaf;
        float xbc = ceilf(xb);
        int xbi = (int)xbc;
        if (xbi > w - 1) xbi = w - 1;
        if (xai > w - 2) xai = w - 2;
        if (xbi <= xai + 1) {
            float xmf = 0.5f * (x + xn) - xaf;
            if (xmf < 0) xmf = 0;
            if (xmf > 1) xmf = 1;
            acc[ls + xai] += d - d * xmf;
            acc[ls + xai + 1] += d * xmf;
        } else {
            float s = 1.0f / (xb - xa);
            float x0f = xa - xaf;
            float a0 = 0.5f * s * (1 - x0f) * (1 - x0f);
            float x1f = xb - xbc + 1;
            float am = 0.5f * s * x1f * x1f;
            acc[ls + xai] += d * a0;
            if (xbi == xai + 2) acc[ls + xai + 1] += d * (1 - a0 - am);
            else {
                float a1 = s * (1.5f - x0f);
                acc[ls + xai + 1] += d * (a1 - a0);
                for (int xi = xai + 2; xi < xbi - 1; xi++) acc[ls + xi] += d * s;
                float a2 = a1 + (xbi - xai - 3) * s;
                acc[ls + xbi - 1] += d * (1 - a2 - am);
            }
            acc[ls + xbi] += d * am;
        }
        x = xn;
    }
}

typedef struct { float* acc; int w, h; float sc, ox, oy, cx, cy, sx, sy; } rctx;
static void rl(rctx* r, float x, float y) {
    float px = x * r->sc - r->ox, py = r->oy - y * r->sc;
    nwe_acc_line(r->acc, r->w, r->h, r->cx, r->cy, px, py);
    r->cx = px;
    r->cy = py;
}
static void rq(rctx* r, float cx, float cy, float x, float y) {
    float px0 = (r->cx + r->ox) / r->sc, py0 = (r->oy - r->cy) / r->sc;
    float ddx = (px0 - 2 * cx + x) * r->sc, ddy = (py0 - 2 * cy + y) * r->sc;
    float dev = sqrtf(ddx * ddx + ddy * ddy);
    int n = 1 + (int)sqrtf(dev * 3);
    if (n > 24) n = 24;
    for (int i = 1; i <= n; i++) {
        float t = (float)i / n, u = 1 - t;
        rl(r, u * u * px0 + 2 * u * t * cx + t * t * x, u * u * py0 + 2 * u * t * cy + t * t * y);
    }
}
static void rc(rctx* r, float c1x, float c1y, float c2x, float c2y, float x, float y) {
    float px0 = (r->cx + r->ox) / r->sc, py0 = (r->oy - r->cy) / r->sc;
    float dx = (x - px0) * r->sc, dy = (y - py0) * r->sc;
    int n = 2 + (int)(sqrtf(sqrtf(dx * dx + dy * dy)) * 2);
    if (n > 32) n = 32;
    for (int i = 1; i <= n; i++) {
        float t = (float)i / n, u = 1 - t;
        float a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
        rl(r, a * px0 + b * c1x + c * c2x + d * x, a * py0 + b * c1y + c * c2y + d * y);
    }
}

static gent* glyph(int face, float size, int cp) {
    int sq = (int)(size * 4 + 0.5f);
    unsigned hsh = ((unsigned)face * 7919u + (unsigned)sq * 104729u + (unsigned)cp * 2654435761u) & (GHASH - 1);
    for (gent* e = gh[hsh]; e; e = e->next) if (e->face == face && e->sq == sq && e->cp == cp) return e;
    int sv = pool_suspend();
    gent* e = nzalloc(sizeof(gent));
    e->face = face; e->sq = sq; e->cp = cp;
    e->next = gh[hsh];
    gh[hsh] = e;
    int g = map_cp(&face, cp);
    if (g < 0) { pool_resume(sv); return e; }
    const nwe_facerec* f = nwe_face(face);
    const short* d = f->d ? f->d + f->g[g].off : ttf_outline(face, f->g[g].off);
    if (!d) { pool_resume(sv); return e; }
    float sc = (sq / 4.0f) / (float)f->upm;
    float mnx = 1e9f, mny = 1e9f, mxx = -1e9f, mxy = -1e9f;
    const short* p = d;
    int any = 0;
    while (*p != 5) {
        int op = *p++;
        int np = op == 0 || op == 1 ? 1 : op == 2 ? 2 : op == 3 ? 3 : 0;
        for (int i = 0; i < np; i++) {
            float x = p[0] * sc, y = p[1] * sc;
            if (x < mnx) mnx = x;
            if (x > mxx) mxx = x;
            if (y < mny) mny = y;
            if (y > mxy) mxy = y;
            p += 2;
            any = 1;
        }
    }
    if (!any) { pool_resume(sv); return e; }
    int ox = (int)floorf(mnx) - 1, oy = (int)ceilf(mxy) + 1;
    int w = (int)ceilf(mxx) - ox + 3, h = oy - (int)floorf(mny) + 2;
    if (w <= 0 || h <= 0 || w > 2000 || h > 2000) { pool_resume(sv); return e; }
    float* acc = nzalloc(sizeof(float) * (size_t)(w * h + 1));
    rctx r = { acc, w, h, sc, (float)ox, (float)oy, 0, 0, 0, 0 };
    p = d;
    while (*p != 5) {
        int op = *p++;
        if (op == 0) { r.cx = p[0] * sc - ox; r.cy = oy - p[1] * sc; r.sx = r.cx; r.sy = r.cy; p += 2; }
        else if (op == 1) { rl(&r, p[0], p[1]); p += 2; }
        else if (op == 2) { rq(&r, p[0], p[1], p[2], p[3]); p += 4; }
        else if (op == 3) { rc(&r, p[0], p[1], p[2], p[3], p[4], p[5]); p += 6; }
        else if (op == 4) {
            if (r.cx != r.sx || r.cy != r.sy) { nwe_acc_line(acc, w, h, r.cx, r.cy, r.sx, r.sy); r.cx = r.sx; r.cy = r.sy; }
        }
    }
    e->a = nalloc((size_t)(w * h));
    float s = 0;
    for (int i = 0; i < w * h; i++) {
        s += acc[i];
        float v = s < 0 ? -s : s;
        if (v > 1) v = 1;
        e->a[i] = (unsigned char)(v * 255 + 0.5f);
    }
    nfree(acc);
    e->w = w;
    e->h = h;
    e->ox = ox;
    e->oy = oy;
    pool_resume(sv);
    return e;
}

static inline unsigned blend(unsigned dst, unsigned col, int a) {
    unsigned rb = (((col & 0xFF00FF) * (unsigned)a) + ((dst & 0xFF00FF) * (unsigned)(255 - a)));
    unsigned g = (((col & 0x00FF00) * (unsigned)a) + ((dst & 0x00FF00) * (unsigned)(255 - a)));
    return (((rb + 0x800080) >> 8) & 0xFF00FF) | (((g + 0x008000) >> 8) & 0x00FF00);
}

void font_draw(unsigned* fb, int stride, int cx0, int cy0, int cx1, int cy1, int face, float size, float x, float y, const char* s, int n, unsigned col, float ls) {
    int ca = (int)(col >> 24);
    if (ca == 0) return;
    int by = (int)floorf(y + 0.5f);
    if (by - size * 1.5f > cy1 || by + size < cy0) return;
    char shp[1024];
    if (face >= nwe_nfaces && n < 1000) { int k = ttf_shape(face, s, n, shp, (int)sizeof shp); if (k >= 0) { s = shp; n = k; } }
    int i = 0;
    while (i < n) {
        int cp;
        int k = u8dec(s + i, n - i, &cp);
        if (k <= 0) break;
        i += k;
        float adv = font_adv(face, size, cp);
        if (cp != ' ' && cp != '\t' && cp != 0xA0) {
            gent* g = glyph(face, size, cp);
            if (g->a) {
                int gx = (int)floorf(x + 0.5f) + g->ox, gy = by - g->oy;
                if (gx < cx1 && gx + g->w > cx0 && gy < cy1 && gy + g->h > cy0) {
                    for (int r = 0; r < g->h; r++) {
                        int py = gy + r;
                        if (py < cy0 || py >= cy1) continue;
                        unsigned* row = fb + (size_t)py * (size_t)stride;
                        const unsigned char* ar = g->a + r * g->w;
                        for (int c = 0; c < g->w; c++) {
                            int px = gx + c;
                            if (px < cx0 || px >= cx1) continue;
                            int a = ar[c];
                            if (!a) continue;
                            if (ca < 255) a = a * ca / 255;
                            row[px] = a >= 255 ? (col & 0xFFFFFF) : blend(row[px], col, a);
                        }
                    }
                }
            }
        }
        x += adv + ls;
        if (x > cx1 + 4) break;
    }
}
