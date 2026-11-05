#include "include/nvx/base.h"
#include "include/nvx/core/crash.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/assets/fontaa.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/core/proc/sched.h"
#include "include/nvx/drivers/usb/xhci.h"

extern volatile u64 g_ticks;
extern volatile u32 g_ml_beat;
extern volatile int g_ml_stage;
extern int g_state;
extern int disk_ok, ufat_ok, ufs_n;
extern u32 g_ram_mb;
extern u8 g_pstack[MAXPROC][16384];
int fs_save_bin(const char*,const u8*,int,int);
int fs_find_name(const char*);
char* fsd(int);
void nvx3_sync_all(void);
int ufat_write_file(const char*,const char*,int);
int ufat_poke(const char*,const char*,int);
int ufat_read(int,char*,int);
int msd_autoinit_safe(void);
void power_reboot(void);
void notify(const char*,const char*);
void toast(const char*);
void editor_open_buf(const char*,int);
void smp_halt_others(void);
unsigned long heap_used(void);
void xhci_mouse_poll(void);
int kpop(void);

#define CX_ST_DESKTOP 2
#define CX_ST_LIVE 7
#define CX_KSYM_CAP 393216
#define CX_REP_CAP 6144
#define CX_CL_N 48
#define CX_CL_W 120
#define CX_STK_MAX 80
#define CX_STK_MAGIC 0x5AFE57AC4B0C0DE5ULL
#define CX_VERS "NoovexOS 64 v10.60"

u8 g_ksyms[CX_KSYM_CAP] __attribute__((section(".data.ksyms"),aligned(16)))={'N','V','X','K','S','Y','M','1'};
u64 __stack_chk_guard=0x00E4B79A5C3D2F17ULL;
volatile u32 g_frz_still=0;
int g_bsg_on=0;

static char g_cx_cl[CX_CL_N][CX_CL_W]; static u32 g_cx_cl_n;
static char g_cx_rep[CX_REP_CAP]; static int g_cx_rep_n;
static char g_cx_frz_rep[CX_REP_CAP]; static int g_cx_frz_n; static char g_cx_frz_scr[CX_REP_CAP];
static volatile int g_cx_frz_shown, g_cx_frz_saved, g_cx_frz_redraw, g_cx_frz_done_s;
static int g_cx_boot_done, g_cx_notice_t;
static u64 g_cx_desk_t; unsigned long g_cx_heap_kb;
static char g_cx_drv[24]; static char g_cx_drv_skip[8][24]; static int g_cx_drv_nskip, g_cx_drv_store, g_cx_drv_notice;
static struct { const char* nm; int idx; u64* base; } g_cx_stk[CX_STK_MAX]; static int g_cx_stk_n; static int g_cx_stk_hit;
static u64 g_cx_bsg_pt[512] __attribute__((aligned(4096)));
static u8 g_cx_pstk[65536] __attribute__((aligned(16)));
static regs_t g_cx_pr; static const char* g_cx_pmsg; static int g_cx_pkind;

typedef struct { char* p; char* e; } cx_buf;
static void cx_c(cx_buf* b,char c){ if(b->p<b->e)*b->p++=c; }
static void cx_s(cx_buf* b,const char* s){ while(s&&*s&&b->p<b->e)*b->p++=*s++; }
static void cx_u(cx_buf* b,u64 v){ char t[24]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)cx_c(b,t[--n]); }
static void cx_x(cx_buf* b,u64 v){ static const char H[]="0123456789abcdef"; cx_s(b,"0x"); int st=0; for(int i=60;i>=0;i-=4){ int d=(int)((v>>i)&15); if(d||st||!i){ cx_c(b,H[d]); st=1; } } }
static int cx_eq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
static int cx_ieq(const char* a,const char* b){ while(*a&&*b){ char x=*a,y=*b; if(x>='a'&&x<='z')x=(char)(x-32); if(y>='a'&&y<='z')y=(char)(y-32); if(x!=y)return 0; a++; b++; } return *a==*b; }
static void cx_cp(char* d,const char* s,int n){ int k=0; while(s&&s[k]&&k<n-1){ d[k]=s[k]; k++; } d[k]=0; }

static int cx_ks_ok(void){ return g_ksyms[0]=='N'&&g_ksyms[1]=='V'&&g_ksyms[2]=='X'&&g_ksyms[3]=='K'&&g_ksyms[4]=='S'&&g_ksyms[5]=='Y'&&g_ksyms[6]=='M'&&g_ksyms[7]=='2'; }
static u32 cx_ks_u32(u32 o){ return (u32)g_ksyms[o]|((u32)g_ksyms[o+1]<<8)|((u32)g_ksyms[o+2]<<16)|((u32)g_ksyms[o+3]<<24); }
int kname_ready(void){ return cx_ks_ok(); }
static int cx_ks_text(u64 a){
    if(cx_ks_ok())return a>=cx_ks_u32(16)&&a<cx_ks_u32(20);
    return a>=0x100000ULL&&a<0x06500000ULL; }
