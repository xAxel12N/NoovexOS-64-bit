#include "web/nwe.h"

void nwe_acc_line(float* acc, int w, int h, float x0, float y0, float x1, float y1);

typedef struct { float a, b, c, d, e, f; } mat;
static mat mmul(mat m, mat n) { mat r = { m.a * n.a + m.c * n.b, m.b * n.a + m.d * n.b, m.a * n.c + m.c * n.d, m.b * n.c + m.d * n.d, m.a * n.e + m.c * n.f + m.e, m.b * n.e + m.d * n.f + m.f }; return r; }
static void mapp(mat m, float x, float y, float* ox, float* oy) { *ox = m.a * x + m.c * y + m.e; *oy = m.b * x + m.d * y + m.f; }

typedef struct { float* xy; int n, cap; int* starts; int ns, scap; } poly;
static void padd2(poly* p, float x, float y) {
    if (p->n + 1 >= p->cap) { int nc = p->cap ? p->cap * 2 : 128; float* nx = nalloc(sizeof(float) * 2 * (size_t)nc); if (p->n) memcpy(nx, p->xy, sizeof(float) * 2 * (size_t)p->n); p->xy = nx; p->cap = nc; }
    p->xy[p->n * 2] = x;
    p->xy[p->n * 2 + 1] = y;
    p->n++;
}
static void pstart(poly* p) {
    if (p->ns >= p->scap) { int nc = p->scap ? p->scap * 2 : 16; int* ns = nalloc(sizeof(int) * (size_t)nc); if (p->ns) memcpy(ns, p->starts, sizeof(int) * (size_t)p->ns); p->starts = ns; p->scap = nc; }
    p->starts[p->ns++] = p->n;
}

static const char* num(const char* p, float* v) {
    while (*p && (is_ws((unsigned char)*p) || *p == ',')) p++;
    const char* e;
    *v = parse_float(p, &e);
    if (e == p) return 0;
    return e;
}
static const char* flag(const char* p, float* v) {
    while (*p && (is_ws((unsigned char)*p) || *p == ',')) p++;
    if (*p == '0' || *p == '1') { *v = (float)(*p - '0'); return p + 1; }
    return num(p, v);
}

static mat g_m;
static float g_tol;
static void L2(poly* p, float x, float y) { float a, b; mapp(g_m, x, y, &a, &b); padd2(p, a, b); }
static int segs_for(float x0, float y0, float x1, float y1, float x2, float y2) {
    float a, b, c, d, e, f;
    mapp(g_m, x0, y0, &a, &b);
    mapp(g_m, x1, y1, &c, &d);
    mapp(g_m, x2, y2, &e, &f);
    float l = sqrtf((c - a) * (c - a) + (d - b) * (d - b)) + sqrtf((e - c) * (e - c) + (f - d) * (f - d));
    int n = (int)(l / 3) + 2;
    return n > 40 ? 40 : n;
}

static void arc_to(poly* p, float x1, float y1, float rx, float ry, float rot, int large, int sweep, float x2, float y2) {
    if (rx == 0 || ry == 0) { L2(p, x2, y2); return; }
    rx = fabsf(rx);
    ry = fabsf(ry);
    float phi = rot * 3.14159265f / 180, cp = cosf(phi), sp = sinf(phi);
    float dx = (x1 - x2) / 2, dy = (y1 - y2) / 2;
    float x1p = cp * dx + sp * dy, y1p = -sp * dx + cp * dy;
    float lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lam > 1) { float s = sqrtf(lam); rx *= s; ry *= s; }
    float num_ = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
    float den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
    float co = den > 0 ? sqrtf(fmaxf(0, num_ / den)) : 0;
    if (large == sweep) co = -co;
    float cxp = co * rx * y1p / ry, cyp = -co * ry * x1p / rx;
    float cx = cp * cxp - sp * cyp + (x1 + x2) / 2, cy = sp * cxp + cp * cyp + (y1 + y2) / 2;
    float ux = (x1p - cxp) / rx, uy = (y1p - cyp) / ry, vx = (-x1p - cxp) / rx, vy = (-y1p - cyp) / ry;
    float t1 = atan2f(uy, ux);
    float dt = atan2f(ux * vy - uy * vx, ux * vx + uy * vy);
    if (!sweep && dt > 0) dt -= 6.2831853f;
    else if (sweep && dt < 0) dt += 6.2831853f;
    float sx, sy;
    mapp(g_m, rx, ry, &sx, &sy);
    int n = (int)(fabsf(dt) * fmaxf(rx, ry) * fmaxf(fabsf(g_m.a), fabsf(g_m.d)) / 3) + 4;
    if (n > 64) n = 64;
    for (int i = 1; i <= n; i++) {
        float t = t1 + dt * i / n;
        float ex = rx * cosf(t), ey = ry * sinf(t);
        L2(p, cp * ex - sp * ey + cx, sp * ex + cp * ey + cy);
    }
}

