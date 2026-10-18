#include "web/nwe.h"
#include "web/css.h"

typedef struct ffent { char* family; int wlo, whi, italic; char* urls[4]; int fmt[4]; int nurl; unsigned rlo, rhi; int latin; int face; int tried; struct ffent* next; } ffent;
static ffent* g_ff;
static struct { char url[512]; int face; } g_loaded[48];
static int g_nloaded;

void fontface_reset(void) { g_ff = 0; }
void* ff_head(void) { return g_ff; }
void* ff_next(void* n) { return ((ffent*)n)->next; }
void ff_relink(void* n) { ffent* e = n; e->next = g_ff; g_ff = e; }
void webfont_reset(void) { g_ff = 0; g_nloaded = 0; }

static char* unquote(const char* s) {
    while (*s == ' ') s++;
    int n = (int)strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\n')) n--;
    if (n >= 2 && (s[0] == '"' || s[0] == '\'') && s[n - 1] == s[0]) { s++; n -= 2; }
    char* o = nstrndup(s, n);
    for (char* q = o; *q; q++) *q = (char)lc((unsigned char)*q);
    return o;
}

static int fmt_rank(const char* f) {
    if (!f) return 3;
    if (strstr(f, "woff2")) return 1;
    if (strstr(f, "woff")) return 2;
    if (strstr(f, "truetype") || strstr(f, "ttf")) return 3;
    if (strstr(f, "opentype")) return 4;
    if (strstr(f, "embedded-opentype") || strstr(f, "svg")) return 0;
    return 3;
}

void fontface_add(decl* d, int nd, const char* base) {
    ffent* e = nalloc(sizeof(ffent));
    memset(e, 0, sizeof *e);
    e->wlo = 400; e->whi = 400; e->rlo = 0; e->rhi = 0x10FFFF; e->latin = 1; e->face = -1;
    for (int i = 0; i < nd; i++) {
        const char* v = d[i].val;
        if (d[i].prop == P_FONTFAMILY) e->family = unquote(v);
        else if (d[i].prop == P_FONTWEIGHT) {
            const char* p = v;
            if (istarts(p, "bold")) e->wlo = e->whi = 700;
            else if (istarts(p, "normal")) e->wlo = e->whi = 400;
            else { e->wlo = (int)parse_float(p, &p); while (*p == ' ') p++; e->whi = *p ? (int)parse_float(p, 0) : e->wlo; if (e->wlo <= 0) e->wlo = 400; if (e->whi < e->wlo) e->whi = e->wlo; }
        } else if (d[i].prop == P_FONTSTYLE) e->italic = istarts(v, "italic") || istarts(v, "oblique");
        else if (d[i].prop == P_UNICODERANGE) {
            const char* p = v;
            e->latin = 0;
            int first = 1;
            while (*p) {
                while (*p == ' ' || *p == ',') p++;
                if (!(*p == 'U' || *p == 'u')) { if (*p) p++; continue; }
                p += 2;
                unsigned a = 0, b;
                int wild = 0;
                while ((*p >= '0' && *p <= '9') || ((*p | 32) >= 'a' && (*p | 32) <= 'f') || *p == '?') { if (*p == '?') { wild++; a = a * 16; } else a = a * 16 + (unsigned)(*p <= '9' ? *p - '0' : (*p | 32) - 'a' + 10); p++; }
                b = a;
                if (wild) { unsigned m = 1; for (int k = 0; k < wild; k++) m *= 16; b = a + m - 1; }
                if (*p == '-') { p++; b = 0; while ((*p >= '0' && *p <= '9') || ((*p | 32) >= 'a' && (*p | 32) <= 'f')) { b = b * 16 + (unsigned)(*p <= '9' ? *p - '0' : (*p | 32) - 'a' + 10); p++; } }
                if (first) { e->rlo = a; e->rhi = b; first = 0; }
                if (a <= 0x61 && b >= 0x41) e->latin = 1;
            }
        } else if (d[i].prop == P_SRC) {
            const char* p = v;
            while (*p && e->nurl < 4) {
                const char* u = strstr(p, "url(");
                if (!u) break;
                u += 4;
                while (*u == ' ') u++;
                char q = (*u == '"' || *u == '\'') ? *u++ : 0;
                const char* s = u;
                while (*u && (q ? *u != q : *u != ')')) u++;
                char* url = nstrndup(s, (int)(u - s));
                if (*u) u++;
                while (*u && *u != ')' && q) u++;
                if (*u == ')') u++;
                const char* f = u;
                while (*f == ' ') f++;
                char* fmt = 0;
                if (istarts(f, "format(")) { const char* fe = strchr(f, ')'); if (fe) fmt = nstrndup(f + 7, (int)(fe - f - 7)); }
                int rk = fmt_rank(fmt);
                if (rk) {
                    char full[1024];
                    url_resolve(base ? base : G.base, url, full, sizeof(full));
                    e->urls[e->nurl] = nstrdup(full);
                    e->fmt[e->nurl] = rk;
                    e->nurl++;
                }
                p = u;
            }
        }
    }
    if (!e->family || !e->nurl) return;
    e->next = g_ff;
    g_ff = e;
}

static int load_entry(ffent* e) {
    if (e->tried) return e->face;
    e->tried = 1;
    for (int pass = 1; pass <= 4 && e->face < 0; pass++) {
        for (int i = 0; i < e->nurl && e->face < 0; i++) {
            if (e->fmt[i] != pass) continue;
            for (int k = 0; k < g_nloaded; k++) if (!strcmp(g_loaded[k].url, e->urls[i])) { e->face = g_loaded[k].face; break; }
            if (e->face >= 0) break;
            int len = 0;
            char* data = eng_fetch_text(e->urls[i], &len);
            int f = data && len > 12 ? ttf_load((const unsigned char*)data, len, e->family) : -1;
            if (g_nloaded < 48) { snprintf(g_loaded[g_nloaded].url, sizeof g_loaded[0].url, "%s", e->urls[i]); g_loaded[g_nloaded].face = f; g_nloaded++; }
            if (f >= 0) e->face = f;
            else { char m[200]; snprintf(m, sizeof m, "font: could not use %.150s", e->urls[i]); nwk_log(m); }
        }
    }
    return e->face;
}

int wf_face(const char* fam, int weight, int italic) {
    ffent* best = 0;
    int bs = 1 << 30;
    for (ffent* e = g_ff; e; e = e->next) {
        if (strcmp(e->family, fam)) continue;
        int sc = 0;
        if (!e->latin) sc += 5000;
        if (e->italic != italic) sc += 2000;
        if (weight < e->wlo) sc += e->wlo - weight;
        else if (weight > e->whi) sc += weight - e->whi;
        if (e->tried && e->face < 0) sc += 100000;
        if (sc < bs) { bs = sc; best = e; }
    }
    if (!best) return -1;
    int f = load_entry(best);
    if (f < 0) {
        for (ffent* e = g_ff; e; e = e->next) if (!strcmp(e->family, fam) && !e->tried) { f = load_entry(e); if (f >= 0) break; }
    }
    return f;
}

int wf_has(const char* fam) { for (ffent* e = g_ff; e; e = e->next) if (!strcmp(e->family, fam)) return 1; return 0; }
