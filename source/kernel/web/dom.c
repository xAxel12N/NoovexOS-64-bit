#include "web/nwe.h"

void eng_script_parsed(node* s);

const char* tag_names[] = {
    "", "html", "head", "body", "title", "meta", "link", "style", "script", "noscript", "base",
    "div", "span", "p", "a", "img", "br", "hr", "h1", "h2", "h3", "h4", "h5", "h6",
    "ul", "ol", "li", "dl", "dt", "dd", "table", "thead", "tbody", "tfoot", "tr", "td", "th", "caption", "col", "colgroup",
    "form", "input", "button", "select", "option", "optgroup", "textarea", "label", "fieldset", "legend",
    "b", "strong", "i", "em", "u", "s", "strike", "del", "ins", "small", "big", "sub", "sup", "code", "pre", "kbd", "samp", "tt", "var",
    "blockquote", "q", "cite", "abbr", "mark", "time", "address", "center", "font",
    "section", "article", "aside", "nav", "header", "footer", "main", "figure", "figcaption", "details", "summary",
    "svg", "canvas", "video", "audio", "iframe", "object", "embed", "source", "picture", "template", "area", "map",
    "wbr", "track", "param", "dialog", "progress", "meter", "output", "data", "nobr", "menu", "hgroup", "search",
    "path", "circle", "rect", "g", "polygon", "line", "polyline", "ellipse", "use",
};

typedef struct thash { const char* k; int v; struct thash* next; } thash;
static thash* tht[256];
static thash thpool[T_COUNT];
static int th_ready;
static unsigned shash(const char* s) { unsigned h = 5381; while (*s) h = h * 33 + (unsigned char)lc((unsigned char)*s++); return h; }
int tag_id(const char* name) {
    if (!th_ready) {
        for (int i = 1; i < T_COUNT; i++) { unsigned h = shash(tag_names[i]) & 255; thpool[i].k = tag_names[i]; thpool[i].v = i; thpool[i].next = tht[h]; tht[h] = &thpool[i]; }
        th_ready = 1;
    }
    for (thash* t = tht[shash(name) & 255]; t; t = t->next) if (ieq(t->k, name)) return t->v;
    return T_UNKNOWN;
}

node* node_new(int type) {
    node* n = nzalloc(sizeof(node));
    n->type = type;
    n->uid = ++G.uidc;
    n->selidx = -1;
    G.nodecount++;
    return n;
}
node* elem_new(const char* name) {
    node* n = node_new(NT_ELEM);
    n->name = nstrdup(name);
    for (char* p = n->name; *p; p++) *p = (char)lc((unsigned char)*p);
    n->tag = tag_id(n->name);
    return n;
}
node* text_new(const char* s, int len) {
    node* n = node_new(NT_TEXT);
    n->text = nstrndup(s, len);
    n->tlen = len;
    return n;
}
int g_mut_on;
int g_dom_parsing;
void (*g_mut_hook)(int, node*, node*, node*, node*, const char*, const char*);
static void mut(int k, node* t, node* c, node* pv, node* nx, const char* nm, const char* old) { if (g_mut_on && g_mut_hook && !g_dom_parsing) g_mut_hook(k, t, c, pv, nx, nm, old); }
void dom_mut_text(node* t, const char* old) { mut(4, t, 0, 0, 0, 0, old); }
void node_remove(node* c) {
    node* p = c->parent;
    if (!p) return;
    node* pv = c->prev;
    node* nx = c->next;
    if (c->prev) c->prev->next = c->next; else p->first = c->next;
    if (c->next) c->next->prev = c->prev; else p->last = c->prev;
    c->parent = c->next = c->prev = 0;
    mut(2, p, c, pv, nx, 0, 0);
}
void node_insert_before(node* p, node* c, node* ref) {
    if (c->type == NT_FRAG) {
        while (c->first) { node* k = c->first; node_remove(k); node_insert_before(p, k, ref); }
        return;
    }
    if (c->parent) node_remove(c);
    if (!ref || ref->parent != p) {
        c->parent = p;
        c->prev = p->last;
        c->next = 0;
        if (p->last) p->last->next = c; else p->first = c;
        p->last = c;
        mut(1, p, c, c->prev, 0, 0, 0);
        return;
    }
    c->parent = p;
    c->next = ref;
    c->prev = ref->prev;
    if (ref->prev) ref->prev->next = c; else p->first = c;
    ref->prev = c;
    mut(1, p, c, c->prev, ref, 0, 0);
}
void node_append(node* p, node* c) { node_insert_before(p, c, 0); }

