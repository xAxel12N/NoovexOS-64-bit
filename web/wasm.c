#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "quickjs.h"
#include "third_party/wasm3/wasm3.h"
#include "third_party/wasm3/m3_env.h"
#include "third_party/wasm3/m3_function.h"
#include "third_party/wasm3/m3_compile.h"

typedef struct { uint8_t* b; uint32_t n; } wmod;
typedef struct { IM3Memory m; char* own; char* exp; JSValue ab; void* p; size_t n; } wmem;
typedef struct { IM3Table t; char* own; char* exp; } wtab;
typedef struct { IM3Global g; char* own; char* exp; } wglob;
typedef struct { JSValue fn; } wimp;

static JSContext* wc;
static IM3Environment wenv;
static IM3Runtime wrt;
static JSValue e_compile, e_link, e_runtime, e_mkfn;
static int e_ok;
static wmod* mods; static int nmods, cmods;
static wmem* mems; static int nmems, cmems;
static wtab* tabs; static int ntabs, ctabs;
static wglob* globs; static int nglobs, cglobs;
static IM3Function* fns; static int nfns, cfns;
static IM3Function* fh_k; static int* fh_v; static int fh_cap;
static JSValue* xrefs; static int nxrefs, cxrefs;
static int ninst;
static JSValue pend_exc; static int has_pend;
static const char js_exc[] = "[trap] JavaScript exception";
#ifndef WASM_STOP_MS
#define WASM_STOP_MS 20000
#endif
unsigned g_m3_ctr;
static unsigned long w_deadline;
static int w_depth;
extern int nwk_busy(void);
extern unsigned long nwk_ms(void);
int m3_host_stop(void) { if (nwk_busy()) return 1; return w_deadline && nwk_ms() > w_deadline; }
static void w_enter(void) { if (w_depth++ == 0) w_deadline = nwk_ms() + WASM_STOP_MS; }
static void w_leave(void) { if (--w_depth <= 0) { w_depth = 0; w_deadline = 0; } }

#define GROW(arr, n, c) do { if ((n) >= (c)) { int nc = (c) ? (c) * 2 : 16; void* np = realloc((arr), sizeof(*(arr)) * (size_t)nc); if (!np) return -1; (arr) = np; (c) = nc; } } while (0)

static char* wdup(const char* s) { size_t l = strlen(s); char* d = m3_Malloc_Impl(l + 1); if (d) memcpy(d, s, l + 1); return d; }

static int rt_ready(void) {
    if (wrt) return 0;
    if (!wenv) wenv = m3_NewEnvironment();
    if (!wenv) return -1;
    wrt = m3_NewRuntime(wenv, 512 * 1024, 0);
    return wrt ? 0 : -1;
}

static JSValue throw_kind(JSContext* c, JSValue ctor, const char* pre, const char* msg) {
    char buf[600];
    if (!strncmp(msg, "[trap] ", 7)) msg += 7;
    if (!strcmp(msg, "out of bounds memory access")) msg = "memory access out of bounds";
    else if (!strcmp(msg, "integer divide by zero")) msg = "divide by zero";
    else if (!strcmp(msg, "integer overflow")) msg = "divide result unrepresentable";
    else if (!strcmp(msg, "unreachable executed")) msg = "unreachable";
    else if (!strcmp(msg, "indirect call type mismatch")) msg = "null function or function signature mismatch";
    snprintf(buf, sizeof(buf), "%s%s", pre, msg);
    if (!e_ok) return JS_ThrowTypeError(c, "%s", buf);
    JSValue m = JS_NewString(c, buf);
    JSValue e = JS_CallConstructor(c, ctor, 1, &m);
    JS_FreeValue(c, m);
    if (JS_IsException(e)) return e;
    return JS_Throw(c, e);
}

static JSValue throw_result(JSContext* c, M3Result r) {
    if (r == js_exc && has_pend) { has_pend = 0; JSValue e = pend_exc; pend_exc = JS_UNDEFINED; return JS_Throw(c, e); }
    if (r == m3Err_trapStackOverflow) return JS_ThrowRangeError(c, "Maximum call stack size exceeded");
    if (r == m3Err_trapStopped) return throw_kind(c, e_runtime, "", "the WebAssembly code ran too long and was stopped");
    return throw_kind(c, e_runtime, "", r);
}

static void fh_rehash(int cap) {
    IM3Function* ok = fh_k; int* ov = fh_v; int oc = fh_cap;
    fh_k = calloc((size_t)cap, sizeof(*fh_k)); fh_v = calloc((size_t)cap, sizeof(int)); fh_cap = cap;
    for (int i = 0; i < oc; i++) if (ok[i]) { unsigned h = (unsigned)(((uintptr_t)ok[i] >> 4) * 2654435761u) & (unsigned)(cap - 1); while (fh_k[h]) h = (h + 1) & (unsigned)(cap - 1); fh_k[h] = ok[i]; fh_v[h] = ov[i]; }
    free(ok); free(ov);
}

