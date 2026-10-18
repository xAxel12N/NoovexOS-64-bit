#include "include/nvx/base.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/lib/mem.h"
#include "include/nvx/mm/heap.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/drivers/video/gfx.h"
#include "include/nvx/drivers/video/gq.h"
#include "include/nvx/arch/x86_64/cpuinfo.h"
#include "include/nvx/arch/x86_64/hrtimer.h"
#include "include/nvx/arch/x86_64/smp.h"
#include "include/nvx/gui/theme.h"
#include "include/nvx/gui/wm.h"
#include "include/nvx/core/klog.h"

extern int mx, my; extern int g_gfx; extern u32 g_ram_mb;
extern int igpu_info(const char** name,u32* did,u32* ss,u32* eu,u32* mhz,u32* maxmhz,u32* stolen,u32* tearfree);
extern int gfx_bench_surf(u32** px,int w,int h);

#define BN_ROWS 14
static int bn_test=0, bn_phase=0, bn_step=0, bn_prog=0, bn_done=0, bn_verdict=0, bn_sel=0;
static char bn_row_k[BN_ROWS][28], bn_row_v[BN_ROWS][48]; static int bn_rows=0;
static char bn_why[6][80]; static int bn_nwhy=0;
static u64 bn_t0=0; static u64 bn_pass_ns[8]; static int bn_npass=0;
static int bn_err=0, bn_warn=0;
static u64 bn_acc=0, bn_x=0; static u64 bn_ns_acc=0;
static u64* bn_mem=0; static u64 bn_memn=0;
static int bn_ids[8], bn_nw=0; static u64 bn_single150=0;
static int bn_dts=0, bn_ape=0, bn_tj=100, bn_t_start=-1, bn_t_max=-1; static u64 bn_a0=0, bn_m0=0, bn_tsc0=0, bn_hr0=0; static u32 bn_mhz_eff=0, bn_mhz_tsc=0;
static int bn_sid=0; static u32* bn_spx=0; static u32 bn_gmhz=0, bn_gmax=0;
static int bx_[8], by_[8], bw_[8], bh_[8], bid_[8], bn_hn=0;

static void bn_hit(int x,int y,int w,int h,int id){ if(bn_hn<8){ bx_[bn_hn]=x; by_[bn_hn]=y; bw_[bn_hn]=w; bh_[bn_hn]=h; bid_[bn_hn]=id; bn_hn++; } }
static void bn_add(const char* k,const char* v){ if(bn_rows>=BN_ROWS)return; int i=0; for(;k[i]&&i<27;i++)bn_row_k[bn_rows][i]=k[i]; bn_row_k[bn_rows][i]=0; for(i=0;v[i]&&i<47;i++)bn_row_v[bn_rows][i]=v[i]; bn_row_v[bn_rows][i]=0; bn_rows++; }
static void bn_why_add(int lvl,const char* s){ if(lvl==2)bn_err=1; else if(lvl==1)bn_warn=1; if(bn_nwhy>=6)return; int i=0; for(;s[i]&&i<79;i++)bn_why[bn_nwhy][i]=s[i]; bn_why[bn_nwhy][i]=0; bn_nwhy++; }
static u64 rdmsr_(u32 m){ return cpu_rdmsr(m); }
static int bn_temp(void){ if(!bn_dts)return -1; u64 v=rdmsr_(0x19C); if(!(v&(1ULL<<31)))return -1; return bn_tj-(int)((v>>16)&0x7F); }
static void bn_caps(void){
    u32 a,b,c,d; cpuid(0,&a,&b,&c,&d); u32 maxl=a; int intel=(b==0x756E6547u&&d==0x49656E69u&&c==0x6C65746Eu);
    bn_dts=0; bn_ape=0; bn_tj=100;
    if(maxl>=6){ cpuid(6,&a,&b,&c,&d); bn_dts=(int)(a&1u); bn_ape=(int)(c&1u); }
    if(bn_dts&&intel){ cpuid(1,&a,&b,&c,&d); u32 fam=(a>>8)&15u; if(fam==6){ u32 t=(u32)((rdmsr_(0x1A2)>>16)&0xFF); if(t>=60&&t<=125)bn_tj=(int)t; } } }
