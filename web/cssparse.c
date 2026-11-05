#include "web/nwe.h"
#include "web/css.h"

static rule** R;
static int nR, cR;
static int rorder;
static const char* g_parse_layer;
static int g_anon_layer;
static char* g_lay_n[128]; static int g_nlay;
int css_layer_rank(const char* name) {
    if (!name) return 1 << 20;
    for (int i = 0; i < g_nlay; i++) if (!strcmp(g_lay_n[i], name)) return i + 1;
    if (g_nlay < 128) { g_lay_n[g_nlay] = nstrdup(name); g_nlay++; return g_nlay; }
    return 127;
}
static const char* g_lay_seq[256]; static int g_nlay_seq;
static void lay_note(const char* name) { css_layer_rank(name); if (g_nlay_seq < 256) g_lay_seq[g_nlay_seq++] = name; }
static const char* lay_join(const char* outer, const char* in) {
    if (!outer) return nstrdup(in);
    int a = (int)strlen(outer), b = (int)strlen(in); char* r = nalloc((size_t)(a + b + 2)); memcpy(r, outer, (size_t)a); r[a] = '.'; memcpy(r + a + 1, in, (size_t)b + 1); return r;
}
static char* g_prop_n[128]; static char* g_prop_v[128]; static int g_nprop;
void css_prop_register(const char* name, const char* init) {
    if (!name || name[0] != '-' || name[1] != '-' || !init) return;
    int f = -1; for (int k = 0; k < g_nprop; k++) if (!strcmp(g_prop_n[k], name)) f = k;
    if (f < 0 && g_nprop < 128) { f = g_nprop++; g_prop_n[f] = nstrdup(name); }
    if (f >= 0) { g_prop_v[f] = nstrdup(init); g_css_gen++; }
}
const char* css_prop_initial(const char* name) { for (int i = 0; i < g_nprop; i++) if (!strcmp(g_prop_n[i], name)) return g_prop_v[i]; return 0; }
int g_css_gen = 1;
int g_css_hover = 0;
static int built_gen = 0;
void css_bump(void) { g_css_gen++; }

static const char* skip_ws_c(const char* p, const char* e) {
    for (;;) {
        while (p < e && is_ws((unsigned char)*p)) p++;
        if (p + 1 < e && p[0] == '/' && p[1] == '*') {
            p += 2;
            while (p + 1 < e && !(p[0] == '*' && p[1] == '/')) p++;
            p = p + 2 <= e ? p + 2 : e;
            continue;
        }
        return p;
    }
}
static const char* skip_str(const char* p, const char* e) {
    char q = *p++;
    while (p < e && *p != q) { if (*p == '\\' && p + 1 < e) p++; p++; }
    return p < e ? p + 1 : e;
}
static const char* skip_block(const char* p, const char* e) {
    int d = 0;
    while (p < e) {
        if (*p == '"' || *p == '\'') { p = skip_str(p, e); continue; }
        if (p + 1 < e && p[0] == '/' && p[1] == '*') { p = skip_ws_c(p, e); continue; }
        if (*p == '{') d++;
        else if (*p == '}') { d--; if (d == 0) return p + 1; }
        p++;
    }
    return e;
}
static char* trimdup(const char* s, const char* e) {
    while (s < e && is_ws((unsigned char)*s)) s++;
    while (e > s && is_ws((unsigned char)e[-1])) e--;
    sbuf b = { 0, 0, 0 };
    const char* p = s;
    while (p < e) {
        if (p + 1 < e && p[0] == '/' && p[1] == '*') { p += 2; while (p + 1 < e && !(p[0] == '*' && p[1] == '/')) p++; p += 2; sb_putc(&b, ' '); continue; }
        if (*p == '"' || *p == '\'') { const char* q = skip_str(p, e); sb_putn(&b, p, (int)(q - p)); p = q; continue; }
        sb_putc(&b, *p++);
    }
    return sb_str(&b);
}

static int ident_ch(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c >= 128 || c == '\\'; }
static char* read_ident(const char** pp, const char* e) {
    const char* p = *pp;
    sbuf b = { 0, 0, 0 };
    while (p < e && ident_ch((unsigned char)*p)) {
        if (*p == '\\' && p + 1 < e) {
            p++;
            int hv = 0, hn = 0;
            while (p < e && hn < 6 && ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))) { hv = hv * 16 + (*p <= '9' ? *p - '0' : (*p | 32) - 'a' + 10); p++; hn++; }
            if (hn) { sb_putu8(&b, hv); if (p < e && *p == ' ') p++; }
            else sb_putc(&b, *p++);
            continue;
        }
        sb_putc(&b, *p++);
    }
    *pp = p;
    return sb_str(&b);
}

static int parse_nth(const char* s, int* a, int* b) {
    while (is_ws((unsigned char)*s)) s++;
    if (istarts(s, "odd")) { *a = 2; *b = 1; return 1; }
    if (istarts(s, "even")) { *a = 2; *b = 0; return 1; }
    int sign = 1;
    const char* p = s;
    if (*p == '+') p++; else if (*p == '-') { sign = -1; p++; }
    int num = 0, hasnum = 0;
    while (*p >= '0' && *p <= '9') { num = num * 10 + (*p - '0'); p++; hasnum = 1; }
    if (*p == 'n' || *p == 'N') {
        *a = sign * (hasnum ? num : 1);
        p++;
        while (is_ws((unsigned char)*p)) p++;
        int bs = 1;
        if (*p == '+') { p++; } else if (*p == '-') { bs = -1; p++; } else { *b = 0; return 1; }
        while (is_ws((unsigned char)*p)) p++;
        int bv = 0;
        while (*p >= '0' && *p <= '9') { bv = bv * 10 + (*p - '0'); p++; }
        *b = bs * bv;
        return 1;
    }
    if (!hasnum) return 0;
    *a = 0;
    *b = sign * num;
    return 1;
}

static int parse_compound_list(const char* s, const char* e, csel*** out, int forgiving);
static int parse_has_list(const char* s, const char* e, csel*** out);

