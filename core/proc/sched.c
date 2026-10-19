#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/arch/x86_64/user/ring3.h"
#include "include/nvx/core/proc/elf.h"
#include "include/nvx/core/proc/sched.h"
#include "include/nvx/lib/js/js.h"
/* ================= STAGE 8: cooperative scheduler (multiple ring-3 tasks) ================= */
extern void sched_swap(u64* save_ksp,u64 load_ksp); extern void user_trampoline(void);

volatile int g_preempt_on=0; u64 g_preempt_kern[8];  
struct proc g_proc[MAXPROC]; int g_curproc=-1;
u8  g_pstack[MAXPROC][16384] __attribute__((aligned(16)));
u64 g_kern_ksp=0, g_kern_cr3=0;
int proc_create(const u8* elf,u32 len){
    int pi=-1; for(int i=0;i<MAXPROC;i++) if(!g_proc[i].used){pi=i;break;} if(pi<0)return -1;
    u64 entry; u64 cr3=elf_build_space(elf,len,&entry); if(!cr3)return -1;
    u64* s=(u64*)(g_pstack[pi]+16384);
    *(--s)=0x23; *(--s)=USTK_V+0x1000ULL; *(--s)=0x202; *(--s)=0x1B; *(--s)=entry;  /* iret frame */
    *(--s)=(u64)user_trampoline;                                                     /* sched_swap ret target */
    for(int k=0;k<6;k++)*(--s)=0;                                                    /* callee-saved */
    g_proc[pi].ksp=(u64)s; g_proc[pi].cr3=cr3; g_proc[pi].used=1; g_proc[pi].alive=1; g_proc[pi].ppid=g_curproc;
    return pi;
}
static int sched_next(int after){ for(int i=1;i<=MAXPROC;i++){ int j=(after+i)%MAXPROC; if(g_proc[j].used&&g_proc[j].alive)return j; } return -1; }
void sched_run_all(void){    /* run every created proc cooperatively; returns when all exit */
    g_curproc=sched_next(-1); if(g_curproc<0)return;
    g_kern_cr3=cpu_get_pt();
    g_sched_active=1; g_tss.rsp0=(u64)(g_pstack[g_curproc]+16384);
    cpu_set_pt(g_proc[g_curproc].cr3);
    sched_swap(&g_kern_ksp, g_proc[g_curproc].ksp);      /* into the first proc; back here when all done */
    g_sched_active=0;
    cpu_set_pt(g_kern_cr3);
}
void sched_yield_now(void){
    int cur=g_curproc, nxt=sched_next(cur); if(nxt<0||nxt==cur)return;
    g_curproc=nxt; g_tss.rsp0=(u64)(g_pstack[nxt]+16384);
    cpu_set_pt(g_proc[nxt].cr3);
    sched_swap(&g_proc[cur].ksp, g_proc[nxt].ksp);
}
void sched_exit_now(void){
    int cur=g_curproc; g_proc[cur].alive=0; g_proc[cur].used=0;
    int nxt=sched_next(cur); u64 dummy;
    if(nxt<0){ cpu_set_pt(g_kern_cr3); sched_swap(&dummy, g_kern_ksp); }  /* back to kernel */
    else { g_curproc=nxt; g_tss.rsp0=(u64)(g_pstack[nxt]+16384); cpu_set_pt(g_proc[nxt].cr3); sched_swap(&dummy, g_proc[nxt].ksp); }
}
/* ================= RUNG 1: preemptive multitasking (timer-driven) ================= */
extern const unsigned char _binary_user_user_preempt_elf_start[], _binary_user_user_preempt_elf_end[];
extern const unsigned char _binary_user_user_ipc_elf_start[], _binary_user_user_ipc_elf_end[];
static int proc_create_pt(const u8* elf,u32 len,char id){
    int pi=-1; for(int i=0;i<MAXPROC;i++) if(!g_proc[i].used){pi=i;break;} if(pi<0)return -1;
    u64 entry; u64 cr3=elf_build_space(elf,len,&entry); if(!cr3)return -1;
    u64 f=pframe_alloc(); if(!f)return -1; if(vm_map(cr3,0x40000050000ULL,f,0x7ULL))return -1; *(volatile char*)f=id;   /* id byte at a fixed user vaddr */
    regs_pt* c=&g_proc[pi].ctx; for(int i=0;i<(int)(sizeof(*c)/8);i++)((u64*)c)[i]=0;
    c->rip=entry; c->cs=0x1B; c->rflags=0x202; c->rsp=USTK_V+0x1000ULL; c->ss=0x23;
    g_proc[pi].cr3=cr3; g_proc[pi].used=1; g_proc[pi].alive=1; g_proc[pi].started=1; g_proc[pi].ppid=g_curproc;
    return pi;
}
void preempt_run(void){
    if(!pf_ready)return; us_init();
    for(int i=0;i<MAXPROC;i++){g_proc[i].used=0;g_proc[i].alive=0;} g_r3out_n=0; g_r3out[0]=0;
    u32 el=(u32)(_binary_user_user_preempt_elf_end-_binary_user_user_preempt_elf_start);
    int a=proc_create_pt(_binary_user_user_preempt_elf_start,el,'1');
    int b=proc_create_pt(_binary_user_user_preempt_elf_start,el,'2');
    if(a<0||b<0)return;
    u64 kcr3; kcr3=cpu_get_pt(); g_kern_cr3=kcr3;
    g_curproc=a; g_sched_active=1;
    if(k_setjmp(g_preempt_kern)==0){
        g_preempt_on=1; g_tss.rsp0=(u64)(g_pstack[a]+16384);
        cpu_set_pt(g_proc[a].cr3);
        enter_ring3(g_proc[a].ctx.rip, g_proc[a].ctx.rsp);   /* first proc; the timer preempts from here */
    }
    g_preempt_on=0; g_sched_active=0;
    cpu_set_pt(kcr3);
    cpu_irq_on();
}
/* ================= RUNG 2+3: IPC — shared memory + message queues ================= */