int kname_find(u64 a,const char** name,u64* off){
    if(!cx_ks_ok())return 0; u32 n=cx_ks_u32(8), so=cx_ks_u32(12);
    if(!n||!cx_ks_text(a)||so>=CX_KSYM_CAP)return 0;
    u32 l=0,r=n; while(r-l>1){ u32 m=(l+r)/2; if(cx_ks_u32(24+m*8)<=a)l=m; else r=m; }
    u32 sa=cx_ks_u32(24+l*8); if(sa>a)return 0; u32 no=so+cx_ks_u32(24+l*8+4); if(no>=CX_KSYM_CAP)return 0;
    *name=(const char*)&g_ksyms[no]; *off=a-sa; return 1; }
int kname_fmt(char* out,int cap,u64 a){
    cx_buf b={out,out+cap-1}; const char* nm; u64 off;
    if(kname_find(a,&nm,&off)){ int k=0; while(nm[k]&&k<60)cx_c(&b,nm[k++]); cx_s(&b,"+"); cx_x(&b,off); }
    else cx_x(&b,a);
    *b.p=0; return (int)(b.p-out); }

static int cx_va_ok(u64 v){
    if(v<0x1000||v>=0x0000800000000000ULL)return 0;
    u64* t=(u64*)(cpu_get_pt()&0x000FFFFFFFFFF000ULL); u64 e=t[(v>>39)&511]; if(!(e&1))return 0;
    t=(u64*)(e&0x000FFFFFFFFFF000ULL); e=t[(v>>30)&511]; if(!(e&1))return 0; if(e&0x80)return 1;
    t=(u64*)(e&0x000FFFFFFFFFF000ULL); e=t[(v>>21)&511]; if(!(e&1))return 0; if(e&0x80)return 1;
    t=(u64*)(e&0x000FFFFFFFFFF000ULL); e=t[(v>>12)&511]; return (int)(e&1); }
int crash_bt(u64 rbp,u64* out,int max){
    int n=0; u64 prev=0;
    while(n<max&&rbp){
        if((rbp&7)||rbp<=prev||(prev&&rbp-prev>0x400000ULL)||!cx_va_ok(rbp)||!cx_va_ok(rbp+15))break;
        u64 ret=((u64*)rbp)[1], nx=((u64*)rbp)[0];
        if(!cx_ks_text(ret))break;
        out[n++]=ret; prev=rbp; rbp=nx; }
    return n; }

void crash_logline(const char* s){
    if(s[0]=='a'&&s[1]=='l'&&s[2]=='i'&&s[3]=='v'&&s[4]=='e'&&s[5]==':')return;
    u32 i=g_cx_cl_n%CX_CL_N; char* d=g_cx_cl[i]; cx_buf b={d,d+CX_CL_W-1}; cx_c(&b,'['); cx_u(&b,g_ticks); cx_s(&b,"] ");
    while(*s&&b.p<b.e){ char c=*s++; cx_c(&b,(c<32||c>126)?'.':c); } *b.p=0; g_cx_cl_n++; }

static const char* CX_EXN[32]={"divide error","debug","NMI","breakpoint","overflow","bound range","invalid instruction","no FPU","double fault","coprocessor overrun","invalid TSS","segment not present","stack fault","general protection fault","page fault","reserved","x87 floating point error","alignment check","machine check","SIMD floating point error","virtualization","control protection","reserved","reserved","reserved","reserved","reserved","reserved","hypervisor injection","VMM communication","security","reserved"};

static int cx_in_guard(u64 a){ return g_bsg_on&&a>=0x09800000ULL-0x400ULL&&a<0x09801000ULL; }

