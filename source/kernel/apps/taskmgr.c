#include "include/nvx/base.h"
#include "include/drivers.h"
#include "include/nvx/mm/heap_api.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/drivers/video/gfx.h"
#include "include/nvx/arch/x86_64/cpuinfo.h"
#include "include/nvx/arch/x86_64/percpu.h"
#include "include/nvx/arch/x86_64/smp.h"
#include "include/nvx/arch/x86_64/smp_sched.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/net/nic_names.h"
#include "include/nvx/drivers/net/nic.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/drivers/input/i2c_hid.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "include/nvx/core/sound.h"
#include "include/nvx/net/tcpip.h"
#include "include/nvx/gui/theme.h"
#include "include/nvx/gui/wm.h"
#include "include/nvx/gui/desktop.h"
#include "include/nvx/apps/files_classic.h"

extern int bat_present, bat_pct, bat_charging, bat_via_ec; extern void battery_tick(void);
extern int gdisk_count(void); extern u32 gdisk_size_of(int); extern const char* gdisk_name_of(int); extern int gdisk_current(void);
extern int igpu_info(const char** name,u32* did,u32* ss,u32* eu,u32* mhz,u32* maxmhz,u32* stolen,u32* tearfree);
extern int g_syn_ready, g_etp_ready, g_wnd_up, msd_ok; extern u32 msd_lastlba; extern u32 g_ram_mb;
extern volatile u64 g_ticks; extern int g_refresh_hz;

static int tmt=0, tmsel=-1, tmdev=0, tmperf=0;
static int hx_[96], hy_[96], hw_[96], hh_[96], hid_[96], hn_=0;
static void hit(int x,int y,int w,int h,int id){ if(hn_<96){ hx_[hn_]=x; hy_[hn_]=y; hw_[hn_]=w; hh_[hn_]=h; hid_[hn_]=id; hn_++; } }
#define HIST 60
static u8 hcpu[HIST], hmem[HIST], hgpu[HIST]; static int hpos=0; static u64 hlast=0; static u64 pt[NCPU], pi[NCPU]; static int cpu_now=0, cpu_cores=0;
static u32 INK_=0, SUB=0, LINE=0, CARD=0, BG_=0;
static void colours(void){ INK_=rgb(26,30,42); SUB=rgb(110,118,138); LINE=rgb(230,232,238); CARD=rgb(255,255,255); BG_=rgb(243,244,248); }
static int mem_pct(void){ u32 tot=g_ram_mb?g_ram_mb:2048; u64 used=(u64)heap_used()/1048576ULL+64; int p=(int)(used*100/tot); return p>100?100:p; }
static int gpu_pct(void){ if(!g_gfx||!g_gfx_frame_us)return 0; u32 f=1000000u/(u32)(g_refresh_hz>0?g_refresh_hz:60); int p=(int)((g_gfx_gpu_us+g_gfx_pres_us)*100u/f); return p>100?100:p; }
static void sample(void){
    if(hlast&&g_ticks-hlast<100)return; hlast=g_ticks;
    u64 dt=0, di=0; int n=0;
    for(int i=1;i<g_ncpu&&i<NCPU;i++){ struct pcpu* c=&g_cpu[i]; if(!c->online)continue; u64 t=c->ticks, id=c->idle_ticks; if(pt[i]){ dt+=t-pt[i]; di+=id-pi[i]; n++; } pt[i]=t; pi[i]=id; }
    cpu_cores=n; cpu_now=(dt&&n)?(int)(100-di*100/dt):0; if(cpu_now<0)cpu_now=0;
    hcpu[hpos]=(u8)cpu_now; hmem[hpos]=(u8)mem_pct(); hgpu[hpos]=(u8)gpu_pct(); hpos=(hpos+1)%HIST; }
static void graph(int x,int y,int w,int h,const u8* d,u32 col){
    fill_rect(x,y,w,h,CARD); for(int k=1;k<4;k++)fill_rect(x,y+h*k/4,w,1,rgb(240,242,246)); for(int k=1;k<6;k++)fill_rect(x+w*k/6,y,1,h,rgb(240,242,246));
    fill_rect(x,y,w,1,LINE); fill_rect(x,y+h-1,w,1,LINE); fill_rect(x,y,1,h,LINE); fill_rect(x+w-1,y,1,h,LINE);
    int px=-1,py=-1; for(int k=0;k<HIST;k++){ int v=d[(hpos+k)%HIST]; int gx=x+1+(w-2)*k/(HIST-1), gy=y+h-2-(h-4)*v/100;
        fill_rect(gx,gy,(w-2)/(HIST-1)+1,y+h-1-gy,mix(CARD,col,40)); if(px>=0) line_bl(px,py,gx,gy,col,255,2); px=gx; py=gy; } }
