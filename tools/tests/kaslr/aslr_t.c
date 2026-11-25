#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
typedef unsigned long long u64; typedef unsigned int u32; typedef unsigned short u16; typedef unsigned char u8;
static FILE* g_ur;
static void csprng_bytes(u8* b,int n){ if(fread(b,1,(size_t)n,g_ur)!=(size_t)n)exit(2); }
#include "aslr_ext.c"
static u64 g_occ[8][2]; static int g_nocc;
static u64 lx_gap_scan(u64 cr3,u64 need,u64 from,u64 to){ (void)cr3; u64 v=from; for(int moved=1;moved;){ moved=0; for(int i=0;i<g_nocc;i++) if(v<g_occ[i][1]&&v+need>g_occ[i][0]){ v=g_occ[i][1]; moved=1; } } return v+need<=to?v:0; }
static int fails, checks;
#define CK(c,msg) do{ checks++; if(c)printf("ok %s\n",msg); else { fails++; printf("FAIL %s (line %d)\n",msg,__LINE__); } }while(0)
#define N 200000
static u64 v[N];
static int cmpu(const void* a,const void* b){ u64 x=*(const u64*)a, y=*(const u64*)b; return x<y?-1:x>y; }
static double bits(int n,int sh){ u64 orv=0, andv=~0ULL; for(int i=0;i<n;i++){ orv|=v[i]>>sh; andv&=v[i]>>sh; } u64 d=orv&~andv; return (double)__builtin_popcountll(d); }
static int distinct(int n){ qsort(v,(size_t)n,8,cmpu); int d=1; for(int i=1;i<n;i++) if(v[i]!=v[i-1])d++; return d; }
static double ent(int n,int sh,int lo,int nb){ static u32 h[1<<16]; memset(h,0,sizeof h); for(int i=0;i<n;i++)h[(v[i]>>(sh+lo))&((1u<<nb)-1)]++; double e=0; for(u32 i=0;i<(1u<<nb);i++) if(h[i]){ double p=(double)h[i]/n; e-=p*log2(p); } return e; }
static u8 g_ph[56*4];
static void mkph(u64 al){ memset(g_ph,0,sizeof g_ph); for(int i=0;i<2;i++){ u8* p=g_ph+i*56; *(u32*)p=1; *(u64*)(p+48)=al; } }
int main(void){
    g_ur=fopen("/dev/urandom","rb"); if(!g_ur)return 2;
    { const u64 lo=0x40004000000ULL, hi=0x48000000000ULL, st=0x46200000000ULL, need=0x4000ULL; u64 xb, xe; lx_stk_excl(st,&xb,&xe);
      u64 bot=st-(u64)(JS_STK_PAGES-1)*0x1000ULL; g_nocc=2; g_occ[0][0]=lo; g_occ[0][1]=bot-0x10000ULL; g_occ[1][0]=bot; g_occ[1][1]=st+0x1000ULL;
      u64 r=lx_gap_scan_x(0,need,lo,hi,xb,xe);
      CK(r&&(r+need+0x100000ULL<=bot||r>=st+0x1000ULL+0x100000ULL),"mmap gap search keeps a 1 MB guard on both sides of the randomised stack");
      CK(lx_gap_scan(0,need,lo,hi)==bot-0x10000ULL,"(model check: without the stack exclusion the hole right below the stack would be chosen)");
      lx_stk_excl(USTK_V,&xb,&xe); g_nocc=1; g_occ[0][0]=lo; g_occ[0][1]=lo+0x5000ULL;
      CK(lx_gap_scan_x(0,need,lo,hi,xb,xe)==lx_gap_scan(0,need,lo,hi),"fixed stack below the mmap window (kaslr off): gap search unchanged");
      g_nocc=1; g_occ[0][0]=lo; g_occ[0][1]=xb-0x1000ULL; lx_stk_excl(st,&xb,&xe); g_occ[0][1]=xb-0x1000ULL;
      CK(lx_gap_scan_x(0,0x2000ULL,lo,hi,xb,xe)==xe,"a request that does not fit below the guard goes above the stack window"); }
    char m[160];
    for(int i=0;i<N;i++)v[i]=kaslr_gap_hint();
    double b=bits(N,12); snprintf(m,sizeof m,"mmap base: %.0f varying page bits (want %d)",b,KASLR_MMAP_BITS); CK(b==KASLR_MMAP_BITS,m);
    double e=ent(N,12,KASLR_MMAP_BITS-12,12); snprintf(m,sizeof m,"mmap base top 12 bits uniform (%.3f bits)",e); CK(e>11.98,m);
    int okr=1; for(int i=0;i<N;i++) if(0x40004000000ULL+v[i]>=KASLR_INTERP_LO||(v[i]&0xFFF))okr=0; CK(okr,"mmap hint page aligned and below the ld.so window");
    for(int i=0;i<N;i++)v[i]=kaslr_user_mmap();
    b=bits(N,12); snprintf(m,sizeof m,"native mmap next: %.0f bits",b); CK(b==KASLR_MMNEXT_BITS,m);
    for(int i=0;i<N;i++){ mkph(0x1000); v[i]=elf_interp_base(g_ph,0,56,2); }
    okr=1; for(int i=0;i<N;i++) if(v[i]<KASLR_INTERP_LO||v[i]>=KASLR_INTERP_LO+KASLR_INTERP_WIN||(v[i]&0xFFF)||v[i]+0x100000000ULL>ELF_BIG_BASE)okr=0;
    CK(okr,"ld.so base inside its window, page aligned, 4 GB span stays below ELF_BIG_BASE");
    b=bits(N,12); snprintf(m,sizeof m,"ld.so base: %.0f varying bits at 4K align",b); CK(b>=25,m);
    int d=distinct(N); snprintf(m,sizeof m,"ld.so base: %d distinct of %d",d,N); CK(d>N-1500,m);
    for(int i=0;i<N;i++){ mkph(0x200000); v[i]=elf_interp_base(g_ph,0,56,2); }
    okr=1; for(int i=0;i<N;i++) if(v[i]&0x1FFFFF)okr=0; CK(okr,"ld.so with 2 MB p_align gets 2 MB aligned base");
    b=bits(N,21); snprintf(m,sizeof m,"ld.so 2M align: %.0f bits",b); CK(b>=16,m);
    for(int i=0;i<N;i++){ mkph(0x10000); v[i]=elf_interp_base(g_ph,0,56,2); } okr=1; for(int i=0;i<N;i++) if(v[i]&0xFFFF)okr=0; CK(okr,"64K p_align honoured");
    g_kaslr_on=0; mkph(0x1000); CK(elf_interp_base(g_ph,0,56,2)==ELF_INTERP_BASE,"kaslr off keeps the old fixed ld.so base"); g_kaslr_on=1;
    u64 span=0x300000; for(int i=0;i<N;i++)v[i]=kaslr_pie_slide(ELF_IMG_TOP-ELF_PIE_BASE-span,12,1ULL<<20);
    okr=1; for(int i=0;i<N;i++) if(ELF_PIE_BASE+v[i]+span>ELF_IMG_TOP)okr=0; CK(okr,"small PIE stays inside the image window");
    b=log2((double)distinct(N)); snprintf(m,sizeof m,"small PIE: %.1f bits (was 8)",b); CK(b>=12,m);
    span=0x40000000ULL; for(int i=0;i<N;i++)v[i]=kaslr_pie_slide(ELF_BIG_MAX-span,12,1ULL<<24);
    okr=1; for(int i=0;i<N;i++) if(v[i]+span>ELF_BIG_MAX)okr=0; CK(okr,"big PIE stays inside its window");
    b=bits(N,12); snprintf(m,sizeof m,"big PIE: %.0f bits (was 9)",b); CK(b>=24,m);
    for(int i=0;i<N;i++)v[i]=kaslr_stack_room(0x40000);
    okr=1; for(int i=0;i<N;i++) if(v[i]>=KASLR_STK_MAX||(v[i]&15))okr=0; CK(okr,"stack offset 16-aligned and capped");
    b=bits(N,4); snprintf(m,sizeof m,"stack offset: %.0f bits (was 8)",b); CK(b==12,m);
    for(int i=0;i<1000;i++){ u64 r=kaslr_stack_room(100); if(r>=100){ okr=0; } } CK(okr,"stack offset never exceeds the free room");
    CK(kaslr_stack_room(0)==0&&kaslr_stack_room(15)==0,"no room gives no offset");
    for(int i=0;i<N;i++)v[i]=kaslr_user_stack(USTK_V,0x100000);
    okr=1; for(int i=0;i<N;i++) if((v[i]&0xFFF)||v[i]-0x100000<KASLR_STK_LO||v[i]+0x1000>=ELF_BIG_BASE||v[i]-0x100000<KASLR_INTERP_LO+KASLR_INTERP_WIN)okr=0; CK(okr,"stack top page aligned, 1 MB stack inside its window, clear of ld.so and ELF_BIG");
    for(int i=0;i<N;i++)v[i]-=KASLR_STK_LO+0x100000;
    b=bits(N,12); snprintf(m,sizeof m,"stack VA: %.0f varying page bits (want %d)",b,KASLR_STK_BITS); CK(b==KASLR_STK_BITS,m);
    e=ent(N,12,KASLR_STK_BITS-12,12); snprintf(m,sizeof m,"stack VA top 12 bits uniform (%.3f bits)",e); CK(e>11.98,m);
    g_kaslr_on=0; CK(kaslr_user_stack(USTK_V,0x100000)==USTK_V,"kaslr off keeps the old fixed stack VA"); g_kaslr_on=1;
    CK(kaslr_align_shift(0)==12&&kaslr_align_shift(0x1000)==12&&kaslr_align_shift(0x10000)==16&&kaslr_align_shift(0x200000)==21&&kaslr_align_shift(1ULL<<30)==21,"align shift clamps 4K..2M");
    printf("%d/%d %s\n",checks-fails,checks,fails?"ASLR FAIL":"ASLR all ok"); return fails!=0; }
