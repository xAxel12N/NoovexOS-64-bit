#include "include/nvx/base.h"
#include "include/drivers.h"
#include "include/nvx/mem/heap_api.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/drivers/video/fb/gfx.h"
#include "include/nvx/arch/x86_64/cpu/cpuinfo.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/arch/x86_64/smp/smp.h"
#include "include/nvx/arch/x86_64/smp/smp_sched.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/net/ethernet/nic_names.h"
#include "include/nvx/drivers/net/ethernet/nic.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/drivers/input/touchpad/i2c_hid.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "include/nvx/core/sound.h"
#include "include/nvx/net/stack/tcpip.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/graphics/ui/desktop.h"
#include "include/nvx/apps/system/files_classic.h"
#include "include/nvx/drivers/video/nvidia/nvgsp.h"
#include "include/nvx/drivers/video/gpuid.h"

extern int bat_present, bat_pct, bat_charging, bat_via_ec; extern void battery_tick(void);
extern int gdisk_count(void); extern u32 gdisk_size_of(int); extern const char* gdisk_name_of(int); extern int gdisk_current(void);
extern int igpu_info(const char** name,u32* did,u32* ss,u32* eu,u32* mhz,u32* maxmhz,u32* stolen,u32* tearfree);
extern int g_syn_ready, g_etp_ready, g_wnd_up, msd_ok; extern u32 msd_lastlba; extern u32 g_ram_mb;
extern volatile u64 g_ticks; extern int g_refresh_hz;

static int tmt=0, tmsel=-1, tmdev=0, tmperf=0;
static int tm_showhid=1, tm_jsel=-1; static u64 tm_bg_t, tm_svc_prev[16], tm_job_prev[JMAX]; static int tm_svc_pct[16], tm_job_pct[JMAX];
int svc_count(void); int svc_info(int i,const char** name,const char** desc,const char** status,int* on,int* required); int svc_set(int i,int on); u64 svc_us(int i);
static int hx_[200], hy_[200], hw_[200], hh_[200], hid_[200], hn_=0;
static void hit(int x,int y,int w,int h,int id){ if(hn_<200){ hx_[hn_]=x; hy_[hn_]=y; hw_[hn_]=w; hh_[hn_]=h; hid_[hn_]=id; hn_++; } }
#define HIST 60
static u8 hcpu[HIST], hmem[HIST], hgpu[HIST], hker[HIST]; static u32 hcm[HIST], hal[HIST]; static u64 pu[NCPU]; static int ker_now=0; static unsigned long g_hu=0, g_hs=0; static int hpos=0; static u64 hlast=0; static u64 pt[NCPU], pi[NCPU]; static int cpu_now=0, cpu_cores=0;
static u32 INK_=0, SUB=0, LINE=0, CARD=0, BG_=0, SEPC=0;
static void colours(void){ INK_=rgb(46,46,52); SUB=rgb(118,118,126); LINE=rgb(222,222,226); CARD=rgb(255,255,255); BG_=rgb(250,250,251); SEPC=rgb(234,234,237); if(!g_gm_ready)gmetrics(); }
static void tbold(int x,int y,const char* t,u32 c){ pdstr(x,y,t,c,1); pdstr(x+1,y,t,c,1); }
static void tbold_ctr(int cx,int y,const char* t,u32 c){ tbold(cx-(pw_str(t,1)+1)/2,y,t,c); }
static void tright(int rx,int y,const char* t,u32 c,int sc){ pdstr(rx-pw_str(t,sc),y,t,c,sc); }
static void tcard(int x,int y,int w,int h){ fill_round(x,y,w,h,12,15,LINE); fill_round(x+1,y+1,w-2,h-2,11,15,CARD); }
static void tsep(int x,int y,int w){ fill_rect(x,y,w,1,SEPC); }
static void tchev(int x,int y,u32 c,int down){ for(int i=0;i<5;i++){ if(down){ fill_rect(x+i,y+i,1,2,c); fill_rect(x+8-i,y+i,1,2,c); } else { fill_rect(x+i,y+i,2,1,c); fill_rect(x+i,y+8-i,2,1,c); } } }
static void tpill(int rx,int y,const char* t,u32 bg,u32 fg){ int w=pw_str(t,1)+22; fill_round(rx-w,y,w,24,12,15,bg); pdstr_ctr(rx-w/2,y+5,t,fg,1); }
static void tclip(char* d,const char* s,int maxw){ int n=0; while(s[n]&&n<95){ d[n]=s[n]; n++; } d[n]=0; if(pw_str(d,1)<=maxw)return; while(n>0&&pw_str(d,1)>maxw-12){ d[--n]=0; } d[n++]='.'; d[n++]='.'; d[n]=0; }
static int mem_pct(void){ u32 tot=g_ram_mb?g_ram_mb:2048; u64 used=(u64)heap_used()/1048576ULL+64; int p=(int)(used*100/tot); return p>100?100:p; }
static int gpu_pct(void){ if(!g_gfx||!g_gfx_frame_us)return 0; u32 f=1000000u/(u32)(g_refresh_hz>0?g_refresh_hz:60); int p=(int)((g_gfx_gpu_us+g_gfx_pres_us)*100u/f); return p>100?100:p; }
static void sample(void){
    if(hlast&&g_ticks-hlast<100)return; hlast=g_ticks;
    u64 dt=0, di=0; int n=0;
    u64 du=0;
    for(int i=1;i<g_ncpu&&i<NCPU;i++){ struct pcpu* c=&g_cpu[i]; if(!c->online)continue; u64 t=c->ticks, id=c->idle_ticks, us=c->user_ticks; if(pt[i]){ dt+=t-pt[i]; di+=id-pi[i]; du+=us-pu[i]; n++; } pt[i]=t; pi[i]=id; pu[i]=us; }
    cpu_cores=n; cpu_now=(dt&&n)?(int)(100-di*100/dt):0; if(cpu_now<0)cpu_now=0;
    ker_now=cpu_now-((dt&&n)?(int)(du*100/dt):0); if(ker_now<0)ker_now=0;
    { extern void heap_stat(unsigned long*,unsigned long*); heap_stat(&g_hu,&g_hs); }
    hker[hpos]=(u8)ker_now; hcm[hpos]=(u32)(g_hs/1024UL); hal[hpos]=(u32)(g_hu/1024UL);
    hcpu[hpos]=(u8)cpu_now; hmem[hpos]=(u8)mem_pct(); hgpu[hpos]=(u8)gpu_pct(); hpos=(hpos+1)%HIST; }
static void graph(int x,int y,int w,int h,const u8* d,u32 col){
    fill_rect(x,y,w,h,CARD); for(int k=0;k<=4;k++)fill_rect(x,y+(h-1)*k/4,w,1,SEPC); for(int k=1;k<6;k++)for(int q=0;q<h;q+=4)fill_rect(x+w*k/6,y+q,1,2,SEPC);
    int px=-1,py=-1; for(int k=0;k<HIST;k++){ int v=d[(hpos+k)%HIST]; int gx=x+(w-1)*k/(HIST-1), gy=y+h-2-(h-4)*v/100;
        fill_rect(gx,gy,(w-1)/(HIST-1)+1,y+h-1-gy,mix(CARD,col,34)); if(px>=0) line_bl(px,py,gx,gy,col,255,2); px=gx; py=gy; }
    pdstr(x,y+h+6,"60 seconds",SUB,1); tright(x+w,y+h+6,"now",SUB,1); }
static char* pct(char* p,int v){ p=udec(p,(u32)v); *p++='%'; *p=0; return p; }
static char* mb(char* p,u64 m){ if(m>=1024){ p=udec(p,(u32)(m/1024)); *p++='.'; p=udec(p,(u32)((m%1024)*10/1024)); return scopy(p," GB"); } p=udec(p,(u32)m); return scopy(p," MB"); }
static int app_kb(int a){ switch(a){ case W_BROWSER:return 149504; case W_DOOM:return 33792; case W_PAINT:return 18432; case W_TERM:return 6144; case W_FILES:return 9216; case W_VIDEO:return 24576; default:return 4096; } }

