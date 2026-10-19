#include "web/layout.h"

typedef struct { int x0, y0, x1, y1; } clipr;
static unsigned* FB;
static int STR;
static float g_scroll;
static int g_vx, g_vy;

static inline unsigned blend(unsigned dst, unsigned col, int a) {
    unsigned rb = (((col & 0xFF00FF) * (unsigned)a) + ((dst & 0xFF00FF) * (unsigned)(255 - a)));
    unsigned g = (((col & 0x00FF00) * (unsigned)a) + ((dst & 0x00FF00) * (unsigned)(255 - a)));
    return (((rb + 0x800080) >> 8) & 0xFF00FF) | (((g + 0x008000) >> 8) & 0x00FF00);
}
static unsigned with_alpha(unsigned c, float op) {
    int a = (int)((c >> 24) * op + 0.5f);
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    return ((unsigned)a << 24) | (c & 0xFFFFFF);
}
static void span(int y, int x0, int x1, unsigned col, clipr* c) {
    if (y < c->y0 || y >= c->y1) return;
    if (x0 < c->x0) x0 = c->x0;
    if (x1 > c->x1) x1 = c->x1;
    if (x0 >= x1) return;
    int a = (int)(col >> 24);
    unsigned* row = FB + (size_t)y * (size_t)STR;
    if (a >= 255) { unsigned v = col & 0xFFFFFF; for (int x = x0; x < x1; x++) row[x] = v; }
    else if (a > 0) for (int x = x0; x < x1; x++) row[x] = blend(row[x], col, a);
}
static void pix(int x, int y, unsigned col, int a, clipr* c) {
    if (x < c->x0 || x >= c->x1 || y < c->y0 || y >= c->y1 || a <= 0) return;
    unsigned* p = FB + (size_t)y * (size_t)STR + x;
    int ca = (int)(col >> 24) * a / 255;
    if (ca >= 255) *p = col & 0xFFFFFF;
    else if (ca > 0) *p = blend(*p, col, ca);
}
static void rect(float x, float y, float w, float h, unsigned col, clipr* c) {
    if ((col >> 24) == 0 || w <= 0 || h <= 0) return;
    int x0 = (int)floorf(x + 0.5f), y0 = (int)floorf(y + 0.5f), x1 = (int)floorf(x + w + 0.5f), y1 = (int)floorf(y + h + 0.5f);
    if (x1 == x0 && w > 0.3f) x1 = x0 + 1;
    if (y1 == y0 && h > 0.3f) y1 = y0 + 1;
    if (y0 < c->y0) y0 = c->y0;
    if (y1 > c->y1) y1 = c->y1;
    for (int yy = y0; yy < y1; yy++) span(yy, x0, x1, col, c);
}
void fill_rect_a(unsigned* fb, int stride, int cx0, int cy0, int cx1, int cy1, float x, float y, float w, float h, unsigned col) {
    FB = fb;
    STR = stride;
    clipr c = { cx0, cy0, cx1, cy1 };
    rect(x, y, w, h, col, &c);
}

static float rr_cov(float px, float py, float x, float y, float w, float h, const float* r) {
    if (px < x - 0.5f || px > x + w + 0.5f || py < y - 0.5f || py > y + h + 0.5f) return 0;
    float cx, cy, rad;
    if (px < x + r[0] && py < y + r[0]) { cx = x + r[0]; cy = y + r[0]; rad = r[0]; }
    else if (px > x + w - r[1] && py < y + r[1]) { cx = x + w - r[1]; cy = y + r[1]; rad = r[1]; }
    else if (px > x + w - r[2] && py > y + h - r[2]) { cx = x + w - r[2]; cy = y + h - r[2]; rad = r[2]; }
    else if (px < x + r[3] && py > y + h - r[3]) { cx = x + r[3]; cy = y + h - r[3]; rad = r[3]; }
    else {
        float cvx = fminf(px - x + 0.5f, x + w - px + 0.5f), cvy = fminf(py - y + 0.5f, y + h - py + 0.5f);
        float v = fminf(cvx, cvy);
        return v > 1 ? 1 : v < 0 ? 0 : v;
    }
    float d = sqrtf((px - cx) * (px - cx) + (py - cy) * (py - cy));
    float v = rad - d + 0.5f;
    return v > 1 ? 1 : v < 0 ? 0 : v;
}
static int has_r(const float* r) { return r[0] > 0 || r[1] > 0 || r[2] > 0 || r[3] > 0; }
static void rrect(float x, float y, float w, float h, const float* r, unsigned col, clipr* c) {
    if ((col >> 24) == 0 || w <= 0 || h <= 0) return;
    if (!has_r(r)) { rect(x, y, w, h, col, c); return; }
    float mr = fmaxf(fmaxf(r[0], r[1]), fmaxf(r[2], r[3]));
    int y0 = (int)floorf(y), y1 = (int)ceilf(y + h);
    if (y0 < c->y0) y0 = c->y0;
    if (y1 > c->y1) y1 = c->y1;
    for (int yy = y0; yy < y1; yy++) {
        float py = yy + 0.5f;
        if (py > y + mr && py < y + h - mr) { span(yy, (int)floorf(x + 0.5f), (int)floorf(x + w + 0.5f), col, c); continue; }
        int xa = (int)floorf(x), xb = (int)ceilf(x + w);
        if (xa < c->x0) xa = c->x0;
        if (xb > c->x1) xb = c->x1;
        for (int xx = xa; xx < xb; xx++) {
            float px = xx + 0.5f;
            if (px > x + mr && px < x + w - mr) {
                int run_end = (int)floorf(x + w - mr);
                if (run_end > xb) run_end = xb;
                float cv = rr_cov(px, py, x, y, w, h, r);
                if (cv >= 1 && run_end > xx + 1) { span(yy, xx, run_end, col, c); xx = run_end - 1; continue; }
            }
            float cv = rr_cov(px, py, x, y, w, h, r);
            if (cv > 0) pix(xx, yy, col, (int)(cv * 255), c);
        }
    }
}
static void rring(float x, float y, float w, float h, const float* r, float bt, float br, float bb, float bl, unsigned col, clipr* c) {
    float ri[4] = { fmaxf(0, r[0] - fmaxf(bl, bt)), fmaxf(0, r[1] - fmaxf(br, bt)), fmaxf(0, r[2] - fmaxf(br, bb)), fmaxf(0, r[3] - fmaxf(bl, bb)) };
    float ix = x + bl, iy = y + bt, iw = w - bl - br, ih = h - bt - bb;
    int y0 = (int)floorf(y), y1 = (int)ceilf(y + h);
    if (y0 < c->y0) y0 = c->y0;
    if (y1 > c->y1) y1 = c->y1;
    for (int yy = y0; yy < y1; yy++) {
        float py = yy + 0.5f;
        int xa = (int)floorf(x), xb = (int)ceilf(x + w);
        if (xa < c->x0) xa = c->x0;
        if (xb > c->x1) xb = c->x1;
        for (int xx = xa; xx < xb; xx++) {
            float px = xx + 0.5f;
            if (px > ix + 1 + fmaxf(ri[0], ri[3]) && px < ix + iw - 1 - fmaxf(ri[1], ri[2]) && py > iy + 1 && py < iy + ih - 1) {
                int jump = (int)floorf(ix + iw - 1 - fmaxf(ri[1], ri[2]));
                if (jump > xx) xx = jump - 1;
                continue;
            }
            float co = rr_cov(px, py, x, y, w, h, r);
            if (co <= 0) continue;
            float ci = (iw > 0 && ih > 0) ? rr_cov(px, py, ix, iy, iw, ih, ri) : 0;
            float cv = co - ci;
            if (cv > 0) pix(xx, yy, col, (int)(cv * 255), c);
        }
    }
}

