/* ================= STAGE 4+5: ring 3 + TSS + syscalls =================
   A GDT with user (ring-3) code/data + a TSS, an INT 0x80 syscall gate, and a
   ring-3 test program (print + exit) run in its own address space. */
extern void gdt_flush(void* p); extern void tss_flush(unsigned short sel);
extern void isr128(void); extern void enter_ring3(u64 rip,u64 rsp); extern void enter_ring3_args(u64 rip,u64 rsp,u64 argc,u64 argv);
extern int  k_setjmp(u64* buf); extern void k_longjmp(u64* buf,int v);
struct tss64 { u32 res0; u64 rsp0,rsp1,rsp2; u64 res1; u64 ist[7]; u64 res2; u16 res3; u16 iomap; } __attribute__((packed));
static struct tss64 g_tss;
static u64 g_gdt[7];                 /* null,kcode,kdata,ucode,udata,tss(2) */
static struct { u16 limit; u64 base; } __attribute__((packed)) g_gdtp;
static u8  g_kstack[16384] __attribute__((aligned(16)));   /* ring-0 stack for syscalls from ring 3 */
static u64 g_kjmp[8];                 /* kernel context for the exit syscall */
static char g_r3out[256]; static int g_r3out_n=0; static int g_us_ready=0; static int g_sched_active=0;
static int g_ufd[8]; static int g_ufd_off[8]; static u64 g_ubrk=0x40002000000ULL; static u64 g_mmap_next=0x40004000000ULL;   /* user fds + heap break + mmap region */
static void sched_yield_now(void); static void sched_exit_now(void); static int thread_create_k(u64);
static int shm_get_k(int,int); static u64 shm_map_k(int); static int msg_send_k(int,const u8*,int); static int msg_recv_k(int,u8*,int); static void wm_demo(void);
struct sysregs { u64 r15,r14,r13,r12,r11,r10,r9,r8,rbp,rdi,rsi,rdx,rcx,rbx,rax; };
static int do_fork(struct sysregs*);
/* ported from the Noovex9 32-bit syscall ABI (INT 0x80, number in rax) */
static void gdt_set(int i,u32 base,u32 limit,u8 access,u8 flags){
    g_gdt[i]=(u64)(limit&0xFFFF) | ((u64)(base&0xFFFFFF)<<16) | ((u64)access<<40)
           | ((u64)((limit>>16)&0xF)<<48) | ((u64)(flags&0xF)<<52) | ((u64)((base>>24)&0xFF)<<56);
}
static void us_init(void){
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
    g_gdtp.limit=sizeof(g_gdt)-1; g_gdtp.base=(u64)g_gdt;
    gdt_flush(&g_gdtp); tss_flush(0x28);
    /* INT 0x80 gate, DPL=3 so ring 3 can invoke it */
    idt_set128();
    for(int i=0;i<8;i++)g_ufd[i]=-1;
    g_us_ready=1;
}
/* returns 0=ok, 1=no frame pool, 2=alloc fail */
static int ring3_run(void){
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
/* ================= STAGE 6: ELF loader for user programs =================
   Parse an ELF64, map its PT_LOAD segments into a fresh user address space at
   their virtual addresses (user-accessible), set up a user stack, run in ring 3. */
extern const unsigned char _binary_user_user_demo_elf_start[], _binary_user_user_demo_elf_end[];
extern const unsigned char _binary_user_user_fork_elf_start[], _binary_user_user_fork_elf_end[];
extern const unsigned char _binary_user_user_app_elf_start[], _binary_user_user_app_elf_end[];
extern const unsigned char _binary_user_user_thread_elf_start[], _binary_user_user_thread_elf_end[];
#define USTK_V   0x40000100000ULL
/* build an address space from an ELF; returns cr3 (0 on failure), sets *entry */
static char g_exec_args[128]={0}; static u64 g_arg_rsp=0,g_arg_argv=0; static int g_arg_argc=0;
static u64 elf_build_space(const u8* elf,u32 len,u64* entry){
    if(len<64 || elf[0]!=0x7F||elf[1]!='E'||elf[2]!='L'||elf[3]!='F') return 0;
    u64 ent=*(const u64*)(elf+24), phoff=*(const u64*)(elf+32);
    u16 phentsz=*(const u16*)(elf+54), phnum=*(const u16*)(elf+56);
    u64 sp=vm_new_pml4(); if(!sp) return 0;
    for(int i=0;i<phnum;i++){ const u8* ph=elf+phoff+(u64)i*phentsz; if(*(const u32*)(ph+0)!=1) continue;
        u64 off=*(const u64*)(ph+8), va=*(const u64*)(ph+16), filesz=*(const u64*)(ph+32), memsz=*(const u64*)(ph+40);
        u64 vs=va&~0xFFFULL, ve=(va+memsz+0xFFF)&~0xFFFULL;
        for(u64 v=vs; v<ve; v+=0x1000){ u64 f=pframe_alloc(); if(!f) return 0; if(vm_map(sp,v,f,0x7ULL)) return 0;
            u8* dst=(u8*)f; for(int b=0;b<4096;b++){ u64 a=v+(u64)b; dst[b]=(a>=va&&a<va+filesz)?elf[off+(a-va)]:0; } } }
    u64 sf=pframe_alloc(); if(!sf) return 0; if(vm_map(sp,USTK_V,sf,0x7ULL)) return 0;
    /* Lay out argc/argv at the top of the user stack frame (sf is the physical page for USTK_V). */
    /* We build: [argv strings][argv[] ptr array][argc] and set g_arg_rsp accordingly. */
    {
        u8* page=(u8*)sf;                 /* kernel-mapped physical page == user USTK_V */
        int argc=0; char* argp[8]; u64 uptr[8];
        /* parse g_exec_args (space separated, program name is argv[0]) into the page top-down */
        int top=4096-8;                   /* leave a little slack */
        const char* a=g_exec_args;
        /* argv[0] = program name */
        /* copy each token */
        char tok[64];
        while(argc<8){
            while(*a==' ')a++;
            if(!*a)break;
            int tl=0; while(*a&&*a!=' '&&tl<63)tok[tl++]=*a++; tok[tl]=0;
            top-=(tl+1); if(top<64)break;
            for(int i=0;i<tl;i++)page[top+i]=(u8)tok[i]; page[top+tl]=0;
            argp[argc]=(char*)&page[top]; uptr[argc]=USTK_V+(u64)top; argc++;
        }
        /* align down for the pointer array */
        top &= ~0xF;
        top-=8; for(int i=0;i<8;i++)page[top+i]=0;                 /* argv[argc]=NULL */
        for(int i=argc-1;i>=0;i--){ top-=8; *(u64*)&page[top]=uptr[i]; }
        u64 argv_uptr=USTK_V+(u64)top;
        top-=8; *(u64*)&page[top]=(u64)argc;                        /* argc on stack top */
        g_arg_rsp=USTK_V+(u64)top;
        g_arg_argc=argc; g_arg_argv=argv_uptr;
    }
    *entry=ent; return sp;
}
/* single-run (no scheduler): load + run one ELF, exit returns to the kernel via longjmp */
static void term_puts(const char*); static void term_putc(char);
/* ================= relocation loader (PIE / relocatable ELF modules) =================
   Loads a position-independent ELF64 (ET_DYN) into kernel-accessible memory at a
   runtime base, applies R_X86_64_RELATIVE relocations, and resolves imported
   symbols against a small kernel-exported symbol table. This is the core of a
   dynamic loader for the tractable case: load a module and link it to the host. */

/* --- kernel symbol table: what a loaded module is allowed to call --- */
typedef struct { const char* name; void* addr; } nvx_ksym;
static int  ksym_puts(const char* s){ term_puts(s); return 0; }
static void ksym_putc(int c){ term_putc((char)c); }
static u32  ksym_ticks(void){ return (u32)g_ticks; }
static void* ksym_alloc(u32 n){ return (void*)(u64)dma_alloc(n); }
static const nvx_ksym NVX_KSYMS[]={
    {"nvx_puts",(void*)ksym_puts},
    {"nvx_putc",(void*)ksym_putc},
    {"nvx_ticks",(void*)ksym_ticks},
    {"nvx_alloc",(void*)ksym_alloc},
    {0,0}
};
static void* ksym_find(const char* name){ for(int i=0;NVX_KSYMS[i].name;i++){ const char* a=NVX_KSYMS[i].name,*b=name; int e=1; while(*a||*b){ if(*a!=*b){e=0;break;} a++;b++; } if(e)return NVX_KSYMS[i].addr; } return 0; }

/* module handle */
typedef struct { u8* base; u64 size; u64 entry; int ok; } nvx_module;

/* Load a relocatable ELF64 from a buffer. Returns 0 on success, fills mod. */
static int nvx_dlload(const u8* elf,u32 len,nvx_module* mod){
    mod->ok=0;
    if(len<64||elf[0]!=0x7F||elf[1]!='E'||elf[2]!='L'||elf[3]!='F')return -1;
    u16 etype=*(const u16*)(elf+16);            /* 2=ET_EXEC 3=ET_DYN */
    u64 phoff=*(const u64*)(elf+32); u16 phentsz=*(const u16*)(elf+54), phnum=*(const u16*)(elf+56);
    /* compute the module's virtual span */
    u64 lo=~0ULL, hi=0;
    for(int i=0;i<phnum;i++){ const u8* ph=elf+phoff+(u64)i*phentsz; if(*(const u32*)(ph+0)!=1)continue;
        u64 va=*(const u64*)(ph+16), memsz=*(const u64*)(ph+40); if(va<lo)lo=va; if(va+memsz>hi)hi=va+memsz; }
    if(hi<=lo)return -2;
    u64 span=(hi-lo+0xFFF)&~0xFFFULL;
    /* allocate one contiguous region from the DMA arena (identity-mapped, executable) */
    u8* base=(u8*)(u64)dma_alloc((u32)span+4096);
    if(!base)return -3;
    for(u64 i=0;i<span;i++)base[i]=0;
    /* copy PT_LOAD segments to base + (va - lo) */
    for(int i=0;i<phnum;i++){ const u8* ph=elf+phoff+(u64)i*phentsz; if(*(const u32*)(ph+0)!=1)continue;
        u64 off=*(const u64*)(ph+8), va=*(const u64*)(ph+16), filesz=*(const u64*)(ph+32);
        for(u64 b=0;b<filesz;b++)base[(va-lo)+b]=elf[off+b]; }
    u64 load_bias=(u64)base - lo;              /* runtime base for a PIE loaded at 'base' */

    /* find PT_DYNAMIC to get relocations + symbol tables */
    u64 dyn_off=0,dyn_sz=0;
    for(int i=0;i<phnum;i++){ const u8* ph=elf+phoff+(u64)i*phentsz; if(*(const u32*)(ph+0)==2){ dyn_off=*(const u64*)(ph+16); dyn_sz=*(const u64*)(ph+40); break; } }
    u64 rela=0,relasz=0,relaent=24, symtab=0,strtab=0,syment=24, jmprel=0,pltrelsz=0;
    if(dyn_off){ const u8* d=base+(dyn_off-lo);
        for(u64 o=0;o+16<=dyn_sz;o+=16){ u64 tag=*(const u64*)(d+o), val=*(const u64*)(d+o+8);
            if(tag==0)break;
            if(tag==7)rela=val;         /* DT_RELA */
            else if(tag==8)relasz=val;  /* DT_RELASZ */
            else if(tag==9)relaent=val; /* DT_RELAENT */
            else if(tag==6)symtab=val;  /* DT_SYMTAB */
            else if(tag==5)strtab=val;  /* DT_STRTAB */
            else if(tag==11)syment=val; /* DT_SYMENT */
            else if(tag==23)jmprel=val; /* DT_JMPREL */
            else if(tag==2)pltrelsz=val;/* DT_PLTRELSZ */
        }
    }
    /* apply relocations */
    if(rela&&relasz){ const u8* rp=base+(rela-lo);
        for(u64 o=0;o+relaent<=relasz;o+=relaent){
            u64 r_off=*(const u64*)(rp+o); u64 r_info=*(const u64*)(rp+o+8); i64 r_add=*(const i64*)(rp+o+16);
            u32 rtype=(u32)(r_info&0xffffffff); u32 rsym=(u32)(r_info>>32);
            u64* slot=(u64*)(base+(r_off-lo));
            if(rtype==8){ *slot=load_bias+(u64)r_add; }            /* R_X86_64_RELATIVE */
            else if(rtype==1||rtype==6||rtype==7){                 /* 64 / GLOB_DAT / JUMP_SLOT */
                if(symtab&&strtab){ const u8* sym=base+(symtab-lo)+(u64)rsym*syment; u32 nameoff=*(const u32*)(sym+0);
                    const char* nm=(const char*)(base+(strtab-lo)+nameoff); void* a=ksym_find(nm);
                    *slot=a?(u64)a:0; }
            }
        }
    }
    /* PLT relocations (DT_JMPREL): JUMP_SLOT entries for imported functions */
    if(jmprel&&pltrelsz){ const u8* rp=base+(jmprel-lo);
        for(u64 o=0;o+24<=pltrelsz;o+=24){
            u64 r_off=*(const u64*)(rp+o); u64 r_info=*(const u64*)(rp+o+8);
            u32 rsym=(u32)(r_info>>32); u64* slot=(u64*)(base+(r_off-lo));
            if(symtab&&strtab){ const u8* sym=base+(symtab-lo)+(u64)rsym*syment; u32 nameoff=*(const u32*)(sym+0);
                const char* nm=(const char*)(base+(strtab-lo)+nameoff); void* a=ksym_find(nm);
                *slot=a?(u64)a:0; }
        }
    }
    mod->base=base; mod->size=span; mod->entry=load_bias+(*(const u64*)(elf+24)); mod->ok=1;
    (void)etype; (void)syment;
    return 0;
}
/* call a loaded module's entry as int entry(void) */
static int nvx_dlcall(nvx_module* mod){ if(!mod->ok)return -1; int(*fn)(void)=(int(*)(void))mod->entry; return fn(); }

static int elf_load_run(const u8* elf,u32 len){
    if(!pf_ready) return 1; us_init(); u64 entry; u64 sp=elf_build_space(elf,len,&entry); if(!sp) return 2;
    g_r3out_n=0; g_r3out[0]=0; g_sched_active=0;
    u64 kcr3; __asm__ volatile("mov %%cr3,%0":"=r"(kcr3));
    if(k_setjmp(g_kjmp)==0){ __asm__ volatile("mov %0,%%cr3"::"r"(sp):"memory"); enter_ring3_args(entry, g_arg_rsp?g_arg_rsp:USTK_V+0x1000ULL, (u64)g_arg_argc, g_arg_argv); }
    __asm__ volatile("mov %0,%%cr3"::"r"(kcr3):"memory");
    return 0;
}
/* ================= STAGE 8: cooperative scheduler (multiple ring-3 tasks) ================= */
extern void sched_swap(u64* save_ksp,u64 load_ksp); extern void user_trampoline(void);
typedef struct { u64 r15,r14,r13,r12,r11,r10,r9,r8,rbp,rdi,rsi,rdx,rcx,rbx,rax; u64 vector,errcode,rip,cs,rflags,rsp,ss; } regs_pt;
#define MAXPROC 6
struct proc { int used, alive; u64 cr3, ksp; regs_pt ctx; int started; int exit_status; int zombie; int ppid; };
static volatile int g_preempt_on=0; static u64 g_preempt_kern[8]; extern int k_setjmp(u64*); extern void k_longjmp(u64*,int);
static struct proc g_proc[MAXPROC]; static int g_curproc=-1;
static u8  g_pstack[MAXPROC][16384] __attribute__((aligned(16)));
static u64 g_kern_ksp=0, g_kern_cr3=0;
static int proc_create(const u8* elf,u32 len){
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
static void sched_run_all(void){    /* run every created proc cooperatively; returns when all exit */
    g_curproc=sched_next(-1); if(g_curproc<0)return;
    __asm__ volatile("mov %%cr3,%0":"=r"(g_kern_cr3));
    g_sched_active=1; g_tss.rsp0=(u64)(g_pstack[g_curproc]+16384);
    __asm__ volatile("mov %0,%%cr3"::"r"(g_proc[g_curproc].cr3):"memory");
    sched_swap(&g_kern_ksp, g_proc[g_curproc].ksp);      /* into the first proc; back here when all done */
    g_sched_active=0;
    __asm__ volatile("mov %0,%%cr3"::"r"(g_kern_cr3):"memory");
}
static void sched_yield_now(void){
    int cur=g_curproc, nxt=sched_next(cur); if(nxt<0||nxt==cur)return;
    g_curproc=nxt; g_tss.rsp0=(u64)(g_pstack[nxt]+16384);
    __asm__ volatile("mov %0,%%cr3"::"r"(g_proc[nxt].cr3):"memory");
    sched_swap(&g_proc[cur].ksp, g_proc[nxt].ksp);
}
static void sched_exit_now(void){
    int cur=g_curproc; g_proc[cur].alive=0; g_proc[cur].used=0;
    int nxt=sched_next(cur); u64 dummy;
    if(nxt<0){ __asm__ volatile("mov %0,%%cr3"::"r"(g_kern_cr3):"memory"); sched_swap(&dummy, g_kern_ksp); }  /* back to kernel */
    else { g_curproc=nxt; g_tss.rsp0=(u64)(g_pstack[nxt]+16384); __asm__ volatile("mov %0,%%cr3"::"r"(g_proc[nxt].cr3):"memory"); sched_swap(&dummy, g_proc[nxt].ksp); }
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
static void preempt_run(void){
    if(!pf_ready)return; us_init();
    for(int i=0;i<MAXPROC;i++){g_proc[i].used=0;g_proc[i].alive=0;} g_r3out_n=0; g_r3out[0]=0;
    u32 el=(u32)(_binary_user_user_preempt_elf_end-_binary_user_user_preempt_elf_start);
    int a=proc_create_pt(_binary_user_user_preempt_elf_start,el,'1');
    int b=proc_create_pt(_binary_user_user_preempt_elf_start,el,'2');
    if(a<0||b<0)return;
    u64 kcr3; __asm__ volatile("mov %%cr3,%0":"=r"(kcr3)); g_kern_cr3=kcr3;
    g_curproc=a; g_sched_active=1;
    if(k_setjmp(g_preempt_kern)==0){
        g_preempt_on=1; g_tss.rsp0=(u64)(g_pstack[a]+16384);
        __asm__ volatile("mov %0,%%cr3"::"r"(g_proc[a].cr3):"memory");
        enter_ring3(g_proc[a].ctx.rip, g_proc[a].ctx.rsp);   /* first proc; the timer preempts from here */
    }
    g_preempt_on=0; g_sched_active=0;
    __asm__ volatile("mov %0,%%cr3"::"r"(kcr3):"memory");
    __asm__ volatile("sti");
}
/* ================= RUNG 2+3: IPC — shared memory + message queues ================= */
#define SHM_MAX 4
#define SHM_FRAMES 24
#define SHM_VADDR 0x40000600000ULL
struct shmreg { int used, key, nframes; u64 frames[SHM_FRAMES]; };
static struct shmreg g_shm[SHM_MAX];
static int shm_get_k(int key,int size){
    for(int i=0;i<SHM_MAX;i++) if(g_shm[i].used && g_shm[i].key==key) return i;   /* existing by key */
    int nf=(size+4095)/4096; if(nf<1)nf=1; if(nf>SHM_FRAMES)nf=SHM_FRAMES;
    int id=-1; for(int i=0;i<SHM_MAX;i++) if(!g_shm[i].used){id=i;break;} if(id<0)return -1;
    for(int f=0;f<nf;f++){ u64 pa=pframe_alloc(); if(!pa)return -1; g_shm[id].frames[f]=pa; u32* z=(u32*)pa; for(int j=0;j<1024;j++)z[j]=0; }
    g_shm[id].nframes=nf; g_shm[id].key=key; g_shm[id].used=1; return id;
}
static u64 shm_map_k(int id){
    if(id<0||id>=SHM_MAX||!g_shm[id].used) return 0;
    u64 cr3; __asm__ volatile("mov %%cr3,%0":"=r"(cr3)); u64 base=SHM_VADDR+(u64)id*0x100000ULL;
    for(int f=0;f<g_shm[id].nframes;f++){ if(vm_map(cr3, base+(u64)f*4096ULL, g_shm[id].frames[f], 0x7ULL)) return 0; }
    return base;
}
#define MSGQ_MAX 6
#define MSG_SIZE 64
#define MSGQ_DEPTH 32
struct msgq { u8 buf[MSGQ_DEPTH][MSG_SIZE]; int len[MSGQ_DEPTH]; int head, tail; };
static struct msgq g_msgq[MSGQ_MAX];
static int msg_send_k(int q,const u8* m,int len){ if(q<0||q>=MSGQ_MAX)return -1; if(len>MSG_SIZE)len=MSG_SIZE; if(len<0)len=0;
    struct msgq* Q=&g_msgq[q]; int nh=(Q->head+1)%MSGQ_DEPTH; if(nh==Q->tail)return -1; for(int i=0;i<len;i++)Q->buf[Q->head][i]=m[i]; Q->len[Q->head]=len; Q->head=nh; return len; }
static int msg_recv_k(int q,u8* out,int cap){ if(q<0||q>=MSGQ_MAX)return -1;
    struct msgq* Q=&g_msgq[q]; if(Q->head==Q->tail)return -1; int len=Q->len[Q->tail]; if(len>cap)len=cap; for(int i=0;i<len;i++)out[i]=Q->buf[Q->tail][i]; Q->tail=(Q->tail+1)%MSGQ_DEPTH; return len; }
static void ipc_reset(void){ for(int i=0;i<SHM_MAX;i++)g_shm[i].used=0; for(int i=0;i<MSGQ_MAX;i++){g_msgq[i].head=0;g_msgq[i].tail=0;} }
/* run two preempt procs from a given ELF (used by the IPC demo) */
static void preempt_run_elf(const u8* elf,u32 len){
    if(!pf_ready)return; us_init();
    for(int i=0;i<MAXPROC;i++){g_proc[i].used=0;g_proc[i].alive=0;} g_r3out_n=0; g_r3out[0]=0;
    int a=proc_create_pt(elf,len,'1'); int b=proc_create_pt(elf,len,'2'); if(a<0||b<0)return;
    u64 kcr3; __asm__ volatile("mov %%cr3,%0":"=r"(kcr3)); g_kern_cr3=kcr3; g_curproc=a; g_sched_active=1;
    if(k_setjmp(g_preempt_kern)==0){ g_preempt_on=1; g_tss.rsp0=(u64)(g_pstack[a]+16384);
        __asm__ volatile("mov %0,%%cr3"::"r"(g_proc[a].cr3):"memory"); enter_ring3(g_proc[a].ctx.rip, g_proc[a].ctx.rsp); }
    g_preempt_on=0; g_sched_active=0; __asm__ volatile("mov %0,%%cr3"::"r"(kcr3):"memory"); __asm__ volatile("sti");
}
/* ================= RUNG 4: windowing protocol (over the IPC primitives) =================
   Message format an app sends the compositor on queue 0. The app draws its pixels into a
   shared-memory region and sends WM_CREATE naming that region; the compositor reads the
   message, reads the shared pixels, and displays them with window chrome. */
#define WM_CREATE 1
#define WM_DAMAGE 2
#define WM_CLOSE  3
struct wmsg { int type,w,h,key,client,x,y,btn; };
extern const unsigned char _binary_user_user_wmapp_elf_start[], _binary_user_user_wmapp_elf_end[];
extern const unsigned char _binary_user_user_compdemo_elf_start[], _binary_user_user_compdemo_elf_end[];
extern const unsigned char _binary_user_user_posix_elf_start[], _binary_user_user_posix_elf_end[];
extern const unsigned char _binary_user_user_mem_elf_start[], _binary_user_user_mem_elf_end[];
static void compositor_run(void){
    if(!pf_ready)return; us_init(); ipc_reset();
    for(int i=0;i<MAXPROC;i++){g_proc[i].used=0;g_proc[i].alive=0;} g_r3out_n=0; g_r3out[0]=0;
    u32 el=(u32)(_binary_user_user_compdemo_elf_end-_binary_user_user_compdemo_elf_start);
    int c=proc_create_pt(_binary_user_user_compdemo_elf_start,el,'C');
    int a=proc_create_pt(_binary_user_user_compdemo_elf_start,el,'1');
    int b=proc_create_pt(_binary_user_user_compdemo_elf_start,el,'2');
    if(c<0||a<0||b<0)return;
    u64 kcr3; __asm__ volatile("mov %%cr3,%0":"=r"(kcr3)); g_kern_cr3=kcr3; g_curproc=c; g_sched_active=1;
    if(k_setjmp(g_preempt_kern)==0){ g_preempt_on=1; g_tss.rsp0=(u64)(g_pstack[c]+16384);
        __asm__ volatile("mov %0,%%cr3"::"r"(g_proc[c].cr3):"memory"); enter_ring3(g_proc[c].ctx.rip, g_proc[c].ctx.rsp); }
    g_preempt_on=0; g_sched_active=0; __asm__ volatile("mov %0,%%cr3"::"r"(kcr3):"memory"); __asm__ volatile("sti");
}
/* ================= STAGE 9: fork (duplicate the address space) ================= */
extern void user_trampoline_fork(void);
static int do_fork(struct sysregs* r){
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
static u64 g_thread_stk_next=0x40000200000ULL;   /* per-thread user stacks */
static int thread_create_k(u64 entry){
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
/* ================= multi-core: detect CPU cores via ACPI MADT (read-only) ================= */
static int acpi_count_cores(void){
    /* find RSDP ("RSD PTR ") in the EBDA + BIOS area */
    u64 rsdp=0;
    for(u64 a=0x000E0000; a<0x00100000; a+=16){ const char* p=(const char*)a;
        if(p[0]=='R'&&p[1]=='S'&&p[2]=='D'&&p[3]==' '&&p[4]=='P'&&p[5]=='T'&&p[6]=='R'&&p[7]==' '){ rsdp=a; break; } }
    if(!rsdp) return 0;
    u8 rev=*(u8*)(rsdp+15); u64 sdt; int use64=0;
    if(rev>=2){ sdt=*(u32*)(rsdp+24); if(*(u64*)(rsdp+24)) sdt=*(u64*)(rsdp+24); use64=1; }   /* XSDT */
    else sdt=*(u32*)(rsdp+16);                                                                  /* RSDT */
    if(!sdt||sdt>=0xF0000000ULL) return 0;
    u32 len=*(u32*)(sdt+4); int n=(int)((len-36)/(use64?8:4));
    u64 madt=0;
    for(int i=0;i<n && i<64;i++){ u64 e=use64?*(u64*)(sdt+36+(u64)i*8):(u64)*(u32*)(sdt+36+(u64)i*4);
        if(!e||e>=0xF0000000ULL)continue; const char* sig=(const char*)e;
        if(sig[0]=='A'&&sig[1]=='P'&&sig[2]=='I'&&sig[3]=='C'){ madt=e; break; } }
    if(!madt) return 0;
    u32 mlen=*(u32*)(madt+4); u64 p=madt+44; int cores=0;
    while(p < madt+mlen){ u8 type=*(u8*)p, elen=*(u8*)(p+1); if(elen==0)break;
        if(type==0){ u8 flags=*(u8*)(p+4); if(flags&1)cores++; }   /* Local APIC, enabled */
        p+=elen; }
    return cores;
}
/* ================= SMP: start the secondary cores (opt-in, parks them) ================= */
extern const unsigned char ap_tramp_start[], ap_tramp_end[];
static volatile int g_cores_online=1;                 /* BSP counts as 1 */
static u8 g_ap_stacks[7][8192] __attribute__((aligned(16)));   /* up to 7 secondary cores */
/* ---- a lock-protected work queue so the extra cores do REAL parallel work ----
   Jobs are self-contained compute (touch only their own struct + stack), so this
   is safe without making the whole kernel SMP-safe: one spinlock guards claiming. */
#define NJOBS 16
struct sjob { volatile int state; u64 arg; volatile u64 result; volatile int core; };  /* state: 0 empty,1 pending,2 running,3 done */
static struct sjob g_jobs[NJOBS];
static volatile int g_smp_lock=0;
static void smp_lock(void){ while(__sync_lock_test_and_set(&g_smp_lock,1)) __asm__ volatile("pause"); }
static void smp_unlock(void){ __sync_lock_release(&g_smp_lock); }
static u64 smp_compute(u64 n){ u64 c=0; for(u64 i=2;i<=n;i++){ int p=1; for(u64 j=2;j*j<=i;j++) if(i%j==0){p=0;break;} if(p)c++; } return c; }  /* count primes <= n (pure) */
static int smp_submit(u64 arg){ int id=-1; smp_lock();
    for(int i=0;i<NJOBS;i++) if(g_jobs[i].state==0){ g_jobs[i].arg=arg; g_jobs[i].result=0; g_jobs[i].core=-1; g_jobs[i].state=1; id=i; break; } smp_unlock(); return id; }
static volatile int g_smp_work_on=0;   /* APs only enter the work loop when this is set */
static void ap_main(void){
    __sync_fetch_and_add(&g_cores_online,1);
    int myid=g_cores_online;   /* approximate core index */
    /* stay parked until the BSP turns the work queue on (keeps boot safe) */
    while(!g_smp_work_on) __asm__ volatile("pause");
    for(;;){ int found=-1;
        smp_lock();
        for(int i=0;i<NJOBS;i++) if(g_jobs[i].state==1){ g_jobs[i].state=2; g_jobs[i].core=myid; found=i; break; }
        smp_unlock();
        if(found>=0){ u64 r=smp_compute(g_jobs[found].arg); g_jobs[found].result=r; __sync_synchronize(); g_jobs[found].state=3; }
        else __asm__ volatile("pause");
    }
}
static u64 g_lapic_base=0xFEE00000ULL;
static inline u32 lapic_rd(u32 off){ return *(volatile u32*)(g_lapic_base+off); }
static inline void lapic_wr(u32 off,u32 v){ *(volatile u32*)(g_lapic_base+off)=v; }
static void ap_udelay(u32 loops){ for(volatile u32 i=0;i<loops*1000u;i++)__asm__ volatile("pause"); }
/* fill out[] with the APIC IDs of enabled cores from the MADT; returns count */
static int madt_apic_ids(u8* out,int max){
    u64 rsdp=0; for(u64 a=0x000E0000; a<0x00100000; a+=16){ const char* p=(const char*)a;
        if(p[0]=='R'&&p[1]=='S'&&p[2]=='D'&&p[3]==' '&&p[4]=='P'&&p[5]=='T'&&p[6]=='R'&&p[7]==' '){ rsdp=a; break; } }
    if(!rsdp)return 0; u8 rev=*(u8*)(rsdp+15); u64 sdt; int use64=0;
    if(rev>=2 && *(u64*)(rsdp+24)){ sdt=*(u64*)(rsdp+24); use64=1; } else sdt=*(u32*)(rsdp+16);
    if(!sdt||sdt>=0xF0000000ULL)return 0; u32 len=*(u32*)(sdt+4); int n=(int)((len-36)/(use64?8:4)); u64 madt=0;
    for(int i=0;i<n&&i<64;i++){ u64 e=use64?*(u64*)(sdt+36+(u64)i*8):(u64)*(u32*)(sdt+36+(u64)i*4);
        if(!e||e>=0xF0000000ULL)continue; const char* s=(const char*)e; if(s[0]=='A'&&s[1]=='P'&&s[2]=='I'&&s[3]=='C'){ madt=e; break; } }
    if(!madt)return 0; u32 mlen=*(u32*)(madt+4); u64 p=madt+44; int c=0;
    while(p<madt+mlen){ u8 type=*(u8*)p,elen=*(u8*)(p+1); if(!elen)break;
        if(type==0){ u8 apicid=*(u8*)(p+3), flags=*(u8*)(p+4); if((flags&1)&&c<max)out[c++]=apicid; } p+=elen; }
    return c;
}
static int smp_start_aps(void){
    u8 ids[8]; int nc=madt_apic_ids(ids,8); if(nc<=1) return nc;
    /* read the real LAPIC base from IA32_APIC_BASE (MSR 0x1B) and make sure it's enabled */
    { u32 lo,hi; __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(0x1B));
      g_lapic_base=((u64)hi<<32)|(u64)(lo&0xFFFFF000u);
      if(g_lapic_base<0x1000ULL||g_lapic_base>=0xFFFFF000ULL) g_lapic_base=0xFEE00000ULL;
      if(!(lo&(1u<<11))){ lo|=(1u<<11); __asm__ volatile("wrmsr"::"a"(lo),"d"(hi),"c"(0x1B)); }   /* global enable */
      lapic_wr(0xF0, lapic_rd(0xF0)|0x100u);   /* spurious-interrupt vector: software-enable the LAPIC */
    }
    /* copy the trampoline to 0x8000 + write the parameter block */
    u32 tlen=(u32)(ap_tramp_end-ap_tramp_start); u8* dst=(u8*)0x8000ULL;
    for(u32 i=0;i<tlen;i++)dst[i]=ap_tramp_start[i];
    u64 cr3; __asm__ volatile("mov %%cr3,%0":"=r"(cr3));
    *(volatile u64*)0x8FF0ULL=cr3; *(volatile u64*)0x8FF8ULL=(u64)ap_main;
    u8 bsp=(u8)(lapic_rd(0x20)>>24);
    int started=0;
    for(int i=0;i<nc;i++){ if(ids[i]==bsp) continue; if(started>=7) break;
        *(volatile u64*)0x8FE0ULL=(u64)(g_ap_stacks[started]+8192);   /* this AP's stack */
        int before=g_cores_online;
        lapic_wr(0x310,(u32)ids[i]<<24); lapic_wr(0x300,0x4500);      /* INIT (assert) */
        ap_udelay(10000);                                            /* ~10 ms */
        lapic_wr(0x310,(u32)ids[i]<<24); lapic_wr(0x300,0x4608);      /* SIPI vector 0x08 */
        ap_udelay(200);
        lapic_wr(0x310,(u32)ids[i]<<24); lapic_wr(0x300,0x4608);      /* SIPI again */
        for(volatile int t=0;t<2000000 && g_cores_online==before;t++)__asm__ volatile("pause");
        started++;
    }
    return nc;
}
/* standard-named aliases so ported C libraries link unchanged */void* malloc(unsigned long n){ return kmalloc(n); }
void  free(void* p){ kfree(p); }
void* realloc(void* p,unsigned long n){ return krealloc(p,n); }
void* calloc(unsigned long a,unsigned long b){ return kcalloc(a,b); }
static unsigned long heap_used(void){ unsigned long u=0; for(hblk* b=heap_head;b;b=b->next) if(b->used) u+=b->size; return u; }
/* ---- functions compiled by the Rune compiler (rune_gfx.rn), linked into the kernel ---- */extern void rune_render(u32* buf, int w, int h, int t);
extern int  rune_fib(int n);
static char* fmt_size(char* p,long b);   /* defined later */