#include "web/nwe.h"
#include "third_party/quickjs/quickjs.h"

#define GT_MAX 160
#define GL_MAXF 16
enum { K_VOID, K_SCALAR, K_VEC, K_MAT, K_SAMPLER, K_ARRAY, K_STRUCT };
typedef struct { int kind, base, n, size, elem; int nf; int fty[GL_MAXF]; int foff[GL_MAXF]; char fname[GL_MAXF][32]; char name[32]; } gtype;
enum { TY_VOID, TY_BOOL, TY_INT, TY_FLOAT, TY_VEC2, TY_VEC3, TY_VEC4, TY_BVEC2, TY_BVEC3, TY_BVEC4, TY_IVEC2, TY_IVEC3, TY_IVEC4, TY_MAT2, TY_MAT3, TY_MAT4, TY_S2D, TY_SCUBE, TY_NBASE };

enum { I_MOV, I_MOVS, I_ADD, I_SUB, I_MUL, I_DIV, I_MOD, I_NEG, I_MVM, I_VMM, I_MMM, I_LT, I_LE, I_GT, I_GE, I_EQ, I_NE, I_AND, I_OR, I_XOR, I_NOT, I_SWZ, I_SWZW, I_IDXR, I_IDXW, I_JMP, I_JZ, I_JNZ, I_CALL, I_RET, I_BI, I_TEX, I_DISC, I_END, I_TOINT, I_TOBOOL };
typedef struct { unsigned char op, n; short aux; int d, a, b, c; } ginst;

enum { B_RADIANS, B_DEGREES, B_SIN, B_COS, B_TAN, B_ASIN, B_ACOS, B_ATAN, B_ATAN2, B_POW, B_EXP, B_LOG, B_EXP2, B_LOG2, B_SQRT, B_ISQRT, B_ABS, B_SIGN, B_FLOOR, B_CEIL, B_FRACT, B_MOD, B_MIN, B_MAX, B_CLAMP, B_MIX, B_STEP, B_SSTEP, B_LENGTH, B_DIST, B_DOT, B_CROSS, B_NORM, B_FFWD, B_REFL, B_REFR, B_MCM, B_LT, B_LE, B_GT, B_GE, B_EQ, B_NE, B_ANY, B_ALL, B_NOTV, B_TEX, B_TEXP, B_TEXCUBE, B_DERIV, B_ROUND, B_TRUNC, B_TEXF, B_TEXSZ, B_N };

typedef struct gsym { char name[64]; int ty, slot, isconst, cval_ok, storage; float cval; struct gsym* next; int depth; } gsym;
enum { ST_LOCAL, ST_UNIFORM, ST_ATTR, ST_VARY, ST_BUILTIN };

typedef struct gfunc { char name[64]; int ret, np, pty[16], pq[16], pslot[16], rslot, entry, defined, nfix, fix[256]; struct gfunc* next; void* body; void* params; } gfunc;

typedef struct { char name[96]; int ty, size, slot, storage, leaf; } gvar;

typedef struct gsh {
    int stage, ok;
    char log[512];
    ginst* code; int ncode, ccode;
    int nmem, msize;
    float* mem;
    gvar vars[256]; int nvars;
    int main_entry;
    int pos_slot, psize_slot, fragcoord_slot, frontface_slot, pointcoord_slot, fragcolor_slot;
} gsh;

static gtype GT[GT_MAX];
static int ngt;

static int gt_new(int kind, int base, int n, int size) { if (ngt >= GT_MAX) return TY_FLOAT; gtype* t = &GT[ngt]; memset(t, 0, sizeof(*t)); t->kind = kind; t->base = base; t->n = n; t->size = size; return ngt++; }
static void gt_init(void) {
    ngt = 0;
    gt_new(K_VOID, 0, 0, 0);
    gt_new(K_SCALAR, 'b', 1, 1); gt_new(K_SCALAR, 'i', 1, 1); gt_new(K_SCALAR, 'f', 1, 1);
    for (int i = 2; i <= 4; i++) gt_new(K_VEC, 'f', i, i);
    for (int i = 2; i <= 4; i++) gt_new(K_VEC, 'b', i, i);
    for (int i = 2; i <= 4; i++) gt_new(K_VEC, 'i', i, i);
    for (int i = 2; i <= 4; i++) gt_new(K_MAT, 'f', i, i * i);
    gt_new(K_SAMPLER, 0, 2, 1); gt_new(K_SAMPLER, 0, 3, 1);
    static const char* NM[] = { "void", "bool", "int", "float", "vec2", "vec3", "vec4", "bvec2", "bvec3", "bvec4", "ivec2", "ivec3", "ivec4", "mat2", "mat3", "mat4", "sampler2D", "samplerCube" };
    for (int i = 0; i < TY_NBASE; i++) snprintf(GT[i].name, sizeof(GT[i].name), "%s", NM[i]);
}
static int ty_vec(int base, int n) { if (n == 1) return base == 'b' ? TY_BOOL : base == 'i' ? TY_INT : TY_FLOAT; if (base == 'b') return TY_BVEC2 + n - 2; if (base == 'i') return TY_IVEC2 + n - 2; return TY_VEC2 + n - 2; }
static int ty_array(int elem, int n) { for (int i = TY_NBASE; i < ngt; i++) if (GT[i].kind == K_ARRAY && GT[i].elem == elem && GT[i].n == n) return i; int t = gt_new(K_ARRAY, GT[elem].base, n, GT[elem].size * n); GT[t].elem = elem; snprintf(GT[t].name, sizeof(GT[t].name), "%s[%d]", GT[elem].name, n); return t; }
static int is_num(int t) { return GT[t].kind == K_SCALAR || GT[t].kind == K_VEC || GT[t].kind == K_MAT; }
static int comps(int t) { return GT[t].size; }

typedef struct { int k; char s[64]; float f; int isint; int line; } gtok;
enum { TK_EOF, TK_ID, TK_NUM, TK_OP };

typedef struct ast {
    int k, op, ty, line, isint, nkids;
    float num;
    char name[64];
    struct ast* a; struct ast* b; struct ast* c; struct ast* d;
    struct ast* next;
    struct ast* kids[24];
} ast;
enum { A_NUM, A_ID, A_BIN, A_UN, A_ASSIGN, A_CALL, A_FIELD, A_INDEX, A_COND, A_POSTINC, A_PREINC, A_SEQ, A_DECL, A_EXPR, A_BLOCK, A_IF, A_FOR, A_WHILE, A_DO, A_RET, A_BREAK, A_CONT, A_DISCARD, A_FUNC, A_EMPTY, A_STRUCTDEF, A_PRECISION };

typedef struct {
    gtok* t; int nt, p;
    char err[256];
    int fail;
    gsh* sh;
    gsym* syms;
    int depth;
    gfunc* funcs;
    gfunc* curf;
    int tmp, tmpmax;
    int brk[32][64], nbrk[32], cont[32][64], ncont[32], loopd;
    char cn[96][64]; int cv[96]; int ncn;
    int ctop;
    float cvals[512]; int cslots[512]; int ncst;
    ast* ginit[256]; int nginit;
    unsigned char* named;
} gctx;

static void cerr(gctx* c, int line, const char* m, const char* x) { if (c->fail) return; c->fail = 1; snprintf(c->err, sizeof(c->err), "ERROR: 0:%d: %s%s%s", line, x ? "'" : "", x ? x : "", x ? "' : " : ""); size_t l = strlen(c->err); snprintf(c->err + l, sizeof(c->err) - l, "%s", m); }

