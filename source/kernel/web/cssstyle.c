#include "web/nwe.h"
#include "web/css.h"

#define PN(a, b) b,
const char* css_prop_names[] = { PROPS(PN) };
#undef PN

static short ph[1024];
static int ph_ready;
static unsigned phash(const char* s) { unsigned h = 2166136261u; while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; } return h; }
int css_prop_id(const char* name) {
    if (!ph_ready) {
        for (int i = 0; i < 1024; i++) ph[i] = -1;
        for (int i = 0; i < P_NPROPS; i++) { unsigned h = phash(css_prop_names[i]) & 1023; while (ph[h] >= 0) h = (h + 1) & 1023; ph[h] = (short)i; }
        ph_ready = 1;
    }
    if (name[0] == '-' && name[1] == 'w' && istarts(name, "-webkit-") && strcmp(name, "-webkit-line-clamp")) name += 8;
    else if (name[0] == '-' && (istarts(name, "-moz-") || istarts(name, "-ms-"))) name += name[1] == 'm' && name[2] == 'o' ? 5 : 4;
    unsigned h = phash(name) & 1023;
    while (ph[h] >= 0) { if (!strcmp(css_prop_names[ph[h]], name)) return ph[h]; h = (h + 1) & 1023; }
    return -1;
}

float lres(L l, float base) {
    float v = l.px + l.pct * base / 100.0f;
    if (v < l.lo) v = l.lo;
    if (v > l.hi) v = l.hi;
    return v;
}
int lauto(L l) { return l.k != LK_VAL; }
static L LV(float px) { L l = { px, 0, -1e30f, 1e30f, LK_VAL }; return l; }
static L LA(void) { L l = { 0, 0, -1e30f, 1e30f, LK_AUTO }; return l; }
static L LN(void) { L l = { 0, 0, -1e30f, 1e30f, LK_NONE }; return l; }

static const char* named_colors =
    "aliceblue f0f8ff antiquewhite faebd7 aqua 00ffff aquamarine 7fffd4 azure f0ffff beige f5f5dc bisque ffe4c4 black 000000 blanchedalmond ffebcd blue 0000ff "
    "blueviolet 8a2be2 brown a52a2a burlywood deb887 cadetblue 5f9ea0 chartreuse 7fff00 chocolate d2691e coral ff7f50 cornflowerblue 6495ed cornsilk fff8dc "
    "crimson dc143c cyan 00ffff darkblue 00008b darkcyan 008b8b darkgoldenrod b8860b darkgray a9a9a9 darkgreen 006400 darkgrey a9a9a9 darkkhaki bdb76b "
    "darkmagenta 8b008b darkolivegreen 556b2f darkorange ff8c00 darkorchid 9932cc darkred 8b0000 darksalmon e9967a darkseagreen 8fbc8f darkslateblue 483d8b "
    "darkslategray 2f4f4f darkslategrey 2f4f4f darkturquoise 00ced1 darkviolet 9400d3 deeppink ff1493 deepskyblue 00bfff dimgray 696969 dimgrey 696969 "
    "dodgerblue 1e90ff firebrick b22222 floralwhite fffaf0 forestgreen 228b22 fuchsia ff00ff gainsboro dcdcdc ghostwhite f8f8ff gold ffd700 goldenrod daa520 "
    "gray 808080 green 008000 greenyellow adff2f grey 808080 honeydew f0fff0 hotpink ff69b4 indianred cd5c5c indigo 4b0082 ivory fffff0 khaki f0e68c "
    "lavender e6e6fa lavenderblush fff0f5 lawngreen 7cfc00 lemonchiffon fffacd lightblue add8e6 lightcoral f08080 lightcyan e0ffff lightgoldenrodyellow fafad2 "
    "lightgray d3d3d3 lightgreen 90ee90 lightgrey d3d3d3 lightpink ffb6c1 lightsalmon ffa07a lightseagreen 20b2aa lightskyblue 87cefa lightslategray 778899 "
    "lightslategrey 778899 lightsteelblue b0c4de lightyellow ffffe0 lime 00ff00 limegreen 32cd32 linen faf0e6 magenta ff00ff maroon 800000 "
    "mediumaquamarine 66cdaa mediumblue 0000cd mediumorchid ba55d3 mediumpurple 9370db mediumseagreen 3cb371 mediumslateblue 7b68ee mediumspringgreen 00fa9a "
    "mediumturquoise 48d1cc mediumvioletred c71585 midnightblue 191970 mintcream f5fffa mistyrose ffe4e1 moccasin ffe4b5 navajowhite ffdead navy 000080 "
    "oldlace fdf5e6 olive 808000 olivedrab 6b8e23 orange ffa500 orangered ff4500 orchid da70d6 palegoldenrod eee8aa palegreen 98fb98 paleturquoise afeeee "
    "palevioletred db7093 papayawhip ffefd5 peachpuff ffdab9 peru cd853f pink ffc0cb plum dda0dd powderblue b0e0e6 purple 800080 rebeccapurple 663399 "
    "red ff0000 rosybrown bc8f8f royalblue 4169e1 saddlebrown 8b4513 salmon fa8072 sandybrown f4a460 seagreen 2e8b57 seashell fff5ee sienna a0522d "
    "silver c0c0c0 skyblue 87ceeb slateblue 6a5acd slategray 708090 slategrey 708090 snow fffafa springgreen 00ff7f steelblue 4682b4 tan d2b48c teal 008080 "
    "thistle d8bfd8 tomato ff6347 turquoise 40e0d0 violet ee82ee wheat f5deb3 white ffffff whitesmoke f5f5f5 yellow ffff00 yellowgreen 9acd32 "
    "canvas ffffff canvastext 000000 linktext 0000ee buttonface efefef buttontext 000000 field ffffff fieldtext 000000 graytext 808080 highlight 3390ff "
    "highlighttext ffffff buttonborder 767676 accentcolor 0075ff accentcolortext ffffff mark ffff00 marktext 000000 visitedtext 551a8b activetext ff0000 ";

static int hexd(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static float hue2rgb(float p, float q, float t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.f / 6) return p + (q - p) * 6 * t;
    if (t < 0.5f) return q;
    if (t < 2.f / 3) return p + (q - p) * (2.f / 3 - t) * 6;
    return p;
}
static unsigned g_curcolor = 0xFF000000;
static int clamp255(float v) { return v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5f); }
int css_parse_color(const char* s, int n, unsigned* out) {
    while (n > 0 && is_ws((unsigned char)*s)) { s++; n--; }
    while (n > 0 && is_ws((unsigned char)s[n - 1])) n--;
    if (n <= 0) return 0;
    if (s[0] == '#') {
        int h[8], k = 0;
        for (int i = 1; i < n && k < 8; i++) { int d = hexd((unsigned char)s[i]); if (d < 0) return 0; h[k++] = d; }
        if (n - 1 != k) return 0;
        if (k == 3 || k == 4) { unsigned a = k == 4 ? (unsigned)(h[3] * 17) : 255; *out = (a << 24) | ((unsigned)h[0] * 17 << 16) | ((unsigned)h[1] * 17 << 8) | (unsigned)h[2] * 17; return 1; }
        if (k == 6 || k == 8) { unsigned a = k == 8 ? (unsigned)(h[6] * 16 + h[7]) : 255; *out = (a << 24) | ((unsigned)(h[0] * 16 + h[1]) << 16) | ((unsigned)(h[2] * 16 + h[3]) << 8) | (unsigned)(h[4] * 16 + h[5]); return 1; }
        return 0;
    }
    if (n >= 4 && (ieqn(s, "rgb(", 4) || ieqn(s, "rgba(", 5) || ieqn(s, "hsl(", 4) || ieqn(s, "hsla(", 5) || ieqn(s, "hwb(", 4))) {
        int hsl = lc(s[0]) == 'h' && lc(s[1]) == 's';
        int hwb = lc(s[0]) == 'h' && lc(s[1]) == 'w';
        const char* p = strchr(s, '(');
        if (!p || p - s > n) return 0;
        p++;
        float v[4] = { 0, 0, 0, 1 };
        int pct[4] = { 0, 0, 0, 0 };
        int k = 0;
        const char* e = s + n;
        while (p < e && k < 4) {
            while (p < e && (is_ws((unsigned char)*p) || *p == ',' || *p == '/')) p++;
            if (p >= e || *p == ')') break;
            if (istarts(p, "none")) { v[k] = 0; p += 4; k++; continue; }
            const char* q;
            v[k] = parse_float(p, &q);
            if (q == p) return 0;
            if (*q == '%') { pct[k] = 1; q++; }
            else if (istarts(q, "deg")) q += 3;
            else if (istarts(q, "turn")) { v[k] *= 360; q += 4; }
            else if (istarts(q, "rad")) { v[k] *= 57.2958f; q += 3; }
            p = q;
            k++;
        }
        if (k < 3) return 0;
        float a = pct[3] ? v[3] / 100 : v[3];
        if (a < 0) a = 0;
        if (a > 1) a = 1;
        int r, g, b;
        if (hsl || hwb) {
            float hh = fmodf(v[0], 360) / 360;
            if (hh < 0) hh += 1;
            float s1 = v[1] / 100, l1 = v[2] / 100;
            if (hwb) {
                float w = s1, bl = l1;
                if (w + bl >= 1) { float gr = w / (w + bl); r = g = b = clamp255(gr * 255); }
                else {
                    float q2 = 0.5f, p2 = 0.5f;
                    float rr = hue2rgb(0, 1, hh + 1.f / 3), gg = hue2rgb(0, 1, hh), bb = hue2rgb(0, 1, hh - 1.f / 3);
                    (void)q2; (void)p2;
                    r = clamp255((rr * (1 - w - bl) + w) * 255); g = clamp255((gg * (1 - w - bl) + w) * 255); b = clamp255((bb * (1 - w - bl) + w) * 255);
                }
            } else {
                float q2 = l1 < 0.5f ? l1 * (1 + s1) : l1 + s1 - l1 * s1, p2 = 2 * l1 - q2;
                r = clamp255(hue2rgb(p2, q2, hh + 1.f / 3) * 255);
                g = clamp255(hue2rgb(p2, q2, hh) * 255);
                b = clamp255(hue2rgb(p2, q2, hh - 1.f / 3) * 255);
            }
        } else {
            r = clamp255(pct[0] ? v[0] * 2.55f : v[0]);
            g = clamp255(pct[1] ? v[1] * 2.55f : v[1]);
            b = clamp255(pct[2] ? v[2] * 2.55f : v[2]);
        }
        *out = ((unsigned)clamp255(a * 255) << 24) | ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
        return 1;
    }
    if (n > 6 && (ieqn(s, "oklch(", 6) || ieqn(s, "oklab(", 6) || ieqn(s, "lab(", 4) || ieqn(s, "lch(", 4))) {
        int ok = lc(s[0]) == 'o';
        int polar = lc(s[ok ? 4 : 2]) == 'h';
        const char* p = strchr(s, '(');
        if (!p || p - s > n) return 0;
        p++;
        float v[4] = { 0, 0, 0, 1 };
        int pct[4] = { 0, 0, 0, 0 };
        int k = 0;
        const char* e = s + n;
        while (p < e && k < 4) {
            while (p < e && (is_ws((unsigned char)*p) || *p == ',' || *p == '/')) p++;
            if (p >= e || *p == ')') break;
            if (istarts(p, "none")) { v[k] = 0; p += 4; k++; continue; }
            const char* q;
            v[k] = parse_float(p, &q);
            if (q == p) return 0;
            if (*q == '%') { pct[k] = 1; q++; }
            else if (istarts(q, "deg")) q += 3;
            else if (istarts(q, "turn")) { v[k] *= 360; q += 4; }
            else if (istarts(q, "rad")) { v[k] *= 57.2958f; q += 3; }
            p = q;
            k++;
        }
        if (k < 3) return 0;
        float L = v[0], A, B;
        if (ok) { if (pct[0]) L /= 100; } else { if (pct[0]) L = L; }
        if (polar) {
            float C = v[1];
            if (pct[1]) C = ok ? C * 0.004f : C * 1.5f;
            float h = v[2] * 3.14159265f / 180;
            A = C * cosf(h); B = C * sinf(h);
        } else {
            A = v[1]; B = v[2];
            if (pct[1]) A = ok ? A * 0.004f : A * 1.25f;
            if (pct[2]) B = ok ? B * 0.004f : B * 1.25f;
        }
        float lr, lg, lb;
        if (ok) {
            float l_ = L + 0.3963377774f * A + 0.2158037573f * B;
            float m_ = L - 0.1055613458f * A - 0.0638541728f * B;
            float s_ = L - 0.0894841775f * A - 1.2914855480f * B;
            float l3 = l_ * l_ * l_, m3 = m_ * m_ * m_, s3 = s_ * s_ * s_;
            lr = 4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3;
            lg = -1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3;
            lb = -0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3;
        } else {
            float fy = (L + 16) / 116, fx = fy + A / 500, fz = fy - B / 200;
            float d = 6.f / 29;
            float X = (fx > d ? fx * fx * fx : 3 * d * d * (fx - 4.f / 29)) * 0.96422f;
            float Y = (fy > d ? fy * fy * fy : 3 * d * d * (fy - 4.f / 29));
            float Z = (fz > d ? fz * fz * fz : 3 * d * d * (fz - 4.f / 29)) * 0.82521f;
            float X6 = 0.9555766f * X - 0.0230393f * Y + 0.0631636f * Z;
            float Y6 = -0.0282895f * X + 1.0099416f * Y + 0.0210077f * Z;
            float Z6 = 0.0122982f * X - 0.0204830f * Y + 1.3299098f * Z;
            lr = 3.2404542f * X6 - 1.5371385f * Y6 - 0.4985314f * Z6;
            lg = -0.9692660f * X6 + 1.8760108f * Y6 + 0.0415560f * Z6;
            lb = 0.0556434f * X6 - 0.2040259f * Y6 + 1.0572252f * Z6;
        }
        float lin[3] = { lr, lg, lb };
        int c8[3];
        for (int i = 0; i < 3; i++) { float x = lin[i]; if (x < 0) x = 0; if (x > 1) x = 1; float sv = x <= 0.0031308f ? 12.92f * x : 1.055f * powf(x, 1 / 2.4f) - 0.055f; c8[i] = clamp255(sv * 255); }
        float a = pct[3] ? v[3] / 100 : v[3];
        if (a < 0) a = 0;
        if (a > 1) a = 1;
        *out = ((unsigned)clamp255(a * 255) << 24) | ((unsigned)c8[0] << 16) | ((unsigned)c8[1] << 8) | (unsigned)c8[2];
        return 1;
    }
    if (n > 10 && ieqn(s, "light-dark(", 11)) {
        const char* p = s + 11;
        const char* e = s + n;
        const char* st = p;
        int d = 0;
        while (p < e && !(*p == ',' && d == 0)) { if (*p == '(') d++; if (*p == ')') d--; p++; }
        return css_parse_color(st, (int)(p - st), out);
    }
    if (n > 6 && ieqn(s, "color(", 6)) {
        const char* p = s + 6;
        const char* e = s + n;
        while (p < e && is_ws((unsigned char)*p)) p++;
        while (p < e && !is_ws((unsigned char)*p)) p++;
        float v[4] = { 0, 0, 0, 1 };
        int k = 0;
        while (p < e && k < 4) {
            while (p < e && (is_ws((unsigned char)*p) || *p == '/')) p++;
            if (p >= e || *p == ')') break;
            const char* q;
            v[k] = parse_float(p, &q);
            if (q == p) return 0;
            if (*q == '%') { v[k] /= 100; q++; }
            p = q;
            k++;
        }
        if (k < 3) return 0;
        *out = ((unsigned)clamp255(v[3] * 255) << 24) | ((unsigned)clamp255(v[0] * 255) << 16) | ((unsigned)clamp255(v[1] * 255) << 8) | (unsigned)clamp255(v[2] * 255);
        return 1;
    }
    if (n == 6 && ieqn(s, "canvas", 6)) { *out = 0xFFFFFFFF; return 1; }
    if (n == 10 && ieqn(s, "canvastext", 10)) { *out = 0xFF000000; return 1; }
    if (n == 10 && ieqn(s, "buttonface", 10)) { *out = 0xFFEFEFEF; return 1; }
    if (n == 10 && ieqn(s, "buttontext", 10)) { *out = 0xFF000000; return 1; }
    if (n == 5 && ieqn(s, "field", 5)) { *out = 0xFFFFFFFF; return 1; }
    if (n == 9 && ieqn(s, "fieldtext", 9)) { *out = 0xFF000000; return 1; }
    if (n == 8 && ieqn(s, "graytext", 8)) { *out = 0xFF6D6D6D; return 1; }
    if (n == 9 && ieqn(s, "linktext", 9)) { *out = 0xFF0000EE; return 1; }
    if (n == 9 && ieqn(s, "highlight", 9)) { *out = 0xFFB4D5FE; return 1; }
    if (n == 11 && ieqn(s, "accentcolor", 11)) { *out = 0xFF0075FF; return 1; }
    if (n == 11 && ieqn(s, "transparent", 11)) { *out = 0; return 1; }
    if (n == 12 && ieqn(s, "currentcolor", 12)) { *out = g_curcolor; return 1; }
    if (ieqn(s, "color-mix(", 10)) {
        unsigned c1 = 0, c2 = 0;
        const char* p = s + 10;
        const char* e = s + n;
        while (p < e && *p != ',') p++;
        int idx = 0;
        float pc1 = 50;
        while (p < e && idx < 2) {
            p++;
            while (p < e && is_ws((unsigned char)*p)) p++;
            const char* st = p;
            int d = 0;
            while (p < e && !((*p == ',' || *p == ')') && d == 0)) { if (*p == '(') d++; if (*p == ')') d--; p++; }
            const char* en = p;
            const char* sp = en;
            while (sp > st && sp[-1] != ' ') sp--;
            float pc = -1;
            if (sp > st && en[-1] == '%') { pc = parse_float(sp, 0); en = sp; }
            unsigned c;
            if (!css_parse_color(st, (int)(en - st), &c)) return 0;
            if (idx == 0) { c1 = c; if (pc >= 0) pc1 = pc; } else { c2 = c; if (pc >= 0 && pc1 == 50) pc1 = 100 - pc; }
            idx++;
        }
        float t = pc1 / 100;
        float a1 = ((c1 >> 24) & 255) / 255.0f, a2 = ((c2 >> 24) & 255) / 255.0f, a = a1 * t + a2 * (1 - t);
        unsigned res = (unsigned)clamp255(a * 255.0f + 0.5f) << 24;
        for (int sh = 0; sh < 24; sh += 8) { float v = a > 0 ? (((c1 >> sh) & 255) * a1 * t + ((c2 >> sh) & 255) * a2 * (1 - t)) / a : 0; res |= (unsigned)clamp255(v + 0.5f) << sh; }
        *out = res;
        return 1;
    }
    if (n < 30) {
        char nm[32];
        for (int i = 0; i < n; i++) nm[i] = (char)lc((unsigned char)s[i]);
        nm[n] = 0;
        const char* p = named_colors;
        while (*p) {
            const char* ns = p;
            while (*p != ' ') p++;
            int nl = (int)(p - ns);
            p++;
            if (nl == n && !memcmp(ns, nm, (size_t)n)) {
                unsigned v = 0;
                for (int i = 0; i < 6; i++) v = v * 16 + (unsigned)hexd((unsigned char)p[i]);
                *out = 0xFF000000u | v;
                return 1;
            }
            p += 7;
        }
    }
    return 0;
}

