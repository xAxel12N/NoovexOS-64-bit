#include "web/layout.h"
int g_prog_indet;
node* g_ishift_n; float g_ishift;

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
    box* lg = 0;
    if (b->kind == BX_TABLE) { float cb = -1; for (box* q = b->first; q; q = q->next) if (q->kind == BX_CAPTION) cb = q->y + q->h + q->m[2]; if (cb > b->y) { float off = cb - b->y; if (off < h) { y += off; h -= off; } } }
    if (b->n && b->n->tag == T_FIELDSET) { box* L = b->first; while (L && !in_flow(L)) L = L->next; if (L && L->n && L->n->tag == T_LEGEND) { float off = L->y + L->h / 2 - b->y - b->b[0] / 2; if (off > 0 && off < h) { y += off; h -= off; lg = L; } } }
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
    if (bt > 0 && lg) {
        float gx0 = lg->x + dx - 2, gx1 = lg->x + dx + lg->w + 2;
        if (gx0 > x) border_side_draw(x, y, gx0 - x, bt, s->bstyle[0], col[0], 1, c);
        if (gx1 < x + w) border_side_draw(gx1, y, x + w - gx1, bt, s->bstyle[0], col[0], 1, c);
    } else if (bt > 0) border_side_draw(x, y, w, bt, s->bstyle[0], col[0], 1, c);
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
        if (focused && n->value && tw > w - 2) {
            int cq = n->caret > n->vlen ? n->vlen : n->caret;
            float cx0 = pw ? font_adv(s->face, s->fsize, 0x2022) * cq : text_width(s->face, s->fsize, n->value, cq, s->lspace);
            shift = cx0 - (w - 2);
            if (shift < 0) shift = 0;
            if (shift > tw - w + 2) shift = tw - w + 2;
        }
        if (focused) { g_ishift_n = n; g_ishift = shift > 0 ? shift : 0; }
        if (b->st->talign == TA_CENTER && tw < w) shift = -(w - tw) / 2;
        if (b->st->talign == TA_RIGHT && tw < w) shift = -(w - tw);
        if (focused && n->value && n->sela && !pw && vl) {
            int sa = n->sela - 1, sb = n->caret; if (sa > n->vlen) sa = n->vlen; if (sb > n->vlen) sb = n->vlen;
            if (sa != sb) { int a = sa < sb ? sa : sb, bb = sa < sb ? sb : sa; float xa = text_width(s->face, s->fsize, n->value, a, s->lspace), xb = text_width(s->face, s->fsize, n->value, bb, s->lspace); rect(x - shift + xa, base - m.ascent, xb - xa, m.ascent + m.descent, with_alpha(0xFF9CC3F5u, op), &cc); }
        }
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
            if (focused && n->value && n->sela) {
                int sa = n->sela - 1, sb = n->caret; if (sa > vl) sa = vl; if (sb > vl) sb = vl;
                int a = sa < sb ? sa : sb, bb = sa < sb ? sb : sa;
                int la = a > st0 ? a : st0, lb = bb < i ? bb : i;
                if (a != bb && la < lb) { float xa = text_width(s->face, s->fsize, v + st0, la - st0, 0), xb = text_width(s->face, s->fsize, v + st0, lb - st0, 0); rect(x + xa, yy - m.ascent, xb - xa, m.ascent + m.descent, with_alpha(0xFF9CC3F5u, op), &cc); }
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
                if ((int)(y + h) < cc.y1) cc.y1 = (int)(y + h);
                int framed = w >= 16 && h >= 16 && h > m.ascent + m.descent + 4;
                if (framed) { unsigned fc = with_alpha(0xFFC0C0C0, op); rect(x, y, w, 1, fc, c); rect(x, y + h - 1, w, 1, fc, c); rect(x, y, 1, h, fc, c); rect(x + w - 1, y, 1, h, fc, c); }
                draw_text(alt, (int)strlen(alt), x + (framed ? 4 : 2), y + (framed ? 3 : 0) + m.ascent, s, op, &cc, s->color);
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
    if (t == T_SELECT && sel_listbox_rows(n) > 0) {
        int rows = sel_listbox_rows(n);
        node* it[256]; int ni = sel_items(n, it, 256);
        float ih = sel_item_h(s);
        int multi = attr_get(n, "multiple") != 0;
        int oi = 0, first_sel = -1;
        for (int i = 0; i < ni; i++) { if (it[i]->tag != T_OPTION) continue; int on = multi ? attr_get(it[i], "selected") != 0 : (n->selidx >= 0 ? oi == n->selidx : attr_get(it[i], "selected") != 0); if (on && first_sel < 0) first_sel = i; oi++; }
        int top = first_sel >= rows ? first_sel - rows + 1 : 0;
        fontm m; font_metrics(s->face, s->fsize, &m);
        clipr cc = *c; if ((int)(x + w) < cc.x1) cc.x1 = (int)(x + w); if ((int)(y + h) < cc.y1) cc.y1 = (int)(y + h); if ((int)x > cc.x0) cc.x0 = (int)x; if ((int)y > cc.y0) cc.y0 = (int)y;
        int foc = G.focus == n;
        oi = 0;
        for (int i = 0; i < ni; i++) { if (it[i]->tag == T_OPTION && i < top) oi++; }
        for (int i = top; i < ni && i < top + rows; i++) {
            node* o = it[i]; float iy = y + (i - top) * ih;
            sbuf tb = { 0, 0, 0 };
            if (o->tag == T_OPTGROUP) { const char* lb = attr_get(o, "label"); draw_text(lb ? lb : "", lb ? (int)strlen(lb) : 0, x + 3, iy + (ih - (m.ascent + m.descent)) / 2 + m.ascent, s, op, &cc, s->color); continue; }
            int on = multi ? attr_get(o, "selected") != 0 : (n->selidx >= 0 ? oi == n->selidx : attr_get(o, "selected") != 0);
            oi++;
            int dis = attr_get(o, "disabled") != 0;
            if (on) rect(x, iy, w, ih, with_alpha(foc ? 0xFF1A73E8 : 0xFFCECECE, op), &cc);
            text_content(o, &tb);
            int ingrp = o->parent && o->parent->tag == T_OPTGROUP;
            draw_text(tb.s ? tb.s : "", tb.n, x + (ingrp ? 14 : 3), iy + (ih - (m.ascent + m.descent)) / 2 + m.ascent, s, op, &cc, on && foc ? 0xFFFFFFFF : dis ? 0xFF999999 : s->color);
        }
        return;
    }
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
        if (t == T_PROGRESS && !vs) {
            extern int g_prog_indet; g_prog_indet = 1;
            float seg = w * 0.3f; float ph = (float)(nwk_ms() % 1600) / 1600.0f; float sx = x - seg + (w + seg) * ph;
            float a0 = sx < x ? x : sx, a1 = sx + seg > x + w ? x + w : sx + seg;
            if (a1 > a0) rrect(a0, y, a1 - a0, h, rr, with_alpha(s->accent ? s->accent : 0xFF0075FF, op), c);
            return;
        }
        float mx = attr_get(n, "max") ? (float)atof(attr_get(n, "max")) : 1;
        float mn = t == T_METER && attr_get(n, "min") ? (float)atof(attr_get(n, "min")) : 0;
        float v = vs ? (float)atof(vs) : 0;
        float t2 = mx > mn ? (v - mn) / (mx - mn) : 0;
        if (t2 > 1) t2 = 1;
        if (t2 < 0) t2 = 0;
        unsigned mcol = 0xFF2E9E3E;
        if (t == T_METER) {
            const char* a1 = attr_get(n, "low"); const char* a2 = attr_get(n, "high"); const char* a3 = attr_get(n, "optimum");
            float lo = a1 ? (float)atof(a1) : mn, hi = a2 ? (float)atof(a2) : mx;
            if (lo < mn) lo = mn; if (hi > mx) hi = mx; if (hi < lo) hi = lo;
            float opt = a3 ? (float)atof(a3) : (mn + mx) / 2;
            int ro = opt < lo ? 0 : opt > hi ? 2 : 1, rv = v < lo ? 0 : v > hi ? 2 : 1;
            int d = ro > rv ? ro - rv : rv - ro;
            if (ro == 1 && rv != 1) d = 1;
            mcol = d == 0 ? 0xFF2E9E3E : d == 1 ? 0xFFF2B600 : 0xFFD93025;
        }
        rrect(x, y, w * t2, h, rr, with_alpha(t == T_METER ? mcol : (s->accent ? s->accent : 0xFF0075FF), op), c);
        return;
    }
    if (t == T_VIDEO || t == T_AUDIO) { wm_paint(n, x, y, w, h, c->x0, c->y0, c->x1, c->y1, op, FB, STR); return; }
    if (t == T_VIDEO || t == T_IFRAME || t == T_CANVAS || t == T_OBJECT || t == T_EMBED) {
        if (t == T_CANVAS) { if (n->img && n->img->state == 2) draw_image(n->img, x, y, w, h, op, c, 0); return; }
        if (t == T_IFRAME) {
            const char* src = attr_get(n, "src");
            const char* sd = attr_get(n, "srcdoc");
            char full[2048];
            full[0] = 0;
            if (src && *src) url_resolve(G.base, src, full, sizeof(full));
            if ((full[0] || (sd && *sd)) && nwk_frame_paint(full, sd, (int)x, (int)y, (int)w, (int)h, c->x0, c->y0, c->x1, c->y1, FB, STR)) return;
            if ((full[0] || (sd && *sd)) && w > 120 && h > 40) {
                char why[200]; int st = nwk_frame_status(full, sd, why, sizeof(why));
                const char* l1 = st == 1 ? "Loading..." : st == 2 ? "Loading..." : st == 3 ? (why[0] ? why : "Could not load the page") : "Waiting for a free frame place";
                rect(x, y, w, h, with_alpha(0xFFF2F2F2, op), c);
                float tw1 = text_width(F_SANS, 13, l1, (int)strlen(l1), 0);
                font_draw(FB, STR, c->x0, c->y0, c->x1, c->y1, F_SANS, 13, x + (w - tw1 > 8 ? (w - tw1) / 2 : 4), y + h / 2 - 4, l1, (int)strlen(l1), with_alpha(st == 3 ? 0xFFB3261E : 0xFF606060, op), 0);
                const char* l2 = full[0] ? full : "about:srcdoc"; int n2 = (int)strlen(l2); char sh[96];
                if (n2 > 90) { memcpy(sh, l2, 87); memcpy(sh + 87, "...", 4); l2 = sh; n2 = 90; }
                while (n2 > 4 && text_width(F_SANS, 11, l2, n2, 0) > w - 8) n2--;
                float tw2 = text_width(F_SANS, 11, l2, n2, 0);
                font_draw(FB, STR, c->x0, c->y0, c->x1, c->y1, F_SANS, 11, x + (w - tw2) / 2, y + h / 2 + 14, l2, n2, with_alpha(0xFF909090, op), 0);
                return;
            }
        }
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

static void flt_blur(unsigned* px, int w, int h, int r) {
    if (r < 1 || w < 2 || h < 2) return;
    if (r > 40) r = 40;
    unsigned* t = malloc((size_t)w * (size_t)h * 4);
    if (!t) return;
    for (int pass = 0; pass < 3; pass++) {
        for (int y = 0; y < h; y++) {
            unsigned* row = px + (size_t)y * w; unsigned* o = t + (size_t)y * w;
            int sr = 0, sg = 0, sb = 0, n = 0;
            for (int x = -r; x <= r; x++) { int xx = x < 0 ? 0 : x >= w ? w - 1 : x; unsigned v = row[xx]; sr += (v >> 16) & 255; sg += (v >> 8) & 255; sb += v & 255; n++; }
            for (int x = 0; x < w; x++) {
                o[x] = ((unsigned)(sr / n) << 16) | ((unsigned)(sg / n) << 8) | (unsigned)(sb / n);
                int xa = x - r < 0 ? 0 : x - r, xb = x + r + 1 >= w ? w - 1 : x + r + 1;
                unsigned va = row[xa], vb = row[xb];
                sr += (int)((vb >> 16) & 255) - (int)((va >> 16) & 255); sg += (int)((vb >> 8) & 255) - (int)((va >> 8) & 255); sb += (int)(vb & 255) - (int)(va & 255);
            }
        }
        for (int x = 0; x < w; x++) {
            int sr = 0, sg = 0, sb = 0, n = 0;
            for (int y = -r; y <= r; y++) { int yy = y < 0 ? 0 : y >= h ? h - 1 : y; unsigned v = t[(size_t)yy * w + x]; sr += (v >> 16) & 255; sg += (v >> 8) & 255; sb += v & 255; n++; }
            for (int y = 0; y < h; y++) {
                px[(size_t)y * w + x] = ((unsigned)(sr / n) << 16) | ((unsigned)(sg / n) << 8) | (unsigned)(sb / n);
                int ya = y - r < 0 ? 0 : y - r, yb = y + r + 1 >= h ? h - 1 : y + r + 1;
                unsigned va = t[(size_t)ya * w + x], vb = t[(size_t)yb * w + x];
                sr += (int)((vb >> 16) & 255) - (int)((va >> 16) & 255); sg += (int)((vb >> 8) & 255) - (int)((va >> 8) & 255); sb += (int)(vb & 255) - (int)(va & 255);
            }
        }
    }
    free(t);
}
static void flt_matrix(int f, float a, float* m) {
    for (int i = 0; i < 9; i++) m[i] = (i % 4 == 0) ? 1 : 0;
    if (a < 0) a = 0;
    if (f == 3 || f == 7) { if (a > 1) a = 1; }
    if (f == 3) { float k = 1 - a; float g[9] = { 0.2126f + 0.7874f * k, 0.7152f - 0.7152f * k, 0.0722f - 0.0722f * k, 0.2126f - 0.2126f * k, 0.7152f + 0.2848f * k, 0.0722f - 0.0722f * k, 0.2126f - 0.2126f * k, 0.7152f - 0.7152f * k, 0.0722f + 0.9278f * k }; for (int i = 0; i < 9; i++) m[i] = g[i]; }
    else if (f == 7) { float k = 1 - a; float g[9] = { 0.393f + 0.607f * k, 0.769f - 0.769f * k, 0.189f - 0.189f * k, 0.349f - 0.349f * k, 0.686f + 0.314f * k, 0.168f - 0.168f * k, 0.272f - 0.272f * k, 0.534f - 0.534f * k, 0.131f + 0.869f * k }; for (int i = 0; i < 9; i++) m[i] = g[i]; }
    else if (f == 6) { float g[9] = { 0.213f + 0.787f * a, 0.715f - 0.715f * a, 0.072f - 0.072f * a, 0.213f - 0.213f * a, 0.715f + 0.285f * a, 0.072f - 0.072f * a, 0.213f - 0.213f * a, 0.715f - 0.715f * a, 0.072f + 0.928f * a }; for (int i = 0; i < 9; i++) m[i] = g[i]; }
    else if (f == 8) { float r = a * 0.01745329f, cs = cosf(r), sn = sinf(r); float g[9] = { 0.213f + cs * 0.787f - sn * 0.213f, 0.715f - cs * 0.715f - sn * 0.715f, 0.072f - cs * 0.072f + sn * 0.928f, 0.213f - cs * 0.213f + sn * 0.143f, 0.715f + cs * 0.285f + sn * 0.140f, 0.072f - cs * 0.072f - sn * 0.283f, 0.213f - cs * 0.213f - sn * 0.787f, 0.715f - cs * 0.715f + sn * 0.715f, 0.072f + cs * 0.928f + sn * 0.072f }; for (int i = 0; i < 9; i++) m[i] = g[i]; }
}
static void flt_apply(style* s, int x0, int y0, int x1, int y1, const unsigned* before) {
    int w = x1 - x0, h = y1 - y0;
    for (int k = 0; k < s->nflt; k++) {
        int f = s->flt_t[k]; float a = s->flt_v[k];
        if (f == 0) { if (a >= 0.5f) { unsigned* tmp = malloc((size_t)w * (size_t)h * 4); if (!tmp) continue; for (int y = 0; y < h; y++) memcpy(tmp + (size_t)y * w, FB + (size_t)(y0 + y) * STR + x0, (size_t)w * 4); flt_blur(tmp, w, h, (int)(a * 0.85f + 0.5f)); for (int y = 0; y < h; y++) memcpy(FB + (size_t)(y0 + y) * STR + x0, tmp + (size_t)y * w, (size_t)w * 4); free(tmp); } continue; }
        float m[9]; flt_matrix(f, a, m);
        for (int y = 0; y < h; y++) {
            unsigned* row = FB + (size_t)(y0 + y) * STR + x0; const unsigned* br = before + (size_t)y * w;
            for (int x = 0; x < w; x++) {
                unsigned v = row[x] & 0xFFFFFF, bv = br[x] & 0xFFFFFF;
                if (v == bv) continue;
                float r = (float)((v >> 16) & 255), g = (float)((v >> 8) & 255), b = (float)(v & 255), nr, ng, nb;
                if (f == 1) { nr = r * a; ng = g * a; nb = b * a; }
                else if (f == 2) { nr = (r - 127.5f) * a + 127.5f; ng = (g - 127.5f) * a + 127.5f; nb = (b - 127.5f) * a + 127.5f; }
                else if (f == 4) { float t = a > 1 ? 1 : a; nr = r + (255 - 2 * r) * t; ng = g + (255 - 2 * g) * t; nb = b + (255 - 2 * b) * t; }
                else if (f == 5) { float t = a > 1 ? 1 : a; float or_ = (float)((bv >> 16) & 255), og = (float)((bv >> 8) & 255), ob = (float)(bv & 255); nr = or_ + (r - or_) * t; ng = og + (g - og) * t; nb = ob + (b - ob) * t; }
                else { nr = m[0] * r + m[1] * g + m[2] * b; ng = m[3] * r + m[4] * g + m[5] * b; nb = m[6] * r + m[7] * g + m[8] * b; }
                int ir = (int)(nr + 0.5f), ig = (int)(ng + 0.5f), ib = (int)(nb + 0.5f);
                if (ir < 0) ir = 0; if (ir > 255) ir = 255; if (ig < 0) ig = 0; if (ig > 255) ig = 255; if (ib < 0) ib = 0; if (ib > 255) ib = 255;
                row[x] = ((unsigned)ir << 16) | ((unsigned)ig << 8) | (unsigned)ib;
            }
        }
    }
}

static void paint_box_inner(box* b, float bdx, float bdy, float nop, clipr* c, plist2* pos, int depth);
static void paint_box(box* b, float dx, float dy, float op, clipr* c, plist2* pos, int depth) {
    if (b->st && b->st->clipall) return;
    if (b->st && b->st->ovx != OV_VISIBLE && b->w <= 1.5f && b->h <= 1.5f) return;
    if (depth > 200) return;
    style* s = b->st;
    if (!s) return;
    float nop = op * s->opacity;
    if (nop <= 0.003f) return;
    if (b->n && b->n->tag == T_DIALOG && attr_get(b->n, "nvx-modal") && attr_get(b->n, "open")) rect((float)c->x0, (float)c->y0, (float)(c->x1 - c->x0), (float)(c->y1 - c->y0), 0x1A000000u, c);
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
    unsigned* fbefore = 0; int fx0 = 0, fy0 = 0, fx1 = 0, fy1 = 0;
    if (s->nflt && b->kind != BX_INLINE && b->kind != BX_ANON) {
        float br = 0; for (int k = 0; k < s->nflt; k++) if (s->flt_t[k] == 0 && s->flt_v[k] * 3 > br) br = s->flt_v[k] * 3;
        fx0 = (int)floorf(b->x + bdx - br); fy0 = (int)floorf(b->y + bdy - br); fx1 = (int)ceilf(b->x + bdx + b->w + br); fy1 = (int)ceilf(b->y + bdy + b->h + br);
        if (fx0 < c->x0) fx0 = c->x0; if (fy0 < c->y0) fy0 = c->y0; if (fx1 > c->x1) fx1 = c->x1; if (fy1 > c->y1) fy1 = c->y1;
        if (fx1 > fx0 && fy1 > fy0 && (long)(fx1 - fx0) * (fy1 - fy0) < 16000000L) { fbefore = malloc((size_t)(fx1 - fx0) * (size_t)(fy1 - fy0) * 4); if (fbefore) for (int y = fy0; y < fy1; y++) memcpy(fbefore + (size_t)(y - fy0) * (fx1 - fx0), FB + (size_t)y * STR + fx0, (size_t)(fx1 - fx0) * 4); }
    }
    if (fbefore) { paint_box_inner(b, bdx, bdy, nop, c, pos, depth); flt_apply(s, fx0, fy0, fx1, fy1, fbefore); free(fbefore); return; }
    paint_box_inner(b, bdx, bdy, nop, c, pos, depth);
}
static void paint_box_inner(box* b, float bdx, float bdy, float nop, clipr* c, plist2* pos, int depth) {
    style* s = b->st;
    if (!s->visibility && b->kind != BX_INLINE && b->kind != BX_ANON) paint_bg_border(b, bdx, bdy, nop, c);
    if (b->kind == BX_REPLACED) { if (!s->visibility) paint_replaced(b, bdx, bdy, nop, c); return; }
    clipr cc;
    clip_children(b, bdx, bdy, c, &cc);
    if (cc.x0 >= cc.x1 || cc.y0 >= cc.y1) return;
    float sdy = bdy - b->sy, sdx = bdx - b->sx;
    paint_frags(b, sdx, sdy, nop, &cc, 0);
    paint_children(b, sdx, sdy, nop, &cc, pos, depth);
    paint_frags(b, sdx, sdy, nop, &cc, 1);
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
    if (f->n == LS_DCLOSED || f->n == LS_DOPEN) {
        float t = s->fsize * 0.5f, x0 = f->x + dx, yc = f->base + dy - s->fsize * 0.35f;
        int n = (int)(t + 0.5f); if (n < 3) n = 3;
        for (int i = 0; i < n; i++) {
            float k = (float)i / (float)n;
            if (f->n == LS_DCLOSED) { float hh = t * (1.0f - k); rect(x0 + i, yc - hh / 2, 1, hh, col, c); }
            else { float ww = t * (1.0f - k); rect(x0 + (t - ww) / 2, yc - t / 2 + i, ww, 1, col, c); }
        }
        return;
    }
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
            {
                extern unsigned g_hsel_gen; extern int g_hsel_on; extern node* g_hs_node; extern int g_hs_off; extern node* g_he_node; extern int g_he_off;
                node* tn = f->b ? f->b->n : 0;
                if (g_hsel_on && tn && tn->type == NT_TEXT && tn->selgen == g_hsel_gen && tn->text && f->s >= tn->text) {
                    int fo = (int)(f->s - tn->text);
                    int a = tn == g_hs_node ? g_hs_off : 0, z = tn == g_he_node ? g_he_off : tn->tlen;
                    int la = a > fo ? a - fo : 0, lb = z < fo + f->n ? z - fo : f->n;
                    if (la < lb) { float xa = la ? text_width(s->face, s->fsize, f->s, la, s->lspace) : 0, xb = text_width(s->face, s->fsize, f->s, lb, s->lspace); if (lb == f->n && (tn != g_he_node || z > fo + f->n) && i + 1 < b->nfr && fabsf(b->fr[i + 1].y - f->y) < 1 && b->fr[i + 1].x > f->x + xb && b->fr[i + 1].x - f->x - xb < s->fsize) xb = b->fr[i + 1].x - f->x; rect(f->x + dx + xa, f->y + dy, xb - xa, f->h, with_alpha(0xFFB4D5FEu, o2), c); }
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
    for (int i = 0; i < b->nfr; i++) { frag* f = &b->fr[i]; float fx0 = f->x + bdx - b->sx, fy0 = f->y + bdy - b->sy; if (fx0 < bb[0]) bb[0] = fx0; if (fy0 < bb[1]) bb[1] = fy0; if (fx0 + f->w > bb[2]) bb[2] = fx0 + f->w; if (fy0 + f->h > bb[3]) bb[3] = fy0 + f->h; }
    if (b->st->ovx != OV_VISIBLE && b->st->ovy != OV_VISIBLE && depth > 0) return;
    for (box* k = b->first; k; k = k->next) sub_bounds(k, bdx - b->sx, bdy - b->sy, bb, depth + 1, cnt);
}
static void k_m4(box* k, float* M) {
    tf_matrix4(k->st, k->w, k->h, M);
    box* pb = k->parent;
    if (!pb || !pb->st || pb->st->persp <= 0) return;
    float plx = pb->x + lres(pb->st->pox, pb->w), ply = pb->y + lres(pb->st->poy, pb->h);
    float elx = k->x + k->ox - k->btx + lres(k->st->tox, k->w), ely = k->y + k->oy - k->bty + lres(k->st->toy, k->h);
    float ex = plx - elx, ey = ply - ely, ip = -1.0f / pb->st->persp;
    float R[16];
    for (int j = 0; j < 4; j++) {
        float m0 = M[j], m1 = M[4 + j], m2 = M[8 + j], m3 = M[12 + j];
        float w = ip * m2 + m3;
        R[j] = m0 - ex * m3 + ex * w;
        R[4 + j] = m1 - ey * m3 + ey * w;
        R[8 + j] = m2;
        R[12 + j] = w;
    }
    memcpy(M, R, sizeof R);
}
static void m4_h(const float* M, float* H) { H[0] = M[0]; H[1] = M[1]; H[2] = M[3]; H[3] = M[4]; H[4] = M[5]; H[5] = M[7]; H[6] = M[12]; H[7] = M[13]; H[8] = M[15]; }
static float h_det(const float* H) { return H[0] * (H[4] * H[8] - H[5] * H[7]) - H[1] * (H[3] * H[8] - H[5] * H[6]) + H[2] * (H[3] * H[7] - H[4] * H[6]); }
static int bf_hidden(box* k) {
    float sgn = 1;
    if (k->st->hastransform) { float M[16], H[9]; k_m4(k, M); m4_h(M, H); if (h_det(H) < 0) sgn = -sgn; }
    for (box* p = k->parent; p && p->st && p->st->p3d; p = p->parent) {
        if (!p->st->hastransform) continue;
        float M[16], H[9]; k_m4(p, M); m4_h(M, H); if (h_det(H) < 0) sgn = -sgn;
    }
    return sgn < 0;
}
static void paint_stack(box* b, float dx, float dy, float op, clipr* c, int depth);
static void paint_transformed(box* k, float dx, float dy, float op, clipr* c, int depth) {
    style* s = k->st;
    float m[6], M4[16], H[9];
    k_m4(k, M4); m4_h(M4, H);
    int proj = fabsf(H[6]) > 1e-7f || fabsf(H[7]) > 1e-7f || fabsf(H[8] - 1) > 1e-6f;
    if (!proj) { m[0] = H[0]; m[1] = H[3]; m[2] = H[1]; m[3] = H[4]; m[4] = H[2]; m[5] = H[5]; }
    else { if (H[8] <= 0) return; m[0] = H[0] / H[8]; m[1] = H[3] / H[8]; m[2] = H[1] / H[8]; m[3] = H[4] / H[8]; m[4] = H[2] / H[8]; m[5] = H[5] / H[8]; }
    float ddx = m[4] - k->btx, ddy = m[5] - k->bty;
    int lin = proj || fabsf(m[0] - 1) > 1e-4f || fabsf(m[1]) > 1e-4f || fabsf(m[2]) > 1e-4f || fabsf(m[3] - 1) > 1e-4f;
    if (!lin) { paint_stack(k, dx + ddx, dy + ddy, op, c, depth); return; }
    float det = proj ? h_det(H) : m[0] * m[3] - m[1] * m[2];
    if (fabsf(det) < 1e-9f) return;
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
        if (proj) { float w = H[6] * ux + H[7] * uy + H[8]; if (w < 0.02f) w = 0.02f; cx[i] = ox + (H[0] * ux + H[1] * uy + H[2]) / w; cy[i] = oy + (H[3] * ux + H[4] * uy + H[5]) / w; }
        else {
        cx[i] = ox + m[0] * ux + m[2] * uy + m[4];
        cy[i] = oy + m[1] * ux + m[3] * uy + m[5];
        }
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
    float Hi[9];
    if (proj) {
        Hi[0] = (H[4] * H[8] - H[5] * H[7]) / det; Hi[1] = (H[2] * H[7] - H[1] * H[8]) / det; Hi[2] = (H[1] * H[5] - H[2] * H[4]) / det;
        Hi[3] = (H[5] * H[6] - H[3] * H[8]) / det; Hi[4] = (H[0] * H[8] - H[2] * H[6]) / det; Hi[5] = (H[2] * H[3] - H[0] * H[5]) / det;
        Hi[6] = (H[3] * H[7] - H[4] * H[6]) / det; Hi[7] = (H[1] * H[6] - H[0] * H[7]) / det; Hi[8] = (H[0] * H[4] - H[1] * H[3]) / det;
    }
    for (int y = dy0; y < dy1; y++) {
        unsigned* row = FB + (size_t)y * (size_t)STR;
        float vy = (float)y + 0.5f - oy - m[5];
        for (int x = dx0; x < dx1; x++) {
            float vx = (float)x + 0.5f - ox - m[4];
            float sx, sy;
            if (proj) {
                float px = (float)x + 0.5f - ox, py = (float)y + 0.5f - oy;
                float hw = Hi[6] * px + Hi[7] * py + Hi[8];
                if (fabsf(hw) < 1e-9f) continue;
                float u = (Hi[0] * px + Hi[1] * py + Hi[2]) / hw, v = (Hi[3] * px + Hi[4] * py + Hi[5]) / hw;
                if (H[6] * u + H[7] * v + H[8] <= 0) continue;
                sx = u + ox - (float)rx0 - 0.5f; sy = v + oy - (float)ry0 - 0.5f;
            } else {
                sx = ia * vx + ic * vy + ox - (float)rx0 - 0.5f;
                sy = ib * vx + id * vy + oy - (float)ry0 - 0.5f;
            }
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
                for (box* q = cb; q; q = q->parent) { ax += q->ox - q->sx; ay += q->oy - q->sy; }
                if (cb) { for (box* q = cb; q; q = q->parent) if (q->st && q->st->position == P_FIXED) { ay += g_scroll; break; } }
                kdx = (float)g_vx + ax;
                kdy = (float)g_vy - g_scroll + ay;
            }
        }
        if (k->st->bfhide && bf_hidden(k)) continue;
        if (k->st->hastransform && (k->st->hasmatrix || k->st->nanim || k->st->ntrans)) paint_transformed(k, kdx, kdy, p->op, &kc, depth + 1);
        else paint_stack(k, kdx, kdy, p->op, &kc, depth + 1);
    }
}

void paint_all_in(unsigned* fb, int stride, int fbw, int fbh, int vx, int vy, int vw, int vh, float scroll);
void paint_all(unsigned* fb, int stride, int fbw, int fbh, int vx, int vy, int vw, int vh, float scroll) {
    unsigned long long t0 = nwk_us();
    paint_all_in(fb, stride, fbw, fbh, vx, vy, vw, vh, scroll);
    g_nwe_perf.paint_us += nwk_us() - t0;
}
void paint_all_in(unsigned* fb, int stride, int fbw, int fbh, int vx, int vy, int vw, int vh, float scroll) {
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
        for (box* q = fb2; q; q = q->parent) { ax += q->ox - (q != fb2 ? q->sx : 0); ay += q->oy - (q != fb2 ? q->sy : 0); }
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
