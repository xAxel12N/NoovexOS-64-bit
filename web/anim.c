#include "web/nwe.h"
#include "web/css.h"

typedef struct kfframe { float off; decl* d; int nd; tfunc tf; int hastf; } kfframe;
typedef struct kfset { char* name; kfframe* f; int n; struct kfset* next; } kfset;

enum { AV_NONE, AV_F, AV_COL, AV_L, AV_TF, AV_DISC };
typedef struct aval { unsigned char kind, ntf; unsigned col; float f; L l; tfn tf[8]; float rot, scale; L tx, ty; } aval;

#define AR_MAX 256
#define TR_MAX 256
#define EV_MAX 64

typedef struct arun {
    node* n;
    char name[64];
    double start;
    int gen, iters_done, started, ended, paused;
    double pause_at;
    animspec spec;
    kfset* kf;
    style* base;
    style** fs;
    int nfs;
    short props[48];
    int nprops;
} arun;

typedef struct trun { node* n; short prop; aval from, to; double start; float dur, delay; tfunc tf; int used; } trun;

static kfset* g_kf;
static arun AR[AR_MAX];
static trun TR[TR_MAX];
static int g_gen;
static struct { node* n; const char* t; } EV[EV_MAX];
static int nev;
static struct { node* n; short prop; aval v; } SNAP[512];
static int nsnap;
int g_anim_active;

style* css_style_with(node* n, style* base, style* par, decl* d, int nd);
unsigned long nwk_ms(void);
static double now_ms(void) { return (double)nwk_ms(); }

void kf_reset(void) { g_kf = 0; }
void* kf_head(void) { return g_kf; }
void* kf_next(void* n) { return ((kfset*)n)->next; }
void kf_relink(void* n) { kfset* k = n; k->next = g_kf; g_kf = k; }
void kf_add(const char* name, float* offs, decl** ds, int* nds, int n) {
    kfset* k = nalloc(sizeof(kfset));
    k->name = nstrdup(name);
    k->n = 0;
    k->f = nalloc(sizeof(kfframe) * (size_t)(n ? n : 1));
    for (int i = 0; i < n; i++) {
        kfframe* f = &k->f[k->n++];
        f->off = offs[i];
        f->d = ds[i];
        f->nd = nds[i];
        f->hastf = 0;
    }
    for (int i = 1; i < k->n; i++) for (int j = i; j > 0 && k->f[j].off < k->f[j - 1].off; j--) { kfframe t = k->f[j]; k->f[j] = k->f[j - 1]; k->f[j - 1] = t; }
    k->next = g_kf;
    g_kf = k;
}
static kfset* kf_find(const char* name) { for (kfset* k = g_kf; k; k = k->next) if (!strcmp(k->name, name)) return k; return 0; }

static int is_layout_prop(int p) {
    switch (p) {
    case P_WIDTH: case P_HEIGHT: case P_MINWIDTH: case P_MINHEIGHT: case P_MAXWIDTH: case P_MAXHEIGHT:
    case P_TOP: case P_RIGHT: case P_BOTTOM: case P_LEFT: case P_MT: case P_MR: case P_MB: case P_ML:
    case P_PT: case P_PR: case P_PB: case P_PL: case P_FONTSIZE: case P_LETTERSPACING: case P_BTW: case P_BRW: case P_BBW: case P_BLW:
        return 1;
    }
    return 0;
}