static int fn_id(IM3Function f) {
    if (!f) return -1;
    f = Function_Implementation(f);
    if (fh_cap == 0 || nfns * 2 >= fh_cap) fh_rehash(fh_cap ? fh_cap * 2 : 256);
    if (!fh_k) return -1;
    unsigned h = (unsigned)(((uintptr_t)f >> 4) * 2654435761u) & (unsigned)(fh_cap - 1);
    while (fh_k[h]) { if (fh_k[h] == f) return fh_v[h]; h = (h + 1) & (unsigned)(fh_cap - 1); }
    GROW(fns, nfns, cfns);
    fns[nfns] = f; fh_k[h] = f; fh_v[h] = nfns;
    return nfns++;
}

static int mem_id(IM3Memory m, const char* own, const char* exp) {
    for (int i = 0; i < nmems; i++) if (mems[i].m == m) return i;
    GROW(mems, nmems, cmems);
    wmem* w = &mems[nmems];
    w->m = m; w->own = wdup(own); w->exp = wdup(exp); w->ab = JS_UNDEFINED; w->p = 0; w->n = 0;
    return nmems++;
}
static int tab_id(IM3Table t, const char* own, const char* exp) {
    for (int i = 0; i < ntabs; i++) if (tabs[i].t == t) return i;
    GROW(tabs, ntabs, ctabs);
    tabs[ntabs].t = t; tabs[ntabs].own = wdup(own); tabs[ntabs].exp = wdup(exp);
    return ntabs++;
}
static int glob_id(IM3Global g, const char* own, const char* exp) {
    for (int i = 0; i < nglobs; i++) if (globs[i].g == g) return i;
    GROW(globs, nglobs, cglobs);
    globs[nglobs].g = g; globs[nglobs].own = wdup(own); globs[nglobs].exp = wdup(exp);
    return nglobs++;
}
static int xref_put(JSContext* c, JSValueConst v) {
    for (int i = 0; i < nxrefs; i++) if (JS_IsStrictEqual(c, xrefs[i], v)) return i;
    GROW(xrefs, nxrefs, cxrefs);
    xrefs[nxrefs] = JS_DupValue(c, v);
    return nxrefs++;
}

static void mem_data(IM3Memory m, void** p, size_t* n) {
    if (!m || !m->mallocated) { *p = 0; *n = 0; return; }
    *p = (void*)(m->mallocated + 1); *n = (size_t)m->mallocated->length;
}
static void mem_sync(JSContext* c) {
    for (int i = 0; i < nmems; i++) {
        wmem* w = &mems[i];
        if (JS_IsUndefined(w->ab)) continue;
        void* p; size_t n; mem_data(w->m, &p, &n);
        if (p != w->p || n != w->n) { JS_DetachArrayBuffer(c, w->ab); JS_FreeValue(c, w->ab); w->ab = JS_UNDEFINED; }
    }
}

static JSValue fn_val(JSContext* c, IM3Function f) {
    if (!f) return JS_NULL;
    int id = fn_id(f);
    if (id < 0 || !e_ok) return JS_NULL;
    JSValue a = JS_NewInt32(c, id);
    return JS_Call(c, e_mkfn, JS_UNDEFINED, 1, &a);
}

static JSValue to_js(JSContext* c, int ty, const void* s) {
    switch (ty) {
    case c_m3Type_i32: return JS_NewInt32(c, *(const int32_t*)s);
    case c_m3Type_i64: return JS_NewBigInt64(c, *(const int64_t*)s);
    case c_m3Type_f32: return JS_NewFloat64(c, (double)*(const float*)s);
    case c_m3Type_f64: return JS_NewFloat64(c, *(const double*)s);
    case c_m3Type_funcref: return fn_val(c, *(IM3Function const*)s);
    case c_m3Type_externref: { uintptr_t h = *(const uintptr_t*)s; if (!h || (int)(h - 1) >= nxrefs) return JS_NULL; return JS_DupValue(c, xrefs[h - 1]); }
    }
    return JS_UNDEFINED;
}

static int from_js(JSContext* c, int ty, JSValueConst v, void* s) {
    switch (ty) {
    case c_m3Type_i32: { int32_t x; if (JS_ToInt32(c, &x, v)) return -1; *(int32_t*)s = x; return 0; }
    case c_m3Type_i64: {
        int64_t x;
        if (JS_IsNumber(v) || JS_IsUndefined(v) || JS_IsNull(v)) { const char* t = JS_ToCString(c, v); JS_ThrowTypeError(c, "Cannot convert %s to a BigInt", t ? t : ""); JS_FreeCString(c, t); return -1; }
        if (JS_ToBigInt64(c, &x, v)) return -1;
        *(int64_t*)s = x; return 0;
    }
    case c_m3Type_f32: { double x; if (JS_ToFloat64(c, &x, v)) return -1; *(float*)s = (float)x; return 0; }
    case c_m3Type_f64: { double x; if (JS_ToFloat64(c, &x, v)) return -1; *(double*)s = x; return 0; }
    case c_m3Type_funcref: {
        if (JS_IsNull(v)) { *(void**)s = 0; return 0; }
        JSValue id = JS_IsObject(v) ? JS_GetPropertyStr(c, v, "__wfid") : JS_UNDEFINED;
        int32_t k = -1;
        if (JS_IsNumber(id)) JS_ToInt32(c, &k, id);
        JS_FreeValue(c, id);
        if (k < 0 || k >= nfns) { JS_ThrowTypeError(c, "type incompatibility when transforming from/to JS"); return -1; }
        *(void**)s = fns[k]; return 0;
    }
    case c_m3Type_externref: { if (JS_IsNull(v)) { *(uintptr_t*)s = 0; return 0; } int k = xref_put(c, v); if (k < 0) { JS_ThrowOutOfMemory(c); return -1; } *(uintptr_t*)s = (uintptr_t)k + 1; return 0; }
    }
    JS_ThrowTypeError(c, "type incompatibility when transforming from/to JS");
    return -1;
}

