#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/arch/x86_64/cpu/cpuinfo.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"

static inline u64 rdmsr(u32 m){ u32 lo,hi; __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(m)); return ((u64)hi<<32)|lo; }
char g_brand[52]; int g_lma;
void get_brand(void){
    u32 a,b,c,d; cpuid(0x80000000,&a,&b,&c,&d); g_brand[0]=0;
    if(a>=0x80000004){ char*q=g_brand;
        cpuid(0x80000002,&a,&b,&c,&d);q=e4(q,a);q=e4(q,b);q=e4(q,c);q=e4(q,d);
        cpuid(0x80000003,&a,&b,&c,&d);q=e4(q,a);q=e4(q,b);q=e4(q,c);q=e4(q,d);
        cpuid(0x80000004,&a,&b,&c,&d);q=e4(q,a);q=e4(q,b);q=e4(q,c);q=e4(q,d);*q=0;
        char*s=g_brand; while(*s==' ')s++; if(s!=g_brand){char*w=g_brand;while(*s)*w++=*s++;*w=0;}
    } else scopy(g_brand,"x86-64 CPU");
    g_lma=(int)((rdmsr(0xC0000080)>>10)&1);
}


static void cpuid2(u32 lf,u32 sub,u32*a,u32*b,u32*c,u32*d){ __asm__ volatile("cpuid":"=a"(*a),"=b"(*b),"=c"(*c),"=d"(*d):"a"(lf),"c"(sub)); }

static const char* const FL_EDX1[32]={"fpu","vme","de","pse","tsc","msr","pae","mce","cx8","apic",0,"sep","mtrr","pge","mca","cmov","pat","pse36","pn","clflush",0,"dts","acpi","mmx","fxsr","sse","sse2","ss","ht","tm","ia64","pbe"};
static const char* const FL_X1EDX[32]={0,0,0,0,0,0,0,0,0,0,0,"syscall",0,0,0,0,0,0,0,"mp","nx",0,"mmxext",0,0,"fxsr_opt","pdpe1gb","rdtscp",0,"lm","3dnowext","3dnow"};
static const char* const FL_ECX1[32]={"pni","pclmulqdq","dtes64","monitor","ds_cpl","vmx","smx","est","tm2","ssse3","cid","sdbg","fma","cx16","xtpr","pdcm",0,"pcid","dca","sse4_1","sse4_2","x2apic","movbe","popcnt","tsc_deadline_timer","aes","xsave",0,"avx","f16c","rdrand","hypervisor"};
static const char* const FL_X1ECX[32]={"lahf_lm","cmp_legacy","svm","extapic","cr8_legacy","abm","sse4a","misalignsse","3dnowprefetch","osvw","ibs","xop","skinit","wdt",0,"lwp","fma4","tce",0,"nodeid_msr",0,"tbm","topoext","perfctr_core","perfctr_nb",0,"bpext","ptsc","perfctr_llc","mwaitx",0,0};
static const char* const FL_7EBX[32]={"fsgsbase","tsc_adjust","sgx","bmi1","hle","avx2",0,"smep","bmi2","erms","invpcid","rtm","cqm",0,"mpx","rdt_a","avx512f","avx512dq","rdseed","adx","smap","avx512ifma",0,"clflushopt","clwb","intel_pt","avx512pf","avx512er","avx512cd","sha_ni","avx512bw","avx512vl"};
static const char* const FL_DEAX[32]={"xsaveopt","xsavec","xgetbv1","xsaves"};
static const char* const FL_71EAX[32]={0,0,0,0,"avx_vnni","avx512_bf16",0,"cmpccxadd",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"avx_ifma"};
static const char* const FL_X8EBX[32]={"clzero","irperf","xsaveerptr",0,"rdpru",0,0,0,0,"wbnoinvd"};
static const char* const FL_6EAX[32]={"dtherm","ida","arat",0,"pln",0,"pts","hwp","hwp_notify","hwp_act_window","hwp_epp","hwp_pkg_req"};
static const char* const FL_7ECX[32]={0,"avx512vbmi","umip","pku","ospke","waitpkg","avx512_vbmi2",0,"gfni","vaes","vpclmulqdq","avx512_vnni","avx512_bitalg","tme","avx512_vpopcntdq",0,0,0,0,0,0,0,"rdpid",0,"bus_lock_detect","cldemote",0,"movdiri","movdir64b","enqcmd","sgx_lc",0};
static const char* const FL_7EDX[32]={0,0,"avx512_4vnniw","avx512_4fmaps","fsrm",0,0,0,"avx512_vp2intersect",0,"md_clear",0,0,0,"serialize",0,"tsxldtrk",0,"pconfig","arch_lbr","ibt",0,"amx_bf16","avx512_fp16","amx_tile","amx_int8",0,0,0,"arch_capabilities",0,0};
static const char* const PWR[13]={"ts","fid","vid","ttp","tm","stc","100mhzsteps","hwpstate",0,"cpb","eff_freq_ro","proc_feedback","acc_power"};

