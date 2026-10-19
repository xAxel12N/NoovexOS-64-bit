#include "web/nwe.h"
#include "third_party/quickjs/quickjs.h"

extern const char nwe_prelude[];
void eng_node_inserted(node* n);
void eng_node_removed(node* n);
void eng_queue_script(node* s);

static JSRuntime* rt;
static JSContext* ctx;
static JSClassID node_class;
static JSValue p_node, p_elem, p_html, p_text, p_comment, p_doc, p_frag, p_svg, p_chardata;
static JSValue tagproto[T_COUNT];
static unsigned long js_deadline;
static unsigned long g_load_js_ms;
static int js_inited;

typedef struct timer { int id; JSValue fn; unsigned long due; int interval, ms, active, raf; } timer;
static timer* timers;
static int ntimers, ctimers, next_tid;

static int interrupt_cb(JSContext* r, void* op) { (void)r; (void)op; if (nwk_busy()) { extern int g_nwe_user_stop; g_nwe_user_stop = 1; return 1; } return js_deadline && nwk_ms() > js_deadline; }
static void arm(void) { js_deadline = nwk_ms() + 6000; }

static void report(void) {
    JSValue e = JS_GetException(ctx);
    const char* s = JS_ToCString(ctx, e);
    JSValue st = JS_IsObject(e) ? JS_GetPropertyStr(ctx, e, "stack") : JS_UNDEFINED;
    const char* ss = JS_IsUndefined(st) ? 0 : JS_ToCString(ctx, st);
    char buf[600];
    snprintf(buf, sizeof(buf), "JS error: %s %s", s ? s : "?", ss ? ss : "");
    nwk_log(buf);
    if (ss) JS_FreeCString(ctx, ss);
    if (s) JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, st);
    JS_FreeValue(ctx, e);
}

static node* N(JSValueConst v) { return JS_GetOpaque(v, node_class); }
static JSValue proto_for(node* n) {
    switch (n->type) {
    case NT_DOC: return p_doc;
    case NT_TEXT: return p_text;
    case NT_COMMENT: return p_comment;
    case NT_FRAG: return p_frag;
    }
    if (n->tag >= T_PATH || n->tag == T_SVG) return p_svg;
    if (n->tag > 0 && n->tag < T_COUNT && !JS_IsUndefined(tagproto[n->tag])) return tagproto[n->tag];
    return p_html;
}
static JSValue wrap(node* n) {
    if (!n) return JS_NULL;
    if (n->jsobj) return JS_DupValue(ctx, *(JSValue*)n->jsobj);
    JSValue o = JS_NewObjectProtoClass(ctx, proto_for(n), node_class);
    if (JS_IsException(o)) return o;
    JS_SetOpaque(o, n);
    int sv = pool_suspend();
    JSValue* keep = nalloc(sizeof(JSValue));
    pool_resume(sv);
    *keep = JS_DupValue(ctx, o);
    n->jsobj = keep;
    return o;
}
static JSValue jstr(const char* s) { return JS_NewString(ctx, s ? s : ""); }
static JSValue jstrn(const char* s, int n) { return JS_NewStringLen(ctx, s ? s : "", (size_t)(s ? n : 0)); }
static uint8_t* jbytes(JSValueConst v, size_t* len) {
    *len = 0;
    if (!JS_IsObject(v)) return 0;
    size_t off = 0, bl = 0, bpe = 0;
    JSValue ab = JS_GetTypedArrayBuffer(ctx, v, &off, &bl, &bpe);
    if (!JS_IsException(ab)) { size_t tl = 0; uint8_t* p = JS_GetArrayBuffer(ctx, &tl, ab); JS_FreeValue(ctx, ab); if (p) { *len = bl; return p + off; } return 0; }
    JS_FreeValue(ctx, JS_GetException(ctx));
    size_t tl = 0;
    uint8_t* p = JS_GetArrayBuffer(ctx, &tl, v);
    if (p) { *len = tl; return p; }
    JS_FreeValue(ctx, JS_GetException(ctx));
    JSValue b = JS_GetPropertyStr(ctx, v, "buffer");
    if (JS_IsObject(b)) {
        p = JS_GetArrayBuffer(ctx, &tl, b);
        if (!p) JS_FreeValue(ctx, JS_GetException(ctx));
        else {
            int32_t o = 0, l = 0;
            JSValue ov = JS_GetPropertyStr(ctx, v, "byteOffset"), lv = JS_GetPropertyStr(ctx, v, "byteLength");
            JS_ToInt32(ctx, &o, ov); JS_ToInt32(ctx, &l, lv);
            JS_FreeValue(ctx, ov); JS_FreeValue(ctx, lv);
            JS_FreeValue(ctx, b);
            if (o >= 0 && l >= 0 && (size_t)o + (size_t)l <= tl) { *len = (size_t)l; return p + o; }
            return 0;
        }
    }
    JS_FreeValue(ctx, b);
    return 0;
}
static char* cstr(JSValueConst v) {
    if (JS_IsNull(v) || JS_IsUndefined(v)) return 0;
    size_t l;
    const char* s = JS_ToCStringLen(ctx, &l, v);
    if (!s) return 0;
    char* r = nstrndup(s, (int)l);
    JS_FreeCString(ctx, s);
    return r;
}
static char* cstr_or(JSValueConst v, const char* def) { char* r = cstr(v); return r ? r : nstrdup(def); }

static void dirty(void) { G.dirty_style = 1; G.dirty_layout = 1; G.dirty_paint = 1; }

static JSValue node_list(node** a, int n) {
    JSValue arr = JS_NewArray(ctx);
    for (int i = 0; i < n; i++) JS_SetPropertyUint32(ctx, arr, (uint32_t)i, wrap(a[i]));
    return arr;
}
static JSValue children_arr(node* p, int elems_only) {
    JSValue arr = JS_NewArray(ctx);
    uint32_t i = 0;
    for (node* c = p->first; c; c = c->next) if (!elems_only || c->type == NT_ELEM) JS_SetPropertyUint32(ctx, arr, i++, wrap(c));
    return arr;
}

static const char* reflect_attr[] = { "id", "class", "name", "type", "alt", "title", "placeholder", "for", "action", "method", "target", "rel", "lang", "dir", "content", "label", "role", "download", "accept", "autocomplete", "pattern", "srcset", "sizes", "loading", "media", "hreflang", "charset", "async", "enctype", "form", "list", "inputmode", "cols", "rows", "colspan", "rowspan", "headers", "scope", "abbr", "datetime", "cite", "crossorigin", "referrerpolicy", "integrity", "nonce", "poster", "preload", "kind", "srclang", "wrap", "accesskey", "translate", "enterkeyhint" };
#define NREFLECT ((int)(sizeof(reflect_attr) / sizeof(reflect_attr[0])))

enum {
    G_NODETYPE = 100, G_NODENAME, G_TAGNAME, G_LOCALNAME, G_TEXTCONTENT, G_INNERHTML, G_OUTERHTML, G_INNERTEXT, G_CHILDREN, G_CHILDNODES,
    G_FIRSTCHILD, G_LASTCHILD, G_FIRSTEL, G_LASTEL, G_NEXTSIB, G_PREVSIB, G_NEXTEL, G_PREVEL, G_PARENTNODE, G_PARENTEL, G_CHILDCOUNT, G_OWNERDOC,
    G_ISCONNECTED, G_VALUE, G_CHECKED, G_DISABLED, G_SELIDX, G_HREF, G_SRC, G_HIDDEN, G_OFFW, G_OFFH, G_OFFT, G_OFFL, G_OFFP, G_CLIW, G_CLIH,
    G_SCRW, G_SCRH, G_SCRT, G_SCRL, G_FORMEL, G_NODEVALUE, G_NATW, G_NATH, G_COMPLETE, G_WIDTH, G_HEIGHT, G_TABINDEX, G_DEFVALUE, G_OPEN, G_TEXT,
    G_SELECTED, G_INDEX, G_DEFCHECKED, G_READONLY, G_REQUIRED, G_MAXLEN, G_ATTRS, G_LENGTH, G_OPTIONS, G_ELEMENTS, G_CURSRC, G_DEFSELECTED,
    G_MULTIPLE, G_AUTOFOCUS, G_NOVALIDATE, G_SELSTART, G_SELEND, G_DEFER, G_NOMODULE, G_CHECKVALIDITY, G_CONTENTEDITABLE, G_ISCONTENTEDITABLE,
    G_CLITOP, G_CLILEFT, G_DRAGGABLE, G_SPELLCHECK, G_VALUEASNUM, G_MIN, G_MAX, G_STEP, G_STYLETEXT
};

static node* form_of(node* n) { for (node* p = n->parent; p; p = p->parent) if (p->tag == T_FORM) return p; const char* f = attr_get(n, "form"); if (f) return find_id(G.doc, f); return 0; }
static const char* input_value(node* n, sbuf* tmp) {
    if (n->tag == T_SELECT) {
        int idx = 0, want = n->selidx;
        node* first = 0;
        node* stack[256];
        int sp = 0;
        for (node* c = n->last; c; c = c->prev) if (sp < 256) stack[sp++] = c;
        while (sp > 0) {
            node* o = stack[--sp];
            if (o->tag == T_OPTGROUP) { for (node* q = o->last; q; q = q->prev) if (sp < 256) stack[sp++] = q; continue; }
            if (o->tag != T_OPTION) continue;
            if (!first) first = o;
            if (want >= 0 ? idx == want : attr_get(o, "selected") != 0) { const char* v = attr_get(o, "value"); if (v) return v; text_content(o, tmp); return tmp->s ? tmp->s : ""; }
            idx++;
        }
        if (want < 0 && first) { const char* v = attr_get(first, "value"); if (v) return v; text_content(first, tmp); return tmp->s ? tmp->s : ""; }
        return "";
    }
    if (n->tag == T_OPTION) { const char* v = attr_get(n, "value"); if (v) return v; text_content(n, tmp); return tmp->s ? tmp->s : ""; }
    if (n->value) return n->value;
    if (n->tag == T_TEXTAREA) { text_content(n, tmp); return tmp->s ? tmp->s : ""; }
    const char* v = attr_get(n, "value");
    if (!v) { const char* ty = attr_get(n, "type"); if (ty && (ieq(ty, "checkbox") || ieq(ty, "radio"))) return "on"; }
    return v ? v : "";
}
static void set_value(node* n, const char* v) {
    if (n->tag == T_SELECT) {
        int idx = 0;
        node* stack[256];
        int sp = 0;
        for (node* c = n->last; c; c = c->prev) if (sp < 256) stack[sp++] = c;
        while (sp > 0) {
            node* o = stack[--sp];
            if (o->tag == T_OPTGROUP) { for (node* q = o->last; q; q = q->prev) if (sp < 256) stack[sp++] = q; continue; }
            if (o->tag != T_OPTION) continue;
            sbuf t = { 0, 0, 0 };
            const char* ov = attr_get(o, "value");
            if (!ov) { text_content(o, &t); ov = t.s ? t.s : ""; }
            if (!strcmp(ov, v)) { n->selidx = idx; dirty(); return; }
            idx++;
        }
        n->selidx = -2;
        dirty();
        return;
    }
    int l = (int)strlen(v);
    n->value = nalloc((size_t)l + 1);
    memcpy(n->value, v, (size_t)l + 1);
    n->vlen = l;
    n->vcap = l + 1;
    n->caret = l;
    n->dirtyval = 1;
    dirty();
}
static void abs_pos(node* n, float* x, float* y, float* w, float* h) { box_rect(n, x, y, w, h); }