typedef struct { const char* s; int n; } tok;
static int split_ws(const char* v, tok* t, int max) {
    int k = 0;
    const char* p = v;
    while (*p && k < max) {
        while (is_ws((unsigned char)*p)) p++;
        if (!*p) break;
        if (*p == ',' || *p == '/') { t[k].s = p; t[k].n = 1; k++; p++; continue; }
        const char* s = p;
        int d = 0;
        while (*p && !(d == 0 && (is_ws((unsigned char)*p) || *p == ',' || *p == '/'))) {
            if (*p == '(') d++;
            else if (*p == ')') d--;
            else if (*p == '"' || *p == '\'') { char q = *p++; while (*p && *p != q) p++; if (!*p) break; }
            p++;
        }
        t[k].s = s;
        t[k].n = (int)(p - s);
        k++;
    }
    return k;
}
static int split_comma(const char* v, tok* t, int max) {
    int k = 0;
    const char* p = v;
    while (*p && k < max) {
        while (is_ws((unsigned char)*p)) p++;
        const char* s = p;
        int d = 0;
        while (*p && !(d == 0 && *p == ',')) { if (*p == '(') d++; else if (*p == ')') d--; p++; }
        const char* e = p;
        while (e > s && is_ws((unsigned char)e[-1])) e--;
        t[k].s = s;
        t[k].n = (int)(e - s);
        k++;
        if (*p == ',') p++;
    }
    return k;
}
static int tkw(tok t, const char* k) { int l = (int)strlen(k); return t.n == l && ieqn(t.s, k, l); }
static int kw(const char* v, const char* k) { return ieq(v, k); }

typedef struct { style* st; style* par; float fs; float pfs; int forfont; } lctx;
static float g_rootfs = 16;

static int parse_len_tok(const char* s, int n, lctx* c, L* out);

static int parse_calc(const char** pp, const char* e, lctx* c, L* out);
static int calc_atom(const char** pp, const char* e, lctx* c, L* out) {
    const char* p = *pp;
    while (p < e && is_ws((unsigned char)*p)) p++;
    if (p < e && *p == '(') { p++; if (!parse_calc(&p, e, c, out)) return 0; while (p < e && is_ws((unsigned char)*p)) p++; if (p < e && *p == ')') p++; *pp = p; return 1; }
    if (p + 5 < e && ieqn(p, "calc(", 5)) { p += 5; if (!parse_calc(&p, e, c, out)) return 0; while (p < e && *p != ')') p++; if (p < e) p++; *pp = p; return 1; }
    if (p + 4 < e && (ieqn(p, "min(", 4) || ieqn(p, "max(", 4) || ieqn(p, "clamp(", 6))) {
        int fn = lc(p[1]) == 'i' ? 1 : lc(p[1]) == 'a' ? 2 : 3;
        p += fn == 3 ? 6 : 4;
        L args[8];
        int na = 0;
        while (p < e && na < 8) {
            L a;
            if (!parse_calc(&p, e, c, &a)) return 0;
            args[na++] = a;
            while (p < e && is_ws((unsigned char)*p)) p++;
            if (p < e && *p == ',') { p++; continue; }
            if (p < e && *p == ')') { p++; break; }
            return 0;
        }
        *pp = p;
        L r = args[0];
        if (fn == 3 && na == 3) {
            r = args[1];
            if (args[0].pct == 0) r.lo = args[0].px; else r.lo = r.lo;
            if (args[2].pct == 0) r.hi = args[2].px;
            if (r.pct == 0 && args[0].pct == 0 && args[2].pct == 0) { float v = r.px; if (v < r.lo) v = r.lo; if (v > r.hi) v = r.hi; r = LV(v); }
            *out = r;
            return 1;
        }
        int pctarg = -1;
        for (int i = 0; i < na; i++) if (args[i].pct != 0) pctarg = i;
        if (pctarg < 0) {
            float v = args[0].px;
            for (int i = 1; i < na; i++) v = fn == 1 ? fminf(v, args[i].px) : fmaxf(v, args[i].px);
            *out = LV(v);
            return 1;
        }
        r = args[pctarg];
        for (int i = 0; i < na; i++) if (i != pctarg && args[i].pct == 0) { if (fn == 1) r.hi = fminf(r.hi, args[i].px); else r.lo = fmaxf(r.lo, args[i].px); }
        *out = r;
        return 1;
    }
    if (p + 4 < e && ieqn(p, "var(", 4)) return 0;
    const char* st = p;
    int d = 0;
    while (p < e && !is_ws((unsigned char)*p) && *p != ')' && *p != ',' && !((*p == '*' || *p == '/') && d == 0)) { if (*p == '(') d++; p++; if ((*p == '+' || *p == '-') && p > st && (p[-1] != 'e' && p[-1] != 'E')) { if (!(p[-1] >= 'a' && p[-1] <= 'z') && !(p[-1] == '%')) {} } }
    if (p == st) return 0;
    if (!parse_len_tok(st, (int)(p - st), c, out)) {
        const char* q;
        float v = parse_float(st, &q);
        if (q != p) return 0;
        *out = LV(v);
        out->k = 99;
    }
    *pp = p;
    return 1;
}
static int calc_term(const char** pp, const char* e, lctx* c, L* out) {
    if (!calc_atom(pp, e, c, out)) return 0;
    for (;;) {
        const char* p = *pp;
        while (p < e && is_ws((unsigned char)*p)) p++;
        if (p < e && (*p == '*' || *p == '/')) {
            char op = *p++;
            L r;
            if (!calc_atom(&p, e, c, &r)) return 0;
            float f = r.px;
            if (op == '*') {
                if (out->k == 99) { float k = out->px; *out = r; out->px *= k; out->pct *= k; if (r.k == 99) out->k = 99; else out->k = LK_VAL; }
                else { out->px *= f; out->pct *= f; }
            } else if (f != 0) { out->px /= f; out->pct /= f; }
            *pp = p;
            continue;
        }
        return 1;
    }
}
static int parse_calc(const char** pp, const char* e, lctx* c, L* out) {
    if (!calc_term(pp, e, c, out)) return 0;
    for (;;) {
        const char* p = *pp;
        while (p < e && is_ws((unsigned char)*p)) p++;
        if (p < e && (*p == '+' || *p == '-')) {
            char op = *p++;
            L r;
            if (!calc_term(&p, e, c, &r)) return 0;
            if (op == '+') { out->px += r.px; out->pct += r.pct; } else { out->px -= r.px; out->pct -= r.pct; }
            if (r.k != 99) out->k = LK_VAL;
            *pp = p;
            continue;
        }
        if (out->k == 99) out->k = LK_VAL;
        return 1;
    }
}

static int parse_len_tok(const char* s, int n, lctx* c, L* out) {
    if (n <= 0) return 0;
    if ((n > 5 && ieqn(s, "calc(", 5)) || (n > 4 && (ieqn(s, "min(", 4) || ieqn(s, "max(", 4))) || (n > 6 && ieqn(s, "clamp(", 6))) {
        const char* p = s;
        L r;
        if (!calc_atom(&p, s + n, c, &r)) return 0;
        if (r.k == 99) r.k = LK_VAL;
        *out = r;
        return 1;
    }
    if (n >= 4 && ieqn(s, "env(", 4)) {
        const char* cm = memchr(s, ',', (size_t)n);
        if (cm) return parse_len_tok(cm + 1, (int)(s + n - cm - 2), c, out);
        *out = LV(0);
        return 1;
    }
    const char* e;
    float v = parse_float(s, &e);
    if (e == s) return 0;
    int ul = n - (int)(e - s);
    float fs = c->forfont ? c->pfs : c->fs;
    if (ul == 0) { if (v == 0 || G.quirks) { *out = LV(v); return 1; } return 0; }
    if (ul == 1 && *e == '%') { L l = LV(0); l.pct = v; if (c->forfont) { l = LV(v * c->pfs / 100); } *out = l; return 1; }
    char u[8];
    if (ul > 6) return 0;
    for (int i = 0; i < ul; i++) u[i] = (char)lc((unsigned char)e[i]);
    u[ul] = 0;
    float px;
    if (!strcmp(u, "px")) px = v;
    else if (!strcmp(u, "em")) px = v * fs;
    else if (!strcmp(u, "rem")) px = v * g_rootfs;
    else if (!strcmp(u, "vw") || !strcmp(u, "svw") || !strcmp(u, "lvw") || !strcmp(u, "dvw")) px = v * G.vw / 100;
    else if (!strcmp(u, "vh") || !strcmp(u, "svh") || !strcmp(u, "lvh") || !strcmp(u, "dvh")) px = v * G.vh / 100;
    else if (!strcmp(u, "vmin")) px = v * fminf(G.vw, G.vh) / 100;
    else if (!strcmp(u, "vmax")) px = v * fmaxf(G.vw, G.vh) / 100;
    else if (!strcmp(u, "pt")) px = v * 4 / 3;
    else if (!strcmp(u, "pc")) px = v * 16;
    else if (!strcmp(u, "in")) px = v * 96;
    else if (!strcmp(u, "cm")) px = v * 96 / 2.54f;
    else if (!strcmp(u, "mm")) px = v * 96 / 25.4f;
    else if (!strcmp(u, "q")) px = v * 96 / 101.6f;
    else if (!strcmp(u, "ex")) px = v * fs * 0.52f;
    else if (!strcmp(u, "ch")) px = v * fs * 0.55f;
    else if (!strcmp(u, "lh")) px = v * fs * 1.15f;
    else if (!strcmp(u, "rlh")) px = v * g_rootfs * 1.15f;
    else if (!strcmp(u, "cqw") || !strcmp(u, "cqi")) px = v * G.vw / 100;
    else if (!strcmp(u, "cqh") || !strcmp(u, "cqb")) px = v * G.vh / 100;
    else if (!strcmp(u, "fr")) return 0;
    else if (!strcmp(u, "deg") || !strcmp(u, "s") || !strcmp(u, "ms")) return 0;
    else return 0;
    *out = LV(px);
    return 1;
}
static int parse_len(const char* s, lctx* c, L* out, int allow_auto) {
    while (is_ws((unsigned char)*s)) s++;
    int n = (int)strlen(s);
    while (n > 0 && is_ws((unsigned char)s[n - 1])) n--;
    if (allow_auto && n == 4 && ieqn(s, "auto", 4)) { *out = LA(); return 1; }
    if (allow_auto == 2 && n == 4 && ieqn(s, "none", 4)) { *out = LN(); return 1; }
    if (n == 11 && ieqn(s, "min-content", 11)) { *out = LA(); out->k = LK_MINC; return 1; }
    if (n == 11 && ieqn(s, "max-content", 11)) { *out = LA(); out->k = LK_MAXC; return 1; }
    if ((n == 11 && ieqn(s, "fit-content", 11)) || (n > 12 && ieqn(s, "fit-content(", 12)) || (n == 19 && ieqn(s, "-webkit-fill-available", 19)) || (n == 9 && ieqn(s, "stretch", 7))) { *out = LA(); out->k = n == 11 ? LK_FITC : LK_AUTO; return 1; }
    if (n == 21 && ieqn(s, "-webkit-fill-available", 21)) { *out = LA(); return 1; }
    return parse_len_tok(s, n, c, out);
}
static int tlen(tok t, lctx* c, L* out, int allow_auto) {
    char b[256];
    int n = t.n < 255 ? t.n : 255;
    memcpy(b, t.s, (size_t)n);
    b[n] = 0;
    return parse_len(b, c, out, allow_auto);
}
static int tcolor(tok t, unsigned* out) { return css_parse_color(t.s, t.n, out); }

static char* subst_vars(const char* v, style* st, int depth) {
    if (!strstr(v, "var(") || depth > 8) return (char*)v;
    sbuf b = { 0, 0, 0 };
    const char* p = v;
    while (*p) {
        if (istarts(p, "var(")) {
            const char* q = p + 4;
            while (is_ws((unsigned char)*q)) q++;
            const char* ns = q;
            while (*q && *q != ',' && *q != ')' && !is_ws((unsigned char)*q)) q++;
            char nm[128];
            int nl = (int)(q - ns) < 127 ? (int)(q - ns) : 127;
            memcpy(nm, ns, (size_t)nl);
            nm[nl] = 0;
            while (is_ws((unsigned char)*q)) q++;
            const char* fb = 0;
            const char* fbe = 0;
            int d = 1;
            if (*q == ',') { fb = q + 1; }
            const char* r = q;
            while (*r && d > 0) { if (*r == '(') d++; else if (*r == ')') { d--; if (!d) break; } r++; }
            if (fb) fbe = r;
            const char* val = 0;
            for (cvar* cv = st->vars; cv; cv = cv->next) if (!strcmp(cv->name, nm)) { val = cv->val; break; }
            if (val && *val) sb_puts(&b, subst_vars(val, st, depth + 1));
            else if (fb) { char* f = nstrndup(fb, (int)(fbe - fb)); sb_puts(&b, subst_vars(f, st, depth + 1)); }
            else return 0;
            p = *r ? r + 1 : r;
            continue;
        }
        sb_putc(&b, *p++);
    }
    return sb_str(&b);
}

int css_face_for(const char* fam, int weight, int italic);
static int face_for(const char* fam, int weight, int italic) { return css_face_for(fam, weight, italic); }
int css_face_for(const char* fam, int weight, int italic) {
    int kind = 0;
    if (fam) {
        tok t[16];
        int k = split_comma(fam, t, 16);
        for (int i = 0; i < k && !kind; i++) {
            char f[64];
            int n = t[i].n < 63 ? t[i].n : 63;
            int o = 0;
            for (int j = 0; j < n; j++) if (t[i].s[j] != '"' && t[i].s[j] != '\'') f[o++] = (char)lc((unsigned char)t[i].s[j]);
            f[o] = 0;
            while (o > 0 && f[o - 1] == ' ') f[--o] = 0;
            { extern int wf_has(const char* fam); extern int wf_face(const char* fam, int weight, int italic); if (wf_has(f)) { int wf = wf_face(f, weight, italic); if (wf >= 0) return wf; } }
            if (strstr(f, "mono") || strstr(f, "courier") || strstr(f, "consolas") || strstr(f, "menlo") || strstr(f, "monaco") || strstr(f, "code") || strstr(f, "lucida console") || strstr(f, "sfmono")) kind = 3;
            else if (!strcmp(f, "serif") || strstr(f, "times") || strstr(f, "georgia") || strstr(f, "garamond") || strstr(f, "cambria") || strstr(f, "palatino") || strstr(f, "baskerville") || strstr(f, "book antiqua") || strstr(f, "merriweather") || strstr(f, "playfair") || strstr(f, "linux libertine") || (strstr(f, "serif") && !strstr(f, "sans"))) kind = 2;
            else if (strstr(f, "sans") || strstr(f, "arial") || strstr(f, "helvetica") || strstr(f, "verdana") || strstr(f, "tahoma") || strstr(f, "segoe") || strstr(f, "system") || strstr(f, "roboto") || strstr(f, "inter") || strstr(f, "apple") || strstr(f, "ubuntu") || strstr(f, "open sans") || strstr(f, "lato") || strstr(f, "noto") || strstr(f, "trebuchet")) kind = 1;
        }
    }
    int b = weight >= 600;
    if (kind == 3) return b ? F_MONO_B : F_MONO;
    if (kind == 2) return italic ? F_SERIF_I : b ? F_SERIF_B : F_SERIF;
    if (b && italic) return F_SANS_BI;
    if (b) return F_SANS_B;
    if (italic) return F_SANS_I;
    return F_SANS;
}

static void set_shadow(style* st, const char* v, lctx* c) {
    st->hasshadow = 0;
    if (kw(v, "none")) return;
    tok cm[4];
    split_comma(v, cm, 4);
    tok t[12];
    char b[256];
    int n = cm[0].n < 255 ? cm[0].n : 255;
    memcpy(b, cm[0].s, (size_t)n);
    b[n] = 0;
    int k = split_ws(b, t, 12);
    float nums[4] = { 0, 0, 0, 0 };
    int nn = 0;
    unsigned col = 0x40000000;
    int inset = 0;
    for (int i = 0; i < k; i++) {
        L l;
        if (tkw(t[i], "inset")) inset = 1;
        else if (nn < 4 && tlen(t[i], c, &l, 0)) nums[nn++] = l.px;
        else tcolor(t[i], &col);
    }
    if (nn < 2) return;
    st->hasshadow = 1;
    st->sinset = inset;
    st->sx = nums[0];
    st->sy = nums[1];
    st->sblur = nums[2];
    st->sspread = nums[3];
    st->scolor = col;
}
static void set_tshadow(style* st, const char* v, lctx* c) {
    st->hastshadow = 0;
    if (kw(v, "none")) return;
    tok cm[4];
    split_comma(v, cm, 4);
    tok t[8];
    char b[256];
    int n = cm[0].n < 255 ? cm[0].n : 255;
    memcpy(b, cm[0].s, (size_t)n);
    b[n] = 0;
    int k = split_ws(b, t, 8);
    float nums[3] = { 0, 0, 0 };
    int nn = 0;
    unsigned col = st->color;
    for (int i = 0; i < k; i++) {
        L l;
        if (nn < 3 && tlen(t[i], c, &l, 0)) nums[nn++] = l.px;
        else tcolor(t[i], &col);
    }
    if (nn < 2) return;
    st->hastshadow = 1;
    st->tsx = nums[0];
    st->tsy = nums[1];
    st->tsblur = nums[2];
    st->tscolor = col;
}