static int cx_build(char* out,int cap,int kind,regs_t* r,const char* msg,u64 cr2,int frozen_s,int nlog){
    cx_buf b={out,out+cap-1}; char t[96];
    cx_s(&b,CX_VERS); cx_s(&b," - "); cx_s(&b,kind==CRASH_FREEZE?"freeze report":"crash report"); cx_c(&b,'\n');
    cx_s(&b,"Uptime: "); cx_u(&b,g_ticks/100); cx_s(&b," s   RAM: "); cx_u(&b,g_ram_mb); cx_s(&b," MB   heap in use: "); cx_u(&b,g_cx_heap_kb); cx_s(&b," KB\n\n");
    cx_s(&b,"What happened: ");
    if(kind==CRASH_FREEZE){ cx_s(&b,"the main loop stopped running for "); cx_u(&b,(u64)frozen_s); cx_s(&b," s (stage "); cx_u(&b,(u64)g_ml_stage); cx_s(&b,")"); }
    else if(kind==CRASH_STACK){ cx_s(&b,"a kernel stack overflowed"); if(msg){ cx_s(&b," ("); cx_s(&b,msg); cx_s(&b,")"); } }
    else if(kind==CRASH_SMASH){ cx_s(&b,"a function wrote past the end of a local buffer (stack protector)"); }
    else if(r&&r->vector<32){ cx_s(&b,CX_EXN[r->vector]); cx_s(&b," (vector "); cx_u(&b,r->vector); cx_s(&b,", error code "); cx_x(&b,r->errcode); cx_s(&b,")");
        if(r->vector==14){ cx_s(&b,"\n  while "); cx_s(&b,(r->errcode&16)?"running code at ":(r->errcode&2)?"writing to ":"reading from "); cx_x(&b,cr2); cx_s(&b,(r->errcode&1)?" (no permission)":" (not mapped)"); if((r->cs&3)==3)cx_s(&b," in a user program"); } }
    else { cx_s(&b,msg?msg:"unspecified fatal error"); }
    cx_c(&b,'\n');
    if(r){ kname_fmt(t,sizeof t,r->rip); cx_s(&b,"Where: "); cx_s(&b,t); cx_s(&b,(r->cs&3)==3?"  (user mode)":""); cx_c(&b,'\n'); }
    if(g_cx_drv[0]){ cx_s(&b,"Driver being started: "); cx_s(&b,g_cx_drv); cx_c(&b,'\n'); }
    if(r&&(r->cs&3)==0){ u64 fr[16]; int n=crash_bt(r->rbp,fr,16); if(n>0&&fr[0]==r->rip){ for(int i=1;i<n;i++)fr[i-1]=fr[i]; n--; }
        cx_s(&b,"\nCall chain (newest first):\n"); kname_fmt(t,sizeof t,r->rip); cx_s(&b,"  #0 "); cx_s(&b,t); cx_c(&b,'\n');
        for(int i=0;i<n;i++){ int rep=1; while(i+rep<n&&fr[i+rep]==fr[i])rep++; kname_fmt(t,sizeof t,fr[i]); cx_s(&b,"  #"); cx_u(&b,(u64)i+1ULL); cx_c(&b,' '); cx_s(&b,t); if(rep>1){ cx_s(&b,"   (and "); cx_u(&b,(u64)rep-1ULL); cx_s(&b," more times)"); i+=rep-1; } cx_c(&b,'\n'); }
        if(!cx_ks_ok())cx_s(&b,"  (this build has no function names)\n"); }
    if(r&&r->vector!=0xFFFFu){ cx_s(&b,"\nRegisters:\n");
        const char* nm[16]={"rax","rbx","rcx","rdx","rsi","rdi","rbp","rsp","r8","r9","r10","r11","r12","r13","r14","r15"};
        u64 v[16]={r->rax,r->rbx,r->rcx,r->rdx,r->rsi,r->rdi,r->rbp,r->rsp,r->r8,r->r9,r->r10,r->r11,r->r12,r->r13,r->r14,r->r15};
        for(int i=0;i<16;i++){ cx_s(&b,"  "); cx_s(&b,nm[i]); cx_s(&b,"="); cx_x(&b,v[i]); if((i&3)==3)cx_c(&b,'\n'); }
        cx_s(&b,"  rip="); cx_x(&b,r->rip); cx_s(&b,"  rflags="); cx_x(&b,r->rflags); cx_s(&b,"  cs="); cx_x(&b,r->cs); if(r->vector==14){ cx_s(&b,"  cr2="); cx_x(&b,cr2); } cx_c(&b,'\n'); }
    { extern u32 g_heap_bad, g_heap_dfree, g_heap_badfree, g_heap_oom; if(g_heap_oom){ cx_s(&b,"\nOut-of-memory failures before: "); cx_u(&b,g_heap_oom); cx_c(&b,'\n'); } if(g_heap_bad||g_heap_dfree||g_heap_badfree){ cx_s(&b,"\nHeap problems seen before: overruns "); cx_u(&b,g_heap_bad); cx_s(&b,", double frees "); cx_u(&b,g_heap_dfree); cx_s(&b,", bad frees "); cx_u(&b,g_heap_badfree); cx_c(&b,'\n'); } }
    cx_s(&b,"\nLast log lines:\n");
    u32 n=g_cx_cl_n<(u32)nlog?g_cx_cl_n:(u32)nlog; for(u32 i=g_cx_cl_n-n;i<g_cx_cl_n;i++){ cx_s(&b,"  "); cx_s(&b,g_cx_cl[i%CX_CL_N]); cx_c(&b,'\n'); }
    *b.p=0; return (int)(b.p-out); }

