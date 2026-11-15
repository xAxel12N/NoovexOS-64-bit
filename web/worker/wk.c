#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "quickjs.h"

extern const char nwe_wkprelude[];
int wk_k_recv(int* wid, int* type, void* buf, int cap);
int wk_k_pending(void);
int wk_k_send(int wid, int type, const void* d, int n, const void* d2, int n2);
int wk_k_term(int wid);
unsigned long wk_k_ms(void);
void nwe_arena_init(void);
void nwe_arena_reset(void);
size_t nwe_arena_size(void);
int snprintf(char* out, size_t cap, const char* fmt, ...);

long wkx_entry(long op, void* arg);
void wkx_fail(const char* why);

#define WK_N 128
#define WK_STASH 4096

enum { M_START = 1, M_MSG = 2, M_TERM = 3, M_REPLY = 4, M_TERMALL = 5 };
enum { O_MSG = 11, O_ERR = 12, O_LOG = 13, O_REQ = 14, O_CLOSED = 15 };

typedef struct { int wid; JSContext* cx; int alive; JSValue deliver, tick, reply, report; int next; } wkw;
typedef struct { int wid, type, len; unsigned char* data; } wkmsg;

static JSRuntime* rt;
static wkw W[WK_N];
static wkmsg stash[WK_STASH];
static int nstash;
static void* jb[5];
static int in_turn, broken;
static unsigned long call_t0;
static int cur_wid;
static unsigned reqseq;
static unsigned char* rbuf;
static int rcap;

void nwe_fail(const char* why) { (void)why; if (in_turn) __builtin_longjmp(jb, 1); for (;;) {} }
void wkx_fail(const char* why) { nwe_fail(why); }

static size_t wk_strnlen(const char* s, size_t m) { size_t i = 0; while (i < m && s[i]) i++; return i; }
static wkw* wk_of(JSContext* cx) { return (wkw*)JS_GetContextOpaque(cx); }
static wkw* wk_find(int wid) { for (int i = 0; i < WK_N; i++) if (W[i].alive && W[i].wid == wid) return &W[i]; return 0; }

static void send_text(int wid, int type, const char* a, const char* b) {
    int na = a ? (int)strlen(a) : 0, nb = b ? (int)strlen(b) : 0;
    if (na > 60000) na = 60000;
    if (nb > 60000) nb = 60000;
    for (int i = 0; i < 200 && wk_k_send(wid, type, a, na, b, nb) < 0; i++) {}
}

static void report_exc(wkw* w) {
    JSContext* cx = w->cx;
    JSValue e = JS_GetException(cx);
    if (JS_IsFunction(cx, w->report) && !JS_IsUncatchableError(e)) {
        JSValue r = JS_Call(cx, w->report, JS_UNDEFINED, 1, (JSValueConst*)&e);
        if (!JS_IsException(r)) { JS_FreeValue(cx, r); JS_FreeValue(cx, e); return; }
        JS_FreeValue(cx, JS_GetException(cx));
    }
    const char* m = JS_ToCString(cx, e);
    char* st = 0;
    if (JS_IsObject(e)) { JSValue s = JS_GetPropertyStr(cx, e, "stack"); if (JS_IsString(s)) { const char* t = JS_ToCString(cx, s); if (t) { size_t n = strlen(t); st = malloc(n + 2); if (st) { st[0] = '\n'; memcpy(st + 1, t, n + 1); } JS_FreeCString(cx, t); } } JS_FreeValue(cx, s); }
    if (JS_IsUncatchableError(e) && wk_k_term(w->wid)) { if (m) JS_FreeCString(cx, m); free(st); JS_FreeValue(cx, e); return; }
    char* mm = 0;
    if (m) { size_t n = strlen(m); mm = malloc(n + 10); if (mm) { memcpy(mm, "Uncaught ", 9); memcpy(mm + 9, m, n + 1); } }
    send_text(w->wid, O_ERR, mm ? mm : "Uncaught error", st);
    free(mm);
    if (m) JS_FreeCString(cx, m);
    free(st);
    JS_FreeValue(cx, e);
}

static int intr(JSRuntime* r, void* op) {
    (void)r; (void)op;
    if (cur_wid && wk_k_term(cur_wid)) return 1;
    return wk_k_ms() - call_t0 > 20000;
}