struct cw { char* o; int n, cap; };
static void cw_s(struct cw* w,const char* s){ while(*s){ if(w->n<w->cap-1)w->o[w->n]=*s; w->n++; s++; } }
static void cw_u(struct cw* w,u64 v){ char t[24]; int k=0; if(!v)t[k++]='0'; while(v){ t[k++]=(char)('0'+v%10); v/=10; } while(k){ char c[2]={t[--k],0}; cw_s(w,c); } }
static void cw_x(struct cw* w,u64 v){ char t[20]; int k=0; if(!v)t[k++]='0'; while(v){ t[k++]="0123456789abcdef"[v&15]; v>>=4; } cw_s(w,"0x"); while(k){ char c[2]={t[--k],0}; cw_s(w,c); } }
static void cw_fix(struct cw* w,u64 milli,int dig){ cw_u(w,milli/1000); cw_s(w,"."); u64 f=milli%1000; if(dig==2){ f/=10; if(f<10)cw_s(w,"0"); cw_u(w,f); } else { if(f<100)cw_s(w,"0"); if(f<10)cw_s(w,"0"); cw_u(w,f); } }
static void cw_bits(struct cw* w,const char* const* tab,u32 v){ for(int i=0;i<32;i++) if((v>>i)&1 && tab[i]){ cw_s(w," "); cw_s(w,tab[i]); } }

struct cpuid_snap { u32 max,xmax,e1a,e1b,e1c,e1d,x1c,x1d,s7b,s7c,s7d,s71a,dxa,x7d,x8a,x8b,s6a; char vendor[13]; u32 cache_kb; int tsc_inv,topo; };
static void cpuid_take(struct cpuid_snap* s){
    u32 a,b,c,d; for(unsigned i=0;i<sizeof *s;i++)((char*)s)[i]=0;
    cpuid2(0,0,&a,&b,&c,&d); s->max=a;
    u32 v[3]={b,d,c}; for(int i=0;i<12;i++)s->vendor[i]=(char)(v[i/4]>>((i%4)*8)); s->vendor[12]=0;
    cpuid2(1,0,&s->e1a,&s->e1b,&s->e1c,&s->e1d);
    if(s->max>=6)cpuid2(6,0,&s->s6a,&b,&c,&d);
    if(s->max>=7){ cpuid2(7,0,&a,&s->s7b,&s->s7c,&s->s7d); if(a>=1)cpuid2(7,1,&s->s71a,&b,&c,&d); }
    if(s->max>=0xd)cpuid2(0xd,1,&s->dxa,&b,&c,&d);
    if(s->max>=0xb){ cpuid2(0xb,0,&a,&b,&c,&d); s->topo=b!=0; }
    cpuid2(0x80000000,0,&s->xmax,&b,&c,&d);
    if(s->xmax>=0x80000001)cpuid2(0x80000001,0,&a,&b,&s->x1c,&s->x1d);
    if(s->xmax>=0x80000007)cpuid2(0x80000007,0,&a,&b,&c,&s->x7d);
    if(s->xmax>=0x80000008)cpuid2(0x80000008,0,&s->x8a,&s->x8b,&c,&d);
    s->tsc_inv=(s->x7d>>8)&1;
    if(s->max>=4 && s->vendor[0]=='G'){ for(u32 i=0;i<16;i++){ cpuid2(4,i,&a,&b,&c,&d); if(!(a&31))break; u32 kb=(((b>>22)&0x3ff)+1)*(((b>>12)&0x3ff)+1)*((b&0xfff)+1)*(c+1)/1024; s->cache_kb=kb; } }
    else if(s->xmax>=0x80000006){ cpuid2(0x80000006,0,&a,&b,&c,&d); u32 l3=(d>>18)*512, l2=c>>16; s->cache_kb=l3?l3:l2; }
}

static void flags_line(struct cw* w,const struct cpuid_snap* s){
    cw_s(w,"flags\t\t:");
    cw_bits(w,FL_EDX1,s->e1d);
    cw_bits(w,FL_X1EDX,s->x1d);
    if(s->tsc_inv)cw_s(w," constant_tsc");
    if(s->topo)cw_s(w," xtopology");
    if(s->tsc_inv)cw_s(w," nonstop_tsc");
    cw_s(w," cpuid");
    cw_bits(w,FL_ECX1,s->e1c);
    cw_bits(w,FL_X1ECX,s->x1c);
    if((s->s7d>>31)&1)cw_s(w," ssbd");
    if((s->s7d>>26)&1)cw_s(w," ibrs ibpb");
    if((s->s7d>>27)&1)cw_s(w," stibp");
    cw_bits(w,FL_7EBX,s->s7b);
    cw_bits(w,FL_DEAX,s->dxa&31);
    cw_bits(w,FL_71EAX,s->s71a);
    cw_bits(w,FL_X8EBX,s->x8b&0x217);
    cw_bits(w,FL_6EAX,s->s6a&0xfd7);
    cw_bits(w,FL_7ECX,s->s7c);
    cw_bits(w,FL_7EDX,s->s7d);
    cw_s(w,"\n");
}