static char* bn_ms(char* p,u64 ns){ u32 us=(u32)(ns/1000ULL); p=udec(p,us/1000u); *p++='.'; u32 f=(us%1000u)/10u; if(f<10)*p++='0'; p=udec(p,f); return scopy(p," ms"); }
static char* bn_dec1(char* p,u32 v10){ p=udec(p,v10/10u); *p++='.'; p=udec(p,v10%10u); *p=0; return p; }
static void bn_reset(int t){ bn_test=t; bn_phase=0; bn_step=0; bn_prog=0; bn_done=0; bn_verdict=0; bn_rows=0; bn_nwhy=0; bn_npass=0; bn_err=0; bn_warn=0; bn_acc=0; bn_x=0x9E3779B97F4A7C15ULL; bn_ns_acc=0; bn_nw=0; bn_t_start=-1; bn_t_max=-1; bn_mhz_eff=0; bn_mhz_tsc=0; bn_t0=hr_ns(); }
void bench_start(int t){ if(t<1||t>2)return; bn_sel=t; bn_reset(t); }
static void bn_temp_track(void){ int t=bn_temp(); if(t>=0){ if(bn_t_start<0)bn_t_start=t; if(t>bn_t_max)bn_t_max=t; } }
static void bn_finish(void){
    bn_done=1; bn_prog=100; bn_verdict=bn_err?2:bn_warn?1:0;
    if(!bn_nwhy) bn_why_add(0,bn_test==1?"All results were correct and the speed stayed steady.":"All pictures drawn by the GPU were correct and the speed stayed steady.");
    char b[80],*p=b; p=scopy(p,"bench: "); p=scopy(p,bn_test==1?"CPU ":"GPU "); p=scopy(p,bn_verdict==2?"problem":bn_verdict==1?"warning":"healthy"); *p=0; klog(b); }