static char* pct(char* p,int v){ p=udec(p,(u32)v); *p++='%'; *p=0; return p; }
static char* mb(char* p,u64 m){ if(m>=1024){ p=udec(p,(u32)(m/1024)); *p++='.'; p=udec(p,(u32)((m%1024)*10/1024)); return scopy(p," GB"); } p=udec(p,(u32)m); return scopy(p," MB"); }
static int app_kb(int a){ switch(a){ case W_BROWSER:return 149504; case W_DOOM:return 33792; case W_PAINT:return 18432; case W_TERM:return 6144; case W_FILES:return 9216; case W_VIDEO:return 24576; default:return 4096; } }

static const char* RAIL[4]={"Processes","Performance","Devices","Services"};
static void rail_icon(int x,int y,int k,u32 c){
    if(k==0){ fill_rect(x,y,16,3,c); fill_rect(x,y+6,16,3,c); fill_rect(x,y+12,16,3,c); }
    else if(k==1){ for(int q=0;q<4;q++)fill_rect(x+q*4,y+14-q*4-2,3,q*4+3,c); }
    else if(k==2){ fill_round(x,y,16,16,3,15,c); fill_rect(x+4,y+4,8,8,CARD); fill_rect(x+6,y+6,4,4,c); for(int q=0;q<3;q++){ fill_rect(x-2,y+3+q*4,2,2,c); fill_rect(x+16,y+3+q*4,2,2,c); } }
    else { fill_circle(x+8,y+8,7,c); fill_circle(x+8,y+8,3,CARD); } }

static void processes(int x,int y,int w,int h){
    dstr(x,y,"Processes",INK_,2); y+=40;
    int cw=w-24; int cCPU=x+cw-300, cMEM=x+cw-200, cST=x+cw-100;
    dstr(x+40,y,"Name",SUB,1); dstr(cST,y,"Status",SUB,1); dstr(cCPU,y,"CPU",SUB,1); dstr(cMEM,y,"Memory",SUB,1); y+=22; fill_rect(x,y,cw,1,LINE); y+=8;
    int napp=0; for(int a=0;a<NWIN;a++) if(WN[a].open)napp++;
    { char b[24],*p=b; p=scopy(p,"Apps ("); p=udec(p,(u32)napp); *p++=')'; *p=0; dstr(x,y,b,SUB,1); } y+=24;
    for(int a=0;a<NWIN&&y<WN[W_SYSMON].y+WN[W_SYSMON].h-140;a++){ if(!WN[a].open)continue; int sel=(a==tmsel);
        if(sel) fill_round(x-6,y-6,cw+6,30,6,15,mix(CARD,ACC,40)); else if(in_rect(mx,my,x-6,y-6,cw+6,30)) fill_round(x-6,y-6,cw+6,30,6,15,rgb(246,247,250));
        { u32 ic=mix(ACC,rgb((a*53)&255,(a*97)&255,(a*31)&255),70); fill_round(x+2,y-3,22,22,6,15,ic); char t2[2]={APPNAME[a][0],0}; dstr_ctr(x+13,y+1,t2,rgb(255,255,255),1); } dstr(x+40,y+2,APPNAME[a],INK_,1);
        dstr(cST,y+2,WN[a].min?"Suspended":(a==focus?"Active":"Running"),WN[a].min?SUB:rgb(40,140,80),1);
        char b[24],*p=b; pct(p,a==focus?(cpu_now>3?cpu_now/2:2):0); dstr(cCPU,y+2,b,INK_,1);
        p=b; p=mb(p,(u64)app_kb(a)/1024); *p=0; dstr(cMEM,y+2,b,INK_,1);
        hit(x-6,y-6,cw+6,30,300+a); y+=32; }
    y+=6; dstr(x,y,"Background (kernel services)",SUB,1); y+=24;
    static const char* BGN[6]={"Window manager and compositor","GPU draw queue","Network stack (TCP/IP, TLS)","File system (NVXFS)","USB and input","Scheduler"};
    for(int k=0;k<6&&y<WN[W_SYSMON].y+WN[W_SYSMON].h-70;k++){ fill_round(x,y-2,16,16,3,15,rgb(150,160,182)); dstr(x+40,y+2,BGN[k],INK_,1); dstr(cST,y+2,"Running",rgb(40,140,80),1); y+=28; }
    int by=WN[W_SYSMON].y+WN[W_SYSMON].h-46; int en=(tmsel>=0&&tmsel<NWIN&&WN[tmsel].open);
    fill_round(x+cw-120,by,120,30,8,15,en?ACC:rgb(232,234,240)); dstr_ctr(x+cw-60,by+8,"End task",en?rgb(255,255,255):SUB,1); hit(x+cw-120,by,120,30,399); }