const char* attr_get(node* n, const char* name) {
    if (!n || n->type != NT_ELEM) return 0;
    for (int i = 0; i < n->nattrs; i++) if (ieq(n->attrs[i].name, name)) return n->attrs[i].val;
    return 0;
}
void attr_set(node* n, const char* name, const char* val) {
    if (g_mut_on && !g_dom_parsing) { const char* ov = attr_get(n, name); mut(3, n, 0, 0, 0, name, ov); }
    for (int i = 0; i < n->nattrs; i++) if (ieq(n->attrs[i].name, name)) { n->attrs[i].val = nstrdup(val); goto done; }
    if (n->nattrs >= n->cattrs) {
        int nc = n->cattrs ? n->cattrs * 2 : 4;
        nattr* na = nalloc(sizeof(nattr) * (size_t)nc);
        if (n->nattrs) memcpy(na, n->attrs, sizeof(nattr) * (size_t)n->nattrs);
        n->attrs = na;
        n->cattrs = nc;
    }
    n->attrs[n->nattrs].name = nstrdup(name);
    for (char* p = n->attrs[n->nattrs].name; *p; p++) *p = (char)lc((unsigned char)*p);
    n->attrs[n->nattrs].val = nstrdup(val);
    n->nattrs++;
done:
    if (ieq(name, "style")) { n->idecl = 0; n->idecl_src = 0; }
    if (ieq(name, "value") && (n->tag == T_INPUT) && !n->dirtyval) { n->value = 0; }
    if (ieq(name, "checked") && !(n->flags & NF_INLINEDIRTY)) n->checked = 1;
}
void attr_del(node* n, const char* name) {
    if (g_mut_on && !g_dom_parsing && attr_get(n, name)) mut(3, n, 0, 0, 0, name, attr_get(n, name));
    for (int i = 0; i < n->nattrs; i++) if (ieq(n->attrs[i].name, name)) {
        for (int j = i; j < n->nattrs - 1; j++) n->attrs[j] = n->attrs[j + 1];
        n->nattrs--;
        if (ieq(name, "style")) { n->idecl = 0; n->idecl_src = 0; }
        if (ieq(name, "checked") && !(n->flags & NF_INLINEDIRTY)) n->checked = 0;
        return;
    }
}
int has_class(node* n, const char* cls) {
    const char* c = attr_get(n, "class");
    if (!c) return 0;
    int l = (int)strlen(cls);
    const char* p = c;
    while (*p) {
        while (is_ws((unsigned char)*p)) p++;
        const char* s = p;
        while (*p && !is_ws((unsigned char)*p)) p++;
        if (p - s == l && !memcmp(s, cls, (size_t)l)) return 1;
    }
    return 0;
}
static node* child_tag(node* p, int tag) { for (node* c = p ? p->first : 0; c; c = c->next) if (c->type == NT_ELEM && c->tag == tag) return c; return 0; }
node* doc_elem(void) { return child_tag(G.doc, T_HTML); }
node* doc_head(void) { return child_tag(doc_elem(), T_HEAD); }
node* doc_body(void) { return child_tag(doc_elem(), T_BODY); }
node* find_id(node* root, const char* id) {
    for (node* c = root ? root->first : 0; c; c = c->next) {
        if (c->type != NT_ELEM) continue;
        const char* v = attr_get(c, "id");
        if (v && !strcmp(v, id)) return c;
        node* r = find_id(c, id);
        if (r) return r;
    }
    return 0;
}
int node_contains(node* a, node* b) { while (b) { if (b == a) return 1; b = b->parent; } return 0; }

void text_content(node* n, sbuf* b) {
    if (n->type == NT_TEXT) { sb_putn(b, n->text, n->tlen); return; }
    if (n->type == NT_COMMENT) return;
    for (node* c = n->first; c; c = c->next) text_content(c, b);
}

static int is_void(int t) {
    return t == T_AREA || t == T_BASE || t == T_BR || t == T_COL || t == T_EMBED || t == T_HR || t == T_IMG || t == T_INPUT || t == T_LINK || t == T_META || t == T_SOURCE || t == T_TRACK || t == T_WBR || t == T_PARAM;
}
static int is_raw(int t) { return t == T_SCRIPT || t == T_STYLE || t == T_TEXTAREA || t == T_TITLE || t == T_NOSCRIPT || t == T_IFRAME; }