#define SHM_VADDR 0x40000600000ULL

struct shmreg g_shm[SHM_MAX];
int shm_get_k(int key,int size){
    for(int i=0;i<SHM_MAX;i++) if(g_shm[i].used && g_shm[i].key==key) return i;   /* existing by key */
    int nf=(size+4095)/4096; if(nf<1)nf=1; if(nf>SHM_FRAMES)nf=SHM_FRAMES;
    int id=-1; for(int i=0;i<SHM_MAX;i++) if(!g_shm[i].used){id=i;break;} if(id<0)return -1;
    for(int f=0;f<nf;f++){ u64 pa=pframe_alloc(); if(!pa)return -1; g_shm[id].frames[f]=pa; u32* z=(u32*)pa; for(int j=0;j<1024;j++)z[j]=0; }
    g_shm[id].nframes=nf; g_shm[id].key=key; g_shm[id].used=1; return id;
}
u64 shm_map_k(int id){
    if(id<0||id>=SHM_MAX||!g_shm[id].used) return 0;
    u64 cr3; cr3=cpu_get_pt(); u64 base=SHM_VADDR+(u64)id*0x100000ULL;
    for(int f=0;f<g_shm[id].nframes;f++){ if(vm_map(cr3, base+(u64)f*4096ULL, g_shm[id].frames[f], 0x7ULL)) return 0; }
    return base;
}
#define MSGQ_MAX 6