static unsigned lerpc(unsigned a, unsigned b, float t) {
    unsigned r = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        float va = (float)((a >> sh) & 255), vb = (float)((b >> sh) & 255);
        int v = (int)(va + (vb - va) * t + 0.5f);
        r |= (unsigned)(v < 0 ? 0 : v > 255 ? 255 : v) << sh;
    }
    return r;
}
static unsigned grad_at(bgimg* g, float t) {
    if (t <= g->stops[0].pos) return g->stops[0].col;
    for (int i = 1; i < g->nstops; i++) {
        if (t <= g->stops[i].pos) {
            float d = g->stops[i].pos - g->stops[i - 1].pos;
            float u = d > 0 ? (t - g->stops[i - 1].pos) / d : 1;
            unsigned a = g->stops[i - 1].col, b = g->stops[i].col;
            if ((a >> 24) == 0 && (b >> 24)) a = b & 0xFFFFFF;
            if ((b >> 24) == 0 && (a >> 24)) b = a & 0xFFFFFF;
            return lerpc(a, b, u);
        }
    }
    return g->stops[g->nstops - 1].col;
}
static void gradient(bgimg* g, float x, float y, float w, float h, const float* r, float op, clipr* c) {
    if (w <= 0 || h <= 0) return;
    int y0 = (int)floorf(y), y1 = (int)ceilf(y + h), x0 = (int)floorf(x), x1 = (int)ceilf(x + w);
    if (y0 < c->y0) y0 = c->y0;
    if (y1 > c->y1) y1 = c->y1;
    if (x0 < c->x0) x0 = c->x0;
    if (x1 > c->x1) x1 = c->x1;
    float ang = g->angle * 3.14159265f / 180;
    float dx = sinf(ang), dy = -cosf(ang);
    float len = fabsf(w * dx) + fabsf(h * dy);
    float cx = x + w / 2, cy = y + h / 2;
    int rounded = r && has_r(r);
    for (int yy = y0; yy < y1; yy++) {
        float py = yy + 0.5f;
        unsigned rowc = 0;
        int rowconst = g->kind == 1 && fabsf(dx) < 1e-4f;
        if (rowconst) { float t = ((py - cy) * dy) / (len > 0 ? len : 1) + 0.5f; rowc = with_alpha(grad_at(g, t), op); }
        for (int xx = x0; xx < x1; xx++) {
            float px = xx + 0.5f;
            unsigned col;
            if (rowconst) col = rowc;
            else if (g->kind == 1) { float t = ((px - cx) * dx + (py - cy) * dy) / (len > 0 ? len : 1) + 0.5f; col = with_alpha(grad_at(g, t), op); }
            else if (g->kind == 2) { float ex = (px - cx) / (w / 2), ey = (py - cy) / (h / 2); float t = sqrtf(ex * ex + ey * ey) / 1.4142f; col = with_alpha(grad_at(g, t), op); }
            else { float a = atan2f(px - cx, -(py - cy)) / 6.2831853f; if (a < 0) a += 1; col = with_alpha(grad_at(g, a), op); }
            int a = (int)(col >> 24);
            if (rounded) { float cv = rr_cov(px, py, x, y, w, h, r); a = (int)(a * cv); }
            if (a <= 0) continue;
            unsigned* p = FB + (size_t)yy * (size_t)STR + xx;
            *p = a >= 255 ? (col & 0xFFFFFF) : blend(*p, col, a);
        }
    }
}

static void draw_image(nimg* im, float x, float y, float w, float h, float op, clipr* c, const float* r) {
    if (!im || im->state != 2 || !im->px || w < 0.5f || h < 0.5f) return;
    int x0 = (int)floorf(x), y0 = (int)floorf(y), x1 = (int)ceilf(x + w), y1 = (int)ceilf(y + h);
    int cx0 = x0 < c->x0 ? c->x0 : x0, cy0 = y0 < c->y0 ? c->y0 : y0, cx1 = x1 > c->x1 ? c->x1 : x1, cy1 = y1 > c->y1 ? c->y1 : y1;
    if (cx0 >= cx1 || cy0 >= cy1) return;
    float sxs = im->w / w, sys = im->h / h;
    int ia = (int)(op * 255);
    int rounded = r && has_r(r);
    for (int yy = cy0; yy < cy1; yy++) {
        float fy = (yy + 0.5f - y) * sys - 0.5f;
        if (fy < 0) fy = 0;
        int iy = (int)fy;
        if (iy >= im->h) iy = im->h - 1;
        unsigned* row = FB + (size_t)yy * (size_t)STR;
        const unsigned* src = im->px + (size_t)iy * (size_t)im->w;
        for (int xx = cx0; xx < cx1; xx++) {
            float fx = (xx + 0.5f - x) * sxs - 0.5f;
            if (fx < 0) fx = 0;
            int ix = (int)fx;
            if (ix >= im->w) ix = im->w - 1;
            unsigned p = src[ix];
            if (sxs < 1.5f && sys < 1.5f && ix + 1 < im->w && iy + 1 < im->h) {
                float tx = fx - ix, ty = fy - iy;
                unsigned p01 = src[ix + 1], p10 = src[ix + im->w], p11 = src[ix + im->w + 1];
                p = lerpc(lerpc(p, p01, tx), lerpc(p10, p11, tx), ty);
            }
            int a = (int)(p >> 24) * ia / 255;
            if (rounded) a = (int)(a * rr_cov(xx + 0.5f, yy + 0.5f, x, y, w, h, r));
            if (a <= 0) continue;
            row[xx] = a >= 255 ? (p & 0xFFFFFF) : blend(row[xx], p, a);
        }
    }
}

static void draw_text(const char* s, int n, float x, float base, style* st, float op, clipr* c, unsigned col) {
    if (st->hastshadow) font_draw(FB, STR, c->x0, c->y0, c->x1, c->y1, st->face, st->fsize, x + st->tsx, base + st->tsy, s, n, with_alpha(st->tscolor, op * 0.8f), st->lspace);
    font_draw(FB, STR, c->x0, c->y0, c->x1, c->y1, st->face, st->fsize, x, base, s, n, with_alpha(col, op), st->lspace);
    if (st->fweight >= 600 && (st->face == F_SANS_I || st->face == F_SERIF_I)) font_draw(FB, STR, c->x0, c->y0, c->x1, c->y1, st->face, st->fsize, x + 0.6f, base, s, n, with_alpha(col, op * 0.6f), st->lspace);
}

static float rad_px(float r, float w, float h) { if (r < 0) return -r * fminf(w, h) / 100; return r; }

static void resolve_radius(style* s, float w, float h, float* r) {
    for (int i = 0; i < 4; i++) r[i] = rad_px(s->radius[i], w, h);
    float f = 1;
    float sums[4] = { r[0] + r[1], r[1] + r[2], r[2] + r[3], r[3] + r[0] };
    float dims[4] = { w, h, w, h };
    for (int i = 0; i < 4; i++) if (sums[i] > dims[i] && sums[i] > 0) { float k = dims[i] / sums[i]; if (k < f) f = k; }
    for (int i = 0; i < 4; i++) r[i] *= f;
}

static void shadow(style* s, float x, float y, float w, float h, const float* r, float op, clipr* c) {
    float blur = s->sblur, spread = s->sspread;
    float bx = x + s->sx - spread, by = y + s->sy - spread, bw = w + 2 * spread, bh = h + 2 * spread;
    unsigned col = with_alpha(s->scolor, op);
    if (blur < 1) { rrect(bx, by, bw, bh, r, col, c); return; }
    int steps = (int)(blur / 2);
    if (steps < 2) steps = 2;
    if (steps > 10) steps = 10;
    float a = (float)(col >> 24) / (steps + 1);
    for (int i = steps; i >= 0; i--) {
        float e = blur * (float)i / steps - blur / 2;
        float rr[4] = { r[0] + e > 0 ? r[0] + e : 0, r[1] + e > 0 ? r[1] + e : 0, r[2] + e > 0 ? r[2] + e : 0, r[3] + e > 0 ? r[3] + e : 0 };
        unsigned cc = ((unsigned)(a * 1.2f) << 24) | (col & 0xFFFFFF);
        if ((cc >> 24) == 0) cc |= 0x01000000;
        rrect(bx - e, by - e, bw + 2 * e, bh + 2 * e, rr, cc, c);
    }
}