static csel* parse_complex(const char* s, const char* e, int* ok) {
    compound cs[32];
    int nc = 0;
    ssel ss[24];
    int ns = 0;
    char comb = 0;
    int spec = 0, pseudo = 0;
    const char* p = s;
    *ok = 1;
    p = skip_ws_c(p, e);
    while (p < e) {
        char c = *p;
        if (is_ws((unsigned char)c) || c == '>' || c == '+' || c == '~') {
            int sawc = 0;
            while (p < e && (is_ws((unsigned char)*p) || *p == '>' || *p == '+' || *p == '~')) { if (*p != ' ' && !is_ws((unsigned char)*p)) { comb = *p; sawc = 1; } p++; }
            if (p >= e) break;
            if (!sawc) comb = ' ';
            if (ns == 0 && nc == 0) { *ok = 0; return 0; }
            if (ns > 0) {
                if (nc >= 32) { *ok = 0; return 0; }
                cs[nc].s = nalloc(sizeof(ssel) * (size_t)ns);
                memcpy(cs[nc].s, ss, sizeof(ssel) * (size_t)ns);
                cs[nc].ns = ns;
                cs[nc].comb = 0;
                nc++;
                ns = 0;
            }
            continue;
        }
        if (pseudo) { *ok = 0; return 0; }
        if (ns >= 24) { *ok = 0; return 0; }
        ssel* x = &ss[ns];
        memset(x, 0, sizeof(*x));
        if (ns == 0 && nc > 0) cs[nc - 1].comb = comb ? comb : ' ', comb = 0;
        if (c == '*') { x->kind = SK_UNIV; p++; ns++; continue; }
        if (c == '&') { x->kind = SK_UNIV; p++; ns++; continue; }
        if (c == '#') { p++; x->kind = SK_ID; x->name = read_ident(&p, e); spec += 10000; ns++; continue; }
        if (c == '.') { p++; x->kind = SK_CLASS; x->name = read_ident(&p, e); if (!x->name[0]) { *ok = 0; return 0; } spec += 100; ns++; continue; }
        if (c == '[') {
            p++;
            p = skip_ws_c(p, e);
            x->kind = SK_ATTR;
            x->name = read_ident(&p, e);
            for (char* q = x->name; *q; q++) *q = (char)lc((unsigned char)*q);
            p = skip_ws_c(p, e);
            x->op = AO_EXISTS;
            if (p < e && *p != ']') {
                if (*p == '=') { x->op = AO_EQ; p++; }
                else if (p + 1 < e && p[1] == '=') {
                    x->op = *p == '~' ? AO_WORD : *p == '|' ? AO_DASH : *p == '^' ? AO_PREFIX : *p == '$' ? AO_SUFFIX : *p == '*' ? AO_SUB : 0;
                    if (!x->op) { *ok = 0; return 0; }
                    p += 2;
                } else { *ok = 0; return 0; }
                p = skip_ws_c(p, e);
                if (p < e && (*p == '"' || *p == '\'')) { const char* q = skip_str(p, e); x->val = nstrndup(p + 1, (int)(q - p - 2)); p = q; }
                else x->val = read_ident(&p, e);
                p = skip_ws_c(p, e);
                if (p < e && (*p == 'i' || *p == 'I')) { x->ci = 1; p++; }
                else if (p < e && (*p == 's' || *p == 'S')) p++;
                p = skip_ws_c(p, e);
            }
            if (p >= e || *p != ']') { *ok = 0; return 0; }
            p++;
            spec += 100;
            ns++;
            continue;
        }
        if (c == ':') {
            p++;
            int el = 0;
            if (p < e && *p == ':') { el = 1; p++; }
            char* nm = read_ident(&p, e);
            for (char* q = nm; *q; q++) *q = (char)lc((unsigned char)*q);
            const char* args = 0;
            const char* argse = 0;
            if (p < e && *p == '(') {
                int d = 0;
                args = p + 1;
                while (p < e) { if (*p == '(') d++; else if (*p == ')') { d--; if (!d) break; } else if (*p == '"' || *p == '\'') { p = skip_str(p, e); continue; } p++; }
                argse = p;
                if (p < e) p++;
            }
            if ((el && !strcmp(nm, "slotted") && args) || (!el && !strcmp(nm, "host"))) {
                x->kind = SK_PSEUDO;
                int kk = nm[0] == 's' ? PS_SLOTTED : PS_HOST;
                x->nsub = args ? parse_compound_list(args, argse, &x->sub, 1) : 0;
                if (args && x->nsub <= 0) { *ok = 0; return 0; }
                x->a = kk;
                x->val = 0;
                x->tag = kk;
                spec += 10;
                ns++;
                continue;
            }
            if (el || !strcmp(nm, "before") || !strcmp(nm, "after") || !strcmp(nm, "first-line") || !strcmp(nm, "first-letter")) {
                if (!strcmp(nm, "before")) pseudo = 1;
                else if (!strcmp(nm, "after")) pseudo = 2;
                else if (!strcmp(nm, "marker") || !strcmp(nm, "placeholder") || !strcmp(nm, "selection") || !strcmp(nm, "first-line") || !strcmp(nm, "first-letter") || !strcmp(nm, "backdrop") || !strcmp(nm, "file-selector-button") || !strcmp(nm, "cue")) pseudo = 3;
                else { *ok = 0; return 0; }
                spec += 1;
                continue;
            }
            x->kind = SK_PSEUDO;
            int k = 0;
            if (!strcmp(nm, "hover")) { k = PS_HOVER; g_css_hover = 1; }
            else if (!strcmp(nm, "focus")) k = PS_FOCUS;
            else if (!strcmp(nm, "focus-visible")) k = PS_FOCUS;
            else if (!strcmp(nm, "active")) { k = PS_ACTIVE; g_css_hover = 1; }
            else if (!strcmp(nm, "first-child")) k = PS_FIRST;
            else if (!strcmp(nm, "last-child")) k = PS_LAST;
            else if (!strcmp(nm, "only-child")) k = PS_ONLY;
            else if (!strcmp(nm, "first-of-type")) k = PS_FIRSTTYPE;
            else if (!strcmp(nm, "last-of-type")) k = PS_LASTTYPE;
            else if (!strcmp(nm, "only-of-type")) k = PS_ONLYTYPE;
            else if (!strcmp(nm, "empty")) k = PS_EMPTY;
            else if (!strcmp(nm, "root")) k = PS_ROOT;
            else if (!strcmp(nm, "link") || !strcmp(nm, "any-link")) k = PS_LINK;
            else if (!strcmp(nm, "visited") || !strcmp(nm, "target") || !strcmp(nm, "indeterminate") || !strcmp(nm, "invalid") || !strcmp(nm, "fullscreen") || !strcmp(nm, "autofill") || !strcmp(nm, "-webkit-autofill") || !strcmp(nm, "user-invalid") || !strcmp(nm, "target-within") || !strcmp(nm, "playing") || !strcmp(nm, "paused")) k = PS_FALSE;
            else if (!strcmp(nm, "checked") || !strcmp(nm, "selected")) k = PS_CHECKED;
            else if (!strcmp(nm, "disabled")) k = PS_DISABLED;
            else if (!strcmp(nm, "enabled")) k = PS_ENABLED;
            else if (!strcmp(nm, "focus-within")) k = PS_FOCUSWITHIN;
            else if (!strcmp(nm, "placeholder-shown")) k = PS_PLACEHOLDER;
            else if (!strcmp(nm, "required")) k = PS_REQUIRED;
            else if (!strcmp(nm, "optional")) k = PS_OPTIONAL;
            else if (!strcmp(nm, "scope")) k = PS_ROOT;
            else if (!strcmp(nm, "defined") || !strcmp(nm, "valid") || !strcmp(nm, "in-range") || !strcmp(nm, "read-write") || !strcmp(nm, "user-valid")) k = PS_TRUE;
            else if (!strcmp(nm, "read-only")) k = PS_FALSE;
            else if (!strcmp(nm, "open")) k = PS_OPEN;
            else if (!strcmp(nm, "popover-open")) k = PS_POPOPEN;
            else if (!strcmp(nm, "modal")) k = PS_MODAL;
            else if (!strcmp(nm, "nth-child") || !strcmp(nm, "nth-last-child") || !strcmp(nm, "nth-of-type") || !strcmp(nm, "nth-last-of-type")) {
                k = !strcmp(nm, "nth-child") ? PS_NTH : !strcmp(nm, "nth-last-child") ? PS_NTHLAST : !strcmp(nm, "nth-of-type") ? PS_NTHTYPE : PS_NTHLASTTYPE;
                if (!args) { *ok = 0; return 0; }
                char* a = nstrndup(args, (int)(argse - args));
                if (k == PS_NTH || k == PS_NTHLAST) {
                    char* of = 0;
                    for (char* q = a; *q; q++) if ((q == a || is_ws((unsigned char)q[-1])) && lc((unsigned char)q[0]) == 'o' && lc((unsigned char)q[1]) == 'f' && is_ws((unsigned char)q[2])) { of = q; break; }
                    if (of) { const char* ss0 = args + (of - a) + 3; *of = 0; x->nsub = parse_compound_list(ss0, argse, &x->sub, 0); if (x->nsub <= 0) { *ok = 0; return 0; } int mx = 0; for (int i = 0; i < x->nsub; i++) if (x->sub[i]->spec > mx) mx = x->sub[i]->spec; spec += mx; }
                }
                if (!parse_nth(a, &x->a, &x->b)) { *ok = 0; return 0; }
            } else if (!strcmp(nm, "not") || !strcmp(nm, "is") || !strcmp(nm, "where") || !strcmp(nm, "matches") || !strcmp(nm, "-webkit-any") || !strcmp(nm, "-moz-any") || !strcmp(nm, "has")) {
                k = !strcmp(nm, "not") ? PS_NOT : !strcmp(nm, "has") ? PS_HAS : PS_IS;
                if (!args) { *ok = 0; return 0; }
                x->nsub = k == PS_HAS ? parse_has_list(args, argse, &x->sub) : parse_compound_list(args, argse, &x->sub, k != PS_NOT);
                if (x->nsub <= 0 && k == PS_NOT) { *ok = 0; return 0; }
                if (strcmp(nm, "where")) { int mx = 0; for (int i = 0; i < x->nsub; i++) if (x->sub[i]->spec > mx) mx = x->sub[i]->spec; spec += mx; }
                x->kind = SK_PSEUDO;
                x->a = k;
                ns++;
                continue;
            } else if (!strcmp(nm, "lang") || !strcmp(nm, "dir")) k = PS_TRUE;
            else if (!strcmp(nm, "host-context") || !strcmp(nm, "state")) k = PS_FALSE;
            else if (nm[0] == '-') k = PS_FALSE;
            else { *ok = 0; return 0; }
            if (k != PS_NTH && k != PS_NTHLAST && k != PS_NTHTYPE && k != PS_NTHLASTTYPE) x->a = k;
            else { x->op = (unsigned char)k; }
            x->val = 0;
            x->tag = k;
            spec += 100;
            ns++;
            continue;
        }
        if (ident_ch((unsigned char)c)) {
            x->kind = SK_TAG;
            x->name = read_ident(&p, e);
            for (char* q = x->name; *q; q++) *q = (char)lc((unsigned char)*q);
            x->tag = tag_id(x->name);
            spec += 1;
            ns++;
            continue;
        }
        *ok = 0;
        return 0;
    }
    if (ns == 0) { if (nc == 0 || comb) { *ok = 0; return 0; } }
    if (ns > 0) {
        cs[nc].s = nalloc(sizeof(ssel) * (size_t)ns);
        memcpy(cs[nc].s, ss, sizeof(ssel) * (size_t)ns);
        cs[nc].ns = ns;
        cs[nc].comb = 0;
        nc++;
    } else if (nc > 0 && pseudo) {}
    if (nc == 0 && pseudo) {
        cs[0].s = nalloc(sizeof(ssel));
        memset(cs[0].s, 0, sizeof(ssel));
        cs[0].s[0].kind = SK_UNIV;
        cs[0].ns = 1;
        cs[0].comb = 0;
        nc = 1;
    }
    for (int i = nc - 1; i > 0; i--) cs[i].comb = cs[i - 1].comb ? cs[i - 1].comb : ' ';
    cs[0].comb = 0;
    csel* r = nzalloc(sizeof(csel));
    r->c = nalloc(sizeof(compound) * (size_t)nc);
    memcpy(r->c, cs, sizeof(compound) * (size_t)nc);
    r->nc = nc;
    r->spec = spec;
    r->pseudo = pseudo;
    return r;
}