static void js_flush_n(node* n) {
    static unsigned long last, spent, win; static int busy;
    if (busy) return;
    unsigned long t = nwk_ms();
    if (t - win > 1000) { win = t; spent = 0; }
    int nobox = n && !n->box;
    if (nobox && G.doc && !(G.dirty_style || G.dirty_layout)) { node* p = n; while (p && p != G.doc) p = p->parent; if (p == G.doc) G.dirty_style = 1; else return; }
    if (!(G.dirty_style || G.dirty_layout)) return;
    if (spent > 60) return;
    busy = 1; eng_layout_if_dirty(); busy = 0;
    unsigned long e = nwk_ms(); spent += e - t; last = e;
}
static JSValue g_get(JSContext* c, JSValueConst this_val, int magic) {
    (void)c;
    node* n = N(this_val);
    if (!n) return JS_UNDEFINED;
    if (magic < NREFLECT) {
        const char* v = attr_get(n, reflect_attr[magic]);
        if (magic == 3 && !v) { if (n->tag == T_INPUT) return jstr("text"); if (n->tag == T_BUTTON) return jstr("submit"); if (n->tag == T_SELECT) return jstr(attr_get(n, "multiple") ? "select-multiple" : "select-one"); if (n->tag == T_TEXTAREA) return jstr("textarea"); }
        if (magic == 3 && v && n->tag == T_INPUT) { char b[32]; int i = 0; for (; v[i] && i < 31; i++) b[i] = (char)lc((unsigned char)v[i]); b[i] = 0; return jstr(b); }
        if (magic == 9 && n->tag == T_FORM) { if (!v) return jstr("get"); char b[16]; int i = 0; for (; v[i] && i < 15; i++) b[i] = (char)lc((unsigned char)v[i]); b[i] = 0; return jstr(b); }
        if (magic == 8 && n->tag == T_FORM) { char full[1024]; url_resolve(G.base, v ? v : G.url, full, sizeof(full)); return jstr(full); }
        if (magic == 27) return JS_NewBool(ctx, v != 0);
        return jstr(v ? v : "");
    }
    sbuf b = { 0, 0, 0 };
    float x, y, w, h;
    switch (magic) {
    case G_NODETYPE: return JS_NewInt32(ctx, n->type);
    case G_NODENAME: case G_TAGNAME: {
        if (n->type == NT_TEXT) return jstr("#text");
        if (n->type == NT_COMMENT) return jstr("#comment");
        if (n->type == NT_DOC) return jstr("#document");
        if (n->type == NT_FRAG) return jstr("#document-fragment");
        if (n->tag >= T_PATH || n->tag == T_SVG) return jstr(n->name);
        char t[64];
        int i = 0;
        for (; n->name[i] && i < 63; i++) t[i] = (char)((n->name[i] >= 'a' && n->name[i] <= 'z') ? n->name[i] - 32 : n->name[i]);
        t[i] = 0;
        return jstr(t);
    }
    case G_LOCALNAME: return n->type == NT_ELEM ? jstr(n->name) : JS_NULL;
    case G_TEXTCONTENT: if (n->type == NT_DOC) return JS_NULL; text_content(n, &b); return jstrn(b.s, b.n);
    case G_INNERTEXT: {
        text_content(n, &b);
        if (!b.s) return jstr("");
        sbuf o = { 0, 0, 0 };
        int sp = 1;
        for (int i = 0; i < b.n; i++) { char ch = b.s[i]; if (is_ws((unsigned char)ch) && (!n->st || n->st->ws < WS_PRE)) { if (!sp) { sb_putc(&o, ' '); sp = 1; } } else { sb_putc(&o, ch); sp = 0; } }
        while (o.n > 0 && o.s[o.n - 1] == ' ') o.n--;
        return jstrn(o.s, o.n);
    }
    case G_INNERHTML: serialize(n, &b, 0); return jstrn(b.s, b.n);
    case G_OUTERHTML: serialize(n, &b, 1); return jstrn(b.s, b.n);
    case G_CHILDREN: return children_arr(n, 1);
    case G_CHILDNODES: return children_arr(n, 0);
    case G_FIRSTCHILD: return wrap(n->first);
    case G_LASTCHILD: return wrap(n->last);
    case G_FIRSTEL: for (node* c = n->first; c; c = c->next) if (c->type == NT_ELEM) return wrap(c); return JS_NULL;
    case G_LASTEL: for (node* c = n->last; c; c = c->prev) if (c->type == NT_ELEM) return wrap(c); return JS_NULL;
    case G_NEXTSIB: return wrap(n->next);
    case G_PREVSIB: return wrap(n->prev);
    case G_NEXTEL: for (node* c = n->next; c; c = c->next) if (c->type == NT_ELEM) return wrap(c); return JS_NULL;
    case G_PREVEL: for (node* c = n->prev; c; c = c->prev) if (c->type == NT_ELEM) return wrap(c); return JS_NULL;
    case G_PARENTNODE: return wrap(n->parent);
    case G_PARENTEL: return (n->parent && n->parent->type == NT_ELEM) ? wrap(n->parent) : JS_NULL;
    case G_CHILDCOUNT: { int k = 0; for (node* c = n->first; c; c = c->next) if (c->type == NT_ELEM) k++; return JS_NewInt32(ctx, k); }
    case G_OWNERDOC: return n->type == NT_DOC ? JS_NULL : wrap(G.doc);
    case G_ISCONNECTED: return JS_NewBool(ctx, node_contains(G.doc, n));
    case G_VALUE: { const char* v = input_value(n, &b); return jstr(v); }
    case G_CHECKED: return JS_NewBool(ctx, n->checked);
    case G_DEFCHECKED: return JS_NewBool(ctx, attr_get(n, "checked") != 0);
    case G_DISABLED: return JS_NewBool(ctx, attr_get(n, "disabled") != 0);
    case G_READONLY: return JS_NewBool(ctx, attr_get(n, "readonly") != 0);
    case G_REQUIRED: return JS_NewBool(ctx, attr_get(n, "required") != 0);
    case G_MULTIPLE: return JS_NewBool(ctx, attr_get(n, "multiple") != 0);
    case G_AUTOFOCUS: return JS_NewBool(ctx, attr_get(n, "autofocus") != 0);
    case G_NOVALIDATE: return JS_NewBool(ctx, attr_get(n, "novalidate") != 0);
    case G_DEFER: return JS_NewBool(ctx, attr_get(n, "defer") != 0);
    case G_NOMODULE: return JS_NewBool(ctx, attr_get(n, "nomodule") != 0);
    case G_DRAGGABLE: return JS_NewBool(ctx, attr_get(n, "draggable") && !ieq(attr_get(n, "draggable"), "false"));
    case G_SPELLCHECK: return JS_TRUE;
    case G_CONTENTEDITABLE: { const char* v = attr_get(n, "contenteditable"); return jstr(v ? (*v ? v : "true") : "inherit"); }
    case G_ISCONTENTEDITABLE: return JS_FALSE;
    case G_MAXLEN: { const char* v = attr_get(n, "maxlength"); return JS_NewInt32(ctx, v ? atoi(v) : -1); }
    case G_MIN: case G_MAX: case G_STEP: { const char* v = attr_get(n, magic == G_MIN ? "min" : magic == G_MAX ? "max" : "step"); return jstr(v ? v : ""); }
    case G_VALUEASNUM: { const char* v = input_value(n, &b); return JS_NewFloat64(ctx, (v && *v) ? strtod(v, 0) : NAN); }
    case G_SELIDX: {
        if (n->tag != T_SELECT) return JS_NewInt32(ctx, -1);
        if (n->selidx >= 0) return JS_NewInt32(ctx, n->selidx);
        if (n->selidx == -2) return JS_NewInt32(ctx, -1);
        int idx = 0, found = -1, any = 0;
        node* stack[256];
        int sp = 0;
        for (node* c = n->last; c; c = c->prev) if (sp < 256) stack[sp++] = c;
        while (sp > 0) { node* o = stack[--sp]; if (o->tag == T_OPTGROUP) { for (node* q = o->last; q; q = q->prev) if (sp < 256) stack[sp++] = q; continue; } if (o->tag != T_OPTION) continue; any = 1; if (attr_get(o, "selected") && found < 0) found = idx; idx++; }
        return JS_NewInt32(ctx, found >= 0 ? found : any ? 0 : -1);
    }
    case G_SELECTED: case G_DEFSELECTED: {
        node* s = n->parent;
        if (s && s->tag == T_OPTGROUP) s = s->parent;
        if (magic == G_DEFSELECTED || !s || s->tag != T_SELECT || s->selidx < 0) return JS_NewBool(ctx, attr_get(n, "selected") != 0 || (s && s->tag == T_SELECT && s->selidx == -1 && magic == G_SELECTED && 0));
        int idx = 0;
        node* stack[256];
        int sp = 0;
        for (node* c = s->last; c; c = c->prev) if (sp < 256) stack[sp++] = c;
        while (sp > 0) { node* o = stack[--sp]; if (o->tag == T_OPTGROUP) { for (node* q = o->last; q; q = q->prev) if (sp < 256) stack[sp++] = q; continue; } if (o->tag != T_OPTION) continue; if (o == n) return JS_NewBool(ctx, idx == s->selidx); idx++; }
        return JS_FALSE;
    }
    case G_INDEX: { int idx = 0; node* s = n->parent; if (s && s->tag == T_OPTGROUP) s = s->parent; if (!s) return JS_NewInt32(ctx, 0); for (node* c = s->first; c; c = c->next) { if (c == n) return JS_NewInt32(ctx, idx); if (c->tag == T_OPTION) idx++; } return JS_NewInt32(ctx, 0); }
    case G_TEXT: text_content(n, &b); return jstrn(b.s, b.n);
    case G_HREF: case G_SRC: case G_CURSRC: {
        const char* v = attr_get(n, magic == G_HREF ? "href" : "src");
        if (!v) return jstr("");
        char full[2048];
        url_resolve(G.base, v, full, sizeof(full));
        return jstr(full);
    }
    case G_HIDDEN: return JS_NewBool(ctx, attr_get(n, "hidden") != 0);
    case G_OFFW: case G_OFFH: case G_OFFT: case G_OFFL: case G_CLIW: case G_CLIH: case G_SCRW: case G_SCRH: case G_CLITOP: case G_CLILEFT: {
        js_flush_n(n);
        if (n->type == NT_ELEM && (n->tag == T_HTML || n->tag == T_BODY) && (magic == G_CLIW || magic == G_CLIH) && n->tag == T_HTML) return JS_NewInt32(ctx, magic == G_CLIW ? (int)G.vw : (int)G.vh);
        abs_pos(n, &x, &y, &w, &h);
        box* bx = box_of(n);
        if (magic == G_OFFT || magic == G_OFFL) {
            float px = 0, py = 0, pw, ph;
            for (node* p = n->parent; p && p->type == NT_ELEM; p = p->parent) if (p->st && (p->st->position != P_STATIC || p->tag == T_BODY || p->tag == T_TD || p->tag == T_TABLE)) { abs_pos(p, &px, &py, &pw, &ph); if (p->box) { px += p->box->b[3]; py += p->box->b[0]; } break; }
            return JS_NewInt32(ctx, (int)roundf(magic == G_OFFT ? y - py : x - px));
        }
        if (magic == G_CLITOP || magic == G_CLILEFT) return JS_NewInt32(ctx, bx ? (int)(magic == G_CLITOP ? bx->b[0] : bx->b[3]) : 0);
        if (magic == G_CLIW || magic == G_CLIH) { if (!bx || bx->kind == BX_INLINE) return JS_NewInt32(ctx, 0); return JS_NewInt32(ctx, (int)(magic == G_CLIW ? w - bx->b[1] - bx->b[3] : h - bx->b[0] - bx->b[2])); }
        if (magic == G_SCRW) { if (n->tag == T_HTML || n->tag == T_BODY) return JS_NewInt32(ctx, (int)g_docw); return JS_NewInt32(ctx, (int)w); }
        if (magic == G_SCRH) { if (n->tag == T_HTML || n->tag == T_BODY) return JS_NewInt32(ctx, (int)g_doch); { float ch2 = bx ? h - bx->b[0] - bx->b[2] : h; if (bx && bx->contenth + bx->p[0] + bx->p[2] > ch2) return JS_NewInt32(ctx, (int)(bx->contenth + bx->p[0] + bx->p[2])); return JS_NewInt32(ctx, (int)ch2); }; }
        return JS_NewInt32(ctx, (int)roundf(magic == G_OFFW ? w : h));
    }
    case G_OFFP: for (node* p = n->parent; p && p->type == NT_ELEM; p = p->parent) if (p->st && (p->st->position != P_STATIC || p->tag == T_BODY)) return wrap(p); return JS_NULL;
    case G_SCRT: if (n->tag == T_HTML || n->tag == T_BODY) return JS_NewInt32(ctx, (int)G.scroll); return JS_NewInt32(ctx, (int)n->scrolly);
    case G_SCRL: return JS_NewInt32(ctx, 0);
    case G_FORMEL: return wrap(form_of(n));
    case G_NODEVALUE: if (n->type == NT_TEXT || n->type == NT_COMMENT) return jstrn(n->text, n->tlen); return JS_NULL;
    case G_NATW: case G_NATH: case G_WIDTH: case G_HEIGHT: {
        if (magic == G_WIDTH || magic == G_HEIGHT) {
            if (n->tag == T_CANVAS) { const char* v = attr_get(n, magic == G_WIDTH ? "width" : "height"); return JS_NewInt32(ctx, v ? atoi(v) : (magic == G_WIDTH ? 300 : 150)); }
            abs_pos(n, &x, &y, &w, &h);
            box* bx = box_of(n);
            if (bx) { w -= bx->p[1] + bx->p[3] + bx->b[1] + bx->b[3]; h -= bx->p[0] + bx->p[2] + bx->b[0] + bx->b[2]; }
            return JS_NewInt32(ctx, (int)(magic == G_WIDTH ? w : h));
        }
        if (n->img && n->img->state == 2) return JS_NewInt32(ctx, magic == G_NATW ? n->img->w : n->img->h);
        return JS_NewInt32(ctx, 0);
    }
    case G_COMPLETE: return JS_NewBool(ctx, !n->img || n->img->state >= 2);
    case G_TABINDEX: { const char* v = attr_get(n, "tabindex"); if (v) return JS_NewInt32(ctx, atoi(v)); int foc = n->tag == T_A || n->tag == T_INPUT || n->tag == T_BUTTON || n->tag == T_SELECT || n->tag == T_TEXTAREA; return JS_NewInt32(ctx, foc ? 0 : -1); }
    case G_DEFVALUE: { if (n->tag == T_TEXTAREA) { text_content(n, &b); return jstrn(b.s, b.n); } const char* v = attr_get(n, "value"); return jstr(v ? v : ""); }
    case G_OPEN: return JS_NewBool(ctx, attr_get(n, "open") != 0);
    case G_SELSTART: case G_SELEND: return JS_NewInt32(ctx, n->caret);
    case G_LENGTH: case G_OPTIONS: case G_ELEMENTS: {
        node* list[4096];
        int cnt = 0;
        node* stack[1024];
        int sp = 0;
        for (node* c = n->last; c; c = c->prev) if (sp < 1024) stack[sp++] = c;
        while (sp > 0) {
            node* c = stack[--sp];
            if (c->type != NT_ELEM) continue;
            int want = n->tag == T_SELECT || magic == G_OPTIONS ? c->tag == T_OPTION : (c->tag == T_INPUT || c->tag == T_SELECT || c->tag == T_TEXTAREA || c->tag == T_BUTTON || c->tag == T_FIELDSET || c->tag == T_OUTPUT);
            if (want && cnt < 4096) list[cnt++] = c;
            for (node* d = c->last; d; d = d->prev) if (sp < 1024) stack[sp++] = d;
        }
        if (magic == G_LENGTH) return JS_NewInt32(ctx, cnt);
        return node_list(list, cnt);
    }
    case G_ATTRS: {
        JSValue arr = JS_NewArray(ctx);
        for (int i = 0; i < n->nattrs; i++) {
            JSValue o = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, o, "name", jstr(n->attrs[i].name));
            JS_SetPropertyStr(ctx, o, "localName", jstr(n->attrs[i].name));
            JS_SetPropertyStr(ctx, o, "value", jstr(n->attrs[i].val));
            JS_SetPropertyStr(ctx, o, "nodeValue", jstr(n->attrs[i].val));
            JS_SetPropertyUint32(ctx, arr, (uint32_t)i, o);
        }
        return arr;
    }
    case G_STYLETEXT: { const char* v = attr_get(n, "style"); return jstr(v ? v : ""); }
    }
    return JS_UNDEFINED;
}