static int expand(int p, short* out) {
    switch (p) {
    case P_MARGIN: out[0] = P_MT; out[1] = P_MR; out[2] = P_MB; out[3] = P_ML; return 4;
    case P_PADDING: out[0] = P_PT; out[1] = P_PR; out[2] = P_PB; out[3] = P_PL; return 4;
    case P_INSET: out[0] = P_TOP; out[1] = P_RIGHT; out[2] = P_BOTTOM; out[3] = P_LEFT; return 4;
    case P_BORDERCOLOR: out[0] = P_BTC; out[1] = P_BRC; out[2] = P_BBC; out[3] = P_BLC; return 4;
    case P_BORDERWIDTH: out[0] = P_BTW; out[1] = P_BRW; out[2] = P_BBW; out[3] = P_BLW; return 4;
    case P_BORDER: out[0] = P_BTC; out[1] = P_BRC; out[2] = P_BBC; out[3] = P_BLC; return 4;
    case P_BACKGROUND: out[0] = P_BACKGROUNDCOLOR; return 1;
    case P_TRANSLATE: case P_ROTATE: case P_SCALE: case P_TRANSFORM: out[0] = P_TRANSFORM; return 1;
    case P_BORDERRADIUS: out[0] = P_BORDERRADIUS; return 1;
    case P_OPACITY: case P_COLOR: case P_BACKGROUNDCOLOR: case P_BTC: case P_BRC: case P_BBC: case P_BLC: case P_OUTLINECOLOR: case P_FILL: case P_STROKE:
    case P_WIDTH: case P_HEIGHT: case P_MINWIDTH: case P_MINHEIGHT: case P_MAXWIDTH: case P_MAXHEIGHT:
    case P_TOP: case P_RIGHT: case P_BOTTOM: case P_LEFT: case P_MT: case P_MR: case P_MB: case P_ML:
    case P_PT: case P_PR: case P_PB: case P_PL: case P_FONTSIZE: case P_LETTERSPACING: case P_BTW: case P_BRW: case P_BBW: case P_BLW:
    case P_VISIBILITY:
        out[0] = (short)p; return 1;
    }
    return 0;
}
static const short ALLP[] = { P_OPACITY, P_COLOR, P_BACKGROUNDCOLOR, P_BTC, P_BRC, P_BBC, P_BLC, P_OUTLINECOLOR, P_TRANSFORM, P_WIDTH, P_HEIGHT, P_MAXHEIGHT, P_MAXWIDTH, P_MINHEIGHT, P_TOP, P_RIGHT, P_BOTTOM, P_LEFT, P_MT, P_MR, P_MB, P_ML, P_PT, P_PR, P_PB, P_PL, P_VISIBILITY, P_BORDERRADIUS, P_FILL, P_LETTERSPACING };

static L* lfield(style* s, int p) {
    switch (p) {
    case P_WIDTH: return &s->width; case P_HEIGHT: return &s->height; case P_MINWIDTH: return &s->minw; case P_MINHEIGHT: return &s->minh;
    case P_MAXWIDTH: return &s->maxw; case P_MAXHEIGHT: return &s->maxh;
    case P_TOP: return &s->inset[0]; case P_RIGHT: return &s->inset[1]; case P_BOTTOM: return &s->inset[2]; case P_LEFT: return &s->inset[3];
    case P_MT: return &s->margin[0]; case P_MR: return &s->margin[1]; case P_MB: return &s->margin[2]; case P_ML: return &s->margin[3];
    case P_PT: return &s->padding[0]; case P_PR: return &s->padding[1]; case P_PB: return &s->padding[2]; case P_PL: return &s->padding[3];
    }
    return 0;
}
static unsigned* cfield(style* s, int p) {
    switch (p) {
    case P_COLOR: return &s->color; case P_BACKGROUNDCOLOR: return &s->bgcolor; case P_BTC: return &s->bcolor[0]; case P_BRC: return &s->bcolor[1];
    case P_BBC: return &s->bcolor[2]; case P_BLC: return &s->bcolor[3]; case P_OUTLINECOLOR: return &s->ocolor; case P_FILL: return &s->fill; case P_STROKE: return &s->stroke;
    }
    return 0;
}
static float* ffield(style* s, int p) {
    switch (p) {
    case P_OPACITY: return &s->opacity; case P_FONTSIZE: return &s->fsize; case P_LETTERSPACING: return &s->lspace;
    case P_BTW: return &s->border[0]; case P_BRW: return &s->border[1]; case P_BBW: return &s->border[2]; case P_BLW: return &s->border[3];
    case P_BORDERRADIUS: return &s->radius[0];
    }
    return 0;
}