static int parse_gradient(const char* v, bgimg* g, lctx* c) {
    int rep = 0;
    if (istarts(v, "repeating-")) { v += 10; rep = 1; }
    (void)rep;
    int radial = istarts(v, "radial-gradient(");
    int conic = istarts(v, "conic-gradient(");
    if (!radial && !conic && !istarts(v, "linear-gradient(") && !istarts(v, "-webkit-linear-gradient(")) return 0;
    const char* p = strchr(v, '(') + 1;
    const char* e = v + strlen(v);
    while (e > p && e[-1] != ')') e--;
    if (e > p) e--;
    char* inner = nstrndup(p, (int)(e - p));
    tok parts[12];
    int np = split_comma(inner, parts, 12);
    g->kind = radial ? 2 : conic ? 3 : 1;
    g->angle = 180;
    int start = 0;
    if (np > 0) {
        tok first = parts[0];
        if (istarts(first.s, "to ")) {
            int top = 0, bottom = 0, left = 0, right = 0;
            for (int i = 3; i < first.n; i++) {
                if (ieqn(first.s + i, "top", 3)) top = 1;
                if (ieqn(first.s + i, "bottom", 6)) bottom = 1;
                if (ieqn(first.s + i, "left", 4)) left = 1;
                if (ieqn(first.s + i, "right", 5)) right = 1;
            }
            float a = 180;
            if (top) a = 0;
            if (right) a = 90;
            if (left) a = 270;
            if (top && right) a = 45;
            if (bottom && right) a = 135;
            if (bottom && left) a = 225;
            if (top && left) a = 315;
            if (bottom && !left && !right) a = 180;
            g->angle = a;
            start = 1;
        } else {
            const char* q;
            float a = parse_float(first.s, &q);
            if (q != first.s && (istarts(q, "deg") || istarts(q, "turn") || istarts(q, "rad") || istarts(q, "grad"))) {
                if (istarts(q, "turn")) a *= 360;
                else if (istarts(q, "rad")) a *= 57.2958f;
                else if (istarts(q, "grad")) a *= 0.9f;
                g->angle = a;
                start = 1;
            } else if (radial || conic) {
                unsigned tc;
                if (!css_parse_color(first.s, first.n, &tc)) {
                    char b2[64];
                    int k2 = first.n < 63 ? first.n : 63;
                    memcpy(b2, first.s, (size_t)k2);
                    b2[k2] = 0;
                    tok tt[2];
                    split_ws(b2, tt, 2);
                    if (!css_parse_color(tt[0].s, tt[0].n, &tc)) start = 1;
                }
            }
        }
    }
    int ns = 0;
    for (int i = start; i < np && ns < 8; i++) {
        tok t[4];
        char b[160];
        int n = parts[i].n < 159 ? parts[i].n : 159;
        memcpy(b, parts[i].s, (size_t)n);
        b[n] = 0;
        int k = split_ws(b, t, 4);
        unsigned col;
        if (k == 0 || !tcolor(t[0], &col)) {
            if (k >= 1) { L l; if (tlen(t[0], c, &l, 0) && ns > 0) continue; }
            continue;
        }
        g->stops[ns].col = col;
        g->stops[ns].pos = -1;
        if (k >= 2) { L l; if (tlen(t[1], c, &l, 0)) g->stops[ns].pos = l.pct != 0 ? l.pct / 100 : l.px / 400; }
        ns++;
        if (k >= 3 && ns < 8) { L l; if (tlen(t[2], c, &l, 0)) { g->stops[ns] = g->stops[ns - 1]; g->stops[ns].pos = l.pct != 0 ? l.pct / 100 : l.px / 400; ns++; } }
    }
    if (ns == 0) return 0;
    if (ns == 1) { g->stops[1] = g->stops[0]; ns = 2; }
    if (g->stops[0].pos < 0) g->stops[0].pos = 0;
    if (g->stops[ns - 1].pos < 0) g->stops[ns - 1].pos = 1;
    for (int i = 1; i < ns - 1; i++) if (g->stops[i].pos < 0) {
        int j = i;
        while (g->stops[j].pos < 0) j++;
        float a = g->stops[i - 1].pos, b = g->stops[j].pos;
        for (int k = i; k < j; k++) g->stops[k].pos = a + (b - a) * (float)(k - i + 1) / (float)(j - i + 1);
    }
    g->nstops = ns;
    return 1;
}

static bgimg* parse_bgimage(const char* v, lctx* c) {
    tok layers[6];
    int nl = split_comma(v, layers, 6);
    bgimg* head = 0;
    bgimg** tail = &head;
    for (int i = 0; i < nl; i++) {
        char* lv = nstrndup(layers[i].s, layers[i].n);
        const char* u = strstr(lv, "url(");
        bgimg* g = nzalloc(sizeof(bgimg));
        if (u && (u == lv || 1) && !strstr(lv, "gradient(")) {
            u += 4;
            while (is_ws((unsigned char)*u)) u++;
            char q = (*u == '"' || *u == '\'') ? *u++ : 0;
            const char* ue = u;
            while (*ue && (q ? *ue != q : *ue != ')')) ue++;
            g->kind = 4;
            g->url = nstrndup(u, (int)(ue - u));
        } else if (strstr(lv, "gradient(")) {
            const char* gs = lv;
            while (*gs && is_ws((unsigned char)*gs)) gs++;
            if (!parse_gradient(gs, g, c)) continue;
        } else if (istarts(lv, "image-set(") || istarts(lv, "-webkit-image-set(")) {
            const char* q = strchr(lv, '"');
            if (!q) q = strchr(lv, '\'');
            if (q) { char qc = *q++; const char* qe = strchr(q, qc); if (qe) { g->kind = 4; g->url = nstrndup(q, (int)(qe - q)); } }
            if (!g->url) continue;
        } else continue;
        *tail = g;
        tail = &g->next;
    }
    return head;
}

static void set4(L* arr, tok* t, int k, lctx* c, int allow_auto) {
    L v[4];
    int n = 0;
    for (int i = 0; i < k && n < 4; i++) { L l; if (tlen(t[i], c, &l, allow_auto)) v[n++] = l; else return; }
    if (n == 0) return;
    if (n == 1) v[1] = v[2] = v[3] = v[0];
    else if (n == 2) { v[2] = v[0]; v[3] = v[1]; }
    else if (n == 3) v[3] = v[1];
    for (int i = 0; i < 4; i++) arr[i] = v[i];
}
static int bstyle_kw(tok t) {
    static const char* n[] = { "none", "solid", "dashed", "dotted", "double", "groove", "ridge", "inset", "outset", "hidden" };
    for (int i = 0; i < 10; i++) if (tkw(t, n[i])) return i;
    return -1;
}
static float bwidth_tok(tok t, lctx* c, int* ok) {
    *ok = 1;
    if (tkw(t, "thin")) return 1;
    if (tkw(t, "medium")) return 3;
    if (tkw(t, "thick")) return 5;
    L l;
    if (tlen(t, c, &l, 0) && l.pct == 0) return l.px;
    *ok = 0;
    return 0;
}
static void border_side(style* st, int side, const char* v, lctx* c, int which) {
    tok t[6];
    int k = split_ws(v, t, 6);
    float w = 3;
    int s = BS_NONE;
    unsigned col = st->color;
    if (kw(v, "none") || kw(v, "0")) { for (int i = 0; i < 4; i++) if (which & (1 << i)) { st->bstyle[i] = BS_NONE; st->border[i] = 0; } return; }
    for (int i = 0; i < k; i++) {
        int ok;
        int bs = bstyle_kw(t[i]);
        if (bs >= 0) { s = bs; continue; }
        float bw = bwidth_tok(t[i], c, &ok);
        if (ok) { w = bw; continue; }
        tcolor(t[i], &col);
    }
    (void)side;
    for (int i = 0; i < 4; i++) if (which & (1 << i)) { st->border[i] = w; st->bstyle[i] = (unsigned char)s; st->bcolor[i] = col; }
}

static int align_kw(const char* v) {
    const char* p = v;
    if (istarts(p, "safe ")) p += 5;
    if (istarts(p, "unsafe ")) p += 7;
    if (kw(p, "flex-start") || kw(p, "start") || kw(p, "left") || kw(p, "self-start") || kw(p, "first baseline")) return AL_START;
    if (kw(p, "flex-end") || kw(p, "end") || kw(p, "right") || kw(p, "self-end") || kw(p, "last baseline")) return AL_END;
    if (kw(p, "center")) return AL_CENTER;
    if (kw(p, "stretch")) return AL_STRETCH;
    if (kw(p, "baseline")) return AL_BASELINE;
    if (kw(p, "space-between")) return AL_BETWEEN;
    if (kw(p, "space-around")) return AL_AROUND;
    if (kw(p, "space-evenly")) return AL_EVENLY;
    if (kw(p, "normal")) return AL_NORMAL;
    if (kw(p, "auto")) return AL_AUTO;
    return -1;
}

static char* top_comma(char* s) { int d = 0; for (; *s; s++) { if (*s == '(') d++; else if (*s == ')') d--; else if (*s == ',' && d == 0) return s; } return 0; }
static int parse_tracks(const char* v, gtrack** out, lctx* c, int* autofill) {
    gtrack tmp[64];
    int n = 0;
    if (autofill) *autofill = 0;
    tok t[64];
    int k = split_ws(v, t, 64);
    for (int i = 0; i < k && n < 64; i++) {
        if (t[i].n > 0 && t[i].s[0] == '[') { while (i < k && t[i].s[t[i].n - 1] != ']') i++; continue; }
        if (t[i].n > 7 && ieqn(t[i].s, "repeat(", 7)) {
            char* in = nstrndup(t[i].s + 7, t[i].n - 8);
            char* cm = top_comma(in);
            if (!cm) continue;
            *cm = 0;
            int cnt;
            gtrack* sub;
            int ns = parse_tracks(cm + 1, &sub, c, 0);
            if (ns <= 0) continue;
            if (istarts(in, "auto-fill") || istarts(in, "auto-fit")) {
                float tw = 0;
                for (int j = 0; j < ns; j++) tw += sub[j].k == GT_PX ? sub[j].v : sub[j].mink == GT_PX ? sub[j].minv : 100;
                cnt = -1;
                if (autofill) *autofill = tw > 0 ? (int)tw : 100;
                for (int j = 0; j < ns && n < 64; j++) { tmp[n] = sub[j]; tmp[n].mink |= 0x80; n++; }
                continue;
            }
            cnt = atoi(in);
            for (int r = 0; r < cnt && n < 64; r++) for (int j = 0; j < ns && n < 64; j++) tmp[n++] = sub[j];
            continue;
        }
        gtrack g = { GT_AUTO, 0, 0, GT_AUTO };
        if (t[i].n > 7 && ieqn(t[i].s, "minmax(", 7)) {
            char* in = nstrndup(t[i].s + 7, t[i].n - 8);
            char* cm = top_comma(in);
            if (!cm) continue;
            *cm = 0;
            gtrack a, b;
            gtrack* pa;
            gtrack* pb;
            if (parse_tracks(in, &pa, c, 0) != 1 || parse_tracks(cm + 1, &pb, c, 0) != 1) continue;
            a = pa[0];
            b = pb[0];
            g = b;
            g.mink = a.k;
            g.minv = a.v;
            tmp[n++] = g;
            continue;
        }
        if (t[i].n > 12 && ieqn(t[i].s, "fit-content(", 12)) { g.k = GT_AUTO; tmp[n++] = g; continue; }
        const char* q;
        float fv = parse_float(t[i].s, &q);
        if (q != t[i].s && (q - t[i].s) + 2 == t[i].n && ieqn(q, "fr", 2)) { g.k = GT_FR; g.v = fv; g.mink = GT_AUTO; tmp[n++] = g; continue; }
        if (tkw(t[i], "auto")) { tmp[n++] = g; continue; }
        if (tkw(t[i], "min-content")) { g.k = GT_MINC; tmp[n++] = g; continue; }
        if (tkw(t[i], "max-content")) { g.k = GT_MAXC; tmp[n++] = g; continue; }
        L l;
        if (tlen(t[i], c, &l, 0)) {
            if (l.pct != 0 && l.hi < 1e29f && l.px == 0) { g.k = GT_PX; g.v = l.hi; } else if (l.pct != 0) { g.k = GT_PCT; g.v = l.pct; } else { g.k = GT_PX; g.v = l.px; }
            g.mink = g.k;
            g.minv = g.v;
            tmp[n++] = g;
        }
    }
    *out = nalloc(sizeof(gtrack) * (size_t)(n ? n : 1));
    memcpy(*out, tmp, sizeof(gtrack) * (size_t)n);
    return n;
}
static void grid_line(const char* v, int* s, int* e) {
    char* sl = strchr(v, '/');
    char a[64], b[64];
    if (sl) {
        int al = (int)(sl - v) < 63 ? (int)(sl - v) : 63;
        memcpy(a, v, (size_t)al);
        a[al] = 0;
        snprintf(b, sizeof(b), "%s", sl + 1);
    } else { snprintf(a, sizeof(a), "%s", v); b[0] = 0; }
    char* pa = a;
    while (is_ws((unsigned char)*pa)) pa++;
    char* pb = b;
    while (is_ws((unsigned char)*pb)) pb++;
    if (istarts(pa, "span")) *s = -1000 - (atoi(pa + 4) > 0 ? atoi(pa + 4) : 1);
    else if (kw(pa, "auto")) *s = 0;
    else *s = atoi(pa);
    if (!*pb) *e = 0;
    else if (istarts(pb, "span")) *e = -1000 - (atoi(pb + 4) > 0 ? atoi(pb + 4) : 1);
    else if (kw(pb, "auto")) *e = 0;
    else *e = atoi(pb);
}

static void set_font_size(style* st, const char* v, lctx* c) {
    float pf = c->pfs;
    static const char* abs[] = { "xx-small", "x-small", "small", "medium", "large", "x-large", "xx-large", "xxx-large" };
    static const float absv[] = { 9, 10, 13, 16, 18, 24, 32, 48 };
    for (int i = 0; i < 8; i++) if (kw(v, abs[i])) { st->fsize = absv[i]; return; }
    if (kw(v, "smaller")) { st->fsize = pf / 1.2f; return; }
    if (kw(v, "larger")) { st->fsize = pf * 1.2f; return; }
    L l;
    c->forfont = 1;
    if (parse_len(v, c, &l, 0)) st->fsize = l.px + l.pct * pf / 100;
    c->forfont = 0;
    if (st->fsize < 0) st->fsize = 0;
}
static void set_line_height(style* st, const char* v, lctx* c) {
    if (kw(v, "normal")) { st->lhmul = -1; st->lh = -1; return; }
    const char* e;
    float f = parse_float(v, &e);
    if (e != v && !*e) { st->lhmul = f; st->lh = -1; return; }
    L l;
    c->fs = st->fsize;
    if (parse_len(v, c, &l, 0)) { st->lh = l.px + l.pct * st->fsize / 100; st->lhmul = 0; }
}
static void set_weight(style* st, const char* v, style* par) {
    if (kw(v, "bold") || kw(v, "bolder")) st->fweight = kw(v, "bolder") ? (par->fweight >= 600 ? 900 : 700) : 700;
    else if (kw(v, "normal")) st->fweight = 400;
    else if (kw(v, "lighter")) st->fweight = par->fweight > 500 ? 400 : 100;
    else { int w = atoi(v); if (w >= 1 && w <= 1000) st->fweight = (short)w; }
}
static void set_family(style* st, const char* v) { st->ffamily = (char*)v; }
static void set_font(style* st, const char* v, lctx* c, style* par) {
    if (kw(v, "caption") || kw(v, "menu") || kw(v, "message-box") || kw(v, "status-bar") || kw(v, "small-caption") || kw(v, "icon")) { st->fsize = 13; return; }
    tok t[16];
    int k = split_ws(v, t, 16);
    int i = 0;
    st->fstyle = 0;
    st->fweight = 400;
    st->lhmul = -1;
    st->lh = -1;
    for (; i < k; i++) {
        if (tkw(t[i], "italic") || tkw(t[i], "oblique")) { st->fstyle = 1; continue; }
        if (tkw(t[i], "normal") || tkw(t[i], "small-caps")) continue;
        if (tkw(t[i], "bold") || tkw(t[i], "bolder") || tkw(t[i], "lighter")) { char b[16]; memcpy(b, t[i].s, (size_t)t[i].n); b[t[i].n] = 0; set_weight(st, b, par); continue; }
        if (t[i].n == 3 && t[i].s[0] >= '1' && t[i].s[0] <= '9' && t[i].s[1] == '0' && t[i].s[2] == '0') { st->fweight = (short)atoi(t[i].s); continue; }
        if (t[i].n > 6 && istarts(t[i].s, "condensed")) continue;
        break;
    }
    if (i >= k) return;
    char b[128];
    int n = t[i].n < 127 ? t[i].n : 127;
    memcpy(b, t[i].s, (size_t)n);
    b[n] = 0;
    set_font_size(st, b, c);
    i++;
    if (i < k && t[i].n == 1 && t[i].s[0] == '/' && i + 1 < k) {
        n = t[i + 1].n < 127 ? t[i + 1].n : 127;
        memcpy(b, t[i + 1].s, (size_t)n);
        b[n] = 0;
        set_line_height(st, b, c);
        i += 2;
    }
    if (i < k) set_family(st, nstrdup(t[i].s));
}

static void apply_bg(style* st, const char* v, lctx* c) {
    st->bgcolor = 0;
    st->bg = 0;
    st->bgrepeat = 0;
    st->bgsizek = 0;
    st->bgpx = LV(0);
    st->bgpy = LV(0);
    if (kw(v, "none") || kw(v, "transparent")) return;
    tok layers[6];
    int nl = split_comma(v, layers, 6);
    if (nl <= 0) return;
    tok last = layers[nl - 1];
    char* lv = nstrndup(last.s, last.n);
    tok t[24];
    int k = split_ws(lv, t, 24);
    for (int i = 0; i < k; i++) {
        unsigned col;
        if (t[i].n > 4 && (memchr(t[i].s, '(', (size_t)t[i].n)) && !ieqn(t[i].s, "rgb", 3) && !ieqn(t[i].s, "hsl", 3) && !ieqn(t[i].s, "hwb", 3) && !ieqn(t[i].s, "color-mix", 9) && !ieqn(t[i].s, "oklch", 5) && !ieqn(t[i].s, "oklab", 5) && !ieqn(t[i].s, "lab(", 4) && !ieqn(t[i].s, "lch(", 4) && !ieqn(t[i].s, "light-dark", 10) && !ieqn(t[i].s, "color(", 6)) continue;
        if (tcolor(t[i], &col)) st->bgcolor = col;
        else if (tkw(t[i], "no-repeat")) st->bgrepeat = 1;
        else if (tkw(t[i], "repeat-x")) st->bgrepeat = 2;
        else if (tkw(t[i], "repeat-y")) st->bgrepeat = 3;
        else if (tkw(t[i], "cover")) st->bgsizek = 1;
        else if (tkw(t[i], "contain")) st->bgsizek = 2;
        else if (tkw(t[i], "center")) { st->bgpx = LV(0); st->bgpx.pct = 50; st->bgpy = st->bgpx; }
    }
    if (strstr(v, "url(") || strstr(v, "gradient(") || strstr(v, "image-set(")) st->bg = parse_bgimage(v, c);
}