static void remove_children(node* n) { while (n->first) { node* c = n->first; node_remove(c); eng_node_removed(c); } }

static JSValue g_set(JSContext* c, JSValueConst this_val, JSValueConst val, int magic) {
    (void)c;
    node* n = N(this_val);
    if (!n) return JS_UNDEFINED;
    if (magic < NREFLECT) {
        if (magic == 27) { if (JS_ToBool(ctx, val)) attr_set(n, reflect_attr[magic], ""); else attr_del(n, reflect_attr[magic]); dirty(); return JS_UNDEFINED; }
        char* s = cstr_or(val, "");
        attr_set(n, reflect_attr[magic], s);
        if (magic == 1 || magic == 0) css_bump();
        dirty();
        return JS_UNDEFINED;
    }
    switch (magic) {
    case G_TEXTCONTENT: case G_INNERTEXT: {
        char* s = cstr_or(val, "");
        if (n->type == NT_TEXT || n->type == NT_COMMENT) { char* old = g_mut_on ? nstrndup(n->text ? n->text : "", n->tlen) : 0; n->text = s; n->tlen = (int)strlen(s); if (old) dom_mut_text(n, old); dirty(); return JS_UNDEFINED; }
        remove_children(n);
        if (*s) {
            if (magic == G_INNERTEXT && strchr(s, '\n')) {
                const char* p = s;
                while (*p) { const char* e = strchr(p, '\n'); int l = e ? (int)(e - p) : (int)strlen(p); if (l) node_append(n, text_new(p, l)); if (!e) break; node_append(n, elem_new("br")); p = e + 1; }
            } else node_append(n, text_new(s, (int)strlen(s)));
        }
        if (n->tag == T_STYLE) css_bump();
        dirty();
        return JS_UNDEFINED;
    }
    case G_INNERHTML: {
        char* s = cstr_or(val, "");
        if (n->tag == T_TEMPLATE) { remove_children(n); }
        node* f = parse_fragment(n, s, (int)strlen(s));
        remove_children(n);
        while (f->first) { node* k = f->first; node_remove(k); node_append(n, k); eng_node_inserted(k); }
        if (n->tag == T_STYLE) css_bump();
        dirty();
        return JS_UNDEFINED;
    }
    case G_OUTERHTML: {
        if (!n->parent) return JS_UNDEFINED;
        char* s = cstr_or(val, "");
        node* f = parse_fragment(n->parent, s, (int)strlen(s));
        node* p = n->parent;
        while (f->first) { node* k = f->first; node_remove(k); node_insert_before(p, k, n); eng_node_inserted(k); }
        node_remove(n);
        eng_node_removed(n);
        dirty();
        return JS_UNDEFINED;
    }
    case G_NODEVALUE: if (n->type == NT_TEXT || n->type == NT_COMMENT) { char* s = cstr_or(val, ""); char* old = g_mut_on ? nstrndup(n->text ? n->text : "", n->tlen) : 0; n->text = s; n->tlen = (int)strlen(s); if (old) dom_mut_text(n, old); dirty(); } return JS_UNDEFINED;
    case G_VALUE: { char* s = cstr_or(val, ""); set_value(n, s); return JS_UNDEFINED; }
    case G_CHECKED: {
        int v = JS_ToBool(ctx, val);
        n->checked = v;
        n->flags |= NF_INLINEDIRTY;
        if (v) {
            const char* ty = attr_get(n, "type");
            const char* nm = attr_get(n, "name");
            if (ty && ieq(ty, "radio") && nm) {
                node* root = form_of(n);
                if (!root) root = G.doc;
                node* stack[1024];
                int sp = 0;
                stack[sp++] = root;
                while (sp > 0) { node* q = stack[--sp]; if (q != n && q->tag == T_INPUT && attr_get(q, "name") && !strcmp(attr_get(q, "name"), nm)) { q->checked = 0; q->flags |= NF_INLINEDIRTY; } for (node* d = q->first; d; d = d->next) if (d->type == NT_ELEM && sp < 1024) stack[sp++] = d; }
            }
        }
        dirty();
        return JS_UNDEFINED;
    }
    case G_DEFCHECKED: if (JS_ToBool(ctx, val)) attr_set(n, "checked", ""); else attr_del(n, "checked"); return JS_UNDEFINED;
    case G_DISABLED: case G_READONLY: case G_REQUIRED: case G_HIDDEN: case G_OPEN: case G_MULTIPLE: case G_AUTOFOCUS: case G_NOVALIDATE: case G_DEFER: case G_NOMODULE: {
        const char* an = magic == G_DISABLED ? "disabled" : magic == G_READONLY ? "readonly" : magic == G_REQUIRED ? "required" : magic == G_HIDDEN ? "hidden" : magic == G_OPEN ? "open" : magic == G_MULTIPLE ? "multiple" : magic == G_AUTOFOCUS ? "autofocus" : magic == G_NOVALIDATE ? "novalidate" : magic == G_DEFER ? "defer" : "nomodule";
        if (JS_ToBool(ctx, val)) attr_set(n, an, ""); else attr_del(n, an);
        css_bump();
        dirty();
        return JS_UNDEFINED;
    }
    case G_SELIDX: { int32_t v; JS_ToInt32(ctx, &v, val); n->selidx = v < 0 ? -2 : v; dirty(); return JS_UNDEFINED; }
    case G_SELECTED: {
        node* s = n->parent;
        if (s && s->tag == T_OPTGROUP) s = s->parent;
        if (s && s->tag == T_SELECT && JS_ToBool(ctx, val)) { int idx = 0; for (node* c = s->first; c; c = c->next) { if (c == n) { s->selidx = idx; break; } if (c->tag == T_OPTION) idx++; } }
        dirty();
        return JS_UNDEFINED;
    }
    case G_DEFSELECTED: if (JS_ToBool(ctx, val)) attr_set(n, "selected", ""); else attr_del(n, "selected"); return JS_UNDEFINED;
    case G_HREF: case G_SRC: {
        char* s = cstr_or(val, "");
        attr_set(n, magic == G_HREF ? "href" : "src", s);
        if (magic == G_SRC && (n->tag == T_IMG || n->tag == T_SCRIPT)) eng_node_inserted(n);
        if (n->tag == T_LINK) { n->value = 0; css_bump(); }
        dirty();
        return JS_UNDEFINED;
    }
    case G_SCRT: {
        double v;
        JS_ToFloat64(ctx, &v, val);
        if (n->tag == T_HTML || n->tag == T_BODY) eng_scroll_to((float)v);
        else { n->scrolly = (float)v; if (n->box) n->box->sy = (float)v; G.dirty_paint = 1; }
        return JS_UNDEFINED;
    }
    case G_TEXT: { char* s = cstr_or(val, ""); remove_children(n); node_append(n, text_new(s, (int)strlen(s))); dirty(); return JS_UNDEFINED; }
    case G_DEFVALUE: { char* s = cstr_or(val, ""); if (n->tag == T_TEXTAREA) { remove_children(n); node_append(n, text_new(s, (int)strlen(s))); } else attr_set(n, "value", s); dirty(); return JS_UNDEFINED; }
    case G_WIDTH: case G_HEIGHT: { char* s = cstr_or(val, ""); attr_set(n, magic == G_WIDTH ? "width" : "height", s); dirty(); return JS_UNDEFINED; }
    case G_TABINDEX: { char* s = cstr_or(val, "0"); attr_set(n, "tabindex", s); return JS_UNDEFINED; }
    case G_SELSTART: case G_SELEND: { int32_t v; JS_ToInt32(ctx, &v, val); if (v < 0) v = 0; if (v > n->vlen) v = n->vlen; n->caret = v; return JS_UNDEFINED; }
    case G_MAXLEN: { int32_t v; JS_ToInt32(ctx, &v, val); char b[16]; snprintf(b, 16, "%d", v); attr_set(n, "maxlength", b); return JS_UNDEFINED; }
    case G_MIN: case G_MAX: case G_STEP: { char* s = cstr_or(val, ""); attr_set(n, magic == G_MIN ? "min" : magic == G_MAX ? "max" : "step", s); return JS_UNDEFINED; }
    case G_CONTENTEDITABLE: { char* s = cstr_or(val, "inherit"); attr_set(n, "contenteditable", s); return JS_UNDEFINED; }
    case G_STYLETEXT: { char* s = cstr_or(val, ""); attr_set(n, "style", s); dirty(); return JS_UNDEFINED; }
    case G_DRAGGABLE: attr_set(n, "draggable", JS_ToBool(ctx, val) ? "true" : "false"); return JS_UNDEFINED;
    case G_LENGTH: {
        int32_t v;
        JS_ToInt32(ctx, &v, val);
        if (n->tag == T_SELECT) { int k = 0; for (node* c = n->first; c;) { node* nx = c->next; if (c->tag == T_OPTION) { if (k >= v) { node_remove(c); eng_node_removed(c); } k++; } c = nx; } dirty(); }
        return JS_UNDEFINED;
    }
    }
    return JS_UNDEFINED;
}