typedef struct { char name[64]; int nargs; char args[8][32]; char* body; } gmac;
typedef struct { gmac m[256]; int n; } gmacs;
static gmac* mac_find(gmacs* M, const char* s, int l) { for (int i = 0; i < M->n; i++) if ((int)strlen(M->m[i].name) == l && !memcmp(M->m[i].name, s, (size_t)l)) return &M->m[i]; return 0; }
static int idc(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
static void sb_cat(sbuf* b, const char* s, int n) { sb_putn(b, s, n); }
static void expand(gmacs* M, const char* s, int n, sbuf* out, int depth);
static void expand_call(gmacs* M, gmac* m, const char* s, int n, int* pos, sbuf* out, int depth) {
    int i = *pos; while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
    if (m->nargs < 0 || i >= n || s[i] != '(') { if (m->nargs >= 0) { sb_cat(out, m->name, (int)strlen(m->name)); return; } expand(M, m->body, (int)strlen(m->body), out, depth + 1); return; }
    i++;
    const char* as[8]; int al[8]; int na = 0, d = 0, st = i;
    for (; i < n; i++) { if (s[i] == '(') d++; else if (s[i] == ')') { if (!d) break; d--; } else if (s[i] == ',' && !d) { if (na < 8) { as[na] = s + st; al[na] = i - st; na++; } st = i + 1; } }
    if (na < 8 && (i > st || m->nargs > 0)) { as[na] = s + st; al[na] = i - st; na++; }
    *pos = i + 1;
    sbuf tmp = { 0, 0, 0 };
    const char* bd = m->body; int bl = (int)strlen(bd);
    for (int k = 0; k < bl;) {
        if (idc((unsigned char)bd[k]) && (k == 0 || !idc((unsigned char)bd[k - 1]))) {
            int e = k; while (e < bl && idc((unsigned char)bd[e])) e++;
            int hit = -1; for (int q = 0; q < m->nargs; q++) if ((int)strlen(m->args[q]) == e - k && !memcmp(m->args[q], bd + k, (size_t)(e - k))) hit = q;
            if (hit >= 0 && hit < na) { sb_cat(&tmp, " ", 1); sb_cat(&tmp, as[hit], al[hit]); sb_cat(&tmp, " ", 1); } else sb_cat(&tmp, bd + k, e - k);
            k = e;
        } else { sb_cat(&tmp, bd + k, 1); k++; }
    }
    expand(M, tmp.s ? tmp.s : "", tmp.n, out, depth + 1);
}
static void expand(gmacs* M, const char* s, int n, sbuf* out, int depth) {
    for (int i = 0; i < n;) {
        if (idc((unsigned char)s[i]) && !(s[i] >= '0' && s[i] <= '9')) {
            int e = i; while (e < n && idc((unsigned char)s[e])) e++;
            gmac* m = depth < 24 ? mac_find(M, s + i, e - i) : 0;
            if (m) { int p = e; expand_call(M, m, s, n, &p, out, depth); i = p; continue; }
            sb_cat(out, s + i, e - i); i = e; continue;
        }
        if (s[i] >= '0' && s[i] <= '9') { int e = i; while (e < n && (idc((unsigned char)s[e]) || s[e] == '.' || ((s[e] == '+' || s[e] == '-') && (s[e - 1] == 'e' || s[e - 1] == 'E')))) e++; sb_cat(out, s + i, e - i); i = e; continue; }
        sb_cat(out, s + i, 1); i++;
    }
}
typedef struct { const char* s; int p, n; gmacs* M; } pexp;
static void pe_ws(pexp* e) { while (e->p < e->n && (e->s[e->p] == ' ' || e->s[e->p] == '\t')) e->p++; }
static long pe_or(pexp* e);
static long pe_prim(pexp* e) {
    pe_ws(e);
    if (e->p >= e->n) return 0;
    char c = e->s[e->p];
    if (c == '(') { e->p++; long v = pe_or(e); pe_ws(e); if (e->p < e->n && e->s[e->p] == ')') e->p++; return v; }
    if (c == '!') { e->p++; return !pe_prim(e); }
    if (c == '-') { e->p++; return -pe_prim(e); }
    if (c == '+') { e->p++; return pe_prim(e); }
    if (c >= '0' && c <= '9') { long v = 0; while (e->p < e->n && e->s[e->p] >= '0' && e->s[e->p] <= '9') v = v * 10 + (e->s[e->p++] - '0'); while (e->p < e->n && idc((unsigned char)e->s[e->p])) e->p++; return v; }
    if (idc((unsigned char)c)) {
        int st = e->p; while (e->p < e->n && idc((unsigned char)e->s[e->p])) e->p++;
        int l = e->p - st;
        if (l == 7 && !memcmp(e->s + st, "defined", 7)) {
            pe_ws(e); int par = 0; if (e->p < e->n && e->s[e->p] == '(') { par = 1; e->p++; pe_ws(e); }
            int s2 = e->p; while (e->p < e->n && idc((unsigned char)e->s[e->p])) e->p++;
            long v = mac_find(e->M, e->s + s2, e->p - s2) != 0;
            if (par) { pe_ws(e); if (e->p < e->n && e->s[e->p] == ')') e->p++; }
            return v;
        }
        gmac* m = mac_find(e->M, e->s + st, l);
        if (m && m->nargs < 0) { sbuf b = { 0, 0, 0 }; expand(e->M, m->body, (int)strlen(m->body), &b, 1); pexp e2 = { b.s ? b.s : "", 0, b.n, e->M }; long rv = pe_or(&e2); nfree(b.s); return rv; }
        return 0;
    }
    e->p++;
    return 0;
}
static long pe_mul(pexp* e) { long v = pe_prim(e); for (;;) { pe_ws(e); if (e->p >= e->n) return v; char c = e->s[e->p]; if (c == '*') { e->p++; v *= pe_prim(e); } else if (c == '/') { e->p++; long r = pe_prim(e); v = r ? v / r : 0; } else if (c == '%') { e->p++; long r = pe_prim(e); v = r ? v % r : 0; } else return v; } }
static long pe_add(pexp* e) { long v = pe_mul(e); for (;;) { pe_ws(e); if (e->p >= e->n) return v; char c = e->s[e->p]; if (c == '+') { e->p++; v += pe_mul(e); } else if (c == '-') { e->p++; v -= pe_mul(e); } else return v; } }
static long pe_rel(pexp* e) { long v = pe_add(e); for (;;) { pe_ws(e); if (e->p + 1 >= e->n + 1) return v; const char* q = e->s + e->p; if (e->p + 1 < e->n && q[0] == '<' && q[1] == '=') { e->p += 2; v = v <= pe_add(e); } else if (e->p + 1 < e->n && q[0] == '>' && q[1] == '=') { e->p += 2; v = v >= pe_add(e); } else if (q[0] == '<' && e->p < e->n) { e->p++; v = v < pe_add(e); } else if (q[0] == '>' && e->p < e->n) { e->p++; v = v > pe_add(e); } else return v; } }
static long pe_eq(pexp* e) { long v = pe_rel(e); for (;;) { pe_ws(e); if (e->p + 1 >= e->n) return v; const char* q = e->s + e->p; if (q[0] == '=' && q[1] == '=') { e->p += 2; v = v == pe_rel(e); } else if (q[0] == '!' && q[1] == '=') { e->p += 2; v = v != pe_rel(e); } else return v; } }
static long pe_and(pexp* e) { long v = pe_eq(e); for (;;) { pe_ws(e); if (e->p + 1 >= e->n) return v; if (e->s[e->p] == '&' && e->s[e->p + 1] == '&') { e->p += 2; long r = pe_eq(e); v = v && r; } else return v; } }
static long pe_or(pexp* e) { long v = pe_and(e); for (;;) { pe_ws(e); if (e->p + 1 >= e->n) return v; if (e->s[e->p] == '|' && e->s[e->p + 1] == '|') { e->p += 2; long r = pe_and(e); v = v || r; } else return v; } }

static int preprocess_in(const char* src, sbuf* out, char* err, int ecap, gmacs* M, sbuf* jb);
static int preprocess(const char* src, sbuf* out, char* err, int ecap) {
    gmacs* M = nzalloc(sizeof(gmacs));
    sbuf jb = { 0, 0, 0 };
    int r = preprocess_in(src, out, err, ecap, M, &jb);
    for (int i = 0; i < M->n && i < 256; i++) nfree(M->m[i].body);
    nfree(M); nfree(jb.s);
    return r;
}
static int preprocess_in(const char* src, sbuf* out, char* err, int ecap, gmacs* M, sbuf* jb) {
    const char* pre[][2] = { { "GL_ES", "1" }, { "__VERSION__", "100" }, { "GL_FRAGMENT_PRECISION_HIGH", "1" } };
    for (int i = 0; i < 3; i++) { gmac* m = &M->m[M->n++]; snprintf(m->name, sizeof(m->name), "%s", pre[i][0]); m->nargs = -1; m->body = nstrdup(pre[i][1]); }
    int stk[64], sp = 0, active = 1, taken[64], line = 1;
    int n = (int)strlen(src);
    sbuf joined = { 0, 0, 0 };
#define joined (*jb)
    for (int i = 0; i < n; i++) { if (src[i] == '\\' && i + 1 < n && src[i + 1] == '\n') { i++; sb_putc(&joined, ' '); continue; } if (src[i] == '/' && i + 1 < n && src[i + 1] == '/') { while (i < n && src[i] != '\n') i++; if (i < n) sb_putc(&joined, '\n'); continue; } if (src[i] == '/' && i + 1 < n && src[i + 1] == '*') { i += 2; while (i + 1 < n && !(src[i] == '*' && src[i + 1] == '/')) { if (src[i] == '\n') sb_putc(&joined, '\n'); i++; } i++; sb_putc(&joined, ' '); continue; } sb_putc(&joined, src[i]); }
    const char* s = joined.s ? joined.s : ""; n = joined.n;
    for (int i = 0; i < n;) {
        int e = i; while (e < n && s[e] != '\n') e++;
        const char* L = s + i; int ll = e - i;
        int k = 0; while (k < ll && (L[k] == ' ' || L[k] == '\t' || L[k] == '\r')) k++;
        if (k < ll && L[k] == '#') {
            k++; while (k < ll && (L[k] == ' ' || L[k] == '\t')) k++;
            int ds = k; while (k < ll && idc((unsigned char)L[k])) k++;
            int dl = k - ds; const char* D = L + ds;
            while (k < ll && (L[k] == ' ' || L[k] == '\t')) k++;
            const char* R = L + k; int rl = ll - k; while (rl > 0 && (R[rl - 1] == ' ' || R[rl - 1] == '\r' || R[rl - 1] == '\t')) rl--;
            #define DIS(x) (dl == (int)strlen(x) && !memcmp(D, x, (size_t)dl))
            if (DIS("ifdef") || DIS("ifndef") || DIS("if")) {
                int v = 0;
                if (active) {
                    if (DIS("if")) { pexp pe = { R, 0, rl, M }; v = pe_or(&pe) != 0; }
                    else { int il = 0; while (il < rl && idc((unsigned char)R[il])) il++; v = mac_find(M, R, il) != 0; if (DIS("ifndef")) v = !v; }
                }
                if (sp < 64) { stk[sp] = active; taken[sp] = v; sp++; }
                active = active && v;
            } else if (DIS("elif")) {
                if (sp > 0) { int pa = stk[sp - 1]; if (taken[sp - 1] || !pa) active = 0; else { pexp pe = { R, 0, rl, M }; int v = pe_or(&pe) != 0; active = v; if (v) taken[sp - 1] = 1; } }
            } else if (DIS("else")) {
                if (sp > 0) { int pa = stk[sp - 1]; active = pa && !taken[sp - 1]; taken[sp - 1] = 1; }
            } else if (DIS("endif")) {
                if (sp > 0) { sp--; active = stk[sp]; }
            } else if (active && DIS("define")) {
                int ns = 0; while (ns < rl && idc((unsigned char)R[ns])) ns++;
                gmac* m = mac_find(M, R, ns);
                if (!m) { if (M->n >= 256) { snprintf(err, (size_t)ecap, "ERROR: too many macros"); return 0; } m = &M->m[M->n++]; }
                int nl = ns < 63 ? ns : 63; memcpy(m->name, R, (size_t)nl); m->name[nl] = 0;
                int q = ns;
                if (q < rl && R[q] == '(') {
                    m->nargs = 0; q++;
                    while (q < rl && R[q] != ')') { while (q < rl && (R[q] == ' ' || R[q] == ',')) q++; int as = q; while (q < rl && idc((unsigned char)R[q])) q++; if (q > as && m->nargs < 8) { int al = q - as < 31 ? q - as : 31; memcpy(m->args[m->nargs], R + as, (size_t)al); m->args[m->nargs][al] = 0; m->nargs++; } while (q < rl && R[q] == ' ') q++; if (q < rl && R[q] != ',' && R[q] != ')') q++; }
                    if (q < rl) q++;
                } else m->nargs = -1;
                while (q < rl && (R[q] == ' ' || R[q] == '\t')) q++;
                char* b = nalloc((size_t)(rl - q) + 1); memcpy(b, R + q, (size_t)(rl - q)); b[rl - q] = 0; m->body = b;
            } else if (active && DIS("undef")) {
                int ns = 0; while (ns < rl && idc((unsigned char)R[ns])) ns++;
                gmac* m = mac_find(M, R, ns); if (m) m->name[0] = 0;
            } else if (active && DIS("error")) { snprintf(err, (size_t)ecap, "ERROR: 0:%d: '#error' : %.*s", line, rl, R); return 0; }
            #undef DIS
            sb_putc(out, '\n');
        } else {
            if (active) expand(M, L, ll, out, 0);
            sb_putc(out, '\n');
        }
        line++;
        i = e + 1;
    }
    return 1;
#undef joined
}

static int lex(const char* s, gtok** out, int* nout) {
    int cap = 1024, n = 0; gtok* t = nalloc(sizeof(gtok) * (size_t)cap);
    int line = 1;
    for (int i = 0; s[i];) {
        char c = s[i];
        if (c == '\n') { line++; i++; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }
        if (n + 2 >= cap) { gtok* nt = nalloc(sizeof(gtok) * (size_t)cap * 2); memcpy(nt, t, sizeof(gtok) * (size_t)n); nfree(t); t = nt; cap *= 2; }
        gtok* k = &t[n]; memset(k, 0, sizeof(*k)); k->line = line;
        if (idc((unsigned char)c) && !(c >= '0' && c <= '9')) { int e = i; while (idc((unsigned char)s[e])) e++; int l = e - i < 63 ? e - i : 63; memcpy(k->s, s + i, (size_t)l); k->s[l] = 0; k->k = TK_ID; i = e; n++; continue; }
        if ((c >= '0' && c <= '9') || (c == '.' && s[i + 1] >= '0' && s[i + 1] <= '9')) {
            int e = i, flt = 0;
            if (c == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) { e += 2; long v = 0; while (1) { char h = s[e]; int d = h >= '0' && h <= '9' ? h - '0' : (h | 32) >= 'a' && (h | 32) <= 'f' ? (h | 32) - 'a' + 10 : -1; if (d < 0) break; v = v * 16 + d; e++; } k->f = (float)v; k->isint = 1; }
            else { while ((s[e] >= '0' && s[e] <= '9') || s[e] == '.' || s[e] == 'e' || s[e] == 'E' || ((s[e] == '+' || s[e] == '-') && (s[e - 1] == 'e' || s[e - 1] == 'E'))) { if (s[e] == '.' || s[e] == 'e' || s[e] == 'E') flt = 1; e++; } char b[64]; int l = e - i < 63 ? e - i : 63; memcpy(b, s + i, (size_t)l); b[l] = 0; k->f = (float)strtod(b, 0); k->isint = !flt; }
            if (s[e] == 'f' || s[e] == 'F' || s[e] == 'u' || s[e] == 'U') e++;
            k->k = TK_NUM; i = e; n++; continue;
        }
        static const char* ops[] = { "<<=", ">>=", "++", "--", "<=", ">=", "==", "!=", "&&", "||", "^^", "+=", "-=", "*=", "/=", "%=", "<<", ">>", "&=", "|=", "^=", 0 };
        int matched = 0;
        for (int o = 0; ops[o]; o++) { int l = (int)strlen(ops[o]); if (!strncmp(s + i, ops[o], (size_t)l)) { memcpy(k->s, ops[o], (size_t)l); k->s[l] = 0; i += l; matched = 1; break; } }
        if (!matched) { k->s[0] = c; k->s[1] = 0; i++; }
        k->k = TK_OP; n++;
    }
    t[n].k = TK_EOF; t[n].line = line; t[n].s[0] = 0;
    *out = t; *nout = n;
    return 1;
}

static gtok* PK(gctx* c) { return &c->t[c->p]; }
static gtok* PK2(gctx* c, int o) { int q = c->p + o; return &c->t[q < c->nt ? q : c->nt]; }
static int isop(gctx* c, const char* o) { gtok* t = PK(c); return t->k == TK_OP && !strcmp(t->s, o); }
static int isid(gctx* c, const char* o) { gtok* t = PK(c); return t->k == TK_ID && !strcmp(t->s, o); }
static int eat(gctx* c, const char* o) { if (isop(c, o)) { c->p++; return 1; } return 0; }
static void expect(gctx* c, const char* o) { if (!eat(c, o)) cerr(c, PK(c)->line, "syntax error", PK(c)->s[0] ? PK(c)->s : "end of shader"); }
static ast* mk(gctx* c, int k) { ast* a = nzalloc(sizeof(ast)); a->k = k; a->line = PK(c)->line; return a; }

static int type_by_name(const char* s) {
    static const char* AL[][2] = { { "uint", "int" }, { "uvec2", "ivec2" }, { "uvec3", "ivec3" }, { "uvec4", "ivec4" }, { "sampler3D", "sampler2D" }, { "sampler2DArray", "sampler2D" }, { "sampler2DShadow", "sampler2D" }, { "samplerCubeShadow", "samplerCube" }, { "sampler2DArrayShadow", "sampler2D" }, { "isampler2D", "sampler2D" }, { "usampler2D", "sampler2D" }, { "isampler3D", "sampler2D" }, { "usampler3D", "sampler2D" }, { "isamplerCube", "samplerCube" }, { "usamplerCube", "samplerCube" }, { "isampler2DArray", "sampler2D" }, { "usampler2DArray", "sampler2D" }, { "samplerExternalOES", "sampler2D" }, { "mat2x2", "mat2" }, { "mat3x3", "mat3" }, { "mat4x4", "mat4" }, { 0, 0 } };
    for (int k = 0; AL[k][0]; k++) if (!strcmp(AL[k][0], s)) { s = AL[k][1]; break; }
    for (int i = 0; i < ngt; i++) if ((i < TY_NBASE || GT[i].kind == K_STRUCT) && !strcmp(GT[i].name, s)) return i; return -1; }
static int is_qual(const char* s) { return !strcmp(s, "const") || !strcmp(s, "uniform") || !strcmp(s, "attribute") || !strcmp(s, "varying") || !strcmp(s, "invariant") || !strcmp(s, "highp") || !strcmp(s, "mediump") || !strcmp(s, "lowp") || !strcmp(s, "in") || !strcmp(s, "out") || !strcmp(s, "inout") || !strcmp(s, "centroid") || !strcmp(s, "flat") || !strcmp(s, "smooth"); }

static ast* p_expr(gctx* c);
static ast* p_assign(gctx* c);
static ast* p_stmt(gctx* c);
static int p_const_int(gctx* c, ast* e, int* out);

static int p_const_int(gctx* c, ast* e, int* out) {
    if (!e) return 0;
    if (e->k == A_NUM) { *out = (int)e->num; return 1; }
    if (e->k == A_ID) { for (int i = c->ncn - 1; i >= 0; i--) if (!strcmp(c->cn[i], e->name)) { *out = c->cv[i]; return 1; } return 0; }
    if (e->k == A_UN && e->op == '-') { int v; if (!p_const_int(c, e->a, &v)) return 0; *out = -v; return 1; }
    if (e->k == A_UN && e->op == '+') return p_const_int(c, e->a, out);
    if (e->k == A_BIN) { int x, y; if (!p_const_int(c, e->a, &x) || !p_const_int(c, e->b, &y)) return 0; switch (e->op) { case '+': *out = x + y; return 1; case '-': *out = x - y; return 1; case '*': *out = x * y; return 1; case '/': *out = y ? x / y : 0; return 1; case '%': *out = y ? x % y : 0; return 1; } return 0; }
    if (e->k == A_CALL && (!strcmp(e->name, "int") || !strcmp(e->name, "float")) && e->nkids == 1) return p_const_int(c, e->kids[0], out);
    return 0;
}
static ast* p_primary(gctx* c) {
    gtok* t = PK(c);
    if (t->k == TK_NUM) { ast* a = mk(c, A_NUM); a->num = t->f; a->isint = t->isint; a->ty = t->isint ? TY_INT : TY_FLOAT; c->p++; return a; }
    if (t->k == TK_ID) {
        if (!strcmp(t->s, "true") || !strcmp(t->s, "false")) { ast* a = mk(c, A_NUM); a->num = t->s[0] == 't'; a->ty = TY_BOOL; c->p++; return a; }
        ast* a = mk(c, A_ID); snprintf(a->name, sizeof(a->name), "%s", t->s); c->p++;
        if (isop(c, "[") && type_by_name(a->name) >= 0) { c->p++; ast* sz = isop(c, "]") ? 0 : p_expr(c); expect(c, "]"); int n = 0; if (sz) p_const_int(c, sz, &n); a->isint = n > 0 ? n : -1; }
        if (isop(c, "(")) {
            c->p++; ast* call = mk(c, A_CALL); snprintf(call->name, sizeof(call->name), "%s", a->name); call->isint = a->isint;
            if (isid(c, "void") && PK2(c, 1)->k == TK_OP && !strcmp(PK2(c, 1)->s, ")")) c->p++;
            if (!isop(c, ")")) { do { if (call->nkids < 24) call->kids[call->nkids++] = p_assign(c); else p_assign(c); } while (eat(c, ",")); }
            expect(c, ")");
            return call;
        }
        return a;
    }
    if (eat(c, "(")) { ast* e = p_expr(c); expect(c, ")"); return e; }
    cerr(c, t->line, "syntax error", t->s[0] ? t->s : "end of shader");
    c->p++;
    return mk(c, A_NUM);
}
static ast* p_postfix(gctx* c) {
    ast* e = p_primary(c);
    for (;;) {
        if (c->fail) return e;
        if (eat(c, "[")) { ast* x = mk(c, A_INDEX); x->a = e; x->b = p_expr(c); expect(c, "]"); e = x; continue; }
        if (eat(c, ".")) { ast* x = mk(c, A_FIELD); x->a = e; if (PK(c)->k != TK_ID) { cerr(c, PK(c)->line, "field name expected", 0); return e; } snprintf(x->name, sizeof(x->name), "%s", PK(c)->s); c->p++; if (isop(c, "(")) { c->p++; expect(c, ")"); ast* call = mk(c, A_CALL); snprintf(call->name, sizeof(call->name), "%s", x->name); call->kids[0] = e; call->nkids = 1; e = call; continue; } e = x; continue; }
        if (isop(c, "++") || isop(c, "--")) { ast* x = mk(c, A_POSTINC); x->op = PK(c)->s[0]; c->p++; x->a = e; e = x; continue; }
        return e;
    }
}
static ast* p_unary(gctx* c) {
    if (isop(c, "-") || isop(c, "!") || isop(c, "+") || isop(c, "~")) { ast* x = mk(c, A_UN); x->op = PK(c)->s[0]; c->p++; x->a = p_unary(c); return x; }
    if (isop(c, "++") || isop(c, "--")) { ast* x = mk(c, A_PREINC); x->op = PK(c)->s[0]; c->p++; x->a = p_unary(c); return x; }
    return p_postfix(c);
}
static int binprec(const char* s) {
    static const struct { const char* o; int p; } T[] = { { "||", 1 }, { "^^", 2 }, { "&&", 3 }, { "|", 4 }, { "^", 5 }, { "&", 6 }, { "==", 7 }, { "!=", 7 }, { "<", 8 }, { ">", 8 }, { "<=", 8 }, { ">=", 8 }, { "<<", 9 }, { ">>", 9 }, { "+", 10 }, { "-", 10 }, { "*", 11 }, { "/", 11 }, { "%", 11 }, { 0, 0 } };
    for (int i = 0; T[i].o; i++) if (!strcmp(T[i].o, s)) return T[i].p;
    return 0;
}
static int opcode_of(const char* s) { if (!strcmp(s, "||")) return 'O'; if (!strcmp(s, "&&")) return 'A'; if (!strcmp(s, "^^")) return 'X'; if (!strcmp(s, "==")) return 'E'; if (!strcmp(s, "!=")) return 'N'; if (!strcmp(s, "<=")) return 'l'; if (!strcmp(s, ">=")) return 'g'; return s[0]; }
static ast* p_bin(gctx* c, int minp) {
    ast* l = p_unary(c);
    for (;;) {
        if (c->fail) return l;
        gtok* t = PK(c);
        if (t->k != TK_OP) return l;
        int pr = binprec(t->s);
        if (!pr || pr < minp) return l;
        int op = opcode_of(t->s); c->p++;
        ast* r = p_bin(c, pr + 1);
        ast* x = mk(c, A_BIN); x->op = op; x->a = l; x->b = r; l = x;
    }
}
static ast* p_cond(gctx* c) {
    ast* e = p_bin(c, 1);
    if (eat(c, "?")) { ast* x = mk(c, A_COND); x->a = e; x->b = p_assign(c); expect(c, ":"); x->c = p_assign(c); return x; }
    return e;
}
static ast* p_assign(gctx* c) {
    ast* l = p_cond(c);
    gtok* t = PK(c);
    if (t->k == TK_OP && (!strcmp(t->s, "=") || !strcmp(t->s, "+=") || !strcmp(t->s, "-=") || !strcmp(t->s, "*=") || !strcmp(t->s, "/=") || !strcmp(t->s, "%="))) {
        ast* x = mk(c, A_ASSIGN); x->op = t->s[0] == '=' ? '=' : t->s[0]; c->p++; x->a = l; x->b = p_assign(c); return x;
    }
    return l;
}
static ast* p_expr(gctx* c) {
    ast* e = p_assign(c);
    while (isop(c, ",")) { c->p++; ast* x = mk(c, A_SEQ); x->a = e; x->b = p_assign(c); e = x; }
    return e;
}

static int p_type(gctx* c, int* ty);
static ast* p_structdef(gctx* c) {
    c->p++;
    char nm[32] = { 0 };
    if (PK(c)->k == TK_ID) { snprintf(nm, sizeof(nm), "%s", PK(c)->s); c->p++; }
    expect(c, "{");
    int st = gt_new(K_STRUCT, 'f', 0, 0); snprintf(GT[st].name, sizeof(GT[st].name), "%s", nm);
    while (!c->fail && !isop(c, "}") && PK(c)->k != TK_EOF) {
        while (PK(c)->k == TK_ID && is_qual(PK(c)->s)) c->p++;
        int ft; if (!p_type(c, &ft)) { cerr(c, PK(c)->line, "unknown type", PK(c)->s); return 0; }
        do {
            if (PK(c)->k != TK_ID) { cerr(c, PK(c)->line, "field name expected", 0); return 0; }
            gtype* S = &GT[st];
            int t2 = ft;
            char fn[32]; snprintf(fn, sizeof(fn), "%s", PK(c)->s); c->p++;
            if (eat(c, "[")) { ast* sz = p_expr(c); expect(c, "]"); int n = 0; if (!p_const_int(c, sz, &n) || n <= 0) { cerr(c, PK(c)->line, "array size must be a constant", 0); return 0; } t2 = ty_array(ft, n); S = &GT[st]; }
            if (S->nf < GL_MAXF) { snprintf(S->fname[S->nf], 32, "%s", fn); S->fty[S->nf] = t2; S->foff[S->nf] = S->size; S->size += GT[t2].size; S->nf++; }
        } while (eat(c, ","));
        expect(c, ";");
    }
    expect(c, "}");
    ast* d = mk(c, A_STRUCTDEF); d->ty = st;
    return d;
}
static int p_type(gctx* c, int* ty) {
    if (isid(c, "struct")) { ast* d = p_structdef(c); if (!d) return 0; *ty = d->ty; return 1; }
    if (PK(c)->k != TK_ID) return 0;
    int t = type_by_name(PK(c)->s);
    if (t < 0) return 0;
    c->p++;
    *ty = t;
    return 1;
}
static int looks_decl(gctx* c) {
    int q = c->p;
    while (c->t[q].k == TK_ID && is_qual(c->t[q].s)) q++;
    if (c->t[q].k != TK_ID) return 0;
    if (!strcmp(c->t[q].s, "struct")) return 1;
    if (type_by_name(c->t[q].s) < 0) return 0;
    q++;
    if (c->t[q].k == TK_OP && !strcmp(c->t[q].s, "[")) { while (c->t[q].k != TK_EOF && !(c->t[q].k == TK_OP && !strcmp(c->t[q].s, "]"))) q++; q++; }
    return c->t[q].k == TK_ID || (c->t[q].k == TK_OP && !strcmp(c->t[q].s, ";"));
}
static ast* p_decl(gctx* c, int global) {
    ast* d = mk(c, A_DECL);
    int qual = 0;
    while (PK(c)->k == TK_ID && is_qual(PK(c)->s)) { const char* q = PK(c)->s; if (!strcmp(q, "const")) qual |= 1; if (!strcmp(q, "uniform")) qual |= 2; if (!strcmp(q, "attribute")) qual |= 4; if (!strcmp(q, "varying")) qual |= 8; c->p++; }
    int ty; if (!p_type(c, &ty)) { cerr(c, PK(c)->line, "unknown type", PK(c)->s); return d; }
    int arrn = 0;
    if (eat(c, "[")) { ast* sz = p_expr(c); expect(c, "]"); p_const_int(c, sz, &arrn); }
    d->op = qual; d->ty = ty;
    if (isop(c, ";")) { c->p++; d->k = A_EMPTY; return d; }
    if (global && PK(c)->k == TK_ID && PK2(c, 1)->k == TK_OP && !strcmp(PK2(c, 1)->s, "(")) {
        ast* f = mk(c, A_FUNC); f->ty = ty; snprintf(f->name, sizeof(f->name), "%s", PK(c)->s); c->p += 2;
        if (isid(c, "void") && PK2(c, 1)->k == TK_OP && !strcmp(PK2(c, 1)->s, ")")) c->p++;
        while (!c->fail && !isop(c, ")") && PK(c)->k != TK_EOF) {
            ast* pa = mk(c, A_DECL); int pq = 0;
            while (PK(c)->k == TK_ID && is_qual(PK(c)->s)) { if (!strcmp(PK(c)->s, "out")) pq = 2; else if (!strcmp(PK(c)->s, "inout")) pq = 3; c->p++; }
            int pt; if (!p_type(c, &pt)) { cerr(c, PK(c)->line, "unknown type", PK(c)->s); return d; }
            if (PK(c)->k == TK_ID) { snprintf(pa->name, sizeof(pa->name), "%s", PK(c)->s); c->p++; }
            if (eat(c, "[")) { ast* sz = p_expr(c); expect(c, "]"); int n = 0; p_const_int(c, sz, &n); if (n > 0) pt = ty_array(pt, n); }
            pa->ty = pt; pa->op = pq;
            if (f->nkids < 16) f->kids[f->nkids++] = pa;
            if (!eat(c, ",")) break;
        }
        expect(c, ")");
        if (eat(c, ";")) { f->a = 0; return f; }
        f->a = p_stmt(c);
        f->op = 1;
        return f;
    }
    ast* head = 0; ast* tail = 0;
    do {
        if (PK(c)->k != TK_ID) { cerr(c, PK(c)->line, "identifier expected", PK(c)->s); return d; }
        ast* v = mk(c, A_DECL); v->op = qual; snprintf(v->name, sizeof(v->name), "%s", PK(c)->s); c->p++;
        int vt = ty;
        if (arrn > 0) vt = ty_array(ty, arrn);
        if (eat(c, "[")) { if (isop(c, "]")) { c->p++; v->isint = -1; } else { ast* sz = p_expr(c); expect(c, "]"); int n = 0; if (!p_const_int(c, sz, &n) || n <= 0) { cerr(c, v->line, "array size must be a constant integer expression", 0); return d; } vt = ty_array(ty, n); } }
        v->ty = vt;
        if (eat(c, "=")) { v->a = p_assign(c); int cv2; if ((qual & 1) && (ty == TY_INT || ty == TY_FLOAT) && c->ncn < 96 && p_const_int(c, v->a, &cv2)) { snprintf(c->cn[c->ncn], 64, "%s", v->name); c->cv[c->ncn++] = cv2; } }
        if (!head) head = v; else tail->next = v;
        tail = v;
    } while (eat(c, ","));
    expect(c, ";");
    d->a = head;
    return d;
}
static ast* p_block(gctx* c) {
    ast* b = mk(c, A_BLOCK); expect(c, "{");
    ast* tail = 0;
    while (!c->fail && !isop(c, "}") && PK(c)->k != TK_EOF) { ast* s = p_stmt(c); if (!s) continue; if (!b->a) b->a = s; else tail->next = s; tail = s; while (tail->next) tail = tail->next; }
    expect(c, "}");
    return b;
}
static ast* p_stmt(gctx* c) {
    if (c->fail) { c->p = c->nt; return 0; }
    if (isop(c, "{")) return p_block(c);
    if (eat(c, ";")) return mk(c, A_EMPTY);
    if (isid(c, "if")) { c->p++; ast* s = mk(c, A_IF); expect(c, "("); s->a = p_expr(c); expect(c, ")"); s->b = p_stmt(c); if (isid(c, "else")) { c->p++; s->c = p_stmt(c); } return s; }
    if (isid(c, "for")) { c->p++; ast* s = mk(c, A_FOR); expect(c, "("); s->a = looks_decl(c) ? p_decl(c, 0) : (isop(c, ";") ? (c->p++, mk(c, A_EMPTY)) : 0); if (!s->a) { ast* e = mk(c, A_EXPR); e->a = p_expr(c); expect(c, ";"); s->a = e; } if (!isop(c, ";")) s->b = p_expr(c); expect(c, ";"); if (!isop(c, ")")) s->c = p_expr(c); expect(c, ")"); s->d = p_stmt(c); return s; }
    if (isid(c, "while")) { c->p++; ast* s = mk(c, A_WHILE); expect(c, "("); s->a = p_expr(c); expect(c, ")"); s->b = p_stmt(c); return s; }
    if (isid(c, "do")) { c->p++; ast* s = mk(c, A_DO); s->b = p_stmt(c); if (!isid(c, "while")) { cerr(c, PK(c)->line, "'while' expected", 0); return s; } c->p++; expect(c, "("); s->a = p_expr(c); expect(c, ")"); expect(c, ";"); return s; }
    if (isid(c, "return")) { c->p++; ast* s = mk(c, A_RET); if (!isop(c, ";")) s->a = p_expr(c); expect(c, ";"); return s; }
    if (isid(c, "break")) { c->p++; expect(c, ";"); return mk(c, A_BREAK); }
    if (isid(c, "continue")) { c->p++; expect(c, ";"); return mk(c, A_CONT); }
    if (isid(c, "discard")) { c->p++; expect(c, ";"); return mk(c, A_DISCARD); }
    if (isid(c, "precision")) { while (!isop(c, ";") && PK(c)->k != TK_EOF) c->p++; eat(c, ";"); return mk(c, A_EMPTY); }
    if (looks_decl(c)) return p_decl(c, 0);
    ast* s = mk(c, A_EXPR); s->a = p_expr(c); expect(c, ";");
    return s;
}

#define GL_MEM 65536
typedef struct { int slot, ty, kind, idx, stride, off, nsw; unsigned char sw[4]; } gval;
enum { V_DIRECT, V_SWZ, V_DYN };

static int emit(gctx* c, int op, int n, int aux, int d, int a, int b, int cc) {
    gsh* s = c->sh;
    if (s->ncode >= s->ccode) { int nc = s->ccode ? s->ccode * 2 : 1024; ginst* ni = nalloc(sizeof(ginst) * (size_t)nc); if (s->ncode) memcpy(ni, s->code, sizeof(ginst) * (size_t)s->ncode); s->code = ni; s->ccode = nc; }
    ginst* i = &s->code[s->ncode]; i->op = (unsigned char)op; i->n = (unsigned char)n; i->aux = (short)aux; i->d = d; i->a = a; i->b = b; i->c = cc;
    return s->ncode++;
}
static int tmp(gctx* c, int size) { int s = c->sh->nmem; c->sh->nmem += size > 0 ? size : 1; if (c->sh->nmem > c->tmpmax) c->tmpmax = c->sh->nmem; if (c->sh->nmem >= c->ctop) cerr(c, 0, "shader uses too much memory", 0); return s; }
static int cst(gctx* c, float v) {
    for (int i = 0; i < c->ncst; i++) if (c->cvals[i] == v && !(v == 0 && 1 / v != 1 / c->cvals[i])) return c->cslots[i];
    int sl = --c->ctop;
    if (sl <= c->sh->nmem) cerr(c, 0, "shader uses too much memory", 0);
    else c->sh->mem[sl] = v;
    if (c->ncst < 512) { c->cvals[c->ncst] = v; c->cslots[c->ncst] = sl; c->ncst++; }
    return sl;
}
static int cstv(gctx* c, const float* v, int n) { int sl = c->ctop - n; if (sl <= c->sh->nmem) { cerr(c, 0, "shader uses too much memory", 0); return 0; } c->ctop = sl; for (int i = 0; i < n; i++) c->sh->mem[sl + i] = v[i]; return sl; }
static gval gv_direct(int slot, int ty) { gval v; memset(&v, 0, sizeof(v)); v.slot = slot; v.ty = ty; v.kind = V_DIRECT; return v; }

static gsym* sym_find(gctx* c, const char* n) { for (gsym* s = c->syms; s; s = s->next) if (!strcmp(s->name, n)) return s; return 0; }
static void mark_named(gctx* c, int slot, int n) { if (!c->named) return; for (int i = 0; i < n; i++) { int k = slot + i; if (k >= 0 && k < GL_MEM) c->named[k >> 3] |= (unsigned char)(1 << (k & 7)); } }
static gsym* sym_add(gctx* c, const char* n, int ty, int slot, int storage) { mark_named(c, slot, GT[ty].size > 0 ? GT[ty].size : 4); gsym* s = nzalloc(sizeof(gsym)); snprintf(s->name, sizeof(s->name), "%s", n); s->ty = ty; s->slot = slot; s->storage = storage; s->depth = c->depth; s->next = c->syms; c->syms = s; return s; }
static void sym_pop(gctx* c, int depth) { while (c->syms && c->syms->depth > depth) c->syms = c->syms->next; }

static int load(gctx* c, gval v) {
    if (v.kind == V_DIRECT) return v.slot;
    int n = comps(v.ty);
    int t = tmp(c, n);
    if (v.kind == V_SWZ) { int m = 0; for (int i = 0; i < v.nsw; i++) m |= v.sw[i] << (2 * i); emit(c, I_SWZ, v.nsw, m, t, v.slot, 0, 0); return t; }
    emit(c, I_IDXR, n, v.stride, t, v.slot, v.idx, v.off);
    return t;
}
static void store(gctx* c, gval lv, int src) {
    int n = comps(lv.ty);
    if (lv.kind == V_DIRECT) { if (src != lv.slot) emit(c, I_MOV, n, 0, lv.slot, src, 0, 0); return; }
    if (lv.kind == V_SWZ) { int m = 0; for (int i = 0; i < lv.nsw; i++) m |= lv.sw[i] << (2 * i); emit(c, I_SWZW, lv.nsw, m, lv.slot, src, 0, 0); return; }
    emit(c, I_IDXW, n, lv.stride, lv.slot, src, lv.idx, lv.off);
}
static gval cexpr(gctx* c, ast* e);
static int rval(gctx* c, ast* e, int* ty) { gval v = cexpr(c, e); *ty = v.ty; return load(c, v); }
static int bcast(gctx* c, int slot, int ty, int n) { if (comps(ty) == n || comps(ty) != 1) return slot; int t = tmp(c, n); emit(c, I_MOVS, n, 0, t, slot, 0, 0); return t; }
static int base_of(int ty) { gtype* g = &GT[ty]; return g->kind == K_SCALAR || g->kind == K_VEC ? g->base : 'f'; }

static int swz_idx(char ch) { switch (ch) { case 'x': case 'r': case 's': return 0; case 'y': case 'g': case 't': return 1; case 'z': case 'b': case 'p': return 2; case 'w': case 'a': case 'q': return 3; } return -1; }

static gval do_index(gctx* c, gval b, ast* ie, int line) {
    gtype* g = &GT[b.ty];
    int et, stride;
    if (g->kind == K_ARRAY) { et = g->elem; stride = GT[et].size; }
    else if (g->kind == K_MAT) { et = ty_vec('f', g->n); stride = g->n; }
    else if (g->kind == K_VEC) { et = ty_vec(g->base, 1); stride = 1; }
    else { cerr(c, line, "cannot index this value", 0); return b; }
    int k;
    if (p_const_int(c, ie, &k)) {
        int lim = g->kind == K_ARRAY ? g->n : g->n;
        if (k < 0 || k >= lim) { cerr(c, line, "index out of range", 0); return b; }
        if (b.kind == V_DIRECT) { gval r = gv_direct(b.slot + k * stride, et); return r; }
        if (b.kind == V_DYN) { b.off += k * stride; b.ty = et; return b; }
        if (b.kind == V_SWZ && g->kind == K_VEC) { gval r = b; r.nsw = 1; r.sw[0] = b.sw[k]; r.ty = et; return r; }
        int t = load(c, b); return gv_direct(t + k * stride, et);
    }
    int ity; int is = rval(c, ie, &ity);
    if (b.kind == V_SWZ || b.kind == V_DYN) { int t = load(c, b); b = gv_direct(t, b.ty); }
    gval r; memset(&r, 0, sizeof(r)); r.kind = V_DYN; r.slot = b.slot; r.idx = is; r.stride = stride; r.ty = et; r.off = 0;
    r.nsw = g->n;
    return r;
}

static int ctor(gctx* c, ast* e, int ty) {
    gtype* g = &GT[ty];
    int n = g->size;
    int t = tmp(c, n);
    if (g->kind == K_STRUCT) {
        if (e->nkids != g->nf) { cerr(c, e->line, "wrong number of arguments to constructor", g->name); return t; }
        for (int i = 0; i < g->nf; i++) { int aty; int a = rval(c, e->kids[i], &aty); emit(c, I_MOV, GT[g->fty[i]].size, 0, t + g->foff[i], a, 0, 0); }
        return t;
    }
    int args[24], aty[24];
    for (int i = 0; i < e->nkids; i++) args[i] = rval(c, e->kids[i], &aty[i]);
    if (g->kind == K_SCALAR) {
        if (e->nkids < 1) { cerr(c, e->line, "constructor needs an argument", 0); return t; }
        if (ty == TY_INT && base_of(aty[0]) != 'i') emit(c, I_TOINT, 1, 0, t, args[0], 0, 0);
        else if (ty == TY_BOOL && base_of(aty[0]) != 'b') emit(c, I_TOBOOL, 1, 0, t, args[0], 0, 0);
        else emit(c, I_MOV, 1, 0, t, args[0], 0, 0);
        return t;
    }
    if (g->kind == K_MAT && e->nkids == 1 && GT[aty[0]].kind == K_SCALAR) {
        float z = 0; int zs = cst(c, z); emit(c, I_MOVS, n, 0, t, zs, 0, 0);
        for (int i = 0; i < g->n; i++) emit(c, I_MOV, 1, 0, t + i * g->n + i, args[0], 0, 0);
        return t;
    }
    if (g->kind == K_MAT && e->nkids == 1 && GT[aty[0]].kind == K_MAT) {
        int sn = GT[aty[0]].n, dn = g->n;
        float z = 0, o = 1; int zs = cst(c, z), os = cst(c, o);
        for (int col = 0; col < dn; col++) for (int row = 0; row < dn; row++) { int d = t + col * dn + row; if (col < sn && row < sn) emit(c, I_MOV, 1, 0, d, args[0] + col * sn + row, 0, 0); else emit(c, I_MOV, 1, 0, d, col == row ? os : zs, 0, 0); }
        return t;
    }
    if (e->nkids == 1 && GT[aty[0]].kind == K_SCALAR) {
        if (g->base == 'i' && base_of(aty[0]) != 'i') { int q = tmp(c, 1); emit(c, I_TOINT, 1, 0, q, args[0], 0, 0); args[0] = q; }
        if (g->base == 'b' && base_of(aty[0]) != 'b') { int q = tmp(c, 1); emit(c, I_TOBOOL, 1, 0, q, args[0], 0, 0); args[0] = q; }
        emit(c, I_MOVS, n, 0, t, args[0], 0, 0); return t;
    }
    int fill = 0;
    for (int i = 0; i < e->nkids && fill < n; i++) { int k = comps(aty[i]); if (k > n - fill) k = n - fill; emit(c, g->base == 'i' && base_of(aty[i]) != 'i' ? I_TOINT : g->base == 'b' && base_of(aty[i]) != 'b' ? I_TOBOOL : I_MOV, k, 0, t + fill, args[i], 0, 0); fill += k; }
    if (fill < n) cerr(c, e->line, "not enough data provided for construction", g->name);
    return t;
}

typedef struct { const char* n; int id, na, rk; } binfo;
enum { R_SAME, R_FLOAT, R_VEC3, R_BVEC, R_BOOL, R_VEC4 };
static const binfo BI[] = {
    { "radians", B_RADIANS, 1, R_SAME }, { "degrees", B_DEGREES, 1, R_SAME }, { "sin", B_SIN, 1, R_SAME }, { "cos", B_COS, 1, R_SAME }, { "tan", B_TAN, 1, R_SAME },
    { "asin", B_ASIN, 1, R_SAME }, { "acos", B_ACOS, 1, R_SAME }, { "atan", B_ATAN, 1, R_SAME }, { "pow", B_POW, 2, R_SAME }, { "exp", B_EXP, 1, R_SAME }, { "log", B_LOG, 1, R_SAME },
    { "exp2", B_EXP2, 1, R_SAME }, { "log2", B_LOG2, 1, R_SAME }, { "sqrt", B_SQRT, 1, R_SAME }, { "inversesqrt", B_ISQRT, 1, R_SAME }, { "abs", B_ABS, 1, R_SAME }, { "sign", B_SIGN, 1, R_SAME },
    { "floor", B_FLOOR, 1, R_SAME }, { "ceil", B_CEIL, 1, R_SAME }, { "fract", B_FRACT, 1, R_SAME }, { "mod", B_MOD, 2, R_SAME }, { "min", B_MIN, 2, R_SAME }, { "max", B_MAX, 2, R_SAME },
    { "clamp", B_CLAMP, 3, R_SAME }, { "mix", B_MIX, 3, R_SAME }, { "step", B_STEP, 2, R_SAME }, { "smoothstep", B_SSTEP, 3, R_SAME }, { "length", B_LENGTH, 1, R_FLOAT }, { "distance", B_DIST, 2, R_FLOAT },
    { "dot", B_DOT, 2, R_FLOAT }, { "cross", B_CROSS, 2, R_VEC3 }, { "normalize", B_NORM, 1, R_SAME }, { "faceforward", B_FFWD, 3, R_SAME }, { "reflect", B_REFL, 2, R_SAME }, { "refract", B_REFR, 3, R_SAME },
    { "matrixCompMult", B_MCM, 2, R_SAME }, { "lessThan", B_LT, 2, R_BVEC }, { "lessThanEqual", B_LE, 2, R_BVEC }, { "greaterThan", B_GT, 2, R_BVEC }, { "greaterThanEqual", B_GE, 2, R_BVEC },
    { "equal", B_EQ, 2, R_BVEC }, { "notEqual", B_NE, 2, R_BVEC }, { "any", B_ANY, 1, R_BOOL }, { "all", B_ALL, 1, R_BOOL }, { "not", B_NOTV, 1, R_SAME },
    { "texture2D", B_TEX, 2, R_VEC4 }, { "texture2DLod", B_TEX, 3, R_VEC4 }, { "texture2DLodEXT", B_TEX, 3, R_VEC4 }, { "texture2DProj", B_TEXP, 2, R_VEC4 }, { "texture2DProjLod", B_TEXP, 3, R_VEC4 }, { "texture2DGradEXT", B_TEX, 4, R_VEC4 },
    { "textureCube", B_TEXCUBE, 2, R_VEC4 }, { "textureCubeLod", B_TEXCUBE, 3, R_VEC4 }, { "textureCubeLodEXT", B_TEXCUBE, 3, R_VEC4 }, { "texture", B_TEX, 2, R_VEC4 },
    { "textureLod", B_TEX, 3, R_VEC4 }, { "textureGrad", B_TEX, 4, R_VEC4 }, { "textureProj", B_TEXP, 2, R_VEC4 }, { "textureProjLod", B_TEXP, 3, R_VEC4 }, { "textureLodOffset", B_TEX, 4, R_VEC4 }, { "textureOffset", B_TEX, 3, R_VEC4 },
    { "texelFetch", B_TEXF, 3, R_VEC4 }, { "textureSize", B_TEXSZ, 2, R_VEC4 }, { "round", B_ROUND, 1, R_SAME }, { "roundEven", B_ROUND, 1, R_SAME }, { "trunc", B_TRUNC, 1, R_SAME },
    { "dFdx", B_DERIV, 1, R_SAME }, { "dFdy", B_DERIV, 1, R_SAME }, { "fwidth", B_DERIV, 1, R_SAME }, { 0, 0, 0, 0 }
};
static gfunc* fn_find(gctx* c, const char* n, int na, int* aty) {
    gfunc* best = 0;
    for (gfunc* f = c->funcs; f; f = f->next) {
        if (strcmp(f->name, n) || f->np != na) continue;
        int exact = 1, ok = 1;
        for (int i = 0; i < na; i++) { if (f->pty[i] != aty[i]) { exact = 0; if (!(comps(f->pty[i]) == comps(aty[i]) && GT[f->pty[i]].kind == GT[aty[i]].kind)) ok = 0; } }
        if (exact) return f;
        if (ok && !best) best = f;
    }
    return best;
}
static gval ccall(gctx* c, ast* e) {
    int tyn = type_by_name(e->name);
    if (tyn >= 0) { int ty = tyn; if (e->isint > 0) ty = ty_array(tyn, e->isint); if (GT[ty].kind == K_ARRAY) { cerr(c, e->line, "array constructors are not supported", 0); return gv_direct(0, TY_FLOAT); } return gv_direct(ctor(c, e, ty), ty); }
    int aty[24], args[24];
    gfunc* uf = 0;
    for (gfunc* f = c->funcs; f; f = f->next) if (!strcmp(f->name, e->name)) { uf = f; break; }
    if (uf) {
        ast* lvs[24]; gval lvv[24];
        for (int i = 0; i < e->nkids; i++) { lvs[i] = e->kids[i]; lvv[i] = cexpr(c, e->kids[i]); aty[i] = lvv[i].ty; args[i] = load(c, lvv[i]); }
        gfunc* f = fn_find(c, e->name, e->nkids, aty);
        if (!f) { cerr(c, e->line, "no matching overloaded function found", e->name); return gv_direct(0, TY_FLOAT); }
        for (int i = 0; i < f->np; i++) if (f->pq[i] != 2) emit(c, I_MOV, comps(f->pty[i]), 0, f->pslot[i], args[i], 0, 0);
        int at = emit(c, I_CALL, 0, 0, f->entry, 0, 0, 0);
        if (f->entry < 0 && f->nfix < 256) f->fix[f->nfix++] = at;
        for (int i = 0; i < f->np; i++) if (f->pq[i] >= 2) store(c, lvv[i], f->pslot[i]);
        (void)lvs;
        if (f->ret == TY_VOID) return gv_direct(0, TY_VOID);
        int t = tmp(c, comps(f->ret)); emit(c, I_MOV, comps(f->ret), 0, t, f->rslot, 0, 0);
        return gv_direct(t, f->ret);
    }
    const binfo* bi = 0;
    for (int i = 0; BI[i].n; i++) if (!strcmp(BI[i].n, e->name) && (BI[i].na == e->nkids || (BI[i].id == B_ATAN && e->nkids == 2) || ((BI[i].id == B_TEX || BI[i].id == B_TEXP || BI[i].id == B_TEXCUBE) && e->nkids >= 2 && e->nkids <= 4))) { bi = &BI[i]; break; }
    if (!bi) { cerr(c, e->line, "no matching overloaded function found", e->name); return gv_direct(0, TY_FLOAT); }
    for (int i = 0; i < e->nkids; i++) args[i] = rval(c, e->kids[i], &aty[i]);
    int id = bi->id;
    if (id == B_ATAN && e->nkids == 2) id = B_ATAN2;
    if (id == B_TEXF || id == B_TEXSZ) {
        int t = tmp(c, 4);
        emit(c, I_TEX, 4, id == B_TEXF ? 2 : 3, t, args[0], id == B_TEXF ? args[1] : args[0], 0);
        return gv_direct(t, id == B_TEXF ? TY_VEC4 : TY_IVEC2);
    }
    if (id == B_TEX && aty[0] == TY_SCUBE) id = B_TEXCUBE;
    if (id == B_TEX || id == B_TEXP || id == B_TEXCUBE) {
        int t = tmp(c, 4);
        int uv = args[1];
        if (id == B_TEXP) { int q = tmp(c, 2); int w = comps(aty[1]); emit(c, I_BI, w, B_TEXP, q, args[1], 0, 0); uv = q; }
        emit(c, I_TEX, 4, id == B_TEXCUBE ? 1 : 0, t, args[0], uv, 0);
        return gv_direct(t, TY_VEC4);
    }
    int vt = aty[0];
    for (int i = 0; i < e->nkids; i++) if (comps(aty[i]) > comps(vt) && GT[aty[i]].kind != K_MAT) vt = aty[i];
    if (id == B_MCM) vt = aty[0];
    int n = comps(vt);
    if (id != B_REFR && id != B_LENGTH && id != B_DIST && id != B_DOT && id != B_ANY && id != B_ALL && id != B_CROSS) for (int i = 0; i < e->nkids; i++) args[i] = bcast(c, args[i], aty[i], n);
    int rt = vt;
    switch (bi->rk) { case R_FLOAT: rt = TY_FLOAT; break; case R_VEC3: rt = TY_VEC3; break; case R_BVEC: rt = ty_vec('b', comps(vt)); break; case R_BOOL: rt = TY_BOOL; break; case R_VEC4: rt = TY_VEC4; break; }
    if (id == B_STEP || id == B_SSTEP) { rt = aty[e->nkids - 1]; if (comps(rt) == 1) rt = vt; }
    int t = tmp(c, comps(rt));
    if (id == B_MCM) { emit(c, I_MUL, n, 0, t, args[0], args[1], 0); return gv_direct(t, rt); }
    emit(c, I_BI, n, id, t, args[0], e->nkids > 1 ? args[1] : 0, e->nkids > 2 ? args[2] : 0);
    return gv_direct(t, rt);
}

static int arith(gctx* c, int op, int a, int at, int b, int bt, int* rty, int line) {
    gtype* A = &GT[at]; gtype* B = &GT[bt];
    if (!is_num(at) || !is_num(bt)) { cerr(c, line, "wrong operand types for arithmetic", 0); *rty = TY_FLOAT; return a; }
    int ib = base_of(at) == 'i' && base_of(bt) == 'i';
    int iop = op == '+' ? I_ADD : op == '-' ? I_SUB : op == '*' ? I_MUL : op == '/' ? I_DIV : I_MOD;
    if (op == '*' && A->kind == K_MAT && B->kind == K_VEC) { int n = A->n; int t = tmp(c, n); emit(c, I_MVM, n, 0, t, a, b, 0); *rty = bt; return t; }
    if (op == '*' && A->kind == K_VEC && B->kind == K_MAT) { int n = B->n; int t = tmp(c, n); emit(c, I_VMM, n, 0, t, a, b, 0); *rty = at; return t; }
    if (op == '*' && A->kind == K_MAT && B->kind == K_MAT) { int n = A->n; int t = tmp(c, n * n); emit(c, I_MMM, n, 0, t, a, b, 0); *rty = at; return t; }
    int na = comps(at), nb = comps(bt);
    int n = na > nb ? na : nb;
    if (na != nb && na != 1 && nb != 1) { cerr(c, line, "wrong operand types - no operation exists for these operand sizes", 0); *rty = at; return a; }
    int aux = (na == 1 && n > 1 ? 1 : 0) | (nb == 1 && n > 1 ? 2 : 0) | (ib ? 4 : 0);
    int t = tmp(c, n);
    emit(c, iop, n, aux, t, a, b, 0);
    *rty = na >= nb ? at : bt;
    if (comps(*rty) == 1 && n > 1) *rty = na > nb ? at : bt;
    if (!ib && base_of(*rty) == 'i') *rty = ty_vec('f', comps(*rty));
    return t;
}
static gval cexpr(gctx* c, ast* e) {
    if (c->fail || !e) return gv_direct(0, TY_FLOAT);
    switch (e->k) {
    case A_NUM: return gv_direct(cst(c, e->num), e->ty);
    case A_ID: {
        gsym* s = sym_find(c, e->name);
        if (!s) { cerr(c, e->line, "undeclared identifier", e->name); return gv_direct(0, TY_FLOAT); }
        if (s->cval_ok) return gv_direct(cst(c, s->cval), s->ty);
        return gv_direct(s->slot, s->ty);
    }
    case A_FIELD: {
        gval b = cexpr(c, e->a);
        gtype* g = &GT[b.ty];
        if (g->kind == K_STRUCT) {
            for (int i = 0; i < g->nf; i++) if (!strcmp(g->fname[i], e->name)) {
                if (b.kind == V_DIRECT) return gv_direct(b.slot + g->foff[i], g->fty[i]);
                if (b.kind == V_DYN) { b.off += g->foff[i]; b.ty = g->fty[i]; return b; }
                int t = load(c, b); return gv_direct(t + g->foff[i], g->fty[i]);
            }
            cerr(c, e->line, "no such field in structure", e->name); return b;
        }
        if (g->kind == K_VEC || g->kind == K_SCALAR) {
            int l = (int)strlen(e->name); if (l < 1 || l > 4) { cerr(c, e->line, "illegal vector field selection", e->name); return b; }
            unsigned char sw[4];
            for (int i = 0; i < l; i++) { int k = swz_idx(e->name[i]); if (k < 0 || k >= g->n) { cerr(c, e->line, "vector field selection out of range", e->name); return b; } sw[i] = (unsigned char)k; }
            int rt = ty_vec(g->base, l);
            if (b.kind == V_SWZ) { gval r = b; for (int i = 0; i < l; i++) r.sw[i] = b.sw[sw[i]]; r.nsw = l; r.ty = rt; return r; }
            if (b.kind == V_DYN) { int t = load(c, b); b = gv_direct(t, b.ty); }
            if (l == 1) return gv_direct(b.slot + sw[0], rt);
            int seq = 1; for (int i = 0; i < l; i++) if (sw[i] != sw[0] + i) seq = 0;
            if (seq) return gv_direct(b.slot + sw[0], rt);
            gval r; memset(&r, 0, sizeof(r)); r.kind = V_SWZ; r.slot = b.slot; r.ty = rt; r.nsw = l; memcpy(r.sw, sw, 4);
            return r;
        }
        if (g->kind == K_ARRAY && !strcmp(e->name, "length")) return gv_direct(cst(c, (float)g->n), TY_INT);
        cerr(c, e->line, "field selection requires structure or vector", e->name); return b;
    }
    case A_INDEX: { gval b = cexpr(c, e->a); return do_index(c, b, e->b, e->line); }
    case A_CALL:
        if (e->nkids == 1 && !strcmp(e->name, "length") && e->kids[0]) { gval b = cexpr(c, e->kids[0]); if (GT[b.ty].kind == K_ARRAY) return gv_direct(cst(c, (float)GT[b.ty].n), TY_INT); }
        return ccall(c, e);
    case A_UN: {
        int ty; int a = rval(c, e->a, &ty);
        if (e->op == '+') return gv_direct(a, ty);
        int t = tmp(c, comps(ty));
        emit(c, e->op == '-' ? I_NEG : I_NOT, comps(ty), 0, t, a, 0, 0);
        return gv_direct(t, ty);
    }
    case A_BIN: {
        int op = e->op;
        if (op == 'A' || op == 'O') {
            int t = tmp(c, 1); int ty; int a = rval(c, e->a, &ty);
            emit(c, I_MOV, 1, 0, t, a, 0, 0);
            int j = emit(c, op == 'A' ? I_JZ : I_JNZ, 0, 0, 0, t, 0, 0);
            int mark = c->sh->nmem;
            int b = rval(c, e->b, &ty);
            emit(c, I_MOV, 1, 0, t, b, 0, 0);
            c->sh->nmem = mark;
            c->sh->code[j].d = c->sh->ncode;
            return gv_direct(t, TY_BOOL);
        }
        int at, bt; int a = rval(c, e->a, &at); int b = rval(c, e->b, &bt);
        if (op == '<' || op == '>' || op == 'l' || op == 'g') { int t = tmp(c, 1); emit(c, op == '<' ? I_LT : op == '>' ? I_GT : op == 'l' ? I_LE : I_GE, 1, 0, t, a, b, 0); return gv_direct(t, TY_BOOL); }
        if (op == 'E' || op == 'N') { int t = tmp(c, 1); int n = comps(at) < comps(bt) ? comps(bt) : comps(at); emit(c, op == 'E' ? I_EQ : I_NE, n, 0, t, a, b, 0); return gv_direct(t, TY_BOOL); }
        if (op == 'X') { int t = tmp(c, 1); emit(c, I_XOR, 1, 0, t, a, b, 0); return gv_direct(t, TY_BOOL); }
        if (op == '+' || op == '-' || op == '*' || op == '/' || op == '%') { int rt; int t = arith(c, op, a, at, b, bt, &rt, e->line); return gv_direct(t, rt); }
        cerr(c, e->line, "unsupported operator", 0);
        return gv_direct(a, at);
    }
    case A_ASSIGN: {
        gval lv = cexpr(c, e->a);
        int rt; int r = rval(c, e->b, &rt);
        if (e->op != '=') { int cur = load(c, lv); int nt; r = arith(c, e->op, cur, lv.ty, r, rt, &nt, e->line); rt = nt; }
        if (comps(rt) == 1 && comps(lv.ty) > 1 && GT[lv.ty].kind != K_MAT) r = bcast(c, r, rt, comps(lv.ty));
        else if (comps(rt) != comps(lv.ty)) { cerr(c, e->line, "cannot convert from '", GT[rt].name); return lv; }
        store(c, lv, r);
        return lv;
    }
    case A_COND: {
        int ty; int cnd = rval(c, e->a, &ty);
        int j = emit(c, I_JZ, 0, 0, 0, cnd, 0, 0);
        int bt; int b = rval(c, e->b, &bt);
        int t = tmp(c, comps(bt));
        emit(c, I_MOV, comps(bt), 0, t, b, 0, 0);
        int j2 = emit(c, I_JMP, 0, 0, 0, 0, 0, 0);
        c->sh->code[j].d = c->sh->ncode;
        int ct; int cc = rval(c, e->c, &ct);
        emit(c, I_MOV, comps(bt), 0, t, cc, 0, 0);
        c->sh->code[j2].d = c->sh->ncode;
        return gv_direct(t, bt);
    }
    case A_POSTINC: case A_PREINC: {
        gval lv = cexpr(c, e->a);
        int cur = load(c, lv);
        int n = comps(lv.ty);
        int old = tmp(c, n); emit(c, I_MOV, n, 0, old, cur, 0, 0);
        int one = cst(c, 1.0f);
        int t = tmp(c, n);
        emit(c, e->op == '+' ? I_ADD : I_SUB, n, n > 1 ? 2 : 0, t, cur, one, 0);
        store(c, lv, t);
        if (e->k == A_POSTINC) return gv_direct(old, lv.ty);
        return lv;
    }
    case A_SEQ: { cexpr(c, e->a); return cexpr(c, e->b); }
    }
    cerr(c, e->line, "unsupported expression", 0);
    return gv_direct(0, TY_FLOAT);
}

static int named_any(gctx* c, int slot, int n) { for (int i = 0; i < n; i++) { int k = slot + i; if (k < 0 || k >= GL_MEM) return 1; if (c->named[k >> 3] & (1 << (k & 7))) return 1; } return 0; }
static int ovl(int a, int an, int b, int bn) { return a < b + bn && b < a + an; }
static void peep(gctx* c, gsh* sh) {
    if (!c->named || sh->ncode < 2) return;
    int n = sh->ncode;
    unsigned char* tgt = nzalloc((size_t)n + 1);
    int* map = nalloc(sizeof(int) * ((size_t)n + 1));
    if (!tgt || !map) { if (tgt) nfree(tgt); if (map) nfree(map); return; }
    for (int i = 0; i < n; i++) { ginst* I = &sh->code[i]; if ((I->op == I_JMP || I->op == I_JZ || I->op == I_JNZ || I->op == I_CALL) && I->d >= 0 && I->d <= n) tgt[I->d] = 1; }
    if (sh->main_entry >= 0 && sh->main_entry <= n) tgt[sh->main_entry] = 1;
    unsigned char* kill = nzalloc((size_t)n + 1);
    if (!kill) { nfree(tgt); nfree(map); return; }
    for (int i = 0; i + 1 < n; i++) {
        ginst* P = &sh->code[i]; ginst* M = &sh->code[i + 1];
        if (M->op != I_MOV || tgt[i + 1] || kill[i]) continue;
        int on;
        switch (P->op) {
        case I_MOV: case I_MOVS: case I_ADD: case I_SUB: case I_MUL: case I_DIV: case I_MOD: case I_NEG: case I_SWZ: case I_MVM: case I_VMM: case I_IDXR: on = P->n; break;
        case I_TEX: on = 4; break;
        case I_LT: case I_LE: case I_GT: case I_GE: on = 1; break;
        case I_BI: { int x = P->aux; on = (x <= B_SSTEP || x == B_NORM || x == B_FFWD || x == B_REFL || x == B_REFR || (x >= B_LT && x <= B_NE) || x == B_NOTV) ? P->n : (x == B_LENGTH || x == B_DIST || x == B_DOT) ? 1 : x == B_CROSS ? 3 : -1; break; }
        default: on = -1;
        }
        if (on <= 0 || M->n != on || M->a != P->d) continue;
        if (named_any(c, P->d, on)) continue;
        int D = M->d;
        if (P->op == I_BI && (ovl(D, on, P->a, P->n) || ovl(D, on, P->b, P->n) || ovl(D, on, P->c, P->n))) continue;
        if (P->op == I_ADD || P->op == I_SUB || P->op == I_MUL || P->op == I_DIV || P->op == I_MOD) { if ((P->aux & 3) && on > 1 && (ovl(D, on, P->a, on) || ovl(D, on, P->b, on))) continue; }
        if (P->op == I_MOVS && on > 1 && ovl(D, on, P->a, 1)) continue;
        if (P->op == I_NEG || P->op == I_MOV) { if (ovl(D, on, P->a, on) && D != P->a) continue; }
        if (P->op == I_LT || P->op == I_LE || P->op == I_GT || P->op == I_GE) { }
        P->d = D; kill[i + 1] = 1;
    }
    int k = 0;
    for (int i = 0; i < n; i++) { map[i] = k; if (!kill[i]) sh->code[k++] = sh->code[i]; }
    map[n] = k;
    for (int i = 0; i < k; i++) { ginst* I = &sh->code[i]; if ((I->op == I_JMP || I->op == I_JZ || I->op == I_JNZ || I->op == I_CALL) && I->d >= 0 && I->d <= n) I->d = map[I->d]; }
    if (sh->main_entry >= 0 && sh->main_entry <= n) sh->main_entry = map[sh->main_entry];
    sh->ncode = k;
    nfree(tgt); nfree(map); nfree(kill);
}
static void cstmt(gctx* c, ast* s);
static void decl_var(gctx* c, ast* v, int global) {
    if (sym_find(c, v->name) && sym_find(c, v->name)->depth == c->depth) { cerr(c, v->line, "redefinition", v->name); return; }
    int ty = v->ty;
    int slot = tmp(c, GT[ty].size);
    int storage = ST_LOCAL;
    int q = v->op;
    if (q & 2) storage = ST_UNIFORM; else if (q & 4) storage = ST_ATTR; else if (q & 8) storage = ST_VARY;
    gsym* s = sym_add(c, v->name, ty, slot, storage);
    if ((q & 1) && v->a && (ty == TY_FLOAT || ty == TY_INT)) { int k; if (p_const_int(c, v->a, &k) && v->a->k != A_NUM) { (void)k; } }
    if (global && storage != ST_LOCAL && c->sh->nvars < 256) { gvar* g = &c->sh->vars[c->sh->nvars++]; snprintf(g->name, sizeof(g->name), "%s", v->name); g->ty = ty; g->size = GT[ty].size; g->slot = slot; g->storage = storage; }
    if (v->a) {
        if (global) { if (c->nginit < 256) { ast* asg = nzalloc(sizeof(ast)); asg->k = A_ASSIGN; asg->op = '='; asg->line = v->line; ast* id = nzalloc(sizeof(ast)); id->k = A_ID; snprintf(id->name, sizeof(id->name), "%s", v->name); asg->a = id; asg->b = v->a; c->ginit[c->nginit++] = asg; } }
        else { int mark = c->sh->nmem; gval lv = gv_direct(slot, ty); int rt; int r = rval(c, v->a, &rt); if (comps(rt) == 1 && comps(ty) > 1 && GT[ty].kind == K_VEC) r = bcast(c, r, rt, comps(ty)); else if (comps(rt) != comps(ty)) cerr(c, v->line, "cannot convert initializer", 0); store(c, lv, r); c->sh->nmem = mark; }
    }
    (void)s;
}
static void cstmt(gctx* c, ast* s) {
    if (c->fail || !s) return;
    int mark = c->sh->nmem;
    switch (s->k) {
    case A_EMPTY: case A_STRUCTDEF: case A_PRECISION: return;
    case A_DECL: for (ast* v = s->a; v; v = v->next) decl_var(c, v, 0); return;
    case A_EXPR: cexpr(c, s->a); c->sh->nmem = mark; return;
    case A_BLOCK: { c->depth++; for (ast* q = s->a; q; q = q->next) cstmt(c, q); sym_pop(c, --c->depth); return; }
    case A_IF: {
        int ty; int cnd = rval(c, s->a, &ty); c->sh->nmem = mark;
        int j = emit(c, I_JZ, 0, 0, 0, cnd, 0, 0);
        cstmt(c, s->b);
        if (s->c) { int j2 = emit(c, I_JMP, 0, 0, 0, 0, 0, 0); c->sh->code[j].d = c->sh->ncode; cstmt(c, s->c); c->sh->code[j2].d = c->sh->ncode; }
        else c->sh->code[j].d = c->sh->ncode;
        return;
    }
    case A_FOR: case A_WHILE: case A_DO: {
        c->depth++;
        if (s->k == A_FOR && s->a) { if (s->a->k == A_DECL) cstmt(c, s->a); else if (s->a->k == A_EXPR) { cexpr(c, s->a->a); } }
        int ld = c->loopd++;
        if (ld >= 32) { cerr(c, s->line, "loops nested too deeply", 0); return; }
        c->nbrk[ld] = 0; c->ncont[ld] = 0;
        int top = c->sh->ncode, jz = -1;
        if (s->k == A_DO) { cstmt(c, s->b); }
        else { if (s->k == A_FOR ? s->b != 0 : 1) { int m2 = c->sh->nmem; int ty; int cnd = rval(c, s->k == A_FOR ? s->b : s->a, &ty); c->sh->nmem = m2; jz = emit(c, I_JZ, 0, 0, 0, cnd, 0, 0); } cstmt(c, s->k == A_FOR ? s->d : s->b); }
        int cont = c->sh->ncode;
        if (s->k == A_FOR && s->c) { int m2 = c->sh->nmem; cexpr(c, s->c); c->sh->nmem = m2; }
        if (s->k == A_DO) { int m2 = c->sh->nmem; int ty; int cnd = rval(c, s->a, &ty); c->sh->nmem = m2; emit(c, I_JNZ, 0, 0, top, cnd, 0, 0); }
        else emit(c, I_JMP, 0, 0, top, 0, 0, 0);
        int end = c->sh->ncode;
        if (jz >= 0) c->sh->code[jz].d = end;
        for (int i = 0; i < c->nbrk[ld]; i++) c->sh->code[c->brk[ld][i]].d = end;
        for (int i = 0; i < c->ncont[ld]; i++) c->sh->code[c->cont[ld][i]].d = cont;
        c->loopd--;
        sym_pop(c, --c->depth);
        return;
    }
    case A_BREAK: case A_CONT: {
        if (!c->loopd) { cerr(c, s->line, "break or continue outside a loop", 0); return; }
        int ld = c->loopd - 1; int j = emit(c, I_JMP, 0, 0, 0, 0, 0, 0);
        if (s->k == A_BREAK) { if (c->nbrk[ld] < 64) c->brk[ld][c->nbrk[ld]++] = j; } else { if (c->ncont[ld] < 64) c->cont[ld][c->ncont[ld]++] = j; }
        return;
    }
    case A_RET: {
        if (s->a && c->curf) { int ty; int r = rval(c, s->a, &ty); emit(c, I_MOV, comps(c->curf->ret), 0, c->curf->rslot, r, 0, 0); }
        emit(c, I_RET, 0, 0, 0, 0, 0, 0);
        c->sh->nmem = mark;
        return;
    }
    case A_DISCARD: emit(c, I_DISC, 0, 0, 0, 0, 0, 0); return;
    }
    cerr(c, s->line, "unsupported statement", 0);
}

typedef struct { char name[96]; int base, count, slot, size; } gleaf;
static void flatten(gsh* sh, gleaf* out, int* n, int cap, const char* name, int ty, int slot) {
    gtype* g = &GT[ty];
    if (g->kind == K_STRUCT) { for (int i = 0; i < g->nf; i++) { char nm[96]; snprintf(nm, sizeof(nm), "%s.%s", name, g->fname[i]); flatten(sh, out, n, cap, nm, g->fty[i], slot + g->foff[i]); } return; }
    if (g->kind == K_ARRAY) {
        gtype* el = &GT[g->elem];
        if (el->kind == K_STRUCT || el->kind == K_ARRAY) { for (int i = 0; i < g->n; i++) { char nm[96]; snprintf(nm, sizeof(nm), "%s[%d]", name, i); flatten(sh, out, n, cap, nm, g->elem, slot + i * el->size); } return; }
        if (*n < cap) { gleaf* l = &out[(*n)++]; snprintf(l->name, sizeof(l->name), "%s[0]", name); l->base = g->elem; l->count = g->n; l->slot = slot; l->size = el->size; }
        return;
    }
    if (*n < cap) { gleaf* l = &out[(*n)++]; snprintf(l->name, sizeof(l->name), "%s", name); l->base = ty; l->count = 1; l->slot = slot; l->size = g->size; }
}

typedef struct gprog_sh { gsh sh; gleaf leaves[256]; int nleaves; gleaf attrs[32]; int nattrs; gleaf varys[64]; int nvarys; } gshader;

static int compile_body(gshader* S, gctx* c, int stage);
static int compile_shader(gshader* S, int stage, const char* src) {
    memset(S, 0, sizeof(*S));
    gsh* sh = &S->sh; sh->stage = stage;
    gt_init();
    sbuf pp = { 0, 0, 0 };
    if (!preprocess(src, &pp, sh->log, sizeof(sh->log))) { nfree(pp.s); return 0; }
    gctx* c = nzalloc(sizeof(gctx)); c->named = nzalloc(GL_MEM / 8);
    c->sh = sh;
    lex(pp.s ? pp.s : "", &c->t, &c->nt);
    nfree(pp.s);
    int r = compile_body(S, c, stage);
    nfree(c->named); nfree(c->t); nfree(c);
    if (!r) { nfree(sh->mem); sh->mem = 0; nfree(sh->code); sh->code = 0; sh->ncode = 0; }
    return r;
}
static int compile_body(gshader* S, gctx* c, int stage) {
    gsh* sh = &S->sh;
    sh->mem = nzalloc(sizeof(float) * GL_MEM);
    c->ctop = GL_MEM;
    ast* top[1024]; int ntop = 0;
    while (!c->fail && PK(c)->k != TK_EOF) {
        if (isid(c, "precision")) { while (!isop(c, ";") && PK(c)->k != TK_EOF) c->p++; eat(c, ";"); continue; }
        if (eat(c, ";")) continue;
        if (!looks_decl(c) && !(PK(c)->k == TK_ID && (isid(c, "void") || type_by_name(PK(c)->s) >= 0))) { cerr(c, PK(c)->line, "syntax error", PK(c)->s); break; }
        ast* d = p_decl(c, 1);
        if (d && ntop < 1024) top[ntop++] = d;
    }
    if (c->fail) { snprintf(sh->log, sizeof(sh->log), "%s", c->err); return 0; }
    struct { const char* n; int ty; int* slot; int st; } BV[] = { { "gl_Position", TY_VEC4, &sh->pos_slot, 0 }, { "gl_PointSize", TY_FLOAT, &sh->psize_slot, 0 }, { "gl_FragCoord", TY_VEC4, &sh->fragcoord_slot, 1 }, { "gl_FrontFacing", TY_BOOL, &sh->frontface_slot, 1 }, { "gl_PointCoord", TY_VEC2, &sh->pointcoord_slot, 1 }, { "gl_FragColor", TY_VEC4, &sh->fragcolor_slot, 1 } };
    for (int i = 0; i < 6; i++) { if (BV[i].st != stage) continue; int sl = tmp(c, comps(BV[i].ty)); *BV[i].slot = sl; sym_add(c, BV[i].n, BV[i].ty, sl, ST_BUILTIN); }
    if (stage == 0) sh->mem[sh->psize_slot] = 1;
    if (stage == 1) sym_add(c, "gl_FragData", ty_array(TY_VEC4, 1), sh->fragcolor_slot, ST_BUILTIN);
    { struct { const char* n; float v; } K[] = { { "gl_MaxVertexAttribs", 16 }, { "gl_MaxVertexUniformVectors", 1024 }, { "gl_MaxVaryingVectors", 30 }, { "gl_MaxVertexTextureImageUnits", 16 }, { "gl_MaxCombinedTextureImageUnits", 32 }, { "gl_MaxTextureImageUnits", 16 }, { "gl_MaxFragmentUniformVectors", 1024 }, { "gl_MaxDrawBuffers", 1 } };
      for (int i = 0; i < 8; i++) { gsym* k = sym_add(c, K[i].n, TY_INT, 0, ST_BUILTIN); k->cval_ok = 1; k->cval = K[i].v; } }
    struct { char n[64]; int ty, slot, st; } G2[512]; int ng = 0;
    for (int i = 0; i < ntop && !c->fail; i++) {
        ast* d = top[i];
        if (d->k == A_DECL) { for (ast* v = d->a; v; v = v->next) { decl_var(c, v, 1); gsym* s2 = sym_find(c, v->name); if (s2 && ng < 512) { snprintf(G2[ng].n, 64, "%s", v->name); G2[ng].ty = s2->ty; G2[ng].slot = s2->slot; G2[ng].st = s2->storage; ng++; } } }
        else if (d->k == A_FUNC) {
            int aty[16]; for (int k = 0; k < d->nkids; k++) aty[k] = d->kids[k]->ty;
            gfunc* f = 0;
            for (gfunc* q = c->funcs; q; q = q->next) { if (strcmp(q->name, d->name) || q->np != d->nkids) continue; int same = 1; for (int k = 0; k < d->nkids; k++) if (q->pty[k] != aty[k]) same = 0; if (same) { f = q; break; } }
            if (!f) { f = nzalloc(sizeof(gfunc)); snprintf(f->name, sizeof(f->name), "%s", d->name); f->ret = d->ty; f->np = d->nkids; for (int k = 0; k < d->nkids; k++) { f->pty[k] = aty[k]; f->pq[k] = d->kids[k]->op; f->pslot[k] = tmp(c, GT[aty[k]].size); } f->rslot = tmp(c, GT[d->ty].size ? GT[d->ty].size : 1); mark_named(c, f->rslot, GT[d->ty].size ? GT[d->ty].size : 1); for (int k = 0; k < d->nkids; k++) mark_named(c, f->pslot[k], GT[aty[k]].size); f->entry = -1; f->next = c->funcs; c->funcs = f; }
            if (d->op == 1) { if (f->body) { cerr(c, d->line, "function already has a body", d->name); break; } f->body = d->a; f->params = d; }
        }
    }
    for (gfunc* f = c->funcs; f && !c->fail; f = f->next) {
        if (!f->body) continue;
        ast* d = f->params;
        if (c->tmpmax > sh->nmem) sh->nmem = c->tmpmax;
        f->entry = sh->ncode;
        c->curf = f; c->depth = 1;
        for (int k = 0; k < f->np; k++) if (d->kids[k]->name[0]) sym_add(c, d->kids[k]->name, f->pty[k], f->pslot[k], ST_LOCAL);
        cstmt(c, f->body);
        emit(c, I_RET, 0, 0, 0, 0, 0, 0);
        sym_pop(c, 0); c->depth = 0; c->curf = 0;
    }
    gfunc* mainf = 0;
    for (gfunc* f = c->funcs; f; f = f->next) { for (int k = 0; k < f->nfix; k++) sh->code[f->fix[k]].d = f->entry; if (!strcmp(f->name, "main") && f->np == 0) mainf = f; }
    for (gfunc* f = c->funcs; f && !c->fail; f = f->next) if (f->nfix && f->entry < 0) cerr(c, 0, "function has no body", f->name);
    if (!c->fail && (!mainf || !mainf->body)) cerr(c, 0, "missing main()", 0);
    if (c->fail) { snprintf(sh->log, sizeof(sh->log), "%s", c->err); return 0; }
    sh->main_entry = sh->ncode;
    for (int i = 0; i < c->nginit; i++) { int mark = sh->nmem; cexpr(c, c->ginit[i]); sh->nmem = mark; }
    emit(c, I_CALL, 0, 0, mainf->entry, 0, 0, 0);
    emit(c, I_END, 0, 0, 0, 0, 0, 0);
    if (c->fail) { snprintf(sh->log, sizeof(sh->log), "%s", c->err); return 0; }
    if (c->tmpmax > sh->nmem) sh->nmem = c->tmpmax;
    peep(c, sh);
    {
        int nm = sh->nmem, ct = c->ctop, delta = ct - nm, msz = nm + (GL_MEM - ct);
        if (delta > 0) {
            for (int i = 0; i < sh->ncode; i++) {
                ginst* I = &sh->code[i]; int op = I->op;
                int jmp = op == I_JMP || op == I_JZ || op == I_JNZ || op == I_CALL || op == I_RET || op == I_END || op == I_DISC;
                if (!jmp && I->d >= ct) I->d -= delta;
                if ((!jmp || op == I_JZ || op == I_JNZ) && I->a >= ct) I->a -= delta;
                if (I->b >= ct) I->b -= delta;
                if (op == I_BI && I->c >= ct) I->c -= delta;
            }
            float* m2 = nzalloc(sizeof(float) * (size_t)(msz + 4));
            memcpy(m2, sh->mem, sizeof(float) * (size_t)nm);
            memcpy(m2 + nm, sh->mem + ct, sizeof(float) * (size_t)(GL_MEM - ct));
            nfree(sh->mem); sh->mem = m2;
        }
        sh->msize = delta > 0 ? msz : GL_MEM;
    }
    for (int i = 0; i < ng; i++) {
        if (G2[i].st == ST_UNIFORM) flatten(sh, S->leaves, &S->nleaves, 256, G2[i].n, G2[i].ty, G2[i].slot);
        else if (G2[i].st == ST_ATTR) flatten(sh, S->attrs, &S->nattrs, 32, G2[i].n, G2[i].ty, G2[i].slot);
        else if (G2[i].st == ST_VARY) flatten(sh, S->varys, &S->nvarys, 64, G2[i].n, G2[i].ty, G2[i].slot);
    }
    sh->ok = 1;
    return 1;
}

typedef struct { const unsigned char* px; int w, h, minf, magf, ws, wt; } gtex;
static gtex g_tex[32];
static float wrapc(float t, int mode) { if (mode == 0x812F) { if (t < 0) return 0; if (t > 1) return 1; return t; } if (mode == 0x8370) { float f = t - 2.0f * floorf(t * 0.5f); return f > 1 ? 2 - f : f; } return t - floorf(t); }
static void texel(const gtex* t, int x, int y, float* o) {
    if (x < 0) x = 0; if (y < 0) y = 0; if (x >= t->w) x = t->w - 1; if (y >= t->h) y = t->h - 1;
    const unsigned char* p = t->px + ((size_t)y * t->w + x) * 4;
    o[0] = p[0] * (1.0f / 255); o[1] = p[1] * (1.0f / 255); o[2] = p[2] * (1.0f / 255); o[3] = p[3] * (1.0f / 255);
}
static void sample2d(int unit, float s, float t, float* out) {
    if (unit < 0 || unit >= 32 || !g_tex[unit].px || g_tex[unit].w <= 0) { out[0] = out[1] = out[2] = 0; out[3] = 1; return; }
    gtex* T = &g_tex[unit];
    s = wrapc(s, T->ws); t = wrapc(t, T->wt);
    if (T->magf == 0x2600) { int x = (int)(s * T->w), y = (int)(t * T->h); if (T->ws != 0x812F && x >= T->w) x = T->w - 1; texel(T, x, y, out); return; }
    float fx = s * T->w - 0.5f, fy = t * T->h - 0.5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy); float ax = fx - x0, ay = fy - y0;
    int x1 = x0 + 1, y1 = y0 + 1;
    if (T->ws != 0x812F) { x0 = ((x0 % T->w) + T->w) % T->w; x1 = ((x1 % T->w) + T->w) % T->w; }
    if (T->wt != 0x812F) { y0 = ((y0 % T->h) + T->h) % T->h; y1 = ((y1 % T->h) + T->h) % T->h; }
    float a[4], b[4], c2[4], d[4];
    texel(T, x0, y0, a); texel(T, x1, y0, b); texel(T, x0, y1, c2); texel(T, x1, y1, d);
    for (int i = 0; i < 4; i++) out[i] = (a[i] * (1 - ax) + b[i] * ax) * (1 - ay) + (c2[i] * (1 - ax) + d[i] * ax) * ay;
}

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static int run_sh(gsh* sh, int start) {
    float* M = sh->mem;
    ginst* code = sh->code;
    int pc = start, csp = 0, cs[64];
    long guard = 0;
    for (;;) {
        if (++guard > 4000000) return 2;
        ginst* I = &code[pc++];
        float* d = M + I->d; const float* a = M + I->a; const float* b = M + I->b; int n = I->n;
        if (n == 1) {
            switch (I->op) {
            case I_MOV: case I_MOVS: d[0] = a[0]; continue;
            case I_ADD: d[0] = a[0] + b[0]; continue;
            case I_SUB: d[0] = a[0] - b[0]; continue;
            case I_MUL: d[0] = a[0] * b[0]; continue;
            case I_LT: d[0] = a[0] < b[0]; continue;
            case I_GT: d[0] = a[0] > b[0]; continue;
            default: break;
            }
        }
        switch (I->op) {
        case I_MOV: if (d != a) { if (d + n <= a || a + n <= d) for (int i = 0; i < n; i++) d[i] = a[i]; else memmove(d, a, sizeof(float) * (size_t)n); } break;
        case I_MOVS: { float v = a[0]; for (int i = 0; i < n; i++) d[i] = v; break; }
        case I_ADD: { int sa = (I->aux & 1) ? 0 : 1, sb = (I->aux & 2) ? 0 : 1; for (int i = 0; i < n; i++) d[i] = a[i * sa] + b[i * sb]; break; }
        case I_SUB: { int sa = (I->aux & 1) ? 0 : 1, sb = (I->aux & 2) ? 0 : 1; for (int i = 0; i < n; i++) d[i] = a[i * sa] - b[i * sb]; break; }
        case I_MUL: { int sa = (I->aux & 1) ? 0 : 1, sb = (I->aux & 2) ? 0 : 1; for (int i = 0; i < n; i++) d[i] = a[i * sa] * b[i * sb]; break; }
        case I_DIV: case I_MOD: {
            int as = I->aux & 1, bs = I->aux & 2, ii = I->aux & 4;
            for (int i = 0; i < n; i++) { float x = a[as ? 0 : i], y = b[bs ? 0 : i], r;
                if (I->op == I_DIV) { r = y != 0 ? x / y : (x == 0 ? 0 : (x > 0 ? 3.4e38f : -3.4e38f)); if (ii) r = (float)(int)r; } else r = y != 0 ? x - y * floorf(x / y) : 0;
                d[i] = r; }
            break;
        }
        case I_NEG: for (int i = 0; i < n; i++) d[i] = -a[i]; break;
        case I_NOT: for (int i = 0; i < n; i++) d[i] = a[i] == 0 ? 1.0f : 0.0f; break;
        case I_MVM: { float t[4]; for (int r = 0; r < n; r++) { float s = 0; for (int k = 0; k < n; k++) s += a[k * n + r] * b[k]; t[r] = s; } memcpy(d, t, sizeof(float) * (size_t)n); break; }
        case I_VMM: { float t[4]; for (int cl = 0; cl < n; cl++) { float s = 0; for (int k = 0; k < n; k++) s += a[k] * b[cl * n + k]; t[cl] = s; } memcpy(d, t, sizeof(float) * (size_t)n); break; }
        case I_MMM: { float t[16]; for (int cl = 0; cl < n; cl++) for (int r = 0; r < n; r++) { float s = 0; for (int k = 0; k < n; k++) s += a[k * n + r] * b[cl * n + k]; t[cl * n + r] = s; } memcpy(d, t, sizeof(float) * (size_t)(n * n)); break; }
        case I_LT: d[0] = a[0] < b[0]; break;
        case I_LE: d[0] = a[0] <= b[0]; break;
        case I_GT: d[0] = a[0] > b[0]; break;
        case I_GE: d[0] = a[0] >= b[0]; break;
        case I_EQ: case I_NE: { int eq = 1; for (int i = 0; i < n; i++) if (a[i] != b[i]) eq = 0; d[0] = (float)(I->op == I_EQ ? eq : !eq); break; }
        case I_AND: d[0] = (a[0] != 0) && (b[0] != 0); break;
        case I_OR: d[0] = (a[0] != 0) || (b[0] != 0); break;
        case I_XOR: d[0] = (a[0] != 0) != (b[0] != 0); break;
        case I_TOINT: for (int i = 0; i < n; i++) d[i] = (float)(int)a[i]; break;
        case I_TOBOOL: for (int i = 0; i < n; i++) d[i] = a[i] != 0 ? 1.0f : 0.0f; break;
        case I_SWZ: { float t[4]; for (int i = 0; i < n; i++) t[i] = a[(I->aux >> (2 * i)) & 3]; memcpy(d, t, sizeof(float) * (size_t)n); break; }
        case I_SWZW: { float t[4]; memcpy(t, a, sizeof(float) * (size_t)n); for (int i = 0; i < n; i++) d[(I->aux >> (2 * i)) & 3] = t[i]; break; }
        case I_IDXR: { int ix = (int)b[0]; if (ix < 0) ix = 0; long addr = (long)I->a + (long)ix * I->aux + I->c; if (addr < 0 || addr + n > sh->msize) addr = I->a; memmove(d, M + addr, sizeof(float) * (size_t)n); break; }
        case I_IDXW: { int ix = (int)b[0]; if (ix < 0) ix = 0; long addr = (long)I->d + (long)ix * I->aux + I->c; if (addr < 0 || addr + n > sh->msize) break; memmove(M + addr, a, sizeof(float) * (size_t)n); break; }
        case I_JMP: pc = I->d; break;
        case I_JZ: if (a[0] == 0) pc = I->d; break;
        case I_JNZ: if (a[0] != 0) pc = I->d; break;
        case I_CALL: if (csp >= 64) return 2; cs[csp++] = pc; pc = I->d; break;
        case I_RET: if (!csp) return 0; pc = cs[--csp]; break;
        case I_END: return 0;
        case I_DISC: return 1;
        case I_TEX: { float o[4] = { 0, 0, 0, 1 }; if (I->aux == 0) sample2d((int)a[0], b[0], b[1], o); else if (I->aux == 2 || I->aux == 3) { int u = (int)a[0]; if (u >= 0 && u < 32 && g_tex[u].px) { if (I->aux == 2) texel(&g_tex[u], (int)b[0], (int)b[1], o); else { o[0] = (float)g_tex[u].w; o[1] = (float)g_tex[u].h; o[2] = o[3] = 0; } } else if (I->aux == 3) o[3] = 0; } memcpy(d, o, sizeof(o)); break; }
        case I_BI: {
            const float* cc = M + I->c;
            switch (I->aux) {
            case B_RADIANS: for (int i = 0; i < n; i++) d[i] = a[i] * 0.017453292519943295f; break;
            case B_DEGREES: for (int i = 0; i < n; i++) d[i] = a[i] * 57.29577951308232f; break;
            case B_SIN: for (int i = 0; i < n; i++) d[i] = sinf(a[i]); break;
            case B_COS: for (int i = 0; i < n; i++) d[i] = cosf(a[i]); break;
            case B_TAN: for (int i = 0; i < n; i++) d[i] = tanf(a[i]); break;
            case B_ASIN: for (int i = 0; i < n; i++) d[i] = asinf(clampf(a[i], -1, 1)); break;
            case B_ACOS: for (int i = 0; i < n; i++) d[i] = acosf(clampf(a[i], -1, 1)); break;
            case B_ATAN: for (int i = 0; i < n; i++) d[i] = atanf(a[i]); break;
            case B_ATAN2: for (int i = 0; i < n; i++) d[i] = atan2f(a[i], b[i]); break;
            case B_POW: for (int i = 0; i < n; i++) d[i] = powf(a[i], b[i]); break;
            case B_EXP: for (int i = 0; i < n; i++) d[i] = expf(a[i]); break;
            case B_LOG: for (int i = 0; i < n; i++) d[i] = logf(a[i]); break;
            case B_EXP2: for (int i = 0; i < n; i++) d[i] = exp2f(a[i]); break;
            case B_LOG2: for (int i = 0; i < n; i++) d[i] = log2f(a[i]); break;
            case B_SQRT: for (int i = 0; i < n; i++) d[i] = sqrtf(a[i] > 0 ? a[i] : 0); break;
            case B_ISQRT: for (int i = 0; i < n; i++) d[i] = a[i] > 0 ? 1.0f / sqrtf(a[i]) : 3.4e38f; break;
            case B_ABS: for (int i = 0; i < n; i++) d[i] = fabsf(a[i]); break;
            case B_SIGN: for (int i = 0; i < n; i++) d[i] = a[i] > 0 ? 1.0f : a[i] < 0 ? -1.0f : 0.0f; break;
            case B_FLOOR: for (int i = 0; i < n; i++) d[i] = floorf(a[i]); break;
            case B_CEIL: for (int i = 0; i < n; i++) d[i] = ceilf(a[i]); break;
            case B_FRACT: for (int i = 0; i < n; i++) d[i] = a[i] - floorf(a[i]); break;
            case B_MOD: for (int i = 0; i < n; i++) d[i] = b[i] != 0 ? a[i] - b[i] * floorf(a[i] / b[i]) : 0; break;
            case B_MIN: for (int i = 0; i < n; i++) d[i] = a[i] < b[i] ? a[i] : b[i]; break;
            case B_MAX: for (int i = 0; i < n; i++) d[i] = a[i] > b[i] ? a[i] : b[i]; break;
            case B_CLAMP: for (int i = 0; i < n; i++) { float v = a[i] > b[i] ? a[i] : b[i]; d[i] = v < cc[i] ? v : cc[i]; } break;
            case B_MIX: for (int i = 0; i < n; i++) d[i] = a[i] * (1 - cc[i]) + b[i] * cc[i]; break;
            case B_STEP: for (int i = 0; i < n; i++) d[i] = b[i] < a[i] ? 0.0f : 1.0f; break;
            case B_SSTEP: for (int i = 0; i < n; i++) { float e = b[i] - a[i]; float t = e != 0 ? clampf((cc[i] - a[i]) / e, 0, 1) : (cc[i] < a[i] ? 0 : 1); d[i] = t * t * (3 - 2 * t); } break;
            case B_LENGTH: { float s = 0; for (int i = 0; i < n; i++) s += a[i] * a[i]; d[0] = sqrtf(s); break; }
            case B_DIST: { float s = 0; for (int i = 0; i < n; i++) { float q = a[i] - b[i]; s += q * q; } d[0] = sqrtf(s); break; }
            case B_DOT: { float s = 0; for (int i = 0; i < n; i++) s += a[i] * b[i]; d[0] = s; break; }
            case B_CROSS: { float t[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }; memcpy(d, t, sizeof(t)); break; }
            case B_NORM: { float s = 0; for (int i = 0; i < n; i++) s += a[i] * a[i]; s = s > 0 ? 1.0f / sqrtf(s) : 0; for (int i = 0; i < n; i++) d[i] = a[i] * s; break; }
            case B_FFWD: { float s = 0; for (int i = 0; i < n; i++) s += cc[i] * b[i]; for (int i = 0; i < n; i++) d[i] = s < 0 ? a[i] : -a[i]; break; }
            case B_REFL: { float s = 0; for (int i = 0; i < n; i++) s += b[i] * a[i]; float t[4]; for (int i = 0; i < n; i++) t[i] = a[i] - 2 * s * b[i]; memcpy(d, t, sizeof(float) * (size_t)n); break; }
            case B_REFR: { float s = 0; for (int i = 0; i < n; i++) s += b[i] * a[i]; float eta = cc[0]; float k = 1 - eta * eta * (1 - s * s); float t[4]; for (int i = 0; i < n; i++) t[i] = k < 0 ? 0 : eta * a[i] - (eta * s + sqrtf(k)) * b[i]; memcpy(d, t, sizeof(float) * (size_t)n); break; }
            case B_LT: for (int i = 0; i < n; i++) d[i] = a[i] < b[i]; break;
            case B_LE: for (int i = 0; i < n; i++) d[i] = a[i] <= b[i]; break;
            case B_GT: for (int i = 0; i < n; i++) d[i] = a[i] > b[i]; break;
            case B_GE: for (int i = 0; i < n; i++) d[i] = a[i] >= b[i]; break;
            case B_EQ: for (int i = 0; i < n; i++) d[i] = a[i] == b[i]; break;
            case B_NE: for (int i = 0; i < n; i++) d[i] = a[i] != b[i]; break;
            case B_ANY: { int r = 0; for (int i = 0; i < n; i++) if (a[i] != 0) r = 1; d[0] = (float)r; break; }
            case B_ALL: { int r = 1; for (int i = 0; i < n; i++) if (a[i] == 0) r = 0; d[0] = (float)r; break; }
            case B_NOTV: for (int i = 0; i < n; i++) d[i] = a[i] == 0; break;
            case B_TEXP: { float w = a[n - 1]; if (w == 0) w = 1; d[0] = a[0] / w; d[1] = a[1] / w; break; }
            case B_DERIV: for (int i = 0; i < n; i++) d[i] = 0; break;
            case B_ROUND: for (int i = 0; i < n; i++) d[i] = floorf(a[i] + 0.5f); break;
            case B_TRUNC: for (int i = 0; i < n; i++) d[i] = (float)(int)a[i]; break;
            }
            break;
        }
        default: return 2;
        }
    }
}