static const char* RAIL[4]={"Processes","Resources","Devices","Services"};
static void rail_icon(int x,int y,int k,u32 c,u32 bg){
    if(k==0){ fill_rect(x,y,16,3,c); fill_rect(x,y+6,16,3,c); fill_rect(x,y+12,16,3,c); }
    else if(k==1){ for(int q=0;q<4;q++)fill_rect(x+q*4,y+14-q*4-2,3,q*4+3,c); }
    else if(k==2){ fill_round(x,y,16,16,3,15,c); fill_rect(x+4,y+4,8,8,bg); fill_rect(x+6,y+6,4,4,c); for(int q=0;q<3;q++){ fill_rect(x-2,y+3+q*4,2,2,c); fill_rect(x+16,y+3+q*4,2,2,c); } }
    else { fill_circle(x+8,y+8,7,c); fill_circle(x+8,y+8,3,bg); } }

static int tm_ybot=0, tm_rx=0;
static void processes(int x,int y,int w,int h){
    (void)h; int cw=w; int napp=0; for(int a=0;a<NWIN;a++) if(WN[a].open)napp++;
    int nsvc=svc_count(); if(nsvc>16)nsvc=16; int njob=0; for(int j=0;j<JMAX;j++) if(g_jp[j].state!=S_FREE&&g_jp[j].state!=S_ZOMB)njob++;
    if(g_ticks-tm_bg_t>=100){ u64 dt=g_ticks-tm_bg_t; if(!tm_bg_t)dt=0; tm_bg_t=g_ticks;
        for(int k=0;k<nsvc;k++){ u64 u=svc_us(k); u64 d=u-tm_svc_prev[k]; tm_svc_prev[k]=u; int p=dt?(int)(d/(dt*100)):0; tm_svc_pct[k]=p>100?100:p; }
        for(int j=0;j<JMAX;j++){ u64 c=g_jp[j].cputicks; u64 d=c-tm_job_prev[j]; tm_job_prev[j]=c; int p=dt?(int)(d*100/dt):0; tm_job_pct[j]=(g_jp[j].state==S_FREE)?0:(p>100?100:p); } }
    int nrow=napp+1+nsvc+1+(tm_showhid?njob:0)+1; int RH=36; int ch=42+nrow*RH; if(y+ch>tm_ybot-30){ RH=(tm_ybot-30-y-42)/nrow; if(RH<26)RH=26; ch=42+nrow*RH; if(y+ch>tm_ybot-30)ch=tm_ybot-30-y; }
    tcard(x,y,cw,ch); int ylim=y+ch-1;
    int rS=x+cw-24, cST=x+cw-130, rMEM=x+cw-170, rCPU=x+cw-270;
    tbold(x+56,y+12,"Process Name",SUB); tbold(rCPU-pw_str("% CPU",1)-1,y+12,"% CPU",SUB); tbold(rMEM-pw_str("Memory",1)-1,y+12,"Memory",SUB); tbold(cST,y+12,"Status",SUB); (void)rS;
    int yy=y+40; tsep(x+1,yy-1,cw-2);
    { char b[24],*p=b; p=udec(p,(u32)napp); *p=0; int ty=yy+(RH-16)/2; tbold(x+18,ty,"Apps",INK_); pdstr(x+26+pw_str("Apps",1),ty,b,SUB,1); yy+=RH; }
    for(int a=0;a<NWIN&&yy+RH<=ylim;a++){ if(!WN[a].open)continue; int sel=(a==tmsel), hov=in_rect(mx,my,x+1,yy,cw-2,RH);
        if(sel)fill_rect(x+1,yy,cw-2,RH,mix(CARD,ACC,40)); else if(hov)fill_rect(x+1,yy,cw-2,RH,rgb(246,246,248));
        int ty=yy+(RH-16)/2; { u32 ic=mix(ACC,rgb((a*53)&255,(a*97)&255,(a*31)&255),70); int s2=RH>30?24:20; fill_round(x+20,yy+(RH-s2)/2,s2,s2,7,15,ic); char t2[2]={APPNAME[a][0],0}; tbold_ctr(x+20+s2/2,yy+(RH-16)/2,t2,rgb(255,255,255)); }
        pdstr(x+56,ty,APPNAME[a],INK_,1);
        char b[24],*p=b; pct(p,a==focus?(cpu_now>3?cpu_now/2:2):0); tright(rCPU,ty,b,INK_,1);
        p=b; p=mb(p,(u64)app_kb(a)/1024); *p=0; tright(rMEM,ty,b,INK_,1);
        pdstr(cST,ty,WN[a].min?"Suspended":(a==focus?"Active":"Running"),WN[a].min?SUB:rgb(38,130,76),1);
        hit(x+1,yy,cw-2,RH,300+a); yy+=RH; tsep(x+56,yy-1,cw-57); }
    if(yy+RH<=ylim){ char b[16],*p=b; p=udec(p,(u32)nsvc); *p=0; tbold(x+18,yy+(RH-16)/2,"Background services",INK_); pdstr(x+26+pw_str("Background services",1),yy+(RH-16)/2,b,SUB,1); yy+=RH; }
    for(int k=0;k<nsvc&&yy+RH<=ylim;k++){ const char *nm,*ds,*st; int on=0, req=0; svc_info(k,&nm,&ds,&st,&on,&req); int ty=yy+(RH-16)/2; int s2=RH>30?24:20;
        if(in_rect(mx,my,x+1,yy,cw-2,RH))fill_rect(x+1,yy,cw-2,RH,rgb(246,246,248));
        fill_round(x+20,yy+(RH-s2)/2,s2,s2,7,15,on?rgb(214,236,222):rgb(228,228,232)); fill_circle(x+20+s2/2,yy+RH/2,4,on?rgb(46,160,90):rgb(160,160,166));
        pdstr(x+56,ty,nm,INK_,1); { char b[24],*p=b; pct(p,on?tm_svc_pct[k]:0); tright(rCPU,ty,b,INK_,1); } tright(rMEM,ty,"-",SUB,1);
        pdstr(cST,ty,on?"Running":"Stopped",on?rgb(38,130,76):SUB,1);
        if(!req){ const char* bt=on?"Stop":"Start"; int bw2=pw_str(bt,1)+20; int bx2=x+cw-bw2-10, by2=yy+(RH-22)/2; int hv=in_rect(mx,my,bx2,by2,bw2,22); fill_round(bx2,by2,bw2,22,11,15,hv?rgb(214,214,220):rgb(232,232,236)); pdstr(bx2+10,by2+3,bt,on?rgb(176,40,40):rgb(38,120,70),1); hit(bx2,by2,bw2,22,700+k); }
        else pdstr(x+cw-pw_str("needed",1)-14,ty,"needed",SUB,1);
        yy+=RH; tsep(x+56,yy-1,cw-57); }
    if(yy+RH<=ylim){ char b[16],*p=b; p=udec(p,(u32)njob); *p=0; int ty=yy+(RH-16)/2; tbold(x+18,ty,"Hidden apps",INK_); pdstr(x+26+pw_str("Hidden apps",1),ty,b,SUB,1);
        const char* bt=tm_showhid?"Hide":"Show hidden"; int bw2=pw_str(bt,1)+20; int bx2=x+cw-bw2-10, by2=yy+(RH-22)/2; int hv=in_rect(mx,my,bx2,by2,bw2,22); fill_round(bx2,by2,bw2,22,11,15,hv?rgb(214,214,220):rgb(232,232,236)); pdstr(bx2+10,by2+3,bt,INK_,1); hit(bx2,by2,bw2,22,698); yy+=RH; }
    if(tm_showhid) for(int j=0;j<JMAX&&yy+RH<=ylim;j++){ struct jproc* jp=&g_jp[j]; if(jp->state==S_FREE||jp->state==S_ZOMB)continue; int ty=yy+(RH-16)/2; int s2=RH>30?24:20; int sel=(j==tm_jsel);
        if(sel)fill_rect(x+1,yy,cw-2,RH,mix(CARD,ACC,40)); else if(in_rect(mx,my,x+1,yy,cw-2,RH))fill_rect(x+1,yy,cw-2,RH,rgb(246,246,248));
        fill_round(x+20,yy+(RH-s2)/2,s2,s2,7,15,rgb(222,222,226)); fill_round(x+26,yy+(RH-s2)/2+6,s2-12,s2-12,3,15,SUB);
        { char nmb[64],*p=nmb; const char* c=jp->cmd; const char* base=c; for(int q=0;c[q]&&c[q]!=' ';q++) if(c[q]=='/')base=c+q+1; int o=0; while(base[o]&&o<40){ *p++=base[o]; o++; } p=scopy(p,"   pid "); p=udec(p,(u32)jp->pid); *p=0; pdstr(x+56,ty,nmb,INK_,1); }
        { char b[24],*p=b; pct(p,tm_job_pct[j]); tright(rCPU,ty,b,INK_,1); } tright(rMEM,ty,"-",SUB,1);
        pdstr(cST,ty,jp->state==S_RUN?"Running":jp->state==S_STOP?"Stopped":"Waiting",jp->state==S_STOP?SUB:rgb(38,130,76),1);
        hit(x+1,yy,cw-2,RH,800+j); yy+=RH; tsep(x+56,yy-1,cw-57); }
    { char b[96],*p=b; p=udec(p,(u32)(napp+nsvc+njob)); p=scopy(p," processes   -   CPU "); p=pct(p,cpu_now); p=scopy(p,"   -   Memory "); p=mb(p,(u64)heap_used()/1048576ULL+64); p=scopy(p," of "); p=mb(p,g_ram_mb); *p=0; pdstr(x+4,tm_ybot-20,b,SUB,1); } }