static node* check_child(JSValueConst v) { node* c = N(v); if (!c) JS_ThrowTypeError(ctx, "parameter is not a Node"); return c; }

static int is_ancestor_or_self(node* a, node* b) { for (node* p = b; p; p = p->parent) if (p == a) return 1; return 0; }

static JSValue m_append(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)c;
    node* p = N(this_val);
    if (!p || argc < 1) return JS_UNDEFINED;
    node* k = check_child(argv[0]);
    if (!k) return JS_EXCEPTION;
    if (is_ancestor_or_self(k, p)) return JS_ThrowTypeError(ctx, "HierarchyRequestError");
    int frag = k->type == NT_FRAG;
    node* items[4096];
    int ni = 0;
    if (frag) for (node* q = k->first; q && ni < 4096; q = q->next) items[ni++] = q;
    node_append(p, k);
    if (frag) for (int i = 0; i < ni; i++) eng_node_inserted(items[i]);
    else eng_node_inserted(k);
    dirty();
    return JS_DupValue(ctx, argv[0]);
}
static JSValue m_insert(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)c;
    node* p = N(this_val);
    if (!p || argc < 1) return JS_UNDEFINED;
    node* k = check_child(argv[0]);
    if (!k) return JS_EXCEPTION;
    if (is_ancestor_or_self(k, p)) return JS_ThrowTypeError(ctx, "HierarchyRequestError");
    node* ref = argc > 1 ? N(argv[1]) : 0;
    if (ref == k) return JS_DupValue(ctx, argv[0]);
    int frag = k->type == NT_FRAG;
    node* items[4096];
    int ni = 0;
    if (frag) for (node* q = k->first; q && ni < 4096; q = q->next) items[ni++] = q;
    node_insert_before(p, k, ref);
    if (frag) for (int i = 0; i < ni; i++) eng_node_inserted(items[i]);
    else eng_node_inserted(k);
    dirty();
    return JS_DupValue(ctx, argv[0]);
}
static JSValue m_removechild(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)c;
    node* p = N(this_val);
    if (!p || argc < 1) return JS_UNDEFINED;
    node* k = check_child(argv[0]);
    if (!k) return JS_EXCEPTION;
    if (k->parent != p) return JS_ThrowTypeError(ctx, "NotFoundError: The node to be removed is not a child of this node");
    node_remove(k);
    eng_node_removed(k);
    dirty();
    return JS_DupValue(ctx, argv[0]);
}
static JSValue m_replacechild(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)c;
    node* p = N(this_val);
    if (!p || argc < 2) return JS_UNDEFINED;
    node* nw = check_child(argv[0]);
    node* old = check_child(argv[1]);
    if (!nw || !old) return JS_EXCEPTION;
    if (old->parent != p) return JS_ThrowTypeError(ctx, "NotFoundError");
    if (nw == old) return JS_DupValue(ctx, argv[1]);
    node_insert_before(p, nw, old);
    node_remove(old);
    eng_node_removed(old);
    eng_node_inserted(nw);
    dirty();
    return JS_DupValue(ctx, argv[1]);
}
static JSValue m_remove(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)c; (void)argc; (void)argv;
    node* n = N(this_val);
    if (n && n->parent) { node_remove(n); eng_node_removed(n); dirty(); }
    return JS_UNDEFINED;
}
static JSValue m_clone(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)c;
    node* n = N(this_val);
    if (!n) return JS_UNDEFINED;
    node* r = clone_node(n, argc > 0 && JS_ToBool(ctx, argv[0]));
    return wrap(r);
}
static JSValue m_contains(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)c;
    node* n = N(this_val);
    node* o = argc > 0 ? N(argv[0]) : 0;
    return JS_NewBool(ctx, n && o && node_contains(n, o));
}
static JSValue m_haschild(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) { (void)c; (void)argc; (void)argv; node* n = N(this_val); return JS_NewBool(ctx, n && n->first); }
static JSValue m_getattr(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)c;
    node* n = N(this_val);
    if (!n || argc < 1) return JS_NULL;
    char* k = cstr_or(argv[0], "");
    const char* v = attr_get(n, k);
    if (!v && ieq(k, "value") && n->value && 0) v = n->value;
    return v ? jstr(v) : JS_NULL;
}
static JSValue m_setattr(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)c;
    node* n = N(this_val);
    if (!n || argc < 2) return JS_UNDEFINED;
    char* k = cstr_or(argv[0], "");
    char* v = cstr_or(argv[1], "");
    attr_set(n, k, v);
    if (ieq(k, "class") || ieq(k, "id") || ieq(k, "style") || istarts(k, "data-") || istarts(k, "aria-") || ieq(k, "open") || ieq(k, "hidden") || ieq(k, "disabled") || ieq(k, "type") || ieq(k, "checked")) css_bump();
    if ((ieq(k, "src") || ieq(k, "srcset") || ieq(k, "data-src")) && (n->tag == T_IMG || n->tag == T_SCRIPT || n->tag == T_SOURCE)) eng_node_inserted(n);
    if (ieq(k, "href") && n->tag == T_LINK) { n->value = 0; css_bump(); }
    dirty();
    return JS_UNDEFINED;
}
static JSValue m_removeattr(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)c;
    node* n = N(this_val);
    if (!n || argc < 1) return JS_UNDEFINED;
    char* k = cstr_or(argv[0], "");
    attr_del(n, k);
    css_bump();
    dirty();
    return JS_UNDEFINED;
}
static JSValue m_hasattr(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)c;
    node* n = N(this_val);
    if (!n || argc < 1) return JS_FALSE;
    char* k = cstr_or(argv[0], "");
    return JS_NewBool(ctx, attr_get(n, k) != 0);
}
static JSValue m_attrnames(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)c; (void)argc; (void)argv;
    node* n = N(this_val);
    JSValue a = JS_NewArray(ctx);
    if (n) for (int i = 0; i < n->nattrs; i++) JS_SetPropertyUint32(ctx, a, (uint32_t)i, jstr(n->attrs[i].name));
    return a;
}
static JSValue m_qs(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv, int all) {
    (void)c;
    node* n = N(this_val);
    if (!n || argc < 1) return all ? JS_NewArray(ctx) : JS_NULL;
    char* s = cstr_or(argv[0], "");
    node** res = 0;
    int cnt = 0;
    node* r = qsel(n, s, all, &res, &cnt);
    if (cnt < 0) return JS_ThrowSyntaxError(ctx, "'%s' is not a valid selector", s);
    if (!all) return wrap(r);
    return node_list(res, cnt);
}
static JSValue m_qs1(JSContext* c, JSValueConst t, int a, JSValueConst* v) { return m_qs(c, t, a, v, 0); }
static JSValue m_qsa(JSContext* c, JSValueConst t, int a, JSValueConst* v) { return m_qs(c, t, a, v, 1); }
static JSValue m_bytag(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    (void)c;
    node* n = N(this_val);
    if (!n || argc < 1) return JS_NewArray(ctx);
    char* s = cstr_or(argv[0], "");
    JSValue arr = JS_NewArray(ctx);
    uint32_t k = 0;
    node* stack[2048];
    int sp = 0;
    for (node* q = n->last; q; q = q->prev) if (sp < 2048) stack[sp++] = q;
    char* classes[16];
    int ncl = 0;
    if (magic == 1) { char* p = s; while (*p && ncl < 16) { while (is_ws((unsigned char)*p)) p++; if (!*p) break; classes[ncl++] = p; while (*p && !is_ws((unsigned char)*p)) p++; if (*p) *p++ = 0; } }
    while (sp > 0) {
        node* q = stack[--sp];
        if (q->type != NT_ELEM) continue;
        int ok;
        if (magic == 0) ok = !strcmp(s, "*") || ieq(q->name, s);
        else if (magic == 1) { ok = ncl > 0; for (int i = 0; i < ncl && ok; i++) if (!has_class(q, classes[i])) ok = 0; }
        else { const char* nm = attr_get(q, "name"); ok = nm && !strcmp(nm, s); }
        if (ok) JS_SetPropertyUint32(ctx, arr, k++, wrap(q));
        for (node* d = q->last; d; d = d->prev) if (sp < 2048) stack[sp++] = d;
    }
    return arr;
}
static JSValue m_closest(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)c;
    node* n = N(this_val);
    if (!n || argc < 1) return JS_NULL;
    char* s = cstr_or(argv[0], "");
    for (node* p = n; p && p->type == NT_ELEM; p = p->parent) { int err = 0; if (sel_match_str(p, s, &err)) return wrap(p); if (err) return JS_ThrowSyntaxError(ctx, "invalid selector"); }
    return JS_NULL;
}
static JSValue m_matches(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)c;
    node* n = N(this_val);
    if (!n || argc < 1 || n->type != NT_ELEM) return JS_FALSE;
    char* s = cstr_or(argv[0], "");
    int err = 0;
    int r = sel_match_str(n, s, &err);
    if (err) return JS_ThrowSyntaxError(ctx, "invalid selector");
    return JS_NewBool(ctx, r);
}
static JSValue m_rect(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)c; (void)argc; (void)argv;
    node* n = N(this_val);
    float x = 0, y = 0, w = 0, h = 0;
    js_flush_n(n);
    if (n) box_rect(n, &x, &y, &w, &h);
    y -= G.scroll;
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "x", JS_NewFloat64(ctx, x));
    JS_SetPropertyStr(ctx, o, "y", JS_NewFloat64(ctx, y));
    JS_SetPropertyStr(ctx, o, "left", JS_NewFloat64(ctx, x));
    JS_SetPropertyStr(ctx, o, "top", JS_NewFloat64(ctx, y));
    JS_SetPropertyStr(ctx, o, "width", JS_NewFloat64(ctx, w));
    JS_SetPropertyStr(ctx, o, "height", JS_NewFloat64(ctx, h));
    JS_SetPropertyStr(ctx, o, "right", JS_NewFloat64(ctx, x + w));
    JS_SetPropertyStr(ctx, o, "bottom", JS_NewFloat64(ctx, y + h));
    return o;
}
static JSValue m_focus(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    (void)c; (void)argc; (void)argv;
    node* n = N(this_val);
    if (!n) return JS_UNDEFINED;
    if (magic == 0) { if (G.focus != n) { G.focus = n; n->caret = n->vlen; css_bump(); dirty(); } }
    else if (G.focus == n) { G.focus = 0; css_bump(); dirty(); }
    return JS_UNDEFINED;
}
static JSValue m_adjacent(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    (void)c;
    node* n = N(this_val);
    if (!n || argc < 2) return JS_UNDEFINED;
    char* pos = cstr_or(argv[0], "");
    node* f;
    if (magic == 0) { char* s = cstr_or(argv[1], ""); f = parse_fragment(ieq(pos, "beforebegin") || ieq(pos, "afterend") ? n->parent : n, s, (int)strlen(s)); }
    else if (magic == 1) { f = N(argv[1]); if (!f) return JS_NULL; }
    else { char* s = cstr_or(argv[1], ""); f = text_new(s, (int)strlen(s)); }
    node* items[4096];
    int ni = 0;
    if (f->type == NT_FRAG) { for (node* q = f->first; q && ni < 4096; q = q->next) items[ni++] = q; }
    else items[ni++] = f;
    for (int i = 0; i < ni; i++) {
        node* k = items[i];
        if (ieq(pos, "beforebegin")) { if (n->parent) node_insert_before(n->parent, k, n); }
        else if (ieq(pos, "afterbegin")) node_insert_before(n, k, n->first);
        else if (ieq(pos, "beforeend")) node_append(n, k);
        else if (ieq(pos, "afterend")) { if (n->parent) node_insert_before(n->parent, k, n->next); }
        eng_node_inserted(k);
    }
    dirty();
    return magic == 1 ? JS_DupValue(ctx, argv[1]) : JS_UNDEFINED;
}
static JSValue m_scrollinto(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)c; (void)argc; (void)argv;
    node* n = N(this_val);
    float x, y, w, h;
    if (n) { box_rect(n, &x, &y, &w, &h); eng_scroll_to(y); }
    return JS_UNDEFINED;
}