static m3ApiRawFunction(w_host) {
    (void)runtime; (void)_mem;
    JSContext* c = wc;
    wimp* im = (wimp*)_ctx->userdata;
    IM3FuncType t = _ctx->function->funcType;
    int nr = t->numRets, na = t->numArgs;
    mem_sync(c);
    JSValue sv[8];
    JSValue* av = na <= 8 ? sv : malloc(sizeof(JSValue) * (size_t)na);
    if (!av) return "out of memory";
    for (int i = 0; i < na; i++) av[i] = to_js(c, d_FuncArgType(t, i), _sp + nr + i);
    JSValue r = JS_Call(c, im->fn, JS_UNDEFINED, na, av);
    for (int i = 0; i < na; i++) JS_FreeValue(c, av[i]);
    if (av != sv) free(av);
    if (JS_IsException(r)) { if (has_pend) JS_FreeValue(c, pend_exc); pend_exc = JS_GetException(c); has_pend = 1; return js_exc; }
    int bad = 0;
    if (nr == 1) bad = from_js(c, d_FuncRetType(t, 0), r, _sp);
    else if (nr > 1) {
        for (int i = 0; i < nr && !bad; i++) { JSValue x = JS_GetPropertyUint32(c, r, (uint32_t)i); if (JS_IsException(x)) bad = 1; else { bad = from_js(c, d_FuncRetType(t, i), x, _sp + i); JS_FreeValue(c, x); } }
    }
    JS_FreeValue(c, r);
    if (bad) { if (has_pend) JS_FreeValue(c, pend_exc); pend_exc = JS_GetException(c); has_pend = 1; return js_exc; }
    return m3Err_none;
}

static uint8_t* bytes_of(JSContext* c, JSValueConst v, size_t* n) {
    uint8_t* p = JS_GetUint8Array(c, n, v);
    if (p) return p;
    JS_FreeValue(c, JS_GetException(c));
    p = JS_GetArrayBuffer(c, n, v);
    if (p) return p;
    JS_FreeValue(c, JS_GetException(c));
    return 0;
}

static JSValue n_wparse(JSContext* c, JSValueConst t, int argc, JSValueConst* argv) {
    (void)t;
    size_t n = 0;
    uint8_t* p = argc > 0 ? bytes_of(c, argv[0], &n) : 0;
    if (!p) return JS_ThrowTypeError(c, "WebAssembly.Module(): Argument 0 must be a buffer source");
    if (rt_ready()) return JS_ThrowOutOfMemory(c);
    uint8_t* b = malloc(n ? n : 1);
    if (!b) return JS_ThrowOutOfMemory(c);
    memcpy(b, p, n);
    IM3Module m = 0;
    M3Result r = m3_ParseModule(wenv, &m, b, (uint32_t)n);
    if (r) { free(b); return throw_kind(c, e_compile, "WebAssembly.Module(): ", r); }
    m3_FreeModule(m);
    if (nmods >= cmods) { int nc = cmods ? cmods * 2 : 16; void* np = realloc(mods, sizeof(*mods) * (size_t)nc); if (!np) { free(b); return JS_ThrowOutOfMemory(c); } mods = np; cmods = nc; }
    mods[nmods].b = b; mods[nmods].n = (uint32_t)n;
    return JS_NewInt32(c, nmods++);
}

static void set_import(M3ImportInfo* ii, const char* own, const char* exp) {
    m3_Free_Impl((void*)ii->moduleUtf8); m3_Free_Impl((void*)ii->fieldUtf8);
    ii->moduleUtf8 = wdup(own); ii->fieldUtf8 = wdup(exp);
}

static int arr_len(JSContext* c, JSValueConst a) { int64_t l = 0; JSValue v = JS_GetPropertyStr(c, a, "length"); JS_ToInt64(c, &l, v); JS_FreeValue(c, v); return (int)l; }
static int arr_int(JSContext* c, JSValueConst a, int i) { int32_t x = -1; JSValue v = JS_GetPropertyUint32(c, a, (uint32_t)i); JS_ToInt32(c, &x, v); JS_FreeValue(c, v); return x; }