static void border_side_draw(float x, float y, float w, float h, int style_, unsigned col, int horiz, clipr* c) {
    if (style_ == BS_DOTTED || style_ == BS_DASHED) {
        float th = horiz ? h : w;
        float seg = style_ == BS_DOTTED ? th : th * 3;
        if (seg < 1) seg = 1;
        float len = horiz ? w : h;
        for (float t = 0; t < len; t += seg * 2) {
            float l = seg < len - t ? seg : len - t;
            if (horiz) rect(x + t, y, l, h, col, c); else rect(x, y + t, w, l, col, c);
        }
        return;
    }
    if (style_ == BS_DOUBLE && (horiz ? h : w) >= 3) {
        float t = (horiz ? h : w) / 3;
        if (horiz) { rect(x, y, w, t, col, c); rect(x, y + h - t, w, t, col, c); }
        else { rect(x, y, t, h, col, c); rect(x + w - t, y, t, h, col, c); }
        return;
    }
    rect(x, y, w, h, col, c);
}
static unsigned shade(unsigned c, float f) {
    int r = (int)(((c >> 16) & 255) * f), g = (int)(((c >> 8) & 255) * f), b = (int)((c & 255) * f);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (c & 0xFF000000) | ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
}

static void bg_image_layer(bgimg* g, style* s, float bx, float by, float bw, float bh, const float* r, float op, clipr* c) {
    if (g->kind >= 1 && g->kind <= 3) { gradient(g, bx, by, bw, bh, r, op, c); return; }
    if (g->kind != 4 || !g->url) return;
    if (!g->img) { char full[1024]; url_resolve(G.base, g->url, full, sizeof(full)); g->img = img_get(full); }
    nimg* im = g->img;
    if (!im || im->state != 2) return;
    float iw = (float)im->w, ih = (float)im->h;
    if (s->bgsizek == 1) { float k = fmaxf(bw / iw, bh / ih); iw *= k; ih *= k; }
    else if (s->bgsizek == 2) { float k = fminf(bw / iw, bh / ih); iw *= k; ih *= k; }
    else if (s->bgsizek == 3) {
        float nw = s->bgsx.k == LK_VAL ? lres(s->bgsx, bw) : -1, nh = s->bgsy.k == LK_VAL ? lres(s->bgsy, bh) : -1;
        if (nw > 0 && nh < 0) { nh = ih * nw / iw; }
        else if (nh > 0 && nw < 0) { nw = iw * nh / ih; }
        if (nw > 0) iw = nw;
        if (nh > 0) ih = nh;
    }
    if (iw < 1 || ih < 1) return;
    float px = bx + s->bgpx.px + s->bgpx.pct * (bw - iw) / 100;
    float py = by + s->bgpy.px + s->bgpy.pct * (bh - ih) / 100;
    clipr cc = *c;
    if (bx > cc.x0) cc.x0 = (int)bx;
    if (by > cc.y0) cc.y0 = (int)by;
    if (bx + bw < cc.x1) cc.x1 = (int)ceilf(bx + bw);
    if (by + bh < cc.y1) cc.y1 = (int)ceilf(by + bh);
    if (cc.x0 >= cc.x1 || cc.y0 >= cc.y1) return;
    int rx = s->bgrepeat == 0 || s->bgrepeat == 2, ry = s->bgrepeat == 0 || s->bgrepeat == 3;
    float sx0 = px, sy0 = py;
    if (rx) while (sx0 > bx) sx0 -= iw;
    if (ry) while (sy0 > by) sy0 -= ih;
    int count = 0;
    for (float yy = sy0; yy < by + bh && count < 4000; yy += ih) {
        for (float xx = sx0; xx < bx + bw && count < 4000; xx += iw) {
            draw_image(im, xx, yy, iw, ih, op, &cc, 0);
            count++;
            if (!rx) break;
        }
        if (!ry) break;
    }
}

static void paint_bg_border(box* b, float dx, float dy, float op, clipr* c) {
    style* s = b->st;
    float x = b->x + dx, y = b->y + dy, w = b->w, h = b->h;
    if (b->kind == BX_ROW || b->kind == BX_ROWGROUP) { if ((s->bgcolor >> 24)) rect(x, y, w, h, with_alpha(s->bgcolor, op), c); return; }
    float r[4];
    resolve_radius(s, w, h, r);
    if (s->hasshadow && !s->sinset) shadow(s, x, y, w, h, r, op, c);
    if ((s->bgcolor >> 24) != 0) rrect(x, y, w, h, r, with_alpha(s->bgcolor, op), c);
    if (s->bg) {
        bgimg* layers[8];
        int nl = 0;
        for (bgimg* g = s->bg; g && nl < 8; g = g->next) layers[nl++] = g;
        for (int i = nl - 1; i >= 0; i--) bg_image_layer(layers[i], s, x, y, w, h, r, op, c);
    }
    float bt = b->b[0], br = b->b[1], bb = b->b[2], bl = b->b[3];
    if (bt <= 0 && br <= 0 && bb <= 0 && bl <= 0) return;
    int same = s->bcolor[0] == s->bcolor[1] && s->bcolor[1] == s->bcolor[2] && s->bcolor[2] == s->bcolor[3] && s->bstyle[0] == s->bstyle[1] && s->bstyle[1] == s->bstyle[2] && s->bstyle[2] == s->bstyle[3];
    if (has_r(r) && same && (s->bstyle[0] == BS_SOLID || s->bstyle[0] == BS_INSET || s->bstyle[0] == BS_OUTSET || s->bstyle[0] == BS_GROOVE || s->bstyle[0] == BS_RIDGE)) {
        rring(x, y, w, h, r, bt, br, bb, bl, with_alpha(s->bcolor[0], op), c);
        return;
    }
    unsigned col[4];
    for (int i = 0; i < 4; i++) {
        col[i] = with_alpha(s->bcolor[i], op);
        int bs = s->bstyle[i];
        if (bs == BS_INSET || bs == BS_GROOVE) col[i] = shade(col[i], (i == 0 || i == 3) ? 0.6f : 1.15f);
        if (bs == BS_OUTSET || bs == BS_RIDGE) col[i] = shade(col[i], (i == 0 || i == 3) ? 1.15f : 0.6f);
    }
    if (bt > 0) border_side_draw(x, y, w, bt, s->bstyle[0], col[0], 1, c);
    if (bb > 0) border_side_draw(x, y + h - bb, w, bb, s->bstyle[2], col[2], 1, c);
    if (bl > 0) border_side_draw(x, y + bt, bl, h - bt - bb, s->bstyle[3], col[3], 0, c);
    if (br > 0) border_side_draw(x + w - br, y + bt, br, h - bt - bb, s->bstyle[1], col[1], 0, c);
}

static void draw_check(float x, float y, float sz, unsigned col, clipr* c) {
    for (float t = 0; t <= 1.0f; t += 0.02f) {
        float px, py;
        if (t < 0.4f) { px = x + sz * (0.2f + t * 0.5f); py = y + sz * (0.5f + t * 0.5f); }
        else { float u = (t - 0.4f) / 0.6f; px = x + sz * (0.4f + u * 0.42f); py = y + sz * (0.7f - u * 0.5f); }
        rect(px - 0.8f, py - 0.8f, 1.8f, 1.8f, col, c);
    }
}