static void perf_tile(int x,int y,int w,int h,int k,const char* name,const char* val,const u8* d,u32 col,int last){
    int on=(tmperf==k), hov=in_rect(mx,my,x+1,y,w-2,h);
    if(on)fill_round(x+6,y+4,w-12,h-8,8,15,rgb(232,232,236)); else if(hov)fill_round(x+6,y+4,w-12,h-8,8,15,rgb(244,244,246));
    fill_circle(x+22,y+h/2,5,col);
    if(on)tbold(x+36,y+h/2-17,name,INK_); else pdstr(x+36,y+h/2-17,name,INK_,1);
    { char b[64]; tclip(b,val,w-36-70); pdstr(x+36,y+h/2+3,b,SUB,1); }
    int gw=56, gh=h-22, gx=x+w-gw-14, gy=y+11;
    if(d){ int px=-1,py=-1; for(int q=0;q<HIST;q+=2){ int v=d[(hpos+q)%HIST]; int xx=gx+gw*q/HIST, yy=gy+gh-1-(gh-2)*v/100; if(px>=0)line_bl(px,py,xx,yy,col,255,1); px=xx; py=yy; } }
    if(!last)tsep(x+36,y+h-1,w-37);
    hit(x,y,w,h,200+k); }
static void row2(int x,int y,const char* a,const char* b){ pdstr(x,y,a,SUB,1); char t[96]; tclip(t,b,tm_rx-x-170); tright(tm_rx,y,t,INK_,1); }
static void performance(int x,int y,int w,int h){
    (void)h; const char* nm; u32 did=0,ss=0,eu=0,mhz=0,mxz=0,st=0,tf=0; int hg=igpu_info(&nm,&did,&ss,&eu,&mhz,&mxz,&st,&tf);
    char v[5][48]; char* p;
    p=v[0]; p=pct(p,cpu_now); p=scopy(p,"  "); p=udec(p,(u32)g_ncpu); p=scopy(p," cores"); *p=0;
    p=v[1]; p=mb(p,(u64)heap_used()/1048576ULL+64); *p++='/'; p=mb(p,g_ram_mb); *p=0;
    nvgsp_info_t ni; nvgsp_info(&ni); int hn=!hg&&ni.dev;
    p=v[2]; if(hg){ p=pct(p,gpu_pct()); p=scopy(p,"  "); p=udec(p,mhz); p=scopy(p," MHz"); } else if(!hn){ gpu_t gl[2]; if(gpu_list(gl,2)){ p=scopy(p,gl[0].vendor); p=scopy(p," detected"); } else p=scopy(p,"no driver"); } else if(hn){ if(ni.state>=7&&!ni.off){ p=pct(p,gpu_pct()); p=scopy(p,ni.drawing?"  NVIDIA drawing":"  NVIDIA on"); } else p=scopy(p,ni.off?"NVIDIA off":ni.state?"NVIDIA starting":"NVIDIA not started"); } *p=0;
    p=v[3]; { int n=gdisk_count(); if(n){ p=mb(p,(u64)gdisk_size_of(gdisk_current())*512ULL/1048576ULL); } else p=scopy(p,"no disk"); } *p=0;
    p=v[4]; p=scopy(p,(net_ok||g_wnd_up)?"Connected":"Not connected"); *p=0;
    static const char* TN[5]={"CPU","Memory","GPU","Disk","Network"}; u32 TC[5]={rgb(30,130,230),rgb(140,90,220),rgb(40,170,120),rgb(60,170,90),rgb(200,120,40)};
    int lw=w>=760?260:220, th=64; tcard(x,y,lw,5*th+8); for(int k=0;k<5;k++) perf_tile(x,y+4+k*th,lw,th,k,TN[k],v[k],k==0?hcpu:k==1?hmem:k==2?hgpu:0,TC[k],k==4);
    int gx=x+lw+16, gw=w-lw-16, gy=y; if(gw<240)return;
    tbold(gx+4,gy+2,TN[tmperf],INK_);
    const char* sub2=tmperf==0?g_brand:tmperf==2&&hg?nm:tmperf==2&&hn?gpu_name(0x10DE,ni.dev):tmperf==3&&gdisk_count()?gdisk_name_of(gdisk_current()):"";
    { char b[96]; const char* q=sub2; while(*q==' ')q++; tclip(b,q,gw-pw_str(TN[tmperf],1)-30); tright(gx+gw-4,gy+2,b,SUB,1); }
    gy+=28; tcard(gx,gy,gw,tm_ybot-gy); gx+=20; gw-=40; gy+=16; tm_rx=gx+gw; int gh=150;
    if(tmperf<3){ pdstr(gx,gy,tmperf==0?"Utilisation of cores 1 and up":tmperf==1?"Memory in use":"GPU busy per frame",SUB,1); { char b[16],*p2=b; pct(p2,tmperf==0?cpu_now:tmperf==1?mem_pct():gpu_pct()); tbold(gx+gw-pw_str(b,1)-1,gy,b,TC[tmperf]); } gy+=24; graph(gx,gy,gw,gh,tmperf==0?hcpu:tmperf==1?hmem:hgpu,TC[tmperf]); gy+=gh+34; }
    char b[64];
    if(tmperf==0){ p=b; p=pct(p,cpu_now); *p=0; row2(gx,gy,"Utilisation",b); gy+=22; p=b; p=udec(p,(u32)g_ncpu); p=scopy(p," ("); p=udec(p,(u32)g_cores_online); p=scopy(p," online)"); *p=0; row2(gx,gy,"Cores",b); gy+=22;
        { extern int ctemp_text(char*,int,int); if(ctemp_text(b,64,0)){ row2(gx,gy,"Temperature",b); gy+=22; } if(ctemp_text(b,64,1)){ row2(gx,gy,"Speed",b); gy+=22; } }
        for(int i=0;i<g_ncpu&&i<NCPU&&i<8;i++){ struct pcpu* c=&g_cpu[i]; p=b; p=scopy(p,"Core "); p=udec(p,(u32)i); *p=0; row2(gx,gy,b,i==0?"desktop, drivers and the shell":c->online?(c->parked?"parked":c->js_running?"running a program":"idle"):"offline"); gy+=20; } }
    else if(tmperf==1){ p=b; p=mb(p,g_ram_mb); *p=0; row2(gx,gy,"Installed",b); gy+=22; p=b; p=mb(p,(u64)heap_used()/1048576ULL); *p=0; row2(gx,gy,"Kernel heap in use",b); gy+=22; p=b; p=udec(p,g_gfx_layer_mb); p=scopy(p," MB in "); p=udec(p,g_gfx_layers); p=scopy(p," window layers"); *p=0; row2(gx,gy,"Graphics buffers",b); }
    else if(tmperf==2&&hn){ p=b; p=scopy(p,"NoovexOS GSP-RM driver, 10de:"); p=hx(p,ni.dev,4); *p=0; row2(gx,gy,"Driver",b); gy+=22;
        row2(gx,gy,"GSP state",ni.state_name); gy+=22;
        if(ni.off){ p=b; p=scopy(p,"OFF "); p=udec(p,(u32)ni.off_age_s); p=scopy(p," s ago - Ctrl+Alt+N turns it on"); *p=0; row2(gx,gy,"NVIDIA drawing",b); gy+=22; row2(gx,gy,"Why off",ni.off_why); gy+=22; }
        else if(ni.state>=7){ row2(gx,gy,"NVIDIA drawing","on"); gy+=22; }
        row2(gx,gy,"Copy engine (screen)",ni.present?"on - frames go through the copy engine":ni.present_ok?"set up, off":"not set up"); gy+=22;
        row2(gx,gy,"2D engine",ni.twod?"working":ni.twod_obj?"created, not verified":"not created"); gy+=22;
        row2(gx,gy,"Compute shaders",ni.compute?"working":ni.compute_loaded?"loaded, not verified":"not loaded"); gy+=22;
        row2(gx,gy,"Desktop drawn by GPU",ni.drawing?"yes":"no"); gy+=22;
        p=b; p=udec(p,ni.cp_subs); p=scopy(p," batches, "); p=udec(p,ni.cp_ops); p=scopy(p," launches, last "); p=udec(p,ni.cp_last_us); p=scopy(p," us"); *p=0; row2(gx,gy,"Compute work",b); gy+=22;
        p=b; p=udec(p,ni.frames); p=scopy(p," frames, "); p=udec(p,ni.ce_timeouts+ni.cp_timeouts+ni.twod_timeouts); p=scopy(p," timeouts, "); p=udec(p,ni.readback_bad); p=scopy(p," bad read-backs"); *p=0; row2(gx,gy,"Screen copies",b); gy+=22;
        if(ni.vram_total){ p=b; p=mb(p,ni.vram_free/1048576ULL); p=scopy(p," free of "); p=mb(p,ni.vram_total/1048576ULL); p=scopy(p,", "); p=udec(p,ni.vram_surfaces); p=scopy(p," work buffers on the card"); *p=0; row2(gx,gy,"Graphics memory",b); gy+=22; }
        p=b; p=udec(p,g_gfx_frame_us/1000); *p++='.'; p=udec(p,(g_gfx_frame_us/100)%10); p=scopy(p," ms"); *p=0; row2(gx,gy,"Last frame",b); }
    else if(tmperf==2){ if(!hg){ gpu_t gl[4]; int gn=gpu_list(gl,4); if(!gn){ row2(gx,gy,"Graphics","none found"); return; }
            for(int k=0;k<gn;k++){ p=b; p=scopy(p,gl[k].vendor); p=scopy(p," "); p=hx(p,gl[k].ven,4); *p++=':'; p=hx(p,gl[k].dev,4); *p=0; row2(gx,gy,b,gl[k].name); gy+=20; { char d[64]; int q=0; while(gl[k].driver[q]&&q<62){ d[q]=gl[k].driver[q]; q++; } d[q]=0; row2(gx,gy,"",d); } gy+=24; } return; }
        p=b; p=pct(p,gpu_pct()); *p=0; row2(gx,gy,"Busy",b); gy+=22; p=b; p=udec(p,mhz); p=scopy(p," MHz of "); p=udec(p,mxz); p=scopy(p," MHz"); *p=0; row2(gx,gy,"Clock",b); gy+=22;
        p=b; p=udec(p,ss); p=scopy(p," x "); p=udec(p,eu); p=scopy(p," = "); p=udec(p,ss*eu); p=scopy(p," EUs"); *p=0; row2(gx,gy,"Execution units",b); gy+=22;
        p=b; p=udec(p,g_gfx_frame_us/1000); *p++='.'; p=udec(p,(g_gfx_frame_us/100)%10); p=scopy(p," ms"); *p=0; row2(gx,gy,"Last frame",b); gy+=22;
        p=b; p=udec(p,st); p=scopy(p," MB shared"); *p=0; row2(gx,gy,"Graphics memory",b); gy+=22; row2(gx,gy,"GPU drawing",g_gfx==GFX_GPU?"on":g_gfx==GFX_EMU?"emulated":"off"); gy+=22; row2(gx,gy,"Tear-free",tf?"on":"off"); }
    else if(tmperf==3){ int n=gdisk_count(); for(int k=0;k<n&&k<8;k++){ p=b; p=mb(p,(u64)gdisk_size_of(k)*512ULL/1048576ULL); if(k==gdisk_current())p=scopy(p,"  system disk"); *p=0; row2(gx,gy,gdisk_name_of(k),b); gy+=22; } if(!n)row2(gx,gy,"Disks","none found"); }
    else { int ek=nic_index(); row2(gx,gy,"Adapter",ek>=0?nic_name(pci_ven[ek],pci_dev[ek]):"none"); gy+=22; row2(gx,gy,"Wired",net_ok?"connected":"not connected"); gy+=22; row2(gx,gy,"Wi-Fi",g_wnd_up?"connected":"not connected"); gy+=22;
        if(net_ok||g_wnd_up){ p=b; for(int q=3;q>=0;q--){ p=udec(p,(my_ip>>(8*(3-q)))&0xFF); if(q)*p++='.'; } *p=0; row2(gx,gy,"IPv4 address",b); } } }