static void get_val(style* s, int p, aval* v) {
    memset(v, 0, sizeof *v);
    if (!s) return;
    L* l = lfield(s, p);
    if (l) { v->kind = AV_L; v->l = *l; return; }
    unsigned* c = cfield(s, p);
    if (c) { v->kind = AV_COL; v->col = *c; return; }
    float* f = ffield(s, p);
    if (f) { v->kind = AV_F; v->f = *f; return; }
    if (p == P_TRANSFORM) {
        v->kind = AV_TF;
        v->ntf = s->ntf > 8 ? 8 : s->ntf;
        if (v->ntf) memcpy(v->tf, s->tf, sizeof(tfn) * v->ntf);
        v->rot = s->rotate; v->scale = s->scale; v->tx = s->tx; v->ty = s->ty;
        return;
    }
    if (p == P_VISIBILITY) { v->kind = AV_DISC; v->f = s->visibility; return; }
}

static void set_tf(style* s, aval* v) {
    if (v->ntf && s->tfcap < 8) { extern int g_pool; int sv = g_pool; g_pool = 1; s->tf = nalloc(sizeof(tfn) * 8); g_pool = sv; s->tfcap = 8; }
    s->ntf = v->ntf;
    if (v->ntf) memcpy(s->tf, v->tf, sizeof(tfn) * v->ntf);
    s->rotate = v->rot; s->scale = v->scale; s->tx = v->tx; s->ty = v->ty;
    s->hastransform = 1;
    s->hasmatrix = (unsigned char)tf_has_linear(s);
}

static void put_val(style* s, int p, aval* v) {
    if (!s || v->kind == AV_NONE) return;
    L* l = lfield(s, p);
    if (l && v->kind == AV_L) { *l = v->l; return; }
    unsigned* c = cfield(s, p);
    if (c && v->kind == AV_COL) { *c = v->col; return; }
    float* f = ffield(s, p);
    if (f && v->kind == AV_F) { *f = v->f; if (p == P_BORDERRADIUS) s->radius[1] = s->radius[2] = s->radius[3] = v->f; return; }
    if (p == P_TRANSFORM && v->kind == AV_TF) { set_tf(s, v); return; }
    if (p == P_VISIBILITY && v->kind == AV_DISC) { s->visibility = (unsigned char)v->f; return; }
}

static int same_val(aval* a, aval* b) {
    if (a->kind != b->kind) return 0;
    switch (a->kind) {
    case AV_F: case AV_DISC: return a->f == b->f;
    case AV_COL: return a->col == b->col;
    case AV_L: return a->l.px == b->l.px && a->l.pct == b->l.pct && a->l.k == b->l.k;
    case AV_TF: return a->ntf == b->ntf && !memcmp(a->tf, b->tf, sizeof(tfn) * a->ntf) && a->rot == b->rot && a->scale == b->scale && a->tx.px == b->tx.px && a->ty.px == b->ty.px && a->tx.pct == b->tx.pct && a->ty.pct == b->ty.pct;
    }
    return 1;
}

