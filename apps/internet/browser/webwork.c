#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/lib/base/mem.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/arch/x86_64/smp/smp.h"
#include "include/nvx/arch/x86_64/smp/smp_sched.h"

#define WK_RING (8u << 20)
#define WK_STACK (4u << 20)
#define WK_MAXID 4096

struct wk_ring { volatile u64 head, tail; u8* buf; };

static struct wk_ring g_in, g_out;
static u8* g_wk_stack;
static u8* g_wk_arena;
static unsigned long g_wk_arena_sz;
static volatile int g_wk_ready, g_wk_turn, g_wk_cpu = -1, g_wk_faults, g_wk_next_id = 1, g_wk_dead;
static volatile u64 g_wk_due;
static volatile u8 g_wk_term[WK_MAXID];
volatile unsigned long g_wk_turns, g_wk_turn_ms;

long nwe_call_stack(long (*fn)(long, void*), long op, void* arg, void* top);
long wkx_entry(long op, void* arg);
void wkx_fail(const char* why);
long long nwk_epoch_ms(void);
u64 hr_ns(void);
void* nwk_alloc_arena(unsigned long n);

static void ring_copy_in(struct wk_ring* r, u64 pos, const void* src, u32 n) {
    u32 o = (u32)(pos % WK_RING), a = WK_RING - o;
    if (a > n) a = n;
    memcpy(r->buf + o, src, a);
    if (n > a) memcpy(r->buf, (const u8*)src + a, n - a);
}
static void ring_copy_out(struct wk_ring* r, u64 pos, void* dst, u32 n) {
    u32 o = (u32)(pos % WK_RING), a = WK_RING - o;
    if (a > n) a = n;
    memcpy(dst, r->buf + o, a);
    if (n > a) memcpy((u8*)dst + a, r->buf, n - a);
}
static int ring_put(struct wk_ring* r, int wid, int type, const void* d, u32 n, const void* d2, u32 n2) {
    if (!r->buf) return -1;
    u32 tot = n + n2, need = 16 + ((tot + 7u) & ~7u);
    u64 h = r->head, t = r->tail;
    if (need > WK_RING - (u32)(h - t)) return -1;
    u32 hd[4] = { tot, (u32)wid, (u32)type, 0 };
    ring_copy_in(r, h, hd, 16);
    if (n) ring_copy_in(r, h + 16, d, n);
    if (n2) ring_copy_in(r, h + 16 + n, d2, n2);
    __sync_synchronize();
    r->head = h + need;
    return 0;
}
static int ring_get(struct wk_ring* r, int* wid, int* type, void* buf, int cap) {
    if (!r->buf) return -1;
    u64 t = r->tail, h = r->head;
    if (t == h) return -1;
    __sync_synchronize();
    u32 hd[4];
    ring_copy_out(r, t, hd, 16);
    if ((int)hd[0] > cap) return -(int)hd[0] - 4;
    if (hd[0]) ring_copy_out(r, t + 16, buf, hd[0]);
    *wid = (int)hd[1];
    *type = (int)hd[2];
    __sync_synchronize();
    r->tail = t + 16 + ((hd[0] + 7u) & ~7u);
    return (int)hd[0];
}