static void input_text(box* b, float dx, float dy, float op, clipr* c, int textarea) {
    node* n = b->n;
    style* s = b->st;
    float x = b->x + dx + b->b[3] + b->p[3], y = b->y + dy + b->b[0] + b->p[0];
    float w = b->w - b->b[1] - b->b[3] - b->p[1] - b->p[3], h = b->h - b->b[0] - b->b[2] - b->p[0] - b->p[2];
    clipr cc = *c;
    int cx0 = (int)(b->x + dx + b->b[3]), cy0 = (int)(b->y + dy + b->b[0]), cx1 = (int)(b->x + dx + b->w - b->b[1]), cy1 = (int)(b->y + dy + b->h - b->b[2]);
    if (cx0 > cc.x0) cc.x0 = cx0;
    if (cy0 > cc.y0) cc.y0 = cy0;
    if (cx1 < cc.x1) cc.x1 = cx1;
    if (cy1 < cc.y1) cc.y1 = cy1;
    const char* v = n->value;
    int vl = n->vlen;
    sbuf tmp = { 0, 0, 0 };
    if (!v) {
        if (textarea) { text_content(n, &tmp); v = tmp.s ? tmp.s : ""; vl = tmp.n; }
        else { v = attr_get(n, "value"); vl = v ? (int)strlen(v) : 0; }
        if (!v) v = "";
    }
    fontm m;
    font_metrics(s->face, s->fsize, &m);
    const char* ty = attr_get(n, "type");
    int pw = ty && ieq(ty, "password");
    sbuf masked = { 0, 0, 0 };
    if (pw && vl) { int cnt = 0; for (int i = 0; i < vl; i++) if (((unsigned char)v[i] & 0xC0) != 0x80) cnt++; for (int i = 0; i < cnt; i++) sb_putu8(&masked, 0x2022); v = masked.s; vl = masked.n; }
    unsigned col = s->color;
    if (vl == 0) {
        const char* ph = attr_get(n, "placeholder");
        if (ph) { v = ph; vl = (int)strlen(ph); col = 0xFF757575; }
    }
    float lh = lineh_of(s);
    if (lh <= 0) lh = s->fsize * 1.2f;
    int focused = G.focus == n;
    if (!textarea) {
        float base = y + (h - (m.ascent + m.descent)) / 2 + m.ascent;
        float tw = text_width(s->face, s->fsize, v, vl, s->lspace);
        float shift = 0;
        if (focused && n->value && tw > w - 2) shift = tw - w + 2;
        if (b->st->talign == TA_CENTER && tw < w) shift = -(w - tw) / 2;
        if (b->st->talign == TA_RIGHT && tw < w) shift = -(w - tw);
        draw_text(v, vl, x - shift, base, s, op, &cc, col);
        if (focused && ((nwk_ms() / 530) & 1) == 0) {
            int cp = n->caret;
            if (cp > n->vlen) cp = n->vlen;
            float cxp = x - shift + (n->value && !pw ? text_width(s->face, s->fsize, n->value, cp, s->lspace) : pw ? font_adv(s->face, s->fsize, 0x2022) * cp : 0);
            if (vl == 0 || !n->value) cxp = x - shift;
            rect(cxp, base - m.ascent, 1, m.ascent + m.descent, with_alpha(s->caretcol ? s->caretcol : s->color, op), &cc);
        }
    } else {
        float yy = y + m.ascent;
        int i = 0;
        float caretx = x, carety = y;
        int cp = n->caret;
        if (!n->value) cp = 0;
        int seen = 0;
        while (i <= vl) {
            int st0 = i;
            float lw = 0;
            int lastsp = -1;
            while (i < vl && v[i] != '\n') {
                int cpp;
                int k = u8dec(v + i, vl - i, &cpp);
                float aw = font_adv(s->face, s->fsize, cpp);
                if (lw + aw > w && i > st0) { if (lastsp > st0) i = lastsp + 1; break; }
                if (cpp == ' ') lastsp = i;
                lw += aw;
                i += k;
            }
            draw_text(v + st0, i - st0, x, yy, s, op, &cc, col);
            if (!seen && cp >= st0 && cp <= i) { caretx = x + text_width(s->face, s->fsize, v + st0, cp - st0, 0); carety = yy - m.ascent; seen = 1; }
            yy += lh;
            if (i < vl && v[i] == '\n') i++;
            else if (i >= vl) break;
            if (yy - m.ascent > y + h + lh) break;
        }
        if (focused && ((nwk_ms() / 530) & 1) == 0) rect(caretx, carety, 1, m.ascent + m.descent, with_alpha(s->color, op), &cc);
    }
}