#define GP_N 64
#define GS_N 128
#define MAXVF 124
typedef struct { int used; gshader* vs; gshader* fs; int nattr; struct { char name[96]; int vslot, base, loc; } attr[16];
    int nuni; struct { char name[96]; int base, count, vslot, fslot, size; } uni[256];
    int nvar; struct { int vslot, fslot, n, off; } var[64]; int nvf; } gprog;
static gprog* g_prog[GP_N];
static gshader* g_shd[GS_N];
static void shd_free(gshader* S) { if (!S) return; nfree(S->sh.mem); nfree(S->sh.code); nfree(S); }
static gshader* shd_clone(const gshader* S) {
    gshader* C = nalloc(sizeof(gshader)); memcpy(C, S, sizeof(gshader));
    C->sh.mem = nalloc(sizeof(float) * (size_t)(S->sh.msize + 4)); memcpy(C->sh.mem, S->sh.mem, sizeof(float) * (size_t)S->sh.msize);
    C->sh.code = nalloc(sizeof(ginst) * (size_t)(S->sh.ncode + 1)); memcpy(C->sh.code, S->sh.code, sizeof(ginst) * (size_t)S->sh.ncode); C->sh.ccode = S->sh.ncode + 1;
    return C;
}
static void prog_free(gprog* P) { if (!P) return; shd_free(P->vs); shd_free(P->fs); nfree(P); }
void webgl_reset(void) { for (int i = 0; i < GP_N; i++) g_prog[i] = 0; for (int i = 0; i < GS_N; i++) g_shd[i] = 0; }

