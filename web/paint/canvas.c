#include "web/layout.h"
#include "web/canvas.h"

static float* acc;
static int acc_cap;

void cv_reset(void) {
    acc = 0;
    acc_cap = 0;
}

nimg* cv_get(node* n, int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > 8192) w = 8192;
    if (h > 8192) h = 8192;
    if ((long long)w * h > 16 * 1024 * 1024) h = 16 * 1024 * 1024 / w;
    nimg* im = n->img;
    if (im && im->w == w && im->h == h && im->px) return im;
    if (!im) {
        int sv = pool_suspend();
        im = nzalloc(sizeof(nimg));
        im->url = nstrdup("");
        pool_resume(sv);
        n->img = im;
    }
    if (im->px) free(im->px);
    im->px = calloc((size_t)w * h, 4);
    im->w = im->px ? w : 0;
    im->h = im->px ? h : 0;
    im->state = 2;
    return im;
}

static unsigned over(unsigned d, unsigned s, float a) {
    float sa = (float)(s >> 24) / 255.0f * a;
    if (sa <= 0) return d;
    float da = (float)(d >> 24) / 255.0f;
    float oa = sa + da * (1 - sa);
    if (oa <= 0) return 0;
    float k1 = sa / oa, k2 = da * (1 - sa) / oa;
    int r = (int)(((s >> 16) & 255) * k1 + ((d >> 16) & 255) * k2 + 0.5f);
    int g = (int)(((s >> 8) & 255) * k1 + ((d >> 8) & 255) * k2 + 0.5f);
    int b = (int)((s & 255) * k1 + (d & 255) * k2 + 0.5f);
    int A = (int)(oa * 255 + 0.5f);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    if (A > 255) A = 255;
    return ((unsigned)A << 24) | ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
}

static unsigned col4(float r, float g, float b, float a) {
    r = r > 0 ? (r < 1 ? r : 1) : 0;
    g = g > 0 ? (g < 1 ? g : 1) : 0;
    b = b > 0 ? (b < 1 ? b : 1) : 0;
    a = a > 0 ? (a < 1 ? a : 1) : 0;
    int R = (int)(r * 255 + 0.5f), G2 = (int)(g * 255 + 0.5f), B = (int)(b * 255 + 0.5f), A = (int)(a * 255 + 0.5f);
    R = R < 0 ? 0 : R > 255 ? 255 : R;
    G2 = G2 < 0 ? 0 : G2 > 255 ? 255 : G2;
    B = B < 0 ? 0 : B > 255 ? 255 : B;
    A = A < 0 ? 0 : A > 255 ? 255 : A;
    return ((unsigned)A << 24) | ((unsigned)R << 16) | ((unsigned)G2 << 8) | (unsigned)B;
}

static unsigned paint_at(const float* p, int np, float x, float y) {
    int t = np > 0 ? (p[0] == 1 ? 1 : p[0] == 2 ? 2 : 0) : 0;
    if (t == 0 || np < 5) return np >= 5 ? col4(p[1], p[2], p[3], p[4]) : 0xFF000000;
    float tt;
    const float* st;
    int ns;
    if (t == 1) {
        if (np < 6) return 0;
        float dx = p[3] - p[1], dy = p[4] - p[2], L = dx * dx + dy * dy;
        tt = L > 0 ? ((x - p[1]) * dx + (y - p[2]) * dy) / L : 0;
        ns = p[5] > 0 && p[5] < 1e6f ? (int)p[5] : 0;
        if (ns > (np - 6) / 5) ns = (np - 6) / 5;
        st = p + 6;
    } else {
        if (np < 8) return 0;
        float dx = x - p[4], dy = y - p[5], d = sqrtf(dx * dx + dy * dy);
        float r0 = p[3], r1 = p[6];
        tt = r1 - r0 != 0 ? (d - r0) / (r1 - r0) : 0;
        ns = p[7] > 0 && p[7] < 1e6f ? (int)p[7] : 0;
        if (ns > (np - 8) / 5) ns = (np - 8) / 5;
        st = p + 8;
    }
    if (ns <= 0) return 0;
    if (tt <= st[0]) return col4(st[1], st[2], st[3], st[4]);
    for (int i = 1; i < ns; i++) {
        const float* a = st + (i - 1) * 5;
        const float* b = st + i * 5;
        if (tt <= b[0]) {
            float k = b[0] - a[0] > 0 ? (tt - a[0]) / (b[0] - a[0]) : 0;
            return col4(a[1] + (b[1] - a[1]) * k, a[2] + (b[2] - a[2]) * k, a[3] + (b[3] - a[3]) * k, a[4] + (b[4] - a[4]) * k);
        }
    }
    const float* z = st + (ns - 1) * 5;
    return col4(z[1], z[2], z[3], z[4]);
}

