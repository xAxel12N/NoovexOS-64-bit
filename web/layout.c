#include "web/layout.h"

box* g_root;
float g_doch, g_docw;
float g_vw, g_vh;
int g_lay_depth;
static box* abs_head;
static box* abs_tail;

box* mkbox(int kind, node* n, style* st) {
    box* b = nzalloc(sizeof(box));
    b->kind = kind;
    b->n = n;
    b->st = st;
    return b;
}
static void addchild(box* p, box* c) {
    c->parent = p;
    c->next = 0;
    if (p->last) p->last->next = c; else p->first = c;
    p->last = c;
}
float lineh_of(style* st) {
    if (st->lh > 0) return st->lh;
    if (st->lhmul > 0) return st->lhmul * st->fsize;
    if (st->lhmul == 0 && st->lh == 0) return 0;
    fontm m;
    font_metrics(st->face, st->fsize, &m);
    return (float)m.lineh;
}
static int oof(box* c) { return c->st && c->kind != BX_TEXT && c->kind != BX_BR && (c->st->flt != FL_NONE || c->st->position == P_ABSOLUTE || c->st->position == P_FIXED); }
int in_flow(box* c) { return !oof(c); }
static int outer_inline(box* c) {
    if (c->kind == BX_TEXT || c->kind == BX_INLINE || c->kind == BX_BR) return 1;
    int d = c->st->display;
    return d == D_INLINE || d == D_IBLOCK || d == D_IFLEX || d == D_IGRID || d == D_ITABLE;
}
int is_block_level(box* c) { return !oof(c) && !outer_inline(c); }
static int atomic_inline(box* c) { return c->kind != BX_TEXT && c->kind != BX_INLINE && c->kind != BX_BR && outer_inline(c) && !oof(c); }

static int is_replaced_tag(node* n) {
    switch (n->tag) {
    case T_IMG: case T_INPUT: case T_TEXTAREA: case T_SELECT: case T_VIDEO: case T_CANVAS: case T_IFRAME: case T_SVG: case T_OBJECT: case T_EMBED: case T_PROGRESS: case T_METER: case T_AUDIO:
        return 1;
    }
    return 0;
}

static int ws_only(box* t) {
    if (t->kind != BX_TEXT) return 0;
    const char* s = t->ptext ? t->ptext : t->n->text;
    int n = t->ptext ? t->ptlen : t->n->tlen;
    if (t->st->ws == WS_PRE || t->st->ws == WS_PREWRAP) return n == 0;
    for (int i = 0; i < n; i++) if (!is_ws((unsigned char)s[i])) return 0;
    return 1;
}

static box* text_box(node* tn, const char* s, int n, style* st) {
    box* t = mkbox(BX_TEXT, tn, st);
    if (s) { t->ptext = s; t->ptlen = n; }
    return t;
}

static char* content_text(style* ps, node* e, int* len) {
    sbuf b = { 0, 0, 0 };
    const char* c = ps->content;
    while (*c) {
        if (*c == 1) {
            const char* e2 = strchr(c, 2);
            if (!e2) break;
            char an[64];
            int l = (int)(e2 - c - 1) < 63 ? (int)(e2 - c - 1) : 63;
            memcpy(an, c + 1, (size_t)l);
            an[l] = 0;
            const char* v = attr_get(e, an);
            if (v) sb_puts(&b, v);
            c = e2 + 1;
            continue;
        }
        sb_putc(&b, *c++);
    }
    *len = b.n;
    return b.s ? b.s : (char*)"";
}

static box* build_elem(node* e, box* parent);
static box* pseudo_box(node* e, style* ps, int which) {
    if (!ps || ps->display == D_NONE) return 0;
    int len;
    char* txt = content_text(ps, e, &len);
    int k = (ps->display == D_INLINE) ? BX_INLINE : ps->display == D_FLEX || ps->display == D_IFLEX ? BX_FLEX : ps->display == D_GRID || ps->display == D_IGRID ? BX_GRID : BX_BLOCK;
    box* b = mkbox(k, e, ps);
    b->pseudo = which;
    if (len > 0) addchild(b, text_box(0, txt, len, ps));
    return b;
}

static node* shadow_host_of(node* n) {
    for (node* p = n->parent; p; p = p->parent) if (p->host) return p->host;
    return 0;
}
static int is_slot(node* c) { return c->type == NT_ELEM && c->name && c->name[0] == 's' && !strcmp(c->name, "slot"); }
static void build_list(node* src, box* b, int depth);
static void build_one(node* c, box* b, int depth) {
    if (c->type == NT_TEXT) {
        if (c->tlen > 0) addchild(b, text_box(c, 0, 0, b->st));
        return;
    }
    if (c->type != NT_ELEM || !c->st) return;
    if (is_slot(c) && depth < 16) {
        node* h = shadow_host_of(c);
        if (h) {
            const char* nm = attr_get(c, "name");
            int any = 0;
            for (node* k = h->first; k; k = k->next) {
                const char* sn = k->type == NT_ELEM ? attr_get(k, "slot") : 0;
                int m = nm && *nm ? (sn && !strcmp(sn, nm)) : (!sn || !*sn);
                if (!m || (k->type != NT_ELEM && k->type != NT_TEXT)) continue;
                any = 1;
                build_one(k, b, depth + 1);
            }
            if (!any) build_list(c, b, depth + 1);
            return;
        }
    }
    if (c->st->display == D_CONTENTS) { build_list(c->shadow ? c->shadow : c, b, depth + 1); return; }
    box* cb = build_elem(c, b);
    if (cb) addchild(b, cb);
}
static void build_list(node* src, box* b, int depth) { for (node* c = src->first; c; c = c->next) build_one(c, b, depth); }
static void build_children(node* e, box* b) {
    if (e->st_before) { box* p = pseudo_box(e, e->st_before, 1); if (p) addchild(b, p); }
    build_list(e->shadow ? e->shadow : e, b, 0);
    if (e->st_after) { box* p = pseudo_box(e, e->st_after, 2); if (p) addchild(b, p); }
}

static void wrap_inline_runs(box* b) {
    int hasblock = 0, hasinline = 0;
    for (box* c = b->first; c; c = c->next) {
        if (is_block_level(c)) hasblock = 1;
        else if (!oof(c) && !ws_only(c)) hasinline = 1;
    }
    if (!hasblock) { b->inl = 1; return; }
    b->inl = 0;
    if (!hasinline) {
        box* prev = 0;
        box* c = b->first;
        while (c) {
            box* nx = c->next;
            if (ws_only(c)) { if (prev) prev->next = nx; else b->first = nx; if (b->last == c) b->last = prev; }
            else prev = c;
            c = nx;
        }
        return;
    }
    box* first = 0;
    box* last = 0;
    box* run = 0;
    box* c = b->first;
    while (c) {
        box* nx = c->next;
        c->next = 0;
        if (is_block_level(c)) {
            run = 0;
            if (last) last->next = c; else first = c;
            last = c;
        } else {
            if (!run) {
                run = mkbox(BX_ANON, b->n, anon_style(b->st, D_BLOCK));
                run->parent = b;
                run->inl = 1;
                if (last) last->next = run; else first = run;
                last = run;
            }
            addchild(run, c);
        }
        c = nx;
    }
    b->first = first;
    b->last = last;
    box* prev = 0;
    for (box* k = b->first; k;) {
        box* nx = k->next;
        if (k->kind == BX_ANON) {
            int all_ws = 1;
            for (box* t = k->first; t; t = t->next) if (!ws_only(t)) { all_ws = 0; break; }
            if (all_ws) {
                int has_oof = 0;
                for (box* t = k->first; t; t = t->next) if (oof(t)) has_oof = 1;
                if (!has_oof) { if (prev) prev->next = nx; else b->first = nx; if (b->last == k) b->last = prev; k = nx; continue; }
            }
        }
        prev = k;
        k = nx;
    }
}

static int inline_has_block(box* b) {
    for (box* c = b->first; c; c = c->next) {
        if (is_block_level(c)) return 1;
        if (c->kind == BX_INLINE && inline_has_block(c)) return 1;
    }
    return 0;
}

static void fix_table(box* t);

static void wrap_items(box* b) {
    box* first = 0;
    box* last = 0;
    box* run = 0;
    box* c = b->first;
    while (c) {
        box* nx = c->next;
        c->next = 0;
        if (c->kind == BX_TEXT || c->kind == BX_INLINE || c->kind == BX_BR) {
            if (c->kind == BX_TEXT && ws_only(c) && !run) { c = nx; continue; }
            if (!run) {
                run = mkbox(BX_ANON, b->n, anon_style(b->st, D_BLOCK));
                run->parent = b;
                run->inl = 1;
                if (last) last->next = run; else first = run;
                last = run;
            }
            addchild(run, c);
        } else {
            run = 0;
            c->parent = b;
            if (last) last->next = c; else first = c;
            last = c;
        }
        c = nx;
    }
    b->first = first;
    b->last = last;
    box* prev = 0;
    for (box* k = b->first; k;) {
        box* nx = k->next;
        if (k->kind == BX_ANON) {
            int all_ws = 1;
            for (box* t = k->first; t; t = t->next) if (!ws_only(t)) { all_ws = 0; break; }
            if (all_ws) { if (prev) prev->next = nx; else b->first = nx; if (b->last == k) b->last = prev; k = nx; continue; }
        }
        prev = k;
        k = nx;
    }
}