#define DMAX 28
static const char* dt_[DMAX]; static char dd_[DMAX][96]; static u32 dc_[DMAX]; static int dn_=0;
static char* D(const char* t,u32 c){ if(dn_>=DMAX)return dd_[DMAX-1]; dt_[dn_]=t; dc_[dn_]=c; dd_[dn_][0]=0; return dd_[dn_++]; }
static const char* pcls(u8 cl,u8 sub){ switch(cl){ case 0x01: return sub==0x06?"SATA controller":sub==0x08?"NVMe controller":"Storage controller"; case 0x02: return sub==0x80?"Wi-Fi adapter":"Ethernet adapter"; case 0x03:return "Display controller"; case 0x04:return "Audio device"; case 0x05:return "Memory controller"; case 0x06:return sub==0x00?"Host bridge":sub==0x01?"ISA bridge":sub==0x04?"PCI bridge":"Bridge"; case 0x07:return "Communication controller"; case 0x08:return "System peripheral"; case 0x0C:return sub==0x03?"USB controller":sub==0x05?"SMBus controller":"Serial bus controller"; case 0x0D:return "Wireless controller"; case 0x11:return "Signal processing"; default:return "Other device"; } }
static const char* vname(u16 v){ return v==0x8086?"Intel":v==0x10DE?"NVIDIA":v==0x1002?"AMD":v==0x10EC?"Realtek":v==0x1B36?"QEMU":v==0x1234?"QEMU":v==0x15AD?"VMware":v==0x80EE?"VirtualBox":v==0x168C?"Qualcomm Atheros":v==0x14E4?"Broadcom":v==0x144D?"Samsung":v==0x1987?"Phison":v==0x15B7?"SanDisk":v==0x1C5C?"SK hynix":"Unknown vendor"; }
static void devlist(int cat){
    char* p; u32 OK=rgb(40,140,80), OFF=rgb(150,156,170), WARN=rgb(210,130,40); dn_=0; if(!pci_n)pci_scan();
    if(cat==0){ p=D("Processor",OK); scopy(p,g_brand)[0]=0;
        p=D("Cores",OK); { u8 ids[NCPU]; int c=madt_apic_ids(ids,NCPU); p=udec(p,(u32)(c>0?c:g_ncpu)); p=scopy(p," cores, "); p=udec(p,(u32)g_cores_online); p=scopy(p," online"); *p=0; }
        { u32 a,b,c,d; cpuid(1,&a,&b,&c,&d); p=D("Family / model / stepping",OK); p=udec(p,(a>>8)&15); p=scopy(p," / "); p=udec(p,((a>>4)&15)|(((a>>16)&15)<<4)); p=scopy(p," / "); p=udec(p,a&15); *p=0;
          p=D("Features",OK); p=scopy(p,"64-bit"); if(d&(1u<<25))p=scopy(p,", SSE"); if(d&(1u<<26))p=scopy(p,", SSE2"); if(c&(1u<<19))p=scopy(p,", SSE4.1"); if(c&(1u<<20))p=scopy(p,", SSE4.2"); if(c&(1u<<25))p=scopy(p,", AES"); if(c&(1u<<28))p=scopy(p,", AVX"); if(c&(1u<<5))p=scopy(p,", VT-x"); *p=0; } }
    else if(cat==1){ const char* nm; u32 did=0,ss=0,eu=0,mhz=0,mxz=0,st=0,tf=0;
        if(igpu_info(&nm,&did,&ss,&eu,&mhz,&mxz,&st,&tf)){ p=D("Intel graphics",OK); p=scopy(p,nm); p=scopy(p,"  8086:"); p=hx(p,did,4); *p=0;
            p=D("Execution units",OK); p=udec(p,ss); p=scopy(p," subslices x "); p=udec(p,eu); p=scopy(p," EUs"); *p=0;
            p=D("Clock",OK); p=udec(p,mhz); p=scopy(p," MHz now, up to "); p=udec(p,mxz); p=scopy(p," MHz"); *p=0;
            p=D("Graphics memory",OK); p=udec(p,st); p=scopy(p," MB reserved from main memory"); *p=0;
            p=D("Driver",g_gfx==GFX_GPU?OK:WARN); p=scopy(p,g_gfx==GFX_GPU?"NoovexOS gen 9 driver, GPU drawing on":"NoovexOS gen 9 driver, GPU drawing off"); *p=0; }
        for(int k=0;k<pci_n;k++) if(pci_cls[k]==0x03&&!(pci_ven[k]==0x8086&&igpu_info(&nm,&did,&ss,&eu,&mhz,&mxz,&st,&tf))){ p=D(pci_ven[k]==0x10DE||pci_ven[k]==0x1002?"Graphics card":"Display controller",OFF); p=scopy(p,gpu_name(pci_ven[k],pci_dev[k])); p=scopy(p,"  "); p=hx(p,pci_ven[k],4); *p++=':'; p=hx(p,pci_dev[k],4); { nvgsp_info_t ni; nvgsp_info(&ni); if(pci_ven[k]==0x10DE&&ni.state>=1){ dc_[dn_-1]=ni.state>=7&&!ni.off?OK:WARN; p=scopy(p,"  GSP-RM: "); p=scopy(p,ni.off?"drawing off":ni.state>=7?(ni.drawing?"desktop drawn by the GPU":"running"):ni.state_name); } else p=scopy(p,"  firmware framebuffer only"); } *p=0; } }
    else if(cat==2){ p=D("Installed memory",OK); p=mb(p,g_ram_mb); *p=0; p=D("Kernel heap in use",OK); p=mb(p,(u64)heap_used()/1048576ULL); *p=0;
        p=D("Graphics buffers",OK); p=udec(p,g_gfx_layer_mb); p=scopy(p," MB window layers"); *p=0;
        for(int k=0;k<pci_n;k++) if(pci_cls[k]==0x05){ p=D("Memory controller",OK); p=scopy(p,vname(pci_ven[k])); p=scopy(p,"  "); p=hx(p,pci_ven[k],4); *p++=':'; p=hx(p,pci_dev[k],4); *p=0; } }
    else if(cat==3){ battery_tick();
        p=D("Power supply",OK); p=scopy(p,bat_present?(bat_charging?"AC adapter connected":"Running on battery"):"Mains power (no battery found)"); *p=0;
        if(bat_present){ p=D("Battery charge",bat_pct<=15?WARN:OK); p=udec(p,(u32)bat_pct); p=scopy(p,bat_charging?"%  charging":"%  discharging"); *p=0; }
        for(int i=0;i<2;i++){ acbat* b=&g_acb[i]; if(!b->present)continue; p=D("Battery",OK); p=scopy(p,b->model[0]?b->model:"Battery"); if(b->oem[0]){ p=scopy(p,"  "); p=scopy(p,b->oem); } if(b->type[0]){ p=scopy(p,"  "); p=scopy(p,b->type); } *p=0;
            if(b->design&&b->design!=0xFFFFFFFFu&&b->full&&b->full!=0xFFFFFFFFu){ p=D("Battery health",OK); p=udec(p,(u32)((u64)b->full*100/b->design)); p=scopy(p,"%  ("); p=udec(p,b->full); p=scopy(p," of "); p=udec(p,b->design); p=scopy(p,b->mwh?" mWh)":" mAh)"); *p=0; }
            if(b->cycles&&b->cycles!=0xFFFFFFFFu){ p=D("Charge cycles",OK); p=udec(p,b->cycles); *p=0; }
            if(b->volt&&b->volt!=0xFFFFFFFFu){ p=D("Design voltage",OK); p=udec(p,b->volt); p=scopy(p," mV"); *p=0; }
            if(b->rate&&b->rate!=0xFFFFFFFFu){ p=D("Power draw",OK); p=udec(p,b->rate); p=scopy(p,b->mwh?" mW":" mA"); *p=0; } } }
    else if(cat==4){ for(int k=0;k<gdisk_count();k++){ p=D(gdisk_name_of(k),k==gdisk_current()?OK:OFF); p=mb(p,(u64)gdisk_size_of(k)*512ULL/1048576ULL); if(k==gdisk_current())p=scopy(p,"  system disk"); *p=0; }
        if(msd_ok){ p=D("USB drive",OK); p=mb(p,(u64)(msd_lastlba+1)*512ULL/1048576ULL); *p=0; }
        for(int k=0;k<pci_n;k++) if(pci_cls[k]==0x01){ p=D(pcls(pci_cls[k],pci_sub[k]),OK); p=scopy(p,vname(pci_ven[k])); p=scopy(p,"  "); p=hx(p,pci_ven[k],4); *p++=':'; p=hx(p,pci_dev[k],4); *p=0; } }
    else if(cat==5){ for(int k=0;k<pci_n;k++) if(pci_cls[k]==0x02||pci_cls[k]==0x0D){ int wl=pci_sub[k]==0x80||pci_cls[k]==0x0D; p=D(wl?"Wi-Fi adapter":"Ethernet adapter",wl?(g_wnd_up?OK:OFF):(net_ok?OK:OFF)); p=scopy(p,nic_name(pci_ven[k],pci_dev[k])); p=scopy(p,wl?(g_wnd_up?"  connected":"  not connected"):(net_ok?"  connected":"  no link")); *p=0; }
        if(net_ok||g_wnd_up){ p=D("IPv4 address",OK); for(int q=3;q>=0;q--){ p=udec(p,(my_ip>>(8*(3-q)))&0xFF); if(q)*p++='.'; } *p=0; p=D("MAC address",OK); for(int q=0;q<6;q++){ p=hx(p,my_mac[q],2); if(q<5)*p++=':'; } *p=0; } }
    else if(cat==6){ for(int k=0;k<pci_n;k++) if(pci_cls[k]==0x04){ p=D("Sound card",g_snd_state?OK:OFF); p=scopy(p,g_snd_state&&g_snd.name?g_snd.name:pci_sub[k]==0x03?"HD Audio controller":"Audio device"); p=scopy(p,g_snd_state?"  driver active":"  driver not started"); *p=0; }
        p=D("Volume",OK); p=udec(p,(u32)g_snd_vol); *p++='%'; *p=0; }
    else if(cat==7){ for(int k=0;k<pci_n;k++) if(pci_cls[k]==0x0C&&pci_sub[k]==0x03){ p=D("USB controller",OK); p=scopy(p,vname(pci_ven[k])); p=scopy(p,"  "); p=hx(p,pci_ven[k],4); *p++=':'; p=hx(p,pci_dev[k],4); *p=0; }
        for(int k=0;k<g_usbdev_n;k++) if(g_usbdev[k].used){ p=D(g_usbdev[k].kind[0]?g_usbdev[k].kind:"USB device",OK); p=scopy(p,g_usbdev[k].name[0]?g_usbdev[k].name:"(no name)"); p=scopy(p,"  "); p=hx(p,g_usbdev[k].vid,4); *p++=':'; p=hx(p,g_usbdev[k].pid,4); p=scopy(p,"  port "); p=udec(p,(u32)g_usbdev[k].port+1); *p=0; }
        if(!dn_){ p=D("No USB",OFF); scopy(p,"No USB controller or device found")[0]=0; } }
    else if(cat==8){ p=D("Screen",OK); p=udec(p,W); *p++='x'; p=udec(p,H); p=scopy(p,"  "); p=udec(p,BPP); p=scopy(p,"-bit colour"); *p=0;
        p=D("Refresh",OK); p=udec(p,(u32)g_refresh_hz); p=scopy(p," Hz desktop updates"); *p=0;
        p=D("Drawing",OK); { extern int g_nvd_on; p=scopy(p,g_gfx==GFX_GPU?(g_nvd_on?"NVIDIA GPU (compute shaders)":"Intel GPU"):g_gfx==GFX_EMU?"CPU (GPU emulated)":"CPU"); } *p=0; }
    else if(cat==9){ p=D("Keyboard",OK); scopy(p,"Built-in keyboard, PS/2 controller")[0]=0;
        p=D("Touchpad",OK); scopy(p,g_tp.ok?"Precision touchpad, HID over I2C":g_syn_ready?"Synaptics touchpad, PS/2":g_etp_ready?"ELAN touchpad, PS/2":"PS/2 pointing device")[0]=0;
        for(int k=0;k<g_usbdev_n;k++) if(g_usbdev[k].used&&g_usbdev[k].hid){ p=D(g_usbdev[k].kind[0]?g_usbdev[k].kind:"USB input",OK); p=scopy(p,g_usbdev[k].name[0]?g_usbdev[k].name:"USB HID device"); *p=0; } }
    else { for(int k=0;k<pci_n;k++){ p=D(pcls(pci_cls[k],pci_sub[k]),OK); p=scopy(p,vname(pci_ven[k])); p=scopy(p,"  "); p=hx(p,pci_ven[k],4); *p++=':'; p=hx(p,pci_dev[k],4); p=scopy(p,"  bus "); p=udec(p,pci_bus[k]); p=scopy(p," slot "); p=udec(p,pci_slot[k]); *p=0; } } }
