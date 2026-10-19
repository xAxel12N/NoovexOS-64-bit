#include "web/nwe.h"

#define WM_MAX 8
#define ST_PLAYING 1
#define ST_PAUSED 2
#define ST_ENDED 4
#define ST_ERROR 8
#define ST_VIDEO 16
#define ST_AUDIO 32
#define ST_WAITING 64
#define ST_READY 128

typedef struct {
    node* n;
    int id;
    int dx, dy, dw, dh;
    int cx0, cy0, cx1, cy1;
    int painted, ctl, audio, opaque;
    unsigned long long seq;
    long long lastsec;
    int vw, vh;
    unsigned lastflags;
    int sw, sh;
    int started;
} mreg;

static mreg R[WM_MAX];
int g_wm_offscreen;
static int g_ox, g_oy;

static mreg* find(node* n) { for (int i = 0; i < WM_MAX; i++) if (R[i].n == n && R[i].id >= 0 && R[i].n) return &R[i]; return 0; }

void wm_free(node* n) {
    mreg* m = find(n);
    if (m) { nwk_media_free(m->id); memset(m, 0, sizeof *m); }
    if (n) n->media = 0;
}

void wm_reset_all(void) {
    for (int i = 0; i < WM_MAX; i++) if (R[i].n) { nwk_media_free(R[i].id); if (R[i].n) R[i].n->media = 0; }
    memset(R, 0, sizeof R);
}

int wm_create(node* n, const char* url, int split, long long start_ms, int paused) {
    wm_free(n);
    int slot = -1;
    for (int i = 0; i < WM_MAX; i++) if (!R[i].n) { slot = i; break; }
    if (slot < 0) return -1;
    int w = 640, h = 360;
    if (n->box && n->box->w > 16 && n->box->h > 16) { w = (int)n->box->w; h = (int)n->box->h; }
    int id = nwk_media_new(url ? url : "", split, w, h, start_ms, paused);
    if (id < 0) return -1;
    mreg* m = &R[slot];
    memset(m, 0, sizeof *m);
    m->n = n;
    m->id = id;
    m->sw = w;
    m->sh = h;
    m->lastsec = -1;
    m->audio = n->tag == T_AUDIO;
    n->media = id + 1;
    return id;
}

int wm_id(node* n) { mreg* m = find(n); return m ? m->id : -1; }
int wm_dims(node* n, int* w, int* h) { mreg* m = find(n); if (!m || m->vw <= 0 || m->vh <= 0) return 0; *w = m->vw; *h = m->vh; return 1; }