static box* build_elem(node* e, box* parent) {
    style* st = e->st;
    if (!st || st->display == D_NONE) { e->box = 0; return 0; }
    (void)parent;
    int k;
    int d = st->display;
    if (e->tag == T_BR) k = BX_BR;
    else if (is_replaced_tag(e) && !(e->tag == T_INPUT && attr_get(e, "type") && ieq(attr_get(e, "type"), "hidden"))) k = BX_REPLACED;
    else if (e->tag == T_BUTTON) k = BX_BLOCK;
    else if (d == D_FLEX || d == D_IFLEX) k = BX_FLEX;
    else if (d == D_GRID || d == D_IGRID) k = BX_GRID;
    else if (d == D_TABLE || d == D_ITABLE) k = BX_TABLE;
    else if (d == D_TROW) k = BX_ROW;
    else if (d == D_TCELL) k = BX_CELL;
    else if (d == D_TROWGROUP || d == D_THEADG || d == D_TFOOTG) k = BX_ROWGROUP;
    else if (d == D_TCAPTION) k = BX_CAPTION;
    else if (d == D_TCOL || d == D_TCOLGROUP) { e->box = 0; return 0; }
    else if (d == D_INLINE) k = BX_INLINE;
    else k = BX_BLOCK;
    box* b = mkbox(k, e, st);
    e->box = b;
    b->sy = e->scrolly;
    if (k == BX_REPLACED || k == BX_BR) return b;
    build_children(e, b);
    if (k == BX_INLINE && inline_has_block(b)) { b->kind = BX_BLOCK; k = BX_BLOCK; }
    if (k == BX_INLINE) return b;
    if (k == BX_FLEX || k == BX_GRID) { wrap_items(b); return b; }
    if (k == BX_TABLE) { fix_table(b); return b; }
    if (k == BX_ROW || k == BX_ROWGROUP) return b;
    wrap_inline_runs(b);
    return b;
}

static void fix_table(box* t) {
    box* first = 0;
    box* last = 0;
    box* grp = 0;
    box* row = 0;
    box* c = t->first;
    while (c) {
        box* nx = c->next;
        c->next = 0;
        if (c->kind == BX_TEXT && ws_only(c)) { c = nx; continue; }
        if (c->kind == BX_ROWGROUP || c->kind == BX_CAPTION) {
            grp = 0;
            row = 0;
            c->parent = t;
            if (last) last->next = c; else first = c;
            last = c;
            if (c->kind == BX_ROWGROUP) {
                box* rf = 0;
                box* rl = 0;
                box* rr = 0;
                box* x = c->first;
                while (x) {
                    box* xn = x->next;
                    x->next = 0;
                    if (x->kind == BX_TEXT && ws_only(x)) { x = xn; continue; }
                    if (x->kind == BX_ROW) { rr = 0; x->parent = c; if (rl) rl->next = x; else rf = x; rl = x; }
                    else {
                        if (!rr) { rr = mkbox(BX_ROW, c->n, anon_style(c->st, D_TROW)); rr->parent = c; if (rl) rl->next = rr; else rf = rr; rl = rr; }
                        if (x->kind != BX_CELL) { box* cell = mkbox(BX_CELL, x->n, anon_style(c->st, D_TCELL)); addchild(cell, x); wrap_inline_runs(cell); x = cell; }
                        addchild(rr, x);
                    }
                    x = xn;
                }
                c->first = rf;
                c->last = rl;
            }
        } else {
            if (!grp) { grp = mkbox(BX_ROWGROUP, t->n, anon_style(t->st, D_TROWGROUP)); grp->parent = t; if (last) last->next = grp; else first = grp; last = grp; row = 0; }
            if (c->kind == BX_ROW) { row = 0; addchild(grp, c); }
            else {
                if (!row) { row = mkbox(BX_ROW, t->n, anon_style(t->st, D_TROW)); addchild(grp, row); }
                if (c->kind != BX_CELL) { box* cell = mkbox(BX_CELL, c->n, anon_style(t->st, D_TCELL)); addchild(cell, c); wrap_inline_runs(cell); c = cell; }
                addchild(row, c);
            }
        }
        c = nx;
    }
    t->first = first;
    t->last = last;
    for (box* g = t->first; g; g = g->next) {
        if (g->kind != BX_ROWGROUP) continue;
        for (box* r = g->first; r; r = r->next) {
            box* cf = 0;
            box* cl = 0;
            box* x = r->first;
            while (x) {
                box* xn = x->next;
                x->next = 0;
                if (x->kind == BX_TEXT && ws_only(x)) { x = xn; continue; }
                if (x->kind != BX_CELL) { box* cell = mkbox(BX_CELL, x->n, anon_style(r->st, D_TCELL)); addchild(cell, x); wrap_inline_runs(cell); x = cell; }
                x->parent = r;
                if (cl) cl->next = x; else cf = x;
                cl = x;
                x = xn;
            }
            r->first = cf;
            r->last = cl;
        }
    }
}

static float LR(L l, float base) { return l.k == LK_VAL ? lres(l, base) : 0; }

void box_edges(box* b, float cbw) {
    style* s = b->st;
    if (b->kind == BX_TEXT || b->kind == BX_ANON || b->kind == BX_BR) {
        for (int i = 0; i < 4; i++) b->m[i] = b->p[i] = b->b[i] = 0;
        return;
    }
    for (int i = 0; i < 4; i++) {
        b->m[i] = LR(s->margin[i], cbw);
        b->p[i] = LR(s->padding[i], cbw);
        if (b->p[i] < 0) b->p[i] = 0;
        b->b[i] = s->border[i];
    }
    if (b->kind == BX_ROW || b->kind == BX_ROWGROUP) for (int i = 0; i < 4; i++) { b->m[i] = 0; b->p[i] = 0; }
    if (b->kind == BX_CELL) for (int i = 0; i < 4; i++) b->m[i] = 0;
}

float spec_content_h(box* b, float cbh) {
    style* s = b->st;
    L h = s->height;
    if (h.k != LK_VAL) return -1;
    if (h.pct != 0 && cbh < 0) return h.px > 0 && h.pct == 0 ? h.px : -1;
    float v = lres(h, cbh < 0 ? 0 : cbh);
    if (s->boxsizing) v -= b->p[0] + b->p[2] + b->b[0] + b->b[2];
    return v < 0 ? 0 : v;
}
float clamp_w(box* b, float w, float cbw) {
    style* s = b->st;
    float pb = b->p[1] + b->p[3] + b->b[1] + b->b[3];
    if (s->maxw.k == LK_VAL) { float m = lres(s->maxw, cbw); if (!s->boxsizing) m += pb; if (w > m) w = m; }
    if (s->minw.k == LK_VAL) { float m = lres(s->minw, cbw); if (!s->boxsizing) m += pb; if (w < m) w = m; }
    if (w < pb) w = pb;
    return w;
}
float clamp_h(box* b, float h, float cbh) {
    style* s = b->st;
    if (s->maxh.k == LK_VAL && !(s->maxh.pct != 0 && cbh < 0)) { float m = lres(s->maxh, cbh < 0 ? 0 : cbh); if (s->boxsizing) m -= b->p[0] + b->p[2] + b->b[0] + b->b[2]; if (h > m) h = m; }
    if (s->minh.k == LK_VAL && !(s->minh.pct != 0 && cbh < 0)) { float m = lres(s->minh, cbh < 0 ? 0 : cbh); if (s->boxsizing) m -= b->p[0] + b->p[2] + b->b[0] + b->b[2]; if (h < m) h = m; }
    return h < 0 ? 0 : h;
}

float shrink_fit(box* b, float avail) {
    float mn, mx;
    intrinsic(b, &mn, &mx);
    float w = mx < avail ? mx : avail;
    if (w < mn) w = mn;
    return w;
}

static void img_intrinsic(node* n, float* w, float* h) {
    *w = 0;
    *h = 0;
    if (n->img && n->img->state == 2) { *w = (float)n->img->w; *h = (float)n->img->h; return; }
    const char* aw = attr_get(n, "width");
    const char* ah = attr_get(n, "height");
    if (aw) *w = (float)atof(aw);
    if (ah) *h = (float)atof(ah);
}