static const char* DCAT[11]={"Processor","Graphics","Memory","Power supply and battery","Storage","Network","Sound","USB","Display","Input","All PCI devices"};
const char* taskmgr_devcat(int k){ return (k>=0&&k<11)?DCAT[k]:""; }
int taskmgr_devdump(int cat,char* out,int cap){ devlist(cat); u32 OK=rgb(40,140,80), WARN=rgb(210,130,40); char* p=out; char* e=out+cap-1;
    for(int i=0;i<dn_;i++){ char ln[200],*q=ln; q=scopy(q,dt_[i]); q=scopy(q,": "); q=scopy(q,dd_[i]); q=scopy(q,dc_[i]==OK?"   [working]":dc_[i]==WARN?"   [warning]":"   [not active]"); *q++='\n'; *q=0;
        for(char* c=ln;*c&&p<e;c++)*p++=*c; }
    *p=0; return (int)(p-out); }
static void dicon(int x,int y,int k){ static const u32 IC[11]={rgb(53,132,228),rgb(46,160,110),rgb(145,65,172),rgb(51,160,90),rgb(230,130,30),rgb(53,132,228),rgb(220,60,110),rgb(94,92,100),rgb(30,150,160),rgb(119,118,123),rgb(61,56,70)};
    fill_round(x,y,30,30,8,15,IC[k]); static const char G1[11]={'C','G','M','P','S','N','A','U','D','I','*'}; char t[2]={G1[k],0}; tbold_ctr(x+15,y+7,t,rgb(255,255,255)); }