static void parse_path(const char* d, poly* p) {
    float cx = 0, cy = 0, sx = 0, sy = 0, lcx = 0, lcy = 0;
    char cmd = 0, lastcmd = 0;
    const char* q = d;
    int guard = 0;
    while (*q && guard++ < 100000) {
        while (*q && (is_ws((unsigned char)*q) || *q == ',')) q++;
        if (!*q) break;
        if ((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z')) { cmd = *q++; if (cmd == 'z' || cmd == 'Z') { if (p->ns) L2(p, sx, sy); cx = sx; cy = sy; lastcmd = cmd; continue; } }
        else if (!cmd) break;
        int rel = cmd >= 'a';
        char C = (char)(rel ? cmd - 32 : cmd);
        float v[7];
        const char* r = q;
        int need = C == 'M' || C == 'L' || C == 'T' ? 2 : C == 'H' || C == 'V' ? 1 : C == 'C' ? 6 : C == 'S' || C == 'Q' ? 4 : C == 'A' ? 7 : 0;
        if (!need) break;
        int ok = 1;
        for (int i = 0; i < need; i++) {
            if (C == 'A' && (i == 3 || i == 4)) r = flag(r, &v[i]); else r = num(r, &v[i]);
            if (!r) { ok = 0; break; }
        }
        if (!ok) break;
        q = r;
        float bx = rel ? cx : 0, by = rel ? cy : 0;
        switch (C) {
        case 'M':
            cx = v[0] + bx; cy = v[1] + by; sx = cx; sy = cy;
            pstart(p);
            L2(p, cx, cy);
            cmd = rel ? 'l' : 'L';
            break;
        case 'L': cx = v[0] + bx; cy = v[1] + by; L2(p, cx, cy); break;
        case 'H': cx = v[0] + (rel ? cx : 0); L2(p, cx, cy); break;
        case 'V': cy = v[0] + (rel ? cy : 0); L2(p, cx, cy); break;
        case 'C': case 'S': {
            float x1, y1, x2, y2, x, y;
            if (C == 'C') { x1 = v[0] + bx; y1 = v[1] + by; x2 = v[2] + bx; y2 = v[3] + by; x = v[4] + bx; y = v[5] + by; }
            else {
                int prevc = lastcmd == 'C' || lastcmd == 'c' || lastcmd == 'S' || lastcmd == 's';
                x1 = prevc ? 2 * cx - lcx : cx; y1 = prevc ? 2 * cy - lcy : cy;
                x2 = v[0] + bx; y2 = v[1] + by; x = v[2] + bx; y = v[3] + by;
            }
            int n = segs_for(cx, cy, x1, y1, x, y) + 2;
            for (int i = 1; i <= n; i++) {
                float t = (float)i / n, u = 1 - t;
                L2(p, u * u * u * cx + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x, u * u * u * cy + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y);
            }
            lcx = x2; lcy = y2; cx = x; cy = y;
            break;
        }
        case 'Q': case 'T': {
            float x1, y1, x, y;
            if (C == 'Q') { x1 = v[0] + bx; y1 = v[1] + by; x = v[2] + bx; y = v[3] + by; }
            else {
                int prevq = lastcmd == 'Q' || lastcmd == 'q' || lastcmd == 'T' || lastcmd == 't';
                x1 = prevq ? 2 * cx - lcx : cx; y1 = prevq ? 2 * cy - lcy : cy;
                x = v[0] + bx; y = v[1] + by;
            }
            int n = segs_for(cx, cy, x1, y1, x, y);
            for (int i = 1; i <= n; i++) { float t = (float)i / n, u = 1 - t; L2(p, u * u * cx + 2 * u * t * x1 + t * t * x, u * u * cy + 2 * u * t * y1 + t * t * y); }
            lcx = x1; lcy = y1; cx = x; cy = y;
            break;
        }
        case 'A': {
            float x = v[5] + bx, y = v[6] + by;
            arc_to(p, cx, cy, v[0], v[1], v[2], v[3] != 0, v[4] != 0, x, y);
            cx = x; cy = y;
            break;
        }
        }
        lastcmd = cmd;
        if (C == 'M') lastcmd = 'M';
    }
}

static unsigned* FB;
static int STR, CX0, CY0, CX1, CY1;

static inline unsigned blend(unsigned dst, unsigned col, int a) {
    unsigned rb = (((col & 0xFF00FF) * (unsigned)a) + ((dst & 0xFF00FF) * (unsigned)(255 - a)));
    unsigned g = (((col & 0x00FF00) * (unsigned)a) + ((dst & 0x00FF00) * (unsigned)(255 - a)));
    return (((rb + 0x800080) >> 8) & 0xFF00FF) | (((g + 0x008000) >> 8) & 0x00FF00);
}

static void fill_poly(poly* p, unsigned col, int evenodd) {
    if (p->n < 2 || (col >> 24) == 0) return;
    float mnx = 1e9f, mny = 1e9f, mxx = -1e9f, mxy = -1e9f;
    for (int i = 0; i < p->n; i++) {
        float x = p->xy[i * 2], y = p->xy[i * 2 + 1];
        if (x < mnx) mnx = x;
        if (x > mxx) mxx = x;
        if (y < mny) mny = y;
        if (y > mxy) mxy = y;
    }
    int ox = (int)floorf(mnx) - 1, oy = (int)floorf(mny) - 1;
    int w = (int)ceilf(mxx) - ox + 3, h = (int)ceilf(mxy) - oy + 2;
    if (w <= 0 || h <= 0 || w > 3000 || h > 3000) return;
    if (ox > CX1 || oy > CY1 || ox + w < CX0 || oy + h < CY0) return;
    float* acc = nzalloc(sizeof(float) * (size_t)(w * h + 1));
    for (int s = 0; s < p->ns; s++) {
        int a = p->starts[s], b = s + 1 < p->ns ? p->starts[s + 1] : p->n;
        if (b - a < 2) continue;
        for (int i = a; i < b; i++) {
            int j = i + 1 < b ? i + 1 : a;
            nwe_acc_line(acc, w, h, p->xy[i * 2] - ox, p->xy[i * 2 + 1] - oy, p->xy[j * 2] - ox, p->xy[j * 2 + 1] - oy);
        }
    }
    int ca = (int)(col >> 24);
    float sum = 0;
    for (int y = 0; y < h; y++) {
        int py = oy + y;
        for (int x = 0; x < w; x++) {
            sum += acc[y * w + x];
            float v = fabsf(sum);
            if (evenodd) { v = fmodf(v, 2.0f); if (v > 1) v = 2 - v; }
            else if (v > 1) v = 1;
            int px = ox + x;
            if (v <= 0.002f || px < CX0 || px >= CX1 || py < CY0 || py >= CY1) continue;
            int a = (int)(v * ca);
            unsigned* d = FB + (size_t)py * (size_t)STR + px;
            *d = a >= 255 ? (col & 0xFFFFFF) : blend(*d, col, a);
        }
    }
    nfree(acc);
}

static void stroke_poly(poly* p, float sw, unsigned col, int closed_hint) {
    if (sw <= 0 || (col >> 24) == 0) return;
    poly q = { 0 };
    float hw = sw / 2;
    for (int s = 0; s < p->ns; s++) {
        int a = p->starts[s], b = s + 1 < p->ns ? p->starts[s + 1] : p->n;
        for (int i = a; i + 1 < b; i++) {
            float x0 = p->xy[i * 2], y0 = p->xy[i * 2 + 1], x1 = p->xy[i * 2 + 2], y1 = p->xy[i * 2 + 3];
            float dx = x1 - x0, dy = y1 - y0;
            float l = sqrtf(dx * dx + dy * dy);
            if (l < 0.01f) continue;
            float nx = -dy / l * hw, ny = dx / l * hw;
            float ex = dx / l * hw * 0.5f, ey = dy / l * hw * 0.5f;
            pstart(&q);
            padd2(&q, x0 + nx - ex, y0 + ny - ey);
            padd2(&q, x1 + nx + ex, y1 + ny + ey);
            padd2(&q, x1 - nx + ex, y1 - ny + ey);
            padd2(&q, x0 - nx - ex, y0 - ny - ey);
        }
    }
    (void)closed_hint;
    if (q.n == 0) return;
    for (int s = 0; s < q.ns; s++) {
        poly one = { q.xy + q.starts[s] * 2, 4, 4, 0, 0, 0 };
        int st0 = 0;
        one.starts = &st0;
        one.ns = 1;
        fill_poly(&one, col, 0);
    }
}

static const char* sattr(node* n, const char* name, char* buf, int cap) {
    const char* st = attr_get(n, "style");
    if (st) {
        const char* p = st;
        int nl = (int)strlen(name);
        while ((p = strstr(p, name))) {
            if ((p == st || p[-1] == ';' || p[-1] == ' ') && (p[nl] == ':' || p[nl] == ' ')) {
                const char* v = strchr(p, ':');
                if (v) {
                    v++;
                    while (*v == ' ') v++;
                    int k = 0;
                    while (v[k] && v[k] != ';' && k < cap - 1) { buf[k] = v[k]; k++; }
                    while (k > 0 && buf[k - 1] == ' ') k--;
                    buf[k] = 0;
                    return buf;
                }
            }
            p += nl;
        }
    }
    return attr_get(n, name);
}

typedef struct { unsigned fill, stroke; float sw, op, fop, sop; int evenodd; int nofill, nostroke; } sstate;

static unsigned resolve_col(const char* v, node* n, unsigned def, int* none) {
    *none = 0;
    if (!v) return def;
    while (*v == ' ') v++;
    if (ieq(v, "none") || ieq(v, "transparent")) { *none = 1; return 0; }
    if (ieq(v, "currentColor")) return n->st ? n->st->color : 0xFF000000;
    if (istarts(v, "url(")) return 0xFF808080;
    unsigned c;
    if (css_parse_color(v, (int)strlen(v), &c)) return c;
    return def;
}

static mat parse_transform(const char* t) {
    mat m = { 1, 0, 0, 1, 0, 0 };
    if (!t) return m;
    const char* p = t;
    while (*p) {
        while (*p && (is_ws((unsigned char)*p) || *p == ',')) p++;
        const char* fs = p;
        while (*p && *p != '(') p++;
        if (!*p) break;
        int fl = (int)(p - fs);
        p++;
        float v[6] = { 0, 0, 0, 0, 0, 0 };
        int nv = 0;
        while (*p && *p != ')' && nv < 6) { const char* r = num(p, &v[nv]); if (!r) break; p = r; nv++; while (*p == ' ' || *p == ',') p++; }
        while (*p && *p != ')') p++;
        if (*p) p++;
        mat n = { 1, 0, 0, 1, 0, 0 };
        if (fl == 9 && !strncmp(fs, "translate", 9)) { n.e = v[0]; n.f = nv > 1 ? v[1] : 0; }
        else if (fl == 5 && !strncmp(fs, "scale", 5)) { n.a = v[0]; n.d = nv > 1 ? v[1] : v[0]; }
        else if (fl == 6 && !strncmp(fs, "matrix", 6) && nv == 6) { n.a = v[0]; n.b = v[1]; n.c = v[2]; n.d = v[3]; n.e = v[4]; n.f = v[5]; }
        else if (fl == 6 && !strncmp(fs, "rotate", 6)) {
            float a = v[0] * 3.14159265f / 180, c = cosf(a), s = sinf(a);
            mat r = { c, s, -s, c, 0, 0 };
            if (nv == 3) { mat t1 = { 1, 0, 0, 1, v[1], v[2] }, t2 = { 1, 0, 0, 1, -v[1], -v[2] }; n = mmul(mmul(t1, r), t2); } else n = r;
        }
        m = mmul(m, n);
    }
    return m;
}

static void draw_el(node* n, mat m, sstate s, int depth);

static float fat(node* n, const char* a) { const char* v = attr_get(n, a); return v ? parse_float(v, 0) : 0; }

static void shape(node* n, mat m, sstate* s) {
    poly p = { 0 };
    g_m = m;
    int closed = 1;
    int t = n->tag;
    if (t == T_PATH) { const char* d = attr_get(n, "d"); if (!d) return; parse_path(d, &p); closed = 0; }
    else if (t == T_RECT) {
        float x = fat(n, "x"), y = fat(n, "y"), w = fat(n, "width"), h = fat(n, "height"), rx = fat(n, "rx"), ry = fat(n, "ry");
        if (rx && !ry) ry = rx;
        if (ry && !rx) rx = ry;
        if (rx > w / 2) rx = w / 2;
        if (ry > h / 2) ry = h / 2;
        pstart(&p);
        if (rx > 0) {
            for (int c = 0; c < 4; c++) {
                float ccx = c == 0 || c == 3 ? x + w - rx : x + rx, ccy = c < 2 ? y + h - ry : y + ry;
                float a0 = c * 1.5707963f;
                for (int i = 0; i <= 6; i++) { float a = a0 + 1.5707963f * i / 6; L2(&p, ccx + rx * cosf(a), ccy + ry * sinf(a)); }
            }
        } else { L2(&p, x, y); L2(&p, x + w, y); L2(&p, x + w, y + h); L2(&p, x, y + h); }
    } else if (t == T_CIRCLE || t == T_ELLIPSE) {
        float cx = fat(n, "cx"), cy = fat(n, "cy"), rx, ry;
        if (t == T_CIRCLE) rx = ry = fat(n, "r"); else { rx = fat(n, "rx"); ry = fat(n, "ry"); }
        if (rx <= 0 || ry <= 0) return;
        pstart(&p);
        float sc = fmaxf(fabsf(m.a), fabsf(m.d));
        int seg = (int)(fmaxf(rx, ry) * sc) + 12;
        if (seg > 96) seg = 96;
        for (int i = 0; i < seg; i++) { float a = 6.2831853f * i / seg; L2(&p, cx + rx * cosf(a), cy + ry * sinf(a)); }
    } else if (t == T_LINE) {
        pstart(&p);
        L2(&p, fat(n, "x1"), fat(n, "y1"));
        L2(&p, fat(n, "x2"), fat(n, "y2"));
        closed = 0;
        sstate s2 = *s;
        s2.nofill = 1;
        *s = s2;
    } else if (t == T_POLYLINE || t == T_POLYGON) {
        const char* pts = attr_get(n, "points");
        if (!pts) return;
        pstart(&p);
        const char* q = pts;
        float a, b;
        while ((q = num(q, &a)) && (q = num(q, &b))) L2(&p, a, b);
        closed = t == T_POLYGON;
    } else return;
    float scale = sqrtf(fabsf(m.a * m.d - m.b * m.c));
    if (!s->nofill && t != T_LINE) fill_poly(&p, ((unsigned)(((s->fill >> 24) * s->op * s->fop)) << 24) | (s->fill & 0xFFFFFF), s->evenodd);
    if (!s->nostroke && s->sw > 0) {
        if (closed || t == T_PATH) {
            for (int k = 0; k < p.ns; k++) { int a = p.starts[k]; if (t != T_PATH || closed) padd2(&p, p.xy[a * 2], p.xy[a * 2 + 1]); break; }
        }
        stroke_poly(&p, s->sw * scale, ((unsigned)(((s->stroke >> 24) * s->op * s->sop)) << 24) | (s->stroke & 0xFFFFFF), closed);
    }
}

static sstate inherit_state(node* n, sstate s) {
    char buf[96];
    const char* v;
    int none;
    if (n->st && n->st->hasfill) { s.fill = n->st->fill; s.nofill = (n->st->fill >> 24) == 0; }
    else if ((v = sattr(n, "fill", buf, sizeof(buf)))) { s.fill = resolve_col(v, n, s.fill, &none); s.nofill = none; }
    if (n->st && n->st->hasstroke) { s.stroke = n->st->stroke; s.nostroke = (n->st->stroke >> 24) == 0; }
    else if ((v = sattr(n, "stroke", buf, sizeof(buf)))) { s.stroke = resolve_col(v, n, s.stroke, &none); s.nostroke = none; }
    if ((v = sattr(n, "stroke-width", buf, sizeof(buf)))) s.sw = parse_float(v, 0);
    if ((v = sattr(n, "opacity", buf, sizeof(buf)))) s.op *= parse_float(v, 0);
    if ((v = sattr(n, "fill-opacity", buf, sizeof(buf)))) s.fop = parse_float(v, 0);
    if ((v = sattr(n, "stroke-opacity", buf, sizeof(buf)))) s.sop = parse_float(v, 0);
    if ((v = sattr(n, "fill-rule", buf, sizeof(buf)))) s.evenodd = ieq(v, "evenodd");
    if ((v = sattr(n, "display", buf, sizeof(buf))) && ieq(v, "none")) s.op = 0;
    if ((v = sattr(n, "visibility", buf, sizeof(buf))) && ieq(v, "hidden")) s.op = 0;
    return s;
}


extern int css_face_for(const char* fam, int weight, int italic);
typedef struct { char s[1024]; int n; } tbuf;
static void tcollect(node* n, tbuf* b, int d) { if (d > 8) return; for (node* c = n->first; c; c = c->next) { if (c->type == NT_TEXT) { for (int i = 0; i < c->tlen && b->n < 1023; i++) { char ch = c->text[i]; if (ch == '\n' || ch == '\t' || ch == '\r') ch = ' '; if (ch == ' ' && b->n && b->s[b->n - 1] == ' ') continue; b->s[b->n++] = ch; } } else if (c->type == NT_ELEM) tcollect(c, b, d + 1); } }
static void svg_text(node* n, mat m, sstate* st) {
    tbuf b; b.n = 0;
    tcollect(n, &b, 0);
    if (!b.n) return;
    char buf[128]; const char* v;
    float fs = n->st ? n->st->fsize : 16;
    if ((v = sattr(n, "font-size", buf, sizeof(buf)))) { float f = parse_float(v, 0); if (f > 0) fs = f; }
    int wt = n->st ? n->st->fweight : 400;
    if ((v = sattr(n, "font-weight", buf, sizeof(buf)))) { if (ieq(v, "bold") || ieq(v, "bolder")) wt = 700; else if (ieq(v, "normal")) wt = 400; else { float f = parse_float(v, 0); if (f > 0) wt = (int)f; } }
    int it = n->st ? n->st->fstyle : 0;
    if ((v = sattr(n, "font-style", buf, sizeof(buf)))) it = ieq(v, "italic") || ieq(v, "oblique");
    const char* fam = n->st ? n->st->ffamily : 0;
    char fb[128];
    if ((v = sattr(n, "font-family", fb, sizeof(fb)))) fam = v;
    int face = css_face_for(fam, wt, it);
    float x = fat(n, "x"), y = fat(n, "y");
    float sc = sqrtf(fabsf(m.a * m.d - m.b * m.c));
    float size = fs * sc;
    if (size < 1 || size > 2000) return;
    int s0 = 0, e = b.n; while (s0 < e && b.s[s0] == ' ') s0++; while (e > s0 && b.s[e - 1] == ' ') e--;
    float tw = text_width(face, size, b.s + s0, e - s0, 0);
    float px = m.a * x + m.c * y + m.e, py = m.b * x + m.d * y + m.f;
    if ((v = sattr(n, "text-anchor", buf, sizeof(buf)))) { if (ieq(v, "middle")) px -= tw / 2; else if (ieq(v, "end")) px -= tw; }
    if ((v = sattr(n, "dominant-baseline", buf, sizeof(buf)))) { if (ieq(v, "middle") || ieq(v, "central")) py += size * 0.35f; else if (ieq(v, "hanging")) py += size * 0.8f; }
    if (st->nofill) return;
    unsigned col = st->fill;
    float a = ((col >> 24) & 255) / 255.0f * st->op * st->fop;
    unsigned c2 = (col & 0xFFFFFF) | ((unsigned)(a * 255.0f + 0.5f) << 24);
    font_draw(FB, STR, CX0, CY0, CX1, CY1, face, size, px, py, b.s + s0, e - s0, c2, 0);
}
static void draw_el(node* n, mat m, sstate s, int depth) {
    if (depth > 30 || n->type != NT_ELEM) return;
    if (n->st && n->st->display == D_NONE && n->tag != T_SVG) return;
    const char* nm = n->name;
    if (ieq(nm, "defs") || ieq(nm, "clippath") || ieq(nm, "mask") || ieq(nm, "lineargradient") || ieq(nm, "radialgradient") || ieq(nm, "symbol") || ieq(nm, "title") || ieq(nm, "desc") || ieq(nm, "style") || ieq(nm, "pattern") || ieq(nm, "filter") || ieq(nm, "marker")) return;
    s = inherit_state(n, s);
    if (s.op <= 0) return;
    const char* tr = attr_get(n, "transform");
    if (tr) m = mmul(m, parse_transform(tr));
    if (n->tag == T_USE) {
        const char* h = attr_get(n, "href");
        if (!h) h = attr_get(n, "xlink:href");
        if (h && h[0] == '#') {
            node* ref = find_id(G.doc, h + 1);
            if (ref && !node_contains(ref, n)) {
                mat t = { 1, 0, 0, 1, fat(n, "x"), fat(n, "y") };
                mat mm = mmul(m, t);
                if (ieq(ref->name, "symbol")) {
                    const char* vb = attr_get(ref, "viewBox");
                    if (!vb) vb = attr_get(ref, "viewbox");
                    float w = fat(n, "width"), h2 = fat(n, "height");
                    if (vb && w > 0 && h2 > 0) {
                        float v[4] = { 0, 0, 0, 0 };
                        const char* q = vb;
                        for (int i = 0; i < 4 && q; i++) q = num(q, &v[i]);
                        if (v[2] > 0 && v[3] > 0) { float k = fminf(w / v[2], h2 / v[3]); mat sc = { k, 0, 0, k, -v[0] * k + (w - v[2] * k) / 2, -v[1] * k + (h2 - v[3] * k) / 2 }; mm = mmul(mm, sc); }
                    }
                    for (node* c = ref->first; c; c = c->next) draw_el(c, mm, s, depth + 1);
                } else draw_el(ref, mm, s, depth + 1);
            }
        }
        return;
    }
    if (ieq(nm, "text")) { svg_text(n, m, &s); return; }
    if (n->tag == T_PATH || n->tag == T_RECT || n->tag == T_CIRCLE || n->tag == T_ELLIPSE || n->tag == T_LINE || n->tag == T_POLYLINE || n->tag == T_POLYGON) { sstate s2 = s; shape(n, m, &s2); return; }
    if (n->tag == T_SVG && depth > 0) {
        mat t = { 1, 0, 0, 1, fat(n, "x"), fat(n, "y") };
        m = mmul(m, t);
    }
    for (node* c = n->first; c; c = c->next) draw_el(c, m, s, depth + 1);
}

void svg_paint(node* n, float x, float y, float w, float h, float op, unsigned* fb, int stride, int cx0, int cy0, int cx1, int cy1) {
    FB = fb;
    STR = stride;
    CX0 = cx0 > (int)floorf(x) ? cx0 : (int)floorf(x);
    CY0 = cy0 > (int)floorf(y) ? cy0 : (int)floorf(y);
    CX1 = cx1 < (int)ceilf(x + w) ? cx1 : (int)ceilf(x + w);
    CY1 = cy1 < (int)ceilf(y + h) ? cy1 : (int)ceilf(y + h);
    if (CX0 >= CX1 || CY0 >= CY1) return;
    const char* vb = attr_get(n, "viewBox");
    if (!vb) vb = attr_get(n, "viewbox");
    float v[4] = { 0, 0, w, h };
    if (vb) { const char* q = vb; for (int i = 0; i < 4 && q; i++) q = num(q, &v[i]); }
    if (v[2] <= 0 || v[3] <= 0) { v[2] = w; v[3] = h; }
    float kx = w / v[2], ky = h / v[3];
    const char* par = attr_get(n, "preserveAspectRatio");
    mat m;
    if (par && istarts(par, "none")) { mat t = { kx, 0, 0, ky, x - v[0] * kx, y - v[1] * ky }; m = t; }
    else { float k = fminf(kx, ky); mat t = { k, 0, 0, k, x - v[0] * k + (w - v[2] * k) / 2, y - v[1] * k + (h - v[3] * k) / 2 }; m = t; }
    sstate s = { n->st ? n->st->color : 0xFF000000, 0xFF000000, 1, op, 1, 1, 0, 0, 1 };
    s.fill = 0xFF000000;
    g_tol = 0.3f;
    draw_el(n, m, s, 0);
}
