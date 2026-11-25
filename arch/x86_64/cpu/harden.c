#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/harden.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/mem/pmm_vmm.h"
int g_hd_smep=HD_NOCPU, g_hd_smap=HD_NOCPU, g_hd_umip=HD_NOCPU, g_hd_wp=HD_OFF, g_hd_nx=HD_NOCPU, g_hd_smap_want=0, g_hd_guard_src=0;
u64 g_hd_cpus;
static inline u64 hd_cr4(void){ u64 v; __asm__ volatile("mov %%cr4,%0":"=r"(v)); return v; }
static inline void hd_set_cr4(u64 v){ __asm__ volatile("mov %0,%%cr4"::"r"(v):"memory"); }
static inline u64 hd_cr0(void){ u64 v; __asm__ volatile("mov %%cr0,%0":"=r"(v)); return v; }
static inline void hd_stac(void){ if(g_hd_smap==HD_ON)__asm__ volatile("stac":::"memory","cc"); }
static inline void hd_clac(void){ if(g_hd_smap==HD_ON)__asm__ volatile("clac":::"memory","cc"); }
int hd_cr4_bits(u32 ebx7,u32 ecx7,int smap_want,u64* set,int* smep,int* smap,int* umip){
    u64 s=0; *smep=(ebx7>>7)&1?HD_ON:HD_NOCPU; *smap=(ebx7>>20)&1?(smap_want?HD_ON:HD_OFF):HD_NOCPU; *umip=(ecx7>>2)&1?HD_ON:HD_NOCPU;
    if(*smep==HD_ON){ s|=1ULL<<20; } if(*smap==HD_ON){ s|=1ULL<<21; } if(*umip==HD_ON){ s|=1ULL<<11; } *set=s; return s!=0; }
void harden_cpu(void){
    u32 a,b,c,d; cpu_cpuid(0,0,&a,&b,&c,&d); u32 b7=0,c7=0; if(a>=7){ cpu_cpuid(7,0,&a,&b,&c,&d); b7=b; c7=c; }
    u64 set; int sme,sma,umi; hd_cr4_bits(b7,c7,g_hd_smap_want,&set,&sme,&sma,&umi);
    u64 cr4=hd_cr4(), nw=(sma==HD_ON?cr4:(cr4&~(1ULL<<21)))|set; if(nw!=cr4)hd_set_cr4(nw);
    u64 rb=hd_cr4(); g_hd_smep=sme==HD_ON&&!((rb>>20)&1)?HD_OFF:sme; g_hd_smap=sma==HD_ON&&!((rb>>21)&1)?HD_OFF:sma; g_hd_umip=umi==HD_ON&&!((rb>>11)&1)?HD_OFF:umi;
    g_hd_wp=(hd_cr0()>>16)&1?HD_ON:HD_OFF; g_hd_nx=g_nx_on?HD_ON:HD_NOCPU;
    __atomic_add_fetch(&g_hd_cpus,1,__ATOMIC_SEQ_CST); }
u64 hd_guard_mix(u64 seed,u64 rd,u64 tsc){
    u64 x=seed^rd^(tsc*0x9E3779B97F4A7C15ULL); x^=x>>30; x*=0xBF58476D1CE4E5B9ULL; x^=x>>27; x*=0x94D049BB133111EBULL; x^=x>>31;
    x&=~0xFFULL; if(!(x>>8))x=0x5EC0DE0000ULL^(tsc<<8); return x; }
__attribute__((no_stack_protector,noinline)) void harden_guard_init(void){
    extern u64 __stack_chk_guard; u32 a,b,c,d; cpu_cpuid(0,0,&a,&b,&c,&d); u32 mx=a; cpu_cpuid(1,0,&a,&b,&c,&d); int rr=(c>>30)&1, rs=0;
    if(mx>=7){ cpu_cpuid(7,0,&a,&b,&c,&d); rs=(b>>18)&1; }
    u64 rd=0; int src=0; u32 v;
    if(rs){ int got=0; for(int i=0;i<64&&got<2;i++) if(cpu_seed32(&v)){ rd=(rd<<32)|v; got++; } if(got==2)src=2; }
    if(!src&&rr){ int got=0; for(int i=0;i<64&&got<2;i++) if(cpu_rand32(&v)){ rd=(rd<<32)|v; got++; } if(got==2)src=1; }
    u64 t1=cpu_tsc(); for(volatile int i=0;i<200;i++){} u64 t2=cpu_tsc();
    __stack_chk_guard=hd_guard_mix(__stack_chk_guard^(t2<<17),rd,t1^(t2-t1)); g_hd_guard_src=src; }