static void esc(sbuf* b, const char* s, int n, int attr) {
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c == '&') sb_puts(b, "&amp;");
        else if (c == '<' && !attr) sb_puts(b, "&lt;");
        else if (c == '>' && !attr) sb_puts(b, "&gt;");
        else if (c == '"' && attr) sb_puts(b, "&quot;");
        else if ((unsigned char)c == 0xC2 && i + 1 < n && (unsigned char)s[i + 1] == 0xA0) { sb_puts(b, "&nbsp;"); i++; }
        else sb_putc(b, c);
    }
}
void serialize(node* n, sbuf* b, int outer) {
    if (n->type == NT_TEXT) {
        if (n->parent && n->parent->type == NT_ELEM && is_raw(n->parent->tag) && n->parent->tag != T_TEXTAREA && n->parent->tag != T_TITLE) sb_putn(b, n->text, n->tlen);
        else esc(b, n->text, n->tlen, 0);
        return;
    }
    if (n->type == NT_COMMENT) { sb_puts(b, "<!--"); sb_putn(b, n->text, n->tlen); sb_puts(b, "-->"); return; }
    if (outer && n->type == NT_ELEM) {
        sb_putc(b, '<');
        sb_puts(b, n->name);
        for (int i = 0; i < n->nattrs; i++) {
            sb_putc(b, ' ');
            sb_puts(b, n->attrs[i].name);
            sb_puts(b, "=\"");
            esc(b, n->attrs[i].val, (int)strlen(n->attrs[i].val), 1);
            sb_putc(b, '"');
        }
        sb_putc(b, '>');
        if (is_void(n->tag)) return;
    }
    for (node* c = n->first; c; c = c->next) serialize(c, b, 1);
    if (outer && n->type == NT_ELEM) { sb_puts(b, "</"); sb_puts(b, n->name); sb_putc(b, '>'); }
}

node* clone_node(node* n, int deep) {
    node* r = node_new(n->type);
    r->tag = n->tag;
    if (n->name) r->name = nstrdup(n->name);
    if (n->text) { r->text = nstrndup(n->text, n->tlen); r->tlen = n->tlen; }
    for (int i = 0; i < n->nattrs; i++) attr_set(r, n->attrs[i].name, n->attrs[i].val);
    r->checked = n->checked;
    if (n->value) { r->value = nstrndup(n->value, n->vlen); r->vlen = n->vlen; r->vcap = n->vlen + 1; r->dirtyval = n->dirtyval; }
    if (deep) for (node* c = n->first; c; c = c->next) node_append(r, clone_node(c, 1));
    return r;
}

static const char* ent_l1[96] = {
    "nbsp", "iexcl", "cent", "pound", "curren", "yen", "brvbar", "sect", "uml", "copy", "ordf", "laquo", "not", "shy", "reg", "macr",
    "deg", "plusmn", "sup2", "sup3", "acute", "micro", "para", "middot", "cedil", "sup1", "ordm", "raquo", "frac14", "frac12", "frac34", "iquest",
    "Agrave", "Aacute", "Acirc", "Atilde", "Auml", "Aring", "AElig", "Ccedil", "Egrave", "Eacute", "Ecirc", "Euml", "Igrave", "Iacute", "Icirc", "Iuml",
    "ETH", "Ntilde", "Ograve", "Oacute", "Ocirc", "Otilde", "Ouml", "times", "Oslash", "Ugrave", "Uacute", "Ucirc", "Uuml", "Yacute", "THORN", "szlig",
    "agrave", "aacute", "acirc", "atilde", "auml", "aring", "aelig", "ccedil", "egrave", "eacute", "ecirc", "euml", "igrave", "iacute", "icirc", "iuml",
    "eth", "ntilde", "ograve", "oacute", "ocirc", "otilde", "ouml", "divide", "oslash", "ugrave", "uacute", "ucirc", "uuml", "yacute", "thorn", "yuml" };