static JSValue call_fn(wkw* w, JSValue fn, int argc, JSValueConst* argv) {
    cur_wid = w->wid;
    call_t0 = wk_k_ms();
    JSValue r = JS_Call(w->cx, fn, JS_UNDEFINED, argc, argv);
    if (JS_IsException(r)) report_exc(w);
    cur_wid = 0;
    return r;
}

static void run_jobs(void) {
    for (int k = 0; k < 100000 && JS_IsJobPending(rt); k++) {
        JSContext* jc = 0;
        call_t0 = wk_k_ms();
        int r = JS_ExecutePendingJob(rt, &jc);
        if (r < 0 && jc) { wkw* w = wk_of(jc); if (w && w->alive) report_exc(w); else JS_FreeValue(jc, JS_GetException(jc)); }
        if (r <= 0) break;
    }
}

static int send_value(JSContext* cx, int wid, int type, unsigned hdr, int use_hdr, JSValueConst v) {
    size_t n = 0;
    uint8_t* p = JS_WriteObject(cx, &n, v, JS_WRITE_OBJ_REFERENCE);
    if (!p) return -1;
    int r = -1;
    if (n < (6u << 20)) for (int i = 0; i < 100000 && (r = wk_k_send(wid, type, use_hdr ? &hdr : 0, use_hdr ? 4 : 0, p, (int)n)) < 0; i++) { __builtin_ia32_pause(); }
    js_free(cx, p);
    return r;
}

static JSValue n_post(JSContext* cx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    wkw* w = wk_of(cx);
    if (!w) return JS_UNDEFINED;
    if (send_value(cx, w->wid, O_MSG, 0, 0, argc > 0 ? argv[0] : JS_UNDEFINED) < 0) {
        if (JS_HasException(cx)) return JS_EXCEPTION;
        return JS_ThrowRangeError(cx, "Failed to execute 'postMessage': the message is too large");
    }
    return JS_UNDEFINED;
}
static JSValue n_close(JSContext* cx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val; (void)argc; (void)argv;
    wkw* w = wk_of(cx);
    if (w) send_text(w->wid, O_CLOSED, "", 0);
    return JS_UNDEFINED;
}
static JSValue n_log(JSContext* cx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    wkw* w = wk_of(cx);
    if (!w || argc < 2) return JS_UNDEFINED;
    int lv = 0;
    JS_ToInt32(cx, &lv, argv[0]);
    const char* s = JS_ToCString(cx, argv[1]);
    if (s) { char t[2] = { (char)('0' + (lv & 7)), 0 }; send_text(w->wid, O_LOG, t, s); JS_FreeCString(cx, s); }
    return JS_UNDEFINED;
}
static JSValue n_err(JSContext* cx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    wkw* w = wk_of(cx);
    if (!w) return JS_UNDEFINED;
    const char* a = argc > 0 ? JS_ToCString(cx, argv[0]) : 0;
    const char* b = argc > 1 ? JS_ToCString(cx, argv[1]) : 0;
    char* bb = 0;
    if (b && *b) { size_t n = strlen(b); bb = malloc(n + 2); if (bb) { bb[0] = '\n'; memcpy(bb + 1, b, n + 1); } }
    send_text(w->wid, O_ERR, a ? a : "Uncaught error", bb);
    free(bb);
    if (a) JS_FreeCString(cx, a);
    if (b) JS_FreeCString(cx, b);
    return JS_UNDEFINED;
}
static JSValue n_req(JSContext* cx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val; (void)argc;
    wkw* w = wk_of(cx);
    if (!w) return JS_UNDEFINED;
    unsigned id = ++reqseq;
    if (send_value(cx, w->wid, O_REQ, id, 1, argv[0]) < 0) return JS_EXCEPTION;
    return JS_NewUint32(cx, id);
}

static int recv_one(int* wid, int* type) {
    for (;;) {
        int n = wk_k_recv(wid, type, rbuf, rcap);
        if (n >= -1) return n;
        int need = -n - 4;
        unsigned char* nb = realloc(rbuf, (size_t)need + 16);
        if (!nb) return -1;
        rbuf = nb;
        rcap = need + 16;
    }
}