float replaced_size(box* b, float cbw, float cbh, float* outh, float forcew) {
    style* s = b->st;
    node* n = b->n;
    float pbw = b->p[1] + b->p[3] + b->b[1] + b->b[3];
    float pbh = b->p[0] + b->p[2] + b->b[0] + b->b[2];
    float iw = 0, ih = 0;
    int t = n->tag;
    const char* ty = t == T_INPUT ? attr_get(n, "type") : 0;
    if (t == T_IMG || (t == T_INPUT && ty && ieq(ty, "image"))) {
        img_intrinsic(n, &iw, &ih);
        if (iw == 0 && ih == 0 && (!n->img || n->img->state != 2)) {
            const char* alt = attr_get(n, "alt");
            if (alt && *alt) { iw = text_width(s->face, s->fsize, alt, (int)strlen(alt), 0) + 4; ih = lineh_of(s); }
        }
    } else if (t == T_INPUT) {
        if (ty && (ieq(ty, "submit") || ieq(ty, "button") || ieq(ty, "reset"))) {
            const char* v = attr_get(n, "value");
            if (!v) v = ieq(ty, "reset") ? "Reset" : ieq(ty, "submit") ? "Submit" : "";
            iw = text_width(s->face, s->fsize, v, (int)strlen(v), 0);
            ih = lineh_of(s);
            if (ih <= 0) ih = s->fsize * 1.2f;
        } else if (ty && (ieq(ty, "checkbox") || ieq(ty, "radio"))) { iw = 13; ih = 13; }
        else if (ty && ieq(ty, "file")) { iw = 240; ih = lineh_of(s); }
        else if (ty && ieq(ty, "range")) { iw = 129; ih = 16; }
        else if (ty && ieq(ty, "color")) { iw = 50; ih = 27; }
        else { const char* sz = attr_get(n, "size"); int cols = sz ? atoi(sz) : 20; if (cols <= 0) cols = 20; iw = cols * s->fsize * 0.630f; ih = lineh_of(s) > 0 ? lineh_of(s) : 15; }
    } else if (t == T_SELECT) {
        float mw = 20;
        for (node* o = n->first; o; o = o->next) {
            node* list[2] = { o, 0 };
            for (int q = 0; q < 1; q++) {
                node* x = list[q];
                if (x->tag == T_OPTION) { sbuf bb = { 0, 0, 0 }; text_content(x, &bb); float w = text_width(s->face, s->fsize, bb.s ? bb.s : "", bb.n, 0); if (w > mw) mw = w; }
                else if (x->tag == T_OPTGROUP) for (node* y = x->first; y; y = y->next) if (y->tag == T_OPTION) { sbuf bb = { 0, 0, 0 }; text_content(y, &bb); float w = text_width(s->face, s->fsize, bb.s ? bb.s : "", bb.n, 0); if (w > mw) mw = w; }
            }
        }
        iw = mw;
        ih = lineh_of(s) > 0 ? lineh_of(s) : 15;
    } else if (t == T_TEXTAREA) { const char* cs = attr_get(n, "cols"); const char* rs = attr_get(n, "rows"); int cols = cs ? atoi(cs) : 20, rows = rs ? atoi(rs) : 2; if (cols <= 0) cols = 20; if (rows <= 0) rows = 2; float lh = lineh_of(s) > 0 ? lineh_of(s) : s->fsize * 1.2f; iw = cols * s->fsize * 0.655f; ih = rows * lh; }
    else if (t == T_PROGRESS || t == T_METER) { iw = 160; ih = 16; }
    else if (t == T_SVG) {
        const char* vb = attr_get(n, "viewBox");
        if (!vb) vb = attr_get(n, "viewbox");
        iw = 300; ih = 150;
        if (vb) { const char* p = vb; float v[4]; for (int i = 0; i < 4; i++) { while (*p == ' ' || *p == ',') p++; v[i] = parse_float(p, &p); } if (v[2] > 0 && v[3] > 0) { iw = v[2]; ih = v[3]; } }
        const char* aw = attr_get(n, "width");
        const char* ah = attr_get(n, "height");
        if (aw && atof(aw) > 0 && !strchr(aw, '%')) { float nw = (float)atof(aw); if (!ah) ih = ih * nw / iw; iw = nw; }
        if (ah && atof(ah) > 0 && !strchr(ah, '%')) { float nh = (float)atof(ah); if (!aw) iw = iw * nh / ih; ih = nh; }
    } else if (t == T_VIDEO) {
        int vw = 0, vh = 0;
        if (wm_dims(n, &vw, &vh)) { iw = (float)vw; ih = (float)vh; }
        else { const char* aw = attr_get(n, "width"); const char* ah = attr_get(n, "height"); iw = aw && atof(aw) > 0 ? (float)atof(aw) : 300; ih = ah && atof(ah) > 0 ? (float)atof(ah) : 150; }
    } else if (t == T_AUDIO) { iw = 300; ih = 54; }
    else { iw = 300; ih = 150; }
    float w, h;
    int wauto = s->width.k != LK_VAL || (s->width.pct != 0 && cbw < 0);
    int hauto = s->height.k != LK_VAL || (s->height.pct != 0 && cbh < 0);
    float ratio = (iw > 0 && ih > 0) ? ih / iw : 0;
    if (s->aspect > 0) ratio = 1.0f / s->aspect;
    if (forcew >= 0) { w = forcew - pbw; wauto = 0; }
    else w = wauto ? iw : lres(s->width, cbw) - (s->boxsizing ? pbw : 0);
    h = hauto ? ih : lres(s->height, cbh) - (s->boxsizing ? pbh : 0);
    if (wauto && !hauto && ratio > 0) w = h / ratio;
    if (hauto && !wauto && ratio > 0) h = w * ratio;
    float bw = w + pbw;
    float cw = clamp_w(b, bw, cbw);
    if (cw != bw) { w = cw - pbw; if (hauto && ratio > 0) h = w * ratio; }
    float ch = clamp_h(b, h, cbh);
    if (ch != h) { h = ch; if (wauto && ratio > 0 && forcew < 0) { w = h / ratio; } }
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    *outh = h + pbh;
    return w + pbw;
}

static float block_width(box* b, float cbw, int shrink) {
    style* s = b->st;
    float pb = b->p[1] + b->p[3] + b->b[1] + b->b[3];
    float w;
    int autow = s->width.k != LK_VAL;
    if (b->kind == BX_REPLACED) { float h; return replaced_size(b, cbw, -1, &h, -1); }
    if (!autow) {
        w = lres(s->width, cbw);
        if (!s->boxsizing) w += pb;
    } else if (shrink || s->width.k == LK_FITC || s->width.k == LK_MAXC || s->width.k == LK_MINC) {
        float avail = cbw - b->m[1] - b->m[3];
        if (s->width.k == LK_MINC) { float mn, mx; intrinsic(b, &mn, &mx); w = mn; }
        else if (s->width.k == LK_MAXC) { float mn, mx; intrinsic(b, &mn, &mx); w = mx; }
        else w = shrink_fit(b, avail < 0 ? 0 : avail);
    } else {
        w = cbw - (s->margin[1].k == LK_AUTO ? 0 : b->m[1]) - (s->margin[3].k == LK_AUTO ? 0 : b->m[3]);
    }
    w = clamp_w(b, w, cbw);
    if (!shrink && b->kind != BX_CELL) {
        int al = s->margin[3].k == LK_AUTO, ar = s->margin[1].k == LK_AUTO;
        float rem = cbw - w - (al ? 0 : b->m[3]) - (ar ? 0 : b->m[1]);
        if (al && ar) { b->m[3] = b->m[1] = rem > 0 ? rem / 2 : 0; }
        else if (al) b->m[3] = rem > 0 ? rem : 0;
        else if (ar) b->m[1] = rem;
    }
    return w;
}

static int is_bfc_root(box* b) {
    style* s = b->st;
    if (!b->parent) return 1;
    if (b->kind == BX_CELL || b->kind == BX_CAPTION || b->kind == BX_FLEX || b->kind == BX_GRID || b->kind == BX_TABLE) return 1;
    if (s->flt != FL_NONE || s->position == P_ABSOLUTE || s->position == P_FIXED) return 1;
    if (s->display == D_IBLOCK || s->display == D_IFLEX || s->display == D_IGRID) return 1;
    if ((s->ovx != OV_VISIBLE && s->ovx != OV_CLIP) || (s->ovy != OV_VISIBLE && s->ovy != OV_CLIP)) return 1;
    if (b->parent && (b->parent->kind == BX_FLEX || b->parent->kind == BX_GRID)) return 1;
    if (b->n && b->n->tag == T_BUTTON) return 1;
    return 0;
}

static void float_space(bfc* f, float y, float h, float x0, float x1, float* l, float* r) {
    *l = x0;
    *r = x1;
    if (!f) return;
    for (flt* q = f->floats; q; q = q->next) {
        if (q->y < y + (h > 0 ? h : 1) && q->y + q->h > y) {
            if (q->side == FL_LEFT && q->x + q->w > *l && q->x < x1) *l = q->x + q->w;
            if (q->side == FL_RIGHT && q->x < *r && q->x + q->w > x0) *r = q->x;
        }
    }
}
static float float_bottom(bfc* f, int side) {
    float b = -1e30f;
    if (!f) return b;
    for (flt* q = f->floats; q; q = q->next) if (side == 3 || q->side == side) if (q->y + q->h > b) b = q->y + q->h;
    return b;
}
static float clear_y(bfc* f, int clr, float y) {
    if (!clr || !f) return y;
    float b = float_bottom(f, clr == 1 ? FL_LEFT : clr == 2 ? FL_RIGHT : 3);
    return b > y ? b : y;
}

static void place_float(box* c, bfc* f, float x0, float x1, float y, float cbw) {
    box_edges(c, cbw);
    float w;
    if (c->kind == BX_REPLACED) { float h; w = replaced_size(c, cbw, -1, &h, -1); }
    else w = block_width(c, cbw, 1);
    float ow = w + c->m[1] + c->m[3];
    y = clear_y(f, c->st->clear, y);
    float l, r;
    for (int guard = 0; guard < 200; guard++) {
        float_space(f, y, 1, x0, x1, &l, &r);
        if (r - l >= ow || (l == x0 && r == x1)) break;
        float ny = 1e30f;
        for (flt* q = f->floats; q; q = q->next) if (q->y + q->h > y && q->y + q->h < ny) ny = q->y + q->h;
        if (ny >= 1e29f) break;
        y = ny;
    }
    float bx = c->st->flt == FL_LEFT ? l + c->m[3] : r - ow + c->m[3];
    lay(c, bx, y + c->m[0], cbw, -1, 0, w, -1);
    flt* q = nalloc(sizeof(flt));
    q->x = c->x - c->m[3];
    q->y = y;
    q->w = c->w + c->m[1] + c->m[3];
    q->h = c->h + c->m[0] + c->m[2];
    q->side = c->st->flt;
    q->next = f->floats;
    f->floats = q;
}

void add_abs(box* b) {
    if (b->absflag) return;
    b->absflag = 1;
    b->nextabs = 0;
    if (abs_tail) abs_tail->nextabs = b; else abs_head = b;
    abs_tail = b;
}

static void frag_add(box* b, frag* f) {
    if (b->nfr >= b->cfr) {
        int nc = b->cfr ? b->cfr * 2 : 16;
        frag* nf = nalloc(sizeof(frag) * (size_t)nc);
        if (b->nfr) memcpy(nf, b->fr, sizeof(frag) * (size_t)b->nfr);
        b->fr = nf;
        b->cfr = nc;
    }
    b->fr[b->nfr++] = *f;
}

enum { PI_WORD, PI_OPEN, PI_CLOSE, PI_ATOM, PI_BR, PI_FLOAT, PI_ABS };
typedef struct piece {
    int kind;
    const char* s;
    int n;
    float w, spw;
    style* st;
    box* b;
    node* tn;
    int brk;
    float x;
} piece;
typedef struct { piece* p; int n, cap; } plist;
static void padd(plist* l, piece* p) {
    if (l->n >= l->cap) {
        int nc = l->cap ? l->cap * 2 : 64;
        piece* np = nalloc(sizeof(piece) * (size_t)nc);
        if (l->n) memcpy(np, l->p, sizeof(piece) * (size_t)l->n);
        l->p = np;
        l->cap = nc;
    }
    l->p[l->n++] = *p;
}

static int is_cjk(int cp) { return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0x3000 && cp <= 0x30FF) || (cp >= 0xFF00 && cp <= 0xFFEF); }

static char* transform_text(const char* s, int n, int tt, int* outn, int* capstate) {
    sbuf b = { 0, 0, 0 };
    for (int i = 0; i < n;) {
        int cp;
        int k = u8dec(s + i, n - i, &cp);
        if (k <= 0) break;
        i += k;
        if (tt == 1 || (tt == 3 && *capstate)) {
            if (cp >= 'a' && cp <= 'z') cp -= 32;
            else if ((cp >= 0xE0 && cp <= 0xFE && cp != 0xF7)) cp -= 32;
            else if (cp >= 0x430 && cp <= 0x44F) cp -= 32;
        } else if (tt == 2) {
            if (cp >= 'A' && cp <= 'Z') cp += 32;
            else if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) cp += 32;
            else if (cp >= 0x410 && cp <= 0x42F) cp += 32;
        }
        if (tt == 3) *capstate = is_ws(cp) || cp == '-' || cp == '(';
        sb_putu8(&b, cp);
    }
    *outn = b.n;
    return b.s ? b.s : (char*)"";
}

