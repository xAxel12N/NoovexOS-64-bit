typedef unsigned long u64;
typedef unsigned int u32;
struct vv { volatile u64 seq, ok, tsc0, mult, epoch, cpus; };
extern const struct vv nvx_vvar __attribute__((visibility("hidden")));
struct ts { long s, ns; };
struct tv { long s, us; };
static inline long sc2(long n, long a, long b){ long r; __asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b):"rcx","r11","memory"); return r; }
static inline long sc3(long n, long a, long b, long c){ long r; __asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c):"rcx","r11","memory"); return r; }
static inline u64 tsc(void){ u32 a, d; __asm__ volatile("rdtsc":"=a"(a),"=d"(d)); return ((u64)d << 32) | a; }
static int now(int clk, u64* out){
    const struct vv* v = &nvx_vvar; int rt;
    if (clk == 0 || clk == 5 || clk == 11) rt = 1; else if (clk == 1 || clk == 4 || clk == 6 || clk == 7) rt = 0; else return 0;
    for (;;) {
        u64 s = v->seq; __asm__ volatile("" ::: "memory");
        if (s & 1) continue;
        if (!v->ok) return 0;
        u64 t = tsc(), b = v->tsc0, m = v->mult, e = v->epoch;
        u64 ns = t > b ? (u64)(((unsigned __int128)(t - b) * m) >> 32) : 0;
        __asm__ volatile("" ::: "memory");
        if (v->seq != s) continue;
        *out = rt ? ns + e : ns; return 1;
    }
}
int __vdso_clock_gettime(int clk, struct ts* t){ u64 ns; if (!now(clk, &ns)) return (int)sc2(228, clk, (long)t); t->s = (long)(ns / 1000000000UL); t->ns = (long)(ns % 1000000000UL); return 0; }
int __vdso_gettimeofday(struct tv* t, void* tz){ u64 ns; if (tz || !now(0, &ns)) return (int)sc2(96, (long)t, (long)tz); if (t) { t->s = (long)(ns / 1000000000UL); t->us = (long)((ns % 1000000000UL) / 1000UL); } return 0; }
long __vdso_time(long* t){ u64 ns; if (!now(0, &ns)) return sc2(201, (long)t, 0); long s = (long)(ns / 1000000000UL); if (t) *t = s; return s; }
int __vdso_clock_getres(int clk, struct ts* r){ u64 ns; if (!now(clk, &ns)) return (int)sc2(229, clk, (long)r); if (r) { r->s = 0; r->ns = 1; } return 0; }
int __vdso_getcpu(u32* cpu, u32* node, void* c){ return (int)sc3(309, (long)cpu, (long)node, (long)c); }
int clock_gettime(int, struct ts*) __attribute__((weak, alias("__vdso_clock_gettime")));
int gettimeofday(struct tv*, void*) __attribute__((weak, alias("__vdso_gettimeofday")));
long time(long*) __attribute__((weak, alias("__vdso_time")));
int clock_getres(int, struct ts*) __attribute__((weak, alias("__vdso_clock_getres")));
int getcpu(u32*, u32*, void*) __attribute__((weak, alias("__vdso_getcpu")));