static JSValue n_winst(JSContext* c, JSValueConst t, int argc, JSValueConst* argv) {
    (void)t;
    if (argc < 6) return JS_ThrowTypeError(c, "bad args");
    int32_t mi; JS_ToInt32(c, &mi, argv[0]);
    if (mi < 0 || mi >= nmods) return JS_ThrowTypeError(c, "WebAssembly.Instance(): Argument 0 must be a WebAssembly.Module");
    if (rt_ready()) return JS_ThrowOutOfMemory(c);
    IM3Module m = 0;
    M3Result r = m3_ParseModule(wenv, &m, mods[mi].b, mods[mi].n);
    if (r) return throw_kind(c, e_compile, "WebAssembly.Instance(): ", r);
    char nm[32]; snprintf(nm, sizeof(nm), "__i%d", ninst++);
    m3_SetModuleName(m, wdup(nm));
    int k = 0;
    for (u32 i = 0; i < m->numMemories; i++) {
        IM3Memory me = m->memories[i];
        if (!me->imported || me->owner != m) continue;
        int id = arr_int(c, argv[2], k++);
        if (id < 0 || id >= nmems) { m3_FreeModule(m); return throw_kind(c, e_link, "WebAssembly.Instance(): ", "memory import must be a WebAssembly.Memory object"); }
        set_import(&me->import, mems[id].own, mems[id].exp);
    }
    k = 0;
    for (u32 i = 0; i < m->numTables; i++) {
        IM3Table tb = m->tables[i];
        if (!tb->imported || tb->owner != m) continue;
        int id = arr_int(c, argv[3], k++);
        if (id < 0 || id >= ntabs) { m3_FreeModule(m); return throw_kind(c, e_link, "WebAssembly.Instance(): ", "table import requires a WebAssembly.Table"); }
        set_import(&tb->import, tabs[id].own, tabs[id].exp);
    }
    k = 0;
    for (u32 i = 0; i < m->numGlobals; i++) {
        IM3Global g = &m->globals[i];
        if (!g->imported || !g->import.moduleUtf8) continue;
        int id = arr_int(c, argv[4], k);
        if (id >= 0 && id < nglobs) set_import(&g->import, globs[id].own, globs[id].exp);
        else {
            JSValue v = JS_GetPropertyUint32(c, argv[5], (uint32_t)k);
            M3TaggedValue tv; memset(&tv, 0, sizeof(tv)); tv.type = (M3ValueType)g->type;
            int bad = 0;
            if (g->type == c_m3Type_i32) { int32_t x; bad = JS_ToInt32(c, &x, v); tv.value.i32 = (uint32_t)x; }
            else if (g->type == c_m3Type_i64) { int64_t x; bad = JS_ToBigInt64(c, &x, v); tv.value.i64 = (uint64_t)x; }
            else if (g->type == c_m3Type_f32) { double x; bad = JS_ToFloat64(c, &x, v); tv.value.f32 = (float)x; }
            else if (g->type == c_m3Type_f64) { double x; bad = JS_ToFloat64(c, &x, v); tv.value.f64 = x; }
            else if (g->type == c_m3Type_externref) { if (JS_IsNull(v)) g->refValue = 0; else { int h = xref_put(c, v); g->refValue = (void*)(uintptr_t)(h + 1); } }
            else if (g->type == c_m3Type_funcref) { void* fp = 0; bad = from_js(c, c_m3Type_funcref, v, &fp); g->refValue = fp; }
            JS_FreeValue(c, v);
            if (bad) { m3_FreeModule(m); return JS_EXCEPTION; }
            if (g->type <= c_m3Type_f64) { r = m3_LinkGlobal(m, g->import.moduleUtf8, g->import.fieldUtf8, &tv); if (r) { m3_FreeModule(m); return throw_kind(c, e_link, "WebAssembly.Instance(): ", r); } }
            set_import(&g->import, "__none", "__none");
        }
        k++;
    }
    int nf = arr_len(c, argv[1]);
    if (nf < (int)m->numFuncImports) { m3_FreeModule(m); return throw_kind(c, e_link, "WebAssembly.Instance(): ", "function import requires a callable"); }
    r = m3_LoadModule(wrt, m);
    if (r) {
        if (r == m3Err_unknownImport || r == m3Err_incompatibleImportType) return throw_kind(c, e_link, "WebAssembly.Instance(): ", r);
        return throw_kind(c, e_runtime, "WebAssembly.Instance(): ", r);
    }
    k = 0;
    for (u32 i = 0; i < m->numFunctions; i++) {
        IM3Function f = &m->functions[i];
        if (f->wasm || !f->import.moduleUtf8 || f->resolved) continue;
        JSValue fv = JS_GetPropertyUint32(c, argv[1], (uint32_t)k++);
        wimp* im = malloc(sizeof(wimp));
        if (!im) { JS_FreeValue(c, fv); return JS_ThrowOutOfMemory(c); }
        im->fn = fv;
        r = CompileRawFunction(m, f, (const void*)w_host, im);
        if (r) return throw_kind(c, e_link, "WebAssembly.Instance(): ", r);
    }
    w_enter();
    r = m3_RunStart(m);
    w_leave();
    mem_sync(c);
    if (r) return throw_result(c, r);
    JSValue out = JS_NewArray(c);
    for (u32 i = 0; i < m->numExports; i++) {
        M3Export* e = &m->exports[i];
        int id = -1, kind = e->kind;
        if (kind == d_externalKind_function) {
            if (e->index < m->numFunctions) {
                IM3Function f = &m->functions[e->index];
                if (!f->wasm && !f->resolved && f->import.moduleUtf8) { int q = 0; for (u32 j = 0; j < e->index; j++) { IM3Function g2 = &m->functions[j]; if (!g2->wasm && !g2->resolved && g2->import.moduleUtf8) q++; } kind = 10; id = q; }
                else {
                    IM3Function im = Function_Implementation(f);
                    if (!im->compiled) { r = CompileFunction(im); if (r) { JS_FreeValue(c, out); return throw_kind(c, e_compile, "WebAssembly.Instance(): Compiling function failed: ", r); } }
                    id = fn_id(im);
                }
            }
        } else if (kind == d_externalKind_memory) { if (e->index < m->numMemories) id = mem_id(m->memories[e->index], nm, e->name); }
        else if (kind == d_externalKind_table) { if (e->index < m->numTables) id = tab_id(m->tables[e->index], nm, e->name); }
        else if (kind == d_externalKind_global) { if (e->index < m->numGlobals) { IM3Global g = &m->globals[e->index]; id = glob_id(g->resolved ? g->resolved : g, nm, e->name); } }
        JSValue row = JS_NewArray(c);
        JS_SetPropertyUint32(c, row, 0, JS_NewStringLen(c, e->name, e->nameLength));
        JS_SetPropertyUint32(c, row, 1, JS_NewInt32(c, kind));
        JS_SetPropertyUint32(c, row, 2, JS_NewInt32(c, id));
        JS_SetPropertyUint32(c, out, i, row);
    }
    return out;
}