#define MSGQ_DEPTH 32
struct msgq { u8 buf[MSGQ_DEPTH][MSG_SIZE]; int len[MSGQ_DEPTH]; int head, tail; };
static struct msgq g_msgq[MSGQ_MAX];
int msg_send_k(int q,const u8* m,int len){ if(q<0||q>=MSGQ_MAX)return -1; if(len>MSG_SIZE)len=MSG_SIZE; if(len<0)len=0;
    struct msgq* Q=&g_msgq[q]; int nh=(Q->head+1)%MSGQ_DEPTH; if(nh==Q->tail)return -1; for(int i=0;i<len;i++)Q->buf[Q->head][i]=m[i]; Q->len[Q->head]=len; Q->head=nh; return len; }
int msg_recv_k(int q,u8* out,int cap){ if(q<0||q>=MSGQ_MAX)return -1;
    struct msgq* Q=&g_msgq[q]; if(Q->head==Q->tail)return -1; int len=Q->len[Q->tail]; if(len>cap)len=cap; for(int i=0;i<len;i++)out[i]=Q->buf[Q->tail][i]; Q->tail=(Q->tail+1)%MSGQ_DEPTH; return len; }
void ipc_reset(void){ for(int i=0;i<SHM_MAX;i++)g_shm[i].used=0; for(int i=0;i<MSGQ_MAX;i++){g_msgq[i].head=0;g_msgq[i].tail=0;} }
/* run two preempt procs from a given ELF (used by the IPC demo) */
void preempt_run_elf(const u8* elf,u32 len){
    if(!pf_ready)return; us_init();
    for(int i=0;i<MAXPROC;i++){g_proc[i].used=0;g_proc[i].alive=0;} g_r3out_n=0; g_r3out[0]=0;
    int a=proc_create_pt(elf,len,'1'); int b=proc_create_pt(elf,len,'2'); if(a<0||b<0)return;
    u64 kcr3; kcr3=cpu_get_pt(); g_kern_cr3=kcr3; g_curproc=a; g_sched_active=1;
    if(k_setjmp(g_preempt_kern)==0){ g_preempt_on=1; g_tss.rsp0=(u64)(g_pstack[a]+16384);
        cpu_set_pt(g_proc[a].cr3); enter_ring3(g_proc[a].ctx.rip, g_proc[a].ctx.rsp); }
    g_preempt_on=0; g_sched_active=0; cpu_set_pt(kcr3); cpu_irq_on();
}
/* ================= RUNG 4: windowing protocol (over the IPC primitives) =================
   Message format an app sends the compositor on queue 0. The app draws its pixels into a
   shared-memory region and sends WM_CREATE naming that region; the compositor reads the
   message, reads the shared pixels, and displays them with window chrome. */

#define WM_DAMAGE 2
#define WM_CLOSE  3