static void bn_cpu_step(void){
    char v[48],*p;
    if(bn_phase==0){
        bn_caps(); bn_temp_track();
        if(bn_ape){ bn_a0=rdmsr_(0xE8); bn_m0=rdmsr_(0xE7); }
        bn_tsc0=hr_rdtsc(); bn_hr0=hr_ns();
        u64 t0=hr_ns(); u64 r=smp_compute(100000); u64 dt=hr_ns()-t0;
        bn_pass_ns[bn_npass++]=dt; if(r!=9592ULL){ bn_why_add(2,"Wrong answer counting prime numbers - the processor made a mistake."); }
        if(bn_npass>=4){
            u64 tsc1=hr_rdtsc(), hr1=hr_ns(); if(hr1>bn_hr0) bn_mhz_tsc=(u32)((tsc1-bn_tsc0)*1000ULL/(hr1-bn_hr0));
            if(bn_ape){ u64 a1=rdmsr_(0xE8), m1=rdmsr_(0xE7); if(m1>bn_m0) bn_mhz_eff=(u32)((u64)bn_mhz_tsc*(a1-bn_a0)/(m1-bn_m0)); }
            u64 mn=bn_pass_ns[0], mxp=bn_pass_ns[0], sum=0; for(int k=0;k<bn_npass;k++){ if(bn_pass_ns[k]<mn)mn=bn_pass_ns[k]; if(bn_pass_ns[k]>mxp)mxp=bn_pass_ns[k]; sum+=bn_pass_ns[k]; }
            p=v; p=bn_ms(p,sum/(u64)bn_npass); p=scopy(p," per pass"); *p=0; bn_add("Single core (primes)",v);
            if(mn&&mxp*100ULL>mn*135ULL) bn_why_add(1,"Single-core speed changed a lot between passes (possible throttling).");
            bn_phase=1; bn_step=0; }
        bn_prog=4+bn_npass*4; return; }
    if(bn_phase==1){
        u64 x=bn_x, acc=bn_acc; u64 t0=hr_ns();
        for(int i=0;i<2000000;i++){ x^=x<<13; x^=x>>7; x^=x<<17; acc=acc*31u+(x>>11)+(u64)(i^bn_step); }
        bn_ns_acc+=hr_ns()-t0; bn_x=x; bn_acc=acc; bn_step++; bn_prog=20+bn_step*3;
        if(bn_step>=8){ if(bn_acc!=0x80f77204ff5617d5ULL) bn_why_add(2,"Wrong answer in the integer math test - the processor made a mistake.");
            p=v; u64 mops=bn_ns_acc?16000000ULL*1000ULL/bn_ns_acc:0; p=udec(p,(u32)mops); p=scopy(p," million steps/s"); *p=0; bn_add("Integer math",v);
            bn_phase=2; bn_step=0; }
        return; }
    if(bn_phase==2){
        if(bn_step==0){ bn_memn=(32ULL<<20)/8ULL; bn_mem=(u64*)kmalloc((unsigned long)(bn_memn*8ULL*2ULL)); if(!bn_mem){ bn_add("Memory","not enough free memory to test"); bn_phase=3; bn_step=0; return; } bn_ns_acc=0; }
        u64* a=bn_mem; u64* b=bn_mem+bn_memn; u64 n=bn_memn;
        if(bn_step<3){ u64 seed=0xA5A5A5A5DEADBEEFULL^(u64)bn_step; u64 t0=hr_ns(); for(u64 i=0;i<n;i++)a[i]=seed^(i*0x9E3779B97F4A7C15ULL); u64 t1=hr_ns(); { cpu_copy64(b,a,(unsigned long)n); } u64 t2=hr_ns();
            u64 bad=0; for(u64 i=0;i<n;i++){ u64 e=seed^(i*0x9E3779B97F4A7C15ULL); if(a[i]!=e||b[i]!=e)bad++; }
            bn_ns_acc+=t2-t1; bn_pass_ns[bn_step]=t1-t0; if(bad){ char w[80],*q=w; q=scopy(q,"Memory test found "); q=udec(q,(u32)bad); q=scopy(q," wrong values - the RAM may be faulty."); *q=0; bn_why_add(2,w); }
            bn_step++; bn_prog=44+bn_step*6; return; }
        u64 bytes=n*8ULL*3ULL; u64 mbs=bn_ns_acc?bytes*1000ULL/bn_ns_acc:0; p=v; p=bn_dec1(p,(u32)(mbs/100ULL)); p=scopy(p," GB/s copy"); *p=0; bn_add("Memory speed",v);
        { u64 wr=bn_pass_ns[0]+bn_pass_ns[1]+bn_pass_ns[2]; u64 wmb=wr?bytes*1000ULL/wr:0; p=v; p=bn_dec1(p,(u32)(wmb/100ULL)); p=scopy(p," GB/s write, 64 MB checked"); *p=0; bn_add("Memory write",v); }
        kfree(bn_mem); bn_mem=0; bn_phase=3; bn_step=0; return; }
    if(bn_phase==3){
        if(bn_step==0){ u64 t0=hr_ns(); u64 r=smp_compute(150000); bn_single150=hr_ns()-t0; if(r!=13848ULL)bn_why_add(2,"Wrong prime count on the main core.");
            bn_nw=g_cores_online-1; if(bn_nw>8)bn_nw=8;
            if(bn_nw<=0){ bn_add("Other cores","only one core is running"); bn_why_add(1,"Only one processor core is running, so multi-core speed was not tested."); bn_phase=4; return; }
            g_smp_work_on=1; for(int k=0;k<bn_nw;k++)bn_ids[k]=smp_submit(150000ULL); bn_t0=hr_ns(); bn_step=1; bn_prog=66; return; }
        int left=0; for(int k=0;k<bn_nw;k++) if(bn_ids[k]>=0&&g_jobs[bn_ids[k]].state!=3)left++;
        u64 el=hr_ns()-bn_t0; bn_temp_track();
        if(left&&el<8000000000ULL){ bn_prog=66+(int)(el/200000000ULL); if(bn_prog>84)bn_prog=84; return; }
        int bad=0, lost=0; for(int k=0;k<bn_nw;k++){ int id=bn_ids[k]; if(id<0){ lost++; continue; } if(g_jobs[id].state!=3)lost++; else if(g_jobs[id].result!=13848ULL)bad++; if(g_jobs[id].state!=2)g_jobs[id].state=0; }
        if(bad) bn_why_add(2,"A processor core returned a wrong answer.");
        if(lost) bn_why_add(2,"A processor core did not finish its work in time.");
        p=v; p=udec(p,(u32)(bn_nw+1)); p=scopy(p," cores, all together "); p=bn_ms(p,el); *p=0; bn_add("Multi-core",v);
        if(!lost&&el){ u32 sc10=(u32)((u64)bn_nw*bn_single150*10ULL/el); p=v; p=bn_dec1(p,sc10); p=scopy(p,"x faster than 1 core ("); p=udec(p,(u32)bn_nw); p=scopy(p," workers)"); *p=0; bn_add("Core scaling",v);
            if(sc10*10u<(u32)bn_nw*55u) bn_why_add(1,"The extra cores are much slower than expected (busy, or throttled)."); }
        bn_phase=4; return; }
    if(bn_phase==4){
        bn_temp_track();
        if(bn_mhz_tsc){ p=v; p=udec(p,bn_mhz_tsc); p=scopy(p," MHz base"); if(bn_mhz_eff){ p=scopy(p,", "); p=udec(p,bn_mhz_eff); p=scopy(p," MHz under load"); } *p=0; bn_add("Clock",v);
            if(bn_mhz_eff&&bn_mhz_eff*100u<bn_mhz_tsc*55u) bn_why_add(1,"The processor ran far below its base clock under load (power saving or overheating)."); }
        if(bn_dts&&bn_t_max>=0){ p=v; p=udec(p,(u32)bn_t_start); p=scopy(p," C at start, "); p=udec(p,(u32)bn_t_max); p=scopy(p," C peak (limit "); p=udec(p,(u32)bn_tj); p=scopy(p," C)"); *p=0; bn_add("Temperature",v);
            if(bn_t_max>=bn_tj-8) bn_why_add(1,"The processor got very hot - check the fan and air vents.");
            u64 ts=rdmsr_(0x19C); if(ts&(1ULL<<1)) bn_why_add(1,"The processor has slowed itself down because of heat since power-on."); }
        else bn_add("Temperature","no sensor available");
        bn_finish(); } }