static int parse_compound_list(const char* s, const char* e, csel*** out, int forgiving);
static int parse_has_list(const char* s, const char* e, csel*** out) {
    csel* tmp[64];
    int n = 0;
    const char* p = s;
    while (p < e) {
        while (p < e && is_ws((unsigned char)*p)) p++;
        unsigned char rel = 0;
        if (p < e && (*p == '>' || *p == '+' || *p == '~')) { rel = (unsigned char)*p; p++; }
        const char* st = p;
        int d = 0;
        while (p < e) {
            if (*p == '(' || *p == '[') d++;
            else if (*p == ')' || *p == ']') d--;
            else if (*p == '"' || *p == '\'') { p = skip_str(p, e); continue; }
            else if (*p == ',' && d == 0) break;
            p++;
        }
        int ok;
        csel* c = parse_complex(st, p, &ok);
        if (ok && c && n < 64) { c->rel = rel; tmp[n++] = c; }
        if (p < e) p++;
    }
    *out = nalloc(sizeof(csel*) * (size_t)(n ? n : 1));
    memcpy(*out, tmp, sizeof(csel*) * (size_t)n);
    return n;
}
static int parse_compound_list(const char* s, const char* e, csel*** out, int forgiving) {
    csel* tmp[64];
    int n = 0;
    const char* p = s;
    while (p < e) {
        const char* st = p;
        int d = 0;
        while (p < e) {
            if (*p == '(' || *p == '[') d++;
            else if (*p == ')' || *p == ']') d--;
            else if (*p == '"' || *p == '\'') { p = skip_str(p, e); continue; }
            else if (*p == ',' && d == 0) break;
            p++;
        }
        int ok;
        csel* c = parse_complex(st, p, &ok);
        if (!ok || !c) { if (!forgiving) return -1; }
        else if (n < 64) tmp[n++] = c;
        if (p < e) p++;
    }
    *out = nalloc(sizeof(csel*) * (size_t)(n ? n : 1));
    memcpy(*out, tmp, sizeof(csel*) * (size_t)n);
    return n;
}

static node* pelem(node* n) { node* p = n->parent; return (p && p->type == NT_ELEM) ? p : 0; }
static node* prev_el(node* n) { for (node* p = n->prev; p; p = p->prev) if (p->type == NT_ELEM) return p; return 0; }
static node* next_el(node* n) { for (node* p = n->next; p; p = p->next) if (p->type == NT_ELEM) return p; return 0; }
static int nth_ok(int a, int b, int idx) {
    if (a == 0) return idx == b;
    int d = idx - b;
    if (d % a) return 0;
    return d / a >= 0;
}
static int is_form_ctl(node* n) { return n->tag == T_INPUT || n->tag == T_BUTTON || n->tag == T_SELECT || n->tag == T_TEXTAREA || n->tag == T_OPTION || n->tag == T_FIELDSET; }

int css_match_sel(csel* cs, node* n);
static int match_simple(ssel* x, node* n) {
    switch (x->kind) {
    case SK_UNIV: return 1;
    case SK_TAG: return x->tag ? n->tag == x->tag : ieq(n->name, x->name);
    case SK_ID: { const char* v = attr_get(n, "id"); return v && !strcmp(v, x->name); }
    case SK_CLASS: return has_class(n, x->name);
    case SK_ATTR: {
        const char* v = attr_get(n, x->name);
        if (!v) return 0;
        if (x->op == AO_EXISTS) return 1;
        const char* w = x->val ? x->val : "";
        int wl = (int)strlen(w), vl = (int)strlen(v);
        int (*cmpn)(const char*, const char*, size_t) = x->ci ? 0 : strncmp;
        #define EQN(a, b, k) (x->ci ? ieqn(a, b, k) : !cmpn(a, b, (size_t)(k)))
        switch (x->op) {
        case AO_EQ: return vl == wl && EQN(v, w, wl);
        case AO_PREFIX: return wl > 0 && vl >= wl && EQN(v, w, wl);
        case AO_SUFFIX: return wl > 0 && vl >= wl && EQN(v + vl - wl, w, wl);
        case AO_SUB: if (!wl) return 0; for (int i = 0; i + wl <= vl; i++) if (EQN(v + i, w, wl)) return 1; return 0;
        case AO_DASH: return (vl == wl && EQN(v, w, wl)) || (vl > wl && EQN(v, w, wl) && v[wl] == '-');
        case AO_WORD: {
            const char* p = v;
            while (*p) {
                while (*p && is_ws((unsigned char)*p)) p++;
                const char* s = p;
                while (*p && !is_ws((unsigned char)*p)) p++;
                if (p - s == wl && wl && EQN(s, w, wl)) return 1;
            }
            return 0;
        }
        }
        #undef EQN
        return 0;
    }
    case SK_PSEUDO: {
        int k = x->a;
        if (x->op) k = x->op;
        switch (k) {
        case PS_HOVER: { for (node* h = G.hover; h; h = h->parent) if (h == n) return 1; return 0; }
        case PS_FOCUS: return G.focus == n;
        case PS_ACTIVE: { for (node* h = G.active; h; h = h->parent) if (h == n) return 1; return 0; }
        case PS_FOCUSWITHIN: return G.focus && node_contains(n, G.focus);
        case PS_FIRST: return pelem(n) && !prev_el(n);
        case PS_LAST: return pelem(n) && !next_el(n);
        case PS_ONLY: return pelem(n) && !prev_el(n) && !next_el(n);
        case PS_FIRSTTYPE: { for (node* p = prev_el(n); p; p = prev_el(p)) if (p->tag == n->tag && ieq(p->name, n->name)) return 0; return 1; }
        case PS_LASTTYPE: { for (node* p = next_el(n); p; p = next_el(p)) if (p->tag == n->tag && ieq(p->name, n->name)) return 0; return 1; }
        case PS_ONLYTYPE: { for (node* p = prev_el(n); p; p = prev_el(p)) if (ieq(p->name, n->name)) return 0; for (node* p = next_el(n); p; p = next_el(p)) if (ieq(p->name, n->name)) return 0; return 1; }
        case PS_NTH: case PS_NTHLAST: {
            int i = 1;
            if (x->nsub > 0) { int m = 0; for (int q = 0; q < x->nsub && !m; q++) if (css_match_sel(x->sub[q], n)) m = 1; if (!m) return 0; }
            for (node* p = k == PS_NTH ? prev_el(n) : next_el(n); p; p = k == PS_NTH ? prev_el(p) : next_el(p)) {
                if (x->nsub > 0) { int m = 0; for (int q = 0; q < x->nsub && !m; q++) if (css_match_sel(x->sub[q], p)) m = 1; if (!m) continue; }
                i++;
            }
            return nth_ok(x->a, x->b, i);
        }
        case PS_NTHTYPE: { int i = 1; for (node* p = prev_el(n); p; p = prev_el(p)) if (ieq(p->name, n->name)) i++; return nth_ok(x->a, x->b, i); }
        case PS_NTHLASTTYPE: { int i = 1; for (node* p = next_el(n); p; p = next_el(p)) if (ieq(p->name, n->name)) i++; return nth_ok(x->a, x->b, i); }
        case PS_EMPTY: { for (node* c = n->first; c; c = c->next) if (c->type == NT_ELEM || (c->type == NT_TEXT && c->tlen)) return 0; return 1; }
        case PS_ROOT: return n->tag == T_HTML && n->parent == G.doc;
        case PS_LINK: return (n->tag == T_A || n->tag == T_AREA) && attr_get(n, "href");
        case PS_CHECKED: return (n->tag == T_INPUT && n->checked) || (n->tag == T_OPTION && attr_get(n, "selected"));
        case PS_DISABLED: return is_form_ctl(n) && attr_get(n, "disabled");
        case PS_ENABLED: return is_form_ctl(n) && !attr_get(n, "disabled");
        case PS_PLACEHOLDER: return (n->tag == T_INPUT || n->tag == T_TEXTAREA) && attr_get(n, "placeholder") && n->vlen == 0 && !(attr_get(n, "value") && *attr_get(n, "value"));
        case PS_REQUIRED: return is_form_ctl(n) && attr_get(n, "required");
        case PS_OPTIONAL: return is_form_ctl(n) && !attr_get(n, "required");
        case PS_OPEN: return attr_get(n, "open") != 0;
        case PS_POPOPEN: return attr_get(n, "nvx-popover-open") != 0;
        case PS_MODAL: return attr_get(n, "nvx-modal") != 0;
        case PS_TRUE: return 1;
        case PS_FALSE: return 0;
        case PS_NOT: for (int i = 0; i < x->nsub; i++) if (css_match_sel(x->sub[i], n)) return 0; return 1;
        case PS_IS: for (int i = 0; i < x->nsub; i++) if (css_match_sel(x->sub[i], n)) return 1; return 0;
        case PS_HOST: if (!n->shadow) return 0; if (!x->nsub) return 1; for (int i = 0; i < x->nsub; i++) if (css_match_sel(x->sub[i], n)) return 1; return 0;
        case PS_SLOTTED: if (!n->parent || !n->parent->shadow) return 0; for (int i = 0; i < x->nsub; i++) if (css_match_sel(x->sub[i], n)) return 1; return 0;
        case PS_HAS: {
            for (int i = 0; i < x->nsub; i++) {
                csel* s = x->sub[i];
                if (s->rel == '+') { node* c = next_el(n); if (c && css_match_sel(s, c)) return 1; continue; }
                if (s->rel == '~') { for (node* c = next_el(n); c; c = next_el(c)) if (css_match_sel(s, c)) return 1; continue; }
                if (s->rel == '>' && s->nc == 1) { for (node* c = n->first; c; c = c->next) if (c->type == NT_ELEM && css_match_sel(s, c)) return 1; continue; }
                node* stack[256];
                int sp = 0;
                for (node* c = n->first; c; c = c->next) if (c->type == NT_ELEM && sp < 256) stack[sp++] = c;
                while (sp > 0) {
                    node* c = stack[--sp];
                    if (css_match_sel(s, c)) return 1;
                    for (node* d = c->first; d; d = d->next) if (d->type == NT_ELEM && sp < 256) stack[sp++] = d;
                }
            }
            return 0;
        }
        }
        return 0;
    }
    }
    return 0;
}
static int match_compound(compound* c, node* n) {
    for (int i = 0; i < c->ns; i++) if (!match_simple(&c->s[i], n)) return 0;
    return 1;
}
static int match_from(csel* cs, int ci, node* n) {
    if (!match_compound(&cs->c[ci], n)) return 0;
    if (ci == 0) return 1;
    char comb = cs->c[ci].comb;
    if (comb == '>') { node* p = pelem(n); if (!p) return 2; return match_from(cs, ci - 1, p); }
    if (comb == '+') { node* p = prev_el(n); if (!p) return 0; return match_from(cs, ci - 1, p); }
    if (comb == '~') { for (node* p = prev_el(n); p; p = prev_el(p)) { int r = match_from(cs, ci - 1, p); if (r) return r; } return 0; }
    for (node* p = pelem(n); p; p = pelem(p)) { int r = match_from(cs, ci - 1, p); if (r) return r; }
    return 2;
}
const char* g_parse_cq;
static const char* cq_join(const char* a, const char* b) {
    while (*b == ' ') b++;
    if (!a) return nstrdup(b);
    size_t la = strlen(a), lb = strlen(b);
    char* r = nalloc(la + lb + 2);
    memcpy(r, a, la); r[la] = '\x01'; memcpy(r + la + 1, b, lb + 1);
    return r;
}
int css_cq_match(const char* cond, node* n);
int css_match_sel(csel* cs, node* n) {
    if (!cs || cs->nc == 0 || n->type != NT_ELEM) return 0;
    return match_from(cs, cs->nc - 1, n) == 1;
}