static void paint_replaced(box* b, float dx, float dy, float op, clipr* c) {
    node* n = b->n;
    style* s = b->st;
    float x = b->x + dx + b->b[3] + b->p[3], y = b->y + dy + b->b[0] + b->p[0];
    float w = b->w - b->b[1] - b->b[3] - b->p[1] - b->p[3], h = b->h - b->b[0] - b->b[2] - b->p[0] - b->p[2];
    int t = n->tag;
    const char* ty = t == T_INPUT ? attr_get(n, "type") : 0;
    float r[4];
    resolve_radius(s, b->w, b->h, r);
    if (t == T_IMG || (t == T_INPUT && ty && ieq(ty, "image"))) {
        nimg* im = n->img;
        if (im && im->state == 2) {
            float dw = w, dh = h, ox = 0, oy = 0;
            if (s->objfit == 1 || s->objfit == 2) {
                float k = s->objfit == 1 ? fmaxf(w / im->w, h / im->h) : fminf(w / im->w, h / im->h);
                dw = im->w * k;
                dh = im->h * k;
                ox = (w - dw) / 2;
                oy = (h - dh) / 2;
            }
            clipr cc = *c;
            if ((int)x > cc.x0) cc.x0 = (int)x;
            if ((int)y > cc.y0) cc.y0 = (int)y;
            if ((int)ceilf(x + w) < cc.x1) cc.x1 = (int)ceilf(x + w);
            if ((int)ceilf(y + h) < cc.y1) cc.y1 = (int)ceilf(y + h);
            draw_image(im, x + ox, y + oy, dw, dh, op, &cc, has_r(r) ? r : 0);
        } else {
            const char* alt = attr_get(n, "alt");
            if (alt && *alt && w > 4) {
                fontm m;
                font_metrics(s->face, s->fsize, &m);
                clipr cc = *c;
                if ((int)(x + w) < cc.x1) cc.x1 = (int)(x + w);
                draw_text(alt, (int)strlen(alt), x + 2, y + m.ascent, s, op, &cc, s->color);
            }
        }
        return;
    }
    if (t == T_INPUT) {
        if (ty && (ieq(ty, "checkbox") || ieq(ty, "radio"))) {
            int radio = ieq(ty, "radio");
            float bx = b->x + dx, by = b->y + dy;
            if (n->checked) {
                unsigned ac = s->accent ? s->accent : 0xFF0075FF;
                if (radio) {
                    float rr[4] = { b->w / 2, b->w / 2, b->w / 2, b->w / 2 };
                    rrect(bx, by, b->w, b->h, rr, with_alpha(ac, op), c);
                    float k = b->w * 0.3f;
                    float r2[4] = { k, k, k, k };
                    rrect(bx + b->w / 2 - k, by + b->h / 2 - k, 2 * k, 2 * k, r2, with_alpha(0xFFFFFFFF, op), c);
                } else {
                    float rr[4] = { 2, 2, 2, 2 };
                    rrect(bx, by, b->w, b->h, rr, with_alpha(ac, op), c);
                    draw_check(bx, by, b->w, with_alpha(0xFFFFFFFF, op), c);
                }
            }
            return;
        }
        if (ty && (ieq(ty, "submit") || ieq(ty, "button") || ieq(ty, "reset"))) {
            const char* v = attr_get(n, "value");
            if (!v) v = ieq(ty, "reset") ? "Reset" : ieq(ty, "submit") ? "Submit" : "";
            fontm m;
            font_metrics(s->face, s->fsize, &m);
            float tw = text_width(s->face, s->fsize, v, (int)strlen(v), 0);
            draw_text(v, (int)strlen(v), x + (w - tw) / 2, y + (h - (m.ascent + m.descent)) / 2 + m.ascent, s, op, c, s->color);
            return;
        }
        if (ty && ieq(ty, "range")) {
            float mid = y + h / 2;
            float rr[4] = { 2, 2, 2, 2 };
            rrect(x, mid - 2, w, 4, rr, with_alpha(0xFFCCCCCC, op), c);
            const char* vs = n->value ? n->value : attr_get(n, "value");
            float mn = attr_get(n, "min") ? (float)atof(attr_get(n, "min")) : 0, mx = attr_get(n, "max") ? (float)atof(attr_get(n, "max")) : 100;
            float v = vs ? (float)atof(vs) : (mn + mx) / 2;
            float t2 = mx > mn ? (v - mn) / (mx - mn) : 0.5f;
            if (t2 < 0) t2 = 0;
            if (t2 > 1) t2 = 1;
            rrect(x, mid - 2, w * t2, 4, rr, with_alpha(s->accent ? s->accent : 0xFF0075FF, op), c);
            float kr[4] = { 8, 8, 8, 8 };
            rrect(x + w * t2 - 8, mid - 8, 16, 16, kr, with_alpha(s->accent ? s->accent : 0xFF0075FF, op), c);
            return;
        }
        if (ty && ieq(ty, "color")) { unsigned cc; const char* v = n->value ? n->value : attr_get(n, "value"); if (!v || !css_parse_color(v, (int)strlen(v), &cc)) cc = 0xFF000000; rect(x + 2, y + 2, w - 4, h - 4, with_alpha(cc, op), c); return; }
        if (ty && ieq(ty, "file")) {
            const char* lbl = "Choose File   No file chosen";
            fontm m;
            font_metrics(s->face, s->fsize, &m);
            draw_text(lbl, (int)strlen(lbl), x, y + (h - (m.ascent + m.descent)) / 2 + m.ascent, s, op, c, s->color);
            return;
        }
        input_text(b, dx, dy, op, c, 0);
        return;
    }
    if (t == T_TEXTAREA) { input_text(b, dx, dy, op, c, 1); return; }
    if (t == T_SELECT) {
        node* sel = 0;
        int idx = 0;
        node* firsto = 0;
        node* stack[64];
        int sp = 0;
        for (node* o = n->last; o; o = o->prev) if (sp < 64) stack[sp++] = o;
        int want = n->selidx;
        while (sp > 0) {
            node* o = stack[--sp];
            if (o->tag == T_OPTGROUP) { for (node* q = o->last; q; q = q->prev) if (sp < 64) stack[sp++] = q; continue; }
            if (o->tag != T_OPTION) continue;
            if (!firsto) firsto = o;
            if (want >= 0 ? idx == want : (attr_get(o, "selected") != 0)) sel = o;
            idx++;
        }
        if (!sel) sel = firsto;
        sbuf tb = { 0, 0, 0 };
        if (sel) text_content(sel, &tb);
        fontm m;
        font_metrics(s->face, s->fsize, &m);
        clipr cc = *c;
        if ((int)(x + w) < cc.x1) cc.x1 = (int)(x + w);
        float base = y + (h - (m.ascent + m.descent)) / 2 + m.ascent;
        if (tb.n) draw_text(tb.s, tb.n, x, base, s, op, &cc, s->color);
        float ax = b->x + dx + b->w - b->b[1] - 13, ay = b->y + dy + b->h / 2 - 2;
        for (int i = 0; i < 4; i++) rect(ax + i, ay + i, 8 - 2 * i, 1, with_alpha(s->color, op), c);
        return;
    }
    if (t == T_PROGRESS || t == T_METER) {
        float rr[4] = { 3, 3, 3, 3 };
        rrect(x, y, w, h, rr, with_alpha(0xFFE6E6E6, op), c);
        const char* vs = attr_get(n, "value");
        float mx = attr_get(n, "max") ? (float)atof(attr_get(n, "max")) : 1;
        float v = vs ? (float)atof(vs) : 0;
        float t2 = mx > 0 ? v / mx : 0;
        if (t2 > 1) t2 = 1;
        if (t2 < 0) t2 = 0;
        rrect(x, y, w * t2, h, rr, with_alpha(t == T_METER ? 0xFF2E9E3E : (s->accent ? s->accent : 0xFF0075FF), op), c);
        return;
    }
    if (t == T_VIDEO || t == T_AUDIO) { wm_paint(n, x, y, w, h, c->x0, c->y0, c->x1, c->y1, op, FB, STR); return; }
    if (t == T_VIDEO || t == T_IFRAME || t == T_CANVAS || t == T_OBJECT || t == T_EMBED) {
        if (t == T_CANVAS) return;
        rect(x, y, w, h, with_alpha(t == T_VIDEO ? 0xFF101010 : 0xFFF2F2F2, op), c);
        const char* lbl = t == T_VIDEO ? "Video" : t == T_IFRAME ? "Embedded page" : "Plugin content";
        fontm m;
        font_metrics(F_SANS, 13, &m);
        float tw = text_width(F_SANS, 13, lbl, (int)strlen(lbl), 0);
        if (w > tw + 8 && h > 20) font_draw(FB, STR, c->x0, c->y0, c->x1, c->y1, F_SANS, 13, x + (w - tw) / 2, y + h / 2 + 4, lbl, (int)strlen(lbl), with_alpha(t == T_VIDEO ? 0xFFDDDDDD : 0xFF808080, op), 0);
        return;
    }
    if (t == T_SVG) {
        extern void svg_paint(node* n, float x, float y, float w, float h, float op, unsigned* fb, int stride, int cx0, int cy0, int cx1, int cy1);
        svg_paint(n, x, y, w, h, op, FB, STR, c->x0, c->y0, c->x1, c->y1);
        return;
    }
}

typedef struct pent { box* b; float dx, dy, op; clipr c; int z, order; } pent;
typedef struct { pent* e; int n, cap; } plist2;
static int g_porder;
static void pl_add(plist2* l, box* b, float dx, float dy, float op, clipr* c) {
    if (l->n >= l->cap) { int nc = l->cap ? l->cap * 2 : 32; pent* ne = nalloc(sizeof(pent) * (size_t)nc); if (l->n) memcpy(ne, l->e, sizeof(pent) * (size_t)l->n); l->e = ne; l->cap = nc; }
    pent* p = &l->e[l->n++];
    p->b = b; p->dx = dx; p->dy = dy; p->op = op; p->c = *c; p->z = b->st->zauto ? 0 : b->st->zindex; p->order = g_porder++;
}
static int pent_cmp(const void* a, const void* b) {
    const pent* x = a;
    const pent* y = b;
    if (x->z != y->z) return x->z - y->z;
    return x->order - y->order;
}

static plist2* g_curpl;
static clipr g_vclip;
static void paint_frags(box* b, float dx, float dy, float op, clipr* c, int phase);

static int positioned(box* b) { return b->st && b->kind != BX_TEXT && b->kind != BX_BR && (b->st->position != P_STATIC || (b->st->hastransform && (!b->st->zauto || b->st->hasmatrix || b->st->nanim || b->st->ntrans)) || (b->st->opacity < 1 && !b->st->zauto)); }
static int clip_children(box* b, float dx, float dy, clipr* in, clipr* out) {
    style* s = b->st;
    *out = *in;
    if (!b->parent) return 0;
    if (s->ovx == OV_VISIBLE && s->ovy == OV_VISIBLE) return 0;
    float x = b->x + dx + b->b[3], y = b->y + dy + b->b[0], w = b->w - b->b[1] - b->b[3], h = b->h - b->b[0] - b->b[2];
    if (s->ovx != OV_VISIBLE) { if ((int)floorf(x) > out->x0) out->x0 = (int)floorf(x); if ((int)ceilf(x + w) < out->x1) out->x1 = (int)ceilf(x + w); }
    if (s->ovy != OV_VISIBLE) { if ((int)floorf(y) > out->y0) out->y0 = (int)floorf(y); if ((int)ceilf(y + h) < out->y1) out->y1 = (int)ceilf(y + h); }
    return 1;
}

static void paint_box(box* b, float dx, float dy, float op, clipr* c, plist2* pos, int depth);