static JSValue n_wfinfo(JSContext* c, JSValueConst t, int argc, JSValueConst* argv) {
    (void)t; (void)argc;
    int32_t id; JS_ToInt32(c, &id, argv[0]);
    if (id < 0 || id >= nfns) return JS_UNDEFINED;
    IM3Function f = fns[id];
    IM3Module m = f->module;
    int idx = (int)(f - m->functions);
    JSValue a = JS_NewArray(c);
    JS_SetPropertyUint32(c, a, 0, JS_NewInt32(c, f->funcType->numArgs));
    JS_SetPropertyUint32(c, a, 1, JS_NewInt32(c, idx));
    return a;
}

static JSValue n_wcall(JSContext* c, JSValueConst t, int argc, JSValueConst* argv) {
    (void)t;
    int32_t id; JS_ToInt32(c, &id, argv[0]);
    if (id < 0 || id >= nfns) return JS_ThrowTypeError(c, "not a wasm function");
    IM3Function f = fns[id];
    IM3FuncType ft = f->funcType;
    int na = ft->numArgs, nr = ft->numRets;
    uint64_t sv[16]; const void* sp[16];
    uint64_t* vals = sv; const void** ptrs = sp;
    int cnt = na > nr ? na : nr;
    if (cnt > 16) { vals = calloc((size_t)cnt, 8); ptrs = calloc((size_t)cnt, sizeof(void*)); if (!vals || !ptrs) { free(vals); free(ptrs); return JS_ThrowOutOfMemory(c); } }
    JSValue res = JS_UNDEFINED;
    for (int i = 0; i < na; i++) {
        vals[i] = 0;
        JSValueConst v = i + 1 < argc ? argv[i + 1] : JS_UNDEFINED;
        if (from_js(c, d_FuncArgType(ft, i), v, &vals[i])) { res = JS_EXCEPTION; goto done; }
        ptrs[i] = &vals[i];
    }
    if (!f->compiled) { M3Result cr = CompileFunction(f); if (cr) { res = throw_kind(c, e_runtime, "", cr); goto done; } }
    w_enter();
    M3Result r = m3_Call(f, (uint32_t)na, ptrs);
    w_leave();
    mem_sync(c);
    if (r) { res = throw_result(c, r); goto done; }
    if (nr == 0) goto done;
    for (int i = 0; i < nr; i++) { vals[i] = 0; ptrs[i] = &vals[i]; }
    r = m3_GetResults(f, (uint32_t)nr, ptrs);
    if (r) { res = throw_kind(c, e_runtime, "", r); goto done; }
    if (nr == 1) res = to_js(c, d_FuncRetType(ft, 0), &vals[0]);
    else { res = JS_NewArray(c); for (int i = 0; i < nr; i++) JS_SetPropertyUint32(c, res, (uint32_t)i, to_js(c, d_FuncRetType(ft, i), &vals[i])); }
done:
    if (vals != sv) { free(vals); free(ptrs); }
    return res;
}

static uint8_t* leb(uint8_t* p, uint64_t v) { do { uint8_t b = v & 0x7f; v >>= 7; if (v) b |= 0x80; *p++ = b; } while (v); return p; }
static uint8_t* sleb(uint8_t* p, int64_t v) { for (;;) { uint8_t b = v & 0x7f; v >>= 7; if ((v == 0 && !(b & 0x40)) || (v == -1 && (b & 0x40))) { *p++ = b; return p; } *p++ = b | 0x80; } }