static void apply_from(int p, style* st, style* src);

static float tok_num(tok t, const char** endp) { char b[64]; int n = t.n < 63 ? t.n : 63; memcpy(b, t.s, (size_t)n); b[n] = 0; const char* e; float v = parse_float(b, &e); if (endp) *endp = t.s + (e - b); return v; }
static int parse_angle(tok t, float* out) {
    const char* e;
    float v = tok_num(t, &e);
    if (e == t.s) return 0;
    int ul = (int)(t.s + t.n - e);
    if (ul == 3 && ieqn(e, "deg", 3)) v = v * 3.14159265f / 180;
    else if (ul == 4 && ieqn(e, "turn", 4)) v = v * 6.2831853f;
    else if (ul == 4 && ieqn(e, "grad", 4)) v = v * 3.14159265f / 200;
    else if (ul == 3 && ieqn(e, "rad", 3)) {}
    else if (ul == 0 && v == 0) {}
    else return 0;
    *out = v;
    return 1;
}
static int parse_time(tok t, float* out) {
    const char* e;
    float v = tok_num(t, &e);
    if (e == t.s) return 0;
    int ul = (int)(t.s + t.n - e);
    if (ul == 2 && ieqn(e, "ms", 2)) v /= 1000;
    else if (ul == 1 && (*e == 's' || *e == 'S')) {}
    else return 0;
    *out = v;
    return 1;
}
static int parse_tfunc(tok t, tfunc* f) {
    f->k = 0; f->steps = 1;
    if (tkw(t, "linear")) { f->p[0] = 0; f->p[1] = 0; f->p[2] = 1; f->p[3] = 1; return 1; }
    if (tkw(t, "ease")) { f->p[0] = 0.25f; f->p[1] = 0.1f; f->p[2] = 0.25f; f->p[3] = 1; return 1; }
    if (tkw(t, "ease-in")) { f->p[0] = 0.42f; f->p[1] = 0; f->p[2] = 1; f->p[3] = 1; return 1; }
    if (tkw(t, "ease-out")) { f->p[0] = 0; f->p[1] = 0; f->p[2] = 0.58f; f->p[3] = 1; return 1; }
    if (tkw(t, "ease-in-out")) { f->p[0] = 0.42f; f->p[1] = 0; f->p[2] = 0.58f; f->p[3] = 1; return 1; }
    if (tkw(t, "step-start")) { f->k = 2; f->steps = 1; return 1; }
    if (tkw(t, "step-end")) { f->k = 1; f->steps = 1; return 1; }
    if (t.n > 13 && ieqn(t.s, "cubic-bezier(", 13)) { const char* p = t.s + 13; for (int i = 0; i < 4; i++) { while (*p == ' ' || *p == ',') p++; f->p[i] = parse_float(p, &p); } return 1; }
    if (t.n > 6 && ieqn(t.s, "steps(", 6)) { const char* p = t.s + 6; f->steps = (int)parse_float(p, &p); if (f->steps < 1) f->steps = 1; f->k = 1; while (*p == ' ' || *p == ',') p++; if (istarts(p, "start") || istarts(p, "jump-start")) f->k = 2; return 1; }
    if (t.n > 7 && ieqn(t.s, "linear(", 7)) { f->p[0] = 0; f->p[1] = 0; f->p[2] = 1; f->p[3] = 1; return 1; }
    return 0;
}
static int parse_transform_list(const char* v, lctx* c, tfn* out, int max) {
    int n = 0;
    const char* q = v;
    while (*q && n < max) {
        while (is_ws((unsigned char)*q)) q++;
        const char* fs = q;
        while (*q && *q != '(') q++;
        if (!*q) break;
        int fl = (int)(q - fs);
        const char* as = ++q;
        int d = 1;
        while (*q && d) { if (*q == '(') d++; else if (*q == ')') d--; if (d) q++; }
        char* args = nstrndup(as, (int)(q - as));
        if (*q) q++;
        tok a[20];
        int na = split_ws(args, a, 20);
        tok aa[20];
        int ai = 0;
        for (int i = 0; i < na; i++) if (!(a[i].n == 1 && a[i].s[0] == ',')) aa[ai++] = a[i];
        tfn* f = &out[n];
        memset(f, 0, sizeof *f);
        L l;
        char nm[24];
        int k = fl < 23 ? fl : 23;
        for (int i = 0; i < k; i++) nm[i] = (char)lc((unsigned char)fs[i]);
        nm[k] = 0;
        if (!strcmp(nm, "translate") || !strcmp(nm, "translate3d")) { f->t = TF_TRANSLATE; f->l[0] = LV(0); f->l[1] = LV(0); if (ai >= 1 && tlen(aa[0], c, &l, 0)) f->l[0] = l; if (ai >= 2 && tlen(aa[1], c, &l, 0)) f->l[1] = l; n++; }
        else if (!strcmp(nm, "translatex")) { f->t = TF_TRANSLATE; f->l[0] = LV(0); f->l[1] = LV(0); if (ai >= 1 && tlen(aa[0], c, &l, 0)) f->l[0] = l; n++; }
        else if (!strcmp(nm, "translatey")) { f->t = TF_TRANSLATE; f->l[0] = LV(0); f->l[1] = LV(0); if (ai >= 1 && tlen(aa[0], c, &l, 0)) f->l[1] = l; n++; }
        else if (!strcmp(nm, "translatez")) {}
        else if (!strcmp(nm, "scale") || !strcmp(nm, "scale3d")) { f->t = TF_SCALE; f->v[0] = ai >= 1 ? tok_num(aa[0], 0) : 1; f->v[1] = ai >= 2 ? tok_num(aa[1], 0) : f->v[0]; if (ai >= 1 && aa[0].s[aa[0].n - 1] == '%') { f->v[0] /= 100; f->v[1] = ai >= 2 ? f->v[1] / 100 : f->v[0]; } n++; }
        else if (!strcmp(nm, "scalex")) { f->t = TF_SCALE; f->v[0] = ai >= 1 ? tok_num(aa[0], 0) : 1; f->v[1] = 1; n++; }
        else if (!strcmp(nm, "scaley")) { f->t = TF_SCALE; f->v[0] = 1; f->v[1] = ai >= 1 ? tok_num(aa[0], 0) : 1; n++; }
        else if (!strcmp(nm, "rotate") || !strcmp(nm, "rotatez")) { f->t = TF_ROTATE; if (ai >= 1 && parse_angle(aa[0], &f->v[0])) n++; }
        else if (!strcmp(nm, "rotate3d")) { f->t = TF_ROTATE; if (ai >= 4 && tok_num(aa[2], 0) != 0 && parse_angle(aa[3], &f->v[0])) { if (tok_num(aa[2], 0) < 0) f->v[0] = -f->v[0]; n++; } }
        else if (!strcmp(nm, "skew")) { f->t = TF_SKEW; if (ai >= 1) parse_angle(aa[0], &f->v[0]); if (ai >= 2) parse_angle(aa[1], &f->v[1]); n++; }
        else if (!strcmp(nm, "skewx")) { f->t = TF_SKEW; if (ai >= 1) parse_angle(aa[0], &f->v[0]); n++; }
        else if (!strcmp(nm, "skewy")) { f->t = TF_SKEW; if (ai >= 1) parse_angle(aa[0], &f->v[1]); n++; }
        else if (!strcmp(nm, "matrix") && ai >= 6) { f->t = TF_MATRIX; for (int i = 0; i < 6; i++) f->v[i] = tok_num(aa[i], 0); n++; }
        else if (!strcmp(nm, "matrix3d") && ai >= 16) { f->t = TF_MATRIX; f->v[0] = tok_num(aa[0], 0); f->v[1] = tok_num(aa[1], 0); f->v[2] = tok_num(aa[4], 0); f->v[3] = tok_num(aa[5], 0); f->v[4] = tok_num(aa[12], 0); f->v[5] = tok_num(aa[13], 0); n++; }
    }
    return n;
}
static void mmul(float* a, const float* b) {
    float r[6];
    r[0] = a[0] * b[0] + a[2] * b[1];
    r[1] = a[1] * b[0] + a[3] * b[1];
    r[2] = a[0] * b[2] + a[2] * b[3];
    r[3] = a[1] * b[2] + a[3] * b[3];
    r[4] = a[0] * b[4] + a[2] * b[5] + a[4];
    r[5] = a[1] * b[4] + a[3] * b[5] + a[5];
    memcpy(a, r, sizeof r);
}
void tf_matrix(style* s, float w, float h, float* m) {
    m[0] = 1; m[1] = 0; m[2] = 0; m[3] = 1; m[4] = 0; m[5] = 0;
    if (!s) return;
    float t[6];
    m[4] = lres(s->tx, w);
    m[5] = lres(s->ty, h);
    if (s->rotate != 0) { float cs = cosf(s->rotate), sn = sinf(s->rotate); t[0] = cs; t[1] = sn; t[2] = -sn; t[3] = cs; t[4] = 0; t[5] = 0; mmul(m, t); }
    if (s->scale != 1) { t[0] = s->scale; t[1] = 0; t[2] = 0; t[3] = s->scale; t[4] = 0; t[5] = 0; mmul(m, t); }
    for (int i = 0; i < s->ntf; i++) {
        tfn* f = &s->tf[i];
        t[0] = 1; t[1] = 0; t[2] = 0; t[3] = 1; t[4] = 0; t[5] = 0;
        if (f->t == TF_TRANSLATE) { t[4] = lres(f->l[0], w); t[5] = lres(f->l[1], h); }
        else if (f->t == TF_SCALE) { t[0] = f->v[0]; t[3] = f->v[1]; }
        else if (f->t == TF_ROTATE) { float cs = cosf(f->v[0]), sn = sinf(f->v[0]); t[0] = cs; t[1] = sn; t[2] = -sn; t[3] = cs; }
        else if (f->t == TF_SKEW) { t[2] = tanf(f->v[0]); t[1] = tanf(f->v[1]); }
        else memcpy(t, f->v, sizeof t);
        mmul(m, t);
    }
}
int tf_has_linear(style* s) {
    if (!s) return 0;
    if (s->rotate != 0 || s->scale != 1) return 1;
    for (int i = 0; i < s->ntf; i++) if (s->tf[i].t != TF_TRANSLATE) return 1;
    return 0;
}
static animspec* anim_slot(style* st, int i) {
    if (i >= 8) return 0;
    if (i >= st->nanim) {
        animspec* na = nalloc(sizeof(animspec) * 8);
        memset(na, 0, sizeof(animspec) * 8);
        if (st->nanim) memcpy(na, st->anims, sizeof(animspec) * st->nanim);
        for (int k = st->nanim; k < 8; k++) { na[k].iter = 1; na[k].tf.p[0] = 0.25f; na[k].tf.p[1] = 0.1f; na[k].tf.p[2] = 0.25f; na[k].tf.p[3] = 1; }
        st->anims = na;
        st->nanim = (unsigned char)(i + 1);
    }
    return &st->anims[i];
}
static transspec* trans_slot(style* st, int i) {
    if (i >= 12) return 0;
    if (i >= st->ntrans) {
        transspec* na = nalloc(sizeof(transspec) * 12);
        memset(na, 0, sizeof(transspec) * 12);
        if (st->ntrans) memcpy(na, st->trans, sizeof(transspec) * st->ntrans);
        for (int k = st->ntrans; k < 12; k++) { na[k].prop = -1; na[k].tf.p[0] = 0.25f; na[k].tf.p[1] = 0.1f; na[k].tf.p[2] = 0.25f; na[k].tf.p[3] = 1; }
        st->trans = na;
        st->ntrans = (unsigned char)(i + 1);
    }
    return &st->trans[i];
}
static void set_animation(style* st, int p, const char* v) {
    tok parts[8];
    int np = split_comma(v, parts, 8);
    if (p == P_ANIMATION || p == P_ANIMNAME) { st->nanim = 0; st->anims = 0; }
    for (int i = 0; i < np; i++) {
        char* one = nstrndup(parts[i].s, parts[i].n);
        if (p == P_ANIMATION) {
            animspec* a = anim_slot(st, i);
            if (!a) break;
            tok t[12];
            int nt = split_ws(one, t, 12);
            int ntime = 0;
            for (int k = 0; k < nt; k++) {
                float f;
                if (parse_time(t[k], &f)) { if (ntime++ == 0) a->dur = f; else a->delay = f; continue; }
                if (parse_tfunc(t[k], &a->tf)) continue;
                if (tkw(t[k], "infinite")) { a->iter = -1; continue; }
                if (t[k].n && ((t[k].s[0] >= '0' && t[k].s[0] <= '9') || t[k].s[0] == '.')) { a->iter = tok_num(t[k], 0); continue; }
                if (tkw(t[k], "normal")) { a->dir = 0; continue; }
                if (tkw(t[k], "reverse")) { a->dir = 1; continue; }
                if (tkw(t[k], "alternate")) { a->dir = 2; continue; }
                if (tkw(t[k], "alternate-reverse")) { a->dir = 3; continue; }
                if (tkw(t[k], "forwards")) { a->fill = 1; continue; }
                if (tkw(t[k], "backwards")) { a->fill = 2; continue; }
                if (tkw(t[k], "both")) { a->fill = 3; continue; }
                if (tkw(t[k], "none")) { a->name = 0; continue; }
                if (tkw(t[k], "running")) { a->paused = 0; continue; }
                if (tkw(t[k], "paused")) { a->paused = 1; continue; }
                a->name = nstrndup(t[k].s, t[k].n);
            }
            continue;
        }
        tok t1;
        t1.s = one;
        t1.n = (int)strlen(one);
        if (p == P_ANIMNAME) { animspec* a = anim_slot(st, i); if (a) a->name = tkw(t1, "none") ? 0 : one; continue; }
        for (int k = 0; k < (st->nanim > np ? st->nanim : np); k++) {
            if (k % np != i) continue;
            animspec* a = anim_slot(st, k);
            if (!a) break;
            float f;
            if (p == P_ANIMDUR && parse_time(t1, &f)) a->dur = f;
            else if (p == P_ANIMDELAY && parse_time(t1, &f)) a->delay = f;
            else if (p == P_ANIMITER) a->iter = tkw(t1, "infinite") ? -1 : tok_num(t1, 0);
            else if (p == P_ANIMDIR) a->dir = tkw(t1, "reverse") ? 1 : tkw(t1, "alternate") ? 2 : tkw(t1, "alternate-reverse") ? 3 : 0;
            else if (p == P_ANIMFILL) a->fill = tkw(t1, "forwards") ? 1 : tkw(t1, "backwards") ? 2 : tkw(t1, "both") ? 3 : 0;
            else if (p == P_ANIMTIMING) parse_tfunc(t1, &a->tf);
            else if (p == P_ANIMPLAY) a->paused = tkw(t1, "paused");
        }
    }
}
static void set_transition(style* st, int p, const char* v) {
    tok parts[12];
    int np = split_comma(v, parts, 12);
    if (p == P_TRANSITION || p == P_TRANSPROP) { st->ntrans = 0; st->trans = 0; }
    for (int i = 0; i < np; i++) {
        char* one = nstrndup(parts[i].s, parts[i].n);
        if (p == P_TRANSITION) {
            transspec* a = trans_slot(st, i);
            if (!a) break;
            tok t[8];
            int nt = split_ws(one, t, 8);
            int ntime = 0;
            a->prop = -1;
            for (int k = 0; k < nt; k++) {
                float f;
                if (parse_time(t[k], &f)) { if (ntime++ == 0) a->dur = f; else a->delay = f; continue; }
                if (parse_tfunc(t[k], &a->tf)) continue;
                if (tkw(t[k], "all")) { a->prop = -1; continue; }
                if (tkw(t[k], "none")) { a->prop = -2; continue; }
                char* nm = nstrndup(t[k].s, t[k].n);
                for (char* q = nm; *q; q++) *q = (char)lc((unsigned char)*q);
                int id = css_prop_id(nm);
                a->prop = (short)(id >= 0 ? id : -2);
            }
            continue;
        }
        tok t1;
        t1.s = one;
        t1.n = (int)strlen(one);
        if (p == P_TRANSPROP) {
            transspec* a = trans_slot(st, i);
            if (!a) break;
            for (char* q = one; *q; q++) *q = (char)lc((unsigned char)*q);
            a->prop = (short)(tkw(t1, "all") ? -1 : tkw(t1, "none") ? -2 : (css_prop_id(one) >= 0 ? css_prop_id(one) : -2));
            continue;
        }
        for (int k = 0; k < (st->ntrans > np ? st->ntrans : np); k++) {
            if (k % np != i) continue;
            transspec* a = trans_slot(st, k);
            if (!a) break;
            float f;
            if (p == P_TRANSDUR && parse_time(t1, &f)) a->dur = f;
            else if (p == P_TRANSDELAY && parse_time(t1, &f)) a->delay = f;
            else if (p == P_TRANSTIMING) parse_tfunc(t1, &a->tf);
        }
    }
}