int sel_match_str(node* n, const char* sel, int* err) {
    csel** l;
    int k = parse_compound_list(sel, sel + strlen(sel), &l, 0);
    if (k <= 0) { if (err) *err = 1; return 0; }
    if (err) *err = 0;
    for (int i = 0; i < k; i++) if (!l[i]->pseudo && css_match_sel(l[i], n)) return 1;
    return 0;
}

#define QC_N 64
typedef struct { node* root; char* sel; unsigned gen; int all, cnt, k; node** res; node* one; csel** l; } qcent;
static qcent g_qc[QC_N];
static unsigned g_qc_tick;
void qsel_reset(void) { memset(g_qc, 0, sizeof(g_qc)); }
static node* qsel_in(node* root, const char* sel, int all, node*** out, int* nout, csel** l, int k);
static int qs_idc(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c >= 0x80; }
node* qsel(node* root, const char* sel, int all, node*** out, int* nout) {
    extern unsigned g_dom_gen;
    if (nout) *nout = 0;
    if (!all && root && sel[0] == '#' && qs_idc((unsigned char)sel[1]) && !(sel[1] >= '0' && sel[1] <= '9')) {
        const char* p = sel + 1; while (qs_idc((unsigned char)*p)) p++;
        node* doc = root; while (doc->parent) doc = doc->parent;
        int il = (int)(p - sel - 1);
        if (doc->type == NT_DOC && il < 128) {
            char id[128]; memcpy(id, sel + 1, (size_t)il); id[il] = 0;
            const char* rest = p; while (*rest == ' ') rest++;
            int simple = *p == 0;
            int desc = !simple && *p == ' ' && *rest && !strpbrk(rest, " >+~,\\") ;
            if (simple || desc) {
                node* e = find_id(doc, id);
                if (!e) return 0;
                if (root == doc || (e != root && node_contains(root, e))) {
                    if (simple) return e;
                    node* r = qsel(e, rest, 0, 0, 0);
                    if (r) return r;
                }
            }
        }
    }
    unsigned h = 2166136261u; for (const char* q = sel; *q; q++) { h ^= (unsigned char)*q; h *= 16777619u; }
    int slot = (int)((h ^ (unsigned)(size_t)root) % QC_N);
    qcent* e = &g_qc[slot];
    if (e->sel && e->root == root && e->all == all && !strcmp(e->sel, sel)) {
        if (e->gen == g_dom_gen && !strchr(sel, ':')) { if (all) { *out = e->res; *nout = e->cnt; return e->cnt ? e->res[0] : 0; } return e->one; }
        node* r = qsel_in(root, sel, all, out, nout, e->l, e->k);
        e->gen = g_dom_gen; e->one = r; if (all) { e->res = *out; e->cnt = *nout; }
        return r;
    }
    csel** l;
    int k = parse_compound_list(sel, sel + strlen(sel), &l, 0);
    if (k <= 0) { if (nout) *nout = -1; return 0; }
    node* r = qsel_in(root, sel, all, out, nout, l, k);
    int sl = (int)strlen(sel);
    if (sl < 512) { char* cp = nalloc((size_t)sl + 1); memcpy(cp, sel, (size_t)sl + 1); e->sel = cp; e->root = root; e->all = all; e->l = l; e->k = k; e->gen = g_dom_gen; e->one = r; if (all) { e->res = *out; e->cnt = *nout; } }
    g_qc_tick++;
    return r;
}
static node* qsel_in(node* root, const char* sel, int all, node*** out, int* nout, csel** l, int k) {
    (void)sel;
    if (nout) *nout = 0;
    int cap = 16, cnt = 0;
    node** res = all ? nalloc(sizeof(node*) * (size_t)cap) : 0;
    int scap = 1024;
    node** stack = nalloc(sizeof(node*) * (size_t)scap);
    int sp = 0;
    for (node* c = root->first; c; c = c->next) sp++;
    if (sp > scap) { nfree(stack); scap = sp + 1024; stack = nalloc(sizeof(node*) * (size_t)scap); }
    sp = 0;
    for (node* c = root->last; c; c = c->prev) stack[sp++] = c;
    while (sp > 0) {
        node* c = stack[--sp];
        nwe_check_stop();
        if (c->type == NT_ELEM) {
            for (int i = 0; i < k; i++) if (!l[i]->pseudo && css_match_sel(l[i], c)) {
                if (!all) { nfree(stack); return c; }
                if (cnt >= cap) { node** nr = nalloc(sizeof(node*) * (size_t)cap * 2); memcpy(nr, res, sizeof(node*) * (size_t)cnt); nfree(res); res = nr; cap *= 2; }
                res[cnt++] = c;
                break;
            }
            int nk = 0;
            for (node* d = c->first; d; d = d->next) nk++;
            if (sp + nk > scap) { int nc = (sp + nk) * 2; node** ns = nalloc(sizeof(node*) * (size_t)nc); memcpy(ns, stack, sizeof(node*) * (size_t)sp); nfree(stack); stack = ns; scap = nc; }
            for (node* d = c->last; d; d = d->prev) stack[sp++] = d;
        }
    }
    nfree(stack);
    if (all) { *out = res; *nout = cnt; }
    return cnt ? res[0] : 0;
}

static float mq_len(const char* s) {
    const char* e;
    float v = parse_float(s, &e);
    if (istarts(e, "em") || istarts(e, "rem")) v *= 16;
    return v;
}
int css_media_match(const char* q, int n) {
    if (!q) return 1;
    const char* p = q;
    const char* e = q + n;
    int any = 0;
    while (p < e) {
        const char* st = p;
        int d = 0;
        while (p < e && !(*p == ',' && d == 0)) { if (*p == '(') d++; else if (*p == ')') d--; p++; }
        const char* qe = p;
        if (p < e) p++;
        int neg = 0, ok = 1;
        const char* t = skip_ws_c(st, qe);
        if (istarts(t, "not ")) { neg = 1; t += 4; }
        if (istarts(t, "only ")) t += 5;
        t = skip_ws_c(t, qe);
        if (istarts(t, "print") || istarts(t, "speech") || istarts(t, "tv") || istarts(t, "handheld")) ok = 0;
        const char* r = t;
        while (r < qe) {
            if (*r == '(') {
                const char* fs = r + 1;
                int dd = 1;
                const char* fe = fs;
                while (fe < qe && dd) { if (*fe == '(') dd++; else if (*fe == ')') dd--; if (dd) fe++; }
                char f[160];
                int fl = (int)(fe - fs) < 159 ? (int)(fe - fs) : 159;
                int o = 0;
                for (int i = 0; i < fl; i++) if (!is_ws((unsigned char)fs[i])) f[o++] = (char)lc((unsigned char)fs[i]);
                f[o] = 0;
                char* colon = strchr(f, ':');
                float vw = G.vw, vh = G.vh;
                if (colon) {
                    *colon = 0;
                    const char* v = colon + 1;
                    if (!strcmp(f, "min-width")) ok &= vw >= mq_len(v);
                    else if (!strcmp(f, "max-width")) ok &= vw <= mq_len(v);
                    else if (!strcmp(f, "min-height")) ok &= vh >= mq_len(v);
                    else if (!strcmp(f, "max-height")) ok &= vh <= mq_len(v);
                    else if (!strcmp(f, "orientation")) ok &= !strcmp(v, vw >= vh ? "landscape" : "portrait");
                    else if (!strcmp(f, "prefers-color-scheme")) ok &= !strcmp(v, "light");
                    else if (!strcmp(f, "prefers-reduced-motion")) ok &= !strcmp(v, "no-preference");
                    else if (!strcmp(f, "hover") || !strcmp(f, "any-hover")) ok &= !strcmp(v, "hover");
                    else if (!strcmp(f, "pointer") || !strcmp(f, "any-pointer")) ok &= !strcmp(v, "fine");
                    else if (!strcmp(f, "min-resolution") || !strcmp(f, "-webkit-min-device-pixel-ratio") || !strcmp(f, "min-device-pixel-ratio")) ok &= mq_len(v) <= 1.0f || strstr(v, "dpi") != 0;
                    else if (!strcmp(f, "max-resolution") || !strcmp(f, "-webkit-max-device-pixel-ratio")) ok &= 1;
                    else if (!strcmp(f, "min-device-width")) ok &= vw >= mq_len(v);
                    else if (!strcmp(f, "max-device-width")) ok &= vw <= mq_len(v);
                    else if (!strcmp(f, "display-mode")) ok &= !strcmp(v, "browser");
                    else if (!strcmp(f, "forced-colors")) ok &= !strcmp(v, "none");
                    else if (!strcmp(f, "prefers-contrast")) ok &= !strcmp(v, "no-preference");
                    else ok = 0;
                } else {
                    char* lt = strchr(f, '<');
                    char* gt = strchr(f, '>');
                    if (lt || gt) {
                        int ge = 0;
                        char* opp = lt ? lt : gt;
                        int isgt = gt != 0 && (!lt || gt < lt);
                        if (opp[1] == '=') ge = 1;
                        *opp = 0;
                        const char* rhs = opp + 1 + ge;
                        const char* nm = f;
                        float val;
                        int flip = 0;
                        if (!strcmp(nm, "width") || !strcmp(nm, "height")) val = mq_len(rhs);
                        else { val = mq_len(nm); nm = rhs; flip = 1; }
                        float cur = !strcmp(nm, "height") ? vh : vw;
                        int g = isgt ^ flip;
                        if (g) ok &= ge ? cur >= val : cur > val;
                        else ok &= ge ? cur <= val : cur < val;
                    } else if (!strcmp(f, "color") || !strcmp(f, "hover") || !strcmp(f, "pointer") || !strcmp(f, "grid") == 0) ok &= 1;
                }
                r = fe + 1;
                continue;
            }
            r++;
        }
        if (neg) ok = !ok;
        if (ok) any = 1;
    }
    return any;
}