static JSValue d_create(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    (void)c; (void)this_val;
    char* s = argc > 0 ? cstr_or(argv[argc > 1 && magic == 3 ? 1 : 0], "") : nstrdup("");
    node* n;
    switch (magic) {
    case 0: case 3: n = elem_new(s); if (magic == 3) { memcpy(n->name, s, strlen(s) + 1); n->tag = tag_id(s); } break;
    case 1: n = text_new(s, (int)strlen(s)); break;
    case 2: n = node_new(NT_COMMENT); n->text = s; n->tlen = (int)strlen(s); break;
    default: n = node_new(NT_FRAG); break;
    }
    if (magic == 0 || magic == 3) n->flags |= 0;
    return wrap(n);
}
static JSValue d_byid(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)c;
    node* n = N(this_val);
    if (!n || argc < 1) return JS_NULL;
    char* s = cstr_or(argv[0], "");
    return wrap(find_id(n, s));
}
static JSValue d_write(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    (void)c; (void)this_val;
    for (int i = 0; i < argc; i++) { char* s = cstr_or(argv[i], ""); doc_write(s, (int)strlen(s)); }
    if (magic) doc_write("\n", 1);
    dirty();
    return JS_UNDEFINED;
}
static JSValue d_frompoint(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)c; (void)this_val;
    double x = 0, y = 0;
    if (argc > 1) { JS_ToFloat64(ctx, &x, argv[0]); JS_ToFloat64(ctx, &y, argv[1]); }
    eng_layout_if_dirty();
    frag* f;
    box* b = hit_test((float)x, (float)y + G.scroll, &f);
    node* n = b ? b->n : 0;
    while (n && n->type != NT_ELEM) n = n->parent;
    return wrap(n);
}

static JSValue n_settimer(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    (void)c; (void)this_val;
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) return JS_NewInt32(ctx, 0);
    double ms = 0;
    if (argc > 1) JS_ToFloat64(ctx, &ms, argv[1]);
    if (!(ms >= 0)) ms = 0;
    if (ntimers >= 50000) return JS_NewInt32(ctx, 0);
    if (ntimers >= ctimers) {
        int nc = ctimers ? ctimers * 2 : 64;
        timer* nt = nalloc(sizeof(timer) * (size_t)nc);
        if (ntimers) memcpy(nt, timers, sizeof(timer) * (size_t)ntimers);
        if (timers) nfree(timers);
        timers = nt;
        ctimers = nc;
    }
    timer* t = &timers[ntimers++];
    t->id = ++next_tid;
    t->fn = JS_DupValue(ctx, argv[0]);
    t->ms = (int)ms;
    if (magic == 1 && t->ms < 4) t->ms = 4;
    t->due = nwk_ms() + (unsigned long)t->ms;
    t->interval = magic == 1;
    t->raf = magic == 2;
    t->active = 1;
    return JS_NewInt32(ctx, t->id);
}
static JSValue n_cleartimer(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)c; (void)this_val;
    int32_t id = 0;
    if (argc > 0) JS_ToInt32(ctx, &id, argv[0]);
    for (int i = 0; i < ntimers; i++) if (timers[i].id == id && timers[i].active) { timers[i].active = 0; JS_FreeValue(ctx, timers[i].fn); timers[i].fn = JS_UNDEFINED; }
    return JS_UNDEFINED;
}
static JSValue n_fetch(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)c; (void)this_val;
    char* url = argc > 0 ? cstr_or(argv[0], "") : nstrdup("");
    char* method = argc > 1 ? cstr_or(argv[1], "GET") : nstrdup("GET");
    char* body = 0;
    int blen = 0;
    if (argc > 2 && JS_IsString(argv[2])) { body = cstr(argv[2]); blen = body ? (int)strlen(body) : 0; }
    else if (argc > 2 && JS_IsObject(argv[2])) { size_t l = 0; uint8_t* p = jbytes(argv[2], &l); if (p) { body = nalloc(l + 1); memcpy(body, p, l); body[l] = 0; blen = (int)l; } }
    char* ctype = argc > 3 ? cstr(argv[3]) : 0;
    int bin = argc > 4 && JS_ToBool(ctx, argv[4]);
    char full[2048];
    url_resolve(G.base, url, full, sizeof(full));
    JSValue arr = JS_NewArray(ctx);
    if (istarts(full, "data:") && bin) {
        extern unsigned char* data_url(const char* u, int* len, int* is_text);
        int dl = 0;
        unsigned char* d = data_url(full, &dl, 0);
        JS_SetPropertyUint32(ctx, arr, 0, JS_NewInt32(ctx, d ? 200 : 400));
        JS_SetPropertyUint32(ctx, arr, 1, JS_NewArrayBufferCopy(ctx, d ? d : (const unsigned char*)"", d ? (size_t)dl : 0));
        JS_SetPropertyUint32(ctx, arr, 2, jstr("text/plain"));
        JS_SetPropertyUint32(ctx, arr, 3, jstr(full));
        return arr;
    }
    if (istarts(full, "data:")) {
        const char* comma = strchr(full, ',');
        JS_SetPropertyUint32(ctx, arr, 0, JS_NewInt32(ctx, comma ? 200 : 400));
        JS_SetPropertyUint32(ctx, arr, 1, jstr(comma ? comma + 1 : ""));
        JS_SetPropertyUint32(ctx, arr, 2, jstr("text/plain"));
        JS_SetPropertyUint32(ctx, arr, 3, jstr(full));
        return arr;
    }
    nwk_resp r;
    memset(&r, 0, sizeof(r));
    int ok = nwk_fetch(full, method, ctype, body, blen, G.url, &r);
    JS_SetPropertyUint32(ctx, arr, 0, JS_NewInt32(ctx, ok >= 0 ? r.status : 0));
    if (bin) JS_SetPropertyUint32(ctx, arr, 1, JS_NewArrayBufferCopy(ctx, (const uint8_t*)(ok >= 0 && r.body ? r.body : ""), ok >= 0 && r.body ? (size_t)r.len : 0));
    else JS_SetPropertyUint32(ctx, arr, 1, ok >= 0 && r.body ? jstrn(r.body, r.len) : jstr(""));
    JS_SetPropertyUint32(ctx, arr, 2, jstr(r.ctype));
    JS_SetPropertyUint32(ctx, arr, 3, jstr(r.final_url[0] ? r.final_url : full));
    if (r.body) nwk_free(r.body);
    return arr;
}
static JSValue n_cookie(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)c; (void)this_val;
    if (argc > 0) { char* s = cstr_or(argv[0], ""); nwk_cookie_set(G.url, s, 1); return JS_UNDEFINED; }
    char buf[8192];
    buf[0] = 0;
    nwk_cookie_get(G.url, 1, buf, sizeof(buf));
    return jstr(buf);
}
static JSValue n_store(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    (void)c; (void)this_val;
    int session = argc > 0 ? JS_ToBool(ctx, argv[0]) : 0;
    char* k = argc > 1 ? cstr_or(argv[1], "") : nstrdup("");
    switch (magic) {
    case 0: { char* buf = nalloc(65536); buf[0] = 0; int r = nwk_store_get(G.origin, session, k, buf, 65536); JSValue v = r >= 0 ? jstr(buf) : JS_NULL; nfree(buf); return v; }
    case 1: { char* v = argc > 2 ? cstr_or(argv[2], "") : nstrdup(""); nwk_store_set(G.origin, session, k, v); return JS_UNDEFINED; }
    case 2: nwk_store_set(G.origin, session, k, 0); return JS_UNDEFINED;
    case 3: nwk_store_clear(G.origin, session); return JS_UNDEFINED;
    case 4: { int32_t i = 0; if (argc > 1) JS_ToInt32(ctx, &i, argv[1]); char buf[512]; if (nwk_store_key(G.origin, session, i, buf, sizeof(buf)) < 0) return JS_NULL; return jstr(buf); }
    case 5: return JS_NewInt32(ctx, nwk_store_len(G.origin, session));
    }
    return JS_UNDEFINED;
}
static void mut_hook(int k, node* t, node* c, node* pv, node* nx, const char* nm, const char* old);
static JSValue n_misc(JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    (void)c; (void)this_val;
    switch (magic) {
    case 0: { char* s = argc > 0 ? cstr_or(argv[0], "") : nstrdup(""); int lvl = 0; if (argc > 1) JS_ToInt32(ctx, &lvl, argv[1]); char b[1100]; snprintf(b, sizeof(b), "%s%.1000s", lvl >= 2 ? "console.error: " : lvl == 1 ? "console.warn: " : "console: ", s); nwk_log(b); return JS_UNDEFINED; }
    case 1: { char* u = argc > 0 ? cstr_or(argv[0], "") : nstrdup(""); char full[2048]; url_resolve(G.base, u, full, sizeof(full)); eng_navigate(full, "GET", 0, 0); return JS_UNDEFINED; }
    case 2: return jstr(G.url);
    case 3: { char* q = argc > 0 ? cstr_or(argv[0], "") : nstrdup(""); return JS_NewBool(ctx, css_media_match(q, (int)strlen(q))); }
    case 4: return JS_NewFloat64(ctx, (double)(nwk_ms() - G.t0));
    case 5: return JS_NewFloat64(ctx, (double)nwk_epoch_ms());
    case 6: { if (argc < 2) return jstr(""); node* n = N(argv[0]); char* p = cstr_or(argv[1], ""); char buf[1024]; buf[0] = 0; if (n) { js_flush_n(n); if (!n->st) eng_layout_if_dirty(); css_computed_get(n, p, buf, sizeof(buf)); } return jstr(buf); }
    case 7: { if (argc < 2) return jstr(""); node* n = N(argv[0]); char* p = cstr_or(argv[1], ""); char buf[1024]; buf[0] = 0; if (n) css_prop_get(n, p, buf, sizeof(buf)); return jstr(buf); }
    case 8: { if (argc < 3) return JS_UNDEFINED; node* n = N(argv[0]); char* p = cstr_or(argv[1], ""); char* v = cstr_or(argv[2], ""); if (n) { css_decl_apply_inline(n, p, v); dirty(); } return JS_UNDEFINED; }
    case 9: { double y = 0; if (argc > 0) JS_ToFloat64(ctx, &y, argv[0]); eng_scroll_to((float)y); return JS_UNDEFINED; }
    case 10: return JS_NewFloat64(ctx, G.scroll);
    case 11: return JS_NewInt32(ctx, (int)G.vw);
    case 12: return JS_NewInt32(ctx, (int)G.vh);
    case 13: { char* s = argc > 0 ? cstr_or(argv[0], "") : nstrdup(""); nwk_alert(s); return JS_UNDEFINED; }
    case 14: if (argc > 0) { char* s = cstr_or(argv[0], ""); snprintf(G.title, sizeof(G.title), "%s", s); node* h = doc_head(); if (h) { node* t = 0; for (node* q = h->first; q; q = q->next) if (q->tag == T_TITLE) t = q; if (!t) { t = elem_new("title"); node_append(h, t); } while (t->first) node_remove(t->first); node_append(t, text_new(s, (int)strlen(s))); } return JS_UNDEFINED; } return jstr(G.title);
    case 15: return jstr(G.loaded >= 2 ? "complete" : G.loaded == 1 ? "interactive" : "loading");
    case 16: { char* u = argc > 0 ? cstr_or(argv[0], "") : nstrdup(""); char full[2048]; url_resolve(G.base, u, full, sizeof(full)); snprintf(G.url, sizeof(G.url), "%s", full); return JS_UNDEFINED; }
    case 17: { uint32_t n = 16; if (argc > 0) JS_ToUint32(ctx, &n, argv[0]); if (n > 65536) n = 65536; JSValue a = JS_NewArray(ctx); static unsigned long long seed = 0x9E3779B97F4A7C15ull; seed ^= nwk_ms() * 0x100000001B3ull; for (uint32_t i = 0; i < n; i++) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; JS_SetPropertyUint32(ctx, a, i, JS_NewInt32(ctx, (int)(seed & 255))); } return a; }
    case 18: return wrap(G.focus ? G.focus : doc_body());
    case 19: return wrap(G.cur_script);
    case 20: { char* u = argc > 0 ? cstr_or(argv[0], "") : nstrdup(""); char* b = argc > 1 ? cstr_or(argv[1], "") : nstrdup(""); char full[2048]; url_resolve(G.base, u, full, sizeof(full)); eng_navigate(full, "POST", b, (int)strlen(b)); return JS_UNDEFINED; }
    case 21: { eng_layout_if_dirty(); return JS_NewInt32(ctx, (int)g_doch); }
    case 22: { if (argc < 2) return JS_UNDEFINED; char* tag = cstr_or(argv[0], ""); node* n = elem_new(tag); JSValue o = wrap(n); JS_SetPrototype(ctx, o, argv[1]); return o; }
    case 23: { if (argc < 2) return JS_UNDEFINED; char* tag = cstr_or(argv[0], ""); int t = tag_id(tag); if (t > 0 && t < T_COUNT) { JS_FreeValue(ctx, tagproto[t]); tagproto[t] = JS_DupValue(ctx, argv[1]); } return JS_UNDEFINED; }
    case 24: { if (argc < 1) return JS_UNDEFINED; node* n = N(argv[0]); if (!n) return JS_UNDEFINED; if (G.focus == n) return JS_TRUE; return JS_FALSE; }
    case 25: { char* s = argc > 0 ? cstr_or(argv[0], "") : nstrdup(""); nwk_status(s); return JS_UNDEFINED; }
    case 26: return JS_NewFloat64(ctx, (double)nwk_tz_min());
    case 27: return jstr(G.origin);
    case 28: { if (argc < 1) return JS_NULL; node* n = N(argv[0]); if (!n) return JS_NULL; node* f = node_new(NT_FRAG); for (node* c2 = n->first; c2; c2 = c2->next) node_append(f, clone_node(c2, 1)); return wrap(f); }
    case 29: { char* s = argc > 0 ? cstr_or(argv[0], "") : nstrdup(""); node* d = node_new(NT_DOC); node* save = G.doc; G.doc = d; parse_html(d, s, (int)strlen(s), 0); G.doc = save; return wrap(d); }
    case 30: { char* u = argc > 0 ? cstr_or(argv[0], "") : nstrdup(""); char* b = argc > 1 ? cstr_or(argv[1], G.base) : nstrdup(G.base); char full[2048]; url_resolve(b, u, full, sizeof(full)); return jstr(full); }
    case 31: { if (argc < 1) return JS_UNDEFINED; node* f = N(argv[0]); node* sub = argc > 1 ? N(argv[1]) : 0; if (f) eng_form_submit(f, sub); return JS_UNDEFINED; }
    case 32: { if (argc < 1) return JS_UNDEFINED; node* n = N(argv[0]); extern void eng_synthetic_click(node* n); if (n) eng_synthetic_click(n); return JS_UNDEFINED; }
    case 33: { int on = 0; if (argc > 0) JS_ToInt32(ctx, &on, argv[0]); g_mut_on = on; g_mut_hook = mut_hook; return JS_UNDEFINED; }
    case 34: { node* n = argc > 0 ? N(argv[0]) : 0; int o = 0; if (argc > 1) JS_ToInt32(ctx, &o, argv[1]); g_ce_node = n; g_ce_off = o; G.dirty_paint = 1; return JS_UNDEFINED; }
    case 39: {
        node* n = argc > 0 ? N(argv[0]) : 0;
        if (!n || n->type != NT_ELEM) return JS_NULL;
        if (!n->shadow) { node* f = node_new(NT_FRAG); f->host = n; n->shadow = f; G.dirty_style = 1; css_bump(); }
        return wrap(n->shadow);
    }
    case 40: { node* n = argc > 0 ? N(argv[0]) : 0; if (!n || !n->shadow) return JS_NULL; return wrap(n->shadow); }
    case 41: { node* n = argc > 0 ? N(argv[0]) : 0; if (!n || !n->host) return JS_NULL; return wrap(n->host); }
    case 37: {
        size_t l = 0;
        uint8_t* p = argc > 0 ? jbytes(argv[0], &l) : 0;
        if (!p) return jstr("");
        return JS_NewStringLen(ctx, (const char*)p, l);
    }
    case 38: {
        size_t l = 0;
        const char* s = argc > 0 ? JS_ToCStringLen(ctx, &l, argv[0]) : 0;
        JSValue r = JS_NewArrayBufferCopy(ctx, (const uint8_t*)(s ? s : ""), s ? l : 0);
        if (s) JS_FreeCString(ctx, s);
        return r;
    }
    case 36: {
        int op = 0;
        if (argc < 2) return JS_UNDEFINED;
        JS_ToInt32(ctx, &op, argv[0]);
        node* n = N(argv[1]);
        if (!n) return JS_NewInt32(ctx, -1);
        if (op == 0) {
            char* u = argc > 2 ? cstr_or(argv[2], "") : nstrdup("");
            int split = 0, paused = 0;
            double st = 0;
            if (argc > 3) JS_ToInt32(ctx, &split, argv[3]);
            if (argc > 4) JS_ToFloat64(ctx, &st, argv[4]);
            if (argc > 5) JS_ToInt32(ctx, &paused, argv[5]);
            return JS_NewInt32(ctx, wm_create(n, u, split, (long long)st, paused));
        }
        int id = wm_id(n);
        if (op == 6) { wm_free(n); G.dirty_paint = 1; return JS_UNDEFINED; }
        if (id < 0) return op == 2 ? JS_NULL : JS_NewInt32(ctx, -1);
        if (op == 1) { int cmd = 0; double a = 0, b = 0; if (argc > 2) JS_ToInt32(ctx, &cmd, argv[2]); if (argc > 3) JS_ToFloat64(ctx, &a, argv[3]); if (argc > 4) JS_ToFloat64(ctx, &b, argv[4]); nwk_media_cmd(id, cmd, (long long)a, (long long)b); G.dirty_paint = 1; return JS_UNDEFINED; }
        if (op == 2) {
            long long v[6] = { 0, -1, 0, 0, 0, 0 };
            if (nwk_media_status(id, v) < 0) return JS_NULL;
            JSValue a = JS_NewArray(ctx);
            for (int i = 0; i < 6; i++) JS_SetPropertyUint32(ctx, a, (uint32_t)i, JS_NewFloat64(ctx, (double)v[i]));
            return a;
        }
        if (op == 3) {
            int stream = 0;
            if (argc > 2) JS_ToInt32(ctx, &stream, argv[2]);
            size_t len = 0;
            uint8_t* d = 0;
            if (argc > 3) d = jbytes(argv[3], &len);
            if (!d || !len) return JS_NewInt32(ctx, 0);
            return JS_NewInt32(ctx, nwk_media_append(id, stream, d, (int)len));
        }
        if (op == 4) { int stream = 0; if (argc > 2) JS_ToInt32(ctx, &stream, argv[2]); nwk_media_end(id, stream); return JS_UNDEFINED; }
        if (op == 5) { double st = 0; int keep = 0; if (argc > 2) JS_ToFloat64(ctx, &st, argv[2]); if (argc > 3) JS_ToInt32(ctx, &keep, argv[3]); return JS_NewInt32(ctx, nwk_media_restart(id, (long long)st, keep)); }
        if (op == 7) { int stream = 0; if (argc > 2) JS_ToInt32(ctx, &stream, argv[2]); return JS_NewFloat64(ctx, (double)nwk_media_len(id, stream)); }
        return JS_UNDEFINED;
    }
    case 35: { node* n = argc > 0 ? N(argv[0]) : 0; int o = 0; if (argc > 1) JS_ToInt32(ctx, &o, argv[1]); float x, y, h; eng_layout_if_dirty(); if (!n || !caret_pos(n, o, &x, &y, &h)) return JS_NULL; JSValue a = JS_NewArray(ctx); JS_SetPropertyUint32(ctx, a, 0, JS_NewFloat64(ctx, x)); JS_SetPropertyUint32(ctx, a, 1, JS_NewFloat64(ctx, y - G.scroll)); JS_SetPropertyUint32(ctx, a, 2, JS_NewFloat64(ctx, h)); return a; }
    }
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry node_funcs[] = {
    JS_CGETSET_MAGIC_DEF("nodeType", g_get, 0, G_NODETYPE),
    JS_CGETSET_MAGIC_DEF("nodeName", g_get, 0, G_NODENAME),
    JS_CGETSET_MAGIC_DEF("textContent", g_get, g_set, G_TEXTCONTENT),
    JS_CGETSET_MAGIC_DEF("childNodes", g_get, 0, G_CHILDNODES),
    JS_CGETSET_MAGIC_DEF("firstChild", g_get, 0, G_FIRSTCHILD),
    JS_CGETSET_MAGIC_DEF("lastChild", g_get, 0, G_LASTCHILD),
    JS_CGETSET_MAGIC_DEF("nextSibling", g_get, 0, G_NEXTSIB),
    JS_CGETSET_MAGIC_DEF("previousSibling", g_get, 0, G_PREVSIB),
    JS_CGETSET_MAGIC_DEF("parentNode", g_get, 0, G_PARENTNODE),
    JS_CGETSET_MAGIC_DEF("parentElement", g_get, 0, G_PARENTEL),
    JS_CGETSET_MAGIC_DEF("ownerDocument", g_get, 0, G_OWNERDOC),
    JS_CGETSET_MAGIC_DEF("isConnected", g_get, 0, G_ISCONNECTED),
    JS_CGETSET_MAGIC_DEF("nodeValue", g_get, g_set, G_NODEVALUE),
    JS_CGETSET_MAGIC_DEF("data", g_get, g_set, G_NODEVALUE),
    JS_CFUNC_DEF("appendChild", 1, m_append),
    JS_CFUNC_DEF("insertBefore", 2, m_insert),
    JS_CFUNC_DEF("removeChild", 1, m_removechild),
    JS_CFUNC_DEF("replaceChild", 2, m_replacechild),
    JS_CFUNC_DEF("cloneNode", 1, m_clone),
    JS_CFUNC_DEF("contains", 1, m_contains),
    JS_CFUNC_DEF("hasChildNodes", 0, m_haschild),
    JS_CFUNC_DEF("__remove", 0, m_remove),
    JS_CFUNC_DEF("querySelector", 1, m_qs1),
    JS_CFUNC_DEF("querySelectorAll", 1, m_qsa),
    JS_CFUNC_MAGIC_DEF("getElementsByTagName", 1, m_bytag, 0),
    JS_CFUNC_MAGIC_DEF("getElementsByClassName", 1, m_bytag, 1),
    JS_CGETSET_MAGIC_DEF("children", g_get, 0, G_CHILDREN),
    JS_CGETSET_MAGIC_DEF("firstElementChild", g_get, 0, G_FIRSTEL),
    JS_CGETSET_MAGIC_DEF("lastElementChild", g_get, 0, G_LASTEL),
    JS_CGETSET_MAGIC_DEF("childElementCount", g_get, 0, G_CHILDCOUNT),
};