static u64 cx_fb_base(void){ return (u64)FBB; }
static void cx_px(int x,int y,u32 c){
    if(x<0||y<0||(u32)x>=W||(u32)y>=H)return; u8* p=FBB+(u64)y*PITCHB+(u64)x*BYPP;
    u32 v=g_fb_rgb?(((c&0xFF)<<16)|(c&0xFF00)|((c>>16)&0xFF)):c;
    if(BYPP==4)*(volatile u32*)p=v; else { p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); } }
static void cx_box(int x,int y,int w,int h,u32 c){ for(int yy=y;yy<y+h;yy++) for(int xx=x;xx<x+w;xx++)cx_px(xx,yy,c); }
static int cx_text(int x,int y,const char* s,u32 c,int sc,int maxw){
    int x0=x; for(int i=0;s[i]&&s[i]!='\n';i++){ if(x+8*sc>x0+maxw)break; int g=(u8)s[i]-32; if(g>=0&&g<95){ const unsigned char* f=fontaa[g];
            for(int r=0;r<14;r++) for(int k=0;k<8;k++) if(f[r*8+k]>110) for(int dy=0;dy<sc;dy++) for(int dx=0;dx<sc;dx++)cx_px(x+k*sc+dx,y+r*sc+dy,c); }
        x+=8*sc; }
    return x; }
static int cx_lines(int x,int y,const char* s,u32 c,int maxw,int maxy){
    while(*s&&y<maxy){ cx_text(x,y,s,c,1,maxw); while(*s&&*s!='\n')s++; if(*s=='\n')s++; y+=17; }
    return y; }

static int g_cx_nousb, g_cx_nofs;
static void cx_save(const char* t,int n,int* usb,int* files){
    *usb=0; *files=0;
    if(msd_ok&&ufat_ok&&!g_cx_nousb){ if(ufat_write_file("CRASH.TXT",t,n)==0){ *usb=1; ufat_poke("CRASHNEW.TXT","1\n",2); } }
    if(g_cx_nofs)return;
    if(fs_save_bin("crash-report.txt",(const u8*)t,n,0)>=0){ *files=1; fs_save_bin("crash-new.flag",(const u8*)"1\n",2,0); if(disk_ok)nvx3_sync_all(); } }

static int cx_in_usb(u64 rip){ const char* nm=0; u64 off=0; return kname_find(rip,&nm,&off)&&nm&&((nm[0]=='x'&&nm[1]=='h')||(nm[0]=='u'&&nm[1]=='f')||(nm[0]=='u'&&nm[1]=='m')||(nm[0]=='s'&&nm[1]=='c'&&nm[2]=='s')||(nm[0]=='m'&&nm[1]=='s'&&nm[2]=='d')); }
static void cx_panic_wait(void){
    for(;;){
        if(inb(0x64)&1){ u8 st=inb(0x64); u8 sc=inb(0x60); if(!(st&0x20)&&sc==0x13)power_reboot(); }
        if(xhid_n&&!g_cx_nousb)xhci_mouse_poll(); int k=kpop(); if(k=='r'||k=='R')power_reboot();
        for(volatile int i=0;i<20000;i++)cpu_pause(); } }