static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static L lerpl(L a, L b, float t) { if (a.k != LK_VAL || b.k != LK_VAL) return t < 0.5f ? a : b; L r = a; r.px = lerpf(a.px, b.px, t); r.pct = lerpf(a.pct, b.pct, t); return r; }
static unsigned lerpc(unsigned a, unsigned b, float t) {
    unsigned r = 0;
    for (int s = 0; s < 32; s += 8) { float x = (float)((a >> s) & 255), y = (float)((b >> s) & 255); int v = (int)(lerpf(x, y, t) + 0.5f); if (v < 0) v = 0; if (v > 255) v = 255; r |= (unsigned)v << s; }
    return r;
}
static void tf_identity_like(tfn* src, tfn* dst) {
    *dst = *src;
    if (src->t == TF_TRANSLATE) { dst->l[0] = (L){ 0, 0, -1e30f, 1e30f, LK_VAL }; dst->l[1] = dst->l[0]; }
    else if (src->t == TF_SCALE) { dst->v[0] = dst->v[1] = 1; }
    else if (src->t == TF_ROTATE) dst->v[0] = 0;
    else if (src->t == TF_SKEW) dst->v[0] = dst->v[1] = 0;
    else { dst->v[0] = 1; dst->v[1] = 0; dst->v[2] = 0; dst->v[3] = 1; dst->v[4] = 0; dst->v[5] = 0; }
}
static void to_matrix(aval* v, node* n, tfn* out) {
    style tmp;
    memset(&tmp, 0, sizeof tmp);
    tmp.tf = v->tf; tmp.ntf = v->ntf; tmp.rotate = v->rot; tmp.scale = v->scale ? v->scale : 1; tmp.tx = v->tx; tmp.ty = v->ty;
    float w = n && n->box ? n->box->w : 0, h = n && n->box ? n->box->h : 0;
    memset(out, 0, sizeof *out);
    out->t = TF_MATRIX;
    tf_matrix(&tmp, w, h, out->v);
}
static void lerp_tf(aval* a, aval* b, float t, aval* r, node* n) {
    *r = *a;
    r->kind = AV_TF;
    r->rot = lerpf(a->rot, b->rot, t);
    r->scale = lerpf(a->scale ? a->scale : 1, b->scale ? b->scale : 1, t);
    r->tx = lerpl(a->tx, b->tx, t);
    r->ty = lerpl(a->ty, b->ty, t);
    tfn A[8], B[8];
    int na = a->ntf, nb = b->ntf;
    memcpy(A, a->tf, sizeof(tfn) * (size_t)na);
    memcpy(B, b->tf, sizeof(tfn) * (size_t)nb);
    if (na == 0 && nb > 0) { for (int i = 0; i < nb; i++) tf_identity_like(&B[i], &A[i]); na = nb; }
    if (nb == 0 && na > 0) { for (int i = 0; i < na; i++) tf_identity_like(&A[i], &B[i]); nb = na; }
    int same = na == nb;
    for (int i = 0; same && i < na; i++) if (A[i].t != B[i].t) same = 0;
    if (!same) {
        aval aa = *a, bb = *b;
        aa.rot = bb.rot = 0; aa.scale = bb.scale = 1;
        aa.tx = bb.tx = (L){ 0, 0, -1e30f, 1e30f, LK_VAL }; aa.ty = bb.ty = aa.tx;
        to_matrix(&aa, n, &A[0]);
        to_matrix(&bb, n, &B[0]);
        na = nb = 1;
    }
    r->ntf = (unsigned char)na;
    for (int i = 0; i < na; i++) {
        tfn* o = &r->tf[i];
        *o = A[i];
        for (int k = 0; k < 6; k++) o->v[k] = lerpf(A[i].v[k], B[i].v[k], t);
        o->l[0] = lerpl(A[i].l[0], B[i].l[0], t);
        o->l[1] = lerpl(A[i].l[1], B[i].l[1], t);
    }
}
static void lerp_val(aval* a, aval* b, float t, aval* r, node* n) {
    if (a->kind == AV_NONE) { *r = *b; return; }
    if (b->kind == AV_NONE || a->kind != b->kind) { *r = t < 0.5f ? *a : *b; return; }
    *r = *a;
    switch (a->kind) {
    case AV_F: r->f = lerpf(a->f, b->f, t); break;
    case AV_COL: r->col = lerpc(a->col, b->col, t); break;
    case AV_L: r->l = lerpl(a->l, b->l, t); break;
    case AV_TF: lerp_tf(a, b, t, r, n); break;
    case AV_DISC: r->f = (a->f == 0 || b->f == 0) ? (t > 0 && t < 1 ? 0 : (t >= 1 ? b->f : a->f)) : (t < 0.5f ? a->f : b->f); break;
    }
}