static JSValue n_reqsync(JSContext* cx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val; (void)argc;
    wkw* w = wk_of(cx);
    if (!w) return JS_UNDEFINED;
    unsigned id = ++reqseq;
    if (send_value(cx, w->wid, O_REQ, id, 1, argv[0]) < 0) return JS_EXCEPTION;
    unsigned long t0 = wk_k_ms();
    for (;;) {
        if (wk_k_term(w->wid)) return JS_ThrowInternalError(cx, "worker terminated");
        if (wk_k_ms() - t0 > 30000) return JS_ThrowTypeError(cx, "importScripts timed out");
        int wid = 0, type = 0;
        int n = recv_one(&wid, &type);
        if (n < 0) { __builtin_ia32_pause(); continue; }
        if (type == M_REPLY && wid == w->wid && n >= 4) {
            unsigned rid; memcpy(&rid, rbuf, 4);
            if (rid == id) return JS_ReadObject(cx, rbuf + 4, (size_t)(n - 4), JS_READ_OBJ_REFERENCE);
        }
        if (nstash < WK_STASH) { wkmsg* m = &stash[nstash]; m->data = malloc((size_t)n + 1); if (m->data) { memcpy(m->data, rbuf, (size_t)n); m->wid = wid; m->type = type; m->len = n; nstash++; } }
    }
}
static JSValue n_now(JSContext* cx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewFloat64(cx, (double)wk_k_ms());
}
static JSValue n_clone(JSContext* cx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    size_t n = 0;
    uint8_t* p = JS_WriteObject(cx, &n, argc > 0 ? argv[0] : JS_UNDEFINED, JS_WRITE_OBJ_REFERENCE);
    if (!p) return JS_EXCEPTION;
    JSValue r = JS_ReadObject(cx, p, n, JS_READ_OBJ_REFERENCE);
    js_free(cx, p);
    return r;
}
static JSValue n_enc(JSContext* cx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    size_t n = 0;
    const char* s = JS_ToCStringLen(cx, &n, argc > 0 ? argv[0] : JS_UNDEFINED);
    if (!s) return JS_EXCEPTION;
    JSValue r = JS_NewUint8ArrayCopy(cx, (const uint8_t*)s, n);
    JS_FreeCString(cx, s);
    return r;
}
static JSValue n_dec(JSContext* cx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    size_t n = 0;
    uint8_t* p = argc > 0 ? JS_GetUint8Array(cx, &n, argv[0]) : 0;
    if (!p) { JS_FreeValue(cx, JS_GetException(cx)); return JS_NewString(cx, ""); }
    int fatal = argc > 1 && JS_ToBool(cx, argv[1]);
    char* o = malloc(n * 3 + 1);
    if (!o) return JS_ThrowOutOfMemory(cx);
    size_t i = 0, k = 0;
    while (i < n) {
        unsigned c = p[i];
        int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        int ok = len > 0 && i + (size_t)len <= n;
        unsigned cp = 0;
        if (ok && len > 1) {
            cp = c & (0x7F >> len);
            for (int j = 1; j < len; j++) { if ((p[i + j] & 0xC0) != 0x80) { ok = 0; break; } cp = (cp << 6) | (p[i + j] & 0x3F); }
            if (ok && ((len == 2 && cp < 0x80) || (len == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)))) ok = 0;
        }
        if (ok) { memcpy(o + k, p + i, (size_t)len); k += (size_t)len; i += (size_t)len; }
        else {
            if (fatal) { free(o); return JS_ThrowTypeError(cx, "The encoded data was not valid for encoding utf-8"); }
            o[k++] = (char)0xEF; o[k++] = (char)0xBF; o[k++] = (char)0xBD;
            i++;
            while (i < n && (p[i] & 0xC0) == 0x80 && c >= 0xC0) { i++; break; }
        }
    }
    JSValue r = JS_NewStringLen(cx, o, k);
    free(o);
    return r;
}