static void perf_tile(int x,int y,int w,int k,const char* name,const char* val,const u8* d,u32 col){
    int on=(tmperf==k); fill_round(x,y,w,64,8,15,on?mix(CARD,ACC,30):CARD);
    if(d){ int gx=x+8, gw=70, gh=44, gy=y+10; fill_rect(gx,gy,gw,gh,rgb(250,251,253)); int px=-1,py=-1; for(int q=0;q<HIST;q+=3){ int v=d[(hpos+q)%HIST]; int xx=gx+gw*q/HIST, yy=gy+gh-1-(gh-2)*v/100; if(px>=0)line_bl(px,py,xx,yy,col,255,1); px=xx; py=yy; } }
    else fill_round(x+8,y+10,70,44,6,15,mix(CARD,col,40));
    dstr(x+90,y+14,name,INK_,1); dstr(x+90,y+36,val,SUB,1); hit(x,y,w,64,200+k); }
static void row2(int x,int y,const char* a,const char* b){ dstr(x,y,a,SUB,1); dstr(x+150,y,b,INK_,1); }
static void performance(int x,int y,int w,int h){
    dstr(x,y,"Performance",INK_,2); y+=44;
    const char* nm; u32 did=0,ss=0,eu=0,mhz=0,mxz=0,st=0,tf=0; int hg=igpu_info(&nm,&did,&ss,&eu,&mhz,&mxz,&st,&tf);
    char v[5][48]; char* p;
    p=v[0]; p=pct(p,cpu_now); p=scopy(p,"  "); p=udec(p,(u32)g_ncpu); p=scopy(p," cores"); *p=0;
    p=v[1]; p=mb(p,(u64)heap_used()/1048576ULL+64); *p++='/'; p=mb(p,g_ram_mb); *p=0;
    p=v[2]; if(hg){ p=pct(p,gpu_pct()); p=scopy(p,"  "); p=udec(p,mhz); p=scopy(p," MHz"); } else p=scopy(p,"no driver"); *p=0;
    p=v[3]; { int n=gdisk_count(); if(n){ p=mb(p,(u64)gdisk_size_of(gdisk_current())*512ULL/1048576ULL); } else p=scopy(p,"no disk"); } *p=0;
    p=v[4]; p=scopy(p,(net_ok||g_wnd_up)?"Connected":"Not connected"); *p=0;
    static const char* TN[5]={"CPU","Memory","GPU","Disk","Network"}; u32 TC[5]={rgb(30,130,230),rgb(140,90,220),rgb(40,170,120),rgb(60,170,90),rgb(200,120,40)};
    int lw=230; for(int k=0;k<5;k++) perf_tile(x,y+k*72,lw,k,TN[k],v[k],k==0?hcpu:k==1?hmem:k==2?hgpu:0,TC[k]);
    int gx=x+lw+20, gw=w-lw-44, gy=y; if(gw<200)return;
    dstr(gx,gy,TN[tmperf],INK_,2);
    const char* sub2=tmperf==0?g_brand:tmperf==2&&hg?nm:tmperf==3&&gdisk_count()?gdisk_name_of(gdisk_current()):"";
    { char b[48]; int n=0; while(sub2[n]&&n<46){ b[n]=sub2[n]; n++; } b[n]=0; dstr(gx+gw-slen(b)*8,gy+6,b,SUB,1); }
    gy+=36; int gh=220;
    if(tmperf<3){ dstr(gx,gy,tmperf==0?"% utilisation over 60 seconds (cores 1 and up)":tmperf==1?"Memory in use over 60 seconds":"GPU busy per frame over 60 seconds",SUB,1); gy+=20; graph(gx,gy,gw,gh,tmperf==0?hcpu:tmperf==1?hmem:hgpu,TC[tmperf]); gy+=gh+18; }
    char b[64];
    if(tmperf==0){ p=b; p=pct(p,cpu_now); *p=0; row2(gx,gy,"Utilisation",b); gy+=22; p=b; p=udec(p,(u32)g_ncpu); p=scopy(p," ("); p=udec(p,(u32)g_cores_online); p=scopy(p," online)"); *p=0; row2(gx,gy,"Cores",b); gy+=22;
        for(int i=0;i<g_ncpu&&i<NCPU&&i<8;i++){ struct pcpu* c=&g_cpu[i]; p=b; p=scopy(p,"Core "); p=udec(p,(u32)i); *p=0; row2(gx,gy,b,i==0?"desktop, drivers and the shell":c->online?(c->parked?"parked":c->js_running?"running a program":"idle"):"offline"); gy+=20; } }
    else if(tmperf==1){ p=b; p=mb(p,g_ram_mb); *p=0; row2(gx,gy,"Installed",b); gy+=22; p=b; p=mb(p,(u64)heap_used()/1048576ULL); *p=0; row2(gx,gy,"Kernel heap in use",b); gy+=22; p=b; p=udec(p,g_gfx_layer_mb); p=scopy(p," MB in "); p=udec(p,g_gfx_layers); p=scopy(p," window layers"); *p=0; row2(gx,gy,"Graphics buffers",b); }
    else if(tmperf==2){ if(!hg){ row2(gx,gy,"Driver","no Intel graphics driver on this machine"); return; }
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
        for(int k=0;k<pci_n;k++) if(pci_cls[k]==0x03&&!(pci_ven[k]==0x8086&&igpu_info(&nm,&did,&ss,&eu,&mhz,&mxz,&st,&tf))){ p=D("Display controller",OFF); p=scopy(p,vname(pci_ven[k])); p=scopy(p,"  "); p=hx(p,pci_ven[k],4); *p++=':'; p=hx(p,pci_dev[k],4); p=scopy(p,"  firmware framebuffer only"); *p=0; } }
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
        p=D("Drawing",OK); p=scopy(p,g_gfx==GFX_GPU?"Intel GPU":g_gfx==GFX_EMU?"CPU (GPU emulated)":"CPU"); *p=0; }
    else if(cat==9){ p=D("Keyboard",OK); scopy(p,"Built-in keyboard, PS/2 controller")[0]=0;
        p=D("Touchpad",OK); scopy(p,g_tp.ok?"Precision touchpad, HID over I2C":g_syn_ready?"Synaptics touchpad, PS/2":g_etp_ready?"ELAN touchpad, PS/2":"PS/2 pointing device")[0]=0;
        for(int k=0;k<g_usbdev_n;k++) if(g_usbdev[k].used&&g_usbdev[k].hid){ p=D(g_usbdev[k].kind[0]?g_usbdev[k].kind:"USB input",OK); p=scopy(p,g_usbdev[k].name[0]?g_usbdev[k].name:"USB HID device"); *p=0; } }
    else { for(int k=0;k<pci_n;k++){ p=D(pcls(pci_cls[k],pci_sub[k]),OK); p=scopy(p,vname(pci_ven[k])); p=scopy(p,"  "); p=hx(p,pci_ven[k],4); *p++=':'; p=hx(p,pci_dev[k],4); p=scopy(p,"  bus "); p=udec(p,pci_bus[k]); p=scopy(p," slot "); p=udec(p,pci_slot[k]); *p=0; } } }