static int glenum_of(int base, int* sz) {
    *sz = 1;
    switch (base) {
    case TY_FLOAT: return 0x1406; case TY_VEC2: return 0x8B50; case TY_VEC3: return 0x8B51; case TY_VEC4: return 0x8B52;
    case TY_INT: return 0x1404; case TY_IVEC2: return 0x8B53; case TY_IVEC3: return 0x8B54; case TY_IVEC4: return 0x8B55;
    case TY_BOOL: return 0x8B56; case TY_BVEC2: return 0x8B57; case TY_BVEC3: return 0x8B58; case TY_BVEC4: return 0x8B59;
    case TY_MAT2: return 0x8B5A; case TY_MAT3: return 0x8B5B; case TY_MAT4: return 0x8B5C; case TY_S2D: return 0x8B5E; case TY_SCUBE: return 0x8B60;
    }
    return 0x1406;
}

static JSValue gl_compile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    int32_t stage = 0; if (argc > 0) JS_ToInt32(ctx, &stage, argv[0]);
    const char* src = argc > 1 ? JS_ToCString(ctx, argv[1]) : 0;
    JSValue r = JS_NewArray(ctx);
    int slot = -1; for (int i = 0; i < GS_N; i++) if (!g_shd[i]) { slot = i; break; }
    if (!src || slot < 0) { JS_SetPropertyUint32(ctx, r, 0, JS_NewInt32(ctx, -1)); JS_SetPropertyUint32(ctx, r, 1, JS_NewString(ctx, "ERROR: out of shader slots")); if (src) JS_FreeCString(ctx, src); return r; }
    int pv = pool_suspend();
    gshader* S = nalloc(sizeof(gshader));
    int ok = compile_shader(S, stage == 0x8B31 ? 0 : 1, src);
    pool_resume(pv);
    JS_FreeCString(ctx, src);
    if (ok) g_shd[slot] = S;
    JS_SetPropertyUint32(ctx, r, 0, JS_NewInt32(ctx, ok ? slot : -1));
    JS_SetPropertyUint32(ctx, r, 1, JS_NewString(ctx, ok ? "" : S->sh.log));
    if (!ok) nfree(S);
    return r;
}
static JSValue gl_delshader(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { (void)this_val; int32_t i = -1; if (argc > 0) JS_ToInt32(ctx, &i, argv[0]); if (i >= 0 && i < GS_N && g_shd[i]) { int pv = pool_suspend(); shd_free(g_shd[i]); pool_resume(pv); g_shd[i] = 0; } return JS_UNDEFINED; }
static JSValue gl_link(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    int32_t vi = -1, fi = -1; if (argc > 0) JS_ToInt32(ctx, &vi, argv[0]); if (argc > 1) JS_ToInt32(ctx, &fi, argv[1]);
    JSValue r = JS_NewArray(ctx);
    char log[512] = { 0 };
    gshader* vs = vi >= 0 && vi < GS_N ? g_shd[vi] : 0; gshader* fs = fi >= 0 && fi < GS_N ? g_shd[fi] : 0;
    int slot = -1; for (int i = 0; i < GP_N; i++) if (!g_prog[i]) { slot = i; break; }
    if (!vs || !fs || vs->sh.stage != 0 || fs->sh.stage != 1) snprintf(log, sizeof(log), "ERROR: a vertex and a fragment shader must be attached and compiled");
    else if (slot < 0) snprintf(log, sizeof(log), "ERROR: too many programs");
    gprog* P = 0;
    if (!log[0]) {
        int pv = pool_suspend(); P = nzalloc(sizeof(gprog)); pool_resume(pv);
        P->vs = vs; P->fs = fs;
        int off = 0;
        for (int i = 0; i < fs->nvarys && !log[0]; i++) {
            gleaf* fv = &fs->varys[i]; gleaf* vv = 0;
            for (int k = 0; k < vs->nvarys; k++) if (!strcmp(vs->varys[k].name, fv->name)) vv = &vs->varys[k];
            if (!vv) { snprintf(log, sizeof(log), "ERROR: Varying %s is not declared in the vertex shader", fv->name); break; }
            if (vv->base != fv->base || vv->count != fv->count) { snprintf(log, sizeof(log), "ERROR: Varyings with the same name but different type, or statically used varyings in fragment shader are not declared in vertex shader: %s", fv->name); break; }
            int n = fv->size * fv->count;
            if (off + n > MAXVF) { snprintf(log, sizeof(log), "ERROR: too many varyings"); break; }
            if (P->nvar < 64) { P->var[P->nvar].vslot = vv->slot; P->var[P->nvar].fslot = fv->slot; P->var[P->nvar].n = n; P->var[P->nvar].off = off; P->nvar++; }
            off += n;
        }
        P->nvf = off;
        int used[64] = { 0 };
        JSValue binds = argc > 2 ? argv[2] : JS_UNDEFINED;
        for (int i = 0; i < vs->nattrs && i < 16; i++) {
            snprintf(P->attr[i].name, sizeof(P->attr[i].name), "%s", vs->attrs[i].name); P->attr[i].vslot = vs->attrs[i].slot; P->attr[i].base = vs->attrs[i].base; P->attr[i].loc = -1;
            if (JS_IsObject(binds)) { JSValue b = JS_GetPropertyStr(ctx, binds, vs->attrs[i].name); if (JS_IsNumber(b)) { int32_t l = -1; JS_ToInt32(ctx, &l, b); if (l >= 0 && l < 16) { P->attr[i].loc = l; used[l] = 1; } } JS_FreeValue(ctx, b); }
        }
        P->nattr = vs->nattrs < 16 ? vs->nattrs : 16;
        for (int i = 0; i < P->nattr; i++) if (P->attr[i].loc < 0) { int l = 0; while (l < 16 && used[l]) l++; if (l >= 16) { snprintf(log, sizeof(log), "ERROR: too many attributes"); break; } P->attr[i].loc = l; used[l] = 1; }
        for (int st = 0; st < 2 && !log[0]; st++) {
            gshader* S = st ? fs : vs;
            for (int i = 0; i < S->nleaves; i++) {
                gleaf* L = &S->leaves[i]; int k = -1;
                for (int q = 0; q < P->nuni; q++) if (!strcmp(P->uni[q].name, L->name)) k = q;
                if (k >= 0) { if (P->uni[k].base != L->base || P->uni[k].count != L->count) { snprintf(log, sizeof(log), "ERROR: Uniform %s has different types in the two shaders", L->name); break; } }
                else { if (P->nuni >= 256) break; k = P->nuni++; snprintf(P->uni[k].name, sizeof(P->uni[k].name), "%s", L->name); P->uni[k].base = L->base; P->uni[k].count = L->count; P->uni[k].size = L->size; P->uni[k].vslot = -1; P->uni[k].fslot = -1; }
                if (st) P->uni[k].fslot = L->slot; else P->uni[k].vslot = L->slot;
            }
        }
    }
    if (log[0] || !P) { if (P) { int pv = pool_suspend(); nfree(P); pool_resume(pv); } JS_SetPropertyUint32(ctx, r, 0, JS_NewInt32(ctx, -1)); JS_SetPropertyUint32(ctx, r, 1, JS_NewString(ctx, log)); return r; }
    { int pv = pool_suspend(); P->vs = shd_clone(vs); P->fs = shd_clone(fs); pool_resume(pv); }
    g_prog[slot] = P;
    JS_SetPropertyUint32(ctx, r, 0, JS_NewInt32(ctx, slot));
    JS_SetPropertyUint32(ctx, r, 1, JS_NewString(ctx, ""));
    JSValue at = JS_NewArray(ctx);
    for (int i = 0; i < P->nattr; i++) { int sz; JSValue e = JS_NewArray(ctx); JS_SetPropertyUint32(ctx, e, 0, JS_NewString(ctx, P->attr[i].name)); JS_SetPropertyUint32(ctx, e, 1, JS_NewInt32(ctx, glenum_of(P->attr[i].base, &sz))); JS_SetPropertyUint32(ctx, e, 2, JS_NewInt32(ctx, 1)); JS_SetPropertyUint32(ctx, e, 3, JS_NewInt32(ctx, P->attr[i].loc)); JS_SetPropertyUint32(ctx, at, (uint32_t)i, e); }
    JS_SetPropertyUint32(ctx, r, 2, at);
    JSValue un = JS_NewArray(ctx);
    for (int i = 0; i < P->nuni; i++) { int sz; JSValue e = JS_NewArray(ctx); JS_SetPropertyUint32(ctx, e, 0, JS_NewString(ctx, P->uni[i].name)); JS_SetPropertyUint32(ctx, e, 1, JS_NewInt32(ctx, glenum_of(P->uni[i].base, &sz))); JS_SetPropertyUint32(ctx, e, 2, JS_NewInt32(ctx, P->uni[i].count)); JS_SetPropertyUint32(ctx, e, 3, JS_NewInt32(ctx, P->uni[i].size)); JS_SetPropertyUint32(ctx, un, (uint32_t)i, e); }
    JS_SetPropertyUint32(ctx, r, 3, un);
    return r;
}
static JSValue gl_delprog(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { (void)this_val; int32_t i = -1; if (argc > 0) JS_ToInt32(ctx, &i, argv[0]); if (i >= 0 && i < GP_N && g_prog[i]) { int pv = pool_suspend(); prog_free(g_prog[i]); pool_resume(pv); g_prog[i] = 0; } return JS_UNDEFINED; }
static JSValue gl_uniform(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    int32_t pi = -1, ui = -1, eoff = 0; if (argc > 0) JS_ToInt32(ctx, &pi, argv[0]); if (argc > 1) JS_ToInt32(ctx, &ui, argv[1]); if (argc > 3) JS_ToInt32(ctx, &eoff, argv[3]);
    gprog* P = pi >= 0 && pi < GP_N ? g_prog[pi] : 0;
    if (!P || ui < 0 || ui >= P->nuni || argc < 3) return JS_FALSE;
    size_t bo = 0, bl = 0, bpe = 0; JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[2], &bo, &bl, &bpe);
    if (JS_IsException(ab)) { JS_FreeValue(ctx, JS_GetException(ctx)); return JS_FALSE; }
    size_t abl = 0; uint8_t* p = JS_GetArrayBuffer(ctx, &abl, ab); JS_FreeValue(ctx, ab);
    if (!p || bpe != 4) return JS_FALSE;
    const float* v = (const float*)(p + bo); int n = (int)(bl / 4);
    int maxn = P->uni[ui].size * (P->uni[ui].count - eoff); if (n > maxn) n = maxn; if (n <= 0) return JS_FALSE;
    int base = eoff * P->uni[ui].size;
    if (P->uni[ui].vslot >= 0) memcpy(P->vs->sh.mem + P->uni[ui].vslot + base, v, sizeof(float) * (size_t)n);
    if (P->uni[ui].fslot >= 0) memcpy(P->fs->sh.mem + P->uni[ui].fslot + base, v, sizeof(float) * (size_t)n);
    return JS_TRUE;
}

typedef struct { int w, h; unsigned char* col; float* dep; } gtarget;
typedef struct { float vx, vy, vw, vh; int dtest, dfunc, dmask, blend, srgb, drgb, sa, da, eqr, eqa, cull, cullface, front, scis, sx, sy, sw, sh, mr, mg, mb, ma; float bc[4]; float lw, pof, pou; int pofill; float dn, df; } gstate;
static gstate GS;
static gtarget GTG;
static int dtest(int f, float z, float d) { switch (f) { case 0x0200: return 0; case 0x0201: return z < d; case 0x0202: return z == d; case 0x0203: return z <= d; case 0x0204: return z > d; case 0x0205: return z != d; case 0x0206: return z >= d; default: return 1; } }
static float bfac(int f, const float* s, const float* d, int ch) {
    switch (f) {
    case 0: return 0; case 1: return 1;
    case 0x0300: return s[ch]; case 0x0301: return 1 - s[ch]; case 0x0302: return s[3]; case 0x0303: return 1 - s[3];
    case 0x0304: return d[3]; case 0x0305: return 1 - d[3]; case 0x0306: return d[ch]; case 0x0307: return 1 - d[ch];
    case 0x0308: { if (ch == 3) return 1; float m = 1 - d[3]; return s[3] < m ? s[3] : m; }
    case 0x8001: return GS.bc[ch]; case 0x8002: return 1 - GS.bc[ch]; case 0x8003: return GS.bc[3]; case 0x8004: return 1 - GS.bc[3];
    }
    return 1;
}
static void put_frag(int x, int y, const float* c) {
    unsigned char* p = GTG.col + ((size_t)y * GTG.w + x) * 4;
    float s[4], d[4], o[4];
    for (int i = 0; i < 4; i++) s[i] = clampf(c[i], 0, 1);
    if (GS.blend) {
        for (int i = 0; i < 4; i++) d[i] = p[i] * (1.0f / 255);
        for (int i = 0; i < 4; i++) {
            int fs = i < 3 ? GS.srgb : GS.sa, fd = i < 3 ? GS.drgb : GS.da, eq = i < 3 ? GS.eqr : GS.eqa;
            float a = s[i] * bfac(fs, s, d, i), b = d[i] * bfac(fd, s, d, i);
            o[i] = eq == 0x800A ? a - b : eq == 0x800B ? b - a : eq == 0x8007 ? (s[i] < d[i] ? s[i] : d[i]) : eq == 0x8008 ? (s[i] > d[i] ? s[i] : d[i]) : a + b;
        }
    } else memcpy(o, s, sizeof(o));
    if (GS.mr) p[0] = (unsigned char)(clampf(o[0], 0, 1) * 255 + 0.5f);
    if (GS.mg) p[1] = (unsigned char)(clampf(o[1], 0, 1) * 255 + 0.5f);
    if (GS.mb) p[2] = (unsigned char)(clampf(o[2], 0, 1) * 255 + 0.5f);
    if (GS.ma) p[3] = (unsigned char)(clampf(o[3], 0, 1) * 255 + 0.5f);
}
typedef struct { float x, y, z, w; float* v; float ps; } gvx;
static gprog* GP;
static int g_rect[4];
static long g_frags;
static int g_gl_stop;
extern int nwk_busy(void);
static void shade(int x, int y, float z, float iw, const float* var, int front, float pcx, float pcy) {
    if (z < 0 || z > 1) return;
    if (GTG.dep && GS.dtest) { float* dp = GTG.dep + (size_t)y * GTG.w + x; if (!dtest(GS.dfunc, z, *dp)) return; }
    gsh* fs = &GP->fs->sh;
    float* M = fs->mem;
    for (int i = 0; i < GP->nvar; i++) memcpy(M + GP->var[i].fslot, var + GP->var[i].off, sizeof(float) * (size_t)GP->var[i].n);
    M[fs->fragcoord_slot] = x + 0.5f; M[fs->fragcoord_slot + 1] = y + 0.5f; M[fs->fragcoord_slot + 2] = z; M[fs->fragcoord_slot + 3] = iw;
    M[fs->frontface_slot] = (float)front;
    M[fs->pointcoord_slot] = pcx; M[fs->pointcoord_slot + 1] = pcy;
    if (g_gl_stop) return;
    if ((++g_frags & 8191) == 0 && nwk_busy()) { g_gl_stop = 1; return; }
    if (run_sh(fs, fs->main_entry)) return;
    if (GTG.dep && GS.dtest && GS.dmask) GTG.dep[(size_t)y * GTG.w + x] = z;
    put_frag(x, y, M + fs->fragcolor_slot);
}
static void raster_tri(gvx* a, gvx* b, gvx* c) {
    float ar = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
    if (ar == 0 || ar != ar) return;
    int ccw = ar > 0;
    int front = GS.front == 0x0901 ? !ccw : ccw;
    if (GS.cull) { if (GS.cullface == 0x0408) return; if (GS.cullface == 0x0404 && front) return; if (GS.cullface == 0x0405 && !front) return; }
    float minx = a->x, maxx = a->x, miny = a->y, maxy = a->y;
    if (b->x < minx) minx = b->x; if (c->x < minx) minx = c->x; if (b->x > maxx) maxx = b->x; if (c->x > maxx) maxx = c->x;
    if (b->y < miny) miny = b->y; if (c->y < miny) miny = c->y; if (b->y > maxy) maxy = b->y; if (c->y > maxy) maxy = c->y;
    int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    if (x0 < g_rect[0]) x0 = g_rect[0]; if (y0 < g_rect[1]) y0 = g_rect[1]; if (x1 > g_rect[2]) x1 = g_rect[2]; if (y1 > g_rect[3]) y1 = g_rect[3];
    if (x0 >= x1 || y0 >= y1) return;
    float inv = 1.0f / ar;
    int nv = GP->nvf;
    float var[MAXVF + 4];
    float dz = 0;
    if (GS.pofill) { float zx = ((b->z - a->z) * (c->y - a->y) - (c->z - a->z) * (b->y - a->y)) * inv, zy = ((c->z - a->z) * (b->x - a->x) - (b->z - a->z) * (c->x - a->x)) * inv; float m = fabsf(zx) > fabsf(zy) ? fabsf(zx) : fabsf(zy); dz = m * GS.pof + GS.pou * (1.0f / 16777216.0f); }
    for (int y = y0; y < y1; y++) {
        float py = y + 0.5f;
        for (int x = x0; x < x1; x++) {
            float px = x + 0.5f;
            float w0 = ((b->x - px) * (c->y - py) - (c->x - px) * (b->y - py)) * inv;
            float w1 = ((c->x - px) * (a->y - py) - (a->x - px) * (c->y - py)) * inv;
            float w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) {
                if (w0 < -1e-6f || w1 < -1e-6f || w2 < -1e-6f) continue;
            }
            float z = w0 * a->z + w1 * b->z + w2 * c->z + dz;
            float iw = w0 * a->w + w1 * b->w + w2 * c->w;
            if (iw <= 0) continue;
            float r = 1.0f / iw;
            for (int i = 0; i < nv; i++) var[i] = (w0 * a->v[i] + w1 * b->v[i] + w2 * c->v[i]) * r;
            shade(x, y, z, iw, var, front, 0, 0);
        }
    }
}
static void to_screen(const float* cv, int nv, gvx* o, float* vbuf) {
    float iw = 1.0f / cv[3];
    o->x = GS.vx + (cv[0] * iw + 1) * 0.5f * GS.vw;
    o->y = GS.vy + (cv[1] * iw + 1) * 0.5f * GS.vh;
    o->z = GS.dn + (cv[2] * iw + 1) * 0.5f * (GS.df - GS.dn);
    o->w = iw;
    o->ps = cv[4];
    o->v = vbuf;
    for (int i = 0; i < nv; i++) vbuf[i] = cv[5 + i] * iw;
}
static void tri_clip(const float* A, const float* B, const float* C, int stride) {
    const float* in[3] = { A, B, C };
    float buf[6][5 + MAXVF]; int n = 0;
    int nv = stride - 5;
    for (int i = 0; i < 3; i++) {
        const float* p = in[i]; const float* q = in[(i + 1) % 3];
        float dp = p[2] + p[3], dq = q[2] + q[3];
        if (dp >= 0) { memcpy(buf[n++], p, sizeof(float) * (size_t)stride); }
        if ((dp >= 0) != (dq >= 0)) { float t = dp / (dp - dq); for (int k = 0; k < stride; k++) buf[n][k] = p[k] + (q[k] - p[k]) * t; n++; }
    }
    if (n < 3) return;
    for (int i = 0; i < n; i++) if (buf[i][3] <= 1e-7f) buf[i][3] = 1e-7f;
    float vb[6][MAXVF + 4]; gvx v[6];
    for (int i = 0; i < n; i++) to_screen(buf[i], nv, &v[i], vb[i]);
    for (int i = 1; i + 1 < n; i++) raster_tri(&v[0], &v[i], &v[i + 1]);
}
static void raster_line(const float* A, const float* B, int stride) {
    float a[5 + MAXVF], b[5 + MAXVF]; memcpy(a, A, sizeof(float) * (size_t)stride); memcpy(b, B, sizeof(float) * (size_t)stride);
    float da = a[2] + a[3], db = b[2] + b[3];
    if (da < 0 && db < 0) return;
    if (da < 0) { float t = da / (da - db); for (int k = 0; k < stride; k++) a[k] = a[k] + (b[k] - a[k]) * t; }
    else if (db < 0) { float t = db / (db - da); for (int k = 0; k < stride; k++) b[k] = b[k] + (a[k] - b[k]) * t; }
    int nv = stride - 5;
    float va[MAXVF + 4], vbb[MAXVF + 4]; gvx p, q;
    if (a[3] <= 1e-7f) a[3] = 1e-7f; if (b[3] <= 1e-7f) b[3] = 1e-7f;
    to_screen(a, nv, &p, va); to_screen(b, nv, &q, vbb);
    float dx = q.x - p.x, dy = q.y - p.y; int steps = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy)); if (steps < 1) steps = 1; if (steps > 16384) steps = 16384;
    float var[MAXVF + 4];
    for (int s = 0; s <= steps; s++) {
        float t = (float)s / steps;
        int x = (int)floorf(p.x + dx * t), y = (int)floorf(p.y + dy * t);
        if (x < g_rect[0] || y < g_rect[1] || x >= g_rect[2] || y >= g_rect[3]) continue;
        float iw = p.w + (q.w - p.w) * t; if (iw <= 0) continue; float r = 1.0f / iw;
        for (int i = 0; i < nv; i++) var[i] = (p.v[i] + (q.v[i] - p.v[i]) * t) * r;
        shade(x, y, p.z + (q.z - p.z) * t, iw, var, 1, 0, 0);
    }
}
static void raster_point(const float* A, int stride) {
    if (A[3] <= 0 || A[2] < -A[3]) return;
    int nv = stride - 5;
    float vb[MAXVF + 4]; gvx p; to_screen(A, nv, &p, vb);
    float sz = A[4]; if (sz < 1) sz = 1; if (sz > 256) sz = 256;
    int x0 = (int)floorf(p.x - sz / 2 + 0.5f), y0 = (int)floorf(p.y - sz / 2 + 0.5f), x1 = x0 + (int)sz, y1 = y0 + (int)sz;
    float var[MAXVF + 4]; for (int i = 0; i < nv; i++) var[i] = A[5 + i];
    for (int y = y0 < g_rect[1] ? g_rect[1] : y0; y < y1 && y < g_rect[3]; y++) for (int x = x0 < g_rect[0] ? g_rect[0] : x0; x < x1 && x < g_rect[2]; x++)
        shade(x, y, p.z, p.w, var, 1, 0.5f + (x + 0.5f - p.x) / sz, 0.5f - (y + 0.5f - p.y) / sz);
}