static int g_lastsp;

static void add_text_pieces(plist* L, box* t, style* st) {
    const char* s = t->ptext ? t->ptext : t->n->text;
    int n = t->ptext ? t->ptlen : t->n->tlen;
    if (st->ttrans) { int cs = 1; s = transform_text(s, n, st->ttrans, &n, &cs); }
    int ws = st->ws;
    int collapse = ws == WS_NORMAL || ws == WS_NOWRAP || ws == WS_PRELINE;
    int nlbreak = ws == WS_PRE || ws == WS_PREWRAP || ws == WS_PRELINE;
    float spw = font_adv(st->face, st->fsize, ' ') + st->lspace + st->wspace;
    int i = 0;
    node* tn = t->n && t->n->type == NT_TEXT ? t->n->parent : t->n;
    while (i < n) {
        int cp;
        int k = u8dec(s + i, n - i, &cp);
        if (k <= 0) break;
        if (cp == '\n' && nlbreak) {
            piece p = { PI_BR, 0, 0, 0, 0, st, t, tn, 1, 0 };
            padd(L, &p);
            g_lastsp = 1;
            i += k;
            continue;
        }
        if (collapse && (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\f')) {
            while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\f' || (s[i] == '\n' && !nlbreak))) i++;
            if (g_lastsp) continue;
            if (L->n > 0) {
                piece* lp = &L->p[L->n - 1];
                int j = L->n - 1;
                while (j >= 0 && (L->p[j].kind == PI_OPEN || L->p[j].kind == PI_CLOSE)) j--;
                if (j >= 0 && (L->p[j].kind == PI_WORD || L->p[j].kind == PI_ATOM)) { L->p[j].spw += spw; L->p[j].brk = 1; }
                else if (j >= 0) { L->p[j].brk = 1; }
                (void)lp;
            }
            g_lastsp = 1;
            continue;
        }
        if (!collapse && (cp == ' ' || cp == '\t')) {
            float w = cp == '\t' ? spw * 8 : spw;
            if (ws == WS_PREWRAP && L->n > 0 && L->p[L->n - 1].kind == PI_WORD) { L->p[L->n - 1].spw += w; L->p[L->n - 1].brk = 1; i += k; continue; }
            piece p = { PI_WORD, s + i, k, w, 0, st, t, tn, ws == WS_PREWRAP, 0 };
            if (cp == '\t') { p.s = "        "; p.n = 8; }
            padd(L, &p);
            g_lastsp = 0;
            i += k;
            continue;
        }
        int st0 = i;
        int brkc = 0;
        while (i < n) {
            k = u8dec(s + i, n - i, &cp);
            if (k <= 0) break;
            if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\f') break;
            if (cp == 0x200B) { i += k; brkc = 1; break; }
            i += k;
            if (is_cjk(cp) || cp == '-' || cp == '/' || cp == 0x2014 || cp == 0x2013) {
                if (cp == '-' || cp == '/') { int nc; if (i < n && u8dec(s + i, n - i, &nc) > 0 && !is_ws(nc) && i - st0 > 2) { brkc = 1; break; } continue; }
                brkc = 1;
                break;
            }
        }
        int wn = i - st0;
        if (wn > 0) {
            piece p = { PI_WORD, s + st0, wn, text_width(st->face, st->fsize, s + st0, wn, st->lspace), 0, st, t, tn, brkc, 0 };
            if (st->wbreak && p.w > 0) p.brk = 2;
            padd(L, &p);
            g_lastsp = 0;
        }
    }
}

static void flatten(plist* L, box* c, style* cst) {
    for (box* x = c->first; x; x = x->next) {
        if (x->kind == BX_TEXT) { add_text_pieces(L, x, x->st); continue; }
        if (x->kind == BX_BR) { piece p = { PI_BR, 0, 0, 0, 0, x->st, x, x->n, 1, 0 }; padd(L, &p); g_lastsp = 1; continue; }
        if (oof(x)) {
            piece p = { x->st->flt != FL_NONE ? PI_FLOAT : PI_ABS, 0, 0, 0, 0, x->st, x, x->n, 0, 0 };
            padd(L, &p);
            continue;
        }
        if (x->kind == BX_INLINE) {
            piece o = { PI_OPEN, 0, 0, 0, 0, x->st, x, x->n, 0, 0 };
            padd(L, &o);
            flatten(L, x, x->st);
            piece cl = { PI_CLOSE, 0, 0, 0, 0, x->st, x, x->n, 0, 0 };
            padd(L, &cl);
            continue;
        }
        piece a = { PI_ATOM, 0, 0, 0, 0, x->st, x, x->n, 1, 0 };
        if (L->n > 0) { int j = L->n - 1; while (j >= 0 && (L->p[j].kind == PI_OPEN || L->p[j].kind == PI_CLOSE)) j--; if (j >= 0) L->p[j].brk |= 1; }
        padd(L, &a);
        g_lastsp = 0;
    }
    (void)cst;
}

static void pmetrics(piece* p, float* above, float* below) {
    style* st = p->st;
    fontm m;
    font_metrics(st->face, st->fsize, &m);
    float lh = lineh_of(st);
    float hl = (lh - (m.ascent + m.descent)) / 2;
    *above = m.ascent + hl;
    *below = m.descent + hl;
}

static float valign_shift(style* st, style* parent) {
    if (st->hasvalignpx) return -st->valignpx;
    switch (st->valign) {
    case VA_SUB: return parent ? parent->fsize * 0.25f : st->fsize * 0.3f;
    case VA_SUPER: return parent ? -parent->fsize * 0.4f : -st->fsize * 0.45f;
    }
    return 0;
}

typedef struct { box* ib; float x0; int open; } openrec;

static float measure_atom(box* a, float cbw, float* h) {
    box_edges(a, cbw);
    if (a->kind == BX_REPLACED) {
        float w = replaced_size(a, cbw, -1, h, -1);
        a->w = w;
        a->h = *h;
        a->firstbase = -1;
        a->haslines = 0;
        return w;
    }
    float w = block_width(a, cbw, 1);
    lay(a, 0, 0, cbw, -1, 0, w, -1);
    *h = a->h;
    return w;
}

static void list_marker_text(box* li, char* out, int cap) {
    style* s = li->st;
    out[0] = 0;
    int idx = 1;
    node* n = li->n;
    if (s->lstype >= LS_DECIMAL && n) {
        node* par = n->parent;
        int start = 1, rev = 0;
        if (par && par->tag == T_OL) { const char* st = attr_get(par, "start"); if (st) start = atoi(st); rev = attr_get(par, "reversed") != 0; }
        int cnt = 0;
        int total = 0;
        for (node* c = par ? par->first : 0; c; c = c->next) if (c->type == NT_ELEM && c->st && c->st->display == D_LISTITEM) total++;
        for (node* c = par ? par->first : 0; c; c = c->next) {
            if (c->type != NT_ELEM || !c->st || c->st->display != D_LISTITEM) continue;
            const char* v = attr_get(c, "value");
            if (v) cnt = atoi(v) - start;
            if (c == n) break;
            cnt++;
        }
        idx = rev ? (attr_get(par, "start") ? start : total) - cnt : start + cnt;
    }
    switch (s->lstype) {
    case LS_DECIMAL: snprintf(out, (size_t)cap, "%d.", idx); break;
    case LS_LALPHA: case LS_UALPHA: {
        char t[16];
        int k = 0, v = idx;
        while (v > 0 && k < 15) { v--; t[k++] = (char)((s->lstype == LS_LALPHA ? 'a' : 'A') + v % 26); v /= 26; }
        int o = 0;
        while (k > 0) out[o++] = t[--k];
        out[o++] = '.';
        out[o] = 0;
        break;
    }
    case LS_LROMAN: case LS_UROMAN: {
        static const int vals[] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
        static const char* syms[] = { "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
        int v = idx, o = 0;
        for (int i = 0; i < 13 && o < cap - 6; i++) while (v >= vals[i]) { for (const char* q = syms[i]; *q; q++) out[o++] = s->lstype == LS_UROMAN ? (char)(*q - 32) : *q; v -= vals[i]; }
        out[o++] = '.';
        out[o] = 0;
        break;
    }
    }
}

static void reset_inline_rects(box* c) {
    for (box* d = c->first; d; d = d->next) if (d->kind == BX_INLINE) { d->x = d->y = d->w = d->h = 0; reset_inline_rects(d); }
}
static float layout_inline(box* c, float cx, float cy, float cw, bfc* f, int first_marker) {
    plist L = { 0, 0, 0 };
    reset_inline_rects(c);
    g_lastsp = 1;
    style* cst = c->st;
    flatten(&L, c, cst);
    while (L.n > 0 && L.p[L.n - 1].kind == PI_WORD && 0) L.n--;
    fontm cm;
    font_metrics(cst->face, cst->fsize, &cm);
    float clh = lineh_of(cst);
    float chl = (clh - (cm.ascent + cm.descent)) / 2;
    float strut_a = cm.ascent + chl, strut_b = cm.descent + chl;
    float y = cy;
    int i = 0;
    int lineno = 0;
    openrec open[32];
    int nopen = 0;
    c->haslines = 0;
    c->firstbase = -1;
    char marker[32] = "";
    if (first_marker && c->st->lstype != LS_NONE) list_marker_text(c, marker, sizeof(marker));
    int any_content = 0;
    for (int k = 0; k < L.n; k++) if (L.p[k].kind == PI_WORD || L.p[k].kind == PI_ATOM || L.p[k].kind == PI_BR) { any_content = 1; break; }
    if (!any_content) {
        for (int k = 0; k < L.n; k++) {
            piece* p = &L.p[k];
            if (p->kind == PI_FLOAT) place_float(p->b, f, cx, cx + cw, y, cw);
            else if (p->kind == PI_ABS) { p->b->staticx = cx; p->b->staticy = y; add_abs(p->b); }
        }
        int hasib = 0;
        for (int k = 0; k < L.n; k++) if (L.p[k].kind == PI_OPEN && (L.p[k].b->st->padding[1].px > 0 || L.p[k].b->st->border[1] > 0 || L.p[k].b->st->margin[1].px > 0 || L.p[k].b->st->padding[3].px > 0 || L.p[k].b->st->border[3] > 0)) hasib = 1;
        if (!hasib) return 0;
    }
    while (i < L.n) {
        float lx0, lx1;
        float probe_h = clh > 0 ? clh : 16;
        float_space(f, y, probe_h, cx, cx + cw, &lx0, &lx1);
        float indent = (lineno == 0 && !c->lindex) ? cst->tindent : 0;
        float avail = lx1 - lx0 - indent;
        int start = i;
        float x = 0;
        int lastbrk = -1;
        float lastbrkx = 0;
        int forced = 0;
        int placed = 0;
        int j = i;
        box* deferred[16];
        int ndef = 0;
        while (j < L.n) {
            piece* p = &L.p[j];
            if (p->kind == PI_BR) { j++; forced = 1; break; }
            if (p->kind == PI_FLOAT && p->x == -1) { j++; continue; }
            if (p->kind == PI_FLOAT) {
                box* fb = p->b;
                float fw;
                box_edges(fb, cw);
                if (fb->kind == BX_REPLACED) { float hh; fw = replaced_size(fb, cw, -1, &hh, -1); } else fw = block_width(fb, cw, 1);
                fw += fb->m[1] + fb->m[3];
                if (placed == 0 || fw <= avail - x) {
                    place_float(fb, f, cx, cx + cw, y, cw);
                    float nl, nr;
                    float_space(f, y, probe_h, cx, cx + cw, &nl, &nr);
                    float shift = nl - lx0;
                    lx0 = nl;
                    lx1 = nr;
                    avail = lx1 - lx0 - indent;
                    (void)shift;
                } else if (ndef < 16) deferred[ndef++] = fb;
                p->x = -1;
                j++;
                continue;
            }
            if (p->kind == PI_ABS) { p->x = x; j++; continue; }
            if (p->kind == PI_OPEN) {
                box* ib = p->b;
                box_edges(ib, cw);
                p->w = ib->m[3] + ib->b[3] + ib->p[3];
                p->x = x;
                x += p->w;
                j++;
                continue;
            }
            if (p->kind == PI_CLOSE) {
                box* ib = p->b;
                p->w = ib->m[1] + ib->b[1] + ib->p[1];
                p->x = x;
                x += p->w;
                if (p->brk) { lastbrk = j; lastbrkx = x; }
                j++;
                continue;
            }
            float w;
            if (p->kind == PI_ATOM) {
                float h;
                w = measure_atom(p->b, cw, &h) + p->b->m[1] + p->b->m[3];
                p->w = w;
            } else w = p->w;
            int canwrap = p->st->ws != WS_NOWRAP && p->st->ws != WS_PRE;
            if (placed > 0 && x + w > avail + 0.01f && lastbrk >= start) {
                if (canwrap || L.p[lastbrk].st->ws != WS_NOWRAP) { j = lastbrk + 1; x = lastbrkx; break; }
            }
            if (p->kind == PI_WORD && w > avail && placed == 0 && (p->brk == 2 || p->st->wbreak) && p->n > 1) {
                int cut = 0;
                float cwid = 0;
                while (cut < p->n) {
                    int cp;
                    int k = u8dec(p->s + cut, p->n - cut, &cp);
                    float aw = font_adv(p->st->face, p->st->fsize, cp) + p->st->lspace;
                    if (cwid + aw > avail - x && cut > 0) break;
                    cwid += aw;
                    cut += k;
                }
                if (cut < p->n) {
                    piece rest = *p;
                    rest.s = p->s + cut;
                    rest.n = p->n - cut;
                    rest.w = text_width(p->st->face, p->st->fsize, rest.s, rest.n, p->st->lspace);
                    p->n = cut;
                    p->w = cwid;
                    p->spw = 0;
                    p->brk = 1;
                    padd(&L, &rest);
                    memmove(&L.p[j + 2], &L.p[j + 1], sizeof(piece) * (size_t)(L.n - j - 2));
                    L.p[j + 1] = rest;
                    p = &L.p[j];
                    w = cwid;
                }
            }
            p->x = x;
            x += w + p->spw;
            placed++;
            if (p->brk) { lastbrk = j; lastbrkx = x; }
            j++;
        }
        int end = j;
        if (end == start && i < L.n) end = start + 1;
        int lastvis = -1;
        for (int k = start; k < end; k++) if (L.p[k].kind == PI_WORD || L.p[k].kind == PI_ATOM) lastvis = k;
        float linew = 0;
        for (int k = start; k < end; k++) {
            piece* p = &L.p[k];
            if (p->kind == PI_FLOAT || p->kind == PI_ABS || p->kind == PI_BR) continue;
            float e = p->x + p->w + ((k == lastvis || p->kind != PI_WORD) ? 0 : p->spw);
            if (k < lastvis && p->kind == PI_WORD) e = p->x + p->w + p->spw;
            if (e > linew) linew = e;
        }
        if (lastvis >= 0) { float e = L.p[lastvis].x + L.p[lastvis].w; for (int k = lastvis + 1; k < end; k++) if (L.p[k].kind == PI_CLOSE) e += L.p[k].w; if (e < linew || 1) linew = e; }
        int has_vis = lastvis >= 0;
        for (int k = start; k < end; k++) if (L.p[k].kind == PI_OPEN || L.p[k].kind == PI_CLOSE) { if (L.p[k].w > 0) has_vis = 1; }
        if (!has_vis && !forced) {
            for (int k = start; k < end; k++) if (L.p[k].kind == PI_ABS) { L.p[k].b->staticx = lx0; L.p[k].b->staticy = y; add_abs(L.p[k].b); }
            i = end;
            for (int d = 0; d < ndef; d++) place_float(deferred[d], f, cx, cx + cw, y, cw);
            continue;
        }
        float above = has_vis ? strut_a : 0, below = has_vis ? strut_b : 0;
        if (!has_vis && forced) { above = strut_a; below = strut_b; }
        float tops[1];
        (void)tops;
        style* parents[32];
        int np = 0;
        parents[np++] = cst;
        for (int k = start; k < end; k++) {
            piece* p = &L.p[k];
            if (p->kind == PI_OPEN) { if (np < 32) parents[np++] = p->st; continue; }
            if (p->kind == PI_CLOSE) { if (np > 1) np--; continue; }
            if (p->kind == PI_WORD || (p->kind == PI_BR && 0)) {
                float a, b2;
                pmetrics(p, &a, &b2);
                float sh = valign_shift(p->st, parents[np - 1]);
                for (int q = 1; q < np; q++) sh += valign_shift(parents[q], parents[q - 1]);
                if (a - sh > above) above = a - sh;
                if (b2 + sh > below) below = b2 + sh;
            } else if (p->kind == PI_ATOM) {
                box* a = p->b;
                float ah = a->h + a->m[0] + a->m[2];
                int va = a->st->valign;
                if (va == VA_MIDDLE) {
                    float mid = cm.xh / 2;
                    if (ah / 2 + mid > above) above = ah / 2 + mid;
                    if (ah / 2 - mid > below) below = ah / 2 - mid;
                } else if (va == VA_TOP || va == VA_BOTTOM || va == VA_TEXTTOP || va == VA_TEXTBOTTOM) {
                    if (ah > above + below) { if (va == VA_BOTTOM || va == VA_TEXTBOTTOM) above = ah - below; else below = ah - above; }
                } else {
                    float bl = a->kind != BX_REPLACED && a->haslines && a->firstbase >= 0 && a->st->ovy == OV_VISIBLE ? (a->firstbase - a->y) + a->m[0] : ah;
                    float sh = valign_shift(a->st, parents[np - 1]);
                    if (bl - sh > above) above = bl - sh;
                    if (ah - bl + sh > below) below = ah - bl + sh;
                }
            }
        }
        for (int k = start; k < end; k++) if (L.p[k].kind == PI_OPEN && L.p[k].st != cst) {
            float a, b2;
            pmetrics(&L.p[k], &a, &b2);
            if (has_vis && a > above && clh > 0) above = a;
            if (has_vis && b2 > below && clh > 0) below = b2;
        }
        float lineh = above + below;
        if (lineh < 0) lineh = 0;
        float base = y + above;
        float_space(f, y, lineh, cx, cx + cw, &lx0, &lx1);
        avail = lx1 - lx0 - indent;
        float off = lx0 + indent;
        int ta = cst->talign;
        float slack = avail - linew;
        if (ta == TA_CENTER && slack > 0) off += slack / 2;
        else if (ta == TA_RIGHT && slack > 0) off += slack;
        float jextra = 0;
        int gaps = 0;
        if (ta == TA_JUSTIFY && !forced && end < L.n && slack > 0) {
            for (int k = start; k < lastvis; k++) if (L.p[k].kind == PI_WORD && L.p[k].spw > 0) gaps++;
            if (gaps) jextra = slack / gaps;
        }
        if (lineno == 0 && marker[0] == 0 && first_marker && c->st->lstype != LS_NONE) {
            frag m = { FK_MARKER, cx - 14 - (c->st->lspos ? 0 : 0), base, 6, 6, base, 0, c->st->lstype, c->st, c->n, c };
            if (c->st->lspos) { m.x = off; }
            frag_add(c, &m);
        } else if (lineno == 0 && marker[0]) {
            float mw = text_width(cst->face, cst->fsize, marker, (int)strlen(marker), 0);
            char* ms = nstrdup(marker);
            frag m = { FK_TEXT, cx - mw - 6, base - above, mw, lineh, base, ms, (int)strlen(ms), cst, c->n, c };
            frag_add(c, &m);
        }
        if (lineno == 0) { c->firstbase = base; c->haslines = 1; }
        c->lindex = lineno + 1;
        for (int q = 0; q < nopen; q++) open[q].x0 = off;
        float jacc = 0;
        np = 1;
        for (int k = start; k < end; k++) {
            piece* p = &L.p[k];
            float px = off + p->x + jacc;
            if (p->kind == PI_OPEN) {
                if (np < 32) parents[np++] = p->st;
                if (nopen < 32) { open[nopen].ib = p->b; open[nopen].x0 = px + p->b->m[3]; open[nopen].open = 1; nopen++; }
                continue;
            }
            if (p->kind == PI_CLOSE) {
                if (np > 1) np--;
                for (int q = nopen - 1; q >= 0; q--) if (open[q].ib == p->b) {
                    box* ib = p->b;
                    float a, b2;
                    pmetrics(p, &a, &b2);
                    fontm fm;
                    font_metrics(ib->st->face, ib->st->fsize, &fm);
                    float top = base - fm.ascent - ib->p[0] - ib->b[0];
                    float h = fm.ascent + fm.descent + ib->p[0] + ib->p[2] + ib->b[0] + ib->b[2];
                    frag g = { FK_IBG, open[q].x0, top, px + p->w - ib->m[1] - open[q].x0, h, base, 0, 1 | 2, ib->st, ib->n, ib };
                    frag_add(c, &g);
                    if (ib->w == 0 && ib->h == 0) { ib->x = open[q].x0; ib->y = top; ib->w = g.w; ib->h = h; }
                    else { float nx0 = fminf(ib->x, open[q].x0), ny0 = fminf(ib->y, top); float nx1 = fmaxf(ib->x + ib->w, open[q].x0 + g.w), ny1 = fmaxf(ib->y + ib->h, top + h); ib->x = nx0; ib->y = ny0; ib->w = nx1 - nx0; ib->h = ny1 - ny0; }
                    for (int r = q; r < nopen - 1; r++) open[r] = open[r + 1];
                    nopen--;
                    break;
                }
                continue;
            }
            if (p->kind == PI_ABS) { p->b->staticx = px; p->b->staticy = y; add_abs(p->b); continue; }
            if (p->kind == PI_FLOAT || p->kind == PI_BR) continue;
            float sh = 0;
            for (int q = 1; q < np; q++) sh += valign_shift(parents[q], parents[q - 1]);
            if (p->kind == PI_WORD) {
                sh += valign_shift(p->st, parents[np - 1]);
                float a, b2;
                pmetrics(p, &a, &b2);
                float wdt = p->w + ((k < lastvis) ? p->spw : 0);
                frag t = { FK_TEXT, px, base + sh - a, wdt, a + b2, base + sh, p->s, p->n, p->st, p->tn, p->b };
                frag_add(c, &t);
                if (jextra > 0 && p->spw > 0 && k < lastvis) jacc += jextra;
            } else if (p->kind == PI_ATOM) {
                box* a = p->b;
                float ah = a->h + a->m[0] + a->m[2];
                float topy;
                int va = a->st->valign;
                if (va == VA_MIDDLE) topy = base - cm.xh / 2 - ah / 2;
                else if (va == VA_TOP || va == VA_TEXTTOP) topy = y;
                else if (va == VA_BOTTOM || va == VA_TEXTBOTTOM) topy = y + lineh - ah;
                else {
                    float bl = a->kind != BX_REPLACED && a->haslines && a->firstbase >= 0 && a->st->ovy == OV_VISIBLE ? (a->firstbase - a->y) + a->m[0] : ah;
                    topy = base + sh + valign_shift(a->st, parents[np - 1]) - bl;
                }
                float nx = px + a->m[3], ny = topy + a->m[0];
                if (a->kind == BX_REPLACED) { a->x = nx; a->y = ny; }
                else shift_box(a, nx - a->x, ny - a->y);
                frag fb = { FK_BOX, a->x, a->y, a->w, a->h, base, 0, 0, a->st, a->n, a };
                frag_add(c, &fb);
                if (jextra > 0 && p->spw > 0 && k < lastvis) jacc += jextra;
            }
        }
        for (int q = 0; q < nopen; q++) {
            box* ib = open[q].ib;
            fontm fm;
            font_metrics(ib->st->face, ib->st->fsize, &fm);
            float top = base - fm.ascent - ib->p[0] - ib->b[0];
            float h = fm.ascent + fm.descent + ib->p[0] + ib->p[2] + ib->b[0] + ib->b[2];
            float x1 = off + linew + jacc;
            frag g = { FK_IBG, open[q].x0, top, x1 - open[q].x0, h, base, 0, 1, ib->st, ib->n, ib };
            frag_add(c, &g);
            if (ib->w == 0) { ib->x = open[q].x0; ib->y = top; ib->w = g.w; ib->h = h; }
        }
        y += lineh;
        lineno++;
        i = end;
        for (int d = 0; d < ndef; d++) place_float(deferred[d], f, cx, cx + cw, y, cw);
        if (forced && i >= L.n && 0) {}
    }
    return y - cy;
}

void inline_measure(box* c, float* mn, float* mx) {
    plist L = { 0, 0, 0 };
    g_lastsp = 1;
    flatten(&L, c, c->st);
    float line = 0, best = 0, word = 0, maxword = 0;
    int nowrap = c->st->ws == WS_NOWRAP || c->st->ws == WS_PRE;
    for (int i = 0; i < L.n; i++) {
        piece* p = &L.p[i];
        switch (p->kind) {
        case PI_WORD:
            word += p->w;
            line += p->w + p->spw;
            if ((p->brk || p->spw > 0) && !nowrap && p->st->ws != WS_NOWRAP && p->st->ws != WS_PRE) { if (word > maxword) maxword = word; word = 0; }
            if (p->brk == 2) { float cwid = p->st->fsize * 0.6f; if (cwid > maxword) maxword = cwid; word = 0; }
            break;
        case PI_OPEN: case PI_CLOSE: {
            box_edges(p->b, 0);
            float e = p->kind == PI_OPEN ? p->b->m[3] + p->b->b[3] + p->b->p[3] : p->b->m[1] + p->b->b[1] + p->b->p[1];
            line += e;
            word += e;
            break;
        }
        case PI_ATOM: {
            float a, b2;
            intrinsic(p->b, &a, &b2);
            box_edges(p->b, 0);
            float mg = p->b->m[1] + p->b->m[3];
            if (word + a + mg > maxword) maxword = word + a + mg;
            word = 0;
            line += b2 + mg + p->spw;
            break;
        }
        case PI_BR:
            if (line > best) best = line;
            if (word > maxword) maxword = word;
            line = 0;
            word = 0;
            break;
        case PI_FLOAT: {
            float a, b2;
            intrinsic(p->b, &a, &b2);
            box_edges(p->b, 0);
            float mg = p->b->m[1] + p->b->m[3];
            if (a + mg > maxword) maxword = a + mg;
            line += b2 + mg;
            break;
        }
        default: break;
        }
    }
    if (line > best) best = line;
    if (word > maxword) maxword = word;
    if (nowrap) maxword = best;
    *mn = maxword + (c->st->tindent > 0 ? c->st->tindent : 0);
    *mx = best + (c->st->tindent > 0 ? c->st->tindent : 0);
}

static float collapse_m(float a, float b) {
    if (a >= 0 && b >= 0) return a > b ? a : b;
    if (a < 0 && b < 0) return a < b ? a : b;
    return a + b;
}

static int through_top(box* b) { return !b->bfcroot && b->b[0] == 0 && b->p[0] == 0 && b->kind != BX_REPLACED && b->kind != BX_TABLE && b->parent; }
static int through_bottom(box* b) { return !b->bfcroot && b->b[2] == 0 && b->p[2] == 0 && b->st->height.k != LK_VAL && b->kind != BX_REPLACED && b->kind != BX_TABLE && b->parent; }

static float layout_blocks(box* b, float cx, float cy, float cw, float ch, bfc* f, float* trailing, float* leading) {
    nwe_check_stop();
    float cur = cy;
    float pend = 0;
    int first = 1;
    int top_thru = through_top(b);
    *leading = 0;
    for (box* c = b->first; c; c = c->next) {
        if (oof(c)) {
            if (c->st->flt != FL_NONE) { float py = cur + (first ? 0 : pend); place_float(c, f, cx, cx + cw, py, cw); }
            else { c->staticx = cx; c->staticy = cur + (first ? 0 : pend); add_abs(c); }
            continue;
        }
        box_edges(c, cw);
        c->bfcroot = is_bfc_root(c);
        float mt = c->m[0];
        if (c->st->clear && f) {
            float cyc = clear_y(f, c->st->clear, cur + pend);
            if (cyc > cur + collapse_m(pend, mt)) { cur = cyc; pend = 0; mt = 0; }
        }
        float offset;
        if (first && top_thru) { *leading = mt; offset = 0; }
        else offset = collapse_m(pend, mt);
        float bx = cx + c->m[3];
        float by = cur + offset;
        float fw = -1;
        if (c->bfcroot && f && f->floats && c->kind != BX_TABLE) {
            float l, r;
            float wtry = block_width(c, cw, 0);
            for (int g = 0; g < 50; g++) {
                float_space(f, by, 20, cx, cx + cw, &l, &r);
                if (r - l >= wtry || (l == cx && r == cx + cw)) break;
                float ny = 1e30f;
                for (flt* q = f->floats; q; q = q->next) if (q->y + q->h > by && q->y + q->h < ny) ny = q->y + q->h;
                if (ny >= 1e29f) break;
                if (c->st->width.k != LK_VAL) break;
                by = ny;
            }
            float_space(f, by, 20, cx, cx + cw, &l, &r);
            if (l > cx || r < cx + cw) {
                if (c->st->width.k != LK_VAL) fw = clamp_w(c, (r - l) - c->m[1] - c->m[3], cw);
                bx = l + c->m[3];
            }
        }
        lay(c, bx, by, cw, ch, c->bfcroot ? 0 : f, fw, -1);
        if (first && top_thru && c->mte != mt) { *leading = collapse_m(*leading, c->mte); }
        else if (c->mte != c->m[0]) {
            float extra = collapse_m(pend, c->mte) - offset;
            if (!(first && top_thru) && extra != 0) { shift_box(c, 0, extra); }
        }
        if (c->h == 0 && c->b[0] == 0 && c->b[2] == 0 && c->p[0] == 0 && c->p[2] == 0 && !c->first && c->kind != BX_REPLACED) {
            pend = collapse_m(offset, collapse_m(c->mte, c->mbe));
            if (first && top_thru) { *leading = collapse_m(*leading, c->mbe); pend = 0; }
            else { shift_box(c, 0, -offset); pend = collapse_m(pend, 0); }
            if (!(first && top_thru)) first = 0;
            continue;
        }
        cur = c->y + c->h;
        pend = c->mbe;
        first = 0;
    }
    *trailing = pend;
    return cur - cy;
}

static void shift_inline(box* c, float dx, float dy);
static void shift_child(box* c, float dx, float dy) {
    if (c->kind == BX_TEXT || c->kind == BX_BR) return;
    if (c->st && (c->st->position == P_ABSOLUTE || c->st->position == P_FIXED)) { c->staticx += dx; c->staticy += dy; return; }
    if (c->kind == BX_INLINE) { shift_inline(c, dx, dy); return; }
    shift_box(c, dx, dy);
}
static void shift_inline(box* c, float dx, float dy) {
    c->x += dx;
    c->y += dy;
    for (box* d = c->first; d; d = d->next) shift_child(d, dx, dy);
}
void shift_box(box* b, float dx, float dy) {
    if (dx == 0 && dy == 0) return;
    b->x += dx;
    b->y += dy;
    if (b->firstbase >= 0) b->firstbase += dy;
    b->staticx += dx;
    b->staticy += dy;
    for (int i = 0; i < b->nfr; i++) { b->fr[i].x += dx; b->fr[i].y += dy; b->fr[i].base += dy; }
    for (box* c = b->first; c; c = c->next) shift_child(c, dx, dy);
}

void lay(box* b, float x, float y, float cbw, float cbh, bfc* f, float forcew, float forceh) {
    if (++g_lay_depth > 220) { g_lay_depth--; b->x = x; b->y = y; b->w = 0; b->h = 0; return; }
    style* s = b->st;
    box_edges(b, cbw);
    float m3pre = b->m[3];
    b->bfcroot = is_bfc_root(b);
    b->nfr = 0;
    b->firstbase = -1;
    b->haslines = 0;
    b->lindex = 0;
    float W;
    if (forcew >= 0) W = forcew;
    else {
        int shrink = s->flt != FL_NONE || s->position == P_ABSOLUTE || s->position == P_FIXED || s->display == D_IBLOCK || s->display == D_IFLEX || s->display == D_IGRID || s->display == D_ITABLE || b->kind == BX_TABLE || (b->n && b->n->tag == T_BUTTON);
        W = block_width(b, cbw, shrink);
        if (!shrink) x += b->m[3] - m3pre;
    }
    b->x = x;
    b->y = y;
    b->w = W;
    float cx = x + b->b[3] + b->p[3];
    float cy = y + b->b[0] + b->p[0];
    float cw = W - b->b[1] - b->b[3] - b->p[1] - b->p[3];
    if (cw < 0) cw = 0;
    float sh = spec_content_h(b, cbh);
    if (forceh >= 0) sh = forceh - b->p[0] - b->p[2] - b->b[0] - b->b[2];
    if (sh < 0 && s->aspect > 0 && s->height.k != LK_VAL) sh = (W / s->aspect) - (s->boxsizing ? b->p[0] + b->p[2] + b->b[0] + b->b[2] : 0);
    float defh = sh >= 0 ? clamp_h(b, sh, cbh) : -1;
    float contenth = 0;
    b->mte = b->m[0];
    b->mbe = b->m[2];
    switch (b->kind) {
    case BX_REPLACED: {
        float h;
        float w2 = replaced_size(b, cbw, cbh, &h, forcew);
        (void)w2;
        contenth = h - b->p[0] - b->p[2] - b->b[0] - b->b[2];
        if (forceh >= 0) contenth = forceh - b->p[0] - b->p[2] - b->b[0] - b->b[2];
        break;
    }
    case BX_FLEX: contenth = layout_flex(b, cx, cy, cw, defh); break;
    case BX_GRID: contenth = layout_grid(b, cx, cy, cw, defh); break;
    case BX_TABLE: contenth = layout_table(b, cx, cy, cw, defh); break;
    case BX_BR: case BX_TEXT: case BX_INLINE: contenth = 0; break;
    default: {
        bfc local = { 0 };
        bfc* ff = b->bfcroot || !f ? &local : f;
        if (b->inl) {
            contenth = layout_inline(b, cx, cy, cw, ff, s->display == D_LISTITEM);
        } else {
            float trailing, leading;
            contenth = layout_blocks(b, cx, cy, cw, defh, ff, &trailing, &leading);
            if (s->display == D_LISTITEM && s->lstype != LS_NONE) {
                box* fc = b->first;
                while (fc && fc->kind != BX_ANON && fc->kind != BX_BLOCK) fc = fc->next;
                float base = fc && fc->firstbase >= 0 ? fc->firstbase : cy + lineh_of(s) * 0.8f;
                char mk[32] = "";
                list_marker_text(b, mk, sizeof(mk));
                if (mk[0]) {
                    float mw = text_width(s->face, s->fsize, mk, (int)strlen(mk), 0);
                    char* ms = nstrdup(mk);
                    frag m = { FK_TEXT, cx - mw - 6, base - s->fsize, mw, s->fsize, base, ms, (int)strlen(ms), s, b->n, b };
                    frag_add(b, &m);
                } else {
                    frag m = { FK_MARKER, cx - 14, base, 6, 6, base, 0, s->lstype, s, b->n, b };
                    frag_add(b, &m);
                }
            }
            if (through_top(b)) {
                b->mte = collapse_m(b->m[0], leading);
            } else if (leading != 0) {
                contenth += leading;
            }
            if (through_bottom(b) && defh < 0) b->mbe = collapse_m(b->m[2], trailing);
            else contenth += trailing > 0 ? trailing : 0;
            box* fc = b->first;
            while (fc && !in_flow(fc)) fc = fc->next;
            if (fc && fc->firstbase >= 0) { b->firstbase = fc->firstbase; b->haslines = 1; }
            box* lcb = b->last;
            if (lcb && lcb->haslines) b->lindex = lcb->lindex;
        }
        if (b->bfcroot && ff == &local) {
            float fb = float_bottom(&local, 3);
            if (fb > cy + contenth) contenth = fb - cy;
        }
        break;
    }
    }
    b->clampy = 0;
    if (s->lineclamp > 0 && b->nfr > 0 && defh < 0) {
        float ys[64];
        int ny = 0;
        for (int i = 0; i < b->nfr; i++) {
            frag* f = &b->fr[i];
            if (f->kind != FK_TEXT && f->kind != FK_BOX && f->kind != FK_IBG) continue;
            float fb = f->y + f->h;
            int seen = 0;
            for (int j = 0; j < ny; j++) if (fabsf(ys[j] - f->y) < 2) { seen = 1; break; }
            if (!seen && ny < 64) ys[ny++] = f->y;
            (void)fb;
        }
        for (int a1 = 1; a1 < ny; a1++) { float t = ys[a1]; int c2 = a1 - 1; while (c2 >= 0 && ys[c2] > t) { ys[c2 + 1] = ys[c2]; c2--; } ys[c2 + 1] = t; }
        if (ny > s->lineclamp) {
            float lim = ys[s->lineclamp] - cy;
            if (lim < contenth) contenth = lim;
            b->clampy = ys[s->lineclamp - 1];
        }
    }
    b->contenth = contenth;
    float H = defh >= 0 ? defh : clamp_h(b, contenth, cbh);
    if (forceh >= 0) H = forceh - b->p[0] - b->p[2] - b->b[0] - b->b[2];
    if (H < 0) H = 0;
    b->h = H + b->p[0] + b->p[2] + b->b[0] + b->b[2];
    if (b->kind == BX_REPLACED) b->h = contenth + b->p[0] + b->p[2] + b->b[0] + b->b[2];
    b->ox = 0;
    b->oy = 0;
    if (s->position == P_RELATIVE || s->position == P_STICKY) {
        if (s->inset[3].k == LK_VAL) b->ox = lres(s->inset[3], cbw);
        else if (s->inset[1].k == LK_VAL) b->ox = -lres(s->inset[1], cbw);
        if (s->inset[0].k == LK_VAL && !(s->inset[0].pct != 0 && cbh < 0)) b->oy = lres(s->inset[0], cbh < 0 ? 0 : cbh);
        else if (s->inset[2].k == LK_VAL && !(s->inset[2].pct != 0 && cbh < 0)) b->oy = -lres(s->inset[2], cbh < 0 ? 0 : cbh);
        if (s->position == P_STICKY) b->ox = b->oy = 0;
    }
    if (s->hastransform) {
        float m[6];
        tf_matrix(s, b->w, b->h, m);
        b->btx = m[4]; b->bty = m[5];
        b->ox += m[4];
        b->oy += m[5];
    } else b->btx = b->bty = 0;
    g_lay_depth--;
}

static box* find_cb(box* b) {
    for (box* p = b->parent; p; p = p->parent) {
        if (!p->st) continue;
        if (p->st->position != P_STATIC && p->kind != BX_INLINE && p->kind != BX_TEXT) return p;
        if (p->st->hastransform && p->kind != BX_INLINE) return p;
    }
    return 0;
}

static void layout_abs(box* b) {
    style* s = b->st;
    box* cb = s->position == P_FIXED ? 0 : find_cb(b);
    float cx, cy, cw, ch;
    if (cb) { cx = cb->x + cb->b[3]; cy = cb->y + cb->b[0]; cw = cb->w - cb->b[1] - cb->b[3]; ch = cb->h - cb->b[0] - cb->b[2]; }
    else { cx = 0; cy = 0; cw = g_vw; ch = g_vh; }
    b->cb = cb;
    box_edges(b, cw);
    L* in = s->inset;
    int hl = in[3].k == LK_VAL, hr = in[1].k == LK_VAL, ht = in[0].k == LK_VAL, hb = in[2].k == LK_VAL;
    float left = hl ? lres(in[3], cw) : 0, right = hr ? lres(in[1], cw) : 0, top = ht ? lres(in[0], ch) : 0, bottom = hb ? lres(in[2], ch) : 0;
    float W;
    float pbw = b->p[1] + b->p[3] + b->b[1] + b->b[3];
    int mla = s->margin[3].k == LK_AUTO, mra = s->margin[1].k == LK_AUTO;
    if (mla) b->m[3] = 0;
    if (mra) b->m[1] = 0;
    if (b->kind == BX_REPLACED) { float h; W = replaced_size(b, cw, ch, &h, -1); }
    else if (s->width.k == LK_VAL) { W = lres(s->width, cw) + (s->boxsizing ? 0 : pbw); W = clamp_w(b, W, cw); }
    else if (hl && hr) { W = cw - left - right - b->m[1] - b->m[3]; W = clamp_w(b, W, cw); }
    else { W = shrink_fit(b, cw - (hl ? left : 0) - (hr ? right : 0) - b->m[1] - b->m[3]); W = clamp_w(b, W, cw); }
    if (hl && hr && (mla || mra)) {
        float rem = cw - left - right - W;
        if (mla && mra) { b->m[3] = b->m[1] = rem / 2; }
        else if (mla) b->m[3] = rem - b->m[1];
        else b->m[1] = rem - b->m[3];
    }
    float x;
    if (hl) x = cx + left + b->m[3];
    else if (hr) x = cx + cw - right - b->m[1] - W;
    else x = b->staticx + b->m[3];
    float fh = -1;
    if (ht && hb && s->height.k != LK_VAL) { fh = ch - top - bottom - b->m[0] - b->m[2]; if (fh < 0) fh = 0; }
    lay(b, x, 0, cw, ch, 0, W, fh);
    float y;
    int mta = s->margin[0].k == LK_AUTO, mba = s->margin[2].k == LK_AUTO;
    if (ht && hb && (mta || mba) && s->height.k == LK_VAL) {
        float rem = ch - top - bottom - b->h;
        b->m[0] = mta && mba ? rem / 2 : mta ? rem - b->m[2] : b->m[0];
    }
    if (ht) y = cy + top + b->m[0];
    else if (hb) y = cy + ch - bottom - b->m[2] - b->h;
    else y = b->staticy + b->m[0];
    shift_box(b, 0, y - b->y);
    if (s->hastransform) { float m[6]; tf_matrix(s, b->w, b->h, m); b->btx = m[4]; b->bty = m[5]; b->ox = m[4]; b->oy = m[5]; } else b->btx = b->bty = 0;
}

static void doc_extent(box* b, float* mx, float* my, int depth) {
    if (depth > 200) return;
    if (b->st && (b->st->ovx == OV_HIDDEN || b->st->ovy == OV_HIDDEN || b->st->ovx == OV_CLIP) && b->parent) {
        if (b->x + b->w > *mx) *mx = b->x + b->w;
        if (b->y + b->h + b->m[2] > *my) *my = b->y + b->h + b->m[2];
        return;
    }
    if (b->st && b->st->position == P_FIXED) return;
    float r = b->x + b->w + b->ox, bt = b->y + b->h + b->oy + (b->parent ? 0 : b->m[2]);
    if (r > *mx) *mx = r;
    if (bt > *my) *my = bt;
    for (int i = 0; i < b->nfr; i++) { frag* f = &b->fr[i]; if (f->y + f->h > *my) *my = f->y + f->h; }
    for (box* c = b->first; c; c = c->next) if (c->kind != BX_TEXT && c->kind != BX_BR) doc_extent(c, mx, my, depth + 1);
}

static void clear_boxes(node* n) {
    n->box = 0;
    if (n->shadow) clear_boxes(n->shadow);
    for (node* c = n->first; c; c = c->next) if (c->type == NT_ELEM) clear_boxes(c);
}

void layout_all(float vw, float vh) {
    g_vw = vw;
    g_vh = vh;
    int sv = g_pool;
    pool_reset(2);
    g_pool = 2;
    abs_head = abs_tail = 0;
    g_lay_depth = 0;
    node* html = doc_elem();
    if (!html || !html->st) { g_root = 0; g_doch = vh; g_pool = sv; return; }
    clear_boxes(G.doc);
    g_root = build_elem(html, 0);
    if (!g_root) { g_pool = sv; g_doch = vh; return; }
    node* body = doc_body();
    if (body && body->st && html->st->bgcolor == 0 && !html->st->bg) { }
    lay(g_root, 0, 0, vw, vh, 0, -1, -1);
    shift_box(g_root, g_root->m[3], g_root->mte > 0 ? g_root->mte : g_root->m[0]);
    for (int pass = 0; pass < 3; pass++) {
        box* a = abs_head;
        abs_head = abs_tail = 0;
        if (!a) break;
        while (a) { box* nx = a->nextabs; layout_abs(a); a = nx; }
    }
    float mx = vw, my = 0;
    doc_extent(g_root, &mx, &my, 0);
    g_docw = mx;
    g_doch = my > vh ? my : vh;
    g_pool = sv;
}

box* box_of(node* n) { return n ? n->box : 0; }
void box_rect(node* n, float* x, float* y, float* w, float* h) {
    *x = *y = *w = *h = 0;
    box* b = n->box;
    if (!b) {
        if (n->type == NT_TEXT && n->parent && n->parent->box) b = n->parent->box;
        else return;
    }
    float ox = 0, oy = 0;
    for (box* p = b; p; p = p->parent) { ox += p->ox; oy += p->oy; }
    *x = b->x + ox;
    *y = b->y + oy;
    *w = b->w;
    *h = b->h;
    if (b->st && b->st->position == P_FIXED) *y += G.scroll;
}

static box* hit_rec(box* b, float x, float y, float dx, float dy, frag** fo, int depth) {
    if (depth > 200 || !b->st) return 0;
    dx += b->ox;
    dy += b->oy;
    if (b->st->position == P_FIXED) dy += G.scroll;
    float bx = b->x + dx, by = b->y + dy;
    int clip = (b->st->ovx != OV_VISIBLE || b->st->ovy != OV_VISIBLE) && b->parent;
    if (clip && (x < bx || x >= bx + b->w || y < by || y >= by + b->h)) return 0;
    float sy = b->sy;
    box* best = 0;
    box* kids[512];
    int nk = 0;
    for (box* c = b->first; c && nk < 512; c = c->next) kids[nk++] = c;
    for (int pass = 1; pass >= 0; pass--) {
        for (int i = nk - 1; i >= 0; i--) {
            box* c = kids[i];
            if (!c->st) continue;
            int pos = c->st->position != P_STATIC || c->st->flt != FL_NONE;
            if ((pass == 1) != pos) continue;
            if (c->kind == BX_TEXT || c->kind == BX_BR) continue;
            if (c->kind == BX_INLINE) {
                box* r = hit_rec(c, x, y, dx, dy - sy, fo, depth + 1);
                if (r) return r;
                continue;
            }
            box* r = hit_rec(c, x, y, dx, dy - sy, fo, depth + 1);
            if (r) return r;
        }
    }
    for (int i = b->nfr - 1; i >= 0; i--) {
        frag* f = &b->fr[i];
        if (f->kind != FK_TEXT && f->kind != FK_IBG) continue;
        float fx = f->x + dx, fy = f->y + dy - sy;
        if (x >= fx && x < fx + f->w && y >= fy && y < fy + f->h) {
            if (fo) *fo = f;
            best = f->b ? f->b : b;
            if (f->kind == FK_IBG && f->b) return f->b;
            return best;
        }
    }
    if (b->kind == BX_INLINE) return 0;
    if (x >= bx && x < bx + b->w && y >= by && y < by + b->h) return b;
    return 0;
}

static int caret_rec(box* b, node* tn, int off, float dx, float dy, float* ox, float* oy, float* oh, int depth) {
    if (depth > 200 || !b->st) return 0;
    dx += b->ox;
    dy += b->oy;
    if (b->st->position == P_FIXED) dy += G.scroll;
    float sy = b->sy;
    for (int i = 0; i < b->nfr; i++) {
        frag* f = &b->fr[i];
        if (f->kind != FK_TEXT || !f->b || f->b->n != tn || !tn->text) continue;
        if (f->s < tn->text || f->s > tn->text + tn->tlen) continue;
        int o0 = (int)(f->s - tn->text);
        if (off < o0) continue;
        int nxt = 0;
        for (int j = i + 1; j < b->nfr; j++) { frag* g = &b->fr[j]; if (g->kind == FK_TEXT && g->b && g->b->n == tn && g->s >= tn->text && g->s <= tn->text + tn->tlen && (int)(g->s - tn->text) <= off) { nxt = 1; break; } }
        if (nxt) continue;
        int k = off - o0;
        float w = k >= f->n ? f->w + (k > f->n ? font_adv(f->st->face, f->st->fsize, ' ') : 0) : text_width(f->st->face, f->st->fsize, f->s, k, f->st->lspace);
        *ox = f->x + dx + w;
        *oy = f->y + dy - sy;
        *oh = f->h;
        return 1;
    }
    for (box* c = b->first; c; c = c->next) if (caret_rec(c, tn, off, dx, dy - sy, ox, oy, oh, depth + 1)) return 1;
    return 0;
}
int caret_pos(node* tn, int off, float* x, float* y, float* h) {
    if (!g_root || !tn) return 0;
    if (tn->type == NT_TEXT) return caret_rec(g_root, tn, off, 0, 0, x, y, h, 0);
    node* c = tn->first;
    for (int i = 0; c && i < off; i++) c = c->next;
    node* t = c;
    while (t && t->type != NT_TEXT) t = t->first;
    if (t && caret_rec(g_root, t, 0, 0, 0, x, y, h, 0)) return 1;
    node* pv = c ? c->prev : tn->last;
    while (pv && pv->type != NT_TEXT) pv = pv->last;
    if (pv && caret_rec(g_root, pv, pv->tlen, 0, 0, x, y, h, 0)) return 1;
    float bx, by, bw, bh;
    node* e = tn;
    if (c && c->type == NT_ELEM && c->box) e = c;
    if (!e->box) return 0;
    box_rect(e, &bx, &by, &bw, &bh);
    box* bb = e->box;
    *x = bx + (e == tn ? bb->b[3] + bb->p[3] : 0);
    *y = by + (e == tn ? bb->b[0] + bb->p[0] : 0);
    *h = e->st ? e->st->fsize * 1.2f : 16;
    return 1;
}

box* hit_test(float x, float y, frag** frout) {
    if (frout) *frout = 0;
    if (!g_root) return 0;
    return hit_rec(g_root, x, y, 0, 0, frout, 0);
}