static const char* DCAT[11]={"Processor","Graphics","Memory","Power supply and battery","Storage","Network","Sound","USB","Display","Input","All PCI devices"};
static void dicon(int x,int y,int k){ static const u32 IC[11]={rgb(30,130,230),rgb(40,170,120),rgb(140,90,220),rgb(70,180,100),rgb(236,150,50),rgb(60,130,230),rgb(220,80,120),rgb(90,100,120),rgb(40,170,170),rgb(120,128,146),rgb(52,58,76)};
    fill_round(x,y,26,26,7,15,IC[k]); static const char G1[11]={'C','G','M','P','S','N','A','U','D','I','*'}; char t[2]={G1[k],0}; dstr_ctr(x+13,y+6,t,rgb(255,255,255),1); }
static int dscroll=0, dmaxs=0;
static void devices(int x,int y,int w,int h){
    dstr(x,y,"Devices",INK_,2); dstr(x+150,y+10,"every component this computer reports",SUB,1); y+=44; int cw=w-24; int ymax=WN[W_SYSMON].y+WN[W_SYSMON].h-12; int top=y; (void)h;
    if(dscroll>dmaxs)dscroll=dmaxs; if(dscroll<0)dscroll=0; y-=dscroll; int y0=y;
    for(int k=0;k<11;k++){ int on=(tmdev==k); devlist(k); int cnt=dn_;
        if(y>=top&&y+40<=ymax){ fill_round(x,y,cw,40,8,15,on?mix(CARD,ACC,24):(in_rect(mx,my,x,y,cw,40)?rgb(248,249,252):CARD)); dicon(x+10,y+7,k); dstr(x+48,y+12,DCAT[k],INK_,1);
            { char b[16],*p=b; p=udec(p,(u32)cnt); *p=0; dstr(x+cw-60,y+12,b,SUB,1); } dstr(x+cw-24,y+12,on?"v":">",SUB,1); hit(x,y,cw,40,100+k); }
        y+=44;
        if(on){ for(int r=0;r<dn_;r++){ if(y>=top&&y+20<=ymax){ fill_circle(x+30,y+9,4,dc_[r]); dstr(x+48,y+2,dt_[r],INK_,1); dstr(x+290,y+2,dd_[r],SUB,1); } y+=22; } y+=8; } }
    dmaxs=(y-y0)-(ymax-top); if(dmaxs<0)dmaxs=0;
    if(dmaxs>0){ int th=ymax-top; int bh=th*th/(th+dmaxs); int by=top+(th-bh)*dscroll/(dmaxs?dmaxs:1); fill_round(x+cw+6,by,4,bh,2,15,rgb(190,196,208)); } }