static int dscroll=0, dmaxs=0;
static void devices(int x,int y,int w,int h){
    (void)h; int cw=w; tbold(x+4,y+2,"Devices",INK_); tright(x+cw-4,y+2,"Every component this computer reports",SUB,1); y+=28;
    int top=y, ymax=tm_ybot; tcard(x,top,cw,ymax-top); int in0=top+1, in1=ymax-1;
    if(dscroll>dmaxs)dscroll=dmaxs; if(dscroll<0)dscroll=0; int yy=top+1-dscroll; int y0=yy;
    for(int k=0;k<11;k++){ int on=(tmdev==k); devlist(k); int cnt=dn_; int RH=50;
        if(yy>=in0&&yy+RH<=in1){ int hov=in_rect(mx,my,x+1,yy,cw-2,RH); if(hov)fill_rect(x+1,yy,cw-2,RH,rgb(246,246,248));
            dicon(x+16,yy+10,k); if(on)tbold(x+60,yy+17,DCAT[k],INK_); else pdstr(x+60,yy+17,DCAT[k],INK_,1);
            { char b[16],*p=b; p=udec(p,(u32)cnt); *p=0; tright(x+cw-48,yy+17,b,SUB,1); } tchev(x+cw-30,yy+(on?22:20),SUB,on); hit(x+1,yy,cw-2,RH,100+k); }
        yy+=RH;
        if(on){ for(int r=0;r<dn_;r++){ int R2=30; if(yy>=in0&&yy+R2<=in1){ fill_rect(x+1,yy,cw-2,R2,rgb(248,248,250)); fill_circle(x+70,yy+15,4,dc_[r]); char t[96]; tclip(t,dt_[r],230); pdstr(x+84,yy+7,t,INK_,1); tclip(t,dd_[r],cw-84-250-24); tright(x+cw-24,yy+7,t,SUB,1); } yy+=R2; } }
        if(k<10&&yy-1>=in0&&yy-1<in1)tsep(x+1,yy-1,cw-2); }
    dmaxs=(yy-y0)-(in1-in0); if(dmaxs<0)dmaxs=0;
    if(dmaxs>0){ int th=in1-in0-12; int bh=th*th/(th+dmaxs); if(bh<24)bh=24; int by=in0+6+(th-bh)*dscroll/(dmaxs?dmaxs:1); fill_round(x+cw-8,by,4,bh,2,15,rgb(190,190,196)); } }