static void add_rule(csel** sels, int nsel, decl* d, int nd, const char* media, int origin) {
    if (nR >= cR) {
        int nc = cR ? cR * 2 : 256;
        rule** nr = nalloc(sizeof(rule*) * (size_t)nc);
        if (nR) memcpy(nr, R, sizeof(rule*) * (size_t)nR);
        R = nr;
        cR = nc;
    }
    rule* r = nalloc(sizeof(rule));
    r->sel = sels;
    r->nsel = nsel;
    r->d = d;
    r->nd = nd;
    r->media = media;
    r->origin = origin;
    { extern const char* g_parse_scope; r->scope = g_parse_scope; }
    r->cq = g_parse_cq;
    r->layer = g_parse_layer;
    r->lrank = css_layer_rank(r->layer);
    r->order = ++rorder;
    R[nR++] = r;
}

int css_parse_decls(const char* s, const char* e, decl** out) {
    decl tmp[256];
    int n = 0;
    const char* p = s;
    while (p < e) {
        p = skip_ws_c(p, e);
        if (p >= e) break;
        if (*p == ';') { p++; continue; }
        const char* ns = p;
        while (p < e && *p != ':' && *p != ';' && *p != '{' && *p != '}') p++;
        if (p >= e || *p != ':') {
            if (p < e && *p == '{') { p = skip_block(p, e); continue; }
            if (p < e) p++;
            continue;
        }
        const char* ne = p;
        p++;
        const char* vs = p;
        int d = 0;
        while (p < e) {
            if (*p == '"' || *p == '\'') { p = skip_str(p, e); continue; }
            if (*p == '(') d++;
            else if (*p == ')') d--;
            else if (*p == '{') { p = skip_block(p, e); goto nested; }
            else if (*p == ';' && d <= 0) break;
            p++;
        }
        {
            const char* ve = p;
            char* nm = trimdup(ns, ne);
            if (!nm[0] || n >= 256) continue;
            int custom = nm[0] == '-' && nm[1] == '-';
            if (!custom) for (char* q = nm; *q; q++) *q = (char)lc((unsigned char)*q);
            char* v = trimdup(vs, ve);
            int imp = 0;
            int vl = (int)strlen(v);
            char* bang = 0;
            for (int i = vl - 1; i >= 0; i--) if (v[i] == '!') { bang = v + i; break; }
            if (bang) {
                const char* t = bang + 1;
                while (is_ws((unsigned char)*t)) t++;
                if (istarts(t, "important")) { imp = 1; *bang = 0; while (bang > v && is_ws((unsigned char)bang[-1])) *--bang = 0; }
            }
            int pid = css_prop_id(nm);
            if (pid < 0 && !custom) continue;
            tmp[n].prop = (short)(custom ? P_CUSTOM : pid);
            tmp[n].name = custom ? nm : 0;
            tmp[n].val = v;
            tmp[n].imp = (unsigned char)imp;
            n++;
        }
        if (p < e) p++;
        continue;
    nested:;
    }
    *out = nalloc(sizeof(decl) * (size_t)(n ? n : 1));
    memcpy(*out, tmp, sizeof(decl) * (size_t)n);
    return n;
}

static void parse_keyframes(const char* name, const char* p, const char* e) {
    float offs[64];
    decl* ds[64];
    int nds[64];
    int n = 0;
    while (p < e && n < 64) {
        p = skip_ws_c(p, e);
        if (p >= e) break;
        const char* ss = p;
        while (p < e && *p != '{') p++;
        if (p >= e) break;
        const char* se = p;
        const char* be = skip_block(p, e);
        decl* d;
        int nd = css_parse_decls(p + 1, be > p + 1 ? be - 1 : p + 1, &d);
        char* sel = trimdup(ss, se);
        const char* q = sel;
        while (*q && n < 64) {
            while (*q == ' ' || *q == ',' || *q == '\n' || *q == '\t') q++;
            if (!*q) break;
            float off = -1;
            if (istarts(q, "from")) { off = 0; q += 4; }
            else if (istarts(q, "to")) { off = 1; q += 2; }
            else { const char* en; float v = parse_float(q, &en); if (en != q) { off = v / 100.0f; q = en; if (*q == '%') q++; } else q++; }
            while (*q && *q != ',') q++;
            if (off >= 0 && off <= 1) { offs[n] = off; ds[n] = d; nds[n] = nd; n++; }
        }
        p = be;
    }
    extern void kf_add(const char* name, float* offs, decl** ds, int* nds, int n);
    if (n) kf_add(name, offs, ds, nds, n);
}
static void parse_sheet(const char* s, int len, const char* base, const char* media, int origin, int depth);
static void parse_body(const char* p, const char* e, const char* sel, const char* base, const char* media, int origin, int depth);

static char* media_join(const char* a, const char* b) {
    if (!a) return nstrdup(b);
    sbuf x = { 0, 0, 0 };
    sb_puts(&x, a);
    sb_puts(&x, " and ");
    sb_puts(&x, b);
    return sb_str(&x);
}