static void acc_line(float* A, int W, int H, float x0, float y0, float x1, float y1) {
    if (y0 == y1) return;
    float dir = 1;
    if (y0 > y1) { float t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; dir = -1; }
    float dxdy = (x1 - x0) / (y1 - y0);
    float x = x0;
    if (y0 < 0) { x -= y0 * dxdy; y0 = 0; }
    if (y1 > H) y1 = (float)H;
    if (y0 >= y1) return;
    int ys = (int)y0, ye = (int)ceilf(y1);
    for (int y = ys; y < ye; y++) {
        int ls = y * (W + 2);
        float ya = y > y0 ? (float)y : y0, yb = y + 1 < y1 ? (float)(y + 1) : y1;
        float dy = yb - ya;
        float xn = x + dxdy * dy;
        float d = dy * dir;
        float xa = x < xn ? x : xn, xb = x < xn ? xn : x;
        if (xa < 0) xa = 0;
        if (xb < 0) xb = 0;
        if (xa > W) xa = (float)W;
        if (xb > W) xb = (float)W;
        float xaf = floorf(xa);
        int xai = (int)xaf;
        float xbc = ceilf(xb);
        int xbi = (int)xbc;
        if (xbi <= xai + 1) {
            float xm = 0.5f * (xa + xb) - xaf;
            A[ls + xai] += d - d * xm;
            A[ls + xai + 1] += d * xm;
        } else {
            float s = 1.0f / (xb - xa);
            float f0 = xa - xaf;
            float a0 = 0.5f * s * (1 - f0) * (1 - f0);
            float f1 = xb - xbc + 1;
            float am = 0.5f * s * f1 * f1;
            A[ls + xai] += d * a0;
            if (xbi == xai + 2) A[ls + xai + 1] += d * (1 - a0 - am);
            else {
                float a1 = s * (1.5f - f0);
                A[ls + xai + 1] += d * (a1 - a0);
                for (int xi = xai + 2; xi < xbi - 1; xi++) A[ls + xi] += d * s;
                float a2 = a1 + (float)(xbi - xai - 3) * s;
                A[ls + xbi - 1] += d * (1 - a2 - am);
            }
            A[ls + xbi] += d * am;
        }
        x = xn;
    }
}