static void apply(int p, const char* v, style* st, style* par, lctx* c) {
    tok t[16];
    int k;
    L l;
    unsigned col;
    if (kw(v, "inherit")) { apply_from(p, st, par); return; }
    if (kw(v, "initial") || kw(v, "unset") || kw(v, "revert") || kw(v, "revert-layer")) {
        static style init;
        static int iready;
        if (!iready) { memset(&init, 0, sizeof(init)); init.display = D_INLINE; init.color = 0xFF000000; init.fsize = 16; init.fweight = 400; init.lhmul = -1; init.lh = -1; init.opacity = 1; init.shrink = 1; init.zauto = 1; init.width = init.height = init.minw = init.minh = LA(); init.maxw = init.maxh = LN(); for (int i = 0; i < 4; i++) { init.margin[i] = init.padding[i] = LV(0); init.inset[i] = LA(); init.bcolor[i] = 0xFF000000; } init.basis = LA(); iready = 1; }
        int inh = p == P_COLOR || p == P_FONTSIZE || p == P_FONTWEIGHT || p == P_FONTSTYLE || p == P_FONTFAMILY || p == P_LINEHEIGHT || p == P_TEXTALIGN || p == P_VISIBILITY || p == P_WHITESPACE || p == P_CURSOR || p == P_LISTSTYLETYPE;
        if (inh && !kw(v, "initial")) apply_from(p, st, par); else apply_from(p, st, &init);
        return;
    }
    switch (p) {
    case P_DISPLAY: {
        static const char* dn[] = { "none", "inline", "block", "inline-block", "list-item", "flex", "inline-flex", "grid", "inline-grid", "table", "inline-table", "table-row", "table-cell", "table-row-group", "table-header-group", "table-footer-group", "table-caption", "table-column", "table-column-group", "contents" };
        for (int i = 0; i < 20; i++) if (kw(v, dn[i])) { st->display = (unsigned char)i; return; }
        if (kw(v, "flow-root") || kw(v, "block flow-root") || kw(v, "flow")) st->display = D_BLOCK;
        else if (kw(v, "-webkit-box")) st->display = D_BLOCK;
        else if (kw(v, "-ms-flexbox") || kw(v, "-webkit-flex") || kw(v, "block flex")) st->display = D_FLEX;
        else if (kw(v, "-webkit-inline-box") || kw(v, "inline-flexbox") || kw(v, "inline flex")) st->display = D_IFLEX;
        else if (kw(v, "inline flow-root") || kw(v, "inline-list-item") || kw(v, "ruby")) st->display = D_IBLOCK;
        else if (kw(v, "block grid")) st->display = D_GRID;
        else if (kw(v, "block list-item") || kw(v, "list-item block")) st->display = D_LISTITEM;
        return;
    }
    case P_POSITION:
        if (kw(v, "static")) st->position = P_STATIC; else if (kw(v, "relative")) st->position = P_RELATIVE; else if (kw(v, "absolute")) st->position = P_ABSOLUTE;
        else if (kw(v, "fixed")) st->position = P_FIXED; else if (kw(v, "sticky") || kw(v, "-webkit-sticky")) st->position = P_STICKY;
        return;
    case P_FLOAT: st->flt = kw(v, "left") || kw(v, "inline-start") ? FL_LEFT : kw(v, "right") || kw(v, "inline-end") ? FL_RIGHT : FL_NONE; return;
    case P_CLEAR: st->clear = kw(v, "left") ? 1 : kw(v, "right") ? 2 : kw(v, "both") ? 3 : 0; return;
    case P_BOXSIZING: st->boxsizing = kw(v, "border-box"); return;
    case P_VISIBILITY: st->visibility = kw(v, "hidden") || kw(v, "collapse"); return;
    case P_OVERFLOW: case P_OVERFLOWX: case P_OVERFLOWY: {
        k = split_ws(v, t, 2);
        int vals[2] = { 0, 0 };
        for (int i = 0; i < k; i++) vals[i] = tkw(t[i], "hidden") ? OV_HIDDEN : tkw(t[i], "scroll") ? OV_SCROLL : tkw(t[i], "auto") || tkw(t[i], "overlay") ? OV_AUTO : tkw(t[i], "clip") ? OV_CLIP : OV_VISIBLE;
        if (k == 1) vals[1] = vals[0];
        if (p == P_OVERFLOW) { st->ovx = (unsigned char)vals[0]; st->ovy = (unsigned char)vals[1]; }
        else if (p == P_OVERFLOWX) st->ovx = (unsigned char)vals[0];
        else st->ovy = (unsigned char)vals[0];
        return;
    }
    case P_WHITESPACE: st->ws = kw(v, "nowrap") ? WS_NOWRAP : kw(v, "pre") ? WS_PRE : kw(v, "pre-wrap") || kw(v, "break-spaces") ? WS_PREWRAP : kw(v, "pre-line") ? WS_PRELINE : WS_NORMAL; return;
    case P_TEXTALIGN: case P_TEXTALIGNLAST:
        if (p == P_TEXTALIGNLAST) return;
        st->talign = kw(v, "right") || kw(v, "end") || kw(v, "-webkit-right") ? TA_RIGHT : kw(v, "center") || kw(v, "-webkit-center") || kw(v, "-moz-center") ? TA_CENTER : kw(v, "justify") ? TA_JUSTIFY : TA_LEFT;
        return;
    case P_TEXTDECORATION: case P_TEXTDECORATIONLINE:
        st->tdec = (strstr(v, "underline") ? 1 : 0) | (strstr(v, "line-through") ? 2 : 0) | (strstr(v, "overline") ? 4 : 0);
        return;
    case P_TEXTTRANSFORM: st->ttrans = kw(v, "uppercase") ? 1 : kw(v, "lowercase") ? 2 : kw(v, "capitalize") ? 3 : 0; return;
    case P_FONTSTYLE: st->fstyle = kw(v, "italic") || istarts(v, "oblique"); return;
    case P_FONTFAMILY: set_family(st, v); return;
    case P_FONTWEIGHT: set_weight(st, v, par); return;
    case P_FONTSIZE: set_font_size(st, v, c); return;
    case P_LINEHEIGHT: set_line_height(st, v, c); return;
    case P_FONT: set_font(st, v, c, par); return;
    case P_LETTERSPACING: if (kw(v, "normal")) st->lspace = 0; else if (parse_len(v, c, &l, 0)) st->lspace = l.px; return;
    case P_WORDSPACING: if (kw(v, "normal")) st->wspace = 0; else if (parse_len(v, c, &l, 0)) st->wspace = l.px; return;
    case P_TEXTINDENT: if (parse_len(v, c, &l, 0)) st->tindent = l.px; return;
    case P_LISTSTYLE: case P_LISTSTYLETYPE: {
        k = split_ws(v, t, 4);
        for (int i = 0; i < k; i++) {
            static const char* ln[] = { "none", "disc", "circle", "square", "decimal", "lower-alpha", "upper-alpha", "lower-roman", "upper-roman" };
            for (int j = 0; j < 9; j++) if (tkw(t[i], ln[j])) st->lstype = (unsigned char)j;
            if (tkw(t[i], "lower-latin")) st->lstype = LS_LALPHA;
            if (tkw(t[i], "upper-latin")) st->lstype = LS_UALPHA;
            if (tkw(t[i], "decimal-leading-zero")) st->lstype = LS_DECIMAL;
            if (tkw(t[i], "inside")) st->lspos = 1;
            if (tkw(t[i], "outside")) st->lspos = 0;
            if (t[i].n > 4 && ieqn(t[i].s, "url(", 4) && k == 1) st->lstype = LS_DISC;
        }
        return;
    }
    case P_LISTSTYLEPOSITION: st->lspos = kw(v, "inside"); return;
    case P_VERTICALALIGN:
        st->hasvalignpx = 0;
        if (kw(v, "middle")) st->valign = VA_MIDDLE; else if (kw(v, "top")) st->valign = VA_TOP; else if (kw(v, "bottom")) st->valign = VA_BOTTOM;
        else if (kw(v, "sub")) st->valign = VA_SUB; else if (kw(v, "super")) st->valign = VA_SUPER; else if (kw(v, "text-top")) st->valign = VA_TEXTTOP;
        else if (kw(v, "text-bottom")) st->valign = VA_TEXTBOTTOM; else if (kw(v, "baseline")) st->valign = VA_BASELINE;
        else if (parse_len(v, c, &l, 0)) { st->valign = VA_BASELINE; st->hasvalignpx = 1; st->valignpx = l.px + l.pct * st->fsize * 1.15f / 100; }
        return;
    case P_CURSOR: st->cursor = kw(v, "pointer") ? 1 : kw(v, "text") ? 2 : 0; return;
    case P_BORDERCOLLAPSE: st->bcollapse = kw(v, "collapse"); return;
    case P_WORDBREAK: case P_OVERFLOWWRAP: case P_WORDWRAP: st->wbreak = kw(v, "break-all") || kw(v, "break-word") || kw(v, "anywhere"); return;
    case P_POINTEREVENTS: st->pevents = kw(v, "none"); return;
    case P_TEXTOVERFLOW: st->tover = kw(v, "ellipsis"); return;
    case P_CLIP: {
        const char* r = strstr(v, "rect(");
        if (!r) { st->clipall = 0; return; }
        r += 5;
        float q[4] = { 0, 0, 0, 0 };
        for (int i = 0; i < 4; i++) { while (*r == ' ' || *r == ',') r++; const char* e; q[i] = parse_float(r, &e); if (e == r) { if (istarts(r, "auto")) { q[i] = i == 1 || i == 2 ? 1e9f : 0; r += 4; continue; } break; } r = e; while (*r && *r != ' ' && *r != ',' && *r != ')') r++; }
        st->clipall = q[2] <= q[0] || q[1] <= q[3];
        return;
    }
    case P_CLIPPATH: {
        const char* r = strstr(v, "inset(");
        st->clipall = 0;
        if (r) { float f = parse_float(r + 6, 0); if (strchr(v, '%') && f >= 50) st->clipall = 1; }
        if (strstr(v, "circle(0") || strstr(v, "polygon(0 0, 0 0")) st->clipall = 1;
        return;
    }
    case P_BGCLIP: st->bgcliptext = strstr(v, "text") != 0; return;
    case P_TEXTFILL: if (css_parse_color(v, (int)strlen(v), &col)) { st->txtfill = col; st->hastxtfill = 1; } else if (kw(v, "currentcolor")) st->hastxtfill = 0; return;
    case P_WEBKITLINECLAMP: { int k = atoi(v); st->lineclamp = k > 0 && k < 255 ? (unsigned char)k : 0; return; }
    case P_OBJECTFIT: st->objfit = kw(v, "cover") ? 1 : kw(v, "contain") || kw(v, "scale-down") ? 2 : kw(v, "none") ? 3 : 0; return;
    case P_BACKGROUND: apply_bg(st, v, c); return;
    case P_BACKGROUNDCOLOR: if (css_parse_color(v, (int)strlen(v), &col)) st->bgcolor = col; return;
    case P_BACKGROUNDIMAGE: st->bg = kw(v, "none") ? 0 : parse_bgimage(v, c); return;
    case P_BACKGROUNDREPEAT: st->bgrepeat = kw(v, "no-repeat") ? 1 : kw(v, "repeat-x") ? 2 : kw(v, "repeat-y") ? 3 : 0; return;
    case P_BACKGROUNDSIZE:
        if (kw(v, "cover")) st->bgsizek = 1;
        else if (kw(v, "contain")) st->bgsizek = 2;
        else {
            k = split_ws(v, t, 3);
            st->bgsizek = 0;
            if (k >= 1 && tlen(t[0], c, &l, 1)) { st->bgsizek = 3; st->bgsx = l; st->bgsy = LA(); if (k >= 2 && tlen(t[1], c, &l, 1)) st->bgsy = l; }
        }
        return;
    case P_BACKGROUNDPOSITION: {
        tok lay[4];
        split_comma(v, lay, 4);
        char* lv = nstrndup(lay[0].s, lay[0].n);
        k = split_ws(lv, t, 4);
        L px = LV(0), py = LV(0);
        px.pct = 0;
        int nx = 0;
        for (int i = 0; i < k; i++) {
            L q = LV(0);
            if (tkw(t[i], "left")) { px = LV(0); continue; }
            if (tkw(t[i], "right")) { px = LV(0); px.pct = 100; continue; }
            if (tkw(t[i], "top")) { py = LV(0); continue; }
            if (tkw(t[i], "bottom")) { py = LV(0); py.pct = 100; continue; }
            if (tkw(t[i], "center")) { q.pct = 50; if (nx == 0 && k == 1) { px = q; py = q; } else if (nx == 0) px = q; else py = q; nx++; continue; }
            if (tlen(t[i], c, &q, 0)) { if (nx == 0) px = q; else py = q; nx++; if (k == 1) { py = LV(0); py.pct = 50; } }
        }
        st->bgpx = px;
        st->bgpy = py;
        return;
    }
    case P_BACKGROUNDATTACHMENT: st->bgattach = kw(v, "fixed"); return;
    case P_COLOR: if (css_parse_color(v, (int)strlen(v), &col)) { st->color = col; g_curcolor = col; } return;
    case P_BORDER: border_side(st, 0, v, c, 15); return;
    case P_BORDERTOP: case P_BORDERBLOCKSTART: border_side(st, 0, v, c, 1); return;
    case P_BORDERRIGHT: case P_BORDERINLINEEND: border_side(st, 1, v, c, 2); return;
    case P_BORDERBOTTOM: case P_BORDERBLOCKEND: border_side(st, 2, v, c, 4); return;
    case P_BORDERLEFT: case P_BORDERINLINESTART: border_side(st, 3, v, c, 8); return;
    case P_BORDERINLINE: border_side(st, 0, v, c, 10); return;
    case P_BORDERBLOCK: border_side(st, 0, v, c, 5); return;
    case P_BORDERWIDTH: {
        k = split_ws(v, t, 4);
        float w[4];
        int n = 0, ok;
        for (int i = 0; i < k; i++) { float x = bwidth_tok(t[i], c, &ok); if (ok) w[n++] = x; }
        if (!n) return;
        if (n == 1) w[1] = w[2] = w[3] = w[0]; else if (n == 2) { w[2] = w[0]; w[3] = w[1]; } else if (n == 3) w[3] = w[1];
        for (int i = 0; i < 4; i++) st->border[i] = w[i];
        return;
    }
    case P_BORDERSTYLE: {
        k = split_ws(v, t, 4);
        int s[4], n = 0;
        for (int i = 0; i < k; i++) { int b = bstyle_kw(t[i]); if (b >= 0) s[n++] = b; }
        if (!n) return;
        if (n == 1) s[1] = s[2] = s[3] = s[0]; else if (n == 2) { s[2] = s[0]; s[3] = s[1]; } else if (n == 3) s[3] = s[1];
        for (int i = 0; i < 4; i++) st->bstyle[i] = (unsigned char)s[i];
        return;
    }
    case P_BORDERCOLOR: {
        k = split_ws(v, t, 4);
        unsigned cc[4];
        int n = 0;
        for (int i = 0; i < k; i++) if (tcolor(t[i], &cc[n])) n++;
        if (!n) return;
        if (n == 1) cc[1] = cc[2] = cc[3] = cc[0]; else if (n == 2) { cc[2] = cc[0]; cc[3] = cc[1]; } else if (n == 3) cc[3] = cc[1];
        for (int i = 0; i < 4; i++) st->bcolor[i] = cc[i];
        return;
    }
    case P_BTW: case P_BRW: case P_BBW: case P_BLW: { int ok; tok x = { v, (int)strlen(v) }; float w = bwidth_tok(x, c, &ok); if (ok) st->border[p - P_BTW] = w; return; }
    case P_BTS: case P_BRS: case P_BBS: case P_BLS: { tok x = { v, (int)strlen(v) }; int b = bstyle_kw(x); if (b >= 0) st->bstyle[p - P_BTS] = (unsigned char)b; return; }
    case P_BTC: case P_BRC: case P_BBC: case P_BLC: if (css_parse_color(v, (int)strlen(v), &col)) st->bcolor[p - P_BTC] = col; return;
    case P_BORDERRADIUS: {
        char* sl = strchr(v, '/');
        char* vv = sl ? nstrndup(v, (int)(sl - v)) : (char*)v;
        k = split_ws(vv, t, 4);
        float r[4];
        int n = 0;
        for (int i = 0; i < k; i++) if (tlen(t[i], c, &l, 0)) r[n++] = l.pct != 0 ? -l.pct : l.px;
        if (!n) return;
        if (n == 1) r[1] = r[2] = r[3] = r[0]; else if (n == 2) { r[2] = r[0]; r[3] = r[1]; } else if (n == 3) r[3] = r[1];
        for (int i = 0; i < 4; i++) st->radius[i] = r[i];
        return;
    }
    case P_BTLR: case P_BTRR: case P_BBRR: case P_BBLR: { k = split_ws(v, t, 2); if (k && tlen(t[0], c, &l, 0)) st->radius[p - P_BTLR] = l.pct != 0 ? -l.pct : l.px; return; }
    case P_OUTLINE: {
        if (kw(v, "none") || kw(v, "0")) { st->ostyle = 0; st->owidth = 0; return; }
        k = split_ws(v, t, 4);
        st->owidth = 3;
        st->ostyle = BS_SOLID;
        st->ocolor = st->color;
        for (int i = 0; i < k; i++) { int ok; int b = bstyle_kw(t[i]); if (b >= 0) { st->ostyle = (unsigned char)b; continue; } float w = bwidth_tok(t[i], c, &ok); if (ok) { st->owidth = w; continue; } tcolor(t[i], &st->ocolor); }
        return;
    }
    case P_OUTLINEWIDTH: { int ok; tok x = { v, (int)strlen(v) }; float w = bwidth_tok(x, c, &ok); if (ok) st->owidth = w; return; }
    case P_OUTLINESTYLE: { tok x = { v, (int)strlen(v) }; int b = bstyle_kw(x); if (b >= 0) st->ostyle = (unsigned char)b; return; }
    case P_OUTLINECOLOR: css_parse_color(v, (int)strlen(v), &st->ocolor); return;
    case P_OUTLINEOFFSET: if (parse_len(v, c, &l, 0)) st->ooffset = l.px; return;
    case P_WIDTH: case P_INLINESIZE: if (parse_len(v, c, &l, 1)) st->width = l; return;
    case P_HEIGHT: case P_BLOCKSIZE: if (parse_len(v, c, &l, 1)) st->height = l; return;
    case P_MINWIDTH: case P_MININLINESIZE: if (parse_len(v, c, &l, 1)) st->minw = l; return;
    case P_MINHEIGHT: if (parse_len(v, c, &l, 1)) st->minh = l; return;
    case P_MAXWIDTH: case P_MAXINLINESIZE: if (parse_len(v, c, &l, 2)) st->maxw = l; return;
    case P_MAXHEIGHT: if (parse_len(v, c, &l, 2)) st->maxh = l; return;
    case P_MARGIN: k = split_ws(v, t, 4); set4(st->margin, t, k, c, 1); return;
    case P_MT: case P_MR: case P_MB: case P_ML: if (parse_len(v, c, &l, 1)) st->margin[p - P_MT] = l; return;
    case P_MBS: if (parse_len(v, c, &l, 1)) st->margin[0] = l; return;
    case P_MBE: if (parse_len(v, c, &l, 1)) st->margin[2] = l; return;
    case P_MIS: if (parse_len(v, c, &l, 1)) st->margin[3] = l; return;
    case P_MIE: if (parse_len(v, c, &l, 1)) st->margin[1] = l; return;
    case P_MARGININLINE: { k = split_ws(v, t, 2); L a, b; if (k >= 1 && tlen(t[0], c, &a, 1)) { b = a; if (k >= 2) tlen(t[1], c, &b, 1); st->margin[3] = a; st->margin[1] = b; } return; }
    case P_MARGINBLOCK: { k = split_ws(v, t, 2); L a, b; if (k >= 1 && tlen(t[0], c, &a, 1)) { b = a; if (k >= 2) tlen(t[1], c, &b, 1); st->margin[0] = a; st->margin[2] = b; } return; }
    case P_PADDING: k = split_ws(v, t, 4); set4(st->padding, t, k, c, 0); return;
    case P_PT: case P_PR: case P_PB: case P_PL: if (parse_len(v, c, &l, 0)) st->padding[p - P_PT] = l; return;
    case P_PBS: if (parse_len(v, c, &l, 0)) st->padding[0] = l; return;
    case P_PBE: if (parse_len(v, c, &l, 0)) st->padding[2] = l; return;
    case P_PIS: if (parse_len(v, c, &l, 0)) st->padding[3] = l; return;
    case P_PIE: if (parse_len(v, c, &l, 0)) st->padding[1] = l; return;
    case P_PADDINGINLINE: { k = split_ws(v, t, 2); L a, b; if (k >= 1 && tlen(t[0], c, &a, 0)) { b = a; if (k >= 2) tlen(t[1], c, &b, 0); st->padding[3] = a; st->padding[1] = b; } return; }
    case P_PADDINGBLOCK: { k = split_ws(v, t, 2); L a, b; if (k >= 1 && tlen(t[0], c, &a, 0)) { b = a; if (k >= 2) tlen(t[1], c, &b, 0); st->padding[0] = a; st->padding[2] = b; } return; }
    case P_TOP: case P_RIGHT: case P_BOTTOM: case P_LEFT: if (parse_len(v, c, &l, 1)) st->inset[p - P_TOP] = l; return;
    case P_INSETINLINESTART: if (parse_len(v, c, &l, 1)) st->inset[3] = l; return;
    case P_INSETINLINEEND: if (parse_len(v, c, &l, 1)) st->inset[1] = l; return;
    case P_INSET: k = split_ws(v, t, 4); set4(st->inset, t, k, c, 1); return;
    case P_INSETINLINE: { k = split_ws(v, t, 2); L a, b; if (k >= 1 && tlen(t[0], c, &a, 1)) { b = a; if (k >= 2) tlen(t[1], c, &b, 1); st->inset[3] = a; st->inset[1] = b; } return; }
    case P_INSETBLOCK: { k = split_ws(v, t, 2); L a, b; if (k >= 1 && tlen(t[0], c, &a, 1)) { b = a; if (k >= 2) tlen(t[1], c, &b, 1); st->inset[0] = a; st->inset[2] = b; } return; }
    case P_ZINDEX: if (kw(v, "auto")) st->zauto = 1; else { st->zauto = 0; st->zindex = atoi(v); } return;
    case P_OPACITY: { const char* e; float o = parse_float(v, &e); if (*e == '%') o /= 100; st->opacity = o < 0 ? 0 : o > 1 ? 1 : o; return; }
    case P_FLEX: {
        if (kw(v, "none")) { st->grow = 0; st->shrink = 0; st->basis = LA(); return; }
        if (kw(v, "auto")) { st->grow = 1; st->shrink = 1; st->basis = LA(); return; }
        k = split_ws(v, t, 3);
        int nn = 0;
        st->grow = 1; st->shrink = 1; st->basis = LV(0);
        for (int i = 0; i < k; i++) {
            const char* e;
            float f = parse_float(t[i].s, &e);
            if (e == t[i].s + t[i].n && e != t[i].s) { if (nn == 0) st->grow = f; else st->shrink = f; nn++; }
            else if (tlen(t[i], c, &l, 1)) st->basis = l;
            else if (tkw(t[i], "content")) st->basis = LA();
        }
        return;
    }
    case P_FLEXDIRECTION: st->flexdir = kw(v, "row-reverse") ? FD_ROWREV : kw(v, "column") ? FD_COL : kw(v, "column-reverse") ? FD_COLREV : FD_ROW; return;
    case P_FLEXWRAP: st->flexwrap = kw(v, "wrap") ? 1 : kw(v, "wrap-reverse") ? 2 : 0; return;
    case P_FLEXFLOW: {
        k = split_ws(v, t, 2);
        for (int i = 0; i < k; i++) {
            if (tkw(t[i], "row")) st->flexdir = FD_ROW; else if (tkw(t[i], "row-reverse")) st->flexdir = FD_ROWREV; else if (tkw(t[i], "column")) st->flexdir = FD_COL;
            else if (tkw(t[i], "column-reverse")) st->flexdir = FD_COLREV; else if (tkw(t[i], "wrap")) st->flexwrap = 1; else if (tkw(t[i], "nowrap")) st->flexwrap = 0; else if (tkw(t[i], "wrap-reverse")) st->flexwrap = 2;
        }
        return;
    }
    case P_FLEXGROW: st->grow = parse_float(v, 0); return;
    case P_FLEXSHRINK: st->shrink = parse_float(v, 0); return;
    case P_FLEXBASIS: if (kw(v, "content")) st->basis = LA(); else if (parse_len(v, c, &l, 1)) st->basis = l; return;
    case P_JUSTIFYCONTENT: { int a = align_kw(v); if (a >= 0) st->justify = (unsigned char)a; return; }
    case P_ALIGNITEMS: { int a = align_kw(v); if (a >= 0) st->alignitems = (unsigned char)a; return; }
    case P_ALIGNSELF: { int a = align_kw(v); if (a >= 0) st->alignself = (unsigned char)a; return; }
    case P_ALIGNCONTENT: { int a = align_kw(v); if (a >= 0) st->aligncontent = (unsigned char)a; return; }
    case P_JUSTIFYITEMS: { int a = align_kw(v); if (a >= 0) st->justifyitems = (unsigned char)a; return; }
    case P_JUSTIFYSELF: { int a = align_kw(v); if (a >= 0) st->justifyself = (unsigned char)a; return; }
    case P_PLACEITEMS: case P_PLACECONTENT: case P_PLACESELF: {
        k = split_ws(v, t, 2);
        char a[32], b[32];
        int an = t[0].n < 31 ? t[0].n : 31;
        memcpy(a, t[0].s, (size_t)an);
        a[an] = 0;
        if (k > 1) { int bn = t[1].n < 31 ? t[1].n : 31; memcpy(b, t[1].s, (size_t)bn); b[bn] = 0; } else strcpy(b, a);
        int x = align_kw(a), y = align_kw(b);
        if (x < 0 || y < 0) return;
        if (p == P_PLACEITEMS) { st->alignitems = (unsigned char)x; st->justifyitems = (unsigned char)y; }
        else if (p == P_PLACECONTENT) { st->aligncontent = (unsigned char)x; st->justify = (unsigned char)y; }
        else { st->alignself = (unsigned char)x; st->justifyself = (unsigned char)y; }
        return;
    }
    case P_ORDER: st->order = atoi(v); return;
    case P_GAP: case P_GRIDGAP: {
        k = split_ws(v, t, 2);
        if (k >= 1 && tlen(t[0], c, &l, 0)) { st->growgap = l; st->gcolgap = l; }
        if (k >= 2 && tlen(t[1], c, &l, 0)) st->gcolgap = l;
        if (k >= 1 && tkw(t[0], "normal")) { st->growgap = LV(0); st->gcolgap = LV(0); }
        return;
    }
    case P_ROWGAP: case P_GRIDROWGAP: if (kw(v, "normal")) st->growgap = LV(0); else if (parse_len(v, c, &l, 0)) st->growgap = l; return;
    case P_COLUMNGAP: case P_GRIDCOLUMNGAP: if (kw(v, "normal")) st->gcolgap = LV(0); else if (parse_len(v, c, &l, 0)) st->gcolgap = l; return;
    case P_GTC: if (kw(v, "none")) st->ngcols = 0; else st->ngcols = parse_tracks(v, &st->gcols, c, &st->gautocols); return;
    case P_GTR: if (kw(v, "none")) st->ngrows = 0; else st->ngrows = parse_tracks(v, &st->grows, c, 0); return;
    case P_GRIDTEMPLATE: case P_GRID: {
        const char* sl = strchr(v, '/');
        if (!sl) { if (!strchr(v, '"')) st->ngrows = parse_tracks(v, &st->grows, c, 0); return; }
        char* a = nstrndup(v, (int)(sl - v));
        if (!strchr(a, '"') && !istarts(a, "auto-flow")) st->ngrows = parse_tracks(a, &st->grows, c, 0);
        const char* cs = sl + 1;
        if (istarts(cs, " auto-flow") || istarts(cs, "auto-flow")) return;
        st->ngcols = parse_tracks(cs, &st->gcols, c, &st->gautocols);
        return;
    }
    case P_GRIDAUTOFLOW: st->gautoflow = strstr(v, "column") ? 1 : 0; if (strstr(v, "dense")) st->gautoflow |= 2; return;
    case P_GRIDAUTOROWS: { gtrack* g; if (parse_tracks(v, &g, c, 0) >= 1) st->gautorow = g[0]; return; }
    case P_GRIDAUTOCOLUMNS: return;
    case P_GRIDCOLUMN: grid_line(v, &st->gcs, &st->gce); return;
    case P_GRIDROW: grid_line(v, &st->grs, &st->gre); return;
    case P_GCS: { int a, b; grid_line(v, &a, &b); st->gcs = a; return; }
    case P_GCE: { int a, b; grid_line(v, &a, &b); st->gce = a; return; }
    case P_GRS: { int a, b; grid_line(v, &a, &b); st->grs = a; return; }
    case P_GRE: { int a, b; grid_line(v, &a, &b); st->gre = a; return; }
    case P_GRIDAREA: {
        int vals[4] = { 0, 0, 0, 0 };
        int n = 0;
        const char* q = v;
        int named = 0;
        while (*q && n < 4) {
            while (is_ws((unsigned char)*q) || *q == '/') q++;
            if (!*q) break;
            if (istarts(q, "span")) { q += 4; while (is_ws((unsigned char)*q)) q++; vals[n++] = -1000 - (atoi(q) > 0 ? atoi(q) : 1); }
            else if ((*q >= '0' && *q <= '9') || *q == '-') vals[n++] = atoi(q);
            else { named = 1; vals[n++] = 0; }
            while (*q && *q != '/') q++;
        }
        if (named && n == 1) { st->areas = nstrdup(v); return; }
        st->grs = vals[0];
        st->gcs = vals[1];
        st->gre = vals[2];
        st->gce = vals[3];
        return;
    }
    case P_GTA: st->gta = (ieq(v, "none") ? 0 : nstrdup(v)); return;
    case P_BOXSHADOW: set_shadow(st, v, c); return;
    case P_TEXTSHADOW: set_tshadow(st, v, c); return;
    case P_TRANSFORM: {
        st->ntf = 0; st->tf = 0;
        if (kw(v, "none")) return;
        tfn tmp[16];
        int n = parse_transform_list(v, c, tmp, 16);
        if (n > 0) { st->tf = nalloc(sizeof(tfn) * (size_t)n); memcpy(st->tf, tmp, sizeof(tfn) * (size_t)n); st->ntf = (unsigned char)n; }
        return;
    }
    case P_TRANSLATE: {
        st->tx = LV(0); st->ty = LV(0);
        if (kw(v, "none")) return;
        k = split_ws(v, t, 3);
        if (k >= 1 && tlen(t[0], c, &l, 0)) st->tx = l;
        if (k >= 2 && tlen(t[1], c, &l, 0)) st->ty = l;
        return;
    }
    case P_SCALE: st->scale = kw(v, "none") ? 1 : parse_float(v, 0); if (strchr(v, '%')) st->scale /= 100; return;
    case P_ROTATE: { st->rotate = 0; if (kw(v, "none")) return; k = split_ws(v, t, 4); if (k >= 1) parse_angle(t[k - 1], &st->rotate); return; }
    case P_TRANSFORMORIGIN: {
        k = split_ws(v, t, 3);
        for (int i = 0; i < k && i < 2; i++) {
            L o = LV(0);
            int vert = 0;
            if (tkw(t[i], "left")) { o.pct = 0; o.px = 0; }
            else if (tkw(t[i], "right")) { o.pct = 100; o.px = 0; }
            else if (tkw(t[i], "top")) { o.pct = 0; vert = 1; }
            else if (tkw(t[i], "bottom")) { o.pct = 100; vert = 1; }
            else if (tkw(t[i], "center")) { o.pct = 50; vert = i; }
            else if (!tlen(t[i], c, &o, 0)) continue;
            else vert = i;
            if (vert) st->toy = o; else st->tox = o;
        }
        if (k == 1 && (tkw(t[0], "top") || tkw(t[0], "bottom"))) { L ce = LV(0); ce.pct = 50; st->tox = ce; }
        if (k == 1 && (tkw(t[0], "left") || tkw(t[0], "right"))) { L ce = LV(0); ce.pct = 50; st->toy = ce; }
        return;
    }
    case P_ANIMATION: case P_ANIMNAME: case P_ANIMDUR: case P_ANIMDELAY: case P_ANIMITER: case P_ANIMDIR: case P_ANIMFILL: case P_ANIMTIMING: case P_ANIMPLAY: set_animation(st, p, v); return;
    case P_TRANSITION: case P_TRANSPROP: case P_TRANSDUR: case P_TRANSDELAY: case P_TRANSTIMING: set_transition(st, p, v); return;
    case P_CONTENT: {
        if (kw(v, "none") || kw(v, "normal")) { st->content = 0; return; }
        sbuf b = { 0, 0, 0 };
        const char* q = v;
        while (*q) {
            if (*q == '"' || *q == '\'') {
                char qc = *q++;
                while (*q && *q != qc) {
                    if (*q == '\\') {
                        q++;
                        int hv = 0, hn = 0;
                        while (hn < 6 && ((*q >= '0' && *q <= '9') || (*q >= 'a' && *q <= 'f') || (*q >= 'A' && *q <= 'F'))) { hv = hv * 16 + hexd((unsigned char)*q); q++; hn++; }
                        if (hn) { sb_putu8(&b, hv); if (*q == ' ') q++; continue; }
                        if (*q) sb_putc(&b, *q++);
                        continue;
                    }
                    sb_putc(&b, *q++);
                }
                if (*q) q++;
                continue;
            }
            if (istarts(q, "attr(")) {
                q += 5;
                char an[64];
                int an_ = 0;
                while (*q && *q != ')' && an_ < 63) an[an_++] = *q++;
                an[an_] = 0;
                if (*q) q++;
                sb_puts(&b, "\x01");
                sb_puts(&b, an);
                sb_puts(&b, "\x02");
                continue;
            }
            if (istarts(q, "open-quote")) { sb_putu8(&b, 0x201C); q += 10; continue; }
            if (istarts(q, "close-quote")) { sb_putu8(&b, 0x201D); q += 11; continue; }
            if (istarts(q, "counter(") || istarts(q, "counters(") || istarts(q, "url(")) { while (*q && *q != ')') q++; if (*q) q++; continue; }
            q++;
        }
        st->content = sb_str(&b);
        return;
    }
    case P_BORDERSPACING: { k = split_ws(v, t, 2); if (k >= 1 && tlen(t[0], c, &l, 0)) { st->bspacingx = st->bspacingy = l.px; if (k >= 2 && tlen(t[1], c, &l, 0)) st->bspacingy = l.px; } return; }
    case P_CARETCOLOR: css_parse_color(v, (int)strlen(v), &st->caretcol); return;
    case P_ACCENTCOLOR: css_parse_color(v, (int)strlen(v), &st->accent); return;
    case P_ASPECTRATIO: {
        const char* e;
        float a = parse_float(v, &e);
        while (*e == ' ') e++;
        if (*e == '/') { e++; float b = parse_float(e, 0); if (b > 0) a /= b; }
        if (a > 0) st->aspect = a;
        return;
    }
    case P_FILL: if (kw(v, "none")) { st->fill = 0; st->hasfill = 1; } else if (css_parse_color(v, (int)strlen(v), &col)) { st->fill = col; st->hasfill = 1; } return;
    case P_STROKE: if (kw(v, "none")) { st->stroke = 0; st->hasstroke = 1; } else if (css_parse_color(v, (int)strlen(v), &col)) { st->stroke = col; st->hasstroke = 1; } return;
    default: return;
    }
}