void taskmgr_wheel(int sc){ if(tmt==2){ dscroll-=sc*44; if(dscroll<0)dscroll=0; if(dscroll>dmaxs)dscroll=dmaxs; } }
static void services(int x,int y,int w,int h){
    dstr(x,y,"Services",INK_,2); y+=44; (void)w; (void)h;
    struct { const char* n; const char* d; int on; } S[10]={{"compositor","windows, shadows, glass, animations",1},{"gpu-draw","Intel GPU draw queue",g_gfx==GFX_GPU},{"netstack","TCP/IP, DHCP, DNS, TLS",net_ok||g_wnd_up},{"wifi","Realtek RTL8821CE driver",g_wnd_up},{"nvxfs","disk file system",1},{"usb","xHCI host, keyboard, mouse, storage",xhci_present},{"audio","HD Audio / AC97 output",g_snd_state},{"acpi","power, battery, backlight",1},{"smp","multi-core scheduler",g_cores_online>1},{"shell","nsh command shell",1}};
    for(int k=0;k<10;k++){ fill_circle(x+8,y+8,5,S[k].on?rgb(40,140,80):rgb(170,176,190)); dstr(x+24,y+2,S[k].n,INK_,1); dstr(x+150,y+2,S[k].d,SUB,1); dstr(x+480,y+2,S[k].on?"Running":"Stopped",S[k].on?rgb(40,140,80):SUB,1); y+=28; } }
void sysmon_draw(int i){
    win_frame(i,"Task Manager"); colours(); hn_=0; sample();
    int wx=WN[i].x, wy=WN[i].y+TITLEH, ww=WN[i].w, wh=WN[i].h-TITLEH; int rw=ww>=760?180:56;
    fill_rect(wx+1,wy,ww-2,wh-1,BG_); fill_rect(wx+1,wy,rw,wh-1,rgb(236,238,243)); fill_rect(wx+rw,wy,1,wh-1,LINE);
    for(int k=0;k<4;k++){ int ry=wy+14+k*46; int on=(k==tmt); if(on){ fill_round(wx+8,ry,rw-16,38,8,15,CARD); fill_rect(wx+8,ry+10,3,18,ACC); } else if(in_rect(mx,my,wx+8,ry,rw-16,38)) fill_round(wx+8,ry,rw-16,38,8,15,rgb(244,245,249));
        rail_icon(wx+22,ry+11,k,on?ACC:SUB); if(rw>100) dstr(wx+50,ry+12,RAIL[k],on?INK_:SUB,1); hit(wx+8,ry,rw-16,38,10+k); }
    int cx=wx+rw+22, cy=wy+16, cw=ww-rw-30;
    if(tmt==0)processes(cx,cy,cw,wh); else if(tmt==1)performance(cx,cy,cw,wh); else if(tmt==2)devices(cx,cy,cw,wh); else services(cx,cy,cw,wh); }
void sysmon_click(int px,int py){
    for(int k=hn_-1;k>=0;k--) if(in_rect(px,py,hx_[k],hy_[k],hw_[k],hh_[k])){ int id=hid_[k];
        if(id>=10&&id<14){ tmt=id-10; return; }
        if(id>=100&&id<111){ tmdev=(tmdev==id-100)?-1:id-100; dscroll=0; return; }
        if(id>=200&&id<205){ tmperf=id-200; return; }
        if(id>=300&&id<300+NWIN){ tmsel=id-300; return; }
        if(id==399){ if(tmsel>=0&&tmsel<NWIN&&WN[tmsel].open){ close_win(tmsel); tmsel=-1; } return; }
        return; } }
void taskmgr_tab(int t,int dev){ if(t>=0&&t<4)tmt=t; if(dev>=0&&dev<11)tmdev=dev; }