static IM3Module synth(uint8_t* body, int blen, int sec, int kind, char* nm) {
    uint8_t* b = malloc((size_t)blen + 64);
    if (!b) return 0;
    uint8_t* p = b;
    memcpy(p, "\0asm\1\0\0\0", 8); p += 8;
    *p++ = (uint8_t)sec; p = leb(p, (uint64_t)blen); memcpy(p, body, (size_t)blen); p += blen;
    *p++ = 7; *p++ = 5; *p++ = 1; *p++ = 1; *p++ = 'x'; *p++ = (uint8_t)kind; *p++ = 0;
    IM3Module m = 0;
    if (m3_ParseModule(wenv, &m, b, (uint32_t)(p - b))) { free(b); return 0; }
    snprintf(nm, 32, "__s%d", ninst++);
    m3_SetModuleName(m, wdup(nm));
    if (m3_LoadModule(wrt, m)) return 0;
    return m;
}

static JSValue n_wnewmem(JSContext* c, JSValueConst t, int argc, JSValueConst* argv) {
    (void)t; (void)argc;
    if (rt_ready()) return JS_ThrowOutOfMemory(c);
    int64_t ini = 0, mx = -1; JS_ToInt64(c, &ini, argv[0]); JS_ToInt64(c, &mx, argv[1]);
    uint8_t body[32], *p = body;
    *p++ = 1; *p++ = mx >= 0 ? 1 : 0; p = leb(p, (uint64_t)ini); if (mx >= 0) p = leb(p, (uint64_t)mx);
    char nm[32];
    IM3Module m = synth(body, (int)(p - body), 5, 2, nm);
    if (!m || !m->numMemories) return JS_ThrowRangeError(c, "WebAssembly.Memory(): could not allocate memory");
    return JS_NewInt32(c, mem_id(m->memories[0], nm, "x"));
}

static JSValue n_wnewtab(JSContext* c, JSValueConst t, int argc, JSValueConst* argv) {
    (void)t; (void)argc;
    if (rt_ready()) return JS_ThrowOutOfMemory(c);
    int32_t ext; int64_t ini = 0, mx = -1; JS_ToInt32(c, &ext, argv[0]); JS_ToInt64(c, &ini, argv[1]); JS_ToInt64(c, &mx, argv[2]);
    uint8_t body[32], *p = body;
    *p++ = 1; *p++ = ext ? 0x6f : 0x70; *p++ = mx >= 0 ? 1 : 0; p = leb(p, (uint64_t)ini); if (mx >= 0) p = leb(p, (uint64_t)mx);
    char nm[32];
    IM3Module m = synth(body, (int)(p - body), 4, 1, nm);
    if (!m || !m->numTables) return JS_ThrowRangeError(c, "WebAssembly.Table(): could not allocate table");
    return JS_NewInt32(c, tab_id(m->tables[0], nm, "x"));
}

static JSValue n_wnewglob(JSContext* c, JSValueConst t, int argc, JSValueConst* argv) {
    (void)t; (void)argc;
    if (rt_ready()) return JS_ThrowOutOfMemory(c);
    int32_t ty, mut; JS_ToInt32(c, &ty, argv[0]); JS_ToInt32(c, &mut, argv[1]);
    uint8_t body[32], *p = body;
    static const uint8_t vt[] = { 0, 0x7f, 0x7e, 0x7d, 0x7c, 0, 0x70, 0x6f };
    if (ty < 1 || ty > 7 || ty == 5) return JS_ThrowTypeError(c, "WebAssembly.Global(): Descriptor property 'value' must be a WebAssembly type");
    *p++ = 1; *p++ = vt[ty]; *p++ = mut ? 1 : 0;
    if (ty == 1) { *p++ = 0x41; *p++ = 0; } else if (ty == 2) { *p++ = 0x42; *p++ = 0; } else if (ty == 3) { *p++ = 0x43; memset(p, 0, 4); p += 4; } else if (ty == 4) { *p++ = 0x44; memset(p, 0, 8); p += 8; } else { *p++ = 0xd0; *p++ = vt[ty]; }
    *p++ = 0x0b;
    char nm[32];
    IM3Module m = synth(body, (int)(p - body), 6, 3, nm);
    if (!m || !m->numGlobals) return JS_ThrowRangeError(c, "WebAssembly.Global(): could not create global");
    return JS_NewInt32(c, glob_id(&m->globals[0], nm, "x"));
}

static JSValue n_wmembuf(JSContext* c, JSValueConst t, int argc, JSValueConst* argv) {
    (void)t; (void)argc;
    int32_t id; JS_ToInt32(c, &id, argv[0]);
    if (id < 0 || id >= nmems) return JS_UNDEFINED;
    mem_sync(c);
    wmem* w = &mems[id];
    if (JS_IsUndefined(w->ab)) {
        mem_data(w->m, &w->p, &w->n);
        static uint8_t z;
        w->ab = JS_NewArrayBuffer(c, w->p ? (uint8_t*)w->p : &z, w->n, 0, 0, 0, 0);
        if (JS_IsException(w->ab)) { w->ab = JS_UNDEFINED; return JS_EXCEPTION; }
    }
    return JS_DupValue(c, w->ab);
}