static const JSCFunctionListEntry wk_funcs[] = {
    JS_CFUNC_DEF("post", 1, n_post),
    JS_CFUNC_DEF("close", 0, n_close),
    JS_CFUNC_DEF("log", 2, n_log),
    JS_CFUNC_DEF("err", 2, n_err),
    JS_CFUNC_DEF("req", 1, n_req),
    JS_CFUNC_DEF("reqSync", 1, n_reqsync),
    JS_CFUNC_DEF("now", 0, n_now),
    JS_CFUNC_DEF("clone", 1, n_clone),
    JS_CFUNC_DEF("enc", 1, n_enc),
    JS_CFUNC_DEF("dec", 2, n_dec),
};

static void wk_free(wkw* w) {
    if (!w->alive) return;
    JSContext* cx = w->cx;
    JS_FreeValue(cx, w->deliver);
    JS_FreeValue(cx, w->tick);
    JS_FreeValue(cx, w->reply);
    JS_FreeValue(cx, w->report);
    JS_FreeContext(cx);
    w->alive = 0;
    w->cx = 0;
    if (rt) JS_RunGC(rt);
}

static void wk_start(int wid, const unsigned char* d, int n) {
    const char* code = (const char*)d;
    int cl = (int)wk_strnlen(code, (size_t)n);
    const char* href = cl + 1 < n ? code + cl + 1 : "";
    int hl = (int)wk_strnlen(href, (size_t)(n - (href - code)));
    const char* name = (href - code) + hl + 1 < n ? href + hl + 1 : "";
    int nl = (int)wk_strnlen(name, (size_t)(n - (name - code)));
    const char* ua = (name - code) + nl + 1 < n ? name + nl + 1 : "";
    wkw* w = 0;
    for (int i = 0; i < WK_N; i++) if (!W[i].alive) { w = &W[i]; break; }
    if (!w) { send_text(wid, O_ERR, "Too many workers are running on the second core", 0); return; }
    JSContext* cx = JS_NewContext(rt);
    if (!cx) { send_text(wid, O_ERR, "Out of memory while starting the worker", 0); return; }
    memset(w, 0, sizeof(*w));
    w->wid = wid; w->cx = cx; w->alive = 1;
    w->deliver = w->tick = w->reply = w->report = JS_UNDEFINED;
    JS_SetContextOpaque(cx, w);
    JSValue g = JS_GetGlobalObject(cx);
    JSValue o = JS_NewObject(cx);
    JS_SetPropertyFunctionList(cx, o, wk_funcs, (int)(sizeof(wk_funcs) / sizeof(wk_funcs[0])));
    JS_SetPropertyStr(cx, o, "href", JS_NewString(cx, href));
    JS_SetPropertyStr(cx, o, "name", JS_NewString(cx, name));
    JS_SetPropertyStr(cx, o, "ua", JS_NewString(cx, ua));
    JS_SetPropertyStr(cx, g, "__wk", o);
    cur_wid = wid;
    call_t0 = wk_k_ms();
    JSValue r = JS_Eval(cx, nwe_wkprelude, strlen(nwe_wkprelude), "<worker>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) { report_exc(w); JS_FreeValue(cx, g); cur_wid = 0; wk_free(w); return; }
    JS_FreeValue(cx, r);
    w->deliver = JS_GetPropertyStr(cx, g, "__wkDeliver");
    w->tick = JS_GetPropertyStr(cx, g, "__wkTick");
    w->reply = JS_GetPropertyStr(cx, g, "__wkReply");
    w->report = JS_GetPropertyStr(cx, g, "__wkReport");
    JSAtom a;
    a = JS_NewAtom(cx, "__wkDeliver"); JS_DeleteProperty(cx, g, a, 0); JS_FreeAtom(cx, a);
    a = JS_NewAtom(cx, "__wkTick"); JS_DeleteProperty(cx, g, a, 0); JS_FreeAtom(cx, a);
    a = JS_NewAtom(cx, "__wkReply"); JS_DeleteProperty(cx, g, a, 0); JS_FreeAtom(cx, a);
    a = JS_NewAtom(cx, "__wkReport"); JS_DeleteProperty(cx, g, a, 0); JS_FreeAtom(cx, a);
    a = JS_NewAtom(cx, "__wk"); JS_DeleteProperty(cx, g, a, 0); JS_FreeAtom(cx, a);
    JS_FreeValue(cx, g);
    call_t0 = wk_k_ms();
    r = JS_Eval(cx, code, (size_t)cl, href[0] ? href : "<worker>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) report_exc(w);
    JS_FreeValue(cx, r);
    cur_wid = 0;
}

static void handle(int wid, int type, unsigned char* d, int n) {
    if (type == M_START) { if (!wk_find(wid) && !wk_k_term(wid)) wk_start(wid, d, n); return; }
    if (type == M_TERMALL) { for (int i = 0; i < WK_N; i++) wk_free(&W[i]); return; }
    wkw* w = wk_find(wid);
    if (!w) return;
    if (type == M_TERM) { wk_free(w); return; }
    if (wk_k_term(wid)) return;
    if (type == M_MSG) {
        JSValue v = JS_ReadObject(w->cx, d, (size_t)n, JS_READ_OBJ_REFERENCE);
        if (JS_IsException(v)) { JS_FreeValue(w->cx, JS_GetException(w->cx)); send_text(wid, O_ERR, "DataCloneError: the message could not be read in the worker", 0); return; }
        JSValue r = call_fn(w, w->deliver, 1, (JSValueConst*)&v);
        JS_FreeValue(w->cx, r);
        JS_FreeValue(w->cx, v);
    } else if (type == M_REPLY && n >= 4) {
        unsigned rid; memcpy(&rid, d, 4);
        JSValue v = JS_ReadObject(w->cx, d + 4, (size_t)(n - 4), JS_READ_OBJ_REFERENCE);
        if (JS_IsException(v)) { JS_FreeValue(w->cx, JS_GetException(w->cx)); v = JS_NULL; }
        JSValue args[2] = { JS_NewUint32(w->cx, rid), v };
        JSValue r = call_fn(w, w->reply, 2, args);
        JS_FreeValue(w->cx, r);
        JS_FreeValue(w->cx, v);
    }
}

long wkx_entry(long op, void* arg) {
    (void)op; (void)arg;
    in_turn = 1;
    if (__builtin_setjmp(jb)) {
        in_turn = 0;
        for (int i = 0; i < WK_N; i++) if (W[i].alive) { send_text(W[i].wid, O_ERR, "The worker engine stopped after an internal error", 0); W[i].alive = 0; }
        broken = 1;
        return -1;
    }
    if (broken) { nwe_arena_reset(); rt = 0; rbuf = 0; rcap = 0; nstash = 0; memset(W, 0, sizeof(W)); broken = 0; }
    if (!rt) {
        nwe_arena_init();
        rt = JS_NewRuntime();
        if (!rt) { in_turn = 0; return -1; }
        size_t as = nwe_arena_size();
        if (as > (24u << 20)) JS_SetMemoryLimit(rt, as - (16u << 20));
        JS_SetMaxStackSize(rt, 3u << 20);
        JS_SetInterruptHandler(rt, intr, 0);
    }
    JS_UpdateStackTop(rt);
    if (!rbuf) { rcap = 1 << 16; rbuf = malloc((size_t)rcap); if (!rbuf) { in_turn = 0; return -1; } }
    unsigned long t0 = wk_k_ms();
    while (nstash > 0) {
        wkmsg m = stash[0];
        memmove(stash, stash + 1, sizeof(wkmsg) * (size_t)(nstash - 1));
        nstash--;
        handle(m.wid, m.type, m.data, m.len);
        free(m.data);
        run_jobs();
    }
    for (int k = 0; k < 4096; k++) {
        int wid = 0, type = 0;
        int n = recv_one(&wid, &type);
        if (n < 0) break;
        handle(wid, type, rbuf, n);
        run_jobs();
        if (wk_k_ms() - t0 > 25) break;
    }
    long next = -1;
    for (int i = 0; i < WK_N; i++) {
        wkw* w = &W[i];
        if (!w->alive) continue;
        JSValue r = call_fn(w, w->tick, 0, 0);
        int32_t d = -1;
        if (JS_IsNumber(r)) JS_ToInt32(w->cx, &d, r);
        JS_FreeValue(w->cx, r);
        run_jobs();
        if (d >= 0 && (next < 0 || d < next)) next = d;
    }
    if (wk_k_pending() || nstash) next = 0;
    in_turn = 0;
    return next;
}