static void paint_children_ctx(box* b, float dx, float dy, float op, clipr* c, plist2* pos, int depth, int inl) {
    for (int phase = 0; phase < 2; phase++) {
        for (box* k = b->first; k; k = k->next) {
            if (!k->st || k->kind == BX_TEXT || k->kind == BX_BR) continue;
            if (positioned(k)) { if (phase == 0) pl_add(pos, k, dx, dy, op, c); continue; }
            int isf = k->st->flt != FL_NONE;
            if ((phase == 1) != isf) continue;
            if (k->kind == BX_INLINE) { paint_children_ctx(k, dx + k->ox, dy + k->oy, op, c, pos, depth + 1, 1); continue; }
            if (inl && !isf) continue;
            paint_box(k, dx, dy, op, c, pos, depth + 1);
        }
    }
}
static void paint_children(box* b, float dx, float dy, float op, clipr* c, plist2* pos, int depth) { paint_children_ctx(b, dx, dy, op, c, pos, depth, b->inl); }

static void paint_box(box* b, float dx, float dy, float op, clipr* c, plist2* pos, int depth) {
    if (b->st && b->st->clipall) return;
    if (b->st && b->st->ovx != OV_VISIBLE && b->w <= 1.5f && b->h <= 1.5f) return;
    if (depth > 200) return;
    style* s = b->st;
    if (!s) return;
    float nop = op * s->opacity;
    if (nop <= 0.003f) return;
    float bdx = dx + b->ox, bdy = dy + b->oy;
    if (s->position == P_STICKY && s->inset[0].k == LK_VAL && b->parent) {
        float top = lres(s->inset[0], (float)g_vh);
        float want = (float)g_vy + top;
        float cur = b->y + bdy;
        if (cur < want) {
            box* pb = b->parent;
            float lim = pb->y + dy + pb->h - pb->b[2] - pb->p[2] - (cur + b->h) - b->m[2];
            float sh = want - cur;
            if (sh > lim) sh = lim;
            if (sh > 0) bdy += sh;
        }
    }
    if (b->y + bdy > c->y1 + 2000 && !b->first) return;
    if (!s->visibility && b->kind != BX_INLINE && b->kind != BX_ANON) paint_bg_border(b, bdx, bdy, nop, c);
    if (b->kind == BX_REPLACED) { if (!s->visibility) paint_replaced(b, bdx, bdy, nop, c); return; }
    clipr cc;
    clip_children(b, bdx, bdy, c, &cc);
    if (cc.x0 >= cc.x1 || cc.y0 >= cc.y1) return;
    float sdy = bdy - b->sy;
    paint_frags(b, bdx, sdy, nop, &cc, 0);
    paint_children(b, bdx, sdy, nop, &cc, pos, depth);
    paint_frags(b, bdx, sdy, nop, &cc, 1);
    if (b->contenth > 0 && (s->ovy == OV_AUTO || s->ovy == OV_SCROLL) && b->parent) {
        float inner = b->h - b->b[0] - b->b[2];
        float total = b->contenth + b->p[0] + b->p[2];
        if (total > inner + 1) {
            float tx = b->x + bdx + b->w - b->b[1] - 6, ty = b->y + bdy + b->b[0];
            float th = inner * inner / total;
            if (th < 20) th = 20;
            float tpos = ty + (inner - th) * (b->sy / (total - inner));
            float rr[4] = { 3, 3, 3, 3 };
            rrect(tx, tpos, 5, th, rr, 0x80808080, c);
        }
    }
}

static void paint_marker(frag* f, float dx, float dy, float op, clipr* c) {
    style* s = f->st;
    float sz = s->fsize * 0.36f;
    float x = f->x + dx, y = f->base + dy - s->fsize * 0.33f - sz / 2;
    unsigned col = with_alpha(s->color, op);
    if (f->n == LS_SQUARE) { rect(x, y, sz, sz, col, c); return; }
    float r[4] = { sz / 2, sz / 2, sz / 2, sz / 2 };
    if (f->n == LS_CIRCLE) { rring(x, y, sz, sz, r, 1, 1, 1, 1, col, c); return; }
    if (f->n == LS_NONE) return;
    rrect(x, y, sz, sz, r, col, c);
}

static void paint_frags(box* b, float dx, float dy, float op, clipr* c, int phase) {
    for (int i = 0; i < b->nfr; i++) {
        frag* f = &b->fr[i];
        if (f->y + dy > c->y1 || f->y + f->h + dy < c->y0) { if (f->kind != FK_BOX) continue; }
        if (phase == 0) {
            if (f->kind == FK_IBG) {
                style* s = f->st;
                box* ib = f->b;
                if (s->visibility) continue;
                float x = f->x + dx, y = f->y + dy;
                float r[4];
                resolve_radius(s, f->w, f->h, r);
                if ((s->bgcolor >> 24)) rrect(x, y, f->w, f->h, r, with_alpha(s->bgcolor * 0 + s->bgcolor, op * s->opacity), c);
                if (s->bg) for (bgimg* g = s->bg; g; g = g->next) bg_image_layer(g, s, x, y, f->w, f->h, r, op * s->opacity, c);
                unsigned bc;
                if (ib->b[0] > 0) { bc = with_alpha(s->bcolor[0], op); rect(x, y, f->w, ib->b[0], bc, c); }
                if (ib->b[2] > 0) { bc = with_alpha(s->bcolor[2], op); rect(x, y + f->h - ib->b[2], f->w, ib->b[2], bc, c); }
                if (ib->b[3] > 0 && (f->n & 1)) { bc = with_alpha(s->bcolor[3], op); rect(x, y, ib->b[3], f->h, bc, c); }
                if (ib->b[1] > 0 && (f->n & 2)) { bc = with_alpha(s->bcolor[1], op); rect(x + f->w - ib->b[1], y, ib->b[1], f->h, bc, c); }
            }
            continue;
        }
        if (f->kind == FK_TEXT) {
            style* s = f->st;
            if (s->visibility) continue;
            float o2 = op;
            for (box* p = f->b ? f->b->parent : 0; p && p != b; p = p->parent) if (p->st && p->kind == BX_INLINE) o2 *= p->st->opacity;
            int ell = (b->st->tover && b->st->ovx != OV_VISIBLE) || (b->st->lineclamp && b->clampy && fabsf(f->y - b->clampy) < 2);
            if (ell) {
                float right = b->x + b->ox + dx + b->w - b->b[1] - b->p[1];
                float fx = f->x + dx;
                int lastline = b->st->lineclamp && b->clampy;
                if (fx >= right - 0.5f) continue;
                int over = fx + f->w > right + 0.5f;
                if (!over && lastline) { int more = 0; for (int q = i + 1; q < b->nfr; q++) if (b->fr[q].kind == FK_TEXT && b->fr[q].y > f->y + 2) { more = 1; break; } int laston = 1; for (int q = i + 1; q < b->nfr; q++) if (b->fr[q].kind == FK_TEXT && fabsf(b->fr[q].y - f->y) < 2) { laston = 0; break; } over = more && laston; }
                if (over) {
                    const char* el = "\xE2\x80\xA6";
                    float ew = text_width(s->face, s->fsize, el, 3, 0);
                    float avail = (fx + f->w > right ? right - fx : f->w) - ew;
                    int k = 0;
                    float acc = 0;
                    while (k < f->n) { int cp; int l = u8dec(f->s + k, f->n - k, &cp); if (l <= 0) break; float aw = font_adv(s->face, s->fsize, cp) + s->lspace; if (acc + aw > avail) break; acc += aw; k += l; }
                    while (k > 0 && f->s[k - 1] == ' ') { k--; acc -= font_adv(s->face, s->fsize, ' ') + s->lspace; }
                    if (k > 0) draw_text(f->s, k, fx, f->base + dy, s, o2, c, s->color);
                    draw_text(el, 3, fx + acc, f->base + dy, s, o2, c, s->color);
                    continue;
                }
            }
            draw_text(f->s, f->n, f->x + dx, f->base + dy, s, o2, c, s->color);
            if (s->tdec) {
                float th = s->fsize / 14;
                if (th < 1) th = 1;
                unsigned col = with_alpha(s->color, o2);
                if (s->tdec & 1) rect(f->x + dx, f->base + dy + s->fsize * 0.12f, f->w, th, col, c);
                if (s->tdec & 2) rect(f->x + dx, f->base + dy - s->fsize * 0.3f, f->w, th, col, c);
                if (s->tdec & 4) rect(f->x + dx, f->base + dy - s->fsize * 0.85f, f->w, th, col, c);
            }
            if (G.find_n >= 0 && 0) {}
        } else if (f->kind == FK_MARKER) {
            if (!f->st->visibility) paint_marker(f, dx, dy, op, c);
        } else if (f->kind == FK_BOX) {
            box* a = f->b;
            if (!a) continue;
            if (positioned(a)) continue;
            paint_box(a, dx, dy, op, c, g_curpl, 1);
        }
    }
}