static void apply_from(int p, style* st, style* s) {
    switch (p) {
    case P_COLOR: st->color = s->color; return;
    case P_FONTSIZE: st->fsize = s->fsize; return;
    case P_FONTWEIGHT: st->fweight = s->fweight; return;
    case P_FONTSTYLE: st->fstyle = s->fstyle; return;
    case P_FONTFAMILY: st->ffamily = s->ffamily; return;
    case P_FONT: st->fsize = s->fsize; st->fweight = s->fweight; st->fstyle = s->fstyle; st->ffamily = s->ffamily; st->lh = s->lh; st->lhmul = s->lhmul; return;
    case P_LINEHEIGHT: st->lh = s->lh; st->lhmul = s->lhmul; return;
    case P_DISPLAY: st->display = s->display; return;
    case P_POSITION: st->position = s->position; return;
    case P_BACKGROUND: case P_BACKGROUNDCOLOR: st->bgcolor = s->bgcolor; if (p == P_BACKGROUND) st->bg = s->bg; return;
    case P_BACKGROUNDIMAGE: st->bg = s->bg; return;
    case P_TEXTALIGN: st->talign = s->talign; return;
    case P_VISIBILITY: st->visibility = s->visibility; return;
    case P_WHITESPACE: st->ws = s->ws; return;
    case P_WIDTH: st->width = s->width; return;
    case P_HEIGHT: st->height = s->height; return;
    case P_MINWIDTH: st->minw = s->minw; return;
    case P_MAXWIDTH: st->maxw = s->maxw; return;
    case P_MINHEIGHT: st->minh = s->minh; return;
    case P_MAXHEIGHT: st->maxh = s->maxh; return;
    case P_MARGIN: for (int i = 0; i < 4; i++) st->margin[i] = s->margin[i]; return;
    case P_MT: case P_MR: case P_MB: case P_ML: st->margin[p - P_MT] = s->margin[p - P_MT]; return;
    case P_PADDING: for (int i = 0; i < 4; i++) st->padding[i] = s->padding[i]; return;
    case P_PT: case P_PR: case P_PB: case P_PL: st->padding[p - P_PT] = s->padding[p - P_PT]; return;
    case P_BORDER: for (int i = 0; i < 4; i++) { st->border[i] = s->border[i]; st->bstyle[i] = s->bstyle[i]; st->bcolor[i] = s->bcolor[i]; } return;
    case P_BORDERCOLOR: for (int i = 0; i < 4; i++) st->bcolor[i] = s->bcolor[i]; return;
    case P_BORDERRADIUS: for (int i = 0; i < 4; i++) st->radius[i] = s->radius[i]; return;
    case P_TOP: case P_RIGHT: case P_BOTTOM: case P_LEFT: st->inset[p - P_TOP] = s->inset[p - P_TOP]; return;
    case P_OPACITY: st->opacity = s->opacity; return;
    case P_TEXTDECORATION: case P_TEXTDECORATIONLINE: st->tdec = s->tdec; return;
    case P_TEXTTRANSFORM: st->ttrans = s->ttrans; return;
    case P_CURSOR: st->cursor = s->cursor; return;
    case P_LISTSTYLE: case P_LISTSTYLETYPE: st->lstype = s->lstype; return;
    case P_LETTERSPACING: st->lspace = s->lspace; return;
    case P_BOXSIZING: st->boxsizing = s->boxsizing; return;
    case P_FLOAT: st->flt = s->flt; return;
    case P_OVERFLOW: st->ovx = s->ovx; st->ovy = s->ovy; return;
    case P_VERTICALALIGN: st->valign = s->valign; return;
    case P_BORDERCOLLAPSE: st->bcollapse = s->bcollapse; return;
    case P_FILL: st->fill = s->fill; st->hasfill = s->hasfill; return;
    case P_TRANSFORM: st->ntf = 0; st->tf = 0; return;
    case P_BOXSHADOW: st->hasshadow = s->hasshadow; st->sx = s->sx; st->sy = s->sy; st->sblur = s->sblur; st->sspread = s->sspread; st->scolor = s->scolor; st->sinset = s->sinset; return;
    }
}