#define R(nm, i) JS_CGETSET_MAGIC_DEF(nm, g_get, g_set, i)
static const JSCFunctionListEntry elem_funcs[] = {
    R("id", 0), R("className", 1), R("name", 2), R("type", 3), R("alt", 4), R("title", 5), R("placeholder", 6), R("htmlFor", 7), R("action", 8), R("method", 9),
    R("target", 10), R("rel", 11), R("lang", 12), R("dir", 13), R("content", 14), R("label", 15), R("role", 16), R("download", 17), R("accept", 18), R("autocomplete", 19),
    R("pattern", 20), R("srcset", 21), R("sizes", 22), R("loading", 23), R("media", 24), R("hreflang", 25), R("charset", 26), R("async", 27), R("enctype", 28),
    R("cols", 32), R("rows", 33), R("colSpan", 34), R("rowSpan", 35), R("poster", 45), R("crossOrigin", 41), R("referrerPolicy", 42), R("integrity", 43), R("nonce", 44),
    JS_CGETSET_MAGIC_DEF("tagName", g_get, 0, G_TAGNAME),
    JS_CGETSET_MAGIC_DEF("localName", g_get, 0, G_LOCALNAME),
    JS_CGETSET_MAGIC_DEF("innerHTML", g_get, g_set, G_INNERHTML),
    JS_CGETSET_MAGIC_DEF("outerHTML", g_get, g_set, G_OUTERHTML),
    JS_CGETSET_MAGIC_DEF("innerText", g_get, g_set, G_INNERTEXT),
    JS_CGETSET_MAGIC_DEF("outerText", g_get, g_set, G_INNERTEXT),
    JS_CGETSET_MAGIC_DEF("nextElementSibling", g_get, 0, G_NEXTEL),
    JS_CGETSET_MAGIC_DEF("previousElementSibling", g_get, 0, G_PREVEL),
    JS_CGETSET_MAGIC_DEF("value", g_get, g_set, G_VALUE),
    JS_CGETSET_MAGIC_DEF("valueAsNumber", g_get, 0, G_VALUEASNUM),
    JS_CGETSET_MAGIC_DEF("checked", g_get, g_set, G_CHECKED),
    JS_CGETSET_MAGIC_DEF("defaultChecked", g_get, g_set, G_DEFCHECKED),
    JS_CGETSET_MAGIC_DEF("disabled", g_get, g_set, G_DISABLED),
    JS_CGETSET_MAGIC_DEF("readOnly", g_get, g_set, G_READONLY),
    JS_CGETSET_MAGIC_DEF("required", g_get, g_set, G_REQUIRED),
    JS_CGETSET_MAGIC_DEF("multiple", g_get, g_set, G_MULTIPLE),
    JS_CGETSET_MAGIC_DEF("autofocus", g_get, g_set, G_AUTOFOCUS),
    JS_CGETSET_MAGIC_DEF("noValidate", g_get, g_set, G_NOVALIDATE),
    JS_CGETSET_MAGIC_DEF("defer", g_get, g_set, G_DEFER),
    JS_CGETSET_MAGIC_DEF("noModule", g_get, g_set, G_NOMODULE),
    JS_CGETSET_MAGIC_DEF("draggable", g_get, g_set, G_DRAGGABLE),
    JS_CGETSET_MAGIC_DEF("spellcheck", g_get, 0, G_SPELLCHECK),
    JS_CGETSET_MAGIC_DEF("contentEditable", g_get, g_set, G_CONTENTEDITABLE),
    JS_CGETSET_MAGIC_DEF("isContentEditable", g_get, 0, G_ISCONTENTEDITABLE),
    JS_CGETSET_MAGIC_DEF("maxLength", g_get, g_set, G_MAXLEN),
    JS_CGETSET_MAGIC_DEF("min", g_get, g_set, G_MIN),
    JS_CGETSET_MAGIC_DEF("max", g_get, g_set, G_MAX),
    JS_CGETSET_MAGIC_DEF("step", g_get, g_set, G_STEP),
    JS_CGETSET_MAGIC_DEF("selectedIndex", g_get, g_set, G_SELIDX),
    JS_CGETSET_MAGIC_DEF("selected", g_get, g_set, G_SELECTED),
    JS_CGETSET_MAGIC_DEF("defaultSelected", g_get, g_set, G_DEFSELECTED),
    JS_CGETSET_MAGIC_DEF("index", g_get, 0, G_INDEX),
    JS_CGETSET_MAGIC_DEF("text", g_get, g_set, G_TEXT),
    JS_CGETSET_MAGIC_DEF("href", g_get, g_set, G_HREF),
    JS_CGETSET_MAGIC_DEF("src", g_get, g_set, G_SRC),
    JS_CGETSET_MAGIC_DEF("currentSrc", g_get, 0, G_CURSRC),
    JS_CGETSET_MAGIC_DEF("hidden", g_get, g_set, G_HIDDEN),
    JS_CGETSET_MAGIC_DEF("offsetWidth", g_get, 0, G_OFFW),
    JS_CGETSET_MAGIC_DEF("offsetHeight", g_get, 0, G_OFFH),
    JS_CGETSET_MAGIC_DEF("offsetTop", g_get, 0, G_OFFT),
    JS_CGETSET_MAGIC_DEF("offsetLeft", g_get, 0, G_OFFL),
    JS_CGETSET_MAGIC_DEF("offsetParent", g_get, 0, G_OFFP),
    JS_CGETSET_MAGIC_DEF("clientWidth", g_get, 0, G_CLIW),
    JS_CGETSET_MAGIC_DEF("clientHeight", g_get, 0, G_CLIH),
    JS_CGETSET_MAGIC_DEF("clientTop", g_get, 0, G_CLITOP),
    JS_CGETSET_MAGIC_DEF("clientLeft", g_get, 0, G_CLILEFT),
    JS_CGETSET_MAGIC_DEF("scrollWidth", g_get, 0, G_SCRW),
    JS_CGETSET_MAGIC_DEF("scrollHeight", g_get, 0, G_SCRH),
    JS_CGETSET_MAGIC_DEF("scrollTop", g_get, g_set, G_SCRT),
    JS_CGETSET_MAGIC_DEF("scrollLeft", g_get, g_set, G_SCRL),
    JS_CGETSET_MAGIC_DEF("form", g_get, 0, G_FORMEL),
    JS_CGETSET_MAGIC_DEF("naturalWidth", g_get, 0, G_NATW),
    JS_CGETSET_MAGIC_DEF("naturalHeight", g_get, 0, G_NATH),
    JS_CGETSET_MAGIC_DEF("complete", g_get, 0, G_COMPLETE),
    JS_CGETSET_MAGIC_DEF("width", g_get, g_set, G_WIDTH),
    JS_CGETSET_MAGIC_DEF("height", g_get, g_set, G_HEIGHT),
    JS_CGETSET_MAGIC_DEF("tabIndex", g_get, g_set, G_TABINDEX),
    JS_CGETSET_MAGIC_DEF("defaultValue", g_get, g_set, G_DEFVALUE),
    JS_CGETSET_MAGIC_DEF("open", g_get, g_set, G_OPEN),
    JS_CGETSET_MAGIC_DEF("selectionStart", g_get, g_set, G_SELSTART),
    JS_CGETSET_MAGIC_DEF("selectionEnd", g_get, g_set, G_SELEND),
    JS_CGETSET_MAGIC_DEF("length", g_get, g_set, G_LENGTH),
    JS_CGETSET_MAGIC_DEF("options", g_get, 0, G_OPTIONS),
    JS_CGETSET_MAGIC_DEF("elements", g_get, 0, G_ELEMENTS),
    JS_CGETSET_MAGIC_DEF("attributes", g_get, 0, G_ATTRS),
    JS_CGETSET_MAGIC_DEF("__styleText", g_get, g_set, G_STYLETEXT),
    JS_CFUNC_DEF("getAttribute", 1, m_getattr),
    JS_CFUNC_DEF("setAttribute", 2, m_setattr),
    JS_CFUNC_DEF("removeAttribute", 1, m_removeattr),
    JS_CFUNC_DEF("hasAttribute", 1, m_hasattr),
    JS_CFUNC_DEF("getAttributeNames", 0, m_attrnames),
    JS_CFUNC_DEF("closest", 1, m_closest),
    JS_CFUNC_DEF("matches", 1, m_matches),
    JS_CFUNC_DEF("getBoundingClientRect", 0, m_rect),
    JS_CFUNC_MAGIC_DEF("focus", 0, m_focus, 0),
    JS_CFUNC_MAGIC_DEF("blur", 0, m_focus, 1),
    JS_CFUNC_MAGIC_DEF("insertAdjacentHTML", 2, m_adjacent, 0),
    JS_CFUNC_MAGIC_DEF("insertAdjacentElement", 2, m_adjacent, 1),
    JS_CFUNC_MAGIC_DEF("insertAdjacentText", 2, m_adjacent, 2),
    JS_CFUNC_DEF("scrollIntoView", 1, m_scrollinto),
    JS_CFUNC_MAGIC_DEF("getElementsByName", 1, m_bytag, 2),
};
#undef R