static void cx_panic_main(void){
    regs_t* r=g_cx_pr.vector==0xFFFFu?0:&g_cx_pr; u64 cr2=cpu_cr2(); regs_t* rb=&g_cx_pr;
    int kind=g_cx_pkind;
    if(r&&(r->vector==8||r->vector==14)&&(cx_in_guard(r->rsp)||(r->vector==14&&cx_in_guard(cr2)))){ kind=CRASH_STACK; g_cx_pmsg="boot stack, guard page hit"; }
    { extern int g_gfx; g_gfx=0; }
    g_cx_rep_n=cx_build(g_cx_rep,CX_REP_CAP,kind,r?r:(rb->rip?rb:0),g_cx_pmsg,cr2,0,20);
    static char scr[CX_REP_CAP]; cx_build(scr,CX_REP_CAP,kind,r?r:(rb->rip?rb:0),g_cx_pmsg,cr2,0,H>=1000?10:5);
    klog("panic: NoovexOS stopped - see the crash report");
    if(!FBB||!W||!H){ int u,f; cx_save(g_cx_rep,g_cx_rep_n,&u,&f); for(;;)cpu_halt(); }
    int sc=W>=1600?2:1, mx=48*sc/2+16, mw=(int)W-2*mx;
    for(u32 y=0;y<H;y++){ u32 c=mix(rgb(150,24,34),rgb(96,12,22),(int)((y*256)/H)); for(u32 x=0;x<W;x++)cx_px((int)x,(int)y,c); }
    int y=40*sc;
    cx_text(mx,y,":(",rgb(255,255,255),4*sc,mw); y+=70*sc;
    cx_text(mx,y,kind==CRASH_STACK?"NoovexOS stopped: a kernel stack overflowed.":kind==CRASH_SMASH?"NoovexOS stopped: a local buffer was overrun.":"NoovexOS ran into a problem and needs to stop.",rgb(255,255,255),sc,mw); y+=28*sc;
    cx_box(mx,y,mw,3,rgb(255,170,176)); y+=14;
    y=cx_lines(mx,y,scr,rgb(255,232,232),mw,(int)H-90);
    int ys=(int)H-70; cx_box(0,ys-6,(int)W,76,rgb(80,10,18));
    cx_text(mx,ys,"Saving the report...",rgb(255,255,255),1,mw);
    int u=0,f=0; { extern volatile int g_heap_busy; g_cx_nofs=g_heap_busy; g_cx_nousb=cx_in_usb(g_cx_pr.rip); } cx_save(g_cx_rep,g_cx_rep_n,&u,&f); if(!disk_ok)f=0;
    cx_box(0,ys-6,(int)W,76,rgb(80,10,18));
    cx_text(mx,ys,u&&f?"Report saved: CRASH.TXT on the USB stick and crash-report.txt in Files.":u?"Report saved: CRASH.TXT on the USB stick.":f?"Report saved: crash-report.txt in Files.":"No USB stick or installed disk to save to - please take a photo of this screen.",rgb(255,255,255),1,mw);
    cx_text(mx,ys+24,"Press R to restart.   " CX_VERS,rgb(255,190,196),1,mw);
    (void)cx_fb_base;
    cx_panic_wait(); }

void crash_panic(regs_t* r,const char* msg,u64 rip,u64 rbp){
    static volatile int in_panic=0;
    cpu_irq_off();
    if(in_panic){ for(;;)cpu_stop(); }
    in_panic=1; smp_halt_others();
    if(r)g_cx_pr=*r; else { for(u32 i=0;i<sizeof g_cx_pr;i++)((u8*)&g_cx_pr)[i]=0; g_cx_pr.vector=0xFFFFu; g_cx_pr.rip=rip; g_cx_pr.rbp=rbp; }
    g_cx_pmsg=msg; g_cx_pkind=CRASH_PANIC;
    if(msg&&msg[0]=='!'){ g_cx_pkind=msg[1]=='S'?CRASH_STACK:CRASH_SMASH; g_cx_pmsg=msg+2; }
    cpu_run_on((u64)(g_cx_pstk+sizeof g_cx_pstk),cx_panic_main); }

void __stack_chk_fail(void){ crash_panic(0,"!Xstack protector",(u64)__builtin_return_address(0),(u64)__builtin_frame_address(0)); }

static void cx_frz_draw(int secs){
    if(!FBB||!W||!H)return; int bw=(int)W-80; if(bw>1100)bw=1100; int bx=40, by=30, bh=(int)H-60; if(bh>48*17)bh=48*17;
    cx_box(bx,by,bw,bh,rgb(40,10,16)); cx_box(bx,by,bw,3,rgb(240,90,100));
    char t[80]; cx_buf b={t,t+79}; cx_s(&b,"NoovexOS is not responding ("); cx_u(&b,(u64)secs); cx_s(&b," s) - the report below is kept"); *b.p=0;
    cx_text(bx+14,by+12,t,rgb(255,255,255),1,bw-28);
    cx_lines(bx+14,by+36,g_cx_frz_scr,rgb(255,226,226),bw-28,by+bh-18); }

volatile int g_frz_hold;
void freeze_tick(regs_t* r){
    static u32 lb;
    if(g_frz_hold){ g_frz_still=0; return; }
    if(!g_ml_beat||g_ml_beat!=lb||g_preempt_on){ lb=g_ml_beat; if(g_cx_frz_shown){ g_cx_frz_done_s=(int)(g_frz_still/100); g_cx_frz_shown=0; g_cx_frz_redraw=1; } g_frz_still=0; return; }
    if(g_frz_still<0x7FFFFFF0u)g_frz_still++;
    if(g_frz_still==500){ regs_t c=*r; if((c.cs&3)!=0)c.rbp=0; g_cx_frz_n=cx_build(g_cx_frz_rep,CX_REP_CAP,CRASH_FREEZE,&c,0,0,5,20); cx_build(g_cx_frz_scr,CX_REP_CAP,CRASH_FREEZE,&c,0,0,5,H>=1000?8:4); g_cx_frz_shown=1; g_cx_frz_saved=0; klog("freeze: the main loop has not run for 5 s - report kept"); cx_frz_draw(5); }
    else if(g_cx_frz_shown&&g_frz_still%500==0&&g_frz_still<=6000)cx_frz_draw((int)(g_frz_still/100));
    if(g_frz_still==3000&&g_cx_frz_shown&&!g_cx_frz_saved){ g_cx_frz_saved=1; int u,f; extern volatile int g_heap_busy;
        g_cx_nousb=cx_in_usb(r->rip); g_cx_nofs=g_heap_busy; cx_save(g_cx_frz_rep,g_cx_frz_n,&u,&f); g_cx_nousb=0; g_cx_nofs=0; klog(u?"freeze: report saved to CRASH.TXT on the USB stick":f?"freeze: report saved to crash-report.txt":"freeze: report could not be saved"); } }