static void bn_gpu_step(void){
    char v[48],*p;
    if(bn_phase==0){
        if(g_gfx!=GFX_GPU&&g_gfx!=GFX_EMU){ bn_add("GPU","GPU drawing is off"); bn_why_add(1,"GPU drawing is off, so the graphics chip could not be tested. Turn it on in Settings > Graphics."); bn_finish(); return; }
        const char* nm=0; u32 did=0,ss=0,eu=0,mhz=0,mxz=0,stl=0,tf=0; int hg=igpu_info(&nm,&did,&ss,&eu,&mhz,&mxz,&stl,&tf);
        if(hg&&nm) bn_add("Graphics",nm); else bn_add("Graphics",g_gfx==GFX_EMU?"emulated on the CPU (test mode)":"GPU");
        if(hg){ p=v; p=udec(p,ss*eu); p=scopy(p," execution units"); *p=0; bn_add("Size",v); }
        bn_gmax=mxz; bn_gmhz=0;
        bn_sid=gfx_bench_surf(&bn_spx,1024,1024); if(bn_sid<=0||!bn_spx){ bn_why_add(2,"Could not get GPU memory for the test."); bn_finish(); return; }
        bn_phase=1; bn_step=0; bn_npass=0; bn_prog=6; return; }
    if(bn_phase>=1&&bn_phase<=3){
        u64 t0=hr_ns();
        for(int k=0;k<48;k++){ u32 c=(u32)(0x102030u+(u32)k*0x030507u)&0xFFFFFFu;
            if(bn_phase==1) gq_fill(bn_sid,0,0,1024,1024,c);
            else if(bn_phase==2) gq_blend(bn_sid,0,0,1024,1024,c,96);
            else gq_copy(bn_sid,(k&1)?0:512,0,512,1024,bn_sid,(k&1)?512:0,0,256,0); }
        gfx_sync(); u64 dt=hr_ns()-t0; bn_pass_ns[bn_npass++]=dt;
        { const char* nm=0; u32 did=0,ss=0,eu=0,mhz=0,mxz=0,stl=0,tf=0; if(igpu_info(&nm,&did,&ss,&eu,&mhz,&mxz,&stl,&tf)&&mhz>bn_gmhz)bn_gmhz=mhz; }
        if(g_gfx!=GFX_GPU&&g_gfx!=GFX_EMU){ bn_why_add(2,"The GPU stopped responding during the test and drawing fell back to the CPU."); bn_finish(); return; }
        bn_prog=6+((bn_phase-1)*3+bn_npass)*8;
        if(bn_npass>=3){ u64 mn=bn_pass_ns[0], mxp=bn_pass_ns[0], best=bn_pass_ns[0]; for(int k=1;k<3;k++){ if(bn_pass_ns[k]<mn)mn=bn_pass_ns[k]; if(bn_pass_ns[k]>mxp)mxp=bn_pass_ns[k]; } best=mn;
            u64 mpx=best?48ULL*1024ULL*1024ULL*1000ULL/best:0;
            p=v; p=udec(p,(u32)mpx); p=scopy(p," million pixels/s"); *p=0; bn_add(bn_phase==1?"Fill":bn_phase==2?"Blend (see-through)":"Copy",v);
            if(mn&&mxp*100ULL>mn*140ULL) bn_why_add(1,"GPU speed changed a lot between passes.");
            bn_phase++; bn_npass=0; }
        return; }
    if(bn_phase==4){
        u32 c0=0x203040u, c1=0xF08020u; gq_fill(bn_sid,0,0,1024,1024,c0); gq_blend(bn_sid,0,0,512,512,c1,128); gq_copy(bn_sid,512,512,512,512,bn_sid,0,0,256,0); gq_fill(bn_sid,512,0,512,512,c1); gfx_sync();
        u32 eb=gq_bl(c0,c1,128)&0xFFFFFFu; u32 bad=0, tot=0;
        for(int y=0;y<1024;y+=3) for(int x=0;x<1024;x+=3){ u32 e=(x<512&&y<512)?eb:(x>=512&&y<512)?c1:(x>=512&&y>=512)?eb:c0; u32 g=bn_spx[(u64)y*1024+x]&0xFFFFFFu; tot++;
            int dr=(int)((g>>16)&255)-(int)((e>>16)&255), dg=(int)((g>>8)&255)-(int)((e>>8)&255), db=(int)(g&255)-(int)(e&255);
            if(dr<-2||dr>2||dg<-2||dg>2||db<-2||db>2)bad++; }
        p=v; if(bad){ p=udec(p,bad); p=scopy(p," of "); p=udec(p,tot); p=scopy(p," pixels wrong"); } else { p=udec(p,tot); p=scopy(p," pixels checked, all correct"); } *p=0; bn_add("Correctness",v);
        if(bad) bn_why_add(2,"The GPU drew wrong pixels - the graphics chip or driver has a problem.");
        if(bn_gmax){ p=v; p=udec(p,bn_gmhz); p=scopy(p," of "); p=udec(p,bn_gmax); p=scopy(p," MHz"); *p=0; bn_add("GPU clock under load",v);
            if(bn_gmhz&&bn_gmhz*100u<bn_gmax*70u) bn_why_add(1,"The GPU did not reach its full clock under load (power limit or heat)."); }
        { int t; bn_caps(); t=bn_temp(); if(t>=0){ p=v; p=udec(p,(u32)t); p=scopy(p," C (chip)"); *p=0; bn_add("Temperature",v); if(t>=bn_tj-8)bn_why_add(1,"The chip is very hot - check the fan and air vents."); } }
        bn_finish(); } }