static style g_initial;
static int g_init_ready;
static void init_initial(void) {
    style* s = &g_initial;
    memset(s, 0, sizeof(*s));
    s->display = D_INLINE;
    s->color = 0xFF000000;
    s->fsize = 16;
    s->fweight = 400;
    s->lhmul = -1;
    s->lh = -1;
    s->opacity = 1;
    s->shrink = 1;
    s->zauto = 1;
    s->width = s->height = s->minw = s->minh = LA();
    s->maxw = s->maxh = LN();
    for (int i = 0; i < 4; i++) { s->margin[i] = LV(0); s->padding[i] = LV(0); s->inset[i] = LA(); s->bcolor[i] = 0xFF000000; s->bstyle[i] = BS_NONE; s->border[i] = 3; }
    s->basis = LA();
    s->tx = s->ty = LV(0);
    s->scale = 1;
    s->tox = LV(0); s->tox.pct = 50;
    s->toy = LV(0); s->toy.pct = 50;
    s->justify = AL_NORMAL;
    s->alignitems = AL_NORMAL;
    s->alignself = AL_AUTO;
    s->aligncontent = AL_NORMAL;
    s->justifyitems = AL_NORMAL;
    s->justifyself = AL_AUTO;
    s->gcolgap = s->growgap = LV(0);
    s->gautorow.k = GT_AUTO;
    s->lstype = LS_DISC;
    s->caretcol = 0;
    s->accent = 0xFF0075FF;
    s->ocolor = 0xFF000000;
    s->bspacingx = s->bspacingy = 0;
    g_init_ready = 1;
}
static void inherit(style* st, style* par) {
    *st = g_initial;
    if (!par) return;
    st->color = par->color;
    st->fsize = par->fsize;
    st->fweight = par->fweight;
    st->fstyle = par->fstyle;
    st->ffamily = par->ffamily;
    st->face = par->face;
    st->lh = par->lh;
    st->lhmul = par->lhmul;
    st->lspace = par->lspace;
    st->wspace = par->wspace;
    st->txtfill = par->txtfill;
    st->hastxtfill = par->hastxtfill;
    st->talign = par->talign;
    st->tindent = par->tindent;
    st->ttrans = par->ttrans;
    st->ws = par->ws;
    st->visibility = par->visibility;
    st->cursor = par->cursor;
    st->lstype = par->lstype;
    st->lspos = par->lspos;
    st->bcollapse = par->bcollapse;
    st->bspacingx = par->bspacingx;
    st->bspacingy = par->bspacingy;
    st->wbreak = par->wbreak;
    st->caretcol = par->caretcol;
    st->accent = par->accent;
    st->hastshadow = par->hastshadow;
    st->tsx = par->tsx;
    st->tsy = par->tsy;
    st->tsblur = par->tsblur;
    st->tscolor = par->tscolor;
    st->vars = par->vars;
    st->pevents = par->pevents;
    st->tdec = par->tdec;
    st->fill = par->fill;
    st->hasfill = par->hasfill;
    st->stroke = par->stroke;
    st->hasstroke = par->hasstroke;
    st->strokew = par->strokew;
    st->hasstrokew = par->hasstrokew;
}

typedef struct { decl* d; int lvl, spec, order, idx; } cent;
static int cent_cmp(const void* a, const void* b) {
    const cent* x = a;
    const cent* y = b;
    if (x->lvl != y->lvl) return x->lvl - y->lvl;
    if (x->spec != y->spec) return x->spec - y->spec;
    if (x->order != y->order) return x->order - y->order;
    return x->idx - y->idx;
}

static int pres_decls(node* n, decl* out, int cap) {
    int k = 0;
    const char* v;
    #define ADD(pp, vv) do { if (k < cap) { out[k].prop = (short)(pp); out[k].val = (char*)(vv); out[k].imp = 0; out[k].name = 0; k++; } } while (0)
    if ((v = attr_get(n, "bgcolor"))) ADD(P_BACKGROUNDCOLOR, v);
    if (n->tag == T_FONT) {
        if ((v = attr_get(n, "color"))) ADD(P_COLOR, v);
        if ((v = attr_get(n, "face"))) ADD(P_FONTFAMILY, v);
        if ((v = attr_get(n, "size"))) {
            int sz = atoi(v);
            if (v[0] == '+' || v[0] == '-') sz = 3 + sz;
            static const char* fs[] = { "x-small", "x-small", "small", "medium", "large", "x-large", "xx-large", "xxx-large" };
            if (sz < 1) sz = 1;
            if (sz > 7) sz = 7;
            ADD(P_FONTSIZE, fs[sz]);
        }
    }
    if (n->tag == T_BODY) {
        if ((v = attr_get(n, "text"))) ADD(P_COLOR, v);
        if ((v = attr_get(n, "background"))) { char* b = nalloc(strlen(v) + 8); sprintf(b, "url(%s)", v); ADD(P_BACKGROUNDIMAGE, b); }
        if ((v = attr_get(n, "marginwidth")) || (v = attr_get(n, "leftmargin"))) { char* b = nalloc(32); snprintf(b, 32, "%dpx", atoi(v)); ADD(P_ML, b); ADD(P_MR, b); }
        if ((v = attr_get(n, "marginheight")) || (v = attr_get(n, "topmargin"))) { char* b = nalloc(32); snprintf(b, 32, "%dpx", atoi(v)); ADD(P_MT, b); ADD(P_MB, b); }
    }
    if (n->tag == T_TABLE || n->tag == T_TD || n->tag == T_TH || n->tag == T_IMG || n->tag == T_HR || n->tag == T_COL || n->tag == T_IFRAME || n->tag == T_CANVAS || n->tag == T_VIDEO || n->tag == T_SVG || n->tag == T_INPUT || n->tag == T_OBJECT || n->tag == T_EMBED || n->tag == T_TR) {
        if ((v = attr_get(n, "width")) && n->tag != T_TR) { char* b = nalloc(strlen(v) + 4); int l = (int)strlen(v); strcpy(b, v); if (l && v[l - 1] != '%' && v[l - 1] >= '0' && v[l - 1] <= '9') strcat(b, "px"); if (n->tag != T_INPUT || (attr_get(n, "type") && ieq(attr_get(n, "type"), "image"))) ADD(P_WIDTH, b); }
        if ((v = attr_get(n, "height"))) { char* b = nalloc(strlen(v) + 4); int l = (int)strlen(v); strcpy(b, v); if (l && v[l - 1] != '%' && v[l - 1] >= '0' && v[l - 1] <= '9') strcat(b, "px"); if (n->tag != T_INPUT) ADD(P_HEIGHT, b); }
    }
    if ((v = attr_get(n, "align"))) {
        if (n->tag == T_TABLE) { if (ieq(v, "center")) { ADD(P_ML, "auto"); ADD(P_MR, "auto"); } else if (ieq(v, "right")) ADD(P_FLOAT, "right"); else if (ieq(v, "left")) ADD(P_FLOAT, "left"); }
        else if (n->tag == T_IMG || n->tag == T_IFRAME || n->tag == T_OBJECT) { if (ieq(v, "left") || ieq(v, "right")) ADD(P_FLOAT, v); else if (ieq(v, "middle") || ieq(v, "absmiddle")) ADD(P_VERTICALALIGN, "middle"); else if (ieq(v, "top")) ADD(P_VERTICALALIGN, "top"); }
        else if (n->tag == T_HR) { if (ieq(v, "left")) { ADD(P_MR, "auto"); ADD(P_ML, "0"); } else if (ieq(v, "right")) { ADD(P_ML, "auto"); ADD(P_MR, "0"); } }
        else ADD(P_TEXTALIGN, v);
    }
    if ((v = attr_get(n, "valign"))) ADD(P_VERTICALALIGN, v);
    if ((v = attr_get(n, "nowrap")) && (n->tag == T_TD || n->tag == T_TH)) ADD(P_WHITESPACE, "nowrap");
    if (n->tag == T_TABLE) {
        if ((v = attr_get(n, "border"))) { int b = *v ? atoi(v) : 1; if (b > 0) { char* s = nalloc(48); snprintf(s, 48, "%dpx outset #808080", b); ADD(P_BORDER, s); } }
        if ((v = attr_get(n, "cellspacing"))) { char* s = nalloc(32); snprintf(s, 32, "%dpx", atoi(v)); ADD(P_BORDERSPACING, s); }
    }
    if (n->tag == T_TD || n->tag == T_TH) {
        node* t = n->parent;
        while (t && t->tag != T_TABLE) t = t->parent;
        if (t) {
            if ((v = attr_get(t, "cellpadding"))) { char* s = nalloc(32); snprintf(s, 32, "%dpx", atoi(v)); ADD(P_PADDING, s); }
            if ((v = attr_get(t, "border")) && (*v ? atoi(v) : 1) > 0) ADD(P_BORDER, "1px inset #808080");
        }
    }
    if ((n->tag == T_IMG || n->tag == T_OBJECT) && (v = attr_get(n, "hspace"))) { char* s = nalloc(32); snprintf(s, 32, "%dpx", atoi(v)); ADD(P_ML, s); ADD(P_MR, s); }
    if ((n->tag == T_IMG || n->tag == T_OBJECT) && (v = attr_get(n, "vspace"))) { char* s = nalloc(32); snprintf(s, 32, "%dpx", atoi(v)); ADD(P_MT, s); ADD(P_MB, s); }
    if (n->tag == T_HR) {
        if ((v = attr_get(n, "color"))) { ADD(P_BORDERCOLOR, v); ADD(P_BACKGROUNDCOLOR, v); }
        if ((v = attr_get(n, "size"))) { char* s = nalloc(32); snprintf(s, 32, "%dpx", atoi(v) > 2 ? atoi(v) - 2 : 0); ADD(P_HEIGHT, s); }
        if (attr_get(n, "noshade")) ADD(P_BORDERSTYLE, "solid");
    }
    if ((n->tag == T_TD || n->tag == T_TH || n->tag == T_TR || n->tag == T_TABLE) && (v = attr_get(n, "background"))) { char* b = nalloc(strlen(v) + 8); sprintf(b, "url(%s)", v); ADD(P_BACKGROUNDIMAGE, b); }
    if (n->tag == T_OL && (v = attr_get(n, "type"))) ADD(P_LISTSTYLETYPE, !strcmp(v, "a") ? "lower-alpha" : !strcmp(v, "A") ? "upper-alpha" : !strcmp(v, "i") ? "lower-roman" : !strcmp(v, "I") ? "upper-roman" : "decimal");
    if (n->tag == T_UL && (v = attr_get(n, "type"))) ADD(P_LISTSTYLETYPE, v);
    if (attr_get(n, "hidden") && !ieq(attr_get(n, "hidden"), "until-found")) ADD(P_DISPLAY, "none");
    if ((n->tag == T_INPUT || n->tag == T_TEXTAREA || n->tag == T_SELECT) && (v = attr_get(n, "size")) && n->tag == T_INPUT) { char* s = nalloc(32); snprintf(s, 32, "%dpx", (int)(atoi(v) * 7.4f + 6)); if (atoi(v) > 0) ADD(P_WIDTH, s); }
    if (n->tag == T_TEXTAREA) {
        int cols = attr_get(n, "cols") ? atoi(attr_get(n, "cols")) : 20, rows = attr_get(n, "rows") ? atoi(attr_get(n, "rows")) : 2;
        if (cols < 1) cols = 20;
        if (rows < 1) rows = 2;
        char* s = nalloc(32); snprintf(s, 32, "%dpx", (int)(cols * 7.3f + 8)); ADD(P_WIDTH, s);
        char* h = nalloc(32); snprintf(h, 32, "%dpx", rows * 16 + 6); ADD(P_HEIGHT, h);
    }
    #undef ADD
    return k;
}

static decl* inline_decls(node* n, int* cnt) {
    const char* s = attr_get(n, "style");
    if (!s || !*s) { *cnt = 0; return 0; }
    if (n->idecl && n->idecl_src == s) { *cnt = n->nattrs >= 0 ? ((int*)n->idecl)[-1] : 0; return n->idecl; }
    int sv = pool_suspend();
    decl* d;
    int k = css_parse_decls(s, s + strlen(s), &d);
    int* blk = nalloc(sizeof(int) * 2 + sizeof(decl) * (size_t)(k ? k : 1));
    blk[1] = k;
    decl* dd = (decl*)(blk + 2);
    memcpy(dd, d, sizeof(decl) * (size_t)k);
    n->idecl = dd;
    ((int*)n->idecl)[-1] = k;
    n->idecl_src = (char*)s;
    *cnt = k;
    pool_resume(sv);
    return dd;
}

static int is_font_prop(int p) { return p == P_FONT || p == P_FONTSIZE || p == P_FONTFAMILY || p == P_FONTWEIGHT || p == P_FONTSTYLE || p == P_LINEHEIGHT; }

static void resolve_area(style* st, style* par) {
    if (st->areas && par && par->gta) {
        char nm[64]; int ln = 0; const char* a = st->areas; while (*a == ' ') a++; while (a[ln] && a[ln] != ' ' && a[ln] != '/' && ln < 63) { nm[ln] = a[ln]; ln++; } nm[ln] = 0;
        int r = 0, rmin = 999, rmax = -1, cmin = 999, cmax = -1; const char* q = par->gta;
        while (*q) { while (*q && *q != '"' && *q != '\'') q++; if (!*q) break; char qc = *q++; int col = 0;
            while (*q && *q != qc) { while (*q == ' ' || *q == '\t') q++; if (*q == qc || !*q) break; const char* st0 = q; while (*q && *q != ' ' && *q != '\t' && *q != qc) q++; int tl = (int)(q - st0);
                if (tl == ln && !strncmp(st0, nm, (size_t)ln)) { if (r < rmin) rmin = r; if (r > rmax) rmax = r; if (col < cmin) cmin = col; if (col > cmax) cmax = col; } col++; }
            if (*q) q++; r++; }
        if (rmax >= 0) { st->grs = rmin + 1; st->gre = rmax + 2; st->gcs = cmin + 1; st->gce = cmax + 2; }
    }
}
static style* compute(node* n, style* par, int pseudo) {
    static ment M[1024];
    static cent C[4096];
    int nm = css_gather(n, M, 1024, pseudo);
    if (pseudo && nm == 0) return 0;
    int nc = 0;
    decl pres[48];
    int np = pseudo ? 0 : pres_decls(n, pres, 48);
    for (int i = 0; i < np && nc < 4096; i++) { C[nc].d = &pres[i]; C[nc].lvl = 1; C[nc].spec = 0; C[nc].order = 0; C[nc].idx = i; nc++; }
    for (int i = 0; i < nm; i++) {
        rule* r = M[i].r;
        for (int j = 0; j < r->nd && nc < 4096; j++) {
            decl* d = &r->d[j];
            int lvl = r->origin == 0 ? (d->imp ? 6 : 0) : (d->imp ? 4 : 2);
            C[nc].d = d; C[nc].lvl = lvl; C[nc].spec = M[i].spec; C[nc].order = r->order; C[nc].idx = j; nc++;
        }
    }
    if (!pseudo) {
        int ni;
        decl* id = inline_decls(n, &ni);
        for (int j = 0; j < ni && nc < 4096; j++) { C[nc].d = &id[j]; C[nc].lvl = id[j].imp ? 5 : 3; C[nc].spec = 0; C[nc].order = 0; C[nc].idx = j; nc++; }
    }
    msort(C, nc, sizeof(cent), cent_cmp);
    style* st = nalloc(sizeof(style));
    inherit(st, par);
    if (n->tag == T_HTML && !pseudo) g_rootfs = 16;
    lctx c = { st, par ? par : &g_initial, par ? par->fsize : 16, par ? par->fsize : 16, 0 };
    g_curcolor = st->color;
    cvar* own = 0;
    for (int i = 0; i < nc; i++) if (C[i].d->prop == P_CUSTOM) {
        cvar* v = nalloc(sizeof(cvar));
        v->name = C[i].d->name;
        v->val = C[i].d->val;
        v->next = own;
        own = v;
    }
    if (own) {
        cvar* tail = own;
        while (tail->next) tail = tail->next;
        tail->next = st->vars;
        st->vars = own;
    }
    for (int i = 0; i < nc; i++) {
        decl* d = C[i].d;
        if (d->prop == P_CUSTOM || !is_font_prop(d->prop)) continue;
        char* v = subst_vars(d->val, st, 0);
        if (!v) continue;
        apply(d->prop, v, st, c.par, &c);
    }
    if (n->tag == T_HTML && !pseudo) g_rootfs = st->fsize;
    c.fs = st->fsize;
    st->face = face_for(st->ffamily, st->fweight, st->fstyle);
    for (int i = 0; i < nc; i++) {
        decl* d = C[i].d;
        if (d->prop == P_CUSTOM || is_font_prop(d->prop)) continue;
        char* v = subst_vars(d->val, st, 0);
        if (!v) continue;
        if (d->prop == P_COLOR) g_curcolor = st->color;
        apply(d->prop, v, st, c.par, &c);
        g_curcolor = st->color;
    }
    for (int i = 0; i < 4; i++) {
        if (st->bstyle[i] == BS_NONE || st->bstyle[i] == BS_HIDDEN) st->border[i] = 0;
    }
    if (st->bgcliptext) {
        unsigned tc = 0;
        if (st->bg && st->bg->nstops > 0) tc = st->bg->stops[0].col;
        else if (st->bgcolor >> 24) tc = st->bgcolor;
        if (tc) { st->color = tc | 0xFF000000u; st->bg = 0; st->bgcolor = 0; st->hastxtfill = 0; }
    }
    if (st->hastxtfill) st->color = st->txtfill;
    st->hastransform = st->ntf > 0 || st->rotate != 0 || st->scale != 1 || st->tx.px != 0 || st->tx.pct != 0 || st->ty.px != 0 || st->ty.pct != 0;
    st->hasmatrix = (unsigned char)tf_has_linear(st);
    resolve_area(st, c.par);
    if (st->position == P_ABSOLUTE || st->position == P_FIXED || st->flt != FL_NONE || (n->tag == T_HTML && !pseudo)) {
        if (st->position == P_ABSOLUTE || st->position == P_FIXED) st->flt = FL_NONE;
        if (st->display == D_INLINE || st->display == D_IBLOCK || st->display == D_TROW || st->display == D_TCELL || st->display == D_TROWGROUP || st->display == D_THEADG || st->display == D_TFOOTG || st->display == D_TCAPTION) st->display = D_BLOCK;
        else if (st->display == D_IFLEX) st->display = D_FLEX;
        else if (st->display == D_IGRID) st->display = D_GRID;
        else if (st->display == D_ITABLE) st->display = D_TABLE;
    }
    if (par && (par->display == D_FLEX || par->display == D_IFLEX || par->display == D_GRID || par->display == D_IGRID) && !pseudo) {
        if (st->display == D_INLINE || st->display == D_IBLOCK) st->display = D_BLOCK;
        else if (st->display == D_IFLEX) st->display = D_FLEX;
        else if (st->display == D_IGRID) st->display = D_GRID;
        else if (st->display == D_ITABLE) st->display = D_TABLE;
    }
    if (st->lhmul < 0 && st->lh < 0) { fontm m; font_metrics(st->face, st->fsize, &m); st->lh = -1; }
    if (n->tag == T_TD || n->tag == T_TH) {
        const char* cs = attr_get(n, "colspan");
        const char* rs = attr_get(n, "rowspan");
        st->colspan = cs ? atoi(cs) : 1;
        st->rowspan = rs ? atoi(rs) : 1;
        if (st->colspan < 1) st->colspan = 1;
        if (st->colspan > 100) st->colspan = 100;
        if (st->rowspan < 1) st->rowspan = 1;
    }
    if (pseudo && !st->content) return 0;
    return st;
}