static void paint_outline(box* b, float dx, float dy, float op, clipr* c) {
    style* s = b->st;
    if (!s || s->ostyle == BS_NONE || s->owidth <= 0) return;
    float o = s->ooffset, w = s->owidth;
    float x = b->x + dx + b->ox - o - w, y = b->y + dy + b->oy - o - w, ww = b->w + 2 * (o + w), hh = b->h + 2 * (o + w);
    unsigned col = with_alpha(s->ocolor ? s->ocolor : s->color, op);
    rect(x, y, ww, w, col, c);
    rect(x, y + hh - w, ww, w, col, c);
    rect(x, y + w, w, hh - 2 * w, col, c);
    rect(x + ww - w, y + w, w, hh - 2 * w, col, c);
}


static void sub_bounds(box* b, float dx, float dy, float* bb, int depth, int* cnt) {
    if (depth > 60 || ++*cnt > 6000 || !b->st) return;
    float bdx = dx + b->ox, bdy = dy + b->oy;
    float x0 = b->x + bdx, y0 = b->y + bdy, x1 = x0 + b->w, y1 = y0 + b->h;
    if (b->kind != BX_TEXT && b->kind != BX_BR && b->st->hasshadow) { float e = fabsf(b->st->sblur) + fabsf(b->st->sspread) + fabsf(b->st->sx) + fabsf(b->st->sy); x0 -= e; y0 -= e; x1 += e; y1 += e; }
    if (x0 < bb[0]) bb[0] = x0;
    if (y0 < bb[1]) bb[1] = y0;
    if (x1 > bb[2]) bb[2] = x1;
    if (y1 > bb[3]) bb[3] = y1;
    for (int i = 0; i < b->nfr; i++) { frag* f = &b->fr[i]; float fx0 = f->x + bdx, fy0 = f->y + bdy - b->sy; if (fx0 < bb[0]) bb[0] = fx0; if (fy0 < bb[1]) bb[1] = fy0; if (fx0 + f->w > bb[2]) bb[2] = fx0 + f->w; if (fy0 + f->h > bb[3]) bb[3] = fy0 + f->h; }
    if (b->st->ovx != OV_VISIBLE && b->st->ovy != OV_VISIBLE && depth > 0) return;
    for (box* k = b->first; k; k = k->next) sub_bounds(k, bdx, bdy - b->sy, bb, depth + 1, cnt);
}
static void paint_stack(box* b, float dx, float dy, float op, clipr* c, int depth);
static void paint_transformed(box* k, float dx, float dy, float op, clipr* c, int depth) {
    style* s = k->st;
    float m[6];
    tf_matrix(s, k->w, k->h, m);
    float ddx = m[4] - k->btx, ddy = m[5] - k->bty;
    int lin = fabsf(m[0] - 1) > 1e-4f || fabsf(m[1]) > 1e-4f || fabsf(m[2]) > 1e-4f || fabsf(m[3] - 1) > 1e-4f;
    if (!lin) { paint_stack(k, dx + ddx, dy + ddy, op, c, depth); return; }
    float det = m[0] * m[3] - m[1] * m[2];
    if (fabsf(det) < 1e-6f) return;
    float px0 = dx - k->btx, py0 = dy - k->bty;
    float bb[4] = { 1e30f, 1e30f, -1e30f, -1e30f };
    int cnt = 0;
    sub_bounds(k, px0, py0, bb, 0, &cnt);
    float ox = k->x + px0 + k->ox + lres(s->tox, k->w), oy = k->y + py0 + k->oy + lres(s->toy, k->h);
    float lim = 1800;
    if (bb[0] < ox - lim) bb[0] = ox - lim;
    if (bb[1] < oy - lim) bb[1] = oy - lim;
    if (bb[2] > ox + lim) bb[2] = ox + lim;
    if (bb[3] > oy + lim) bb[3] = oy + lim;
    int rx0 = (int)floorf(bb[0]) - 1, ry0 = (int)floorf(bb[1]) - 1, rw = (int)ceilf(bb[2]) + 1 - rx0, rh = (int)ceilf(bb[3]) + 1 - ry0;
    if (rw <= 0 || rh <= 0) return;
    if ((long)rw * rh > 1600000L) { paint_stack(k, dx + ddx, dy + ddy, op, c, depth); return; }
    float cx[4], cy[4];
    float qx[4] = { (float)rx0, (float)(rx0 + rw), (float)rx0, (float)(rx0 + rw) }, qy[4] = { (float)ry0, (float)ry0, (float)(ry0 + rh), (float)(ry0 + rh) };
    float dminx = 1e30f, dminy = 1e30f, dmaxx = -1e30f, dmaxy = -1e30f;
    for (int i = 0; i < 4; i++) {
        float ux = qx[i] - ox, uy = qy[i] - oy;
        cx[i] = ox + m[0] * ux + m[2] * uy + m[4];
        cy[i] = oy + m[1] * ux + m[3] * uy + m[5];
        if (cx[i] < dminx) dminx = cx[i];
        if (cy[i] < dminy) dminy = cy[i];
        if (cx[i] > dmaxx) dmaxx = cx[i];
        if (cy[i] > dmaxy) dmaxy = cy[i];
    }
    int dx0 = (int)floorf(dminx), dy0 = (int)floorf(dminy), dx1 = (int)ceilf(dmaxx), dy1 = (int)ceilf(dmaxy);
    if (dx0 < c->x0) dx0 = c->x0;
    if (dy0 < c->y0) dy0 = c->y0;
    if (dx1 > c->x1) dx1 = c->x1;
    if (dy1 > c->y1) dy1 = c->y1;
    if (dx0 >= dx1 || dy0 >= dy1) return;
    unsigned* sfb = FB;
    int sstr = STR, svx = g_vx, svy = g_vy;
    clipr svc = g_vclip;
    size_t np = (size_t)rw * (size_t)rh;
    unsigned* B0 = nalloc(np * 4);
    unsigned* B1 = nalloc(np * 4);
    if (!B0 || !B1) return;
    extern int g_wm_offscreen;
    g_wm_offscreen++;
    for (int pass = 0; pass < 2; pass++) {
        unsigned* buf = pass ? B1 : B0;
        unsigned fillc = pass ? 0xFFFFFF : 0;
        for (size_t i = 0; i < np; i++) buf[i] = fillc;
        FB = buf;
        STR = rw;
        g_vx = svx - rx0;
        g_vy = svy - ry0;
        clipr oc = { 0, 0, rw, rh };
        g_vclip = oc;
        paint_stack(k, px0 - rx0, py0 - ry0, op, &oc, depth);
    }
    g_wm_offscreen--;
    FB = sfb; STR = sstr; g_vx = svx; g_vy = svy; g_vclip = svc;
    for (size_t i = 0; i < np; i++) {
        unsigned b = B0[i], w = B1[i];
        int dr = (int)((w >> 16) & 255) - (int)((b >> 16) & 255), dg = (int)((w >> 8) & 255) - (int)((b >> 8) & 255), db = (int)(w & 255) - (int)(b & 255);
        int a = 255 - (dr + dg + db) / 3;
        if (a < 0) a = 0;
        if (a > 255) a = 255;
        B0[i] = ((unsigned)a << 24) | (b & 0xFFFFFF);
    }
    float ia = m[3] / det, ib = -m[1] / det, ic = -m[2] / det, id = m[0] / det;
    for (int y = dy0; y < dy1; y++) {
        unsigned* row = FB + (size_t)y * (size_t)STR;
        float vy = (float)y + 0.5f - oy - m[5];
        for (int x = dx0; x < dx1; x++) {
            float vx = (float)x + 0.5f - ox - m[4];
            float sx = ia * vx + ic * vy + ox - (float)rx0 - 0.5f;
            float sy = ib * vx + id * vy + oy - (float)ry0 - 0.5f;
            if (sx < -1 || sy < -1 || sx >= rw || sy >= rh) continue;
            int x0 = (int)floorf(sx), y0 = (int)floorf(sy);
            float fx = sx - x0, fy = sy - y0;
            float acc[4] = { 0, 0, 0, 0 };
            for (int q = 0; q < 4; q++) {
                int xx = x0 + (q & 1), yy = y0 + (q >> 1);
                if (xx < 0 || yy < 0 || xx >= rw || yy >= rh) continue;
                float wgt = ((q & 1) ? fx : 1 - fx) * ((q >> 1) ? fy : 1 - fy);
                unsigned p = B0[(size_t)yy * (size_t)rw + (size_t)xx];
                acc[0] += wgt * (float)(p >> 24);
                acc[1] += wgt * (float)((p >> 16) & 255);
                acc[2] += wgt * (float)((p >> 8) & 255);
                acc[3] += wgt * (float)(p & 255);
            }
            int a = (int)(acc[0] + 0.5f);
            if (a <= 0) continue;
            unsigned d = row[x];
            int inv = 255 - (a > 255 ? 255 : a);
            int r = (int)(acc[1] + ((d >> 16) & 255) * inv / 255.0f + 0.5f), g = (int)(acc[2] + ((d >> 8) & 255) * inv / 255.0f + 0.5f), bl = (int)(acc[3] + (d & 255) * inv / 255.0f + 0.5f);
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (bl > 255) bl = 255;
            row[x] = ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)bl;
        }
    }
}