int cpuinfo_has(const char* flag){
    static char b[2048]; struct cw w={b,0,(int)sizeof b}; struct cpuid_snap s; cpuid_take(&s); flags_line(&w,&s); b[w.n<w.cap?w.n:w.cap-1]=0;
    int L=0; while(flag[L])L++;
    for(char* p=b;*p;p++){ if((p==b||p[-1]==' ')){ int i=0; while(i<L&&p[i]==flag[i])i++; if(i==L&&(p[L]==' '||p[L]=='\n'||!p[L]))return 1; } }
    return 0;
}

int cpuinfo_linux(char* out,int cap){
    if(cap<=0)return -1;
    struct cpuid_snap s; cpuid_take(&s);
    char brand[52]; brand[0]=0; if(g_brand[0]){ int k=0; for(;g_brand[k]&&k<51;k++)brand[k]=g_brand[k]; brand[k]=0; } else if(s.xmax>=0x80000004){ u32 r[12]; for(u32 i=0;i<3;i++)cpuid2(0x80000002+i,0,&r[i*4],&r[i*4+1],&r[i*4+2],&r[i*4+3]); char* q=brand; for(int i=0;i<12;i++)q=e4(q,r[i]); *q=0; char* t=brand; while(*t==' ')t++; if(t!=brand){ char* w=brand; while(*t)*w++=*t++; *w=0; } }
    u32 fam=(s.e1a>>8)&0xf, mod=(s.e1a>>4)&0xf, step=s.e1a&0xf;
    if(fam==0xf)fam+=(s.e1a>>20)&0xff;
    if(fam==6||fam>=0xf)mod+=((s.e1a>>16)&0xf)<<4;
    u64 khz=g_hr.tsc_hz/1000;
    int ncpu=0; for(int i=0;i<NCPU;i++) if(i==0||(g_smp_on&&g_cpu[i].online))ncpu++;
    struct cw w={out,0,cap};
    int idx=0;
    for(int ci=0;ci<NCPU;ci++){
        if(ci!=0&&!(g_smp_on&&g_cpu[ci].online))continue;
        cw_s(&w,"processor\t: "); cw_u(&w,(u64)idx);
        cw_s(&w,"\nvendor_id\t: "); cw_s(&w,s.vendor);
        cw_s(&w,"\ncpu family\t: "); cw_u(&w,fam);
        cw_s(&w,"\nmodel\t\t: "); cw_u(&w,mod);
        cw_s(&w,"\nmodel name\t: "); cw_s(&w,brand[0]?brand:"x86-64 CPU");
        cw_s(&w,"\nstepping\t: "); cw_u(&w,step);
        cw_s(&w,"\nmicrocode\t: "); cw_x(&w,0);
        cw_s(&w,"\ncpu MHz\t\t: "); cw_fix(&w,khz,3);
        if(s.cache_kb){ cw_s(&w,"\ncache size\t: "); cw_u(&w,s.cache_kb); cw_s(&w," KB"); }
        cw_s(&w,"\nphysical id\t: 0\nsiblings\t: "); cw_u(&w,(u64)ncpu);
        cw_s(&w,"\ncore id\t\t: "); cw_u(&w,(u64)idx);
        cw_s(&w,"\ncpu cores\t: "); cw_u(&w,(u64)ncpu);
        cw_s(&w,"\napicid\t\t: "); cw_u(&w,g_cpu[ci].apic_id);
        cw_s(&w,"\ninitial apicid\t: "); cw_u(&w,g_cpu[ci].apic_id);
        cw_s(&w,"\nfpu\t\t: yes\nfpu_exception\t: yes\ncpuid level\t: "); cw_u(&w,s.max);
        cw_s(&w,"\nwp\t\t: yes\n");
        flags_line(&w,&s);
        cw_s(&w,"bugs\t\t:\nbogomips\t: "); cw_fix(&w,khz*2,2);
        u32 cl=((s.e1b>>8)&0xff)*8; if(!cl)cl=64;
        cw_s(&w,"\nclflush size\t: "); cw_u(&w,cl);
        cw_s(&w,"\ncache_alignment\t: "); cw_u(&w,cl);
        u32 pb=s.x8a&0xff, vb=(s.x8a>>8)&0xff; if(!pb)pb=36; if(!vb)vb=48;
        cw_s(&w,"\naddress sizes\t: "); cw_u(&w,pb); cw_s(&w," bits physical, "); cw_u(&w,vb); cw_s(&w," bits virtual");
        cw_s(&w,"\npower management:");
        for(int i=0;i<13;i++) if(((s.x7d>>i)&1)&&PWR[i]){ cw_s(&w," "); cw_s(&w,PWR[i]); }
        cw_s(&w,"\n\n");
        idx++;
    }
    if(w.n>=cap){ out[cap-1]=0; return cap-1; }
    out[w.n]=0; return w.n;
}