typedef struct { int on; const unsigned char* p; size_t len; int size, type, norm, stride, off; float k[4]; } gattr;
static float fetch1(const gattr* A, const unsigned char* q) {
    switch (A->type) {
    case 0x1406: { float f; memcpy(&f, q, 4); return f; }
    case 0x1401: return A->norm ? q[0] / 255.0f : q[0];
    case 0x1400: { signed char v = (signed char)q[0]; return A->norm ? (v < -127 ? -1.0f : v / 127.0f) : v; }
    case 0x1403: { unsigned short v; memcpy(&v, q, 2); return A->norm ? v / 65535.0f : v; }
    case 0x1402: { short v; memcpy(&v, q, 2); return A->norm ? (v < -32767 ? -1.0f : v / 32767.0f) : v; }
    case 0x1405: { unsigned v; memcpy(&v, q, 4); return A->norm ? v / 4294967295.0f : (float)v; }
    case 0x1404: { int v; memcpy(&v, q, 4); return (float)v; }
    }
    return 0;
}
static int tsize(int t) { return t == 0x1406 || t == 0x1404 || t == 0x1405 ? 4 : t == 0x1402 || t == 0x1403 ? 2 : 1; }

static JSValue gl_draw(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 9) return JS_NewInt32(ctx, -1);
    int32_t pi = -1, mode = 4, first = 0, count = 0;
    JS_ToInt32(ctx, &pi, argv[0]); JS_ToInt32(ctx, &mode, argv[1]); JS_ToInt32(ctx, &first, argv[2]); JS_ToInt32(ctx, &count, argv[3]);
    gprog* P = pi >= 0 && pi < GP_N ? g_prog[pi] : 0;
    if (!P || count <= 0) return JS_NewInt32(ctx, 0);
    GP = P;
    const unsigned char* ix = 0; size_t ixn = 0; int ixb = 0;
    if (JS_IsObject(argv[4])) { size_t bo = 0, bl = 0, bpe = 0; JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[4], &bo, &bl, &bpe); if (!JS_IsException(ab)) { size_t al; uint8_t* p = JS_GetArrayBuffer(ctx, &al, ab); if (p) { ix = p + bo; ixn = bl / (bpe ? bpe : 1); ixb = (int)bpe; } JS_FreeValue(ctx, ab); } else JS_FreeValue(ctx, JS_GetException(ctx)); }
    gattr AT[16]; memset(AT, 0, sizeof(AT));
    for (int l = 0; l < 16; l++) {
        JSValue e = JS_GetPropertyUint32(ctx, argv[5], (uint32_t)l);
        if (JS_IsArray(e)) {
            int32_t v[8] = { 0 }; for (int k = 0; k < 6; k++) { JSValue q = JS_GetPropertyUint32(ctx, e, (uint32_t)(k == 0 ? 0 : k + 1)); if (k > 0) JS_ToInt32(ctx, &v[k], q); else JS_ToInt32(ctx, &v[0], q); JS_FreeValue(ctx, q); }
            AT[l].on = v[0]; AT[l].size = v[1]; AT[l].type = v[2]; AT[l].norm = v[3]; AT[l].stride = v[4]; AT[l].off = v[5];
            for (int k = 0; k < 4; k++) { JSValue q = JS_GetPropertyUint32(ctx, e, (uint32_t)(7 + k)); double dv = k == 3 ? 1 : 0; JS_ToFloat64(ctx, &dv, q); AT[l].k[k] = (float)dv; JS_FreeValue(ctx, q); }
            JSValue bufv = JS_GetPropertyUint32(ctx, e, 1);
            if (AT[l].on && JS_IsObject(bufv)) { size_t al = 0; uint8_t* p = JS_GetArrayBuffer(ctx, &al, bufv); if (p) { AT[l].p = p; AT[l].len = al; } else { JS_FreeValue(ctx, JS_GetException(ctx)); AT[l].on = 0; } }
            else AT[l].on = 0;
            JS_FreeValue(ctx, bufv);
        }
        JS_FreeValue(ctx, e);
    }
    memset(g_tex, 0, sizeof(g_tex));
    for (int u = 0; u < 32; u++) {
        JSValue e = JS_GetPropertyUint32(ctx, argv[6], (uint32_t)u);
        if (JS_IsArray(e)) {
            int32_t v[7] = { 0 }; for (int k = 0; k < 7; k++) { if (k == 2) continue; JSValue q = JS_GetPropertyUint32(ctx, e, (uint32_t)k); JS_ToInt32(ctx, &v[k], q); JS_FreeValue(ctx, q); }
            JSValue pv = JS_GetPropertyUint32(ctx, e, 2); size_t bo = 0, bl = 0, bpe = 0; JSValue ab = JS_GetTypedArrayBuffer(ctx, pv, &bo, &bl, &bpe);
            if (!JS_IsException(ab)) { size_t al; uint8_t* p = JS_GetArrayBuffer(ctx, &al, ab); if (p && bl >= (size_t)v[0] * (size_t)v[1] * 4 && v[0] > 0 && v[1] > 0) { g_tex[u].px = p + bo; g_tex[u].w = v[0]; g_tex[u].h = v[1]; g_tex[u].minf = v[3]; g_tex[u].magf = v[4]; g_tex[u].ws = v[5]; g_tex[u].wt = v[6]; } JS_FreeValue(ctx, ab); } else JS_FreeValue(ctx, JS_GetException(ctx));
            JS_FreeValue(ctx, pv);
        }
        JS_FreeValue(ctx, e);
    }
    { size_t bo = 0, bl = 0, bpe = 0; JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[7], &bo, &bl, &bpe); if (JS_IsException(ab)) { JS_FreeValue(ctx, JS_GetException(ctx)); return JS_NewInt32(ctx, -1); } size_t al; uint8_t* p = JS_GetArrayBuffer(ctx, &al, ab); JS_FreeValue(ctx, ab); if (!p || bl < 36 * 4) return JS_NewInt32(ctx, -1);
      const float* f = (const float*)(p + bo);
      GS.vx = f[0]; GS.vy = f[1]; GS.vw = f[2]; GS.vh = f[3]; GS.dtest = (int)f[4]; GS.dfunc = (int)f[5]; GS.dmask = (int)f[6]; GS.blend = (int)f[7]; GS.srgb = (int)f[8]; GS.drgb = (int)f[9]; GS.sa = (int)f[10]; GS.da = (int)f[11]; GS.eqr = (int)f[12]; GS.eqa = (int)f[13];
      GS.cull = (int)f[14]; GS.cullface = (int)f[15]; GS.front = (int)f[16]; GS.scis = (int)f[17]; GS.sx = (int)f[18]; GS.sy = (int)f[19]; GS.sw = (int)f[20]; GS.sh = (int)f[21]; GS.mr = (int)f[22]; GS.mg = (int)f[23]; GS.mb = (int)f[24]; GS.ma = (int)f[25];
      GS.bc[0] = f[26]; GS.bc[1] = f[27]; GS.bc[2] = f[28]; GS.bc[3] = f[29]; GS.lw = f[30]; GS.pof = f[31]; GS.pou = f[32]; GS.pofill = (int)f[33]; GS.dn = f[34]; GS.df = f[35]; }
    { int32_t w = 0, h = 0; JSValue q = JS_GetPropertyUint32(ctx, argv[8], 0); JS_ToInt32(ctx, &w, q); JS_FreeValue(ctx, q); q = JS_GetPropertyUint32(ctx, argv[8], 1); JS_ToInt32(ctx, &h, q); JS_FreeValue(ctx, q);
      GTG.w = w; GTG.h = h; GTG.col = 0; GTG.dep = 0;
      JSValue cv = JS_GetPropertyUint32(ctx, argv[8], 2); size_t bo = 0, bl = 0, bpe = 0; JSValue ab = JS_GetTypedArrayBuffer(ctx, cv, &bo, &bl, &bpe); if (!JS_IsException(ab)) { size_t al; uint8_t* p = JS_GetArrayBuffer(ctx, &al, ab); if (p && bl >= (size_t)w * h * 4) GTG.col = p + bo; JS_FreeValue(ctx, ab); } else JS_FreeValue(ctx, JS_GetException(ctx)); JS_FreeValue(ctx, cv);
      JSValue dv = JS_GetPropertyUint32(ctx, argv[8], 3); if (JS_IsObject(dv)) { ab = JS_GetTypedArrayBuffer(ctx, dv, &bo, &bl, &bpe); if (!JS_IsException(ab)) { size_t al; uint8_t* p = JS_GetArrayBuffer(ctx, &al, ab); if (p && bl >= (size_t)w * h * 4) GTG.dep = (float*)(p + bo); JS_FreeValue(ctx, ab); } else JS_FreeValue(ctx, JS_GetException(ctx)); } JS_FreeValue(ctx, dv); }
    if (!GTG.col || GTG.w <= 0 || GTG.h <= 0) return JS_NewInt32(ctx, -1);
    g_rect[0] = (int)GS.vx; g_rect[1] = (int)GS.vy; g_rect[2] = (int)(GS.vx + GS.vw); g_rect[3] = (int)(GS.vy + GS.vh);
    if (g_rect[0] < 0) g_rect[0] = 0; if (g_rect[1] < 0) g_rect[1] = 0; if (g_rect[2] > GTG.w) g_rect[2] = GTG.w; if (g_rect[3] > GTG.h) g_rect[3] = GTG.h;
    if (GS.scis) { if (GS.sx > g_rect[0]) g_rect[0] = GS.sx; if (GS.sy > g_rect[1]) g_rect[1] = GS.sy; if (GS.sx + GS.sw < g_rect[2]) g_rect[2] = GS.sx + GS.sw; if (GS.sy + GS.sh < g_rect[3]) g_rect[3] = GS.sy + GS.sh; }
    if (g_rect[0] >= g_rect[2] || g_rect[1] >= g_rect[3]) return JS_NewInt32(ctx, 0);
    int stride = 5 + P->nvf;
    int nvert = count;
    long maxi = -1;
    if (ix) { if ((size_t)first + (size_t)count > ixn) return JS_NewInt32(ctx, -2); for (int i = 0; i < count; i++) { long v = ixb == 1 ? ix[first + i] : ixb == 2 ? ((const unsigned short*)ix)[first + i] : (long)((const unsigned*)ix)[first + i]; if (v > maxi) maxi = v; } }
    long nslots = ix ? maxi + 1 : count;
    if (nslots > 4000000 || (long long)nslots * stride > 12000000LL) return JS_NewInt32(ctx, -3);
    int pv = pool_suspend();
    float* VO = nalloc(sizeof(float) * (size_t)stride * (size_t)nslots);
    unsigned char* done = nzalloc((size_t)nslots);
    pool_resume(pv);
    gsh* vs = &P->vs->sh;
    for (long i0 = 0; i0 < (ix ? count : nvert); i0++) {
        long vi = ix ? (ixb == 1 ? ix[first + i0] : ixb == 2 ? ((const unsigned short*)ix)[first + i0] : (long)((const unsigned*)ix)[first + i0]) : first + i0;
        long slot = ix ? vi : i0;
        if (done[slot]) continue;
        done[slot] = 1;
        for (int a = 0; a < P->nattr; a++) {
            gattr* A = &AT[P->attr[a].loc];
            float* dst = vs->mem + P->attr[a].vslot;
            int n = comps(P->attr[a].base);
            float v[4] = { A->k[0], A->k[1], A->k[2], A->k[3] };
            if (A->on) {
                int es = tsize(A->type); int st = A->stride ? A->stride : es * A->size;
                size_t o = (size_t)A->off + (size_t)vi * (size_t)st;
                v[0] = 0; v[1] = 0; v[2] = 0; v[3] = 1;
                if (o + (size_t)es * (size_t)A->size <= A->len) for (int k = 0; k < A->size && k < 4; k++) v[k] = fetch1(A, A->p + o + (size_t)k * es);
            }
            for (int k = 0; k < n && k < 4; k++) dst[k] = v[k];
        }
        if (run_sh(vs, vs->main_entry) == 2) { nfree(VO); nfree(done); return JS_NewInt32(ctx, -4); }
        float* o = VO + (size_t)slot * stride;
        memcpy(o, vs->mem + vs->pos_slot, sizeof(float) * 4);
        o[4] = vs->mem[vs->psize_slot];
        for (int k = 0; k < P->nvar; k++) memcpy(o + 5 + P->var[k].off, vs->mem + P->var[k].vslot, sizeof(float) * (size_t)P->var[k].n);
    }
    #define VP(i) (VO + (size_t)(ix ? (ixb == 1 ? ix[first + (i)] : ixb == 2 ? ((const unsigned short*)ix)[first + (i)] : (long)((const unsigned*)ix)[first + (i)]) : (i)) * stride)
    g_frags = 0; g_gl_stop = 0;
    if (mode == 4) for (int i = 0; i + 2 < count; i += 3) tri_clip(VP(i), VP(i + 1), VP(i + 2), stride);
    else if (mode == 5) for (int i = 0; i + 2 < count; i++) { if (i & 1) tri_clip(VP(i + 1), VP(i), VP(i + 2), stride); else tri_clip(VP(i), VP(i + 1), VP(i + 2), stride); }
    else if (mode == 6) for (int i = 1; i + 1 < count; i++) tri_clip(VP(0), VP(i), VP(i + 1), stride);
    else if (mode == 1) for (int i = 0; i + 1 < count; i += 2) raster_line(VP(i), VP(i + 1), stride);
    else if (mode == 3 || mode == 2) { for (int i = 0; i + 1 < count; i++) raster_line(VP(i), VP(i + 1), stride); if (mode == 2 && count > 2) raster_line(VP(count - 1), VP(0), stride); }
    else if (mode == 0) for (int i = 0; i < count; i++) raster_point(VP(i), stride);
    #undef VP
    nfree(VO); nfree(done);
    return JS_NewInt32(ctx, (int)(g_frags > 0x7FFFFFFF ? 0x7FFFFFFF : g_frags));
}
static JSValue gl_clear(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 9) return JS_UNDEFINED;
    int32_t w = 0, h = 0, mask = 0, sx = 0, sy = 0, sw = 0, shh = 0, scis = 0, cm = 15; double cr = 0, cg = 0, cb = 0, ca = 0, dd = 1;
    JS_ToInt32(ctx, &mask, argv[1]); JS_ToFloat64(ctx, &cr, argv[2]); JS_ToFloat64(ctx, &cg, argv[3]); JS_ToFloat64(ctx, &cb, argv[4]); JS_ToFloat64(ctx, &ca, argv[5]); JS_ToFloat64(ctx, &dd, argv[6]);
    JS_ToInt32(ctx, &cm, argv[7]);
    { JSValue q; q = JS_GetPropertyUint32(ctx, argv[8], 0); JS_ToInt32(ctx, &scis, q); JS_FreeValue(ctx, q); q = JS_GetPropertyUint32(ctx, argv[8], 1); JS_ToInt32(ctx, &sx, q); JS_FreeValue(ctx, q); q = JS_GetPropertyUint32(ctx, argv[8], 2); JS_ToInt32(ctx, &sy, q); JS_FreeValue(ctx, q); q = JS_GetPropertyUint32(ctx, argv[8], 3); JS_ToInt32(ctx, &sw, q); JS_FreeValue(ctx, q); q = JS_GetPropertyUint32(ctx, argv[8], 4); JS_ToInt32(ctx, &shh, q); JS_FreeValue(ctx, q); }
    JSValue q = JS_GetPropertyUint32(ctx, argv[0], 0); JS_ToInt32(ctx, &w, q); JS_FreeValue(ctx, q); q = JS_GetPropertyUint32(ctx, argv[0], 1); JS_ToInt32(ctx, &h, q); JS_FreeValue(ctx, q);
    unsigned char* col = 0; float* dep = 0;
    { JSValue cv = JS_GetPropertyUint32(ctx, argv[0], 2); size_t bo = 0, bl = 0, bpe = 0; JSValue ab = JS_GetTypedArrayBuffer(ctx, cv, &bo, &bl, &bpe); if (!JS_IsException(ab)) { size_t al; uint8_t* p = JS_GetArrayBuffer(ctx, &al, ab); if (p && bl >= (size_t)w * h * 4) col = p + bo; JS_FreeValue(ctx, ab); } else JS_FreeValue(ctx, JS_GetException(ctx)); JS_FreeValue(ctx, cv);
      JSValue dv = JS_GetPropertyUint32(ctx, argv[0], 3); if (JS_IsObject(dv)) { ab = JS_GetTypedArrayBuffer(ctx, dv, &bo, &bl, &bpe); if (!JS_IsException(ab)) { size_t al; uint8_t* p = JS_GetArrayBuffer(ctx, &al, ab); if (p && bl >= (size_t)w * h * 4) dep = (float*)(p + bo); JS_FreeValue(ctx, ab); } else JS_FreeValue(ctx, JS_GetException(ctx)); } JS_FreeValue(ctx, dv); }
    int x0 = 0, y0 = 0, x1 = w, y1 = h;
    if (scis) { if (sx > x0) x0 = sx; if (sy > y0) y0 = sy; if (sx + sw < x1) x1 = sx + sw; if (sy + shh < y1) y1 = sy + shh; }
    unsigned char cc[4] = { (unsigned char)(clampf((float)cr, 0, 1) * 255 + 0.5f), (unsigned char)(clampf((float)cg, 0, 1) * 255 + 0.5f), (unsigned char)(clampf((float)cb, 0, 1) * 255 + 0.5f), (unsigned char)(clampf((float)ca, 0, 1) * 255 + 0.5f) };
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        size_t i = (size_t)y * w + x;
        if ((mask & 0x4000) && col) { unsigned char* p = col + i * 4; for (int k = 0; k < 4; k++) if (cm & (1 << k)) p[k] = cc[k]; }
        if ((mask & 0x100) && dep) dep[i] = (float)clampf((float)dd, 0, 1);
    }
    return JS_UNDEFINED;
}
nimg* cv_get(node* n, int w, int h);
node* js_node_of(JSValueConst v);
static JSValue gl_present(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 5) return JS_UNDEFINED;
    node* n = js_node_of(argv[0]); if (!n) return JS_UNDEFINED;
    int32_t w = 0, h = 0, pm = 1, alpha = 1; JS_ToInt32(ctx, &w, argv[2]); JS_ToInt32(ctx, &h, argv[3]); JS_ToInt32(ctx, &pm, argv[4]); if (argc > 5) JS_ToInt32(ctx, &alpha, argv[5]);
    size_t bo = 0, bl = 0, bpe = 0; JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[1], &bo, &bl, &bpe); if (JS_IsException(ab)) { JS_FreeValue(ctx, JS_GetException(ctx)); return JS_UNDEFINED; }
    size_t al; uint8_t* p = JS_GetArrayBuffer(ctx, &al, ab); JS_FreeValue(ctx, ab);
    if (!p || w <= 0 || h <= 0 || bl < (size_t)w * h * 4) return JS_UNDEFINED;
    const unsigned char* src = p + bo;
    nimg* im = cv_get(n, w, h);
    if (!im || !im->px) return JS_UNDEFINED;
    for (int y = 0; y < h; y++) {
        const unsigned char* s = src + (size_t)(h - 1 - y) * w * 4; unsigned* d = im->px + (size_t)y * w;
        for (int x = 0; x < w; x++, s += 4) {
            unsigned r = s[0], g = s[1], b = s[2], a = alpha ? s[3] : 255;
            if (pm && a && a < 255) { r = r * 255 / a; g = g * 255 / a; b = b * 255 / a; if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255; }
            d[x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    G.dirty_paint = 1;
    return JS_UNDEFINED;
}
static JSValue gl_imgpx(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    node* n = argc > 0 ? js_node_of(argv[0]) : 0;
    if (!n || !n->img || !n->img->px || n->img->w <= 0 || n->img->h <= 0) return JS_NULL;
    nimg* im = n->img; size_t np = (size_t)im->w * im->h;
    int pv = pool_suspend(); unsigned char* b = nalloc(np * 4); pool_resume(pv);
    for (size_t i = 0; i < np; i++) { unsigned v = im->px[i]; b[i * 4] = (unsigned char)(v >> 16); b[i * 4 + 1] = (unsigned char)(v >> 8); b[i * 4 + 2] = (unsigned char)v; b[i * 4 + 3] = (unsigned char)(v >> 24); }
    JSValue r = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, r, 0, JS_NewInt32(ctx, im->w)); JS_SetPropertyUint32(ctx, r, 1, JS_NewInt32(ctx, im->h));
    JS_SetPropertyUint32(ctx, r, 2, JS_NewUint8ArrayCopy(ctx, b, np * 4));
    nfree(b);
    return r;
}
static const JSCFunctionListEntry gl_funcs[] = {
    JS_CFUNC_DEF("glCompile", 2, gl_compile), JS_CFUNC_DEF("glDelShader", 1, gl_delshader), JS_CFUNC_DEF("glLink", 3, gl_link), JS_CFUNC_DEF("glDelProgram", 1, gl_delprog),
    JS_CFUNC_DEF("glUniform", 4, gl_uniform), JS_CFUNC_DEF("glDraw", 9, gl_draw), JS_CFUNC_DEF("glClear", 9, gl_clear), JS_CFUNC_DEF("glPresent", 6, gl_present), JS_CFUNC_DEF("glImagePixels", 1, gl_imgpx),
};
void webgl_install(JSContext* ctx, JSValue nat) { JS_SetPropertyFunctionList(ctx, nat, gl_funcs, (int)(sizeof(gl_funcs) / sizeof(gl_funcs[0]))); }