int nwk_wk_ok(void) {
    if (g_wk_dead) return 0;
    if (!g_smp_on || g_cores_online < 2 || g_ncpu < 2 || !g_cpu[1].online) return 0;
    if (g_wk_ready) return 1;
    if (!g_in.buf) g_in.buf = kmalloc(WK_RING);
    if (!g_out.buf) g_out.buf = kmalloc(WK_RING);
    if (!g_wk_stack) { g_wk_stack = kmalloc(WK_STACK); extern int mm_guard_range_bottom(void*, unsigned long); if (g_wk_stack) mm_guard_range_bottom(g_wk_stack, WK_STACK); }
    if (!g_wk_arena) { unsigned long want[] = { 256ul << 20, 192ul << 20, 128ul << 20, 96ul << 20, 64ul << 20, 48ul << 20, 32ul << 20 }; for (int i = 0; i < 7 && !g_wk_arena; i++) { g_wk_arena = nwk_alloc_arena(want[i]); if (g_wk_arena) g_wk_arena_sz = want[i]; } }
    if (!g_in.buf || !g_out.buf || !g_wk_stack || !g_wk_arena) { static int said; if (!said) { said = 1; klog("web workers: not enough memory for the second-core worker engine, workers run on the main core"); } return 0; }
    g_in.head = g_in.tail = g_out.head = g_out.tail = 0;
    __sync_synchronize();
    g_wk_ready = 1;
    { char m[80] = "web workers: running on CPU 1 with a "; int n = 0; while (m[n]) n++; unsigned long mb = g_wk_arena_sz >> 20; char d[8]; int k = 0; do { d[k++] = (char)('0' + mb % 10); mb /= 10; } while (mb && k < 7); while (k) m[n++] = d[--k]; const char* t = " MB heap"; while (*t) m[n++] = *t++; m[n] = 0; klog(m); }
    return 1;
}
int nwk_wk_new(void) {
    if (!nwk_wk_ok()) return -1;
    int id = g_wk_next_id++;
    if (g_wk_next_id >= 0x7FFFFFFF) g_wk_next_id = 1;
    g_wk_term[id % WK_MAXID] = 0;
    return id;
}
int nwk_wk_send(int wid, int type, const void* d, int n) {
    if (!g_wk_ready || n < 0) return -1;
    if (type == 3) g_wk_term[wid % WK_MAXID] = 1;
    if (type == 5) for (int i = 0; i < WK_MAXID; i++) g_wk_term[i] = 1;
    int r = ring_put(&g_in, wid, type, d, (u32)n, 0, 0);
    if (r == 0) smp_kick(1);
    return r;
}
int nwk_wk_recv(int* wid, int* type, void* buf, int cap) { if (!g_wk_ready) return -1; return ring_get(&g_out, wid, type, buf, cap); }
int nwk_wk_busy(void) { return g_wk_ready && (g_out.head != g_out.tail || g_in.head != g_in.tail || g_wk_due); }

int wk_k_recv(int* wid, int* type, void* buf, int cap) { return ring_get(&g_in, wid, type, buf, cap); }
int wk_k_pending(void) { return g_in.head != g_in.tail; }
int wk_k_send(int wid, int type, const void* d, int n, const void* d2, int n2) { return ring_put(&g_out, wid, type, d, (u32)n, d2, (u32)n2); }
int wk_k_term(int wid) { return g_wk_term[wid % WK_MAXID]; }
unsigned long wk_k_ms(void) { return (unsigned long)(hr_ns() / 1000000ULL); }
long long wk_k_epoch(void) { return nwk_epoch_ms(); }
int wk_k_tz(void) { return 0; }
void* wk_k_arena(unsigned long n) { return n <= g_wk_arena_sz ? g_wk_arena : 0; }
void wk_k_log(const char* s) { int n = 0; while (s[n] && n < 4000) n++; ring_put(&g_out, 0, 13, s, (u32)n, 0, 0); }

static void wk_fault_land(void) { wkx_fail("the worker crashed the engine (CPU exception); it was stopped safely"); for (;;) {} }
int wk_fault_redirect(u64* rip, u64* rsp) {
    if (!g_wk_turn || !g_wk_stack || cpu_id() != g_wk_cpu) return 0;
    u64 lo = (u64)g_wk_stack, hi = lo + WK_STACK, sp = *rsp;
    if (sp >= lo + 0x10000 && sp <= hi) sp -= 2048;
    else sp = lo + WK_STACK / 2;
    if (++g_wk_faults > 32) { g_wk_dead = 1; return 0; }
    *rsp = (sp & ~15ULL) - 8;
    *rip = (u64)wk_fault_land;
    return 1;
}

int wk_ap_turn(void) {
    if (!g_wk_ready || g_wk_dead || cpu_id() != 1) return 0;
    u64 now = hr_ns() / 1000000ULL;
    int has_in = g_in.head != g_in.tail;
    if (!has_in && (!g_wk_due || now < g_wk_due)) return 0;
    g_wk_cpu = cpu_id();
    g_wk_turn = 1;
    long r = nwe_call_stack(wkx_entry, 1, 0, g_wk_stack + WK_STACK - 64);
    g_wk_turn = 0;
    u64 end = hr_ns() / 1000000ULL;
    g_wk_turns++;
    g_wk_turn_ms += end - now;
    if (r < 0) g_wk_due = 0;
    else g_wk_due = end + (u64)r;
    return 1;
}
