#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/sysfeat.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "drivers/video/amd/amd.h"
#include "drivers/video/amd/fw/amd_fw.h"
#include "drivers/video/amd/sdma/sdma_core.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/drivers/video/fb/damage.h"
#include "drivers/video/amd/sdma/sdma_gart.h"
#include "drivers/video/amd/psp/psp_core.h"

int amd_vram_scratch(u64* mc,volatile u8** cpu);
u64 amd_vram_scratch_len(void);
int g_amdsdma_live=0;
static asd_t SE; static u8* SFW; static u64 s_mc0; static u8* s_cpu0; static u32 s_fails, s_copies, s_fills, s_fallback; static char s_why[160];
int g_amdsdma_sys=0; static agt_t SG; static u64 s_wva, s_wlen, s_wpa0, s_wpa1; static u32 s_frames, s_sysfills, s_remaps; static int s_psp; static char s_gwhy[96];
static aps_t SP; static char s_pwhy[96], s_pnote[96]; static int s_pload;

static u32 sd_rd(void* c,u32 dw){ (void)c; return amd_rreg(dw); }
static void sd_wr(void* c,u32 dw,u32 v){ (void)c; amd_wreg(dw,v); }
static u64 sd_ns(void* c){ (void)c; return hr_ns(); }
static const asd_io_t sd_io={sd_rd,sd_wr,sd_ns,0};

static void sd_why(const char* w){ int n=0; while(w&&w[n]&&n<159){ s_why[n]=w[n]; n++; } s_why[n]=0; }
static void sd_gwhy(const char* w){ int n=0; while(w&&w[n]&&n<95){ s_gwhy[n]=w[n]; n++; } s_gwhy[n]=0; }
static int sd_gno(const char* w){ sd_gwhy(w); char b[160],*p=b; p=scopy(p,"amdgpu: SDMA system-memory copies off - "); p=scopy(p,w); *p=0; klog(b); return -1; }
static int sd_psp(void){ int seg=amd_ipver(AMD_IP_MP0)==0x0D0004u?1:0; if(!g_amd.ip[AMD_IP_MP0].base[seg])return 0; u32 x=amd_rreg(amd_ipreg(AMD_IP_MP0,seg,0x91)); return x&&x!=0xFFFFFFFFu; }
static int sd_no(const char* w,int r){ sd_why(w); char b[200],*p=b; p=scopy(p,"amdgpu: SDMA off - "); p=scopy(p,w); *p=0; klog(b); return r; }

static void sd_cp(char* d,const char* w){ int n=0; while(w&&w[n]&&n<95){ d[n]=w[n]; n++; } d[n]=0; }
static int sd_pfail(const char* w){ sd_cp(s_pwhy,w); char b[160],*p=b; p=scopy(p,"amdgpu: PSP firmware load off - "); p=scopy(p,w); if(SP.last){ p=scopy(p," (status 0x"); p=uhex(p,SP.last,8); p=scopy(p,")"); } *p=0; klog(b); return -1; }
static void sd_pnote(const char* w,u32 st){ char* p=s_pnote; p=scopy(p,w); if(st){ p=scopy(p," 0x"); p=uhex(p,st,8); } *p=0; }