static float bez_x(float u, float x1, float x2) { float v = 1 - u; return 3 * v * v * u * x1 + 3 * v * u * u * x2 + u * u * u; }
static float bez_dx(float u, float x1, float x2) { float v = 1 - u; return 3 * v * v * x1 + 6 * v * u * (x2 - x1) + 3 * u * u * (1 - x2); }
static float timing(tfunc* f, float t) {
    if (t <= 0) return f->k == 2 ? 1.0f / (float)f->steps : 0;
    if (t >= 1) return 1;
    if (f->k == 1) return floorf(t * (float)f->steps) / (float)f->steps;
    if (f->k == 2) { float v = ceilf(t * (float)f->steps) / (float)f->steps; return v > 1 ? 1 : v; }
    float x1 = f->p[0], y1 = f->p[1], x2 = f->p[2], y2 = f->p[3];
    if (x1 == y1 && x2 == y2) return t;
    float u = t;
    for (int i = 0; i < 8; i++) { float x = bez_x(u, x1, x2) - t; float d = bez_dx(u, x1, x2); if (fabsf(x) < 1e-4f) break; if (fabsf(d) < 1e-6f) break; u -= x / d; if (u < 0) u = 0; if (u > 1) u = 1; }
    float v = 1 - u;
    return 3 * v * v * u * y1 + 3 * v * u * u * y2 + u * u * u;
}

static void ev_push(node* n, const char* t) { if (nev < EV_MAX) { EV[nev].n = n; EV[nev].t = t; nev++; } }

static int add_prop(short* list, int* n, int cap, short p) { for (int i = 0; i < *n; i++) if (list[i] == p) return 0; if (*n < cap) list[(*n)++] = p; return 1; }

static void snap_node(node* n) {
    style* s = n->st;
    if (!s || s->ntrans == 0) return;
    short props[64];
    int np = 0;
    for (int i = 0; i < s->ntrans; i++) {
        transspec* t = &s->trans[i];
        if (t->dur <= 0 && t->delay <= 0) continue;
        if (t->prop == -1) { for (unsigned k = 0; k < sizeof(ALLP) / sizeof(ALLP[0]); k++) add_prop(props, &np, 64, ALLP[k]); }
        else if (t->prop >= 0) { short e[4]; int ne = expand(t->prop, e); for (int k = 0; k < ne; k++) add_prop(props, &np, 64, e[k]); }
    }
    for (int i = 0; i < np && nsnap < 512; i++) { SNAP[nsnap].n = n; SNAP[nsnap].prop = props[i]; get_val(s, props[i], &SNAP[nsnap].v); nsnap++; }
}
static void walk(node* n, void (*fn)(node*)) {
    node* stack[4096];
    int sp = 0;
    stack[sp++] = n;
    while (sp > 0) {
        node* x = stack[--sp];
        if (x->type == NT_ELEM && x->st) fn(x);
        for (node* c = x->last; c; c = c->prev) if (c->type == NT_ELEM && sp < 4096) stack[sp++] = c;
    }
}

void anim_before_style(node* root) {
    nsnap = 0;
    if (!root) return;
    walk(root, snap_node);
}

static transspec* trans_for(style* s, int prop) {
    if (!s) return 0;
    transspec* best = 0;
    for (int i = 0; i < s->ntrans; i++) {
        transspec* t = &s->trans[i];
        if (t->prop == -1) best = t;
        else if (t->prop >= 0) { short e[4]; int ne = expand(t->prop, e); for (int k = 0; k < ne; k++) if (e[k] == prop) best = t; }
    }
    return best && best->dur > 0 ? best : 0;
}