static void paint_stack(box* b, float dx, float dy, float op, clipr* c, int depth) {
    if (depth > 60) return;
    plist2 pos = { 0, 0, 0 };
    plist2* save = g_curpl;
    g_curpl = &pos;
    paint_box(b, dx, dy, op, c, &pos, 0);
    g_curpl = save;
    if (pos.n == 0) { if (b->n && G.focus == b->n && b->st->ostyle) paint_outline(b, dx, dy, op, c); return; }
    msort(pos.e, pos.n, sizeof(pent), pent_cmp);
    for (int i = 0; i < pos.n; i++) {
        pent* p = &pos.e[i];
        box* k = p->b;
        float kdx = p->dx, kdy = p->dy;
        clipr kc = p->c;
        if (k->st->position == P_FIXED) {
            kdx = (float)g_vx;
            kdy = (float)g_vy;
            kc = g_vclip;
        }
        if (k->st->position == P_ABSOLUTE || k->st->position == P_FIXED) {
            box* cb = k->cb;
            if (k->st->position == P_ABSOLUTE) {
                float ax = 0, ay = 0;
                for (box* q = cb; q; q = q->parent) { ax += q->ox; ay += q->oy - q->sy; }
                if (cb) { for (box* q = cb; q; q = q->parent) if (q->st && q->st->position == P_FIXED) { ay += g_scroll; break; } }
                kdx = (float)g_vx + ax;
                kdy = (float)g_vy - g_scroll + ay;
            }
        }
        if (k->st->hastransform && (k->st->hasmatrix || k->st->nanim || k->st->ntrans)) paint_transformed(k, kdx, kdy, p->op, &kc, depth + 1);
        else paint_stack(k, kdx, kdy, p->op, &kc, depth + 1);
    }
}

void paint_all(unsigned* fb, int stride, int fbw, int fbh, int vx, int vy, int vw, int vh, float scroll) {
    FB = fb;
    STR = stride;
    int svp = g_pool;
    pool_reset(3);
    g_pool = 3;
    g_scroll = scroll;
    g_vx = vx;
    g_vy = vy;
    clipr c = { vx, vy, vx + vw, vy + vh };
    if (c.x1 > fbw) c.x1 = fbw;
    if (c.y1 > fbh) c.y1 = fbh;
    if (c.x0 < 0) c.x0 = 0;
    if (c.y0 < 0) c.y0 = 0;
    g_vclip = c;
    wm_begin_paint(vx, vy);
    unsigned bg = 0xFFFFFFFF;
    node* html = doc_elem();
    node* body = doc_body();
    style* rs = html ? html->st : 0;
    style* bs = body ? body->st : 0;
    bgimg* canvasimg = 0;
    style* canvas_st = 0;
    if (rs && ((rs->bgcolor >> 24) || rs->bg)) { if (rs->bgcolor >> 24) bg = rs->bgcolor; canvasimg = rs->bg; canvas_st = rs; }
    else if (bs && ((bs->bgcolor >> 24) || bs->bg)) { if (bs->bgcolor >> 24) bg = bs->bgcolor; canvasimg = bs->bg; canvas_st = bs; }
    if ((bg >> 24) < 255) { unsigned a = bg >> 24; bg = 0xFF000000 | blend(0xFFFFFF, bg, (int)a); }
    for (int y = c.y0; y < c.y1; y++) span(y, c.x0, c.x1, bg, &c);
    if (!g_root) { g_pool = svp; return; }
    if (canvasimg) {
        float th = g_doch > vh ? g_doch : vh;
        int sb = canvas_st->bgattach;
        for (bgimg* g = canvasimg; g; g = g->next) bg_image_layer(g, canvas_st, (float)vx, (float)vy - (sb ? 0 : scroll), (float)vw, sb ? (float)vh : th, 0, 1, &c);
    }
    if (rs && ((rs->bgcolor >> 24) || rs->bg)) {}
    else if (bs) { bs->bgcolor = bs->bgcolor; }
    style saveb;
    int restore = 0;
    if (bs && canvas_st == bs && body->box) { saveb = *bs; bs->bgcolor = 0; bs->bg = 0; restore = 1; }
    style saver;
    int restorer = 0;
    if (rs && canvas_st == rs) { saver = *rs; rs->bgcolor = 0; rs->bg = 0; restorer = 1; }
    paint_stack(g_root, (float)vx, (float)vy - scroll, 1.0f, &c, 0);
    if (restore) *bs = saveb;
    if (restorer) *rs = saver;
    if (G.focus && G.focus->box && G.focus->st) {
        box* fb2 = G.focus->box;
        float ax = 0, ay = 0;
        for (box* q = fb2; q; q = q->parent) { ax += q->ox; ay += q->oy - (q != fb2 ? q->sy : 0); }
        if (G.focus->tag == T_INPUT || G.focus->tag == T_TEXTAREA || G.focus->tag == T_SELECT) {
            if (G.focus->st->ostyle != BS_NONE || 1) {
                style* s = G.focus->st;
                if (!(s->ostyle == BS_NONE && s->owidth == 0 && 0)) {
                    float x = fb2->x + vx + ax - fb2->ox, y = fb2->y + vy - scroll + ay - fb2->oy;
                    if (fb2->st->position == P_FIXED) y += scroll;
                    float rr[4];
                    resolve_radius(s, fb2->w, fb2->h, rr);
                    for (int i = 0; i < 4; i++) rr[i] += 1;
                    rring(x + fb2->ox - 1, y + fb2->oy - 1, fb2->w + 2, fb2->h + 2, rr, 2, 2, 2, 2, 0xFF005FCC, &c);
                }
            }
        }
    }
    g_pool = svp;
}