void bench_tick(void){ if(!bn_test||bn_done)return; if(!WN[W_BENCH].open){ return; } if(bn_test==1)bn_cpu_step(); else bn_gpu_step(); }
static void bn_card(int x,int y,int w,int h,int t,const char* title,const char* sub,int id){
    int on=bn_sel==t, hov=mx>=x&&mx<x+w&&my>=y&&my<y+h;
    fill_round(x,y,w,h,12,15,on?mix(rgb(255,255,255),ACC,30):hov?rgb(248,249,252):rgb(255,255,255));
    if(on){ for(int k=0;k<w-24;k++){ putpx(x+12+k,y,ACC); putpx(x+12+k,y+h-1,ACC); } }
    int ix=x+20, iy=y+22;
    if(t==1){ fill_round(ix,iy,40,40,6,15,rgb(60,70,96)); fill_round(ix+8,iy+8,24,24,3,15,rgb(120,140,190)); for(int k=0;k<4;k++){ fill_rect(ix+6+k*9,iy-5,3,5,rgb(150,160,180)); fill_rect(ix+6+k*9,iy+40,3,5,rgb(150,160,180)); fill_rect(ix-5,iy+6+k*9,5,3,rgb(150,160,180)); fill_rect(ix+40,iy+6+k*9,5,3,rgb(150,160,180)); } }
    else { fill_round(ix-4,iy+4,48,32,5,15,rgb(40,120,80)); fill_circle(ix+28,iy+20,10,rgb(30,34,46)); fill_circle(ix+28,iy+20,6,rgb(110,200,150)); fill_rect(ix,iy+12,10,4,rgb(220,200,120)); fill_rect(ix,iy+20,10,4,rgb(220,200,120)); }
    dstr(x+80,y+20,title,rgb(26,30,42),2); dstr(x+80,y+48,sub,rgb(110,118,138),1);
    int bx=x+w-110, byy=y+h-44; int run=bn_test==t&&!bn_done;
    fill_round(bx,byy,94,30,8,15,run?rgb(200,204,214):ACC); dstr_ctr(bx+47,byy+9,run?"Running...":"Run test",rgb(255,255,255),1);
    bn_hit(x,y,w,h,10+t); bn_hit(bx,byy,94,30,t); }