static void link_anims(node* n) {
    n->agen = (unsigned)g_gen;
    style* s = n->st;
    if (!s || !s->nanim) return;
    for (int i = 0; i < s->nanim; i++) {
        animspec* a = &s->anims[i];
        if (!a->name || a->dur <= 0) continue;
        kfset* k = kf_find(a->name);
        if (!k || k->n == 0) continue;
        arun* r = 0;
        for (int j = 0; j < AR_MAX; j++) if (AR[j].n == n && !strcmp(AR[j].name, a->name)) { r = &AR[j]; break; }
        if (!r) {
            for (int j = 0; j < AR_MAX; j++) if (!AR[j].n) { r = &AR[j]; break; }
            if (!r) continue;
            memset(r, 0, sizeof *r);
            r->n = n;
            snprintf(r->name, sizeof r->name, "%s", a->name);
            r->start = now_ms();
        }
        if (r->paused && !a->paused) { r->start += now_ms() - r->pause_at; r->paused = 0; }
        if (!r->paused && a->paused) { r->paused = 1; r->pause_at = now_ms(); }
        r->gen = g_gen;
        r->spec = *a;
        r->kf = k;
        style* base = nalloc(sizeof(style));
        *base = *s;
        base->tfcap = 0;
        r->base = base;
        r->nfs = k->n;
        r->fs = nalloc(sizeof(style*) * (size_t)k->n);
        r->nprops = 0;
        style* par = n->parent && n->parent->st ? n->parent->st : 0;
        for (int f = 0; f < k->n; f++) {
            r->fs[f] = css_style_with(n, base, par, k->f[f].d, k->f[f].nd);
            for (int d = 0; d < k->f[f].nd; d++) {
                short e[4];
                int ne = expand(k->f[f].d[d].prop, e);
                for (int q = 0; q < ne; q++) add_prop(r->props, &r->nprops, 48, e[q]);
            }
        }
    }
}

static void start_transitions(void) {
    double now = now_ms();
    for (int i = 0; i < nsnap; i++) {
        node* n = SNAP[i].n;
        style* s = n->st;
        if (!s || s->display == D_NONE) continue;
        int p = SNAP[i].prop;
        transspec* t = trans_for(s, p);
        if (!t) continue;
        aval nv;
        get_val(s, p, &nv);
        trun* r = 0;
        for (int j = 0; j < TR_MAX; j++) if (TR[j].used && TR[j].n == n && TR[j].prop == p) { r = &TR[j]; break; }
        if (r && same_val(&r->to, &nv)) continue;
        if (same_val(&SNAP[i].v, &nv)) { if (r) r->used = 0; continue; }
        if (!r) for (int j = 0; j < TR_MAX; j++) if (!TR[j].used) { r = &TR[j]; break; }
        if (!r) continue;
        r->used = 1;
        r->n = n;
        r->prop = (short)p;
        r->from = SNAP[i].v;
        r->to = nv;
        r->start = now;
        r->dur = t->dur * 1000;
        r->delay = t->delay * 1000;
        r->tf = t->tf;
        ev_push(n, "transitionrun");
        if (r->delay <= 0) ev_push(n, "transitionstart");
    }
    nsnap = 0;
}

