#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/smp.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/net/http.h"
#include "include/nvx/arch/x86_64/interrupts.h"
#include "include/nvx/core/power.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "include/nvx/arch/x86_64/hrtimer.h"
#define HR_LAPIC_VEC 0x40
#define HR_PIT_COUNT 59659
#define HR_PIT_WIN_NS 49999930ULL
struct nvx_g_hr_t g_hr;
u64 hr_rdtsc(void){ u32 a,d; __asm__ volatile("rdtsc":"=a"(a),"=d"(d)); return ((u64)d<<32)|a; }
static void hr_cpuid(u32 leaf,u32 sub,u32* r){ __asm__ volatile("cpuid":"=a"(r[0]),"=b"(r[1]),"=c"(r[2]),"=d"(r[3]):"a"(leaf),"c"(sub)); }
static u64 hr_rdmsr(u32 m){ u32 a,d; __asm__ volatile("rdmsr":"=a"(a),"=d"(d):"c"(m)); return ((u64)d<<32)|a; }
static void hr_wrmsr(u32 m,u64 v){ __asm__ volatile("wrmsr"::"a"((u32)v),"d"((u32)(v>>32)),"c"(m)); }
static u32 hr_rd32(u64 a){ return *(volatile u32*)a; }
static u64 hr_rd64(u64 a){ return *(volatile u64*)a; }
static void hr_wr64(u64 a,u64 v){ *(volatile u64*)a=v; }
static u64 hr_irqsave(void){ u64 f; __asm__ volatile("pushfq; popq %0; cli":"=r"(f)::"memory"); return f; }
static void hr_irqrestore(u64 f){ if(f&0x200)__asm__ volatile("sti":::"memory"); }
static void hr_set_why(const char* s){ int i=0; while(s[i]&&i<47){ g_hr.why[i]=s[i]; i++; } g_hr.why[i]=0; }
u64 hr_mul_shr32(u64 a,u64 m){ return (u64)(((unsigned __int128)a*m)>>32); }
u64 hr_muldiv(u64 a,u64 b,u64 c){ if(!c)return 0; return (a/c)*b+((a%c)*b)/c; }
u64 hr_hpet_count(void){
    if(g_hr.hpet_64)return hr_rd64(g_hr.hpet_va+0xF0);
    u64 f=hr_irqsave(); u32 v=hr_rd32(g_hr.hpet_va+0xF0); if(v<g_hr.hpet_last)g_hr.hpet_hi+=0x100000000ULL; g_hr.hpet_last=v; u64 r=g_hr.hpet_hi|v; hr_irqrestore(f); return r; }
static u64 hr_hpet_ns(void){ return hr_mul_shr32(hr_hpet_count()-g_hr.hpet0,g_hr.hpet_mult); }
static int hr_hpet_init(void){
    const u8* t=acpi_find_table("HPET",0); if(!t){ hr_set_why("no HPET table"); return 0; }
    if(acpi_r32(t+4)<56||t[40]!=0){ hr_set_why("HPET table not in memory space"); return 0; }
    u64 pa=acpi_r64(t+44); if(!pa||(pa&7)){ hr_set_why("HPET address invalid"); return 0; }
    u64 va=acpi_hi_map(pa); if(!va){ hr_set_why("HPET above 512 GB"); return 0; }
    u64 cap=hr_rd64(va); u64 fs=cap>>32; if(cap==~0ULL||fs==0||fs>100000000ULL){ hr_set_why("HPET period invalid"); return 0; }
    g_hr.hpet_pa=pa; g_hr.hpet_va=va; g_hr.hpet_fs=fs; g_hr.hpet_64=(int)((cap>>13)&1); g_hr.hpet_ntim=(int)((cap>>8)&31)+1;
    u64 conf=hr_rd64(va+0x10); if((conf&3)!=1)hr_wr64(va+0x10,(conf&~2ULL)|1);
    u32 c0=hr_rd32(va+0xF0); int moved=0; for(int i=0;i<20000;i++){ if(hr_rd32(va+0xF0)!=c0){ moved=1; break; } }
    if(!moved){ hr_set_why("HPET counter does not run"); return 0; }
    g_hr.hpet_mult=(fs<<32)/1000000ULL;
    g_hr.hpet_last=hr_rd32(va+0xF0); g_hr.hpet_hi=0; g_hr.hpet_ok=1; g_hr.hpet0=hr_hpet_count(); return 1; }