static JSValue n_wmemop(JSContext* c, JSValueConst t, int argc, JSValueConst* argv, int magic) {
    (void)t; (void)argc;
    int32_t id; JS_ToInt32(c, &id, argv[0]);
    if (id < 0 || id >= nmems) return JS_UNDEFINED;
    IM3Memory m = mems[id].m;
    if (magic == 0) {
        int64_t d = 0; JS_ToInt64(c, &d, argv[1]);
        u64 old = m->numPages;
        if (d < 0 || (u64)d > m->maxPages || old + (u64)d > m->maxPages || old + (u64)d > 65536) return JS_ThrowRangeError(c, m->hasMax && old + (u64)d > m->maxPages ? "WebAssembly.Memory.grow(): Maximum memory size exceeded" : "WebAssembly.Memory.grow(): Unable to grow instance memory");
        M3Result r = ResizeMemory(wrt, m, old + (u64)d);
        if (r) return JS_ThrowRangeError(c, "WebAssembly.Memory.grow(): Unable to grow instance memory");
        m->numPages = old + (u64)d;
        mem_sync(c);
        if (d == 0 && !JS_IsUndefined(mems[id].ab)) { JS_DetachArrayBuffer(c, mems[id].ab); JS_FreeValue(c, mems[id].ab); mems[id].ab = JS_UNDEFINED; }
        return JS_NewInt64(c, (int64_t)old);
    }
    JSValue a = JS_NewArray(c);
    JS_SetPropertyUint32(c, a, 0, JS_NewInt64(c, (int64_t)m->numPages));
    JS_SetPropertyUint32(c, a, 1, m->hasMax ? JS_NewInt64(c, (int64_t)m->maxPages) : JS_NewInt32(c, -1));
    JS_SetPropertyUint32(c, a, 2, JS_NewBool(c, m->isShared));
    return a;
}

static JSValue tab_val(JSContext* c, IM3Table tb, void* e) {
    if (tb->type == c_m3Type_externref) { uintptr_t h = (uintptr_t)e; if (!h || (int)(h - 1) >= nxrefs) return JS_NULL; return JS_DupValue(c, xrefs[h - 1]); }
    return fn_val(c, (IM3Function)e);
}
static int tab_in(JSContext* c, IM3Table tb, JSValueConst v, void** out) {
    if (tb->type == c_m3Type_externref) { if (JS_IsNull(v) || JS_IsUndefined(v)) { *out = 0; return 0; } int h = xref_put(c, v); if (h < 0) return -1; *out = (void*)(uintptr_t)(h + 1); return 0; }
    if (JS_IsUndefined(v)) { *out = 0; return 0; }
    if (JS_IsNull(v) || JS_IsFunction(c, v)) { JSValue id = JS_IsNull(v) ? JS_UNDEFINED : JS_GetPropertyStr(c, v, "__wfid"); int ok = JS_IsNull(v) || JS_IsNumber(id); JS_FreeValue(c, id); if (ok) return from_js(c, c_m3Type_funcref, v, out); }
    JS_ThrowTypeError(c, "WebAssembly.Table.set(): Argument 1 is invalid for table: function-typed object must be null (if nullable) or a Wasm function object");
    return -1;
}

static JSValue n_wtabop(JSContext* c, JSValueConst t, int argc, JSValueConst* argv, int magic) {
    (void)t;
    int32_t id; JS_ToInt32(c, &id, argv[0]);
    if (id < 0 || id >= ntabs) return JS_UNDEFINED;
    IM3Table tb = tabs[id].t;
    if (magic == 3) { JSValue a = JS_NewArray(c); JS_SetPropertyUint32(c, a, 0, JS_NewInt64(c, tb->size)); JS_SetPropertyUint32(c, a, 1, JS_NewInt32(c, tb->type == c_m3Type_externref)); return a; }
    int64_t i = 0; if (argc > 1) JS_ToInt64(c, &i, argv[1]);
    if (magic == 0) { if (i < 0 || i >= tb->size) return JS_ThrowRangeError(c, "WebAssembly.Table.get(): invalid address %lld in %s table of size %u", (long long)i, tb->type == c_m3Type_externref ? "externref" : "funcref", tb->size); return tab_val(c, tb, tb->elements[i]); }
    if (magic == 1) { if (i < 0 || i >= tb->size) return JS_ThrowRangeError(c, "WebAssembly.Table.set(): invalid address %lld in %s table of size %u", (long long)i, tb->type == c_m3Type_externref ? "externref" : "funcref", tb->size); void* e; if (tab_in(c, tb, argc > 2 ? argv[2] : JS_NULL, &e)) return JS_EXCEPTION; tb->elements[i] = e; return JS_UNDEFINED; }
    if (magic == 2) {
        u32 old = tb->size;
        if (i < 0 || (tb->hasMax && (u64)old + (u64)i > tb->maxSize) || (u64)old + (u64)i > 10000000) return JS_ThrowRangeError(c, "WebAssembly.Table.grow(): failed to grow table by %lld", (long long)i);
        void* e; if (tab_in(c, tb, argc > 2 ? argv[2] : JS_NULL, &e)) return JS_EXCEPTION;
        void** ne = m3_Realloc_Impl(tb->elements, sizeof(void*) * (size_t)(old + i), sizeof(void*) * old);
        if (!ne && old + i) return JS_ThrowRangeError(c, "WebAssembly.Table.grow(): failed to grow table by %lld", (long long)i);
        tb->elements = ne;
        for (u32 k = old; k < old + (u32)i; k++) tb->elements[k] = e;
        tb->size = old + (u32)i;
        return JS_NewInt64(c, old);
    }
    return JS_UNDEFINED;
}