void cv_fill(nimg* im, const float* pts, const int* cnt, int nsub, const float* pnt, int np, float alpha, int mode, const int* clip) {
    if (!im || !im->px || nsub <= 0) return;
    float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f;
    int tot = 0;
    for (int s = 0; s < nsub; s++) tot += cnt[s];
    for (int i = 0; i < tot; i++) {
        float x = pts[i * 2], y = pts[i * 2 + 1];
        if (!(x > -1e7f && x < 1e7f && y > -1e7f && y < 1e7f)) return;
        if (x < bx0) bx0 = x;
        if (x > bx1) bx1 = x;
        if (y < by0) by0 = y;
        if (y > by1) by1 = y;
    }
    int cx0 = clip ? clip[0] : 0, cy0 = clip ? clip[1] : 0, cx1 = clip ? clip[2] : im->w, cy1 = clip ? clip[3] : im->h;
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 > im->w) cx1 = im->w;
    if (cy1 > im->h) cy1 = im->h;
    int X0 = (int)floorf(bx0), Y0 = (int)floorf(by0), X1 = (int)ceilf(bx1) + 1, Y1 = (int)ceilf(by1) + 1;
    if (X0 < cx0) X0 = cx0;
    if (Y0 < cy0) Y0 = cy0;
    if (X1 > cx1) X1 = cx1;
    if (Y1 > cy1) Y1 = cy1;
    if (X0 >= X1 || Y0 >= Y1) return;
    int W = X1 - X0, H = Y1 - Y0;
    long long need = (long long)(W + 2) * H;
    if (need > acc_cap) {
        if (acc) free(acc);
        acc = malloc((size_t)need * sizeof(float));
        acc_cap = acc ? (int)need : 0;
        if (!acc) return;
    }
    memset(acc, 0, (size_t)need * sizeof(float));
    int k = 0;
    for (int s = 0; s < nsub; s++) {
        int c = cnt[s];
        for (int i = 0; i < c; i++) {
            int j = i + 1 < c ? i + 1 : 0;
            acc_line(acc, W, H, pts[(k + i) * 2] - X0, pts[(k + i) * 2 + 1] - Y0, pts[(k + j) * 2] - X0, pts[(k + j) * 2 + 1] - Y0);
        }
        k += c;
    }
    if (!(alpha >= 0)) return;
    if (alpha > 1) alpha = 1;
    int solid = np >= 5 && pnt[0] != 1 && pnt[0] != 2;
    unsigned sc = solid ? paint_at(pnt, np, 0, 0) : 0;
    for (int y = 0; y < H; y++) {
        float sum = 0;
        unsigned* row = im->px + (size_t)(y + Y0) * im->w + X0;
        float* a = acc + (size_t)y * (W + 2);
        for (int x = 0; x < W; x++) {
            sum += a[x];
            float cov = sum < 0 ? -sum : sum;
            if (cov > 1) cov = 1;
            if (cov < 0.002f) continue;
            if (mode == 1) {
                unsigned d = row[x];
                int na = (int)((d >> 24) * (1 - cov) + 0.5f);
                row[x] = na <= 0 ? 0 : (((unsigned)na << 24) | (d & 0xFFFFFF));
            } else {
                unsigned c = solid ? sc : paint_at(pnt, np, x + X0 + 0.5f, y + Y0 + 0.5f);
                row[x] = over(row[x], c, cov * alpha);
            }
        }
    }
}

void cv_text(nimg* im, const char* s, int n, float x, float base, int face, float size, const float* pnt, int np, float alpha, const int* clip) {
    if (!im || !im->px || n <= 0 || !(size > 0 && size < 2000) || !(x > -1e6f && x < 1e6f && base > -1e6f && base < 1e6f)) return;
    float tw = text_width(face, size, s, n, 0);
    if (!(tw >= 0 && tw < 1e6f)) return;
    int X0 = (int)floorf(x) - 2, Y0 = (int)floorf(base - size * 1.2f) - 2;
    int W = (int)ceilf(tw) + 6, H = (int)ceilf(size * 1.7f) + 6;
    int cx0 = clip ? clip[0] : 0, cy0 = clip ? clip[1] : 0, cx1 = clip ? clip[2] : im->w, cy1 = clip ? clip[3] : im->h;
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 > im->w) cx1 = im->w;
    if (cy1 > im->h) cy1 = im->h;
    if (W <= 0 || H <= 0 || (long long)W * H > 4 * 1024 * 1024) return;
    if (!(alpha >= 0)) return;
    if (alpha > 1) alpha = 1;
    unsigned* t = calloc((size_t)W * H, 4);
    if (!t) return;
    font_draw(t, W, 0, 0, W, H, face, size, x - X0, base - Y0, s, n, 0xFFFFFFFF, 0);
    for (int y = 0; y < H; y++) {
        int py = y + Y0;
        if (py < cy0 || py >= cy1) continue;
        for (int xx = 0; xx < W; xx++) {
            int px = xx + X0;
            if (px < cx0 || px >= cx1) continue;
            unsigned v = t[(size_t)y * W + xx];
            int g = (int)((v >> 8) & 255);
            if (!g) continue;
            unsigned c = paint_at(pnt, np, px + 0.5f, py + 0.5f);
            unsigned* d = im->px + (size_t)py * im->w + px;
            *d = over(*d, c, alpha * (float)g / 255.0f);
        }
    }
    free(t);
}