static const JSCFunctionListEntry doc_funcs[] = {
    JS_CFUNC_MAGIC_DEF("createElement", 1, d_create, 0),
    JS_CFUNC_MAGIC_DEF("createElementNS", 2, d_create, 3),
    JS_CFUNC_MAGIC_DEF("createTextNode", 1, d_create, 1),
    JS_CFUNC_MAGIC_DEF("createComment", 1, d_create, 2),
    JS_CFUNC_MAGIC_DEF("createDocumentFragment", 0, d_create, 4),
    JS_CFUNC_DEF("getElementById", 1, d_byid),
    JS_CFUNC_MAGIC_DEF("write", 1, d_write, 0),
    JS_CFUNC_MAGIC_DEF("writeln", 1, d_write, 1),
    JS_CFUNC_DEF("elementFromPoint", 2, d_frompoint),
    JS_CFUNC_MAGIC_DEF("getElementsByName", 1, m_bytag, 2),
};

static const JSCFunctionListEntry native_funcs[] = {
    JS_CFUNC_MAGIC_DEF("setTimeout", 2, n_settimer, 0),
    JS_CFUNC_MAGIC_DEF("setInterval", 2, n_settimer, 1),
    JS_CFUNC_MAGIC_DEF("raf", 1, n_settimer, 2),
    JS_CFUNC_DEF("clearTimer", 1, n_cleartimer),
    JS_CFUNC_DEF("fetch", 4, n_fetch),
    JS_CFUNC_DEF("cookie", 1, n_cookie),
    JS_CFUNC_MAGIC_DEF("storeGet", 2, n_store, 0),
    JS_CFUNC_MAGIC_DEF("storeSet", 3, n_store, 1),
    JS_CFUNC_MAGIC_DEF("storeRemove", 2, n_store, 2),
    JS_CFUNC_MAGIC_DEF("storeClear", 1, n_store, 3),
    JS_CFUNC_MAGIC_DEF("storeKey", 2, n_store, 4),
    JS_CFUNC_MAGIC_DEF("storeLen", 1, n_store, 5),
    JS_CFUNC_MAGIC_DEF("log", 2, n_misc, 0),
    JS_CFUNC_MAGIC_DEF("navigate", 1, n_misc, 1),
    JS_CFUNC_MAGIC_DEF("url", 0, n_misc, 2),
    JS_CFUNC_MAGIC_DEF("media", 1, n_misc, 3),
    JS_CFUNC_MAGIC_DEF("now", 0, n_misc, 4),
    JS_CFUNC_MAGIC_DEF("epoch", 0, n_misc, 5),
    JS_CFUNC_MAGIC_DEF("computed", 2, n_misc, 6),
    JS_CFUNC_MAGIC_DEF("styleGet", 2, n_misc, 7),
    JS_CFUNC_MAGIC_DEF("styleSet", 3, n_misc, 8),
    JS_CFUNC_MAGIC_DEF("scrollTo", 1, n_misc, 9),
    JS_CFUNC_MAGIC_DEF("scrollY", 0, n_misc, 10),
    JS_CFUNC_MAGIC_DEF("vw", 0, n_misc, 11),
    JS_CFUNC_MAGIC_DEF("vh", 0, n_misc, 12),
    JS_CFUNC_MAGIC_DEF("alert", 1, n_misc, 13),
    JS_CFUNC_MAGIC_DEF("title", 1, n_misc, 14),
    JS_CFUNC_MAGIC_DEF("readyState", 0, n_misc, 15),
    JS_CFUNC_MAGIC_DEF("setUrl", 1, n_misc, 16),
    JS_CFUNC_MAGIC_DEF("random", 1, n_misc, 17),
    JS_CFUNC_MAGIC_DEF("active", 0, n_misc, 18),
    JS_CFUNC_MAGIC_DEF("currentScript", 0, n_misc, 19),
    JS_CFUNC_MAGIC_DEF("post", 2, n_misc, 20),
    JS_CFUNC_MAGIC_DEF("docHeight", 0, n_misc, 21),
    JS_CFUNC_MAGIC_DEF("newElement", 2, n_misc, 22),
    JS_CFUNC_MAGIC_DEF("setProto", 2, n_misc, 23),
    JS_CFUNC_MAGIC_DEF("hasFocus", 1, n_misc, 24),
    JS_CFUNC_MAGIC_DEF("status", 1, n_misc, 25),
    JS_CFUNC_MAGIC_DEF("tz", 0, n_misc, 26),
    JS_CFUNC_MAGIC_DEF("origin", 0, n_misc, 27),
    JS_CFUNC_MAGIC_DEF("templateContent", 1, n_misc, 28),
    JS_CFUNC_MAGIC_DEF("parseDoc", 1, n_misc, 29),
    JS_CFUNC_MAGIC_DEF("resolve", 2, n_misc, 30),
    JS_CFUNC_MAGIC_DEF("submit", 2, n_misc, 31),
    JS_CFUNC_MAGIC_DEF("click", 1, n_misc, 32),
    JS_CFUNC_MAGIC_DEF("moOn", 1, n_misc, 33),
    JS_CFUNC_MAGIC_DEF("setCaret", 2, n_misc, 34),
    JS_CFUNC_MAGIC_DEF("caretRect", 2, n_misc, 35),
    JS_CFUNC_MAGIC_DEF("mediaOp", 6, n_misc, 36),
    JS_CFUNC_MAGIC_DEF("utf8dec", 1, n_misc, 37),
    JS_CFUNC_MAGIC_DEF("attachShadow", 1, n_misc, 39),
    JS_CFUNC_MAGIC_DEF("getShadow", 1, n_misc, 40),
    JS_CFUNC_MAGIC_DEF("shadowHost", 1, n_misc, 41),
    JS_CFUNC_MAGIC_DEF("utf8enc", 1, n_misc, 38),
};