static JSValue n_wglobop(JSContext* c, JSValueConst t, int argc, JSValueConst* argv, int magic) {
    (void)t;
    int32_t id; JS_ToInt32(c, &id, argv[0]);
    if (id < 0 || id >= nglobs) return JS_UNDEFINED;
    IM3Global g = globs[id].g;
    if (g->resolved) g = g->resolved;
    if (magic == 2) { JSValue a = JS_NewArray(c); JS_SetPropertyUint32(c, a, 0, JS_NewInt32(c, g->type)); JS_SetPropertyUint32(c, a, 1, JS_NewBool(c, g->isMutable)); return a; }
    if (magic == 0) {
        if (g->type == c_m3Type_funcref) return fn_val(c, (IM3Function)g->refValue);
        if (g->type == c_m3Type_externref) { uintptr_t h = (uintptr_t)g->refValue; if (!h || (int)(h - 1) >= nxrefs) return JS_NULL; return JS_DupValue(c, xrefs[h - 1]); }
        return to_js(c, g->type, &g->i64Value);
    }
    JSValueConst v = argc > 1 ? argv[1] : JS_UNDEFINED;
    if (g->type == c_m3Type_funcref) { void* e = 0; if (from_js(c, c_m3Type_funcref, v, &e)) return JS_EXCEPTION; g->refValue = e; return JS_UNDEFINED; }
    uint64_t tmp = 0;
    if (from_js(c, g->type, v, &tmp)) return JS_EXCEPTION;
    if (g->type == c_m3Type_externref) g->refValue = (void*)(uintptr_t)tmp;
    else if (g->type == c_m3Type_i32) g->i32Value = *(int32_t*)&tmp;
    else if (g->type == c_m3Type_f32) g->f32Value = *(float*)&tmp;
    else memcpy(&g->i64Value, &tmp, 8);
    return JS_UNDEFINED;
}

static JSValue n_winit(JSContext* c, JSValueConst t, int argc, JSValueConst* argv) {
    (void)t;
    if (argc < 4) return JS_UNDEFINED;
    e_compile = JS_DupValue(c, argv[0]); e_link = JS_DupValue(c, argv[1]); e_runtime = JS_DupValue(c, argv[2]); e_mkfn = JS_DupValue(c, argv[3]); e_ok = 1;
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry wasm_funcs[] = {
    JS_CFUNC_DEF("wasmInit", 4, n_winit),
    JS_CFUNC_DEF("wasmParse", 1, n_wparse),
    JS_CFUNC_DEF("wasmInst", 6, n_winst),
    JS_CFUNC_DEF("wasmFnInfo", 1, n_wfinfo),
    JS_CFUNC_DEF("wasmCall", 1, n_wcall),
    JS_CFUNC_DEF("wasmNewMem", 2, n_wnewmem),
    JS_CFUNC_DEF("wasmNewTab", 3, n_wnewtab),
    JS_CFUNC_DEF("wasmNewGlob", 2, n_wnewglob),
    JS_CFUNC_DEF("wasmMemBuf", 1, n_wmembuf),
    JS_CFUNC_MAGIC_DEF("wasmMemGrow", 2, n_wmemop, 0),
    JS_CFUNC_MAGIC_DEF("wasmMemInfo", 1, n_wmemop, 1),
    JS_CFUNC_MAGIC_DEF("wasmTabGet", 2, n_wtabop, 0),
    JS_CFUNC_MAGIC_DEF("wasmTabSet", 3, n_wtabop, 1),
    JS_CFUNC_MAGIC_DEF("wasmTabGrow", 3, n_wtabop, 2),
    JS_CFUNC_MAGIC_DEF("wasmTabInfo", 1, n_wtabop, 3),
    JS_CFUNC_MAGIC_DEF("wasmGlobGet", 1, n_wglobop, 0),
    JS_CFUNC_MAGIC_DEF("wasmGlobSet", 2, n_wglobop, 1),
    JS_CFUNC_MAGIC_DEF("wasmGlobInfo", 1, n_wglobop, 2),
};

void wasm_install(JSContext* c, JSValue nat) {
    wc = c;
    wenv = 0; wrt = 0; e_ok = 0;
    mods = 0; nmods = cmods = 0; mems = 0; nmems = cmems = 0; tabs = 0; ntabs = ctabs = 0; globs = 0; nglobs = cglobs = 0;
    fns = 0; nfns = cfns = 0; fh_k = 0; fh_v = 0; fh_cap = 0; xrefs = 0; nxrefs = cxrefs = 0; ninst = 0; has_pend = 0; w_depth = 0; w_deadline = 0; pend_exc = JS_UNDEFINED;
    JS_SetPropertyFunctionList(c, nat, wasm_funcs, (int)(sizeof(wasm_funcs) / sizeof(wasm_funcs[0])));
}