void cv_image(nimg* dst, const nimg* src, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, float alpha, const int* clip) {
    if (!dst || !dst->px || !src || !src->px || src->w <= 0 || src->h <= 0 || sw == 0 || sh == 0 || dw == 0 || dh == 0) return;
    if (!(sx > -1e6f && sx < 1e6f && sy > -1e6f && sy < 1e6f && sw > -1e6f && sw < 1e6f && sh > -1e6f && sh < 1e6f && dx > -1e6f && dx < 1e6f && dy > -1e6f && dy < 1e6f && dw > -1e6f && dw < 1e6f && dh > -1e6f && dh < 1e6f)) return;
    if (fabsf(dw) < 0.01f || fabsf(dh) < 0.01f || fabsf(sw) < 0.01f || fabsf(sh) < 0.01f) return;
    if (dw < 0) { dx += dw; dw = -dw; sx += sw; sw = -sw; }
    if (dh < 0) { dy += dh; dh = -dh; sy += sh; sh = -sh; }
    int cx0 = clip ? clip[0] : 0, cy0 = clip ? clip[1] : 0, cx1 = clip ? clip[2] : dst->w, cy1 = clip ? clip[3] : dst->h;
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 > dst->w) cx1 = dst->w;
    if (cy1 > dst->h) cy1 = dst->h;
    int x0 = (int)floorf(dx), y0 = (int)floorf(dy), x1 = (int)ceilf(dx + dw), y1 = (int)ceilf(dy + dh);
    if (x0 < cx0) x0 = cx0;
    if (y0 < cy0) y0 = cy0;
    if (x1 > cx1) x1 = cx1;
    if (y1 > cy1) y1 = cy1;
    unsigned* tmp = 0;
    const unsigned* spx = src->px;
    if (src == dst) {
        tmp = malloc((size_t)src->w * src->h * 4);
        if (!tmp) return;
        memcpy(tmp, src->px, (size_t)src->w * src->h * 4);
        spx = tmp;
    }
    if (!(alpha >= 0)) { if (tmp) free(tmp); return; }
    if (alpha > 1) alpha = 1;
    float kx = sw / dw, ky = sh / dh;
    for (int y = y0; y < y1; y++) {
        int iy = (int)floorf(sy + (y + 0.5f - dy) * ky);
        if (iy < 0 || iy >= src->h) continue;
        unsigned* row = dst->px + (size_t)y * dst->w;
        for (int x = x0; x < x1; x++) {
            int ix = (int)floorf(sx + (x + 0.5f - dx) * kx);
            if (ix < 0 || ix >= src->w) continue;
            row[x] = over(row[x], spx[(size_t)iy * src->w + ix], alpha);
        }
    }
    if (tmp) free(tmp);
}

void cv_getdata(const nimg* im, int x, int y, int w, int h, unsigned char* out) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            unsigned char* o = out + ((size_t)j * w + i) * 4;
            int px = x + i, py = y + j;
            if (!im || !im->px || px < 0 || py < 0 || px >= im->w || py >= im->h) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
            unsigned v = im->px[(size_t)py * im->w + px];
            o[0] = (unsigned char)(v >> 16);
            o[1] = (unsigned char)(v >> 8);
            o[2] = (unsigned char)v;
            o[3] = (unsigned char)(v >> 24);
        }
}

void cv_putdata(nimg* im, const unsigned char* in, int w, int h, int x, int y) {
    if (!im || !im->px) return;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int px = x + i, py = y + j;
            if (px < 0 || py < 0 || px >= im->w || py >= im->h) continue;
            const unsigned char* p = in + ((size_t)j * w + i) * 4;
            im->px[(size_t)py * im->w + px] = ((unsigned)p[3] << 24) | ((unsigned)p[0] << 16) | ((unsigned)p[1] << 8) | (unsigned)p[2];
        }
}