static int cx_flag_files(const char* nm){ int k=fs_find_name(nm); if(k<0)return -1; char* d=fsd(k); return d?d[0]:-1; }
static int cx_usb_find(const char* nm){ if(!(msd_ok&&ufat_ok))return -1; for(int i=0;i<ufs_n;i++) if(cx_ieq(ufs[i].name,nm))return i; return -1; }
static int cx_flag_usb(const char* nm){ int i=cx_usb_find(nm); if(i<0)return -1; char c[4]; if(ufat_read(i,c,2)>=1)return c[0]; return -1; }

static void cx_notice(void){
    int fl=cx_flag_files("crash-new.flag"), ul=cx_flag_usb("CRASHNEW.TXT");
    if(fl!='1'&&ul!='1')return;
    static char rb[CX_REP_CAP]; int n=0;
    { int k=fs_find_name("crash-report.txt"); if(k>=0){ char* d=fsd(k); if(d){ while(d[n]&&n<CX_REP_CAP-1){ rb[n]=d[n]; n++; } } } }
    if(!n){ int i=cx_usb_find("CRASH.TXT"); if(i>=0){ n=ufat_read(i,rb,CX_REP_CAP-1); if(n<0)n=0; if(n)fs_save_bin("crash-report.txt",(const u8*)rb,n,0); } }
    rb[n]=0;
    if(fl=='1'){ fs_save_bin("crash-new.flag",(const u8*)"0\n",2,0); if(disk_ok)nvx3_sync_all(); }
    if(ul=='1')ufat_poke("CRASHNEW.TXT","0\n",2);
    notify("NoovexOS stopped last time","The report is crash-report.txt in Files (type crash in the terminal).");
    if(n&&g_state==CX_ST_DESKTOP)editor_open_buf(rb,n);
    klog("crash: a report from the last start was found and shown"); }

int crash_tick(void){
    int need=0;
    { static u64 hk; if(!hk||g_ticks-hk>=200){ hk=g_ticks?g_ticks:1; g_cx_heap_kb=heap_used()/1024; } }
    if(g_cx_frz_redraw){ g_cx_frz_redraw=0; need=1; { extern void fb_shadow_invalidate(void); fb_shadow_invalidate(); } int s=g_cx_frz_done_s; char t[96]; cx_buf b={t,t+95}; cx_s(&b,"NoovexOS was not responding for "); cx_u(&b,(u64)s); cx_s(&b," s - report saved as crash-report.txt"); *b.p=0;
        if(!g_cx_frz_saved&&g_cx_frz_n){ cx_buf a={g_cx_frz_rep+g_cx_frz_n,g_cx_frz_rep+CX_REP_CAP-1}; cx_s(&a,"\nThe system recovered after "); cx_u(&a,(u64)s); cx_s(&a," s.\n"); *a.p=0; g_cx_frz_n=(int)(a.p-g_cx_frz_rep);
            if(fs_save_bin("crash-report.txt",(const u8*)g_cx_frz_rep,g_cx_frz_n,0)>=0&&disk_ok)nvx3_sync_all(); if(msd_ok&&ufat_ok)ufat_write_file("CRASH.TXT",g_cx_frz_rep,g_cx_frz_n); g_cx_frz_saved=1; }
        klog(t); toast(t); }
    if(!g_cx_boot_done&&(g_state==CX_ST_DESKTOP||g_state==CX_ST_LIVE)){
        if(!g_cx_desk_t)g_cx_desk_t=g_ticks?g_ticks:1;
        else if(g_ticks-g_cx_desk_t>=300){ g_cx_boot_done=1; cx_notice();
            if(g_cx_drv_notice){ char t[120]; cx_buf b={t,t+119}; cx_s(&b,"The driver '"); for(int i=0;i<g_cx_drv_nskip;i++){ if(i)cx_s(&b,", "); cx_s(&b,g_cx_drv_skip[i]); } cx_s(&b,"' stopped the last start, so it was skipped once."); *b.p=0; notify("Driver skipped",t); g_cx_drv_notice=0; need=1; } } }
    (void)g_cx_notice_t;
    return need; }

