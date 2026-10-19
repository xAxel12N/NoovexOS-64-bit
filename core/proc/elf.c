#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/lib/crypto/nvxtls.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/arch/x86_64/user/ring3.h"
#include "include/nvx/core/proc/elf.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/sys/native/syscall.h"
#include "include/nvx/core/proc/jobs.h"
/* ================= STAGE 6: ELF loader for user programs =================
   Parse an ELF64, map its PT_LOAD segments into a fresh user address space at
   their virtual addresses (user-accessible), set up a user stack, run in ring 3. */
extern const unsigned char _binary_user_user_demo_elf_start[], _binary_user_user_demo_elf_end[];
extern const unsigned char _binary_user_user_fork_elf_start[], _binary_user_user_fork_elf_end[];
extern const unsigned char _binary_user_user_app_elf_start[], _binary_user_user_app_elf_end[];
extern const unsigned char _binary_user_user_thread_elf_start[], _binary_user_user_thread_elf_end[];

#define ELF_PIE_BASE 0x40000400000ULL
#define ELF_INTERP_BASE 0x40030000000ULL
#define ELF_IMG_LO   0x40000000000ULL

char g_exec_args[2048]={0}; u64 g_arg_rsp=0,g_arg_argv=0; int g_arg_argc=0;
char g_xa_buf[XA_CAP]; int g_xa_off[XA_MAX], g_xa_n=0, g_xa_set=0;
char g_xe_buf[XA_CAP]; int g_xe_off[XA_MAX], g_xe_n=0, g_xe_set=0;
#define STKIMG 0x50000
static u8 g_stkimg[STKIMG]; int g_elf_pie=0;

