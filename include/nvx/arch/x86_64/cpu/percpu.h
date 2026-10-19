#ifndef NVX_ARCH_X86_64_PERCPU_H
#define NVX_ARCH_X86_64_PERCPU_H

#include "include/nvx/base.h"

#define NCPU 16
struct tss64 { u32 res0; u64 rsp0,rsp1,rsp2; u64 res1; u64 ist[7]; u64 res2; u16 res3; u16 iomap; } __attribute__((packed));
struct pcpu {
    u64 kstack_top;
    u64 user_rsp;
    struct pcpu* self;
    u64 fsbase;
    int id;
    u32 apic_id;
    int jcur;
    volatile int js_running;
    u64 js_kern[8];
    u64 js_slice;
    int dcwd, uid, gid, umask;
    u64 ubrk, mmap_next;
    int us_ready;
    volatile int online, stop, parked, tlb_pending, in_user;
    volatile u64 ucr3;
    volatile u64 ticks, user_ticks, idle_ticks, switches, syscalls, ipis;
    struct tss64 tss __attribute__((aligned(16)));
    u64 gdt[7];
    struct { u16 limit; u64 base; } __attribute__((packed)) gdtp;
    u8 kstack[16384] __attribute__((aligned(16)));
    u8 jkstack[16384] __attribute__((aligned(16)));
    u8 ist_nmi[4096] __attribute__((aligned(16)));
    u8 ist_df[8192] __attribute__((aligned(16)));
};
#define PCPU ((struct pcpu*)cpu_percpu())
#define g_jcur        (PCPU->jcur)
#define g_js_running  (PCPU->js_running)
#define g_js_kern     (PCPU->js_kern)
#define g_js_slice    (PCPU->js_slice)
#define g_dcwd        (PCPU->dcwd)
#define g_uid         (PCPU->uid)
#define g_gid         (PCPU->gid)
#define g_umask       (PCPU->umask)
#define g_ubrk        (PCPU->ubrk)
#define g_mmap_next   (PCPU->mmap_next)
#define g_us_ready    (PCPU->us_ready)
#define lsys_kstack   (PCPU->kstack_top)
#define lsys_fsbase   (PCPU->fsbase)
#define g_tss         (PCPU->tss)
#define g_gdt         (PCPU->gdt)
#define g_gdtp        (PCPU->gdtp)
#define g_kstack      (PCPU->kstack)
#define g_jkstack     (PCPU->jkstack)
#define g_ist_nmi     (PCPU->ist_nmi)
#define g_ist_df      (PCPU->ist_df)
struct bkl { volatile u32 next, serving; volatile int owner; volatile int depth; };

extern struct pcpu g_cpu[NCPU] __attribute__((aligned(64)));
extern struct bkl g_bkl;
extern volatile int g_smp_on;

void percpu_setup(int i,u32 apic);
void percpu_init_bsp(void);
void bkl_lock(void);
void bkl_unlock(void);
int bkl_held(void);
int bkl_drop(void);
void bkl_take(int depth);
void bkl_yield(void);

static inline int cpu_id(void){ return PCPU->id; }

#endif
