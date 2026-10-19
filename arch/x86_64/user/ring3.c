#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/arch/x86_64/user/ring3.h"
#include "include/nvx/arch/x86_64/interrupts/interrupts.h"
/* ================= STAGE 4+5: ring 3 + TSS + syscalls =================
   A GDT with user (ring-3) code/data + a TSS, an INT 0x80 syscall gate, and a
   ring-3 test program (print + exit) run in its own address space. */
extern void gdt_flush(void* p); extern void tss_flush(unsigned short sel);

   /* ring-0 stack for syscalls from ring 3 */
u64 g_kjmp[8];                 /* kernel context for the exit syscall */
char g_r3out[256]; int g_r3out_n=0; int g_sched_active=0;
static int g_ufd[8]; static int g_ufd_off[8];   /* user fds + heap break + mmap region */

extern void lsys_entry(void);
static void lsys_init(void){
    lsys_kstack=(u64)(g_kstack+sizeof(g_kstack));
    cpu_wrmsr(0xC0000080,cpu_rdmsr(0xC0000080)|1ULL);
    cpu_wrmsr(0xC0000081,(0x08ULL<<32)|(0x10ULL<<48));
    cpu_wrmsr(0xC0000082,(u64)lsys_entry);
    cpu_wrmsr(0xC0000084,0x47700ULL);
}
/* ported from the Noovex9 32-bit syscall ABI (INT 0x80, number in rax) */
static void gdt_set(int i,u32 base,u32 limit,u8 access,u8 flags){
    g_gdt[i]=(u64)(limit&0xFFFF) | ((u64)(base&0xFFFFFF)<<16) | ((u64)access<<40)
           | ((u64)((limit>>16)&0xF)<<48) | ((u64)(flags&0xF)<<52) | ((u64)((base>>24)&0xFF)<<56);
}
void us_init(void){
    if(g_us_ready)return;
    gdt_set(0,0,0,0,0);
    gdt_set(1,0,0xFFFFF,0x9A,0xA);   /* kernel code (L=1) */
    gdt_set(2,0,0xFFFFF,0x92,0xC);   /* kernel data       */
    gdt_set(3,0,0xFFFFF,0xFA,0xA);   /* user code   (DPL3, L=1) */
    gdt_set(4,0,0xFFFFF,0xF2,0xC);   /* user data   (DPL3)      */
    /* TSS descriptor (16 bytes: slots 5 and 6) */
    { u64 base=(u64)&g_tss; u32 limit=sizeof(g_tss)-1;
      g_gdt[5]=(u64)(limit&0xFFFF) | ((u64)(base&0xFFFFFF)<<16) | ((u64)0x89<<40) | ((u64)((limit>>16)&0xF)<<48) | ((u64)((base>>24)&0xFF)<<56);
      g_gdt[6]=(base>>32)&0xFFFFFFFF; }
    for(int i=0;i<(int)sizeof(g_tss);i++)((u8*)&g_tss)[i]=0;
    g_tss.rsp0=(u64)(g_kstack+sizeof(g_kstack));
    g_tss.iomap=sizeof(g_tss);
    g_tss.ist[0]=(u64)(g_ist_nmi+sizeof(g_ist_nmi)); g_tss.ist[1]=(u64)(g_ist_df+sizeof(g_ist_df));
    g_gdtp.limit=sizeof(g_gdt)-1; g_gdtp.base=(u64)g_gdt;
    gdt_flush(&g_gdtp); tss_flush(0x28);
    /* INT 0x80 gate, DPL=3 so ring 3 can invoke it */
    idt_set128();
    lsys_init();
    idt_use_ist(2,1); idt_use_ist(8,2);
    for(int i=0;i<8;i++)g_ufd[i]=-1;
    g_us_ready=1;
}
/* returns 0=ok, 1=no frame pool, 2=alloc fail */
int ring3_run(void){
    if(!pf_ready) return 1;
    us_init();
    u64 sp=vm_new_pml4(); if(!sp) return 2;
    u64 cf=pframe_alloc(), sf=pframe_alloc(); if(!cf||!sf) return 2;
    u64 UCODE=0x40000000000ULL, USTK=0x40000002000ULL;   /* PML4 slot 8: no kernel-map collision */
    if(vm_map(sp,UCODE,cf,0x7ULL)||vm_map(sp,USTK,sf,0x7ULL)) return 2;   /* user-accessible */
    /* hand-assembled user program (writes into the identity-mapped code frame) */
    u8* p=(u8*)cf; int n=0; u64 MSGV=UCODE+26;
    p[n++]=0xB8; *(u32*)(p+n)=1; n+=4;                 /* mov eax,1  (print) */
    p[n++]=0x48; p[n++]=0xBB; *(u64*)(p+n)=MSGV; n+=8; /* mov rbx,MSGV */
    p[n++]=0xCD; p[n++]=0x80;                          /* int 0x80 */
    p[n++]=0xB8; *(u32*)(p+n)=0; n+=4;                 /* mov eax,0  (exit) */
    p[n++]=0xCD; p[n++]=0x80;                          /* int 0x80 */
    p[n++]=0xEB; p[n++]=0xFE;                          /* jmp . (guard) */
    { const char* m="hello from ring 3!"; int i=0; while(m[i])p[n++]=(u8)m[i++]; p[n++]=0; }
    g_r3out_n=0; g_r3out[0]=0;
    u64 kcr3; __asm__ volatile("mov %%cr3,%0":"=r"(kcr3));
    if(k_setjmp(g_kjmp)==0){
        __asm__ volatile("mov %0,%%cr3"::"r"(sp):"memory");
        enter_ring3(UCODE, USTK+0x1000ULL);           /* -> ring 3; exit longjmps back here */
    }
    __asm__ volatile("mov %0,%%cr3"::"r"(kcr3):"memory");   /* back to kernel space */
    pframe_free(cf); pframe_free(sf);
    return 0;
}