static int sd_psp_load(const asd_gen_t* g,const asd_fw_t* f){
    s_pwhy[0]=0; SP.last=0;
    const aps_gen_t* pg=aps_gen(amd_ipver(AMD_IP_MP0)); if(!pg)return sd_pfail("this PSP version is not supported");
    u32 base=g_amd.ip[AMD_IP_MP0].base[pg->seg]; if(!base)return sd_pfail("PSP register block not found");
    u64 sl=amd_vram_scratch_len(); if(sl<0x30000+APS_AREA)return sd_pfail("the video memory slot is too small for the PSP ring");
    u8* a=s_cpu0+0x30000; if(!acpi_hi_map((u64)a)||!acpi_hi_map((u64)a+APS_AREA-1))return sd_pfail("the PSP ring memory cannot be mapped");
    if(!SP.g){ SP=(aps_t){0}; SP.io=&sd_io; SP.g=pg; SP.base=base; SP.mem=(volatile u8*)a; SP.mc=s_mc0+0x30000; }
    if(aps_ring_up(&SP,100000))return sd_pfail(SP.why);
    if(!SP.tmr){ u32 sz=APS_TMR_SZ; u8* tf=0; u32 tn=0; char path[96],*p=path; p=scopy(p,"/lib/firmware/"); p=scopy(p,pg->toc); *p=0;
        if(amd_fw_read&&amd_fw_read(path,&tf,&tn)>=0&&tf){ const u8* td; u32 ts, ns=0;
            if(!aps_toc_file(tf,tn,&td,&ts)&&!aps_toc(&SP,td,ts,&ns,2000000)&&ns>=0x1000&&ns<=0x4000000)sz=(ns+0xFFFu)&~0xFFFu; }
        if(tf)kfree(tf);
        u64 tm=(SP.mc+APS_AREA+APS_TMR_AL-1)&~(u64)(APS_TMR_AL-1); const agt_gen_t* ag=agt_gen(amd_ipver(AMD_IP_GC)); u32 gs=g_amd.ip[AMD_IP_GC].base[0];
        if(tm+sz>s_mc0+sl)sd_pnote("no room for a driver TMR, using the firmware TMR",0);
        else if(!ag||!gs)sd_pnote("graphics memory window unknown, using the firmware TMR",0);
        else { u64 fb=(u64)(amd_rreg(gs+ag->fbb)&0xFFFFFFu)<<24, fo=(u64)(amd_rreg(gs+ag->fbo)&0xFFFFFFu)<<24;
            if(tm<fb)sd_pnote("TMR below the memory window, using the firmware TMR",0);
            else { int r=aps_tmr(&SP,tm,tm-fb+fo,sz,2000000);
                if(r<0){ aps_ring_down(&SP,100000); return sd_pfail(SP.why); }
                if(r)sd_pnote("TMR setup answered status",SP.last); else { char* q=s_pnote; q=scopy(q,"driver TMR at 0x"); q=uhex(q,tm,10); q=scopy(q," size 0x"); q=uhex(q,sz,8); *q=0; } } } }
    int r=aps_sdma(&SP,g,f,2000000);
    if(r<0){ aps_ring_down(&SP,100000); return sd_pfail(SP.why); }
    if(r)sd_pnote("SDMA load answered status",SP.last);
    char b[200],*p=b; p=scopy(p,"amdgpu: PSP loaded SDMA firmware - "); p=scopy(p,pg->name); if(s_pnote[0]){ p=scopy(p,", "); p=scopy(p,s_pnote); } *p=0; klog(b);
    return 0; }

int amdsdma_hw(void){ return g_amd.found&&g_amd.mmio&&g_amd.disc_ok&&asd_gen(amd_ipver(AMD_IP_SDMA0))!=0; }

static int sd_win(const void* v,u64 len,u64* mc){
    u64 va=(u64)v, b=va&~0xFFFULL, e=(va+len+0xFFFULL)&~0xFFFULL; u32 n=(u32)((e-b)>>12);
    if(!g_amdsdma_sys||!len||n>SG.npte)return -1;
    if(s_wva&&b>=s_wva&&e<=s_wva+s_wlen&&kva_to_pa(s_wva)==s_wpa0&&kva_to_pa(s_wva+s_wlen-4096)==s_wpa1){ *mc=SG.start+(va-s_wva); return 0; }
    s_wva=0; static u64 pa[256];
    for(u32 i=0;i<n;){ u32 k=n-i>256?256:n-i; for(u32 j=0;j<k;j++){ u64 x=kva_to_pa(b+((u64)(i+j)<<12)); if(!x||(x&0xFFFULL))return -1; pa[j]=x; }
        if(agt_map(&SG,i,pa,k,100000))return -1;
        i+=k; }
    s_wva=b; s_wlen=e-b; s_wpa0=kva_to_pa(b); s_wpa1=kva_to_pa(e-4096); s_remaps++; *mc=SG.start+(va-b); return 0; }

static void sd_gart_off(void){ if(SG.up)agt_disable(&SG,100000); g_amdsdma_sys=0; s_wva=0; }