void bench_draw(int i){
    win_frame(i,"Benchmark"); bn_hn=0;
    int wx=WN[i].x+1, wy=WN[i].y+TITLEH, ww=WN[i].w-2, wh=WN[i].h-TITLEH-1;
    fill_rect(wx,wy,ww,wh,rgb(243,244,248));
    dstr(wx+20,wy+16,"Choose what to test",rgb(26,30,42),2);
    dstr(wx+20,wy+44,"The test checks speed, correct answers, heat and clock, then says if the part looks healthy.",rgb(110,118,138),1);
    int cw=(ww-60)/2; const char* nm=0; u32 a1=0,a2=0,a3=0,a4=0,a5=0,a6=0,a7=0; int hg=igpu_info(&nm,&a1,&a2,&a3,&a4,&a5,&a6,&a7);
    char sub[64],*p=sub; p=scopy(p,g_brand[0]?g_brand:"Processor"); *p=0; if(slen(sub)>40){ sub[40]=0; }
    bn_card(wx+20,wy+70,cw,110,1,"CPU",sub,0);
    bn_card(wx+40+cw,wy+70,cw,110,2,"GPU",hg&&nm?nm:(g_gfx?"Graphics":"GPU drawing is off"),0);
    int y=wy+198;
    if(!bn_test){ dstr(wx+20,y,"Pick CPU or GPU and press Run test. It takes about 5 to 15 seconds.",rgb(110,118,138),1); return; }
    fill_round(wx+20,y,ww-40,10,5,15,rgb(226,229,238)); int pw=(ww-40)*bn_prog/100; if(pw>10)fill_round(wx+20,y,pw,10,5,15,bn_done?(bn_verdict==2?rgb(220,70,60):bn_verdict==1?rgb(230,160,40):rgb(46,170,90)):ACC);
    y+=24;
    if(bn_done){ u32 bc=bn_verdict==2?rgb(220,70,60):bn_verdict==1?rgb(230,160,40):rgb(46,170,90); const char* vt=bn_verdict==2?"Problem found":bn_verdict==1?"Works, with warnings":"Healthy";
        fill_round(wx+20,y,ww-40,46,10,15,mix(rgb(255,255,255),bc,40)); fill_circle(wx+44,y+23,12,bc);
        if(bn_verdict==0){ for(int k=0;k<5;k++)fill_rect(wx+37+k,y+22+k,2,2,rgb(255,255,255)); for(int k=0;k<9;k++)fill_rect(wx+42+k,y+26-k,2,2,rgb(255,255,255)); } else dstr_ctr(wx+44,y+15,"!",rgb(255,255,255),2);
        p=sub; p=scopy(p,bn_test==1?"CPU: ":"GPU: "); p=scopy(p,vt); *p=0; dstr(wx+66,y+14,sub,rgb(26,30,42),2); y+=58; }
    else { dstr(wx+20,y,bn_test==1?"Testing the processor...":"Testing the graphics chip...",rgb(26,30,42),1); y+=22; }
    fill_round(wx+20,y,ww-40,bn_rows*22+16,10,15,rgb(255,255,255));
    for(int k=0;k<bn_rows;k++){ dstr(wx+36,y+10+k*22,bn_row_k[k],rgb(110,118,138),1); dstr(wx+230,y+10+k*22,bn_row_v[k],rgb(26,30,42),1); }
    y+=bn_rows*22+28;
    if(bn_done) for(int k=0;k<bn_nwhy&&y<wy+wh-18;k++){ dstr(wx+24,y,"-",rgb(110,118,138),1); dstr(wx+36,y,bn_why[k],rgb(50,56,72),1); y+=18; } }
void bench_click(int px,int py){ for(int k=bn_hn-1;k>=0;k--) if(px>=bx_[k]&&px<bx_[k]+bw_[k]&&py>=by_[k]&&py<by_[k]+bh_[k]){ int id=bid_[k];
        if(id==1||id==2){ if(!(bn_test&&!bn_done))bench_start(id); } else if(id==11||id==12) bn_sel=id-10; return; } }
void bench_key(int c){ if(c=='\n'||c=='\r'||c==' '){ if(bn_sel&&!(bn_test&&!bn_done))bench_start(bn_sel); } else if(c=='c'||c=='C')bn_sel=1; else if(c=='g'||c=='G')bn_sel=2; }
int bench_busy(void){ return bn_test&&!bn_done; }