static void parse_rules(const char* p, const char* e, const char* base, const char* media, int origin, int depth) {
    while (p < e) {
        p = skip_ws_c(p, e);
        if (p >= e) break;
        if (*p == '}' || *p == ';') { p++; continue; }
        if (p + 4 <= e && (!memcmp(p, "<!--", 4) || !memcmp(p, "-->", 3))) { p += (*p == '<') ? 4 : 3; continue; }
        if (*p == '@') {
            const char* st = p;
            p++;
            char* kw = read_ident(&p, e);
            for (char* q = kw; *q; q++) *q = (char)lc((unsigned char)*q);
            const char* ps = p;
            while (p < e && *p != '{' && *p != ';') { if (*p == '"' || *p == '\'') { p = skip_str(p, e); continue; } p++; }
            const char* pe = p;
            if (p >= e) break;
            if (*p == ';') {
                p++;
                if (!strcmp(kw, "layer")) {
                    char* ln = trimdup(ps, pe);
                    for (char* q = ln; *q;) { while (*q == ',' || is_ws((unsigned char)*q)) q++; if (!*q) break; char* st2 = q; while (*q && *q != ',' && !is_ws((unsigned char)*q)) q++; char cch = *q; *q = 0; lay_note(lay_join(g_parse_layer, st2)); *q = cch; }
                    continue;
                }
                if (!strcmp(kw, "import") && depth < 4) {
                    char* pre = trimdup(ps, pe);
                    char url[4096] = "";
                    const char* q = pre;
                    if (istarts(q, "url(")) { q += 4; while (*q == ' ') q++; char qc = (*q == '"' || *q == '\'') ? *q++ : ')'; int k = 0; while (*q && *q != qc && k < 1000) url[k++] = *q++; url[k] = 0; if (*q) q++; if (qc != ')') { while (*q && *q != ')') q++; if (*q) q++; } }
                    else if (*q == '"' || *q == '\'') { char qc = *q++; int k = 0; while (*q && *q != qc && k < 1000) url[k++] = *q++; url[k] = 0; if (*q) q++; }
                    while (*q == ' ') q++;
                    if (url[0] && (!*q || css_media_match(q, (int)strlen(q)) || istarts(q, "layer"))) {
                        char full[4096];
                        url_resolve(base, url, full, sizeof(full));
                        int tl = 0;
                        char* t = eng_fetch_text(full, &tl);
                        if (t) parse_sheet(t, tl, full, media, origin, depth + 1);
                    }
                }
                continue;
            }
            const char* be = skip_block(p, e);
            const char* bs = p + 1;
            const char* bend = be > bs ? be - 1 : bs;
            if (!strcmp(kw, "keyframes") || !strcmp(kw, "-webkit-keyframes") || !strcmp(kw, "-moz-keyframes")) {
                char* nm = trimdup(ps, pe);
                if (nm[0] == '"' || nm[0] == '\'') { nm++; int l = (int)strlen(nm); if (l > 0) nm[l - 1] = 0; }
                parse_keyframes(nm, bs, bend);
            } else if (!strcmp(kw, "font-face")) {
                decl* fd;
                int fn = css_parse_decls(bs, bend, &fd);
                extern void fontface_add(decl* d, int nd, const char* base);
                fontface_add(fd, fn, base);
            } else if (!strcmp(kw, "media")) {
                char* mq = trimdup(ps, pe);
                if (css_media_match(mq, (int)strlen(mq)) || 1) parse_rules(bs, bend, base, media_join(media, mq), origin, depth);
            } else if (!strcmp(kw, "supports")) {
                char* c = trimdup(ps, pe);
                if (!istarts(c, "not") && !strstr(c, "-webkit-touch-callout") && !strstr(c, "-moz-")) parse_rules(bs, bend, base, media, origin, depth);
            } else if (!strcmp(kw, "container")) {
                char* c = trimdup(ps, pe);
                const char* sv = g_parse_cq;
                g_parse_cq = cq_join(sv, c);
                parse_rules(bs, bend, base, media, origin, depth);
                g_parse_cq = sv;
            } else if (!strcmp(kw, "layer")) {
                char* nm = trimdup(ps, pe);
                const char* sv = g_parse_layer;
                char lb[64];
                if (!nm[0]) { snprintf(lb, sizeof lb, "\x01anon%d", ++g_anon_layer); nm = nstrdup(lb); }
                g_parse_layer = lay_join(sv, nm);
                lay_note(g_parse_layer);
                parse_rules(bs, bend, base, media, origin, depth);
                g_parse_layer = sv;
            } else if (!strcmp(kw, "property")) {
                char* nm = trimdup(ps, pe);
                const char* iv = 0;
                for (const char* q = bs; q + 14 <= bend; q++) if (ieqn(q, "initial-value", 13) && (q == bs || q[-1] == ';' || q[-1] == '{' || is_ws((unsigned char)q[-1]))) { const char* c2 = q + 13; while (c2 < bend && is_ws((unsigned char)*c2)) c2++; if (c2 < bend && *c2 == ':') { iv = c2 + 1; break; } }
                if (iv && nm[0] == '-' && nm[1] == '-') { const char* ie = iv; while (ie < bend && *ie != ';' && *ie != '}') ie++; char* v = trimdup(iv, ie); int f = -1; for (int k = 0; k < g_nprop; k++) if (!strcmp(g_prop_n[k], nm)) f = k; if (f < 0 && g_nprop < 128) { f = g_nprop++; g_prop_n[f] = nstrdup(nm); } if (f >= 0) g_prop_v[f] = v; }
            } else if (!strcmp(kw, "document") || !strcmp(kw, "-moz-document") || !strcmp(kw, "scope") || !strcmp(kw, "starting-style") == 0) {
                if (strcmp(kw, "starting-style") && strcmp(kw, "font-face") && strcmp(kw, "keyframes") && strcmp(kw, "-webkit-keyframes") && strcmp(kw, "page") && strcmp(kw, "counter-style") && strcmp(kw, "property") && strcmp(kw, "font-feature-values") && strcmp(kw, "view-transition") && strcmp(kw, "font-palette-values") && strcmp(kw, "-moz-keyframes"))
                    parse_rules(bs, bend, base, media, origin, depth);
            }
            (void)st;
            p = be;
            continue;
        }
        const char* ss = p;
        while (p < e && *p != '{') { if (*p == '"' || *p == '\'') { p = skip_str(p, e); continue; } if (*p == '}') break; p++; }
        if (p >= e) break;
        if (*p == '}') { p++; continue; }
        const char* se = p;
        const char* be = skip_block(p, e);
        csel** sl;
        char* stext = trimdup(ss, se);
        int nested = 0;
        for (const char* q = p + 1; q < be - 1; q++) { if (*q == '"' || *q == '\'') { q = skip_str(q, be) - 1; continue; } if (*q == '{') { nested = 1; break; } }
        if (nested) { parse_body(p + 1, be > p + 1 ? be - 1 : p + 1, stext, base, media, origin, depth); p = be; continue; }
        int ns = parse_compound_list(stext, stext + strlen(stext), &sl, 0);
        if (ns > 0) {
            decl* d;
            int nd = css_parse_decls(p + 1, be > p + 1 ? be - 1 : p + 1, &d);
            if (nd > 0) add_rule(sl, ns, d, nd, media, origin);
        }
        p = be;
    }
}

static char* nest_combine(const char* par, const char* ch) {
    sbuf o = { 0, 0, 0 };
    const char* cp = ch;
    int first = 1;
    while (*cp) {
        const char* cs = cp;
        int d = 0;
        while (*cp && !(*cp == ',' && d == 0)) { if (*cp == '(') d++; else if (*cp == ')') d--; cp++; }
        const char* ce = cp;
        if (*cp == ',') cp++;
        while (cs < ce && is_ws((unsigned char)*cs)) cs++;
        while (ce > cs && is_ws((unsigned char)ce[-1])) ce--;
        if (ce <= cs) continue;
        const char* pp = par;
        while (*pp) {
            const char* ps = pp;
            int d2 = 0;
            while (*pp && !(*pp == ',' && d2 == 0)) { if (*pp == '(') d2++; else if (*pp == ')') d2--; pp++; }
            const char* pe = pp;
            if (*pp == ',') pp++;
            while (ps < pe && is_ws((unsigned char)*ps)) ps++;
            while (pe > ps && is_ws((unsigned char)pe[-1])) pe--;
            if (pe <= ps) continue;
            if (!first) sb_puts(&o, ", ");
            first = 0;
            int amp = 0;
            for (const char* q = cs; q < ce; q++) if (*q == '&') amp = 1;
            if (amp) { for (const char* q = cs; q < ce; q++) { if (*q == '&') sb_putn(&o, ps, (int)(pe - ps)); else sb_putc(&o, *q); } }
            else { sb_putn(&o, ps, (int)(pe - ps)); sb_putc(&o, ' '); sb_putn(&o, cs, (int)(ce - cs)); }
        }
    }
    return sb_str(&o);
}
static void parse_body(const char* p, const char* e, const char* sel, const char* base, const char* media, int origin, int depth) {
    if (depth > 12) return;
    sbuf dec = { 0, 0, 0 };
    while (p < e) {
        p = skip_ws_c(p, e);
        if (p >= e) break;
        if (*p == ';' || *p == '}') { p++; continue; }
        const char* st = p;
        int d = 0;
        while (p < e) { if (*p == '"' || *p == '\'') { p = skip_str(p, e); continue; } if (*p == '(') d++; else if (*p == ')') d--; else if (d == 0 && (*p == ';' || *p == '{' || *p == '}')) break; p++; }
        if (p < e && *p == '{') {
            const char* be = skip_block(p, e);
            const char* bs = p + 1;
            const char* bend = be > bs ? be - 1 : bs;
            char* head = trimdup(st, p);
            if (head[0] == '@') {
                const char* h = head + 1;
                if (istarts(h, "media")) parse_body(bs, bend, sel, base, media_join(media, h + 5), origin, depth + 1);
                else if (istarts(h, "container")) { const char* sv = g_parse_cq; g_parse_cq = cq_join(sv, h + 9); parse_body(bs, bend, sel, base, media, origin, depth + 1); g_parse_cq = sv; }
                else if (istarts(h, "supports") || istarts(h, "layer") || istarts(h, "scope")) parse_body(bs, bend, sel, base, media, origin, depth + 1);
            } else {
                parse_body(bs, bend, nest_combine(sel, head), base, media, origin, depth + 1);
            }
            p = be;
            continue;
        }
        sb_putn(&dec, st, (int)(p - st));
        sb_putc(&dec, ';');
        if (p < e) p++;
    }
    if (dec.n > 0) {
        csel** sl;
        int ns = parse_compound_list(sel, sel + strlen(sel), &sl, 0);
        if (ns > 0) {
            decl* dd;
            int nd = css_parse_decls(dec.s, dec.s + dec.n, &dd);
            if (nd > 0) add_rule(sl, ns, dd, nd, media, origin);
        }
    }
}

static void parse_sheet(const char* s, int len, const char* base, const char* media, int origin, int depth) {
    parse_rules(s, s + len, base, media, origin, depth);
}
void css_add_sheet(const char* src, int len, const char* base, int origin) { parse_sheet(src, len, base, 0, origin, 0); }