style* css_style_with(node* n, style* base, style* par, decl* d, int nd) {
    style* st = nalloc(sizeof(style));
    *st = *base;
    st->tfcap = 0;
    lctx c = { st, par ? par : &g_initial, par ? par->fsize : 16, st->fsize, 0 };
    for (int i = 0; i < nd; i++) {
        if (d[i].prop == P_CUSTOM || d[i].prop == P_ANIMATION || d[i].prop == P_ANIMNAME || d[i].prop == P_ANIMTIMING) continue;
        char* v = subst_vars(d[i].val, st, 0);
        if (!v) continue;
        apply(d[i].prop, v, st, c.par, &c);
    }
    st->hastransform = st->ntf > 0 || st->rotate != 0 || st->scale != 1 || st->tx.px != 0 || st->tx.pct != 0 || st->ty.px != 0 || st->ty.pct != 0;
    st->hasmatrix = (unsigned char)tf_has_linear(st);
    resolve_area(st, c.par);
    (void)n;
    return st;
}

static node* find_slot(node* r, const char* nm, int depth) {
    if (depth > 64) return 0;
    for (node* k = r->first; k; k = k->next) {
        if (k->type != NT_ELEM) continue;
        if (k->name && !strcmp(k->name, "slot")) { const char* sn = attr_get(k, "name"); if ((nm && *nm) ? (sn && !strcmp(sn, nm)) : (!sn || !*sn)) return k; }
        node* f = find_slot(k, nm, depth + 1);
        if (f) return f;
    }
    return 0;
}
static style* slot_parent(node* host, node* c, style* par) {
    const char* sn = c->type == NT_ELEM ? attr_get(c, "slot") : 0;
    node* s = find_slot(host->shadow, sn, 0);
    return s && s->st ? s->st : par;
}
static void style_tree(node* n, style* par) {
    style* par0 = par;
    for (node* c = n->first; c; c = c->next) {
        if (n->shadow) par = slot_parent(n, c, par0);
        if (c->type != NT_ELEM) { c->st = par; continue; }
        nwe_check_stop();
        c->st = compute(c, par, 0);
        c->st_before = c->st_after = 0;
        if (c->st->display != D_NONE) {
            c->st_before = compute(c, c->st, 1);
            c->st_after = compute(c, c->st, 2);
            if (c->tag == T_SVG || c->tag == T_SCRIPT || c->tag == T_STYLE || c->tag == T_TEMPLATE) { c->st_before = c->st_after = 0; }
            css_anc_push(c);
            if (c->shadow) { c->shadow->st = c->st; style_tree(c->shadow, c->st); }
            style_tree(c, c->st);
            css_anc_pop(c);
        } else {
            for (node* d = c->first; d; d = d->next) { d->st = 0; if (d->type == NT_ELEM) d->st = 0; }
        }
    }
}

void style_all(node* root, float vw, float vh) {
    (void)vw;
    (void)vh;
    if (!g_init_ready) init_initial();
    int sv = pool_suspend();
    css_rebuild_if_needed();
    pool_resume(sv);
    extern void anim_before_style(node* root);
    extern void anim_after_style(node* root);
    anim_before_style(root);
    pool_reset(1);
    g_pool = 1;
    style* base = nalloc(sizeof(style));
    inherit(base, 0);
    base->display = D_BLOCK;
    g_css_mgen++;
    css_anc_reset();
    { extern int g_anc_on; g_anc_on = 1; style_tree(root, base); g_anc_on = 0; }
    anim_after_style(root);
    g_pool = sv;
}

style* style_of(node* n) { return n ? n->st : 0; }
style* anon_style(style* par, int display) {
    if (!g_init_ready) init_initial();
    style* s = nalloc(sizeof(style));
    inherit(s, par);
    s->display = (unsigned char)display;
    return s;
}

const char* css_prop_get(node* n, const char* prop, char* out, int cap) {
    out[0] = 0;
    int nd;
    decl* d = inline_decls(n, &nd);
    int pid = css_prop_id(prop);
    for (int i = nd - 1; i >= 0; i--) {
        if ((pid >= 0 && d[i].prop == pid) || (d[i].name && !strcmp(d[i].name, prop))) { snprintf(out, (size_t)cap, "%s", d[i].val); return out; }
    }
    return out;
}

void css_decl_apply_inline(node* n, const char* prop, const char* val) {
    int nd;
    decl* d = inline_decls(n, &nd);
    sbuf b = { 0, 0, 0 };
    int pid = css_prop_id(prop);
    int done = 0;
    for (int i = 0; i < nd; i++) {
        int same = (pid >= 0 && d[i].prop == pid && !d[i].name) || (d[i].name && !strcmp(d[i].name, prop));
        if (same) {
            if (done || !val || !*val) continue;
            done = 1;
            sb_puts(&b, prop); sb_puts(&b, ": "); sb_puts(&b, val); sb_puts(&b, "; ");
            continue;
        }
        sb_puts(&b, d[i].name ? d[i].name : css_prop_names[d[i].prop]);
        sb_puts(&b, ": ");
        sb_puts(&b, d[i].val);
        if (d[i].imp) sb_puts(&b, " !important");
        sb_puts(&b, "; ");
    }
    if (!done && val && *val) { sb_puts(&b, prop); sb_puts(&b, ": "); sb_puts(&b, val); sb_puts(&b, ";"); }
    while (b.n > 0 && b.s[b.n - 1] == ' ') b.s[--b.n] = 0;
    attr_set(n, "style", b.s ? b.s : "");
}

static void fmt_color(unsigned c, char* out, int cap) {
    unsigned a = c >> 24;
    if (a == 255) snprintf(out, (size_t)cap, "rgb(%u, %u, %u)", (c >> 16) & 255, (c >> 8) & 255, c & 255);
    else { int ai = (int)(a * 1000 / 255); snprintf(out, (size_t)cap, "rgba(%u, %u, %u, %d.%03d)", (c >> 16) & 255, (c >> 8) & 255, c & 255, ai / 1000, ai % 1000); }
}
static void fmt_px(float v, char* out, int cap) {
    if (v == (int)v) snprintf(out, (size_t)cap, "%dpx", (int)v);
    else snprintf(out, (size_t)cap, "%.2fpx", (double)v);
}
void css_computed_get(node* n, const char* prop, char* out, int cap) {
    out[0] = 0;
    style* s = n->st;
    if (!s) return;
    int p = css_prop_id(prop);
    if (prop[0] == '-' && prop[1] == '-') {
        for (cvar* v = s->vars; v; v = v->next) if (!strcmp(v->name, prop)) { snprintf(out, (size_t)cap, "%s", v->val); return; }
        return;
    }
    static const char* dn[] = { "none", "inline", "block", "inline-block", "list-item", "flex", "inline-flex", "grid", "inline-grid", "table", "inline-table", "table-row", "table-cell", "table-row-group", "table-header-group", "table-footer-group", "table-caption", "table-column", "table-column-group", "contents" };
    static const char* pn[] = { "static", "relative", "absolute", "fixed", "sticky" };
    float x, y, w, h;
    switch (p) {
    case P_DISPLAY: snprintf(out, (size_t)cap, "%s", dn[s->display]); return;
    case P_POSITION: snprintf(out, (size_t)cap, "%s", pn[s->position]); return;
    case P_COLOR: fmt_color(s->color, out, cap); return;
    case P_BACKGROUNDCOLOR: fmt_color(s->bgcolor, out, cap); return;
    case P_FONTSIZE: fmt_px(s->fsize, out, cap); return;
    case P_FONTWEIGHT: snprintf(out, (size_t)cap, "%d", s->fweight); return;
    case P_FONTSTYLE: snprintf(out, (size_t)cap, "%s", s->fstyle ? "italic" : "normal"); return;
    case P_FONTFAMILY: snprintf(out, (size_t)cap, "%s", s->ffamily ? s->ffamily : "sans-serif"); return;
    case P_VISIBILITY: snprintf(out, (size_t)cap, "%s", s->visibility ? "hidden" : "visible"); return;
    case P_OPACITY: snprintf(out, (size_t)cap, "%g", (double)s->opacity); return;
    case P_FLOAT: snprintf(out, (size_t)cap, "%s", s->flt == FL_LEFT ? "left" : s->flt == FL_RIGHT ? "right" : "none"); return;
    case P_ZINDEX: if (s->zauto) snprintf(out, (size_t)cap, "auto"); else snprintf(out, (size_t)cap, "%d", s->zindex); return;
    case P_OVERFLOW: case P_OVERFLOWY: { static const char* o[] = { "visible", "hidden", "scroll", "auto", "clip" }; snprintf(out, (size_t)cap, "%s", o[s->ovy]); return; }
    case P_TEXTALIGN: { static const char* t[] = { "left", "right", "center", "justify", "start", "end" }; snprintf(out, (size_t)cap, "%s", t[s->talign]); return; }
    case P_LINEHEIGHT: if (s->lh > 0) fmt_px(s->lh, out, cap); else if (s->lhmul > 0) fmt_px(s->lhmul * s->fsize, out, cap); else snprintf(out, (size_t)cap, "normal"); return;
    case P_WIDTH: case P_HEIGHT:
        box_rect(n, &x, &y, &w, &h);
        fmt_px(p == P_WIDTH ? w : h, out, cap);
        return;
    case P_MT: case P_MR: case P_MB: case P_ML: { box* b = box_of(n); fmt_px(b ? b->m[p - P_MT] : 0, out, cap); return; }
    case P_PT: case P_PR: case P_PB: case P_PL: { box* b = box_of(n); fmt_px(b ? b->p[p - P_PT] : 0, out, cap); return; }
    case P_BTW: case P_BRW: case P_BBW: case P_BLW: fmt_px(s->border[p - P_BTW], out, cap); return;
    case P_TOP: case P_LEFT: case P_RIGHT: case P_BOTTOM: { L l = s->inset[p - P_TOP]; if (l.k == LK_AUTO) snprintf(out, (size_t)cap, "auto"); else fmt_px(l.px, out, cap); return; }
    case P_TRANSFORM: { if (!s->hastransform) { snprintf(out, (size_t)cap, "none"); return; } float m[6]; tf_matrix(s, n->box ? n->box->w : 0, n->box ? n->box->h : 0, m); snprintf(out, (size_t)cap, "matrix(%g, %g, %g, %g, %g, %g)", m[0], m[1], m[2], m[3], m[4], m[5]); return; }
    case P_FLEXDIRECTION: { static const char* f[] = { "row", "row-reverse", "column", "column-reverse" }; snprintf(out, (size_t)cap, "%s", f[s->flexdir]); return; }
    case P_BOXSIZING: snprintf(out, (size_t)cap, "%s", s->boxsizing ? "border-box" : "content-box"); return;
    case P_CURSOR: snprintf(out, (size_t)cap, "%s", s->cursor == 1 ? "pointer" : s->cursor == 2 ? "text" : "auto"); return;
    case P_WHITESPACE: { static const char* t[] = { "normal", "nowrap", "pre", "pre-wrap", "pre-line" }; snprintf(out, (size_t)cap, "%s", t[s->ws]); return; }
    case P_POINTEREVENTS: snprintf(out, (size_t)cap, "%s", s->pevents ? "none" : "auto"); return;
    default: break;
    }
    if (p >= 0) css_prop_get(n, prop, out, cap);
}

const char css_ua_text[] =
    "html,address,blockquote,body,dd,div,dl,dt,fieldset,form,frame,frameset,h1,h2,h3,h4,h5,h6,noframes,ol,p,ul,center,dir,hr,menu,pre,article,aside,footer,header,hgroup,main,nav,section,figure,figcaption,details,legend,search,dialog[open],optgroup,listing,plaintext,xmp{display:block}"
    "li{display:list-item}summary{display:block}"
    "head,script,style,link,meta,title,base,noscript,template,datalist,param,area,map,input[type=hidden],source,track,dialog,[hidden]{display:none}"
    "dialog[open]{display:block;position:absolute;left:0;right:0;margin:auto;border:2px solid;padding:1em;background:white;color:black;width:fit-content}"
    "html{color:#000;font-family:sans-serif;font-size:16px;line-height:normal}"
    "body{margin:8px}"
    "p,dl,figure,blockquote,pre,fieldset+p{margin-top:1em;margin-bottom:1em}"
    "h1{font-size:2em;margin:.67em 0;font-weight:bold}h2{font-size:1.5em;margin:.83em 0;font-weight:bold}h3{font-size:1.17em;margin:1em 0;font-weight:bold}"
    "h4{margin:1.33em 0;font-weight:bold}h5{font-size:.83em;margin:1.67em 0;font-weight:bold}h6{font-size:.67em;margin:2.33em 0;font-weight:bold}"
    "article h1,aside h1,nav h1,section h1{font-size:1.5em;margin:.83em 0}"
    "ul,menu,dir{display:block;list-style-type:disc;margin:1em 0;padding-left:40px}ol{display:block;list-style-type:decimal;margin:1em 0;padding-left:40px}"
    "li ul,li ol,ul ul,ol ul,ul ol,ol ol,li menu{margin-top:0;margin-bottom:0}ul ul,ol ul{list-style-type:circle}ul ul ul,ol ul ul,ul ol ul,ol ol ul{list-style-type:square}"
    "dd{margin-left:40px}blockquote,figure{margin-left:40px;margin-right:40px}"
    "b,strong{font-weight:bold}i,em,cite,var,dfn,address{font-style:italic}u,ins{text-decoration:underline}s,strike,del{text-decoration:line-through}"
    "abbr[title],acronym[title]{text-decoration:underline}"
    "code,kbd,samp,tt,pre,listing,xmp,plaintext{font-family:monospace;font-size:.8125em}pre,listing,xmp,plaintext{white-space:pre}"
    "a:link,a:visited{color:#0000ee;text-decoration:underline;cursor:pointer}a:link:active{color:#ee0000}"
    "small{font-size:smaller}big{font-size:larger}sub{vertical-align:sub;font-size:smaller}sup{vertical-align:super;font-size:smaller}"
    "center{text-align:center}hr{display:block;margin:.5em auto;border-style:inset;border-width:1px;border-color:#c0c0c0 #eeeeee #eeeeee #c0c0c0;color:gray;overflow:hidden}"
    "table{display:table;border-spacing:2px;border-collapse:separate;box-sizing:border-box;text-indent:0}"
    "caption{display:table-caption;text-align:center}thead{display:table-header-group;vertical-align:middle}tbody{display:table-row-group;vertical-align:middle}"
    "tfoot{display:table-footer-group;vertical-align:middle}col{display:table-column}colgroup{display:table-column-group}"
    "tr{display:table-row;vertical-align:inherit}td,th{display:table-cell;vertical-align:inherit;padding:1px}th{font-weight:bold;text-align:center}"
    "table>tr,thead>tr,tbody>tr,tfoot>tr{vertical-align:middle}td,th{vertical-align:middle}"
    "mark{background-color:yellow;color:black}"
    "img,svg,video,canvas,iframe,object,embed,audio{display:inline-block}audio:not([controls]){display:none}"
    "iframe{border:2px inset #c0c0c0;width:300px;height:150px}video,canvas,object,embed{width:300px;height:150px}svg:not([width]){width:300px}svg:not([height]){height:150px}"
    "input,button,select,textarea{display:inline-block;font-family:sans-serif;font-size:13.333px;color:#000;letter-spacing:normal;word-spacing:normal;line-height:normal;text-transform:none;text-indent:0;text-align:start;box-sizing:border-box}"
    "input,textarea{background:#fff;border:1px solid #767676;border-radius:2px;padding:1px 2px}input{height:21px}"
    "textarea{font-family:monospace;padding:2px;white-space:pre-wrap;vertical-align:text-bottom}"
    "input[type=checkbox],input[type=radio]{width:13px;height:13px;margin:3px 3px 3px 4px;padding:0;border:1px solid #767676;vertical-align:-2px}input[type=radio]{border-radius:50%}"
    "button,input[type=submit],input[type=button],input[type=reset],input[type=file]{width:auto;height:auto;background:#efefef;border:1px solid #767676;border-radius:3px;padding:1px 6px;text-align:center;cursor:default}"
    "input[type=range]{border:0;background:transparent;width:129px;height:16px;padding:0}input[type=color]{width:50px;height:27px;padding:1px 2px}input[type=image]{width:auto;height:auto;border:0;padding:0}"
    "select{background:#fff;border:1px solid #767676;border-radius:3px;padding:1px 20px 1px 4px;height:21px}option,optgroup{display:block}select option,select optgroup{display:none}"
    "fieldset{margin:0 2px;padding:.35em .75em .625em;border:2px groove #c0c0c0;min-width:min-content}legend{padding:0 2px}"
    "progress,meter{display:inline-block;width:160px;height:16px;vertical-align:-.2em}"
    "details>summary:first-of-type{display:list-item;list-style:disclosure-closed inside;cursor:pointer}"
    "details:not([open])>:not(summary){display:none}"
    "q::before{content:open-quote}q::after{content:close-quote}"
    "ruby{display:inline}rt{font-size:50%}rp{display:none}"
    "slot{display:contents}";