static int sd_gart_on(void){
    g_amdsdma_sys=0; s_wva=0; const agt_gen_t* g=agt_gen(amd_ipver(AMD_IP_GC)); if(!g)return sd_gno("no GART layout for this graphics version");
    u8* a=s_cpu0+0x20000; if(!acpi_hi_map((u64)a)||!acpi_hi_map((u64)a+0xFFFF))return sd_gno("the page table memory cannot be mapped");
    SG=(agt_t){0}; SG.io=&sd_io; SG.g=g; SG.seg0=g_amd.ip[AMD_IP_GC].base[0]; SG.pt=(volatile u64*)a; SG.pt_mc=s_mc0+0x20000; SG.npte=0x10000/8;
    if(agt_place(&SG)||agt_enable(&SG,100000))return sd_gno(SG.why);
    u8* m=(u8*)kmalloc(3*4096); if(!m){ sd_gart_off(); return sd_gno("no memory for the self-test page"); }
    u32* t=(u32*)(((u64)m+0xFFFULL)&~0xFFFULL); for(u32 i=0;i<2048;i++)t[i]=0;
    g_amdsdma_sys=1; u64 mc; const char* bad=0; volatile u32* vb=SE.mem+(ASD_TEST_OFF+0x4000)/4;
    if(sd_win(t,8192,&mc))bad="the self-test page could not be mapped";
    else if(asd_fill_rect(&SE,mc+64,1024,200,8,0x5EC0FFEEu,100000))bad="the fill into system memory timed out";
    else if(t[15]!=0||t[16]!=0x5EC0FFEEu||t[16+256*7+199]!=0x5EC0FFEEu||t[16+256*7+200]!=0)bad="the fill into system memory did not arrive";
    else { for(u32 i=0;i<1024;i++){ t[1024+i]=i*0x9E3779B1u; vb[i]=0; }
        if(asd_copy_rect(&SE,mc+4096,256,SE.mc+ASD_TEST_OFF+0x4000,256,64,16,100000))bad="the copy from system memory timed out";
        else for(u32 i=0;i<1024;i++)if(vb[i]!=i*0x9E3779B1u){ bad="the copy from system memory read back wrong data"; break; } }
    s_wva=0; kfree(m);
    if(bad){ sd_gart_off(); return sd_gno(bad); }
    s_gwhy[0]=0; char b[160],*p=b; p=scopy(p,"amdgpu: SDMA system-memory copies on - GART 0x"); p=uhex(p,SG.start,12); p=scopy(p," table 0x"); p=uhex(p,SG.pt_mc,10); *p=0; klog(b);
    return 0; }

static int amdsdma_on(void){
    if(g_amdsdma_live)return 0;
    if(!amdsdma_hw())return sd_no("this SDMA version is not supported",-1);
    const asd_gen_t* g=asd_gen(amd_ipver(AMD_IP_SDMA0));
    u64 mc; volatile u8* cpu; if(amd_vram_scratch(&mc,&cpu))return sd_no("no free video memory slot",-1);
    u8* a=(u8*)cpu+0x10000; if(!acpi_hi_map((u64)a)||!acpi_hi_map((u64)a+ASD_AREA-1))return sd_no("the ring memory cannot be mapped",-1);
    volatile u32* t=(volatile u32*)a; t[0]=0x5D3A11CEu; t[ASD_AREA/4-1]=0xEC11A3D5u; if(t[0]!=0x5D3A11CEu||t[ASD_AREA/4-1]!=0xEC11A3D5u)return sd_no("the ring memory did not keep data",-1);
    if(!SFW){ char path[96],*p=path; p=scopy(p,"/lib/firmware/"); p=scopy(p,g->fw); *p=0; u32 n=0;
        if(!amd_fw_read||amd_fw_read(path,&SFW,&n)<0||!SFW){ SFW=0; return sd_no("firmware file missing in /lib/firmware",-1); }
        asd_fw_t f; if(asd_fw_parse(&f,SFW,n)){ kfree(SFW); SFW=0; return sd_no(f.why,-1); }
        SE.g=g; SE.io=&sd_io; SE.seg0=g_amd.ip[AMD_IP_GC].base[0]; SE.seg1=g_amd.ip[AMD_IP_GC].base[1]; SE.mem=(volatile u32*)a; SE.mc=mc+0x10000; s_mc0=mc; s_cpu0=(u8*)cpu;
        static asd_fw_t F; F=f; SE.c_fw=&F; }
    if(!SE.seg0||!SE.seg1)return sd_no("graphics register block not found",-1);
    s_psp=sd_psp(); SE.psp=0; s_pload=0;
    if(s_psp&&!sd_psp_load(g,(const asd_fw_t*)SE.c_fw)){ SE.psp=1; s_pload=1; }
    int r=asd_start(&SE,(const asd_fw_t*)SE.c_fw);
    if(r==-1)return sd_no("the firmware does not match this SDMA layout",-1);
    if(r==-2)return sd_no("SDMA registers read all ones",-2);
    if(r){ if(SE.psp)return sd_no("the SDMA engine did not answer after the PSP loaded its firmware",-2);
        if(s_psp){ char w[160],*p=w; p=scopy(p,"PSP firmware load failed ("); p=scopy(p,s_pwhy); p=scopy(p,") and the direct load was ignored"); *p=0; return sd_no(w,-2); }
        return sd_no("the SDMA engine did not answer the ring test",-2); }
    volatile u32* ta=SE.mem+ASD_TEST_OFF/4; volatile u32* tb=SE.mem+(ASD_TEST_OFF+0x4000)/4;
    for(u32 i=0;i<1024;i++){ ta[i]=0; tb[i]=0xFFFFFFFFu; }
    if(asd_fill_rect(&SE,SE.mc+ASD_TEST_OFF,256,64,16,0xC0FFEE11u,100000)||ta[0]!=0xC0FFEE11u||ta[1023]!=0xC0FFEE11u){ asd_stop(&SE); return sd_no("the fill self-test failed",-2); }
    for(u32 i=0;i<1024;i++)ta[i]=i*2654435761u;
    if(asd_copy_rect(&SE,SE.mc+ASD_TEST_OFF,256,SE.mc+ASD_TEST_OFF+0x4000,256,64,16,100000)){ asd_stop(&SE); return sd_no("the copy self-test timed out",-2); }
    for(u32 i=0;i<1024;i++)if(tb[i]!=i*2654435761u){ asd_stop(&SE); return sd_no("the copy self-test read back wrong data",-2); }
    g_amdsdma_live=1; s_fails=0; s_why[0]=0;
    char b[160],*p=b; p=scopy(p,"amdgpu: SDMA on - "); p=scopy(p,g->name); p=scopy(p," ring 0x"); p=uhex(p,SE.mc,10); if(s_pload)p=scopy(p," (firmware loaded by the PSP)"); else if(s_psp)p=scopy(p," (direct load accepted with PSP running)"); *p=0; klog(b);
    sd_gart_on();
    return 0; }

