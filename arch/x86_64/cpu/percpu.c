#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/core/kaslr.h"


_Static_assert(__builtin_offsetof(struct pcpu,kstack_top)==0,"pcpu.kstack_top");
_Static_assert(__builtin_offsetof(struct pcpu,user_rsp)==8,"pcpu.user_rsp");
_Static_assert(__builtin_offsetof(struct pcpu,self)==16,"pcpu.self");
_Static_assert(__builtin_offsetof(struct pcpu,fsbase)==24,"pcpu.fsbase");
struct pcpu g_cpu[NCPU] __attribute__((aligned(64)));

void percpu_setup(int i,u32 apic){
    struct pcpu* c=&g_cpu[i];
    c->self=c; c->id=i; c->apic_id=apic; c->jcur=-1; c->umask=022; c->ubrk=0x40002000000ULL; c->mmap_next=0x40004000000ULL;
    c->kstack_top=(u64)(c->kstack+sizeof(c->kstack))-kaslr_kstk_off();
    { extern void stk_cpu(int) __attribute__((weak)); if(stk_cpu)stk_cpu(i); }
}
void percpu_init_bsp(void){ percpu_setup(0,0); cpu_set_percpu(&g_cpu[0]); g_cpu[0].online=1; }

struct bkl g_bkl={1,0,0,1};
volatile int g_smp_on=0;
u64 g_bklw[16], g_bklh[16], g_bklt0; u32 g_bklc[16];
static inline u64 bkl_tsc(void){ u32 a,d; __asm__ volatile("rdtsc":"=a"(a),"=d"(d)); return ((u64)d<<32)|a; }
volatile int g_bkl_bsp_want, g_bkl_apq;
u32 g_bkl_apq_waits;
#define BKL_APQ_CAP 2
static u64 bkl_ap_admit(void){
    int sp=0; u32 spins=0; u64 w0=0;
    for(;;){
        u64 f=cpu_irq_save();
        int n=__atomic_load_n(&g_bkl_apq,__ATOMIC_RELAXED);
        if(!__atomic_load_n(&g_bkl_bsp_want,__ATOMIC_ACQUIRE)&&n<BKL_APQ_CAP&&__atomic_compare_exchange_n(&g_bkl_apq,&n,n+1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){ if(sp)g_bkl_apq_waits++; return f; }
        if(!sp){ PCPU->bkl_wait=1; sp=1; w0=bkl_tsc(); }
        cpu_irq_restore(f);
        for(int i=0;i<16;i++)cpu_pause();
        if(!(++spins&0xFFFFu)){ extern void frz_bkl_wait(int,int,u64) __attribute__((weak)); if(frz_bkl_wait)frz_bkl_wait(cpu_id(),g_bkl.owner,w0); }
    }
}
void bkl_lock(void){
    int me=cpu_id();
    if(g_bkl.owner==me){ g_bkl.depth++; return; }
    int ap=me!=0&&g_smp_on;
    u64 f=ap?bkl_ap_admit():cpu_irq_save();
    u32 t=__atomic_fetch_add(&g_bkl.next,1,__ATOMIC_RELAXED);
    if(!ap&&__atomic_load_n(&g_bkl.serving,__ATOMIC_ACQUIRE)!=t)__atomic_store_n(&g_bkl_bsp_want,1,__ATOMIC_RELEASE);
    if(__atomic_load_n(&g_bkl.serving,__ATOMIC_ACQUIRE)!=t){ u64 w0=bkl_tsc(); PCPU->bkl_wait=1; u32 sp=0; while(__atomic_load_n(&g_bkl.serving,__ATOMIC_ACQUIRE)!=t){ cpu_pause(); if(!(++sp&0xFFFFFu)){ extern void frz_bkl_wait(int,int,u64) __attribute__((weak)); if(frz_bkl_wait)frz_bkl_wait(me,g_bkl.owner,w0); } } g_bklw[me&15]+=bkl_tsc()-w0; g_bklc[me&15]++; }
    g_bkl.owner=me; g_bkl.depth=1; PCPU->bkl_wait=0; g_bklt0=bkl_tsc();
    if(ap)__atomic_fetch_sub(&g_bkl_apq,1,__ATOMIC_RELEASE); else if(g_bkl_bsp_want)__atomic_store_n(&g_bkl_bsp_want,0,__ATOMIC_RELEASE);
    if(PCPU->tlb_pending){ PCPU->tlb_pending=0; cpu_set_pt(cpu_get_pt()); }
    cpu_irq_restore(f);
}
void bkl_unlock(void){
    if(g_bkl.owner!=cpu_id())return;
    u64 f=cpu_irq_save();
    if(--g_bkl.depth>0){ cpu_irq_restore(f); return; }
    { u64 h=bkl_tsc()-g_bklt0; int me=cpu_id()&15; if(h>g_bklh[me])g_bklh[me]=h; }
    g_bkl.owner=-1;
    __atomic_store_n(&g_bkl.serving,g_bkl.serving+1,__ATOMIC_RELEASE);
    cpu_irq_restore(f);
}
int bkl_try(void){
    int me=cpu_id();
    if(g_bkl.owner==me){ g_bkl.depth++; return 1; }
    u32 s=__atomic_load_n(&g_bkl.serving,__ATOMIC_ACQUIRE); u32 t=s;
    if(!__atomic_compare_exchange_n(&g_bkl.next,&t,s+1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return 0;
    g_bkl.owner=me; g_bkl.depth=1; g_bklt0=bkl_tsc();
    if(PCPU->tlb_pending){ PCPU->tlb_pending=0; cpu_set_pt(cpu_get_pt()); }
    return 1; }
int bkl_held(void){ return g_bkl.owner==cpu_id(); }
int bkl_drop(void){ if(g_bkl.owner!=cpu_id())return 0; int d=g_bkl.depth; g_bkl.depth=1; bkl_unlock(); return d; }
void bkl_take(int depth){ if(depth<=0)return; bkl_lock(); g_bkl.depth=depth; }
void bkl_yield(void){ if(!g_smp_on||!bkl_held()||g_bkl.depth!=1)return; if(g_bkl.next==g_bkl.serving+1)return; bkl_unlock(); for(int i=0;i<64;i++)cpu_pause(); bkl_lock(); }