static void elf_args_from_string(const char* a){
    int used=0; g_xa_n=0;
    while(g_xa_n<XA_MAX){ while(*a==' ')a++; if(!*a)break;
        int st=used; while(*a&&*a!=' '&&used<XA_CAP-1){ char ch=*a++; g_xa_buf[used++]=(ch==0x1F)?' ':ch; }
        if(used>=XA_CAP-1)break; g_xa_buf[used++]=0; g_xa_off[g_xa_n++]=st;
        while(*a&&*a!=' ')a++; }
}
static int elf_ph_ok(u32 len,u64 phoff,u16 phentsz,u16 phnum){
    if(phentsz<56||phnum==0||phnum>64)return 0;
    if(phoff>len||(u64)phentsz*phnum>len-phoff)return 0;
    return 1;
}
static int elf_map_segs(u64 sp,const u8* elf,u64 phoff,u16 phentsz,u16 phnum,u64 base){
    for(int i=0;i<phnum;i++){ const u8* ph=elf+phoff+(u64)i*phentsz; if(*(const u32*)ph!=1)continue;
        u64 off=*(const u64*)(ph+8), va=*(const u64*)(ph+16)+base, filesz=*(const u64*)(ph+32), memsz=*(const u64*)(ph+40);
        u64 vs=va&~0xFFFULL, ve=(va+memsz+0xFFF)&~0xFFFULL;
        for(u64 v=vs;v<ve;v+=0x1000){
            u64* e=vm_walk(sp,v,0); u64 f;
            if(e&&(*e&1))f=*e&0x000FFFFFFFFFF000ULL;
            else { f=pframe_alloc(); if(!f||vm_map(sp,v,f,0x7ULL)){ if(f)pframe_free(f); return -1; } }
            u8* dst=(u8*)f;
            u64 lo=v<va?va:v, hi=v+0x1000ULL; if(hi>va+memsz)hi=va+memsz;
            for(u64 a=lo;a<hi;a++) dst[a-v]=(a<va+filesz)?elf[off+(a-va)]:0; } }
    return 0;
}
static u64 elf_load_interp(u64 sp,const char* path,u64* ient){
    u32 il=0; const u8* ie=jc_find_lib(path,&il); if(!ie||il<64)return 0;
    if(ie[0]!=0x7F||ie[1]!='E'||ie[2]!='L'||ie[3]!='F'||ie[4]!=2||ie[5]!=1)return 0;
    if(*(const u16*)(ie+16)!=3||*(const u16*)(ie+18)!=62)return 0;
    u64 phoff=*(const u64*)(ie+32); u16 phentsz=*(const u16*)(ie+54), phnum=*(const u16*)(ie+56);
    if(!elf_ph_ok(il,phoff,phentsz,phnum))return 0;
    for(int i=0;i<phnum;i++){ const u8* ph=ie+phoff+(u64)i*phentsz; if(*(const u32*)ph!=1)continue;
        u64 off=*(const u64*)(ph+8), va=*(const u64*)(ph+16), filesz=*(const u64*)(ph+32), memsz=*(const u64*)(ph+40);
        if(filesz>memsz||off>il||filesz>il-off||va>0x100000000ULL||memsz>0x100000000ULL)return 0; }
    if(elf_map_segs(sp,ie,phoff,phentsz,phnum,ELF_INTERP_BASE))return 0;
    *ient=ELF_INTERP_BASE+*(const u64*)(ie+24);
    return ELF_INTERP_BASE;
}
u64 elf_build_space(const u8* elf,u32 len,u64* entry){
    if(len<64||elf[0]!=0x7F||elf[1]!='E'||elf[2]!='L'||elf[3]!='F'||elf[4]!=2||elf[5]!=1)return 0;
    u16 etype=*(const u16*)(elf+16), mach=*(const u16*)(elf+18);
    if((etype!=2&&etype!=3)||mach!=62)return 0;
    u64 ent=*(const u64*)(elf+24), phoff=*(const u64*)(elf+32);
    u16 phentsz=*(const u16*)(elf+54), phnum=*(const u16*)(elf+56);
    if(!elf_ph_ok(len,phoff,phentsz,phnum))return 0;
    u64 base=(etype==3)?ELF_PIE_BASE:0; g_elf_pie=(etype==3);
    u64 phdr_va=0, first_off0=0; int have_off0=0, nload=0, entok=0; char interp[128]; interp[0]=0;
    for(int i=0;i<phnum;i++){ const u8* ph=elf+phoff+(u64)i*phentsz; u32 t=*(const u32*)ph;
        u64 off=*(const u64*)(ph+8), va=*(const u64*)(ph+16)+base, filesz=*(const u64*)(ph+32), memsz=*(const u64*)(ph+40);
        if(t==6)phdr_va=va;
        if(t==3){ if(filesz<2||filesz>127||off>len||filesz>len-off)return 0; for(u64 k=0;k<filesz;k++)interp[k]=(char)elf[off+k]; interp[filesz]=0; continue; }
        if(t!=1)continue;
        if(filesz>memsz||off>len||filesz>len-off)return 0;
        if(va<ELF_IMG_LO||memsz>ELF_IMG_TOP||va>ELF_IMG_TOP-memsz)return 0;
        if(va<USTK_V+0x1000ULL&&va+memsz>ELF_STK_LO)return 0;
        if(off==0&&!have_off0){ have_off0=1; first_off0=va; }
        if(ent+base>=va&&ent+base<va+memsz&&(*(const u32*)(ph+4)&1))entok=1;
        nload++; }
    if(!nload||!entok)return 0;
    if(!phdr_va&&have_off0)phdr_va=first_off0+phoff;
    if(interp[0]&&etype!=3)return 0;
    u64 sp=vm_new_pml4(); if(!sp)return 0;
    if(elf_map_segs(sp,elf,phoff,phentsz,phnum,base)){ vm_free_user(sp); return 0; }
    u64 ibase=0, ient=0;
    if(interp[0]){ ibase=elf_load_interp(sp,interp,&ient); if(!ibase){ vm_free_user(sp); return 0; } }
    if(!g_xa_set)elf_args_from_string(g_exec_args);
    if(!g_xe_set)g_xe_n=env_pack(g_xe_buf,XA_CAP,g_xe_off,XA_MAX);
    g_xa_set=0; g_xe_set=0;
    {
        u8* S=g_stkimg; const int SZ=STKIMG; u64 SB=USTK_V+0x1000ULL-(u64)SZ;
        for(int i=0;i<SZ;i++)S[i]=0;
        int top=SZ-16;
        u64 x=cpu_tsc()^0x9E3779B97F4A7C15ULL; for(int i=0;i<16;i++){ x^=x<<13; x^=x>>7; x^=x<<17; S[top+i]=(u8)(x>>24); }
        u64 rnd=SB+(u64)top;
        static u64 uargv[XA_MAX], uenv[XA_MAX]; { long need=64+16+(long)(g_xa_n+g_xe_n+4)*8+256; for(int i=0;i<g_xe_n;i++){ const char* z=g_xe_buf+g_xe_off[i]; while(*z++)need++; need++; } for(int i=0;i<g_xa_n;i++){ const char* z=g_xa_buf+g_xa_off[i]; while(*z++)need++; need++; } if(need>SZ-4096){ vm_free_user(sp); return 0; } }
        for(int i=g_xe_n-1;i>=0;i--){ const char* z=g_xe_buf+g_xe_off[i]; int L=0; while(z[L])L++; top-=L+1; for(int k=0;k<=L;k++)S[top+k]=(u8)z[k]; uenv[i]=SB+(u64)top; }
        for(int i=g_xa_n-1;i>=0;i--){ const char* z=g_xa_buf+g_xa_off[i]; int L=0; while(z[L])L++; top-=L+1; for(int k=0;k<=L;k++)S[top+k]=(u8)z[k]; uargv[i]=SB+(u64)top; }
        u64 execfn=g_xa_n?uargv[0]:0;
        u64 aux[]={3,phdr_va,4,phentsz,5,phnum,6,4096,7,ibase,8,0,9,ent+base,11,(u64)g_uid,12,(u64)g_uid,13,(u64)g_gid,14,(u64)g_gid,16,0,17,100,23,0,25,rnd,31,execfn,0,0};
        int na=(int)(sizeof(aux)/8);
        int words=1+g_xa_n+1+g_xe_n+1+na;
        top-=words*8; top&=~0xF;
        u64* W=(u64*)(S+top); int w=0;
        W[w++]=(u64)g_xa_n; for(int i=0;i<g_xa_n;i++)W[w++]=uargv[i]; W[w++]=0;
        for(int i=0;i<g_xe_n;i++)W[w++]=uenv[i]; W[w++]=0;
        for(int i=0;i<na;i++)W[w++]=aux[i];
        int pg0=top>>12; if(pg0>SZ/4096-2)pg0=SZ/4096-2;
        for(int pg=pg0;pg<SZ/4096;pg++){ u64 f=pframe_alloc(); if(!f||vm_map(sp,SB+(u64)pg*0x1000ULL,f,0x7ULL)){ if(f)pframe_free(f); vm_free_user(sp); return 0; } u8* d=(u8*)f; const u8* q=S+pg*4096; for(int i=0;i<4096;i++)d[i]=q[i]; }
        g_arg_rsp=SB+(u64)top; g_arg_argc=g_xa_n; g_arg_argv=g_arg_rsp+8;
    }
    *entry=ibase?ient:ent+base; return sp;
}
/* single-run (no scheduler): load + run one ELF, exit returns to the kernel via longjmp */
 
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

/* Load a relocatable ELF64 from a buffer. Returns 0 on success, fills mod. */
int nvx_dlload(const u8* elf,u32 len,nvx_module* mod){
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
int nvx_dlcall(nvx_module* mod){ if(!mod->ok)return -1; int(*fn)(void)=(int(*)(void))mod->entry; return fn(); }

int elf_load_run(const u8* elf,u32 len){
    if(!pf_ready) return 1; us_init(); u64 entry; u64 sp=elf_build_space(elf,len,&entry); if(!sp) return 2;
    g_r3out_n=0; g_r3out[0]=0; g_sched_active=0;
    u64 kcr3; kcr3=cpu_get_pt();
    if(k_setjmp(g_kjmp)==0){ cpu_set_pt(sp); enter_ring3_args(entry, g_arg_rsp?g_arg_rsp:USTK_V+0x1000ULL, (u64)g_arg_argc, g_arg_argv); }
    cpu_set_pt(kcr3);
    return 0;
}