void amdsdma_off(const char* why){ if(!g_amdsdma_live)return; sd_gart_off(); asd_stop(&SE); g_amdsdma_live=0; sd_no(why?why:"turned off",0); }

static int sd_mc(const void* cpu,u64* mc){ i64 d=(i64)((u64)cpu-(u64)s_cpu0); if(!s_cpu0||(u64)cpu<(u64)FBB)return -1; *mc=s_mc0+(u64)d; return 0; }
static int sd_res(int r){ if(!r){ s_fails=0; return 0; } s_fallback++; if(r==-2||++s_fails>=3){ amdsdma_off(r==-2?"registers stopped answering":"three operations in a row did not finish"); sysfeat_fail(SYSF_AMD_SDMA,s_why); } return -1; }

int amdsdma_copy(const void* src,u32 spb,void* dst,u32 dpb,u32 w,u32 h){
    if(!g_amdsdma_live)return -1; u64 s,d; if(sd_mc(src,&s)||sd_mc(dst,&d))return -1;
    int r=asd_copy_rect(&SE,s,spb,d,dpb,w,h,100000); if(!r)s_copies++; return sd_res(r); }

int amdsdma_fill(void* dst,u32 dpb,u32 w,u32 h,u32 v){
    if(!g_amdsdma_live)return -1; u64 d; if(sd_mc(dst,&d))return -1;
    int r=asd_fill_rect(&SE,d,dpb,w,h,v,100000); if(!r)s_fills++; return sd_res(r); }