void taskmgr_wheel(int sc){ if(tmt==2){ dscroll-=sc*50; if(dscroll<0)dscroll=0; if(dscroll>dmaxs)dscroll=dmaxs; } }
static void services(int x,int y,int w,int h){
    (void)h; int cw=w; tbold(x+4,y+2,"Services",INK_); tright(x+cw-4,y+2,"Parts of NoovexOS that run in the background",SUB,1); y+=28;
    struct { const char* n; const char* d; int on; } S[10]={{"compositor","windows, shadows, glass, animations",1},{"gpu-draw","GPU draw queue (Intel or NVIDIA)",g_gfx==GFX_GPU},{"netstack","TCP/IP, DHCP, DNS, TLS",net_ok||g_wnd_up},{"wifi","Realtek RTL8821CE driver",g_wnd_up},{"nvxfs","disk file system",1},{"usb","xHCI host, keyboard, mouse, storage",xhci_present},{"audio","HD Audio / AC97 output",g_snd_state},{"acpi","power, battery, backlight",1},{"smp","multi-core scheduler",g_cores_online>1},{"shell","nsh command shell",1}};
    int RH=56; if(y+10*RH+2>tm_ybot){ RH=(tm_ybot-y-2)/10; if(RH<34)RH=34; }
    int n=10; while(n>0&&y+n*RH+2>tm_ybot)n--; tcard(x,y,cw,n*RH+2);
    for(int k=0;k<n;k++){ int yy=y+1+k*RH; int big=RH>=48;
        fill_circle(x+24,yy+RH/2,5,S[k].on?rgb(46,160,90):rgb(190,190,196));
        if(big){ pdstr(x+44,yy+9,S[k].n,INK_,1); pdstr(x+44,yy+29,S[k].d,SUB,1); } else { pdstr(x+44,yy+(RH-16)/2,S[k].n,INK_,1); pdstr(x+170,yy+(RH-16)/2,S[k].d,SUB,1); }
        tpill(x+cw-16,yy+(RH-24)/2,S[k].on?"Running":"Stopped",S[k].on?rgb(222,244,229):rgb(236,236,239),S[k].on?rgb(38,130,76):SUB);
        if(k<n-1)tsep(x+44,yy+RH-1,cw-45); } }
static void twinbtn(int b,int bx,int cy){ int hov=in_rect(mx,my,bx-1,cy-10,20,20); fill_circle(bx+9,cy,9,hov?rgb(210,210,215):rgb(226,226,230)); u32 g=INK_;
    if(b==0)fill_rect(bx+6,cy+2,7,2,g);
    else if(b==1){ fill_rect(bx+6,cy-3,7,1,g); fill_rect(bx+6,cy+3,7,1,g); fill_rect(bx+6,cy-3,1,7,g); fill_rect(bx+12,cy-3,1,7,g); fill_rect(bx+6,cy-2,7,1,g); }
    else { for(int t=0;t<6;t++){ fill_rect(bx+6+t,cy-3+t,2,1,g); fill_rect(bx+11-t,cy-3+t,2,1,g); } } }
int sysmon_cpu_pct(void){ sample(); return cpu_now; }
int sysmon_mem_pct(void){ return mem_pct(); }
static int tm_hdr_lo=0;
static int tm_ctab=0;
static void sm_graph(int x,int y,int w,int h,const u32* a,u32 af,u32 al,const u32* b,u32 bf,u32 bl,u32 mx){
    ui_bevel(x,y,w,h,1); fill_rect(x+2,y+2,w-4,h-4,rgb(255,255,255)); int gx0=x+2, gy0=y+2, gw=w-4, gh=h-4; if(!mx)mx=1;
    for(int r=1;r<4;r++) for(int q=0;q<gw;q+=4) putpx(gx0+q,gy0+gh*r/4,rgb(224,224,224));
    int pa=-1,pb=-1;
    for(int q=0;q<gw;q++){ int k2=q*(HIST-1)*256/(gw>1?gw-1:1); int k=k2>>8, f=k2&255; int k1=k<HIST-1?k+1:k;
        u32 va=(a[(hpos+k)%HIST]*(256-f)+a[(hpos+k1)%HIST]*f)>>8; int ya=gh-1-(int)((u64)va*(gh-2)/mx); if(ya<0)ya=0;
        fill_rect(gx0+q,gy0+ya,1,gh-ya,af); if(pa>=0){ int t0=pa<ya?pa:ya, t1=pa<ya?ya:pa; fill_rect(gx0+q,gy0+t0,1,t1-t0+1,al); } else putpx(gx0+q,gy0+ya,al); pa=ya;
        if(b){ u32 vb=(b[(hpos+k)%HIST]*(256-f)+b[(hpos+k1)%HIST]*f)>>8; int yb=gh-1-(int)((u64)vb*(gh-2)/mx); if(yb<0)yb=0;
            fill_rect(gx0+q,gy0+yb,1,gh-yb,bf); if(pb>=0){ int t0=pb<yb?pb:yb, t1=pb<yb?yb:pb; fill_rect(gx0+q,gy0+t0,1,t1-t0+1,bl); } else putpx(gx0+q,gy0+yb,bl); pb=yb; } } }
static void sm_rtext(int xr,int y,const char* t,u32 c){ int l=0; while(t[l])l++; fill_rect(xr-l*8-2,y,l*8+2,16,rgb(255,255,255)); dstr(xr-l*8,y,t,c,1); }
static void cl_group(int x,int y,int w,int h,const char* t){ fill_rect(x,y+7,w,1,rgb(128,128,128)); fill_rect(x,y+8,w,1,rgb(255,255,255)); fill_rect(x,y+7,1,h-7,rgb(128,128,128)); fill_rect(x+1,y+8,1,h-9,rgb(255,255,255)); fill_rect(x+w-2,y+7,1,h-7,rgb(128,128,128)); fill_rect(x+w-1,y+7,1,h-7,rgb(255,255,255)); fill_rect(x,y+h-2,w,1,rgb(128,128,128)); fill_rect(x,y+h-1,w,1,rgb(255,255,255));
    int l=0; while(t[l])l++; fill_rect(x+8,y,l*8+6,16,rgb(212,208,200)); dstr(x+11,y,t,rgb(0,0,0),1); }