static const struct { const char* n; int cp; } ents[] = {
    {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"hellip", 0x2026}, {"mdash", 0x2014}, {"ndash", 0x2013},
    {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"sbquo", 0x201A}, {"bdquo", 0x201E}, {"bull", 0x2022},
    {"trade", 0x2122}, {"euro", 0x20AC}, {"larr", 0x2190}, {"uarr", 0x2191}, {"rarr", 0x2192}, {"darr", 0x2193}, {"harr", 0x2194},
    {"le", 0x2264}, {"ge", 0x2265}, {"ne", 0x2260}, {"infin", 0x221E}, {"minus", 0x2212}, {"asymp", 0x2248}, {"prime", 0x2032}, {"Prime", 0x2033},
    {"dagger", 0x2020}, {"Dagger", 0x2021}, {"permil", 0x2030}, {"lsaquo", 0x2039}, {"rsaquo", 0x203A}, {"thinsp", 0x2009}, {"ensp", 0x2002},
    {"emsp", 0x2003}, {"zwj", 0x200D}, {"zwnj", 0x200C}, {"lrm", 0x200E}, {"rlm", 0x200F}, {"alpha", 0x3B1}, {"beta", 0x3B2}, {"gamma", 0x3B3},
    {"delta", 0x3B4}, {"lambda", 0x3BB}, {"mu", 0x3BC}, {"pi", 0x3C0}, {"sigma", 0x3C3}, {"Delta", 0x394}, {"Sigma", 0x3A3}, {"Omega", 0x3A9},
    {"hearts", 0x2665}, {"spades", 0x2660}, {"clubs", 0x2663}, {"diams", 0x2666}, {"check", 0x2713}, {"star", 0x2606}, {"starf", 0x2605},
    {"oline", 0x203E}, {"frasl", 0x2044}, {"loz", 0x25CA}, {"nbsp", 0xA0}, {"NewLine", '\n'}, {"Tab", '\t'}, {"colon", ':'}, {"comma", ','},
    {"period", '.'}, {"excl", '!'}, {"quest", '?'}, {"num", '#'}, {"dollar", '$'}, {"percnt", '%'}, {"lpar", '('}, {"rpar", ')'}, {"ast", '*'},
    {"plus", '+'}, {"sol", '/'}, {"semi", ';'}, {"equals", '='}, {"commat", '@'}, {"lsqb", '['}, {"rsqb", ']'}, {"lowbar", '_'}, {"lcub", '{'},
    {"rcub", '}'}, {"verbar", '|'}, {"vert", '|'}, {"bsol", '\\'}, {"Hat", '^'}, {"grave", '`'}, {"OElig", 0x152}, {"oelig", 0x153}, {"Scaron", 0x160},
    {"scaron", 0x161}, {"Yuml", 0x178}, {"fnof", 0x192}, {"circ", 0x2C6}, {"tilde", 0x2DC}, {"rarrow", 0x2192}, {"times", 0xD7}, {"middot", 0xB7} };