void stk_watch(const char* nm,int idx,void* base){
    u64* p=(u64*)base; for(int i=0;i<g_cx_stk_n;i++) if(g_cx_stk[i].base==p){ for(int k=0;k<8;k++)p[k]=CX_STK_MAGIC^(u64)k; return; }
    if(g_cx_stk_n>=CX_STK_MAX)return; g_cx_stk[g_cx_stk_n].nm=nm; g_cx_stk[g_cx_stk_n].idx=idx; g_cx_stk[g_cx_stk_n].base=p; g_cx_stk_n++;
    for(int k=0;k<8;k++)p[k]=CX_STK_MAGIC^(u64)k; }
void stk_cpu(int i){
    if(i<0||i>=NCPU)return; struct pcpu* c=&g_cpu[i];
    stk_watch("kernel stack of CPU",i,c->kstack); stk_watch("job stack of CPU",i,c->jkstack); stk_watch("NMI stack of CPU",i,c->ist_nmi); stk_watch("double-fault stack of CPU",i,c->ist_df); }
void stk_check(void){
    if(g_cx_stk_hit)return;
    for(int i=0;i<g_cx_stk_n;i++){ u64* p=g_cx_stk[i].base; if(p[0]!=CX_STK_MAGIC||p[7]!=(CX_STK_MAGIC^7ULL)){
            static char m[96]; cx_buf b={m,m+95}; cx_s(&b,"!S"); cx_s(&b,g_cx_stk[i].nm); cx_c(&b,' '); cx_u(&b,(u64)g_cx_stk[i].idx); *b.p=0; g_cx_stk_hit=1; crash_panic(0,m,(u64)__builtin_return_address(0),(u64)__builtin_frame_address(0)); } } }
void stk_guard_boot(void){
    u64 v=0x09800000ULL; u64* t=(u64*)(cpu_get_pt()&0x000FFFFFFFFFF000ULL); u64 e=t[(v>>39)&511]; if(!(e&1))return;
    t=(u64*)(e&0x000FFFFFFFFFF000ULL); e=t[(v>>30)&511]; if(!(e&1)||(e&0x80))return;
    u64* pd=(u64*)(e&0x000FFFFFFFFFF000ULL); u64* pde=&pd[(v>>21)&511]; u64 d=*pde; if(!(d&1)||!(d&0x80)||(d&(1ULL<<12)))return;
    u64 sp=cpu_sp(); if(sp>=v&&sp<v+0x10000ULL)return;
    u64 pa=d&0x000FFFFFFFE00000ULL, fl=(d&0x17FULL)|(d&(1ULL<<63));
    for(int i=0;i<512;i++)g_cx_bsg_pt[i]=(pa+((u64)i<<12))|fl;
    extern u64 kva_to_pa(u64); u64 ptp=kva_to_pa((u64)g_cx_bsg_pt); if(!ptp||(ptp&0xFFFULL))return;
    g_cx_bsg_pt[0]=0; *pde=ptp|3ULL|(d&4ULL);
    for(int i=0;i<512;i++)cpu_flush_page(v+((u64)i<<12));
    g_bsg_on=1; klog("memory: guard page under the boot stack is on (a stack overflow now stops with a clear report)"); }

static int cx_drv_load(char* out){
    out[0]=0; char b[40]; int n=-1;
    if(disk_ok){ int k=fs_find_name("driver-start.txt"); if(k>=0){ char* d=fsd(k); if(d){ n=0; while(n<32&&d[n]){ b[n]=d[n]; n++; } } } }
    if(n<0){ int i=cx_usb_find("DRVSTART.TXT"); if(i>=0)n=ufat_read(i,b,32); }
    if(n<=0)return 0; int k=0; while(k<n&&k<23&&b[k]!=' '&&b[k]!='\n'&&b[k]!='\r'&&b[k]){ out[k]=b[k]; k++; } out[k]=0; return k>0; }
static void cx_drv_store(const char* v){
    char b[32]; int k=0; while(v[k]&&k<30){ b[k]=v[k]; k++; } while(k<31)b[k++]=' '; b[31]='\n';
    if(disk_ok){ fs_save_bin("driver-start.txt",(const u8*)b,32,0); nvx3_sync_all(); }
    if(msd_ok&&ufat_ok)ufat_poke("DRVSTART.TXT",b,32); }