static void sysmon_classic(int i){
    extern int g_round_off; g_round_off=1; colours(); hn_=0; sample(); BG_=rgb(212,208,200); LINE=rgb(128,128,128);
    int X=WN[i].x, Y=WN[i].y, ww=WN[i].w, wh=WN[i].h;
    win_frame(i,"System Monitor"); tm_hdr_lo=Y+TITLEH;
    static const char* const MB[3]={"File","Frequency","Help"}; ui_menubar(X+3,Y+TITLEH,ww-6,MB,3);
    static const char* const TB[5]={"Processes","Performance","Storage","Network","Hardware"};
    int ty=Y+TITLEH+26; int tx=X+8; ui_tabs(tx,ty,TB,5,tm_ctab);
    { int cx=tx; for(int k=0;k<5;k++){ int l=0; while(TB[k][l])l++; int iw=l*8+16; hit(cx,ty,iw,22,500+k); cx+=iw; } }
    int px=X+6, py=ty+22, pw=ww-12, ph=wh-(py-Y)-30;
    fill_rect(px,py,pw,1,rgb(255,255,255)); fill_rect(px,py,1,ph,rgb(255,255,255)); fill_rect(px+pw-1,py,1,ph,rgb(64,64,64)); fill_rect(px+pw-2,py+1,1,ph-1,rgb(128,128,128)); fill_rect(px,py+ph-1,pw,1,rgb(64,64,64)); fill_rect(px+1,py+ph-2,pw-2,1,rgb(128,128,128));
    int cx=px+10, cy=py+10, cw=pw-20; tm_ybot=py+ph-10;
    if(tm_ctab==1){ int gh=(ph-30)/2-26; char b[64],*p;
        static u32 tc[HIST], kc[HIST]; for(int k=0;k<HIST;k++){ tc[k]=hcpu[k]; kc[k]=hker[k]; }
        u32 TCL=rgb(0,170,170), TF=rgb(120,210,210), KCL=rgb(200,30,30), KF=rgb(150,70,70);
        cl_group(cx,cy,cw,gh+26,"CPU usage"); sm_graph(cx+10,cy+18,cw-20,gh,tc,TF,TCL,kc,KF,KCL,100);
        int xr=cx+cw-16; p=scopy(b,"Total: "); pct(p,cpu_now); sm_rtext(xr,cy+22,b,TCL); p=scopy(b,"Kernel: "); pct(p,ker_now); sm_rtext(xr,cy+38,b,KCL);
        int my2=cy+gh+36; cl_group(cx,my2,cw,gh+26,"Memory usage");
        u32 mx=1; for(int k=0;k<HIST;k++) if(hcm[k]>mx)mx=hcm[k]; mx+=mx/8;
        sm_graph(cx+10,my2+18,cw-20,gh,hcm,rgb(200,200,200),rgb(150,150,150),hal,rgb(120,210,210),TCL,mx);
        p=scopy(b,"Committed: "); p=udec(p,(u32)(g_hs/1024UL)); p=scopy(p," KiB"); sm_rtext(xr,my2+22,b,rgb(128,128,128));
        p=scopy(b,"Allocated: "); p=udec(p,(u32)(g_hu/1024UL)); p=scopy(p," KiB"); sm_rtext(xr,my2+38,b,TCL);
        p=scopy(b,"Physical: "); p=udec(p,(u32)g_ram_mb*1024U); p=scopy(p," KiB"); sm_rtext(xr,my2+54,b,KCL); }
    else if(tm_ctab==0) processes(cx,cy,cw,wh);
    else { devices(cx,cy,cw,wh); }
    int sy=Y+wh-26; int napp=0; for(int a=0;a<NWIN;a++) if(WN[a].open)napp++;
    { char b[48],*p; int sw=(ww-12)/3; for(int k=0;k<3;k++){ ui_bevel(X+6+k*sw,sy,sw-2,20,1); p=b; if(k==0){ p=scopy(p,"Processes: "); p=udec(p,(u32)(napp+6)); } else if(k==1){ p=scopy(p,"Threads: "); p=udec(p,(u32)(napp+6+g_cores_online)); } else { p=scopy(p,"CPU usage: "); p=pct(p,cpu_now); } *p=0; dstr(X+12+k*sw,sy+3,b,rgb(0,0,0),1); } }
    g_round_off=0;
}
void sysmon_draw(int i){
    { extern int g_dstyle; if(g_dstyle==1){ sysmon_classic(i); return; } }
    colours(); hn_=0; sample();
    int X=WN[i].x, Y=WN[i].y, ww=WN[i].w, wh=WN[i].h, foc=(focus==i), HB=48;
    shadow_soft(X,Y,ww,wh,12);
    fill_round(X,Y,ww,wh,12,15,foc?rgb(190,190,196):rgb(210,210,214)); fill_round(X+1,Y+1,ww-2,wh-2,11,15,BG_);
    fill_round(X+1,Y+1,ww-2,HB,11,1|2,rgb(242,242,244)); fill_rect(X+1,Y+HB,ww-2,1,LINE); tm_hdr_lo=Y+TITLEH;
    int lab=ww>=720; int bw[4], tot=0; for(int k=0;k<4;k++){ bw[k]=lab?pw_str(RAIL[k],1)+56:44; tot+=bw[k]+4; }
    int sx=X+ww/2-tot/2; for(int k=0;k<4;k++){ int on=(k==tmt), hov=in_rect(mx,my,sx,Y+8,bw[k],32);
        if(on)fill_round(sx,Y+8,bw[k],32,8,15,rgb(220,220,224)); else if(hov)fill_round(sx,Y+8,bw[k],32,8,15,rgb(232,232,235));
        rail_icon(sx+14,Y+16,k,INK_,on?rgb(220,220,224):hov?rgb(232,232,235):rgb(242,242,244)); if(lab){ if(on)tbold(sx+40,Y+16,RAIL[k],INK_); else pdstr(sx+40,Y+16,RAIL[k],INK_,1); } hit(sx,Y+8,bw[k],32,10+k); sx+=bw[k]+4; }
    if(tmt==0){ int en=(tmsel>=0&&tmsel<NWIN&&WN[tmsel].open)||(tm_jsel>=0&&g_jp[tm_jsel].state!=S_FREE&&g_jp[tm_jsel].state!=S_ZOMB&&g_jp[tm_jsel].pid>1); int ew=pw_str("End task",1)+34; int ex=X+10, ey=Y+8; int hov=en&&in_rect(mx,my,ex,ey,ew,32);
        fill_round(ex,ey,ew,32,8,15,hov?rgb(222,222,226):rgb(232,232,236)); if(en)tbold(ex+17,ey+8,"End task",rgb(192,28,40)); else pdstr(ex+17,ey+8,"End task",rgb(160,160,166),1); hit(ex,ey,ew,32,399); }
    for(int b=0;b<3;b++){ int cx=X+ww-6-18; int bx=(b==2)?cx:(b==1)?cx-2-18:cx-2-18-18; twinbtn(b,bx,Y+16); }
    int cx=X+16, cy=Y+HB+16, cw=ww-32; tm_ybot=Y+wh-16;
    if(tmt==0)processes(cx,cy,cw,wh); else if(tmt==1)performance(cx,cy,cw,wh); else if(tmt==2)devices(cx,cy,cw,wh); else services(cx,cy,cw,wh); }
void sysmon_click(int px,int py){
    for(int k=hn_-1;k>=0;k--) if(in_rect(px,py,hx_[k],hy_[k],hw_[k],hh_[k])){ int id=hid_[k];
        if(id>=10&&id<14){ tmt=id-10; return; }
        if(id>=500&&id<505){ tm_ctab=id-500; tmt=tm_ctab==0?0:tm_ctab==1?1:2; tmdev=tm_ctab==2?4:tm_ctab==3?5:tm_ctab==4?-1:tmdev; dscroll=0; return; }
        if(id>=100&&id<111){ tmdev=(tmdev==id-100)?-1:id-100; dscroll=0; return; }
        if(id>=200&&id<205){ tmperf=id-200; return; }
        if(id>=300&&id<300+NWIN){ tmsel=id-300; tm_jsel=-1; return; }
        if(id==399){ if(tm_jsel>=0&&tm_jsel<JMAX){ struct jproc* jp=&g_jp[tm_jsel]; if(jp->state!=S_FREE&&jp->state!=S_ZOMB&&jp->pid>1){ jc_kill(jp->pid,9,0); toast("The program was ended"); } tm_jsel=-1; return; } if(tmsel>=0&&tmsel<NWIN&&WN[tmsel].open){ close_win(tmsel); tmsel=-1; } return; }
        if(id==698){ tm_showhid=!tm_showhid; return; }
        if(id>=700&&id<716){ const char *nm,*ds,*st; int on=0, req=0; if(svc_info(id-700,&nm,&ds,&st,&on,&req)&&!req){ svc_set(id-700,!on); toast(on?"Service stopped":"Service started"); } return; }
        if(id>=800&&id<800+JMAX){ tm_jsel=id-800; tmsel=-1; return; }
        return; } }
int sysmon_title_click(int px,int py){ if(py>=tm_hdr_lo)return 0; for(int k=hn_-1;k>=0;k--){ int id=hid_[k]; if(((id>=10&&id<14)||id==399)&&in_rect(px,py,hx_[k],hy_[k],hw_[k],hh_[k])){ sysmon_click(px,py); return 1; } } return 0; }
void taskmgr_tab(int t,int dev){ if(t>=0&&t<4)tmt=t; if(dev>=0&&dev<11)tmdev=dev; }