static u32 hr_lapic_rd(u32 off){ if(g_hr.lapic_x2)return (u32)hr_rdmsr(0x800+(off>>4)); return hr_rd32(g_lapic_base+off); }
static void hr_lapic_wr(u32 off,u32 v){ if(g_hr.lapic_x2){ hr_wrmsr(0x800+(off>>4),v); return; } *(volatile u32*)(g_lapic_base+off)=v; }
static int hr_lapic_init(void){
    u64 b=hr_rdmsr(0x1B); if(!(b&(1ULL<<11))){ hr_set_why("local APIC disabled by firmware"); return 0; }
    g_hr.lapic_x2=(b>>10)&1; if(!g_hr.lapic_x2){ u64 pa=b&0xFFFFFF000ULL; if(!pa||pa>=0x100000000ULL){ hr_set_why("local APIC above 4 GB"); return 0; } g_lapic_base=pa; }
    u32 svr=hr_lapic_rd(0xF0); if(!(svr&0x100)){ hr_lapic_wr(0x80,0); hr_lapic_wr(0xF0,0x1FF); hr_lapic_wr(0x350,0x700); hr_lapic_wr(0x360,0x400); }
    g_hr.lapic_id=g_hr.lapic_x2?hr_lapic_rd(0x20):(hr_lapic_rd(0x20)>>24);
    hr_lapic_wr(0x320,0x10000u|HR_LAPIC_VEC); hr_lapic_wr(0x3E0,0x3); hr_lapic_wr(0x380,0); return 1; }
static int hr_calibrate(void){
    u64 best_tsc=0, best_lap=0, win=0; int ref=0;
    for(int run=0;run<3;run++){
        u64 f=hr_irqsave(); u64 dt=0, dl=0, wn=0; int ok=0;
        if(g_hr.lapic_ok){ hr_lapic_wr(0x320,0x10000u|HR_LAPIC_VEC); hr_lapic_wr(0x3E0,0x3); hr_lapic_wr(0x380,0xFFFFFFFFu); }
        if(g_hr.hpet_ok){ u64 h0=hr_hpet_count(), t0=hr_rdtsc(); u32 l0=g_hr.lapic_ok?hr_lapic_rd(0x390):0; u64 h1=h0;
            for(int i=0;i<400000;i++){ h1=hr_hpet_count(); if(hr_mul_shr32(h1-h0,g_hr.hpet_mult)>=20000000ULL){ ok=1; break; } }
            u64 t1=hr_rdtsc(); u32 l1=g_hr.lapic_ok?hr_lapic_rd(0x390):0; wn=hr_mul_shr32(h1-h0,g_hr.hpet_mult); dt=t1-t0; dl=(u64)(l0-l1); ref=1; }
        else { u8 g=inb(0x61); outb(0x61,(u8)((g&~0x02)|0x01)); outb(0x43,0xB0); outb(0x42,(u8)(HR_PIT_COUNT&0xFF)); outb(0x42,(u8)(HR_PIT_COUNT>>8));
            g=inb(0x61); outb(0x61,(u8)(g&~0x01)); outb(0x61,(u8)(g|0x01)); u64 t0=hr_rdtsc(); u32 l0=g_hr.lapic_ok?hr_lapic_rd(0x390):0;
            for(int i=0;i<200000;i++){ if(inb(0x61)&0x20){ ok=1; break; } }
            u32 l1=g_hr.lapic_ok?hr_lapic_rd(0x390):0; u64 t1=hr_rdtsc(); wn=HR_PIT_WIN_NS; dt=t1-t0; dl=(u64)(l0-l1); ref=2; }
        if(g_hr.lapic_ok)hr_lapic_wr(0x380,0);
        hr_irqrestore(f);
        if(!ok||!wn){ if(ref==2)break; continue; }
        if(!best_tsc||dt*win<best_tsc*wn){ best_tsc=dt; best_lap=dl; win=wn; } }
    if(!win){ g_hr.ref=0; return 0; }
    g_hr.ref=ref; g_hr.tsc_hz=hr_muldiv(best_tsc,1000000000ULL,win); g_hr.lapic_hz=g_hr.lapic_ok?hr_muldiv(best_lap,1000000000ULL,win):0;
    return 1; }