static char* mod_normalize(JSContext* c, const char* base, const char* name, void* op) {
    (void)op;
    char full[2048];
    url_resolve(base && strchr(base, ':') ? base : G.base, name, full, sizeof(full));
    size_t l = strlen(full);
    char* r = js_malloc(c, l + 1);
    if (r) memcpy(r, full, l + 1);
    return r;
}
static JSModuleDef* mod_loader(JSContext* c, const char* name, void* op) {
    (void)op;
    int len = 0;
    char* src = eng_fetch_text(name, &len);
    if (!src) { JS_ThrowReferenceError(c, "could not load module '%s'", name); return 0; }
    JSValue fn = JS_Eval(c, src, (size_t)len, name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(fn)) return 0;
    JSModuleDef* m = JS_VALUE_GET_PTR(fn);
    JS_FreeValue(c, fn);
    return m;
}

static JSClassDef node_cls = { "Node", 0, 0, 0, 0 };

int js_init(void) {
    g_load_js_ms = 0;
    rt = JS_NewRuntime();
    if (!rt) return -1;
    extern size_t nwe_arena_size(void);
    size_t asz = nwe_arena_size();
    JS_SetMemoryLimit(rt, asz ? asz * 7 / 10 : (64u << 20));
    JS_SetMaxStackSize(rt, 3u * 1024 * 1024);
    JS_SetInterruptHandler(rt, interrupt_cb, 0);
    JS_SetModuleLoaderFunc(rt, mod_normalize, mod_loader, 0);
    ctx = JS_NewContext(rt);
    if (!ctx) return -1;
    node_class = 0;
    JS_NewClassID(rt, &node_class);
    JS_NewClass(rt, node_class, &node_cls);
    p_node = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, p_node, node_funcs, (int)(sizeof(node_funcs) / sizeof(node_funcs[0])));
    p_chardata = JS_NewObjectProto(ctx, p_node);
    p_text = JS_NewObjectProto(ctx, p_chardata);
    p_comment = JS_NewObjectProto(ctx, p_chardata);
    p_elem = JS_NewObjectProto(ctx, p_node);
    JS_SetPropertyFunctionList(ctx, p_elem, elem_funcs, (int)(sizeof(elem_funcs) / sizeof(elem_funcs[0])));
    p_html = JS_NewObjectProto(ctx, p_elem);
    p_svg = JS_NewObjectProto(ctx, p_elem);
    p_doc = JS_NewObjectProto(ctx, p_node);
    JS_SetPropertyFunctionList(ctx, p_doc, doc_funcs, (int)(sizeof(doc_funcs) / sizeof(doc_funcs[0])));
    p_frag = JS_NewObjectProto(ctx, p_node);
    for (int i = 0; i < T_COUNT; i++) tagproto[i] = JS_UNDEFINED;
    timers = 0;
    ntimers = ctimers = 0;
    next_tid = 0;
    JSValue glob = JS_GetGlobalObject(ctx);
    JSValue nat = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, nat, native_funcs, (int)(sizeof(native_funcs) / sizeof(native_funcs[0])));
    JSValue protos = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, protos, "Node", JS_DupValue(ctx, p_node));
    JS_SetPropertyStr(ctx, protos, "CharacterData", JS_DupValue(ctx, p_chardata));
    JS_SetPropertyStr(ctx, protos, "Text", JS_DupValue(ctx, p_text));
    JS_SetPropertyStr(ctx, protos, "Comment", JS_DupValue(ctx, p_comment));
    JS_SetPropertyStr(ctx, protos, "Element", JS_DupValue(ctx, p_elem));
    JS_SetPropertyStr(ctx, protos, "HTMLElement", JS_DupValue(ctx, p_html));
    JS_SetPropertyStr(ctx, protos, "SVGElement", JS_DupValue(ctx, p_svg));
    JS_SetPropertyStr(ctx, protos, "Document", JS_DupValue(ctx, p_doc));
    JS_SetPropertyStr(ctx, protos, "DocumentFragment", JS_DupValue(ctx, p_frag));
    JS_SetPropertyStr(ctx, nat, "protos", protos);
    JS_SetPropertyStr(ctx, nat, "document", wrap(G.doc));
    JS_SetPropertyStr(ctx, glob, "__n", nat);
    JS_FreeValue(ctx, glob);
    arm();
    JSValue r = JS_Eval(ctx, nwe_prelude, strlen(nwe_prelude), "<prelude>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) report();
    JS_FreeValue(ctx, r);
    js_deadline = 0;
    js_inited = 1;
    return 0;
}

void js_drain(void) {
    if (!js_inited) return;
    JSContext* c;
    arm();
    for (int i = 0; i < 10000; i++) {
        int r = JS_ExecutePendingJob(rt, &c);
        if (r <= 0) { if (r < 0) report(); break; }
    }
    js_deadline = 0;
}


void js_run_script(const char* src, int len, const char* name, int module) {
    if (!js_inited || !src) return;
    if (g_load_js_ms > 8000) { char m[200]; snprintf(m, sizeof(m), "script skipped (page scripts already used 8 s): %s", name ? name : "<script>"); nwk_log(m); return; }
    unsigned long st0 = nwk_ms();
    char* buf = nalloc((size_t)len + 1);
    memcpy(buf, src, (size_t)len);
    buf[len] = 0;
    arm();
    { unsigned long extra = (unsigned long)len / 200000ul * 1000ul; if (extra > 8000ul) extra = 8000ul; js_deadline += extra; }
    JSValue r = JS_Eval(ctx, buf, (size_t)len, name ? name : "<script>", module ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) report();
    JS_FreeValue(ctx, r);
    js_deadline = 0;
    nfree(buf);
    js_drain();
    g_load_js_ms += nwk_ms() - st0;
}

static int mut_busy;
static void mut_hook(int k, node* t, node* c, node* pv, node* nx, const char* nm, const char* old) {
    if (!js_inited || !ctx || mut_busy) return;
    mut_busy = 1;
    JSValue glob = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, glob, "__mut");
    if (JS_IsFunction(ctx, fn)) {
        JSValue a[7] = { JS_NewInt32(ctx, k), wrap(t), c ? wrap(c) : JS_NULL, pv ? wrap(pv) : JS_NULL, nx ? wrap(nx) : JS_NULL, nm ? jstr(nm) : JS_NULL, old ? jstr(old) : JS_NULL };
        JSValue r = JS_Call(ctx, fn, glob, 7, a);
        if (JS_IsException(r)) report();
        JS_FreeValue(ctx, r);
        for (int i = 0; i < 7; i++) JS_FreeValue(ctx, a[i]);
    }
    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, glob);
    mut_busy = 0;
}
void js_ce_call(const char* name, node* a, int b) {
    if (!js_inited || !a) return;
    JSValue glob = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, glob, name);
    if (JS_IsFunction(ctx, fn)) {
        JSValue args[2] = { wrap(a), JS_NewInt32(ctx, b) };
        int outer = js_deadline != 0;
        if (!outer) arm();
        JSValue r = JS_Call(ctx, fn, glob, 2, args);
        if (JS_IsException(r)) report();
        JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, args[0]);
        if (!outer) { js_drain(); js_deadline = 0; }
    }
    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, glob);
}
void js_dispatch(node* target, const char* type, int bubbles, float cx, float cy, int key, int* prevented) {
    if (prevented) *prevented = 0;
    if (!js_inited || !target) return;
    JSValue glob = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, glob, "__dispatch");
    if (JS_IsFunction(ctx, fn)) {
        JSValue args[6];
        args[0] = target == G.doc && 0 ? JS_UNDEFINED : wrap(target);
        args[1] = jstr(type);
        args[2] = JS_NewBool(ctx, bubbles);
        args[3] = JS_NewFloat64(ctx, cx);
        args[4] = JS_NewFloat64(ctx, cy);
        args[5] = JS_NewInt32(ctx, key);
        arm();
        JSValue r = JS_Call(ctx, fn, glob, 6, args);
        if (JS_IsException(r)) report();
        else if (prevented) *prevented = JS_ToBool(ctx, r);
        JS_FreeValue(ctx, r);
        for (int i = 0; i < 6; i++) JS_FreeValue(ctx, args[i]);
        js_deadline = 0;
    }
    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, glob);
    js_drain();
}

void js_fire_load(void) {
    if (!js_inited) return;
    js_run_script("__fireLoad()", 12, "<load>", 0);
}

static void call_timer(JSValue fn, double arg, int witharg) {
    JSValue a = JS_NewFloat64(ctx, arg);
    arm();
    JSValue r = JS_Call(ctx, fn, JS_UNDEFINED, witharg ? 1 : 0, &a);
    if (JS_IsException(r)) report();
    JS_FreeValue(ctx, r);
    js_deadline = 0;
}

int js_tick(void) {
    if (!js_inited) return 0;
    unsigned long now = nwk_ms();
    int ran = 0;
    int n = ntimers;
    for (int i = 0; i < n && ran < 400; i++) {
        timer* t = &timers[i];
        if (!t->active || t->raf || t->due > now) continue;
        if (ran && nwk_ms() - now > 40) break;
        JSValue fn = JS_DupValue(ctx, t->fn);
        int id = t->id;
        if (t->interval) t->due = now + (unsigned long)t->ms;
        else { t->active = 0; JS_FreeValue(ctx, t->fn); t->fn = JS_UNDEFINED; }
        call_timer(fn, 0, 0);
        JS_FreeValue(ctx, fn);
        ran++;
        (void)id;
        js_drain();
        if (G.nav_pending) return 1;
    }
    int hasraf = 0;
    for (int i = 0; i < ntimers; i++) if (timers[i].active && timers[i].raf) { hasraf = 1; break; }
    if (hasraf) {
        static unsigned long lastraf;
        if (now - lastraf >= 15) {
            lastraf = now;
            int m = ntimers;
            double ts = (double)(now - G.t0);
            for (int i = 0; i < m; i++) {
                timer* t = &timers[i];
                if (!t->active || !t->raf) continue;
                JSValue fn = t->fn;
                t->active = 0;
                t->fn = JS_UNDEFINED;
                call_timer(fn, ts, 1);
                JS_FreeValue(ctx, fn);
                ran++;
            }
            js_drain();
        }
    }
    int k = 0;
    for (int i = 0; i < ntimers; i++) if (timers[i].active) timers[k++] = timers[i];
    ntimers = k;
    return ran > 0;
}

int js_has_timers(void) { for (int i = 0; i < ntimers; i++) if (timers[i].active) return 1; return 0; }

int js_eval_expr(const char* src, char* out, int cap) {
    out[0] = 0;
    if (!js_inited) return -1;
    char* buf = nstrdup(src);
    arm();
    JSValue r = JS_Eval(ctx, buf, strlen(buf), "<eval>", JS_EVAL_TYPE_GLOBAL);
    js_deadline = 0;
    int rc = 0;
    if (JS_IsException(r)) {
        JSValue e = JS_GetException(ctx);
        const char* s = JS_ToCString(ctx, e);
        snprintf(out, (size_t)cap, "Uncaught %s", s ? s : "error");
        if (s) JS_FreeCString(ctx, s);
        JS_FreeValue(ctx, e);
        rc = -1;
    } else if (!JS_IsUndefined(r)) {
        JSValue glob = JS_GetGlobalObject(ctx);
        JSValue fmt = JS_GetPropertyStr(ctx, glob, "__fmt");
        JSValue sv = JS_IsFunction(ctx, fmt) ? JS_Call(ctx, fmt, glob, 1, &r) : JS_DupValue(ctx, r);
        const char* s = JS_ToCString(ctx, sv);
        snprintf(out, (size_t)cap, "%s", s ? s : "");
        if (s) JS_FreeCString(ctx, s);
        JS_FreeValue(ctx, sv);
        JS_FreeValue(ctx, fmt);
        JS_FreeValue(ctx, glob);
    } else snprintf(out, (size_t)cap, "undefined");
    JS_FreeValue(ctx, r);
    js_drain();
    return rc;
}

void js_reset(void) { g_mut_on = 0; g_ce_node = 0; g_ce_off = 0; js_inited = 0; rt = 0; ctx = 0; timers = 0; ntimers = ctimers = 0; }
int js_ready(void) { return js_inited; }