int amdsdma_present_rects(const u32* sc,u32 sw,u32 w,u32 h,const dmg_rect* r,int n,u8* fb,u32 pitchb){
    if(!g_amdsdma_live||!g_amdsdma_sys||n<=0||!sc||!fb||sw<w||(pitchb&3)||pitchb/4<w)return 0;
    u64 s,d; if(sd_win(sc,(u64)sw*h*4,&s)||sd_mc(fb,&d))return 0;
    for(int i=0;i<n;i++){ int x=r[i].x,y=r[i].y,rw=r[i].w,rh=r[i].h; if(x<0||y<0||rw<=0||rh<=0||(u32)x+(u32)rw>w||(u32)y+(u32)rh>h||rw>16384||rh>16384){ s_fallback++; return 0; } }
    u32 pk[16]; int q=0, rr=0;
    for(int i=0;i<n;i++){ int x=r[i].x,y=r[i].y,rw=r[i].w,rh=r[i].h;
        int k=asd_pk_subwin(pk,s+((u64)y*sw+(u32)x)*4,sw,d+(u64)y*pitchb+(u64)x*4,pitchb/4,(u32)rw,(u32)rh);
        if(asd_emit(&SE,pk,k)<0){ rr=-1; break; }
        if(++q==128&&i+1<n){ q=0; rr=asd_kick(&SE,100000); if(rr)break; } }
    if(!rr)rr=asd_kick(&SE,100000);
    if(!rr)s_frames++;
    return sd_res(rr)==0; }

int amdsdma_fill_sys(u32* base,u32 sw,u32 h,int x,int y,int w,int hh,u32 v){
    if(!g_amdsdma_live||!g_amdsdma_sys||!base||x<0||y<0||w<=0||hh<=0||(u32)x+(u32)w>sw||(u32)y+(u32)hh>h)return -1;
    u64 d; if(sd_win(base,(u64)sw*h*4,&d))return -1;
    int r=asd_fill_rect(&SE,d+((u64)y*sw+(u32)x)*4,sw*4,(u32)w,(u32)hh,v,100000); if(!r)s_sysfills++; return sd_res(r); }

const char* amdsdma_gart_why(void){ return s_gwhy; }

void amdsdma_boot(void){
    if(!amdsdma_hw())return;
    if(!sysfeat_begin(SYSF_AMD_SDMA)){ sd_why("switched off in Settings or after failures"); return; }
    int r=amdsdma_on(); if(r==-2)sysfeat_fail(SYSF_AMD_SDMA,s_why); else sysfeat_ok(SYSF_AMD_SDMA); }

int amdsdma_setting(int on){
    sysfeat_set(SYSF_AMD_SDMA,on);
    if(!on){ amdsdma_off("turned off in Settings"); return 0; }
    if(g_amdsdma_live)return 0;
    if(!sysfeat_begin(SYSF_AMD_SDMA))return -1;
    int r=amdsdma_on(); if(r==-2)sysfeat_fail(SYSF_AMD_SDMA,s_why); else sysfeat_ok(SYSF_AMD_SDMA); return r; }

const char* amdsdma_why(void){ return s_why; }
int amdsdma_psp_loaded(void){ return s_pload; }

int amdsdma_describe(char* o,int cap){
    if(!o||cap<2)return 0; char t[640],*p=t; p=scopy(p,"sdma: ");
    if(g_amdsdma_live){ p=scopy(p,"on, "); p=scopy(p,SE.g->name); p=scopy(p," copies "); p=udec(p,s_copies); p=scopy(p," fills "); p=udec(p,s_fills); p=scopy(p," cpu fallbacks "); p=udec(p,s_fallback); p=scopy(p," timeouts "); p=udec(p,SE.timeouts);
        p=scopy(p,"\n  ram frames: "); if(g_amdsdma_sys){ p=scopy(p,"on, GART 0x"); p=uhex(p,SG.start,12); p=scopy(p," frames "); p=udec(p,s_frames); p=scopy(p," fills "); p=udec(p,s_sysfills); p=scopy(p," remaps "); p=udec(p,s_remaps); } else { p=scopy(p,"off - "); p=scopy(p,s_gwhy[0]?s_gwhy:"not started"); } }
    else { p=scopy(p,"off - "); p=scopy(p,s_why[0]?s_why:amdsdma_hw()?"not started":"not supported on this GPU"); }
    if(s_psp){ p=scopy(p,"\n  psp: "); if(s_pload){ p=scopy(p,"loaded the firmware, "); p=scopy(p,SP.g?SP.g->name:"?"); if(s_pnote[0]){ p=scopy(p,", "); p=scopy(p,s_pnote); } p=scopy(p," commands "); p=udec(p,SP.cmds); }
        else { p=scopy(p,"not used - "); p=scopy(p,s_pwhy[0]?s_pwhy:"not tried"); } }
    p=scopy(p,"\n  "); p=scopy(p,sysfeat_text(SYSF_AMD_SDMA)); *p++='\n'; *p=0;
    int n=0; while(t[n]&&n<cap-1){ o[n]=t[n]; n++; } o[n]=0; return n; }