typedef struct bent { rule* r; csel* s; } bent;
typedef struct bvec { char* key; bent* v; int n, cap; struct bvec* next; } bvec;
#define BH 2048
static bvec* bid[BH];
static bvec* bcls[BH];
static bvec* btag[BH];
static bvec* battr[BH];
int g_css_sib, g_css_has, g_css_pos;
static int sel_nonlocal(csel* s, int depth) {
    if (depth > 6) return 1;
    for (int i = 0; i < s->nc; i++) {
        if (s->c[i].comb == '+' || s->c[i].comb == '~') g_css_sib = 1;
        for (int j = 0; j < s->c[i].ns; j++) { ssel* x = &s->c[i].s[j]; if (x->kind == SK_PSEUDO && x->tag == PS_HAS) g_css_has = 1; if (x->kind == SK_PSEUDO && ((x->tag >= PS_FIRST && x->tag <= PS_EMPTY))) { int tg = 0; for (int q = 0; q < s->c[i].ns; q++) if (s->c[i].s[q].kind == SK_TAG && s->c[i].s[q].name && !strcmp(s->c[i].s[q].name, "summary")) tg = 1; if (!tg) g_css_pos = 1; } for (int k = 0; k < x->nsub; k++) if (x->sub[k]) sel_nonlocal(x->sub[k], depth + 1); }
    }
    return 0;
}
static bvec buni;
static unsigned khash(const char* s) { unsigned h = 2166136261u; while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; } return h; }
static void bpush(bvec* v, rule* r, csel* s) {
    if (v->n >= v->cap) { int nc = v->cap ? v->cap * 2 : 8; bent* nv = nalloc(sizeof(bent) * (size_t)nc); if (v->n) memcpy(nv, v->v, sizeof(bent) * (size_t)v->n); v->v = nv; v->cap = nc; }
    v->v[v->n].r = r;
    v->v[v->n].s = s;
    v->n++;
}
static bvec* bget(bvec** t, const char* k, int create) {
    unsigned h = khash(k) & (BH - 1);
    for (bvec* v = t[h]; v; v = v->next) if (!strcmp(v->key, k)) return v;
    if (!create) return 0;
    bvec* v = nzalloc(sizeof(bvec));
    v->key = nstrdup(k);
    v->next = t[h];
    t[h] = v;
    return v;
}

static unsigned short g_anc[4096];
int g_anc_on;
int g_css_mgen = 1;
static unsigned anc_hash(char kind, const char* s) { unsigned h = 2166136261u ^ (unsigned char)kind; while (*s) { h ^= (unsigned char)lc((unsigned char)*s++); h *= 16777619u; } return h & 4095; }
static unsigned anc_hash_n(char kind, const char* s, int n) { unsigned h = 2166136261u ^ (unsigned char)kind; for (int i = 0; i < n; i++) { h ^= (unsigned char)lc((unsigned char)s[i]); h *= 16777619u; } return h & 4095; }
static void anc_feat(node* n, int d) {
    if (n->type != NT_ELEM) return;
    if (n->name) { unsigned h = anc_hash('t', n->name); g_anc[h] = (unsigned short)(g_anc[h] + d); }
    const char* id = attr_get(n, "id");
    if (id && *id) { unsigned h = anc_hash('#', id); g_anc[h] = (unsigned short)(g_anc[h] + d); }
    const char* cl = attr_get(n, "class");
    if (cl) {
        const char* p = cl;
        while (*p) {
            while (is_ws((unsigned char)*p)) p++;
            const char* s = p;
            while (*p && !is_ws((unsigned char)*p)) p++;
            if (p > s) { unsigned h = anc_hash_n('.', s, (int)(p - s)); g_anc[h] = (unsigned short)(g_anc[h] + d); }
        }
    }
}
void css_anc_push(node* n) { anc_feat(n, 1); }
void css_anc_pop(node* n) { anc_feat(n, -1); }
void css_anc_reset(void) { memset(g_anc, 0, sizeof g_anc); }
static void sel_hints(csel* s) {
    s->ahdone = 1;
    s->shk = 0;
    { compound* rc = &s->c[s->nc - 1]; for (int k = 0; k < rc->ns; k++) if (rc->s[k].kind == SK_PSEUDO) { if (rc->s[k].a == PS_HOST) s->shk = 1; else if (rc->s[k].a == PS_SLOTTED) s->shk = 2; } }
    s->nah = 0;
    for (int j = s->nc - 2; j >= 0 && s->nah < 4; j--) {
        char cb = s->c[j + 1].comb;
        if (cb != ' ' && cb != '>') continue;
        compound* c = &s->c[j];
        const char* id = 0; const char* cl = 0; const char* tg = 0;
        for (int k = 0; k < c->ns; k++) {
            if (c->s[k].kind == SK_ID) id = c->s[k].name;
            else if (c->s[k].kind == SK_CLASS && !cl) cl = c->s[k].name;
            else if (c->s[k].kind == SK_TAG && c->s[k].name) tg = c->s[k].name;
        }
        if (id) s->ah[s->nah++] = (unsigned short)anc_hash('#', id);
        else if (cl) s->ah[s->nah++] = (unsigned short)anc_hash('.', cl);
        else if (tg && strcmp(tg, "*")) s->ah[s->nah++] = (unsigned short)anc_hash('t', tg);
    }
}

static void index_rules_from(int from) {
    if (from == 0) {
        memset(bid, 0, sizeof(bid));
        memset(bcls, 0, sizeof(bcls));
        memset(btag, 0, sizeof(btag));
        memset(battr, 0, sizeof(battr));
        memset(&buni, 0, sizeof(buni));
        g_css_sib = 0;
        g_css_has = 0;
        g_css_pos = 0;
    }
    for (int i = from; i < nR; i++) {
        rule* r = R[i];
        for (int k = 0; k < r->nsel; k++) {
            csel* s = r->sel[k];
            sel_nonlocal(s, 0);
            compound* c = &s->c[s->nc - 1];
            const char* id = 0;
            const char* cl = 0;
            const char* tg = 0;
            const char* at = 0;
            for (int j = 0; j < c->ns; j++) {
                if (c->s[j].kind == SK_ID) id = c->s[j].name;
                else if (c->s[j].kind == SK_CLASS && !cl) cl = c->s[j].name;
                else if (c->s[j].kind == SK_TAG) tg = c->s[j].name;
                else if (c->s[j].kind == SK_ATTR && !at && c->s[j].name && c->s[j].name[0] && !c->s[j].ci) at = c->s[j].name;
            }
            if (id) bpush(bget(bid, id, 1), r, s);
            else if (cl) bpush(bget(bcls, cl, 1), r, s);
            else if (tg) bpush(bget(btag, tg, 1), r, s);
            else if (at) bpush(bget(battr, at, 1), r, s);
            else bpush(&buni, r, s);
        }
    }
}

typedef struct scache { unsigned h; int len, origin; rule** rules; int nr; void** kf; int nkf; void** ff; int nff; const char** lay; int nlay; const char* outer; } scache;
static scache g_sc[512];
static int g_nsc;
static int g_seq[1024], g_nseq, g_prevseq[1024], g_nprev, g_prev_nR, g_seq_over;
void* kf_head(void);
void kf_relink(void* node);
void* kf_next(void* node);
void* ff_head(void);
void ff_relink(void* node);
void* ff_next(void* node);
static unsigned fnv(const char* s, int n, unsigned h) { for (int i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 16777619u; } return h; }
const char* g_parse_scope;
static void parse_cached(const char* s, int len, const char* base, const char* media, int origin) {
    unsigned h = fnv(s, len, 2166136261u);
    if (g_parse_scope) h = fnv(g_parse_scope, (int)strlen(g_parse_scope), h ^ 0x51ED27u);
    if (base) h = fnv(base, (int)strlen(base), h);
    if (media) h = fnv(media, (int)strlen(media), h ^ 0x9E3779B9u);
    for (int i = 0; i < g_nsc; i++) {
        scache* c = &g_sc[i];
        if (c->h != h || c->len != len || c->origin != origin) continue;
        if ((c->outer == 0) != (g_parse_layer == 0) || (c->outer && strcmp(c->outer, g_parse_layer))) continue;
        for (int k = 0; k < c->nlay; k++) lay_note(c->lay[k]);
        for (int k = 0; k < c->nr; k++) {
            rule* r = c->rules[k];
            if (nR >= cR) { int nc = cR ? cR * 2 : 256; rule** nr2 = nalloc(sizeof(rule*) * (size_t)nc); if (nR) memcpy(nr2, R, sizeof(rule*) * (size_t)nR); R = nr2; cR = nc; }
            r->lrank = css_layer_rank(r->layer);
            r->order = ++rorder;
            R[nR++] = r;
        }
        for (int k = c->nkf - 1; k >= 0; k--) kf_relink(c->kf[k]);
        for (int k = c->nff - 1; k >= 0; k--) ff_relink(c->ff[k]);
        if (g_nseq < 1024) g_seq[g_nseq++] = i; else g_seq_over = 1;
        return;
    }
    int r0 = nR;
    int l0 = g_nlay_seq;
    void* k0 = kf_head();
    void* f0 = ff_head();
    parse_sheet(s, len, base, media, origin, 0);
    if (g_nsc >= 512) { g_seq_over = 1; return; }
    if (g_nseq < 1024) g_seq[g_nseq++] = g_nsc; else g_seq_over = 1;
    scache* c = &g_sc[g_nsc++];
    c->h = h; c->len = len; c->origin = origin;
    c->outer = g_parse_layer;
    c->nlay = g_nlay_seq > l0 ? g_nlay_seq - l0 : 0;
    c->lay = nalloc(sizeof(char*) * (size_t)(c->nlay ? c->nlay : 1));
    for (int k = 0; k < c->nlay; k++) c->lay[k] = g_lay_seq[l0 + k];
    c->nr = nR - r0;
    c->rules = nalloc(sizeof(rule*) * (size_t)(c->nr ? c->nr : 1));
    memcpy(c->rules, R + r0, sizeof(rule*) * (size_t)c->nr);
    int nk = 0, nf = 0;
    for (void* p = kf_head(); p && p != k0; p = kf_next(p)) nk++;
    for (void* p = ff_head(); p && p != f0; p = ff_next(p)) nf++;
    c->kf = nalloc(sizeof(void*) * (size_t)(nk ? nk : 1));
    c->ff = nalloc(sizeof(void*) * (size_t)(nf ? nf : 1));
    c->nkf = 0; c->nff = 0;
    for (void* p = kf_head(); p && p != k0; p = kf_next(p)) c->kf[c->nkf++] = p;
    for (void* p = ff_head(); p && p != f0; p = ff_next(p)) c->ff[c->nff++] = p;
}