int live_lock_on(void){ char b[8]; int i=cx_usb_find("LIVELOCK.TXT"); if(!(msd_ok&&ufat_ok))return -1; if(i<0)return 1; int n=ufat_read(i,b,4); return !(n>0&&b[0]=='0'); }
int live_lock_set(int on){ if(!(msd_ok&&ufat_ok))return -1; return ufat_poke("LIVELOCK.TXT",on?"1\n":"0\n",2)==0?0:-1; }
void drv_boot_check(void){
    char v[24]; if(!msd_ok)msd_autoinit_safe();
    { klog(msd_ok&&ufat_ok?"boot: USB stick mounted - start-up notes and crash reports are saved on it":msd_ok?"boot: USB storage found but no FAT file system":disk_ok?"boot: no USB stick - start-up notes are kept in Files":"boot: no USB stick and no installed disk - start-up notes are kept in memory only"); if(!msd_ok&&xhci_failstep[0])klog2("boot: USB stick check stopped at: ",xhci_failstep); }
    g_cx_drv_store=1;
    if(cx_drv_load(v)&&v[0]!='-'){ if(g_cx_drv_nskip<8){ cx_cp(g_cx_drv_skip[g_cx_drv_nskip++],v,24); } g_cx_drv_notice=1; klog2("boot: skipping this start-up driver once, it did not finish last time: ",v); cx_drv_store("-"); } }
int drv_skipped(const char* id){ for(int i=0;i<g_cx_drv_nskip;i++) if(cx_eq(g_cx_drv_skip[i],id))return 1; return 0; }
int drv_begin(const char* id){ if(drv_skipped(id))return 0; cx_cp(g_cx_drv,id,24); if(g_cx_drv_store)cx_drv_store(id); return 1; }
void drv_end(void){ g_cx_drv[0]=0; if(g_cx_drv_store==2)cx_drv_store("-"); }
void drv_boot_done(void){ g_cx_drv[0]=0; if(g_cx_drv_store)cx_drv_store("-"); g_cx_drv_store=2; }
const char* drv_current(void){ return g_cx_drv[0]?g_cx_drv:0; }

void crash_init(void){
    { extern int (*g_heap_namefn)(char*,int,u64); extern void (*g_heap_log)(const char*); g_heap_namefn=kname_fmt; g_heap_log=klog; }
    for(int i=0;i<MAXPROC;i++)stk_watch("process kernel stack",i,g_pstack[i]);
    klog(cx_ks_ok()?"crash: function names are built in (crash reports name the function)":"crash: this build has no function name table"); }

static void cx_recurse(volatile int d){ volatile char pad[512]; pad[0]=(char)d; if(d<1000000)cx_recurse(d+1); pad[1]=pad[0]; }
static volatile char g_cx_sink;
static __attribute__((noinline)) void cx_smash(const char* s){ char b[16]; volatile int n=40; for(int i=0;i<n;i++)b[i]=s[i%4]; g_cx_sink=b[3]; }

void crash_cmd(const char* a){
    void term_puts(const char*);
    while(*a==' ')a++;
    if(cx_eq(a,"")){
        int k=fs_find_name("crash-report.txt"); if(k>=0){ char* d=fsd(k); if(d){ term_puts(d); term_puts("\n"); return; } }
        if(g_cx_frz_n){ term_puts(g_cx_frz_rep); term_puts("\n"); return; }
        term_puts("No crash or freeze report has been saved.\n  crash test panic|freeze|stack|smash|heap   cause one on purpose to check the reports\n"); return; }
    if(cx_eq(a,"test panic")){ volatile u64* p=(volatile u64*)0x8000000000000ULL; *p=1; return; }
    if(cx_eq(a,"test msr")){ u64 v=0; int r=cpu_rdmsr_safe(0xDEADBEEFu,&v); term_puts(r?"reading a CPU register that does not exist was refused and recovered - no crash\n":"the CPU accepted the register\n"); return; }
    if(cx_eq(a,"test freeze")){ term_puts("spinning for 40 s with interrupts on...\n"); u64 t=g_ticks; while(g_ticks-t<4000)cpu_pause(); term_puts("back\n"); return; }
    if(cx_eq(a,"test stack")){ cx_recurse(0); return; }
    if(cx_eq(a,"test smash")){ cx_smash("ABCD"); return; }
    if(cx_eq(a,"test heap")){ void* kmalloc(unsigned long); void kfree(void*); char* p=(char*)kmalloc(24); if(p){ for(int i=0;i<30;i++)p[i]='x'; kfree(p); kfree(p); } term_puts("wrote 6 bytes past a 24-byte block and freed it twice - see the log\n"); return; }
    term_puts("usage: crash   |   crash test panic|freeze|stack|smash|heap|msr\n"); }