int uacc_range(u64 p,u64 n){ return n==0||jc_uok(p,n); }
int uacc_in(void* dst,u64 usrc,u64 n){ if(!n)return 0; if(!jc_uok(usrc,n)||!jc_umapped(usrc,n))return -14; hd_stac(); cpu_copy(dst,(const void*)usrc,n); hd_clac(); return 0; }
int uacc_out(u64 udst,const void* src,u64 n){ if(!n)return 0; if(!jc_uok(udst,n)||!jc_umapped_w(udst,n))return -14; hd_stac(); cpu_copy((void*)udst,src,n); hd_clac(); return 0; }
long uacc_str(char* dst,u64 usrc,long cap){ if(cap<=0)return -22; long i=0;
    while(i<cap){ u64 p=usrc+(u64)i; u64 chunk=4096-(p&4095); if(chunk>(u64)(cap-i))chunk=(u64)(cap-i); if(!jc_uok(p,chunk)||!jc_umapped(p,chunk))return -14;
        hd_stac(); const char* s=(const char*)p; u64 k=0; for(;k<chunk;k++){ char ch=s[k]; dst[i+(long)k]=ch; if(!ch)break; } hd_clac(); if(k<chunk)return i+(long)k; i+=(long)chunk; }
    dst[cap-1]=0; return -36; }
static char* hd_s(char* p,char* e,const char* s){ while(*s&&p<e)*p++=*s++; return p; }
static const char* hd_st(int v){ return v==HD_ON?"on":v==HD_OFF?"off":"not on this CPU"; }
int harden_report(char* o,int cap){
    g_hd_nx=g_nx_on?HD_ON:HD_NOCPU;
    if(!o||cap<2){ return 0; } char* p=o; char* e=o+cap-1;
    p=hd_s(p,e,"SMEP (no kernel run of user code): "); p=hd_s(p,e,hd_st(g_hd_smep)); p=hd_s(p,e,"\n");
    p=hd_s(p,e,"SMAP (no kernel touch of user memory): "); p=hd_s(p,e,g_hd_smap==HD_OFF?"off (system calls still read user memory directly)":hd_st(g_hd_smap)); p=hd_s(p,e,"\n");
    p=hd_s(p,e,"UMIP (hide descriptor tables from programs): "); p=hd_s(p,e,hd_st(g_hd_umip)); p=hd_s(p,e,"\n");
    p=hd_s(p,e,"NX (no-execute for program data and stacks): "); p=hd_s(p,e,hd_st(g_hd_nx)); p=hd_s(p,e,"\n");
    p=hd_s(p,e,"Kernel write protect (CR0.WP): "); p=hd_s(p,e,hd_st(g_hd_wp)); p=hd_s(p,e,"\n");
    p=hd_s(p,e,"Stack protector guard: random per boot from "); p=hd_s(p,e,g_hd_guard_src==2?"RDSEED":g_hd_guard_src==1?"RDRAND":"the CPU clock only"); p=hd_s(p,e,"\n");
    *p=0; return (int)(p-o); }
const char* harden_line(void){ g_hd_nx=g_nx_on?HD_ON:HD_NOCPU; static char b[96]; char* p=b; char* e=b+sizeof b-1; int n=0;
    if(g_hd_smep==HD_ON){ p=hd_s(p,e,"SMEP"); n++; } if(g_hd_smap==HD_ON){ p=hd_s(p,e,n?", SMAP":"SMAP"); n++; } if(g_hd_umip==HD_ON){ p=hd_s(p,e,n?", UMIP":"UMIP"); n++; }
    if(g_hd_nx==HD_ON){ p=hd_s(p,e,n?", NX":"NX"); n++; } if(g_hd_wp==HD_ON){ p=hd_s(p,e,n?", WP":"WP"); n++; } p=hd_s(p,e,n?", random stack guard":"Random stack guard"); *p=0; return b; }