static int apply_runs(double now) {
    int flags = 0;
    for (int i = 0; i < AR_MAX; i++) {
        arun* r = &AR[i];
        if (!r->n) continue;
        style* s = r->n->st;
        if (!s || !r->kf) continue;
        animspec* a = &r->spec;
        double el = (r->paused ? r->pause_at : now) - r->start - a->delay * 1000.0;
        double dur = a->dur * 1000.0;
        float iter = a->iter;
        int active = 1;
        double prog;
        int cur_iter;
        if (el < 0) {
            if (!(a->fill & 2)) { active = 0; }
            prog = 0;
            cur_iter = 0;
        } else {
            if (!r->started) { r->started = 1; ev_push(r->n, "animationstart"); }
            double total = iter < 0 ? 1e300 : dur * iter;
            if (el >= total) {
                if (!r->ended) { r->ended = 1; ev_push(r->n, "animationend"); }
                if (!(a->fill & 1)) active = 0;
                double it = iter;
                cur_iter = (int)floor(it);
                prog = it - floor(it);
                if (prog == 0 && it > 0) { prog = 1; cur_iter--; }
            } else {
                cur_iter = (int)(el / dur);
                prog = (el - cur_iter * dur) / dur;
                if (cur_iter > r->iters_done) { r->iters_done = cur_iter; ev_push(r->n, "animationiteration"); }
                flags |= 1;
            }
        }
        if (!active) continue;
        int rev = a->dir == 1 || (a->dir == 2 && (cur_iter & 1)) || (a->dir == 3 && !(cur_iter & 1));
        float t = (float)(rev ? 1 - prog : prog);
        kfset* k = r->kf;
        for (int pi = 0; pi < r->nprops; pi++) {
            int p = r->props[pi];
            int lo = -1, hi = -1;
            for (int f = 0; f < k->n; f++) {
                int has = 0;
                for (int d = 0; d < k->f[f].nd && !has; d++) { short e[4]; int ne = expand(k->f[f].d[d].prop, e); for (int q = 0; q < ne; q++) if (e[q] == p) has = 1; }
                if (!has) continue;
                if (k->f[f].off <= t) lo = f;
                if (k->f[f].off >= t && hi < 0) hi = f;
            }
            aval va, vb, vr;
            float o0 = 0, o1 = 1;
            style* sa = r->base;
            style* sb = r->base;
            if (lo >= 0) { sa = r->fs[lo]; o0 = k->f[lo].off; }
            if (hi >= 0) { sb = r->fs[hi]; o1 = k->f[hi].off; }
            get_val(sa, p, &va);
            get_val(sb, p, &vb);
            float lt = o1 > o0 ? (t - o0) / (o1 - o0) : 0;
            lt = timing(&a->tf, lt);
            lerp_val(&va, &vb, lt, &vr, r->n);
            put_val(s, p, &vr);
            if (is_layout_prop(p) && (flags & 1)) flags |= 2;
        }
    }
    for (int i = 0; i < TR_MAX; i++) {
        trun* r = &TR[i];
        if (!r->used) continue;
        style* s = r->n->st;
        if (!s) { r->used = 0; continue; }
        double el = now - r->start - r->delay;
        float t = el <= 0 ? 0 : (r->dur > 0 ? (float)(el / r->dur) : 1);
        if (t >= 1) {
            put_val(s, r->prop, &r->to);
            r->used = 0;
            ev_push(r->n, "transitionend");
            flags |= 1;
            if (is_layout_prop(r->prop)) flags |= 2;
            continue;
        }
        aval v;
        lerp_val(&r->from, &r->to, timing(&r->tf, t), &v, r->n);
        put_val(s, r->prop, &v);
        flags |= 1;
        if (is_layout_prop(r->prop)) flags |= 2;
    }
    return flags;
}

void anim_after_style(node* root) {
    g_gen++;
    if (root) walk(root, link_anims);
    for (int j = 0; j < AR_MAX; j++) if (AR[j].n && AR[j].gen != g_gen) memset(&AR[j], 0, sizeof AR[j]);
    for (int j = 0; j < TR_MAX; j++) if (TR[j].used && TR[j].n->agen != (unsigned)g_gen) TR[j].used = 0;
    { int k = 0; for (int i = 0; i < nsnap; i++) if (SNAP[i].n->agen == (unsigned)g_gen) SNAP[k++] = SNAP[i]; nsnap = k; }
    start_transitions();
    g_anim_active = apply_runs(now_ms());
}

int anim_tick(void) {
    int any = 0;
    for (int j = 0; j < AR_MAX && !any; j++) if (AR[j].n) any = 1;
    for (int j = 0; j < TR_MAX && !any; j++) if (TR[j].used) any = 1;
    if (!any) { g_anim_active = 0; return 0; }
    g_anim_active = apply_runs(now_ms());
    return g_anim_active;
}

int anim_events(node** ns, const char** ts, int cap) {
    int n = nev < cap ? nev : cap;
    for (int i = 0; i < n; i++) { ns[i] = EV[i].n; ts[i] = EV[i].t; }
    nev = 0;
    return n;
}

void anim_reset(void) {
    memset(AR, 0, sizeof AR);
    memset(TR, 0, sizeof TR);
    nev = 0;
    nsnap = 0;
    g_kf = 0;
    g_anim_active = 0;
}

void anim_forget_node(node* n) {
    for (int j = 0; j < AR_MAX; j++) if (AR[j].n == n) memset(&AR[j], 0, sizeof AR[j]);
    for (int j = 0; j < TR_MAX; j++) if (TR[j].n == n) TR[j].used = 0;
}
