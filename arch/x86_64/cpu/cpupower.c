#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/arch/x86_64/cpu/cpuinfo.h"
#include "include/nvx/core/klog.h"

int g_mwait_hint=-1; volatile u64 g_mwait_line[8] __attribute__((aligned(64)));
int g_cp_epp_ok, g_cp_epb_ok;
int g_cp_mode, g_cp_cstate, g_cp_lo, g_cp_hi, g_cp_epp=0x80, g_cp_epp_gen;
static int g_cp_epp_seen[64];
static u64 g_cp_t0[64], g_cp_idle[64], g_cp_i0[64]; static int g_cp_cur[64];
u32 g_cp_switches;
static inline u64 cp_tsc(void){ u32 a,d; __asm__ volatile("rdtsc":"=a"(a),"=d"(d)); return ((u64)d<<32)|a; }
static void cp_cpuid(u32 lf,u32 sub,u32* r){ __asm__ volatile("cpuid":"=a"(r[0]),"=b"(r[1]),"=c"(r[2]),"=d"(r[3]):"a"(lf),"c"(sub)); }
static int cp_rd(u32 m,u64* v){ return cpu_rdmsr_safe(m,v); }
static int cp_wr(u32 m,u64 v){ return cpu_wrmsr_safe(m,v); }
static void cp_set(int ratio){
    if(g_cp_mode==3){ u64 v; if(cp_rd(0x199,&v))return; v=(v&~0xFF00ULL)|((u64)(ratio&0xFF)<<8); cp_wr(0x199,v); }
    else if(g_cp_mode==4){ cp_wr(0xC0010062,(u64)ratio); } }
static void cp_hwp_req(void){
    if(g_cp_mode==1){ u64 cap; if(cp_rd(0x771,&cap))return; u64 hi=cap&0xFF, lo=(cap>>24)&0xFF; cp_wr(0x774,lo|(hi<<8)|(g_cp_epp_ok?((u64)(g_cp_epp&0xFF)<<24):0));
        if(!g_cp_epp_ok&&g_cp_epb_ok){ u64 b; if(!cp_rd(0x1B0,&b))cp_wr(0x1B0,(b&~0xFULL)|(g_cp_epp>=0xC0?12u:6u)); } }
    else if(g_cp_mode==2){ u64 cap; if(cp_rd(0xC00102B0,&cap))return; u64 hi=(cap>>24)&0xFF, lo=cap&0xFF; cp_wr(0xC00102B3,hi|(lo<<8)|((u64)(g_cp_epp&0xFF)<<24)); } }