static void hr_cpuid_tsc(void){
    u32 r[4]; hr_cpuid(0,0,r); u32 maxl=r[0]; if(maxl>=0x15){ hr_cpuid(0x15,0,r); if(r[0]&&r[1]&&r[2]){ g_hr.tsc_hz=(u64)r[2]*r[1]/r[0]; g_hr.ref=3; if(!g_hr.lapic_hz&&g_hr.lapic_ok)g_hr.lapic_hz=(u64)r[2]/16; return; } }
    if(maxl>=0x16){ hr_cpuid(0x16,0,r); if(r[0]&0xFFFF){ g_hr.tsc_hz=(u64)(r[0]&0xFFFF)*1000000ULL; g_hr.ref=4; } } }
u64 hr_ns(void){
    if(g_hr.src==2)return hr_mul_shr32(hr_rdtsc()-g_hr.tsc0,g_hr.tsc_mult);
    if(g_hr.src==1)return hr_hpet_ns();
    return g_ticks*10000000ULL; }
static u64 hr_us(void){ return hr_ns()/1000ULL; }
int hr_precise(void){ return g_hr.src!=0; }
void hr_delay_us(u32 us){
    if(!g_hr.src){ for(u32 i=0;i<us;i++)io_wait(); return; }
    u64 end=hr_ns()+(u64)us*1000ULL, lim=(u64)us*1000ULL+100000ULL; for(u64 i=0;i<lim;i++){ if(hr_ns()>=end)return; __asm__ volatile("pause"); } }
void hr_delay_ms(u32 ms){ while(ms--)hr_delay_us(1000); }
void hr_lapic_isr(void){ g_hr.lapic_irqs++; hr_lapic_wr(0xB0,0); }
int hr_wake_at(u64 ns){
    if(!g_hr.lapic_ok||!g_hr.lapic_hz||!g_hr.src)return 0;
    u64 now=hr_ns(); if(ns<=now)return 0; u64 d=ns-now; if(d>1000000000ULL)d=1000000000ULL;
    u64 cnt=hr_muldiv(d,g_hr.lapic_hz,1000000000ULL); if(cnt<1)cnt=1; if(cnt>0xFFFFFFFFULL)cnt=0xFFFFFFFFULL;
    hr_lapic_wr(0x320,HR_LAPIC_VEC); hr_lapic_wr(0x3E0,0x3); hr_lapic_wr(0x380,(u32)cnt); g_hr.wakes++; return 1; }
void hr_wake_cancel(void){ if(g_hr.lapic_ok){ hr_lapic_wr(0x380,0); hr_lapic_wr(0x320,0x10000u|HR_LAPIC_VEC); } }
static int hr_lapic_periodic(u32 hz){
    if(!g_hr.lapic_ok||!g_hr.lapic_hz||!hz)return 0; u64 cnt=g_hr.lapic_hz/hz; if(cnt<1||cnt>0xFFFFFFFFULL)return 0;
    hr_lapic_wr(0x3E0,0x3); hr_lapic_wr(0x320,HR_LAPIC_VEC|(1u<<17)); hr_lapic_wr(0x380,(u32)cnt); return 1; }
void hr_init(void){
    if(g_hr.ready)return;
    u32 r[4]; hr_cpuid(0x80000000u,0,r); if(r[0]>=0x80000007u){ hr_cpuid(0x80000007u,0,r); g_hr.tsc_inv=(r[3]>>8)&1; }
    hr_cpuid(1,0,r); g_hr.tsc_ok=(r[3]>>4)&1;
    hr_hpet_init();
    g_hr.lapic_ok=hr_lapic_init();
    if(!hr_calibrate())hr_cpuid_tsc();
    if(g_hr.tsc_ok&&g_hr.tsc_hz>=10000000ULL){ g_hr.tsc_mult=(1000000000ULL<<32)/g_hr.tsc_hz; g_hr.tsc0=hr_rdtsc(); }
    else g_hr.tsc_ok=0;
    if(g_hr.lapic_ok&&g_hr.lapic_hz<100000ULL)g_hr.lapic_ok=0;
    if(g_hr.tsc_ok&&g_hr.tsc_inv)g_hr.src=2; else if(g_hr.hpet_ok)g_hr.src=1; else if(g_hr.tsc_ok)g_hr.src=2; else g_hr.src=0;
    g_hr.boot_ns_ticks=g_ticks; g_hr.ready=1;
    klog(g_hr.src==2?(g_hr.tsc_inv?"timers: clock = invariant TSC":"timers: clock = TSC (not invariant)"):g_hr.src==1?"timers: clock = HPET":"timers: no precise clock, using the 100 Hz PIT");
    if(g_hr.hpet_ok)klog("timers: HPET running"); if(g_hr.lapic_ok)klog("timers: local APIC timer calibrated"); }