extern const char css_ua_text[];
static char* link_text(node* n, int* len) {
    if (n->value) { *len = n->vlen; return n->value; }
    const char* href = attr_get(n, "href");
    if (!href || !*href) return 0;
    char full[4096];
    url_resolve(G.base, href, full, sizeof(full));
    int tl = 0;
    char* t = eng_fetch_text(full, &tl);
    if (!t) { n->value = nstrdup(""); n->vlen = 0; return 0; }
    n->value = t;
    n->vlen = tl;
    *len = tl;
    return t;
}
static void pl_walk(node* n, const char** list, char bufs[][1024], int* m) {
    for (node* c = n->first; c && *m < 8; c = c->next) {
        if (c->type != NT_ELEM) continue;
        if (c->tag == T_LINK && !c->value) {
            const char* rel = attr_get(c, "rel"); const char* href = attr_get(c, "href");
            if (rel && href && *href && strstr(rel, "stylesheet") && !strstr(rel, "alternate") && !attr_get(c, "disabled")) {
                url_resolve(G.base, href, bufs[*m], 1024);
                if (istarts(bufs[*m], "http")) { int dup = 0; for (int k = 0; k < *m; k++) if (!strcmp(list[k], bufs[*m])) dup = 1; if (!dup) { list[*m] = bufs[*m]; (*m)++; } }
            }
        }
        if (c->first) pl_walk(c, list, bufs, m);
    }
}
static void prefetch_links(node* root) {
    static char bufs[8][1024];
    const char* list[8];
    int m = 0;
    pl_walk(root, list, bufs, &m);
    if (m >= 2) nwk_prefetch_more(list, m, G.url);
}
static void collect(node* n) {
    for (node* c = n->first; c; c = c->next) {
        if (c->type != NT_ELEM) continue;
        if (c->tag == T_STYLE) {
            const char* ty = attr_get(c, "type");
            if (ty && *ty && !istarts(ty, "text/css")) continue;
            sbuf b = { 0, 0, 0 };
            text_content(c, &b);
            const char* m = attr_get(c, "media");
            if (b.n) parse_cached(b.s, b.n, G.base, (m && *m) ? nstrdup(m) : 0, 2);
            continue;
        }
        if (c->tag == T_LINK) {
            const char* rel = attr_get(c, "rel");
            if (!rel) continue;
            int ss = 0;
            const char* p = rel;
            while (*p) { while (is_ws((unsigned char)*p)) p++; if (istarts(p, "stylesheet")) ss = 1; if (istarts(p, "alternate")) { ss = 0; break; } while (*p && !is_ws((unsigned char)*p)) p++; }
            if (!ss || attr_get(c, "disabled")) continue;
            int len = 0;
            char* t = link_text(c, &len);
            const char* m = attr_get(c, "media");
            char full[4096];
            url_resolve(G.base, attr_get(c, "href"), full, sizeof(full));
            if (t && len) parse_cached(t, len, full, (m && *m && strcmp(m, "all")) ? nstrdup(m) : 0, 2);
            continue;
        }
        if (c->tag == T_TEMPLATE) continue;
        if (c->tag == T_SVG) { node* stk[256]; int sp = 0; for (node* d = c->last; d && sp < 256; d = d->prev) stk[sp++] = d; while (sp) { node* d = stk[--sp]; if (d->type != NT_ELEM) continue; if (d->tag == T_STYLE) { sbuf b = { 0, 0, 0 }; text_content(d, &b); if (b.n) parse_cached(b.s, b.n, G.base, 0, 2); continue; } for (node* e = d->last; e && sp < 256; e = e->prev) stk[sp++] = e; } continue; }
        if (c->shadow) { const char* sv = g_parse_scope; g_parse_scope = c->name ? c->name : "?"; if (c->shadow->value && c->shadow->vlen > 0) parse_cached(c->shadow->value, c->shadow->vlen, G.base, 0, 2); collect(c->shadow); g_parse_scope = sv; }
        collect(c);
    }
}

void css_reset_all(void) {
    g_nsc = 0;
    g_nprev = 0;
    g_prev_nR = 0;
    R = 0;
    nR = cR = 0;
    rorder = 0;
    g_nlay = 0; g_nlay_seq = 0; g_parse_layer = 0; g_anon_layer = 0; g_nprop = 0;
    built_gen = 0;
    g_css_gen++;
    g_css_hover = 0;
    memset(bid, 0, sizeof(bid));
    memset(bcls, 0, sizeof(bcls));
    memset(btag, 0, sizeof(btag));
    memset(battr, 0, sizeof(battr));
    memset(&buni, 0, sizeof(buni));
}

int css_needs_rebuild(void) { return built_gen != g_css_gen; }
void css_rebuild_if_needed(void) {
    if (built_gen == g_css_gen) return;
    { extern void kf_reset(void); extern void fontface_reset(void); kf_reset(); fontface_reset(); }
    nR = 0;
    rorder = 0;
    g_nlay = 0; g_nlay_seq = 0; g_parse_layer = 0;
    g_nseq = 0;
    g_seq_over = 0;
    parse_cached(css_ua_text, (int)strlen(css_ua_text), "about:ua", 0, 0);
    if (G.doc) prefetch_links(G.doc);
    if (G.doc) collect(G.doc);
    int inc = !g_seq_over && g_nprev > 0 && g_nseq >= g_nprev && g_prev_nR <= nR && !memcmp(g_seq, g_prevseq, sizeof(int) * (size_t)g_nprev);
    index_rules_from(inc ? g_prev_nR : 0);
    memcpy(g_prevseq, g_seq, sizeof(int) * (size_t)g_nseq);
    g_nprev = g_seq_over ? 0 : g_nseq;
    g_prev_nR = nR;
    built_gen = g_css_gen;
}

static node* g_cur;
int css_gather(node* n, ment* out, int cap, int pseudo) {
    int cnt = 0;
    const char* nscope = 0;
    { node* t = n; for (int g = 0; t->parent && g < 4096; g++) t = t->parent; if (t->host) nscope = t->host->name ? t->host->name : "?"; }
    g_cur = n;
    bvec* lists[64];
    int nl = 0;
    const char* id = attr_get(n, "id");
    if (id) { bvec* v = bget(bid, id, 0); if (v) lists[nl++] = v; }
    const char* cls = attr_get(n, "class");
    if (cls) {
        const char* p = cls;
        char tmp[128];
        while (*p && nl < 60) {
            while (is_ws((unsigned char)*p)) p++;
            const char* s = p;
            while (*p && !is_ws((unsigned char)*p)) p++;
            int l = (int)(p - s);
            if (l <= 0 || l > 127) continue;
            memcpy(tmp, s, (size_t)l);
            tmp[l] = 0;
            bvec* v = bget(bcls, tmp, 0);
            if (v) { int dup = 0; for (int k = 0; k < nl; k++) if (lists[k] == v) dup = 1; if (!dup) lists[nl++] = v; }
        }
    }
    if (n->name) { bvec* v = bget(btag, n->name, 0); if (v) lists[nl++] = v; }
    for (int ai = 0; ai < n->nattrs && nl < 62; ai++) { char lk[64]; const char* an = n->attrs[ai].name; int q = 0; for (; an[q] && q < 63; q++) lk[q] = (char)lc((unsigned char)an[q]); lk[q] = 0; if (an[q]) continue; bvec* v = bget(battr, lk, 0); if (v) { int dup = 0; for (int k = 0; k < nl; k++) if (lists[k] == v) dup = 1; if (!dup) lists[nl++] = v; } }
    lists[nl++] = &buni;
    for (int li = 0; li < nl; li++) {
        bvec* v = lists[li];
        for (int i = 0; i < v->n; i++) {
            csel* s = v->v[i].s;
            if (s->pseudo != pseudo) continue;
            if (!s->ahdone) sel_hints(s);
            if (s->nah && g_anc_on) { int ok2 = 1; for (int q = 0; q < s->nah; q++) if (!g_anc[s->ah[q]]) { ok2 = 0; break; } if (!ok2) continue; }
            rule* r = v->v[i].r;
            if (r->origin != 0) {
                if (r->scope) {
                    int okc = 0;
                    if (s->shk == 1) okc = n->shadow && n->name && !strcmp(n->name, r->scope);
                    else if (s->shk == 2) okc = n->parent && n->parent->shadow && n->parent->name && !strcmp(n->parent->name, r->scope);
                    else okc = nscope && !strcmp(nscope, r->scope);
                    if (!okc) continue;
                } else if (nscope) continue;
            }
            if (r->cq && !css_cq_match(r->cq, n)) continue;
            if (r->media) { if (r->mgen != g_css_mgen) { r->mgen = g_css_mgen; r->mok = css_media_match(r->media, (int)strlen(r->media)); } if (!r->mok) continue; }
            if (!css_match_sel(s, n)) continue;
            int dupe = 0;
            for (int k = 0; k < cnt; k++) if (out[k].r == r) { if (s->spec > out[k].spec) out[k].spec = s->spec; dupe = 1; break; }
            if (dupe) continue;
            if (cnt < cap) { out[cnt].r = r; out[cnt].spec = s->spec; cnt++; }
        }
    }
    return cnt;
}