void cpupower_init(int bsp){
    u32 r[4]; cp_cpuid(0,0,r); u32 maxl=r[0]; int intel=r[1]==0x756E6547u, amd=r[1]==0x68747541u;
    cp_cpuid(1,0,r); int hv=(int)((r[2]>>31)&1), mw=(int)((r[2]>>3)&1), eist=(int)((r[2]>>7)&1);
    u32 l6=0, c6=0; if(maxl>=6){ cp_cpuid(6,0,r); l6=r[0]; c6=r[2]; }
    if(bsp){ g_cp_epp_ok=(int)((l6>>10)&1); g_cp_epb_ok=(int)((c6>>3)&1); }
    int inv=0; { cp_cpuid(0x80000000u,0,r); u32 mx=r[0]; if(mx>=0x80000007u){ cp_cpuid(0x80000007u,0,r); inv=(int)((r[3]>>8)&1); } if(amd&&mx>=0x80000008u){ cp_cpuid(0x80000008u,0,r); if(bsp&&((r[1]>>27)&1))g_cp_mode=2; } }
    if(bsp){
        if(intel&&!hv&&mw&&maxl>=5&&(l6&4)&&inv){ cp_cpuid(5,0,r); if((r[2]&3)==3){ int best=-1; for(int k=1;k<=7;k++){ int n=(int)((r[3]>>(4*k))&0xF); if(n&&k<=3)best=k; } if(best>1){ g_mwait_hint=(best-1)<<4; g_cp_cstate=best; } } }
        if(!g_cp_mode&&intel&&((l6>>7)&1))g_cp_mode=1;
        if(!g_cp_mode&&intel&&eist&&!hv){ u64 pi; if(!cp_rd(0xCE,&pi)){ g_cp_lo=(int)((pi>>40)&0xFF); g_cp_hi=(int)((pi>>8)&0xFF); if(g_cp_lo>0&&g_cp_hi>g_cp_lo)g_cp_mode=3; } }
        if(!g_cp_mode&&amd&&!hv){ u64 lim; if(!cp_rd(0xC0010061,&lim)){ g_cp_hi=0; g_cp_lo=(int)((lim>>4)&7); if(g_cp_lo>0)g_cp_mode=4; } }
        if(hv&&g_cp_mode!=1&&g_cp_mode!=2)g_cp_mode=0;
    }
    if(g_cp_mode==1){ if(cp_wr(0x770,1)){ if(bsp)g_cp_mode=0; } else cp_hwp_req(); }
    else if(g_cp_mode==2){ if(cp_wr(0xC00102B1,1)){ if(bsp)g_cp_mode=0; } else cp_hwp_req(); }
    else if(g_cp_mode==3){ u64 me; if(!cp_rd(0x1A0,&me)&&!(me&(1ULL<<16))){ cp_wr(0x1A0,me|(1ULL<<16)); if(cp_rd(0x1A0,&me)||!(me&(1ULL<<16))){ if(bsp)g_cp_mode=0; } } if(g_cp_mode==3)cp_set(g_cp_lo); }
    else if(g_cp_mode==4)cp_set(g_cp_lo);
    int c=cpu_id()&63; g_cp_cur[c]=g_cp_lo; g_cp_t0[c]=cp_tsc(); g_cp_epp_seen[c]=g_cp_epp_gen;
    if(bsp){ static const char* const M[5]={"firmware","Intel HWP (Speed Shift)","AMD CPPC","Intel SpeedStep (NoovexOS chooses the speed)","AMD P-states (NoovexOS chooses the speed)"};
        char b[160],*p=b; const char* s="cpu power: speed control "; while(*s)*p++=*s++; s=M[g_cp_mode]; while(*s)*p++=*s++; s=g_mwait_hint>=0?", deep idle with MWAIT C":", idle with HLT"; while(*s)*p++=*s++; if(g_mwait_hint>=0)*p++=(char)('0'+g_cp_cstate); *p=0; klog(b); } }
void cpupower_idle_add(u64 cyc){ g_cp_idle[cpu_id()&63]+=cyc; }
u64 cpupower_idle_cycles(int c){ return g_cp_idle[c&63]; }
void cpupower_tick(void){
    int c=cpu_id()&63;
    if((g_cp_mode==1||g_cp_mode==2)&&g_cp_epp_seen[c]!=g_cp_epp_gen){ g_cp_epp_seen[c]=g_cp_epp_gen; cp_hwp_req(); }
    if(g_cp_mode!=3&&g_cp_mode!=4)return;
    u64 t=cp_tsc(), el=t-g_cp_t0[c]; extern u64 g_tsc_hz_cp; u64 per=g_tsc_hz_cp?g_tsc_hz_cp/20:50000000ULL; if(el<per)return;
    u64 id=g_cp_idle[c]-g_cp_i0[c]; g_cp_i0[c]=g_cp_idle[c]; g_cp_t0[c]=t; if(id>el)id=el;
    int busy=(int)(100-id*100/el), want=g_cp_cur[c];
    if(g_cp_mode==3){ int top=g_cp_epp>=0xC0?(g_cp_lo+g_cp_hi)/2:g_cp_hi; if(busy>70)want=top; else if(busy<25)want=g_cp_lo; else if(busy>45&&want<top)want++; else if(want>g_cp_lo&&busy<40)want--; }
    else { int top=g_cp_epp>=0xC0?1:0; if(busy>70)want=top; else if(busy<25)want=g_cp_lo; }
    if(want!=g_cp_cur[c]){ g_cp_cur[c]=want; cp_set(want); g_cp_switches++; } }
void cpupower_saver(int on){ int e=on?0xC0:0x80; if(e!=g_cp_epp){ g_cp_epp=e; g_cp_epp_gen++; } }
int cpupower_info(char* out,int cap){ static const char* const M[5]={"firmware","Intel HWP","AMD CPPC","Intel SpeedStep","AMD P-states"}; char* p=out; const char* s=M[g_cp_mode]; while(*s&&p-out<cap-24)*p++=*s++; if(g_mwait_hint>=0){ s=", idle C"; while(*s)*p++=*s++; *p++=(char)('0'+g_cp_cstate); } *p=0; return (int)(p-out); }
u64 g_tsc_hz_cp;