int decode_entity(const char* s, int n, int* used) {
    if (n < 2 || s[0] != '&') return -1;
    if (s[1] == '#') {
        int i = 2, v = 0, hex = 0, any = 0;
        if (i < n && (s[i] == 'x' || s[i] == 'X')) { hex = 1; i++; }
        while (i < n && i < 12) {
            char c = s[i];
            int d = (c >= '0' && c <= '9') ? c - '0' : (hex && c >= 'a' && c <= 'f') ? c - 'a' + 10 : (hex && c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (d < 0) break;
            v = v * (hex ? 16 : 10) + d;
            i++;
            any = 1;
        }
        if (!any) return -1;
        if (i < n && s[i] == ';') i++;
        *used = i;
        if (v == 0 || v > 0x10FFFF) v = 0xFFFD;
        if (v >= 0x80 && v < 0xA0) { static const int w[32] = { 0x20AC, 0x81, 0x201A, 0x192, 0x201E, 0x2026, 0x2020, 0x2021, 0x2C6, 0x2030, 0x160, 0x2039, 0x152, 0x8D, 0x17D, 0x8F, 0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x2DC, 0x2122, 0x161, 0x203A, 0x153, 0x9D, 0x17E, 0x178 }; v = w[v - 0x80]; }
        return v;
    }
    int e = 1;
    while (e < n && e < 34 && ((s[e] >= 'a' && s[e] <= 'z') || (s[e] >= 'A' && s[e] <= 'Z') || (s[e] >= '0' && s[e] <= '9'))) e++;
    int nl = e - 1;
    if (nl <= 0) return -1;
    for (int k = nl; k >= 2; k--) {
        int semi = (k == nl && e < n && s[e] == ';');
        for (int i = 0; i < 96; i++) if ((int)strlen(ent_l1[i]) == k && !memcmp(ent_l1[i], s + 1, (size_t)k)) { *used = 1 + k + semi; return 0xA0 + i; }
        for (size_t i = 0; i < sizeof(ents) / sizeof(ents[0]); i++) if ((int)strlen(ents[i].n) == k && !memcmp(ents[i].n, s + 1, (size_t)k)) { if (!semi && k != nl) continue; *used = 1 + k + semi; return ents[i].cp; }
        if (k == nl && !semi) continue;
        break;
    }
    return -1;
}

static void decode_text(const char* s, int n, sbuf* out) {
    for (int i = 0; i < n;) {
        if (s[i] == '&') {
            int used = 0;
            int cp = decode_entity(s + i, n - i, &used);
            if (cp >= 0) { sb_putu8(out, cp); i += used; continue; }
        }
        if (s[i] == '\r') { sb_putc(out, '\n'); i++; if (i < n && s[i] == '\n') i++; continue; }
        if (s[i] == 0) { i++; continue; }
        sb_putc(out, s[i++]);
    }
}

typedef struct {
    node* stack[512];
    int sp;
    node* doc;
    node* html;
    node* head;
    node* body;
    int run_scripts;
    int frag;
    int in_svg;
} pstate;

static node* cur(pstate* P) { return P->stack[P->sp - 1]; }
static void push(pstate* P, node* n) { if (P->sp < 511) P->stack[P->sp++] = n; }
static void ensure_html(pstate* P) {
    if (P->frag) return;
    if (!P->html) { P->html = elem_new("html"); node_append(P->doc, P->html); P->sp = 1; push(P, P->html); }
}
static void ensure_head(pstate* P) {
    if (P->frag) return;
    ensure_html(P);
    if (!P->head) { P->head = elem_new("head"); node_insert_before(P->html, P->head, P->html->first); }
}
static void ensure_body(pstate* P) {
    if (P->frag) return;
    ensure_head(P);
    if (!P->body) {
        P->body = elem_new("body");
        node_append(P->html, P->body);
        while (P->sp > 2) P->sp--;
        push(P, P->body);
    }
}
static int in_stack(pstate* P, int tag, int stopset) {
    for (int i = P->sp - 1; i > 0; i--) {
        node* n = P->stack[i];
        if (n->tag == tag) return i;
        if (stopset == 1 && (n->tag == T_TABLE || n->tag == T_TD || n->tag == T_TH || n->tag == T_HTML || n->tag == T_BUTTON || n->tag == T_CAPTION)) return -1;
        if (stopset == 2 && (n->tag == T_UL || n->tag == T_OL || n->tag == T_TABLE)) return -1;
        if (stopset == 3 && (n->tag == T_TABLE)) return -1;
        if (stopset == 4 && (n->tag == T_TR || n->tag == T_TABLE)) return -1;
        if (stopset == 5 && (n->tag == T_DL || n->tag == T_TABLE)) return -1;
        if (stopset == 6 && (n->tag == T_SELECT)) return -1;
    }
    return -1;
}
static void pop_to(pstate* P, int idx) { if (idx > 0) P->sp = idx; }
static void close_tag(pstate* P, int tag, int stop) { int i = in_stack(P, tag, stop); if (i > 0) pop_to(P, i); }

static int closes_p(int t) {
    switch (t) {
    case T_ADDRESS: case T_ARTICLE: case T_ASIDE: case T_BLOCKQUOTE: case T_DETAILS: case T_DIV: case T_DL: case T_FIELDSET: case T_FIGCAPTION:
    case T_FIGURE: case T_FOOTER: case T_FORM: case T_H1: case T_H2: case T_H3: case T_H4: case T_H5: case T_H6: case T_HEADER: case T_HGROUP:
    case T_HR: case T_MAIN: case T_MENU: case T_NAV: case T_OL: case T_P: case T_PRE: case T_SECTION: case T_TABLE: case T_UL: case T_LI: case T_DD:
    case T_DT: case T_CENTER: case T_SEARCH: case T_DIALOG:
        return 1;
    }
    return 0;
}
static int head_tag(int t) { return t == T_META || t == T_LINK || t == T_STYLE || t == T_SCRIPT || t == T_TITLE || t == T_BASE || t == T_NOSCRIPT; }

static void insert_text(pstate* P, const char* s, int n) {
    if (n <= 0) return;
    if (!P->frag && !P->body) {
        int allws = 1;
        for (int i = 0; i < n; i++) if (!is_ws((unsigned char)s[i])) { allws = 0; break; }
        if (allws) return;
        ensure_body(P);
    }
    node* p = cur(P);
    if (p->tag == T_TABLE || p->tag == T_TBODY || p->tag == T_THEAD || p->tag == T_TFOOT || p->tag == T_TR) {
        int allws = 1;
        for (int i = 0; i < n; i++) if (!is_ws((unsigned char)s[i])) { allws = 0; break; }
        if (allws) return;
        int ti = in_stack(P, T_TABLE, 0);
        if (ti > 0 && P->stack[ti]->parent) { node* t = P->stack[ti]; node* tn = text_new(s, n); node_insert_before(t->parent, tn, t); return; }
    }
    if (p->last && p->last->type == NT_TEXT) {
        node* t = p->last;
        char* ns = nalloc((size_t)(t->tlen + n + 1));
        memcpy(ns, t->text, (size_t)t->tlen);
        memcpy(ns + t->tlen, s, (size_t)n);
        ns[t->tlen + n] = 0;
        t->text = ns;
        t->tlen += n;
        return;
    }
    node_append(p, text_new(s, n));
}

static void parse_run(pstate* P, const char* src, int len);

static void handle_start(pstate* P, node* e, int selfclose) {
    int t = e->tag;
    if (P->frag) goto generic;
    if (t == T_HTML) {
        ensure_html(P);
        for (int i = 0; i < e->nattrs; i++) if (!attr_get(P->html, e->attrs[i].name)) attr_set(P->html, e->attrs[i].name, e->attrs[i].val);
        return;
    }
    if (t == T_HEAD) { ensure_head(P); if (!P->body) { while (P->sp > 2) P->sp--; push(P, P->head); } return; }
    if (t == T_BODY) {
        if (P->body) { for (int i = 0; i < e->nattrs; i++) if (!attr_get(P->body, e->attrs[i].name)) attr_set(P->body, e->attrs[i].name, e->attrs[i].val); return; }
        ensure_head(P);
        P->body = e;
        node_append(P->html, e);
        while (P->sp > 2) P->sp--;
        push(P, e);
        return;
    }
    if (!P->body) {
        ensure_head(P);
        if (head_tag(t)) {
            node* h = P->head;
            node_append(h, e);
            if (!is_void(t) && !selfclose) push(P, e);
            return;
        }
        ensure_body(P);
    }
generic:
    if (P->in_svg && selfclose) { node_append(cur(P), e); return; }
    if (closes_p(t)) close_tag(P, T_P, 1);
    if (t >= T_H1 && t <= T_H6) { node* c = cur(P); if (c->tag >= T_H1 && c->tag <= T_H6) P->sp--; }
    if (t == T_LI) close_tag(P, T_LI, 2);
    if (t == T_DT || t == T_DD) { close_tag(P, T_DT, 5); close_tag(P, T_DD, 5); }
    if (t == T_OPTION) { if (cur(P)->tag == T_OPTION) P->sp--; }
    if (t == T_OPTGROUP) { if (cur(P)->tag == T_OPTION) P->sp--; if (cur(P)->tag == T_OPTGROUP) P->sp--; }
    if (t == T_A) close_tag(P, T_A, 1);
    if (t == T_BUTTON) close_tag(P, T_BUTTON, 3);
    if (t == T_FORM && in_stack(P, T_FORM, 0) > 0) return;
    if (t == T_TD || t == T_TH) {
        int i = in_stack(P, T_TD, 4); if (i < 0) i = in_stack(P, T_TH, 4);
        if (i > 0) pop_to(P, i);
        node* c = cur(P);
        if (c->tag == T_TABLE || c->tag == T_TBODY || c->tag == T_THEAD || c->tag == T_TFOOT) {
            if (c->tag == T_TABLE) { node* tb = elem_new("tbody"); node_append(c, tb); push(P, tb); }
            node* tr = elem_new("tr");
            node_append(cur(P), tr);
            push(P, tr);
        }
    }
    if (t == T_TR) {
        int i = in_stack(P, T_TR, 3);
        if (i > 0) pop_to(P, i);
        node* c = cur(P);
        if (c->tag == T_TD || c->tag == T_TH) { int k = in_stack(P, T_TR, 3); if (k > 0) pop_to(P, k); c = cur(P); }
        if (c->tag == T_TABLE) { node* tb = elem_new("tbody"); node_append(c, tb); push(P, tb); }
    }
    if (t == T_TBODY || t == T_THEAD || t == T_TFOOT) {
        int ti = in_stack(P, T_TABLE, 0);
        if (ti > 0) pop_to(P, ti + 1);
    }
    if (t == T_COL) { node* c = cur(P); if (c->tag == T_TBODY || c->tag == T_TR) { int ti = in_stack(P, T_TABLE, 0); if (ti > 0) pop_to(P, ti + 1); } }
    if (t == T_CAPTION || t == T_COLGROUP) { int ti = in_stack(P, T_TABLE, 0); if (ti > 0) pop_to(P, ti + 1); }
    if (t == T_TABLE) {
        node* c = cur(P);
        if (c->tag == T_TABLE || c->tag == T_TBODY || c->tag == T_TR) {
            int ti = in_stack(P, T_TABLE, 0);
            if (ti > 0) pop_to(P, ti);
        }
    }
    {
        node* c = cur(P);
        if ((c->tag == T_TABLE || c->tag == T_TBODY || c->tag == T_THEAD || c->tag == T_TFOOT || c->tag == T_TR) &&
            t != T_TR && t != T_TD && t != T_TH && t != T_TBODY && t != T_THEAD && t != T_TFOOT && t != T_CAPTION && t != T_COL && t != T_COLGROUP &&
            t != T_SCRIPT && t != T_STYLE && t != T_TEMPLATE && t != T_FORM && t != T_INPUT) {
            int ti = in_stack(P, T_TABLE, 0);
            if (ti > 0 && P->stack[ti]->parent) {
                node* tb = P->stack[ti];
                node_insert_before(tb->parent, e, tb);
                if (!is_void(t) && !selfclose) {
                    P->stack[P->sp++] = e;
                }
                return;
            }
        }
    }
    node_append(cur(P), e);
    if (t == T_SVG) P->in_svg++;
    if (!is_void(t) && !(selfclose && (P->in_svg || t == T_UNKNOWN))) push(P, e);
    else if (t == T_SVG) P->in_svg--;
}

static void handle_end(pstate* P, const char* name) {
    int t = tag_id(name);
    if (t == T_BODY || t == T_HTML) return;
    if (t == T_HEAD) { if (P->head && cur(P) == P->head) P->sp--; return; }
    if (t == T_BR) { node_append(cur(P), elem_new("br")); return; }
    if (t == T_P && in_stack(P, T_P, 1) < 0) { node* p = elem_new("p"); node_append(cur(P), p); return; }
    for (int i = P->sp - 1; i > 0; i--) {
        node* n = P->stack[i];
        if (ieq(n->name, name)) {
            if (n->tag == T_SVG && P->in_svg > 0) P->in_svg--;
            for (int k = i + 1; k < P->sp; k++) if (P->stack[k]->tag == T_SVG && P->in_svg > 0) P->in_svg--;
            P->sp = i;
            return;
        }
        if (t != T_UNKNOWN && (n->tag == T_TABLE || n->tag == T_TD || n->tag == T_TH) && t != T_TABLE && t != T_TD && t != T_TH && t != T_TR && t != T_TBODY && t != T_THEAD && t != T_TFOOT && t != T_CAPTION) {
            if (t == T_FORM || t == T_DIV || t == T_SPAN || t == T_A || t == T_P || t == T_LI || t == T_UL) return;
        }
    }
}

static int name_ch(int c) { return c && !is_ws(c) && c != '/' && c != '>' && c != '='; }

static void run_script_now(pstate* P, node* s) {
    if (!P->run_scripts) return;
    G.cur_script = s;
    eng_script_parsed(s);
    G.cur_script = 0;
    while (G.wbuf.n > 0) {
        char* w = G.wbuf.s;
        int wn = G.wbuf.n;
        G.wbuf.s = 0;
        G.wbuf.n = G.wbuf.cap = 0;
        parse_run(P, w, wn);
    }
}

static void parse_run(pstate* P, const char* s, int n) {
    int i = 0;
    sbuf tb = { 0, 0, 0 };
    while (i < n) {
        if (s[i] != '<') {
            int st = i;
            while (i < n && s[i] != '<') i++;
            tb.n = 0;
            decode_text(s + st, i - st, &tb);
            insert_text(P, tb.s, tb.n);
            continue;
        }
        if (i + 3 < n && s[i + 1] == '!' && s[i + 2] == '-' && s[i + 3] == '-') {
            int st = i + 4, e = st;
            while (e + 2 < n && !(s[e] == '-' && s[e + 1] == '-' && s[e + 2] == '>')) e++;
            if (P->body || P->frag) { node* c = node_new(NT_COMMENT); c->text = nstrndup(s + st, (e < n ? e : n) - st); c->tlen = (int)strlen(c->text); node_append(cur(P), c); }
            i = e + 3 < n ? e + 3 : n;
            continue;
        }
        if (i + 1 < n && (s[i + 1] == '!' || s[i + 1] == '?')) {
            if (i + 9 < n && ieqn(s + i + 2, "doctype", 7)) G.quirks = 0;
            if (i + 8 < n && !memcmp(s + i, "<![CDATA[", 9)) {
                int e = i + 9;
                while (e + 2 < n && !(s[e] == ']' && s[e + 1] == ']' && s[e + 2] == '>')) e++;
                insert_text(P, s + i + 9, e - i - 9);
                i = e + 3;
                continue;
            }
            while (i < n && s[i] != '>') i++;
            i++;
            continue;
        }
        if (i + 1 < n && s[i + 1] == '/') {
            int st = i + 2, e = st;
            while (e < n && name_ch((unsigned char)s[e])) e++;
            if (e == st) { while (i < n && s[i] != '>') i++; i++; continue; }
            char nm[64];
            int nl = e - st < 63 ? e - st : 63;
            memcpy(nm, s + st, (size_t)nl);
            nm[nl] = 0;
            while (e < n && s[e] != '>') e++;
            i = e + 1;
            handle_end(P, nm);
            continue;
        }
        if (i + 1 < n && ((s[i + 1] >= 'a' && s[i + 1] <= 'z') || (s[i + 1] >= 'A' && s[i + 1] <= 'Z'))) {
            int st = i + 1, e = st;
            while (e < n && name_ch((unsigned char)s[e])) e++;
            char nm[64];
            int nl = e - st < 63 ? e - st : 63;
            memcpy(nm, s + st, (size_t)nl);
            nm[nl] = 0;
            node* el = elem_new(nm);
            if (P->in_svg || el->tag == T_SVG) { memcpy(el->name, nm, (size_t)nl + 1); }
            int selfclose = 0;
            i = e;
            for (;;) {
                while (i < n && (is_ws((unsigned char)s[i]) || s[i] == '/')) { if (s[i] == '/' && i + 1 < n && s[i + 1] == '>') selfclose = 1; i++; }
                if (i >= n || s[i] == '>') { i++; break; }
                int as = i;
                while (i < n && name_ch((unsigned char)s[i])) i++;
                if (i == as) { i++; continue; }
                char an[96];
                int al = i - as < 95 ? i - as : 95;
                memcpy(an, s + as, (size_t)al);
                an[al] = 0;
                while (i < n && is_ws((unsigned char)s[i])) i++;
                tb.n = 0;
                if (i < n && s[i] == '=') {
                    i++;
                    while (i < n && is_ws((unsigned char)s[i])) i++;
                    if (i < n && (s[i] == '"' || s[i] == '\'')) {
                        char q = s[i++];
                        int vs = i;
                        while (i < n && s[i] != q) i++;
                        decode_text(s + vs, i - vs, &tb);
                        i++;
                    } else {
                        int vs = i;
                        while (i < n && !is_ws((unsigned char)s[i]) && s[i] != '>') i++;
                        decode_text(s + vs, i - vs, &tb);
                    }
                }
                if (!attr_get(el, an)) attr_set(el, an, tb.s ? sb_str(&tb) : "");
            }
            if (el->tag == T_TEXTAREA || el->tag == T_PRE) { if (i < n && s[i] == '\n') i++; else if (i + 1 < n && s[i] == '\r' && s[i + 1] == '\n') i += 2; }
            int raw = is_raw(el->tag) && !P->in_svg;
            if (el->tag == T_NOSCRIPT && !P->run_scripts) raw = 0;
            if (raw && !selfclose) {
                int st = i, e = i;
                int tl = (int)strlen(el->name);
                while (e < n) {
                    if (s[e] == '<' && e + 1 + tl < n && s[e + 1] == '/' && ieqn(s + e + 2, el->name, tl) && (e + 2 + tl >= n || !name_ch((unsigned char)s[e + 2 + tl]))) break;
                    e++;
                }
                handle_start(P, el, 0);
                if (el->parent && cur(P) == el) {
                    if (e > st) {
                        if (el->tag == T_TEXTAREA || el->tag == T_TITLE) { tb.n = 0; decode_text(s + st, e - st, &tb); node_append(el, text_new(tb.s ? tb.s : "", tb.n)); }
                        else node_append(el, text_new(s + st, e - st));
                    }
                    P->sp--;
                }
                while (e < n && s[e] != '>') e++;
                i = e + 1;
                if (el->tag == T_SCRIPT) run_script_now(P, el);
                continue;
            }
            handle_start(P, el, selfclose);
            if (el->tag == T_SCRIPT && selfclose) run_script_now(P, el);
            continue;
        }
        insert_text(P, "<", 1);
        i++;
    }
    if (tb.s) nfree(tb.s);
}

static pstate* g_pp;
void parse_html(node* doc, const char* src, int len, int run_scripts) {
    pstate* P = nzalloc(sizeof(pstate));
    P->doc = doc;
    P->run_scripts = run_scripts;
    P->stack[0] = doc;
    P->sp = 1;
    G.quirks = 1;
    if (len >= 3 && (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF) { src += 3; len -= 3; }
    pstate* save = g_pp;
    g_pp = P;
    g_dom_parsing++;
    G.parsing = 1;
    parse_run(P, src, len);
    ensure_body(P);
    G.parsing = 0;
    g_pp = save;
    g_dom_parsing--;
    nfree(P);
}

void doc_write(const char* s, int n) {
    if (G.parsing && g_pp) { sb_putn(&G.wbuf, s, n); return; }
    node* b = doc_body();
    if (!b) return;
    node* f = parse_fragment(b, s, n);
    while (f->first) { node* c = f->first; node_remove(c); node_append(b, c); }
}

node* parse_fragment(node* ctx, const char* src, int len) {
    pstate* P = nzalloc(sizeof(pstate));
    node* f = node_new(NT_FRAG);
    P->doc = f;
    P->frag = 1;
    P->stack[0] = f;
    P->sp = 1;
    node* holder = 0;
    if (ctx && (ctx->tag == T_TABLE || ctx->tag == T_TBODY || ctx->tag == T_THEAD || ctx->tag == T_TFOOT || ctx->tag == T_TR || ctx->tag == T_SELECT)) {
        holder = elem_new(ctx->name);
        node_append(f, holder);
        P->stack[P->sp++] = holder;
    }
    if (ctx && (ctx->tag == T_SVG || (ctx->parent && ctx->tag >= T_PATH))) P->in_svg = 1;
    int save = G.parsing;
    G.parsing = 0;
    parse_run(P, src, len);
    G.parsing = save;
    if (holder) {
        node_remove(holder);
        while (holder->first) { node* c = holder->first; node_remove(c); node_append(f, c); }
    }
    nfree(P);
    return f;
}