static void fillr(unsigned* fb, int stride, int x, int y, int w, int h, unsigned col, int a, int cx0, int cy0, int cx1, int cy1) {
    int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y, x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h;
    for (int yy = y0; yy < y1; yy++) {
        unsigned* row = fb + (size_t)yy * (size_t)stride;
        if (a >= 255) for (int xx = x0; xx < x1; xx++) row[xx] = col;
        else for (int xx = x0; xx < x1; xx++) {
            unsigned d = row[xx];
            unsigned r = (((d >> 16) & 255) * (unsigned)(255 - a) + ((col >> 16) & 255) * (unsigned)a) / 255;
            unsigned g = (((d >> 8) & 255) * (unsigned)(255 - a) + ((col >> 8) & 255) * (unsigned)a) / 255;
            unsigned b = ((d & 255) * (unsigned)(255 - a) + (col & 255) * (unsigned)a) / 255;
            row[xx] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
}

static void tri(unsigned* fb, int stride, int x, int y, int s, unsigned col, int cx0, int cy0, int cx1, int cy1) {
    for (int r = 0; r < s; r++) { int l = r < s / 2 ? r : s - 1 - r; fillr(fb, stride, x, y + r, l + 1, 1, col, 255, cx0, cy0, cx1, cy1); }
}

static void tfmt(char* o, long long ms) {
    if (ms < 0) ms = 0;
    long long s = ms / 1000;
    if (s >= 3600) snprintf(o, 16, "%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    else snprintf(o, 16, "%lld:%02lld", s / 60, s % 60);
}

static void controls(mreg* m, unsigned* fb, int stride, int x, int y, int w, int h, int cx0, int cy0, int cx1, int cy1) {
    long long v[6] = { 0, -1, 0, 0, 0, 0 };
    nwk_media_status(m->id, v);
    unsigned fl = (unsigned)v[2];
    int bh = 36;
    if (h < bh) bh = h;
    int by = y + h - bh;
    fillr(fb, stride, x, by, w, bh, 0x000000, m->audio ? 255 : 150, cx0, cy0, cx1, cy1);
    int playing = (fl & ST_PLAYING) && !(fl & ST_ENDED);
    int px = x + 12, py = by + bh / 2 - 8;
    if (playing) { fillr(fb, stride, px, py, 5, 16, 0xFFFFFF, 255, cx0, cy0, cx1, cy1); fillr(fb, stride, px + 9, py, 5, 16, 0xFFFFFF, 255, cx0, cy0, cx1, cy1); }
    else tri(fb, stride, px, py - 1, 18, 0xFFFFFF, cx0, cy0, cx1, cy1);
    char a[16], b[16], t[40];
    tfmt(a, v[0]);
    tfmt(b, v[1]);
    if (v[1] > 0) snprintf(t, sizeof t, "%s / %s", a, b); else snprintf(t, sizeof t, "%s", a);
    float tw = text_width(F_SANS, 12, t, (int)strlen(t), 0);
    font_draw(fb, stride, cx0, cy0, cx1, cy1, F_SANS, 12, (float)(x + 36), (float)(by + bh / 2 + 4), t, (int)strlen(t), 0xFFFFFFFF, 0);
    int sx = x + 36 + (int)tw + 12, sw = x + w - 14 - sx;
    if (sw > 20) {
        fillr(fb, stride, sx, by + bh / 2 - 2, sw, 4, 0x808080, 255, cx0, cy0, cx1, cy1);
        if (v[1] > 0) {
            int f = (int)((long long)sw * v[0] / v[1]);
            if (f < 0) f = 0;
            if (f > sw) f = sw;
            fillr(fb, stride, sx, by + bh / 2 - 2, f, 4, 0xFF0033, 255, cx0, cy0, cx1, cy1);
            fillr(fb, stride, sx + f - 5, by + bh / 2 - 5, 10, 10, 0xFF0033, 255, cx0, cy0, cx1, cy1);
        }
    }
}

void wm_begin_paint(int vx, int vy) {
    g_ox = vx;
    g_oy = vy;
    for (int i = 0; i < WM_MAX; i++) R[i].painted = 0;
}

static void blit(mreg* m, unsigned* fb, int stride) {
    int x = m->dx, y = m->dy, w = m->dw, h = m->dh;
    int cx0 = m->cx0, cy0 = m->cy0, cx1 = m->cx1, cy1 = m->cy1;
    int fw = 0, fh = 0;
    unsigned long long seq = 0;
    const unsigned* f = m->audio ? 0 : nwk_media_frame(m->id, &fw, &fh, &seq);
    m->seq = seq;
    if (!m->audio) {
        if (!f || fw <= 0 || fh <= 0) fillr(fb, stride, x, y, w, h, 0x000000, 255, cx0, cy0, cx1, cy1);
        else {
            int dw = w, dh = (int)((long long)w * fh / fw);
            if (dh > h) { dh = h; dw = (int)((long long)h * fw / fh); }
            if (dw < 1) dw = 1;
            if (dh < 1) dh = 1;
            int dx = x + (w - dw) / 2, dy = y + (h - dh) / 2;
            if (dw != w || dh != h) {
                fillr(fb, stride, x, y, w, dy - y, 0, 255, cx0, cy0, cx1, cy1);
                fillr(fb, stride, x, dy + dh, w, y + h - dy - dh, 0, 255, cx0, cy0, cx1, cy1);
                fillr(fb, stride, x, dy, dx - x, dh, 0, 255, cx0, cy0, cx1, cy1);
                fillr(fb, stride, dx + dw, dy, x + w - dx - dw, dh, 0, 255, cx0, cy0, cx1, cy1);
            }
            int x0 = dx < cx0 ? cx0 : dx, x1 = dx + dw > cx1 ? cx1 : dx + dw;
            int y0 = dy < cy0 ? cy0 : dy, y1 = dy + dh > cy1 ? cy1 : dy + dh;
            if (x0 < x1 && y0 < y1) {
                unsigned sxs = (unsigned)(((unsigned long long)fw << 16) / (unsigned)dw), sys = (unsigned)(((unsigned long long)fh << 16) / (unsigned)dh);
                for (int yy = y0; yy < y1; yy++) {
                    unsigned sy = (unsigned)(((unsigned long long)(yy - dy) * sys) >> 16);
                    if (sy >= (unsigned)fh) sy = (unsigned)fh - 1;
                    const unsigned* srow = f + (size_t)sy * (size_t)fw;
                    unsigned* d = fb + (size_t)yy * (size_t)stride;
                    if (dw == fw) { for (int xx = x0; xx < x1; xx++) d[xx] = srow[xx - dx] | 0xFF000000u; continue; }
                    unsigned sx = (unsigned)(x0 - dx) * sxs;
                    for (int xx = x0; xx < x1; xx++, sx += sxs) { unsigned si = sx >> 16; if (si >= (unsigned)fw) si = (unsigned)fw - 1; d[xx] = srow[si] | 0xFF000000u; }
                }
            }
        }
        long long v[6] = { 0, -1, 0, 0, 0, 0 };
        nwk_media_status(m->id, v);
        unsigned fl = (unsigned)v[2];
        if (!m->ctl && (fl & ST_WAITING) && f) {
            int cxm = x + w / 2, cym = y + h / 2;
            fillr(fb, stride, cxm - 20, cym - 20, 40, 40, 0x000000, 140, cx0, cy0, cx1, cy1);
        }
        if (m->ctl && !(fl & ST_PLAYING) && !m->started && w > 80 && h > 80) {
            int cxm = x + w / 2, cym = y + h / 2;
            fillr(fb, stride, cxm - 34, cym - 24, 68, 48, 0xFF0033, 230, cx0, cy0, cx1, cy1);
            tri(fb, stride, cxm - 8, cym - 12, 24, 0xFFFFFF, cx0, cy0, cx1, cy1);
        }
    }
    if (m->ctl) controls(m, fb, stride, x, y, w, h, cx0, cy0, cx1, cy1);
}

void wm_paint(node* n, float x, float y, float w, float h, int cx0, int cy0, int cx1, int cy1, float op, unsigned* fb, int stride) {
    mreg* m = find(n);
    int ctl = attr_get(n, "controls") != 0;
    if (m && g_wm_offscreen) { mreg tmp = *m; tmp.dx = (int)x; tmp.dy = (int)y; tmp.dw = (int)w; tmp.dh = (int)h; tmp.cx0 = cx0; tmp.cy0 = cy0; tmp.cx1 = cx1; tmp.cy1 = cy1; tmp.ctl = ctl; blit(&tmp, fb, stride); return; }
    if (!m) {
        if (n->tag == T_VIDEO) fillr(fb, stride, (int)x, (int)y, (int)w, (int)h, 0x000000, 255, cx0, cy0, cx1, cy1);
        if (n->tag == T_AUDIO && ctl) {
            fillr(fb, stride, (int)x, (int)y, (int)w, (int)h, 0xF1F3F4, 255, cx0, cy0, cx1, cy1);
            tri(fb, stride, (int)x + 14, (int)(y + h / 2) - 9, 18, 0x202124, cx0, cy0, cx1, cy1);
        }
        return;
    }
    m->dx = (int)x;
    m->dy = (int)y;
    m->dw = (int)w;
    m->dh = (int)h;
    m->cx0 = cx0; m->cy0 = cy0; m->cx1 = cx1; m->cy1 = cy1;
    m->ctl = ctl;
    m->opaque = op >= 0.99f;
    m->painted = 1;
    if (!m->audio && m->dw >= 16 && m->dh >= 16 && (m->dw != m->sw || m->dh != m->sh)) { m->sw = m->dw; m->sh = m->dh; nwk_media_cmd(m->id, 5, m->dw, m->dh); }
    blit(m, fb, stride);
}

int wm_fast(unsigned* fb, int stride, int* rx, int* ry, int* rw, int* rh) {
    int n = 0, x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
    for (int i = 0; i < WM_MAX; i++) {
        mreg* m = &R[i];
        if (!m->n || !m->painted || m->audio) continue;
        int fw, fh;
        unsigned long long seq = 0;
        if (!nwk_media_frame(m->id, &fw, &fh, &seq) || seq == m->seq) continue;
        if (!m->opaque) return -1;
        blit(m, fb, stride);
        int a = m->dx < m->cx0 ? m->cx0 : m->dx, b = m->dy < m->cy0 ? m->cy0 : m->dy;
        int c = m->dx + m->dw > m->cx1 ? m->cx1 : m->dx + m->dw, d = m->dy + m->dh > m->cy1 ? m->cy1 : m->dy + m->dh;
        if (c <= a || d <= b) continue;
        if (a < x0) x0 = a;
        if (b < y0) y0 = b;
        if (c > x1) x1 = c;
        if (d > y1) y1 = d;
        n++;
    }
    if (!n) return 0;
    *rx = x0; *ry = y0; *rw = x1 - x0; *rh = y1 - y0;
    return n;
}

int wm_tick(void) {
    int dirty = 0;
    for (int i = 0; i < WM_MAX; i++) {
        mreg* m = &R[i];
        if (!m->n) continue;
        long long v[6] = { 0, -1, 0, 0, 0, 0 };
        if (nwk_media_status(m->id, v) < 0) continue;
        unsigned fl = (unsigned)v[2];
        if (fl & ST_PLAYING) m->started = 1;
        if (!m->audio && v[3] > 0 && v[4] > 0 && (v[3] != m->vw || v[4] != m->vh)) { m->vw = (int)v[3]; m->vh = (int)v[4]; dirty |= 2; }
        if (fl != m->lastflags) { m->lastflags = fl; dirty = 1; }
        if (m->ctl && v[0] / 1000 != m->lastsec) { m->lastsec = v[0] / 1000; dirty = 1; }
        if (!m->painted) continue;
        int fw, fh;
        unsigned long long seq = 0;
        if (!m->audio && nwk_media_frame(m->id, &fw, &fh, &seq) && m->seq == 0 && seq) dirty = 1;
    }
    return dirty;
}

int wm_click(node* n, float vx, float vy) {
    mreg* m = find(n);
    if (!m || !m->ctl) return -100;
    int x = (int)vx + g_ox, y = (int)vy + g_oy;
    int bh = m->dh < 36 ? m->dh : 36;
    if (y < m->dy + m->dh - bh) return -1;
    if (x < m->dx + 32) return -1;
    long long v[6] = { 0, -1, 0, 0, 0, 0 };
    nwk_media_status(m->id, v);
    char a[16], b[16], t[40];
    tfmt(a, v[0]);
    tfmt(b, v[1]);
    if (v[1] > 0) snprintf(t, sizeof t, "%s / %s", a, b); else snprintf(t, sizeof t, "%s", a);
    int sx = m->dx + 36 + (int)text_width(F_SANS, 12, t, (int)strlen(t), 0) + 12, sw = m->dx + m->dw - 14 - sx;
    if (sw > 20 && x >= sx - 6 && x <= sx + sw + 6) {
        int r = x - sx;
        if (r < 0) r = 0;
        if (r > sw) r = sw;
        return r * 10000 / sw;
    }
    return -1;
}
