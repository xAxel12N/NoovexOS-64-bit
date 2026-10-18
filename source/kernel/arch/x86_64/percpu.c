#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/percpu.h"


_Static_assert(__builtin_offsetof(struct pcpu,kstack_top)==0,"pcpu.kstack_top");
_Static_assert(__builtin_offsetof(struct pcpu,user_rsp)==8,"pcpu.user_rsp");
_Static_assert(__builtin_offsetof(struct pcpu,self)==16,"pcpu.self");
_Static_assert(__builtin_offsetof(struct pcpu,fsbase)==24,"pcpu.fsbase");
struct pcpu g_cpu[NCPU] __attribute__((aligned(64)));

void percpu_setup(int i,u32 apic){
    struct pcpu* c=&g_cpu[i];
    c->self=c; c->id=i; c->apic_id=apic; c->jcur=-1; c->umask=022; c->ubrk=0x40002000000ULL; c->mmap_next=0x40004000000ULL;
    c->kstack_top=(u64)(c->kstack+sizeof(c->kstack));
}
void percpu_init_bsp(void){ percpu_setup(0,0); cpu_set_percpu(&g_cpu[0]); g_cpu[0].online=1; }

struct bkl g_bkl={1,0,0,1};
volatile int g_smp_on=0;
void bkl_lock(void){
    int me=cpu_id();
    if(g_bkl.owner==me){ g_bkl.depth++; return; }
    u64 f=cpu_irq_save();
    u32 t=__atomic_fetch_add(&g_bkl.next,1,__ATOMIC_RELAXED);
    while(__atomic_load_n(&g_bkl.serving,__ATOMIC_ACQUIRE)!=t)cpu_pause();
    g_bkl.owner=me; g_bkl.depth=1;
    if(PCPU->tlb_pending){ PCPU->tlb_pending=0; cpu_set_pt(cpu_get_pt()); }
    cpu_irq_restore(f);
}
void bkl_unlock(void){
    if(g_bkl.owner!=cpu_id())return;
    if(--g_bkl.depth>0)return;
    g_bkl.owner=-1;
    __atomic_store_n(&g_bkl.serving,g_bkl.serving+1,__ATOMIC_RELEASE);
}
int bkl_held(void){ return g_bkl.owner==cpu_id(); }
int bkl_drop(void){ if(g_bkl.owner!=cpu_id())return 0; int d=g_bkl.depth; g_bkl.depth=1; bkl_unlock(); return d; }
void bkl_take(int depth){ bkl_lock(); if(depth>0)g_bkl.depth=depth; }
void bkl_yield(void){ if(!g_smp_on||!bkl_held()||g_bkl.depth!=1)return; if(g_bkl.next==g_bkl.serving+1)return; bkl_unlock(); for(int i=0;i<64;i++)cpu_pause(); bkl_lock(); }