extern const unsigned char _binary_user_user_wmapp_elf_start[], _binary_user_user_wmapp_elf_end[];
extern const unsigned char _binary_user_user_compdemo_elf_start[], _binary_user_user_compdemo_elf_end[];
extern const unsigned char _binary_user_user_posix_elf_start[], _binary_user_user_posix_elf_end[];
extern const unsigned char _binary_user_user_mem_elf_start[], _binary_user_user_mem_elf_end[];
void compositor_run(void){
    if(!pf_ready)return; us_init(); ipc_reset();
    for(int i=0;i<MAXPROC;i++){g_proc[i].used=0;g_proc[i].alive=0;} g_r3out_n=0; g_r3out[0]=0;
    u32 el=(u32)(_binary_user_user_compdemo_elf_end-_binary_user_user_compdemo_elf_start);
    int c=proc_create_pt(_binary_user_user_compdemo_elf_start,el,'C');
    int a=proc_create_pt(_binary_user_user_compdemo_elf_start,el,'1');
    int b=proc_create_pt(_binary_user_user_compdemo_elf_start,el,'2');
    if(c<0||a<0||b<0)return;
    u64 kcr3; kcr3=cpu_get_pt(); g_kern_cr3=kcr3; g_curproc=c; g_sched_active=1;
    if(k_setjmp(g_preempt_kern)==0){ g_preempt_on=1; g_tss.rsp0=(u64)(g_pstack[c]+16384);
        cpu_set_pt(g_proc[c].cr3); enter_ring3(g_proc[c].ctx.rip, g_proc[c].ctx.rsp); }
    g_preempt_on=0; g_sched_active=0; cpu_set_pt(kcr3); cpu_irq_on();
}
/* ================= STAGE 9: fork (duplicate the address space) ================= */
extern void user_trampoline_fork(void);
int do_fork(struct sysregs* r){
    if(!g_sched_active) return -1;
    int pi=-1; for(int i=0;i<MAXPROC;i++) if(!g_proc[i].used){pi=i;break;} if(pi<0) return -1;
    int cur=g_curproc;
    u64 child=vm_new_pml4(); if(!child) return -1;
    /* copy every mapped user page (PML4 slot 8) into the child */
    u64* ppml4=(u64*)(g_proc[cur].cr3&~0xFFFULL);
    if(ppml4[8]&1){ u64* ppdpt=(u64*)(ppml4[8]&~0xFFFULL);
        for(int i3=0;i3<512;i3++){ if(!(ppdpt[i3]&1))continue; u64* ppd=(u64*)(ppdpt[i3]&~0xFFFULL);
            for(int i2=0;i2<512;i2++){ if(!(ppd[i2]&1))continue; u64* ppt=(u64*)(ppd[i2]&~0xFFFULL);
                for(int i1=0;i1<512;i1++){ if(!(ppt[i1]&1))continue;
                    u64 v=((u64)8<<39)|((u64)i3<<30)|((u64)i2<<21)|((u64)i1<<12);
                    u64 srcpa=ppt[i1]&~0xFFFULL, f=pframe_alloc(); if(!f) return -1;
                    u8* d=(u8*)f; u8* sp=(u8*)srcpa; for(int b=0;b<4096;b++)d[b]=sp[b];
                    vm_map(child,v,f,0x7ULL); } } } }
    /* child resumes at the parent's user RIP/RSP (iret frame is above the 15 saved regs) */
    u64* fr=(u64*)r; u64 user_rip=fr[15], user_rsp=fr[18];
    u64* s=(u64*)(g_pstack[pi]+16384);
    *(--s)=0x23; *(--s)=user_rsp; *(--s)=0x202; *(--s)=0x1B; *(--s)=user_rip;
    *(--s)=(u64)user_trampoline_fork;           /* child returns rax=0 */
    for(int k=0;k<6;k++)*(--s)=0;
    g_proc[pi].ksp=(u64)s; g_proc[pi].cr3=child; g_proc[pi].used=1; g_proc[pi].alive=1; g_proc[pi].ppid=g_curproc;
    return pi;   /* parent gets the child pid */
}
/* ================= threads: a task sharing the current address space ================= */
u64 g_thread_stk_next=0x40000200000ULL;   /* per-thread user stacks */
int thread_create_k(u64 entry){
    if(!g_sched_active) return -1;
    int pi=-1; for(int i=0;i<MAXPROC;i++) if(!g_proc[i].used){pi=i;break;} if(pi<0) return -1;
    u64 cr3=g_proc[g_curproc].cr3;                        /* SAME address space (thread) */
    u64 ustk=g_thread_stk_next; g_thread_stk_next+=0x10000ULL;
    u64 sf=pframe_alloc(); if(!sf) return -1; if(vm_map(cr3,ustk,sf,0x7ULL)) return -1;
    u64* s=(u64*)(g_pstack[pi]+16384);
    *(--s)=0x23; *(--s)=ustk+0x1000ULL; *(--s)=0x202; *(--s)=0x1B; *(--s)=entry;   /* iret frame */
    *(--s)=(u64)user_trampoline; for(int k=0;k<6;k++)*(--s)=0;
    g_proc[pi].ksp=(u64)s; g_proc[pi].cr3=cr3; g_proc[pi].used=1; g_proc[pi].alive=1; g_proc[pi].ppid=g_curproc;
    return pi;
}
