#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/mm/pmm_vmm.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "include/nvx/arch/x86_64/hrtimer.h"
#include "include/nvx/arch/x86_64/smp_sched.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/gui/theme.h"
#include "include/nvx/drivers/video/igpu.h"
#include "include/nvx/drivers/video/gen9asm.h"
#include "include/nvx/mm/heap.h"
#include "include/nvx/drivers/video/gq.h"
#include "include/nvx/drivers/video/gpukern.h"
#include "include/nvx/drivers/video/gfx.h"

int g_fb_wc=0, g_pat_ok=0, g_igpu_flip=0; u8* g_igpu_front; u8* g_igpu_back;
static int g_wc_pages=0;
typedef struct { int found, gen, bus, dev; u16 did; u64 mmio, aper, gtt; u32 stolen_mb; const char* name;
    u32 pipesrc, htot, vtot, edp, scaler, plane_ctl, stride, surf, pf; int nat_w, nat_h, cur_w, cur_h, linear, fmt_ok, scanout_match;
    u64 back_off, front_off; u32 flips, timeouts, pend; char why[64]; u32 bench_us; int scale_on; } igpu_t;
static igpu_t G;
static volatile u32* R(u32 o){ return (volatile u32*)(G.mmio+o); }
static u32 rr(u32 o){ return *R(o); }
static void ww(u32 o,u32 v){ *R(o)=v; }

void pat_cpu_init(void){
    u32 a,b,c,d; cpu_cpuid(1,0,&a,&b,&c,&d); if(!(d&(1u<<16)))return;
    u64 pat=0x0007040600070106ULL; cpu_wrmsr(0x277,pat); g_pat_ok=1; }
static int wc_map(u64 va,u64 len){
    if(!g_pat_ok||len<(2u<<20))return 0; u64 cr3=cpu_get_pt()&~0xFFFULL; int n=0;
    for(u64 p=va&~0x1FFFFFULL;p<va+len;p+=0x200000ULL){
        u64* pml4=(u64*)cr3; u64 e4=pml4[(p>>39)&511]; if(!(e4&1))return n; u64* pdpt=(u64*)(e4&PTE_ADDR); u64 e3=pdpt[(p>>30)&511]; if(!(e3&1)||(e3&0x80))return n;
        u64* pd=(u64*)(e3&PTE_ADDR); u64* e2=&pd[(p>>21)&511]; if(!(*e2&1))return n;
        if(*e2&0x80){ *e2=(*e2&~(0x10ULL|0x1000ULL))|0x08ULL; n++; }
        else { u64* pt=(u64*)(*e2&PTE_ADDR); for(int k=0;k<512;k++){ u64 q=p+(u64)k*4096; if(q<(va&~0xFFFULL)||q>=va+len)continue; if(pt[k]&1){ pt[k]=(pt[k]&~(0x10ULL|0x80ULL))|0x08ULL; } } n++; } }
    cpu_cache_flush(); cpu_set_pt(cpu_get_pt()); smp_tlb_shootdown(); return n; }
void igpu_early(void){ pat_cpu_init(); u64 len=(u64)PITCHB*H; g_wc_pages=wc_map((u64)FBB,len); g_fb_wc=g_wc_pages>0; }
static const char* igpu_name(u16 did,int* gen){
    u8 hi=(u8)(did>>8);
    switch(hi){ case 0x59: *gen=9; return "Kaby Lake"; case 0x87: *gen=9; return "Amber Lake"; case 0x3E: *gen=9; return "Coffee/Whiskey Lake"; case 0x9B: *gen=9; return "Comet Lake";
        case 0x19: *gen=9; return "Skylake"; case 0x5A: *gen=9; return "Apollo Lake"; case 0x31: *gen=9; return "Gemini Lake"; case 0x16: *gen=8; return "Broadwell"; case 0x22: *gen=8; return "Cherry Trail";
        case 0x04: case 0x0A: case 0x0C: case 0x0D: *gen=7; return "Haswell"; case 0x01: *gen=6; return "Sandy/Ivy Bridge"; case 0x8A: *gen=11; return "Ice Lake"; case 0x4E: case 0x4F: *gen=11; return "Jasper/Elkhart Lake";
        case 0x9A: *gen=12; return "Tiger Lake"; case 0x46: *gen=12; return "Alder Lake"; case 0xA7: *gen=12; return "Raptor Lake"; case 0x7D: *gen=12; return "Meteor Lake"; }
    *gen=0; return "Intel graphics"; }
static void wcpy(char* d,const char* s,int n){ int i=0; for(;s[i]&&i<n-1;i++)d[i]=s[i]; d[i]=0; }
static void gl(const char* s){ term_puts(s); term_putc('\n'); }
void igpu_blt_init(void); void igpu_blt_info(void);
void igpu_init(void){
    for(u32 i=0;i<sizeof(G);i++)((u8*)&G)[i]=0;
    u32 id=pci_cfg(0,2,0,0); if((id&0xFFFF)!=0x8086||((pci_cfg(0,2,0,8)>>24)&0xFF)!=0x03){ wcpy(G.why,"no Intel graphics at 00:02.0",64); return; }
    G.found=1; G.did=(u16)(id>>16); G.name=igpu_name(G.did,&G.gen);
    u32 b0=pci_cfg(0,2,0,0x10), b1=pci_cfg(0,2,0,0x14), a0=pci_cfg(0,2,0,0x18), a1=pci_cfg(0,2,0,0x1C);
    G.mmio=(u64)(b0&0xFFFFFFF0u)|(((b0&6)==4)?((u64)b1<<32):0); G.aper=(u64)(a0&0xFFFFFFF0u)|(((a0&6)==4)?((u64)a1<<32):0);
    u32 ggc=pci_cfg(0,2,0,0x50); if(G.gen>=8){ u32 gms=(ggc>>8)&0xFF; G.stolen_mb=gms<0xF0?gms*32:(gms-0xF0+1)*4; } else G.stolen_mb=((ggc>>3)&0x1F)*32;
    if(!G.mmio||!acpi_hi_map(G.mmio)){ wcpy(G.why,"registers not reachable",64); G.found=2; return; }
    if(G.aper>=0x100000000ULL)acpi_hi_map(G.aper);
    G.gtt=G.mmio+0x800000ULL;
    G.pipesrc=rr(0x6001C); G.cur_w=(int)((G.pipesrc>>16)&0x1FFF)+1; G.cur_h=(int)(G.pipesrc&0x1FFF)+1;
    if(G.gen>=7){ G.edp=rr(0x6F400); }
    u32 ht=(G.edp>>31)?rr(0x6F000):rr(0x60000), vt=(G.edp>>31)?rr(0x6F00C):rr(0x6000C); G.htot=ht; G.vtot=vt; G.nat_w=(int)(ht&0x1FFF)+1; G.nat_h=(int)(vt&0x1FFF)+1;
    if(G.gen>=9){ G.scaler=rr(0x68180); G.scale_on=(G.scaler>>31)&1; } else if(G.gen>=7){ G.pf=rr(0x68080); G.scale_on=(G.pf>>31)&1; }
    G.plane_ctl=rr(0x70180); G.stride=rr(0x70188); G.surf=rr(0x7019C);
    if(G.gen>=9){ G.linear=((G.plane_ctl>>10)&7)==0; u32 f=(G.plane_ctl>>24)&0xF; G.fmt_ok=(f==4); }
    else { G.linear=!((G.plane_ctl>>10)&1); u32 f=(G.plane_ctl>>26)&0xF; G.fmt_ok=(f==6||f==7); }
    G.front_off=G.surf&~0xFFFULL; G.scanout_match=(G.aper&&(u64)FBB==G.aper+G.front_off);
    { char b[120],*p=b; p=scopy(p,"gpu: Intel "); p=scopy(p,(char*)G.name); p=scopy(p," 8086:"); p=hx(p,G.did,4); p=scopy(p," panel "); p=udec(p,(u32)G.nat_w); *p++='x'; p=udec(p,(u32)G.nat_h); p=scopy(p," mode "); p=udec(p,(u32)G.cur_w); *p++='x'; p=udec(p,(u32)G.cur_h); p=scopy(p,g_fb_wc?" fb write-combining":" fb uncached"); *p=0; klog(b); }
    if(igpu_flip_on()==0){ klog("gpu: tear-free double buffering on"); igpu_blt_init(); } else { char b[100],*p=b; p=scopy(p,"gpu: tear-free off - "); p=scopy(p,G.why); *p=0; klog(b); } }
static u32 surflive(void){ return rr(0x701AC)&~0xFFFu; }
void igpu_wait_flip(void){ if(!g_igpu_flip||!G.pend)return; u64 t0=hr_ns(); while(surflive()!=(u32)G.pend){ if(hr_ns()-t0>40000000ULL){ G.timeouts++; if(G.timeouts>=3){ igpu_flip_off("flips did not complete"); return; } break; } cpu_pause(); } G.pend=0; }
void igpu_pre_blit(void){ if(!g_igpu_flip)return; igpu_wait_flip(); if(!g_igpu_flip)return; FBB=g_igpu_back; }
void igpu_post_blit(void){ if(!g_igpu_flip)return; u64 bo=(FBB==g_igpu_back)?G.back_off:G.front_off; ww(0x7019C,(rr(0x7019C)&0xFFFu)|(u32)bo); G.pend=(u32)bo; G.flips++;
    u8* t=g_igpu_front; g_igpu_front=g_igpu_back; g_igpu_back=t; u64 to=G.front_off; G.front_off=G.back_off; G.back_off=to; FBB=g_igpu_front; }
static u64 g_orig_surf=0; static u8* g_orig_fbb=0; static u64 g_bfront0, g_bback0; extern int g_blt_ok, g_blt_present;
void igpu_flip_off(const char* why){
    if(!g_igpu_flip)return; g_igpu_flip=0; g_blt_present=0; ww(0x7019C,(rr(0x7019C)&0xFFFu)|(u32)g_orig_surf); FBB=g_orig_fbb; G.pend=0; wcpy(G.why,why,64); blit_all(); }
int igpu_flip_on(void){
    if(g_igpu_flip)return 0;
    if(G.found!=1){ wcpy(G.why,"no Intel graphics",64); return -1; }
    if(G.gen<8||G.gen>12){ wcpy(G.why,"GPU generation not supported yet",64); return -1; }
    if(!((G.plane_ctl>>31)&1)){ wcpy(G.why,"primary plane is off",64); return -1; }
    if(!G.linear||!G.fmt_ok||BYPP!=4){ wcpy(G.why,"screen format is not linear 32-bit",64); return -1; }
    if(!G.scanout_match){ wcpy(G.why,"framebuffer is not the scanned-out surface",64); return -1; }
    u64 sz=((u64)PITCHB*H+0x3FFFFULL)&~0x3FFFFULL; u64 bo=(G.front_off+sz+0x3FFFFULL)&~0x3FFFFULL; if(bo+sz>(256ULL<<20)){ wcpy(G.why,"no room in the graphics aperture",64); return -1; }
    volatile u64* gtt=(volatile u64*)G.gtt; u64 fpte=gtt[G.front_off>>12]; if(!(fpte&1)){ wcpy(G.why,"GTT entry of the screen is not valid",64); return -1; }
    u64 fl=fpte&0xFFFULL; u32 np=(u32)(sz>>12); static int built=0;
    for(u32 i=0;i<np&&!built;i++){ u64 pa=pframe_alloc(); if(!pa){ wcpy(G.why,"out of memory for the second buffer",64); return -1; } gtt[(bo>>12)+i]=(pa&PTE_ADDR)|fl; }
    built=1; (void)gtt[(bo>>12)+np-1]; ww(0x101008,1); (void)rr(0x101008);
    u8* back=(u8*)(G.aper+bo); if(G.aper+bo>=0x100000000ULL&&!acpi_hi_map(G.aper+bo)){ wcpy(G.why,"aperture not mapped",64); return -1; }
    wc_map((u64)back,sz);
    volatile u32* t=(volatile u32*)back; t[0]=0x12345678u; t[np*1024-1]=0x9ABCDEF0u; if(t[0]!=0x12345678u||t[np*1024-1]!=0x9ABCDEF0u){ wcpy(G.why,"second buffer did not keep data",64); return -1; }
    g_orig_surf=G.front_off; g_orig_fbb=FBB; g_igpu_front=FBB; g_igpu_back=back; G.back_off=bo;
    for(u32 y=0;y<H;y++){ u32* s=(u32*)(FBB+(u64)y*PITCHB); u32* d=(u32*)(back+(u64)y*PITCHB); for(u32 x=0;x<W;x++)d[x]=s[x]; }
    ww(0x7019C,(rr(0x7019C)&0xFFFu)|(u32)bo); u64 t0=hr_ns(); int ok=0; while(hr_ns()-t0<100000000ULL){ if(surflive()==(u32)bo){ ok=1; break; } cpu_pause(); }
    if(!ok){ ww(0x7019C,(rr(0x7019C)&0xFFFu)|(u32)G.front_off); wcpy(G.why,"display did not switch buffers",64); return -1; }
    g_igpu_front=back; g_igpu_back=g_orig_fbb; u64 to=G.front_off; G.front_off=bo; G.back_off=to; FBB=g_igpu_front; g_igpu_flip=1; G.why[0]=0; g_bfront0=to; g_bback0=bo; return 0; }

void gpuinfo_run(void){ char b[400],*p;
    if(!G.found){ p=b; p=scopy(p,"GPU: "); p=scopy(p,G.why[0]?G.why:"not probed"); *p=0; gl(b); }
    else { p=b; p=scopy(p,"GPU: Intel "); p=scopy(p,(char*)G.name); p=scopy(p,"  8086:"); p=hx(p,G.did,4); p=scopy(p,"  gen "); p=udec(p,(u32)G.gen); p=scopy(p,"  registers 0x"); if(G.mmio>>32)p=hx(p,(u32)(G.mmio>>32),4); p=hx(p,(u32)G.mmio,8); p=scopy(p,"  aperture 0x"); if(G.aper>>32)p=hx(p,(u32)(G.aper>>32),4); p=hx(p,(u32)G.aper,8); p=scopy(p,"  stolen "); p=udec(p,G.stolen_mb); p=scopy(p," MB"); *p=0; gl(b);
        if(G.found==1){ p=b; p=scopy(p,"Display: "); p=scopy(p,(G.edp>>31)?"built-in panel (eDP)":"pipe A"); p=scopy(p,"  panel "); p=udec(p,(u32)G.nat_w); *p++='x'; p=udec(p,(u32)G.nat_h); p=scopy(p,"  mode "); p=udec(p,(u32)G.cur_w); *p++='x'; p=udec(p,(u32)G.cur_h);
                p=scopy(p,G.scale_on?"  scaled to the panel":(G.cur_w==G.nat_w&&G.cur_h==G.nat_h)?"  native":""); *p=0; gl(b);
            p=b; p=scopy(p,"Plane: "); p=scopy(p,(G.plane_ctl>>31)?"on":"off"); p=scopy(p,G.linear?"  linear":"  tiled"); p=scopy(p,G.fmt_ok?"  32-bit":"  other format"); p=scopy(p,"  stride reg 0x"); p=hx(p,G.stride,4); p=scopy(p,"  surface 0x"); p=hx(p,(u32)(rr(0x7019C)&~0xFFFu),8); p=scopy(p,G.scanout_match?"  matches framebuffer":"  differs from framebuffer"); *p=0; gl(b); } }
    p=b; p=scopy(p,"Framebuffer: "); p=udec(p,W); *p++='x'; p=udec(p,H); *p++=' '; p=udec(p,BYPP*8); p=scopy(p,"-bit  pitch "); p=udec(p,PITCHB); p=scopy(p,"  at 0x"); if(((u64)FBB)>>32)p=hx(p,(u32)(((u64)FBB)>>32),4); p=hx(p,(u32)(u64)FBB,8);
    p=scopy(p,g_fb_wc?"  write-combining":g_pat_ok?"  uncached (too small for WC)":"  uncached (no PAT)"); p=scopy(p,*(volatile u32*)0x502CULL==0x49464555u?"  UEFI":"  legacy BIOS"); *p=0; gl(b);
    { u64 t0=hr_ns(); for(int i=0;i<10;i++)blit_all(); u64 us=(hr_ns()-t0)/10000ULL; G.bench_us=(u32)us; p=b; p=scopy(p,"Drawing: full screen copy "); p=udec(p,(u32)(us/1000)); *p++='.'; p=udec(p,(u32)((us%1000)/100)); p=scopy(p,g_igpu_flip?" ms (average of 10, includes waiting for the screen refresh)":" ms (average of 10)"); *p=0; gl(b); }
    p=b; p=scopy(p,"Tear-free: "); if(g_igpu_flip){ p=scopy(p,"on, double buffer at GTT 0x"); p=hx(p,(u32)G.back_off,8); p=scopy(p,"  flips "); p=udec(p,G.flips); p=scopy(p,"  late "); p=udec(p,G.timeouts); } else { p=scopy(p,"off - "); p=scopy(p,G.why[0]?G.why:"not started"); } *p=0; gl(b);
    gl("  'gpu flip off' or Ctrl+Alt+F turns tear-free drawing off, 'gpu flip on' turns it back on");
    igpu_blt_info(); }
#define BCS 0x22000u
#define RCS 0x2000u
typedef struct { u32 base; int render; u32 ctxp, rpages; u32 *hwsp,*ring,*ctx; u64 hwsp_g,ring_g,ctx_g; u32 seq, csb; int ok, step, ran; char why[64]; } geng;
static geng EB, ER;
int g_blt_ok=0, g_blt_present=0, g_rcs_ok=0, g_sh_ok=0; static int b_flush=0, b_snoop=0, b_ready=0, b_swapped=0;
static u64 b_gtt_next; static u64* b_pd; static u64 b_pd_p; static u64* b_ptv[512];
static u32 b_cpu_us=0,b_gpu_us=0,b_frames=0,b_fail=0,b_ppat_lo=0,b_ppat_hi=0,b_ppat_lo2=0,b_ppat_hi2=0;
static u32* b_scr; static u64 b_scr_g; static int b_fw=0;
static u32* b_gs; static u64 b_gs_g, b_old_g, b_gs_pa, b_old_pa; static int rt_res[3][2]; static u32 rt_got[3][2];
static u32* s_st; static u64 s_st_g; static u32 *s_a, *s_b; static u64 s_a_g, s_b_g; static int s_step=0, s_hung=0; static char s_why[64];
static u32 s_fill_us=0, s_copy_us=0, s_scr_us=0, s_groups=0; static u32 b_rpcs=0, b_ss=0, b_eu=0;
#define SA_W 256
#define SA_H 128
u64 kva_to_pa(u64 va){ u64* pml4=(u64*)(cpu_get_pt()&~0xFFFULL); u64 e=pml4[(va>>39)&511]; if(!(e&1))return 0; u64* pdpt=(u64*)(e&PTE_ADDR); e=pdpt[(va>>30)&511]; if(!(e&1))return 0;
    if(e&0x80)return (e&0x000FFFFFC0000000ULL)|(va&0x3FFFFFFFULL); u64* pd=(u64*)(e&PTE_ADDR); e=pd[(va>>21)&511]; if(!(e&1))return 0; if(e&0x80)return (e&0x000FFFFFFFE00000ULL)|(va&0x1FFFFFULL);
    u64* pt=(u64*)(e&PTE_ADDR); e=pt[(va>>12)&511]; if(!(e&1))return 0; return (e&PTE_ADDR)|(va&0xFFFULL); }
static void clf(const void* p,u64 n){ u64 a=(u64)p&~63ULL; for(;a<(u64)p+n;a+=64)cpu_clflush(a); cpu_mb(); }
static int fw_get(u32 req,u32 ack){ ww(req,0x00010001u); u64 t0=hr_ns(); while(!(rr(ack)&1)){ if(hr_ns()-t0>50000000ULL)return -1; } return 0; }
static void fw_put(void){ if(!b_fw)return; ww(0xA278,0x00010000u); ww(0xA188,0x00010000u); ww(0xA270,0x00010000u); b_fw=0; }
static void efail(geng* e,int step,const char* why){ e->step=step; wcpy(e->why,why,64); e->ok=0; if(e==&EB){ g_blt_ok=0; g_blt_present=0; } else { g_rcs_ok=0; g_sh_ok=0; } }
static int ppg_map(u64 ga,u64 pa,u64 fl){ if(ga>=(1ULL<<30)||!pa)return -1; u32 pdi=(u32)((ga>>21)&511);
    if(!b_ptv[pdi]){ u64* n=(u64*)dma_alloc_pages(1); if(!n)return -1; for(int i=0;i<512;i++)n[i]=0; b_ptv[pdi]=n; b_pd[pdi]=(kva_to_pa((u64)n)&PTE_ADDR)|3ULL; }
    b_ptv[pdi][(ga>>12)&511]=(pa&PTE_ADDR)|fl; return 0; }
static void ppg_flush(void){ clf(b_pd,4096); for(int i=0;i<512;i++) if(b_ptv[i])clf(b_ptv[i],4096); }
static u64 gmap_x(u8* p0,u64 bytes,int ggtt){ u64 g=b_gtt_next; volatile u64* gtt=(volatile u64*)G.gtt; u64 off=(u64)p0&0xFFFULL; u8* p=p0-off; u64 n=(off+bytes+4095)>>12;
    for(u64 i=0;i<n;i++){ u64 pa=kva_to_pa((u64)p+i*4096); if(!pa)return 0; if(ggtt)gtt[(g>>12)+i]=(pa&PTE_ADDR)|1ULL; if(ppg_map(g+i*4096,pa,0x3ULL))return 0; }
    if(ggtt)(void)gtt[(g>>12)+n-1]; b_gtt_next=(g+n*4096+0xFFFFULL)&~0xFFFFULL; return g+off; }
static u64 gmap(u8* p0,u64 bytes){ return gmap_x(p0,bytes,1); }
static u64 gobj(u32 pages,u32** cpu,int heap){ u8* p;
    if(heap){ u8* raw=(u8*)kmalloc((unsigned long)pages*4096UL+4096UL); if(!raw)return 0; p=(u8*)(((u64)raw+4095ULL)&~4095ULL); } else { p=(u8*)dma_alloc_pages(pages); if(!p)return 0; }
    for(u64 i=0;i<(u64)pages*4096;i++)p[i]=0; clf(p,(u64)pages*4096); u64 g=gmap(p,(u64)pages*4096); if(!g)return 0; *cpu=(u32*)p; return g; }
static void creg(u32* st,int i,u32 reg,u32 val){ st[i]=reg; st[i+1]=val; }
static void ctx_fill(geng* e){ u32* st=e->ctx+1024; u32 B=e->base; for(u32 i=0;i<(e->ctxp-1)*1024;i++)st[i]=0;
    st[1]=(0x22u<<23)|(2*(e->render?14:11)-1)|(1u<<12);
    creg(st,0x02,B+0x244,0x00090009u); creg(st,0x04,B+0x34,0); creg(st,0x06,B+0x30,0); creg(st,0x08,B+0x38,(u32)e->ring_g); creg(st,0x0A,B+0x3C,((e->rpages*4096u-4096u)&0x1FF000u)|1u);
    creg(st,0x0C,B+0x168,0); creg(st,0x0E,B+0x140,0); creg(st,0x10,B+0x110,0x20); creg(st,0x12,B+0x11C,0); creg(st,0x14,B+0x114,0); creg(st,0x16,B+0x118,0);
    if(e->render){ creg(st,0x18,B+0x1C0,0); creg(st,0x1A,B+0x1C4,0); creg(st,0x1C,B+0x1C8,0); }
    st[0x21]=(0x22u<<23)|(2*9-1)|(1u<<12); creg(st,0x22,B+0x3A8,0);
    for(int n=3;n>=0;n--){ int i=0x24+(3-n)*4; creg(st,i,B+0x270+(u32)n*8+4,(u32)(b_pd_p>>32)); creg(st,i+2,B+0x270+(u32)n*8,(u32)b_pd_p); }
    if(e->render){ st[0x41]=(0x22u<<23)|1u; creg(st,0x42,0x20C8,b_rpcs); }
    clf(e->ctx,(u64)e->ctxp*4096); }
static int csb_idle(geng* e){ u32 wp=rr(e->base+0x3A0)&7u; int idle=0; if(wp>5)return 0; int g=0; while(e->csb!=wp&&g<8){ e->csb=(e->csb+1)%6; u32 st=rr(e->base+0x370+e->csb*8); if(st&((1u<<3)|(1u<<4)))idle=1; g++; } ww(e->base+0x3A0,(0x0700u<<16)|(e->csb<<8)); return idle; }
static u64 e_tmo=50000000ULL;
static int eng_run(geng* e,u32 n){
    u32* R0=e->ring; while(n&1)R0[n++]=0; clf(R0,(u64)n*4);
    u32* st=e->ctx+1024; clf(st,256); st[0x05]=0; st[0x07]=n*4; if(e->ran)st[0x03]=0x00090008u; clf(st,256);
    u32 want=e->seq; csb_idle(e);
    u64 d=e->ctx_g|1ULL|(1ULL<<3)|(1ULL<<8); u32 sp=e->base+0x230; ww(sp,0); ww(sp,0); ww(sp,e->render?2u:1u); ww(sp,(u32)d);
    u64 t0=hr_ns(); for(;;){ clf(e->hwsp+0x30,4); if(e->hwsp[0x30]==want)break; if(hr_ns()-t0>e_tmo)return -1; cpu_pause(); }
    t0=hr_ns(); int idle=0; while(hr_ns()-t0<10000000ULL){ if(csb_idle(e)){ idle=1; break; } cpu_pause(); } if(!idle)hr_delay_us(500);
    e->ran=1; return 0; }
static u32 put_flush(geng* e,u32 i){ u32* r=e->ring; e->seq++;
    if(e->render){ r[i++]=0x7A000004u; r[i++]=(1u<<24)|(1u<<20)|(1u<<14)|(1u<<12)|(1u<<5); r[i++]=(u32)(e->hwsp_g+0x30*4); r[i++]=0; r[i++]=e->seq; r[i++]=0; return i; }
    r[i++]=(0x26u<<23)|2u|(1u<<14); r[i++]=(u32)(e->hwsp_g+0x30*4)|(1u<<2); r[i++]=0; r[i++]=e->seq; return i; }
static u32 bput_fill(u32 i,u64 dst,u32 pitch,int x1,int y1,int x2,int y2,u32 color){ u32* r=EB.ring;
    r[i++]=(2u<<29)|(0x50u<<22)|(3u<<20)|5u; r[i++]=(3u<<24)|(0xF0u<<16)|pitch; r[i++]=((u32)y1<<16)|(u32)x1; r[i++]=((u32)y2<<16)|(u32)x2; r[i++]=(u32)dst; r[i++]=(u32)(dst>>32); r[i++]=color; return i; }
static u32 bput_copy(u32 i,u64 dst,u32 dpitch,int dx,int dy,int w,int h,u64 src,u32 spitch,int sx,int sy){ u32* r=EB.ring;
    r[i++]=(2u<<29)|(0x53u<<22)|(3u<<20)|8u; r[i++]=(3u<<24)|(0xCCu<<16)|dpitch; r[i++]=((u32)dy<<16)|(u32)dx; r[i++]=((u32)(dy+h)<<16)|(u32)(dx+w);
    r[i++]=(u32)dst; r[i++]=(u32)(dst>>32); r[i++]=((u32)sy<<16)|(u32)sx; r[i++]=spitch; r[i++]=(u32)src; r[i++]=(u32)(src>>32); return i; }
static int eng_setup(geng* e,u32 base,int render,u32 ctxp){
    e->base=base; e->render=render; e->ctxp=ctxp; e->seq=0; e->ok=0; e->ran=0; e->step=4;
    e->rpages=render?64u:4u; e->hwsp_g=gobj(1,&e->hwsp,0); e->ring_g=gobj(e->rpages,&e->ring,render); e->ctx_g=gobj(ctxp,&e->ctx,0);
    if(!e->hwsp_g||!e->ring_g||!e->ctx_g){ efail(e,4,"out of memory for GPU objects"); return -1; }
    ppg_flush(); ww(0x101008,1); (void)rr(0x101008);
    e->step=6; ctx_fill(e); clf(e->hwsp,4096);
    ww(base+0xA8,0xFFFFFFFFu); ww(base+0x98,0xFFFFFFFFu); ww(base+0x80,(u32)e->hwsp_g); (void)rr(base+0x80);
    ww(base+0x9C,0x01000000u); ww(base+0x29C,0x80008000u); (void)rr(base+0x29C);
    { u32 wp=rr(base+0x3A0)&7u; e->csb=(wp>5)?5u:wp; ww(base+0x3A0,(0x0700u<<16)|(e->csb<<8)); }
    return 0; }
static u64 cpu_copy_us(u8* dst){ u64 t0=hr_ns(); for(int k=0;k<5;k++) for(u32 y=0;y<H;y++){ u32* s=SCENE+(u64)y*SW; u32* d=(u32*)(dst+(u64)y*PITCHB); for(u32 x=0;x<W;x++)d[x]=s[x]; } return (hr_ns()-t0)/5000ULL; }
int igpu_shader_present(void); static int igpu_blt_present2(void);
int igpu_blt_present(void){
    if(g_gfx_present==2)return igpu_shader_present(); void igpu_present_dirty_all(void); igpu_present_dirty_all(); u64 bp0=hr_ns(); int br=igpu_blt_present2(); g_gfx_pres_ns+=hr_ns()-bp0; return br; }
static int igpu_blt_present2(void){
    if(!g_blt_ok||!g_blt_present||!b_swapped)return -1; u64 dst=(FBB==g_igpu_back)?G.back_off:G.front_off;
    if(b_flush)clf(SCENE,(u64)SW*H*4);
    u32 i=bput_copy(0,dst,PITCHB,0,0,(int)W,(int)H,b_gs_g,SW*4,0,0); i=put_flush(&EB,i);
    if(eng_run(&EB,i)){ b_fail++; if(b_fail>=3)efail(&EB,10,"blitter stopped answering"); return -1; } b_frames++; return 0; }
static int rd_test(u32* cpu,u64 g,int flush,u32 base,u32* got){
    u32 keep[64]; for(int x=0;x<64;x++)keep[x]=cpu[x];
    for(int x=0;x<64;x++)cpu[x]=base+(u32)x; if(flush)clf(cpu,256);
    u32* d=b_scr+0x3000; for(int x=0;x<64;x++)d[x]=0xDEADBEEFu; clf(d,256);
    u32 i=bput_copy(0,b_scr_g+0xC000,256,0,0,64,1,g,256,0,0); i=put_flush(&EB,i); int r=0;
    if(eng_run(&EB,i))r=-1;
    else { clf(d,256); for(int x=0;x<64;x++) if(d[x]!=base+(u32)x){ *got=d[x]; r=1; break; } }
    for(int x=0;x<64;x++)cpu[x]=keep[x]; return r; }
static void rd_tests(void){
    u32* bufs[3]={b_scr+0x3800,b_gs,SCENE_STATIC}; u64 gs[3]={b_scr_g+0xE000,b_gs_g,b_old_g};
    for(int b=0;b<3;b++){ rt_res[b][0]=rt_res[b][1]=-2; rt_got[b][0]=rt_got[b][1]=0; if(!gs[b])continue;
        rt_res[b][0]=rd_test(bufs[b],gs[b],1,0x00A01000u+(u32)b*0x2000u,&rt_got[b][0]); if(rt_res[b][0]<0)return;
        rt_res[b][1]=rd_test(bufs[b],gs[b],0,0x00B01000u+(u32)b*0x2000u,&rt_got[b][1]); if(rt_res[b][1]<0)return; } }
static void scene_swap(void){
    if(b_swapped||!b_gs)return; u32* old=SCENE; for(u64 k=0;k<(u64)SW*H;k++)b_gs[k]=old[k]; g_scene=b_gs; b_swapped=1; }
static u32 sh_curbe(u32 off,u32 base,u32 sx,u32 sy){ u32* c=s_st+off/4; for(int k=0;k<16;k++)c[k]=0; c[0]=base; c[1]=sx; c[2]=sy; return off; }
static void sh_surf(u32 off,u64 g,u32 w,u32 h,u32 pitch){ u32* s=s_st+off/4; for(int k=0;k<16;k++)s[k]=0;
    s[0]=(1u<<29)|(0x0C0u<<18)|(1u<<16)|(1u<<14)|(1u<<8); s[2]=((h-1)<<16)|(w-1); s[3]=pitch-1;
    s[7]=(4u<<25)|(5u<<22)|(6u<<19)|(7u<<16); s[8]=(u32)g; s[9]=(u32)(g>>32); }
static void sh_idd(u32 off,u32 kern,u32 bt){ u32* d=s_st+off/4; for(int k=0;k<8;k++)d[k]=0; d[0]=kern&~63u; d[2]=1u<<18; d[4]=bt&0xFFE0u; d[5]=1u<<16; d[6]=1; }
static int sh_run(u32 idd,u32 curbe,u32 gx,u32 gy,u32* us){
    geng* e=&ER; u32* r=e->ring; u32 i=0; u64 S=s_st_g;
    r[i++]=0x7A000004u; r[i++]=(1u<<20)|(1u<<12)|(1u<<5); r[i++]=0; r[i++]=0; r[i++]=0; r[i++]=0;
    r[i++]=0x69040302u;
    r[i++]=0x61010011u; r[i++]=1; r[i++]=0; r[i++]=0; r[i++]=(u32)S|1u; r[i++]=(u32)(S>>32); r[i++]=(u32)S|1u; r[i++]=(u32)(S>>32); r[i++]=1; r[i++]=0; r[i++]=(u32)S|1u; r[i++]=(u32)(S>>32);
    r[i++]=0xFFFFF001u; r[i++]=0xFFFFF001u; r[i++]=0xFFFFF001u; r[i++]=0xFFFFF001u; r[i++]=1; r[i++]=0; r[i++]=0xFFFFF000u;
    r[i++]=0x7A000004u; r[i++]=(1u<<20)|(1u<<11)|(1u<<10)|(1u<<3)|(1u<<2); r[i++]=0; r[i++]=0; r[i++]=0; r[i++]=0;
    r[i++]=0x70000007u; r[i++]=0; r[i++]=0; r[i++]=(63u<<16)|(2u<<8); r[i++]=0; r[i++]=(2u<<16)|2u; r[i++]=0; r[i++]=0; r[i++]=0;
    r[i++]=0x70010002u; r[i++]=0; r[i++]=64; r[i++]=curbe;
    r[i++]=0x70020002u; r[i++]=0; r[i++]=32; r[i++]=idd;
    r[i++]=0x7105000Du; r[i++]=0; r[i++]=0; r[i++]=0; r[i++]=0; r[i++]=0; r[i++]=0; r[i++]=gx; r[i++]=0; r[i++]=0; r[i++]=gy; r[i++]=0; r[i++]=1; r[i++]=0xFFu; r[i++]=0xFFFFFFFFu;
    r[i++]=0x70040000u; r[i++]=0;
    r[i++]=0x7A000004u; r[i++]=(1u<<20)|(1u<<5); r[i++]=0; r[i++]=0; r[i++]=0; r[i++]=0;
    i=put_flush(e,i); clf(s_st,4096*4);
    u64 t0=hr_ns(); int rc=eng_run(e,i); if(us)*us=(u32)((hr_ns()-t0)/1000ULL); if(rc)s_hung=1; return rc; }
static u32 sh_px(u32 x,u32 y,u32 base,u32 sx,u32 sy){ return (((x>>sx)&0xFFu)<<16)|(((y>>sy)&0xFFu)<<8)|base; }
static void sfail(int step,const char* why){ s_step=step; wcpy(s_why,why,64); g_sh_ok=0; }
static void sfail_px(int step,const char* what,u32 x,u32 y,u32 got,u32 want){ char m[64],*p=m; p=scopy(p,(char*)what); p=scopy(p," at "); p=udec(p,x); *p++=','; p=udec(p,y); p=scopy(p," got 0x"); p=hx(p,got,6); p=scopy(p," want 0x"); p=hx(p,want,6); *p=0; sfail(step,m); }
static void sh_init(void){
    s_step=12; if(!s_st||!s_a||!s_b){ sfail(12,"out of memory for shader objects"); return; }
    g9asm a; g9_begin(&a,s_st+0x1000/4,120); g9_kernel_fill(&a); int nf=a.n; if(a.err){ sfail(12,"fill shader did not assemble"); return; }
    g9_begin(&a,s_st+0x1800/4,120); g9_kernel_copy(&a); if(a.err){ sfail(12,"copy shader did not assemble"); return; } (void)nf;
    sh_surf(0x200,s_a_g,SA_W,SA_H,SA_W*4); sh_surf(0x240,s_b_g,SA_W,SA_H,SA_W*4); if(b_gs)sh_surf(0x280,b_gs_g,W,H,SW*4);
    s_st[0x100/4]=0x200; s_st[0x120/4]=0x240; s_st[0x124/4]=0x200; s_st[0x140/4]=0x280;
    sh_idd(0x400,0x1000,0x100); sh_idd(0x440,0x1800,0x120); sh_idd(0x480,0x1000,0x140);
    sh_curbe(0x40,0x80,0,0);
    s_step=13; if(sh_run(0x400,0x40,SA_W/8,SA_H/8,&s_fill_us)){ char m[64],*p=m; p=scopy(p,"fill shader did not finish (head 0x"); p=hx(p,rr(RCS+0x34),4); p=scopy(p," status 0x"); p=hx(p,rr(RCS+0x234),8); *p++=')'; *p=0; sfail(13,m); efail(&ER,12,"stopped by the fill shader"); return; }
    s_step=14; clf(s_a,SA_W*SA_H*4); for(u32 y=0;y<SA_H;y++) for(u32 x=0;x<SA_W;x++){ u32 w=sh_px(x,y,0x80,0,0), g=s_a[y*SA_W+x]; if(g!=w){ sfail_px(14,g==0?"fill shader wrote nothing":"fill shader wrong pixel",x,y,g,w); return; } }
    s_step=15; if(sh_run(0x440,0x40,SA_W/8,SA_H/8,&s_copy_us)){ sfail(15,"copy shader did not finish"); efail(&ER,12,"stopped by the copy shader"); return; }
    clf(s_b,SA_W*SA_H*4); for(u32 y=0;y<SA_H;y++) for(u32 x=0;x<SA_W;x++){ u32 w=sh_px(x,y,0x80,0,0), g=s_b[y*SA_W+x]; if(g!=w){ sfail_px(15,g==0?"copy shader wrote nothing":"copy shader wrong pixel",x,y,g,w); return; } }
    s_step=16; if(b_gs&&!b_swapped){ u32 sx=0,sy=0; while(((W-1)>>sx)>255)sx++; while(((H-1)>>sy)>255)sy++; sh_curbe(0x80,0x60,sx,sy);
        u32 gx=(W+7)/8, gy=(H+7)/8; s_groups=gx*gy;
        if(sh_run(0x480,0x80,gx,gy,&s_scr_us)){ sfail(16,"full-screen shader did not finish"); efail(&ER,12,"stopped by the full-screen shader"); return; }
        clf(b_gs,(u64)SW*H*4); for(u32 y=0;y<H;y++) for(u32 x=y%7;x<W;x+=7){ u32 w=sh_px(x,y,0x60,sx,sy), g=b_gs[(u64)y*SW+x]; if(g!=w){ sfail_px(16,"full-screen shader wrong pixel",x,y,g,w); return; } } }
    s_step=17; s_why[0]=0; g_sh_ok=1;
    { char m[110],*p=m; p=scopy(p,"gpu: shaders ready, fill "); p=udec(p,s_fill_us); p=scopy(p," us, copy "); p=udec(p,s_copy_us); p=scopy(p," us, full screen "); p=udec(p,s_scr_us); p=scopy(p," us"); *p=0; klog(m); } }

static u32* q_st; static u64 q_st_g; static u32* q_k; static u64 q_k_g; static u32* q_tp[4]; static u64 q_tg[4]; static u32* q_tc[4];
static const u32 TW[4]={160,96,136,64}, TH[4]={120,72,100,64}, TP[4]={160,96,144,64}; static int q_step=0, q_scene=0, q_disp[2];
static u32 q_test_us=0, q_pres_us[3]={0,0,0}, q_kinsn=0;
static int q_ready=0; static char q_why[64];
static int q_submit(u32 n){ u32 i=put_flush(&ER,n); u64 t0=hr_ns(); e_tmo=250000000ULL; int r=eng_run(&ER,i); e_tmo=50000000ULL; g_gq_last_us=(u32)((hr_ns()-t0)/1000ULL); if(r){ efail(&ER,12,"a drawing batch did not finish"); q_ready=0; wcpy(q_why,"a drawing batch did not finish - restart to use the GPU again",64); } return r; }
static void q_flush(const void* p,u64 n){ clf(p,n); }
static int igpu_surf_alloc(u32 w,u32 h,u32 pitch,u32** px){
    gq_sync(); u64 bytes=(u64)pitch*h*4; u8* raw=(u8*)kmalloc((unsigned long)bytes+8192UL); if(!raw)return -1; u8* p=(u8*)(((u64)raw+4095ULL)&~4095ULL);
    for(u64 k=0;k<bytes/8;k++)((u64*)p)[k]=0; u64 g=gmap_x(p,bytes,0); if(!g){ kfree(raw); return -1; } ppg_flush(); g_gq_tlb=1;
    int s=gq_surf((u32*)p,g,w,h,pitch); if(s<0){ kfree(raw); return -1; } *px=(u32*)p; return s; }
static void q_alloc(void){
    u32* p; q_st_g=gobj(80,&p,1); q_st=q_st_g?p:0; q_k_g=gobj(16,&p,1); q_k=q_k_g?p:0;
    for(int k=0;k<4;k++){ u32* c; q_tg[k]=gobj((TP[k]*TH[k]*4+4095)/4096,&c,1); q_tp[k]=q_tg[k]?c:0; q_tc[k]=(u32*)kmalloc(TP[k]*TH[k]*4); } }
static int q_init(void){
    q_step=18; if(!q_st||!q_k){ wcpy(q_why,"out of memory for drawing state",64); return -1; }
    u32 off=0; for(int k=0;k<GK_N;k++){ g9asm a; g9_begin(&a,q_k+off/4,(int)((16*4096-off)/16)); gk_build(k,&a); if(a.err){ wcpy(q_why,"a drawing shader did not assemble",64); return -1; } g_gqhw.koff[k]=off; q_kinsn+=(u32)a.n; off+=((u32)a.n*16+63)&~63u; }
    clf(q_k,16*4096);
    g_gqhw.ring=ER.ring; g_gqhw.ring_cap=ER.rpages*1024u-64u; g_gqhw.st=q_st; g_gqhw.st_g=q_st_g; g_gqhw.st_size=80*4096; g_gqhw.kern_g=q_k_g; g_gqhw.scratch_g=ER.hwsp_g+0x800; g_gqhw.submit=q_submit; g_gqhw.flush=q_flush; g_gqhw.now=hr_ns;
    q_step=19; int sid[4]; u32* gp[4];
    for(int k=0;k<4;k++){ if(!q_tp[k]||!q_tc[k]){ wcpy(q_why,"out of memory for test images",64); return -1; } }
    for(int k=0;k<4;k++){ u32 w=TW[k],h=TH[k]; u32* c=q_tp[k]; gp[k]=c; sid[k]=gq_surf(c,q_tg[k],w,h,TP[k]); if(sid[k]<0){ wcpy(q_why,"no free surface slots",64); return -1; }
        u32 rs=0x9E3779B9u*(u32)(k+1); for(u32 i=0;i<TP[k]*h;i++){ rs^=rs<<13; rs^=rs>>17; rs^=rs<<5; u32 v=rs; if(k==3)v=rs%257u; else if((rs>>8)%7u==0)v|=0xFF000000u; else v&=0x00FFFFFFu; c[i]=v; q_tc[k][i]=v; } }
    g_gq_mode=GQM_GPU; g_gq_tlb=1;
    gq_fill(sid[0],0,0,8,8,0x00123456u); if(gq_sync()){ wcpy(q_why,"first drawing batch did not finish",64); g_gq_mode=GQM_OFF; return -1; }
    if(gp[0][0]!=0x00123456u||gp[0][7*160+7]!=0x00123456u){ clf(gp[0],160*8*4); { char m[64],*q=m; q=scopy(q,"first fill wrote nothing (0x"); q=hx(q,gp[0][0],8); *q++=')'; *q=0; wcpy(q_why,m,64); g_gq_mode=GQM_OFF; return -1; } }
    for(int pass=0;pass<2;pass++){ u32 base=pass?0x00508000u:0x00A01000u; for(int i=0;i<64;i++)gp[1][i]=base+(u32)i;
        gq_copy(sid[2],0,0,64,1,sid[1],0,0,256,0); if(gq_sync()){ wcpy(q_why,"cache test did not finish",64); g_gq_mode=GQM_OFF; return -1; }
        for(int i=0;i<64;i++) if(gp[2][i]!=base+(u32)i){ wcpy(q_why,pass?"the GPU kept old data in its cache":"the GPU did not read what the CPU wrote",64); g_gq_mode=GQM_OFF; return -1; } }
    for(int k=0;k<3;k++) for(u32 i=0;i<TP[k]*TH[k];i++) q_tc[k][i]=gp[k][i];
    for(int i=0;i<64;i++)gp[0][i]=0x00ABCDEFu; gq_fill(sid[0],0,0,64,1,0x00FEDCBAu); if(gq_sync()){ wcpy(q_why,"coherency test did not finish",64); g_gq_mode=GQM_OFF; return -1; }
    for(int i=0;i<64;i++) if(gp[0][i]!=0x00FEDCBAu){ wcpy(q_why,"the CPU does not see what the GPU drew",64); g_gq_mode=GQM_OFF; return -1; }
    for(int i=0;i<64;i++) q_tc[0][i]=0x00FEDCBAu;
    q_step=20; u64 t0=hr_ns(); char why[64]; int r=gq_selftest(sid,q_tc,3,sid[3],0xC0FFEEu,400,why); q_test_us=(u32)((hr_ns()-t0)/1000ULL);
    if(r<0){ char m[64],*q=m; q=scopy(q,r==-2?"":"wrong pixel: "); q=scopy(q,why); *q=0; wcpy(q_why,m,64); g_gq_mode=GQM_OFF; return -1; }
    q_scene=gq_surf(b_gs,b_gs_g,W,H,SW); q_disp[0]=gq_surf((u32*)g_orig_fbb,g_bfront0,W,H,PITCHB/4); q_disp[1]=gq_surf((u32*)g_orig_fbb,g_bback0,W,H,PITCHB/4);
    if(q_scene<0||q_disp[0]<0||q_disp[1]<0){ wcpy(q_why,"no free surface slots",64); g_gq_mode=GQM_OFF; return -1; }
    g_gq_mode=GQM_OFF; q_ready=1; q_step=21; q_why[0]=0; return 0; }
static int q_disp_sid(void){ u64 dst=(FBB==g_igpu_back)?G.back_off:G.front_off; return dst==g_bfront0?q_disp[0]:q_disp[1]; }
extern volatile u64 g_ticks; int g_pres_eng=0; static int ph_ok=0, ph[4], pd_n[2], pd[2][4]; static u64 pfull_t=0; u32 g_pres_part=0, g_pres_full=0, g_pres_px=0;
void igpu_present_hint(int x,int y,int w,int h){ if(x<0){ w+=x; x=0; } if(y<0){ h+=y; y=0; } if(x+w>(int)W)w=(int)W-x; if(y+h>(int)H)h=(int)H-y; if(w<=0||h<=0){ w=0; h=0; } ph[0]=x; ph[1]=y; ph[2]=x+w; ph[3]=y+h; ph_ok=1; }
void igpu_present_dirty_all(void){ for(int b=0;b<2;b++){ pd_n[b]=1; pd[b][0]=0; pd[b][1]=0; pd[b][2]=(int)W; pd[b][3]=(int)H; } ph_ok=0; }
static void pd_add(int b,const int* r){ if(r[2]<=r[0]||r[3]<=r[1])return; if(!pd_n[b]){ for(int k=0;k<4;k++)pd[b][k]=r[k]; pd_n[b]=1; return; } if(r[0]<pd[b][0])pd[b][0]=r[0]; if(r[1]<pd[b][1])pd[b][1]=r[1]; if(r[2]>pd[b][2])pd[b][2]=r[2]; if(r[3]>pd[b][3])pd[b][3]=r[3]; }
int igpu_shader_present(void){ if(!q_ready||g_gfx!=GFX_GPU||g_gfx_present!=2)return -1; u64 t0=hr_ns(); gfx_sync(); u64 t1=hr_ns();
    int sd=q_disp_sid(); int bi=(sd==q_disp[0])?0:1; int full=!ph_ok||(g_ticks-pfull_t>=100); int H4[4]={0,0,(int)W,(int)H}; if(!full){ for(int k=0;k<4;k++)H4[k]=ph[k]; }
    pd_add(0,H4); pd_add(1,H4); ph_ok=0;
    if(pd_n[bi]){ int x=pd[bi][0], y=pd[bi][1], w=pd[bi][2]-x, h=pd[bi][3]-y;
        if(w>0&&h>0){ if(g_pres_eng==0&&g_blt_ok&&b_swapped){ u64 dst=(FBB==g_igpu_back)?G.back_off:G.front_off; if(b_flush){ for(int r=0;r<h;r++)clf(b_gs+(u64)(y+r)*SW+x,(u64)w*4); } u32 i=bput_copy(0,dst,PITCHB,x,y,w,h,b_gs_g,SW*4,x,y); i=put_flush(&EB,i); if(eng_run(&EB,i)){ b_fail++; g_pres_eng=1; gq_copy(sd,0,0,(int)W,(int)H,q_scene,0,0,256,0); } }
            else gq_copy(sd,x,y,w,h,q_scene,x,y,256,0); }
        pd_n[bi]=0; g_pres_px=(u32)(w>0&&h>0?w*h:0); } else g_pres_px=0;
    if(full){ pfull_t=g_ticks; g_pres_full++; } else g_pres_part++;
    gfx_sync(); g_gfx_pres_ns+=hr_ns()-t1; (void)t0; return g_gfx==GFX_GPU?0:-1; }
static u32 f_max=0, f_min=0, f_req=0, f_orig=0; static int f_ok=0;
static u32 fmhz(u32 v){ return (v*50u+1u)/3u; }
static void gfreq_init(void){ int lp=(G.did==0x3184||G.did==0x3185||G.did==0x5A84||G.did==0x5A85||G.did==0x0A84); if(!lp||G.found!=1)return; f_orig=rr(0xA008); u32 cap=rr(0x138170); u32 a=cap&0xFFu, c=(cap>>16)&0xFFu; f_max=a>c?a:c; f_min=a>c?c:a; f_ok=(f_max>0&&f_max<120&&f_min<=f_max); }
static void gfreq_set(int hi){ if(!f_ok)return; if(!hi){ ww(0xA008,f_orig); f_req=(f_orig>>23)&0x1FFu; return; } ww(0xA008,f_max<<23); f_req=f_max; }
static void gfreq_restore(void){ gfreq_set(0); }
static u32 gfreq_now(void){ return (rr(0xA01C)>>23)&0x1FFu; }
static void q_start(void){
    if(!q_ready||!b_swapped)return;
    q_step=22; if(gfx_start(GFX_GPU,igpu_surf_alloc,b_gs,q_scene)){ wcpy(q_why,g_gfx_why,64); return; }
    u8* bk=g_igpu_back; q_pres_us[0]=b_cpu_us?b_cpu_us:(u32)cpu_copy_us(bk); q_pres_us[1]=(g_blt_ok&&EB.step>=11)?b_gpu_us:0;
    { int sd=(bk==g_igpu_back)?((G.back_off==g_bfront0)?q_disp[0]:q_disp[1]):q_disp[0]; u64 t0=hr_ns(); int bad=0; for(int k=0;k<5;k++){ gq_copy(sd,0,0,(int)W,(int)H,q_scene,0,0,256,0); gfx_sync(); if(g_gfx!=GFX_GPU){ bad=1; break; } } q_pres_us[2]=bad?0:(u32)((hr_ns()-t0)/5000ULL); }
    if(g_gfx!=GFX_GPU){ wcpy(q_why,"screen copy shader did not finish",64); return; }
    int best=0; u32 bt=q_pres_us[0]; if(q_pres_us[1]&&q_pres_us[1]<bt){ best=1; bt=q_pres_us[1]; } if(q_pres_us[2]&&q_pres_us[2]<bt){ best=2; bt=q_pres_us[2]; }
    g_gfx_present=best; g_blt_present=(q_pres_us[1]&&q_pres_us[1]<q_pres_us[0]); q_step=23; gfreq_set(1); { extern void (*g_gfx_on_stop)(void); g_gfx_on_stop=gfreq_restore; }
    char m[120],*p=m; p=scopy(p,"gpu: GPU drawing on, screen copy CPU "); p=udec(p,q_pres_us[0]); p=scopy(p," us, blitter "); p=udec(p,q_pres_us[1]); p=scopy(p," us, shader "); p=udec(p,q_pres_us[2]); p=scopy(p," us"); *p=0; klog(m); }
static int emu_alloc(u32 w,u32 h,u32 pitch,u32** px){ u64 bytes=(u64)pitch*h*4; u8* raw=(u8*)kmalloc((unsigned long)bytes+4096UL); if(!raw)return -1; u32* p=(u32*)(((u64)raw+4095ULL)&~4095ULL); for(u64 k=0;k<(u64)pitch*h;k++)p[k]=0; *px=p; return gq_surf(p,0,w,h,pitch); }
int igpu_draw_mode(int mode){
    if(mode==GFX_EMU){ if(g_gfx==GFX_GPU)gfx_stop("switching"); g_gfx_present=0; return gfx_start(GFX_EMU,emu_alloc,0,0); }
    if(mode==GFX_GPU){ if(!q_ready||!g_rcs_ok)return -1; if(g_gfx==GFX_GPU)return 0; if(g_gfx==GFX_EMU)gfx_stop("switching"); q_start(); return g_gfx==GFX_GPU?0:-1; }
    gfx_stop("turned off by the user"); g_gfx_present=0; return 0; }
static void rcs_init(void){
    geng* e=&ER; e->step=1;
    if(eng_setup(e,RCS,1,23))return;
    e->step=7; u32* r=e->ring; u32 i=0; r[i++]=(0x20u<<23)|(1u<<22)|2u; r[i++]=(u32)(e->hwsp_g+0x40*4); r[i++]=0; r[i++]=0x600DCAFEu; i=put_flush(e,i);
    if(eng_run(e,i)){ char m[64],*p=m; p=scopy(p,"first command did not finish (head 0x"); p=hx(p,rr(RCS+0x34),4); p=scopy(p," status 0x"); p=hx(p,rr(RCS+0x234),8); *p++=')'; *p=0; efail(e,7,m); return; }
    clf(e->hwsp+0x40,4); if(e->hwsp[0x40]!=0x600DCAFEu){ efail(e,7,"store command wrote nothing"); return; }
    e->step=8; i=0; r[i++]=0x7A000004u; r[i++]=(1u<<20)|(1u<<12)|(1u<<5)|(1u<<0); r[i++]=0; r[i++]=0; r[i++]=0; r[i++]=0;
    r[i++]=0x69040302u; i=put_flush(e,i);
    if(eng_run(e,i)){ efail(e,8,"compute pipeline select did not finish"); return; }
    e->step=9; i=0; r[i++]=0x69040302u; u32 sb=i; r[i++]=0x61010011u; for(int k=0;k<18;k++)r[i++]=0; r[sb+1]=1; r[sb+4]=1; r[sb+6]=1; r[sb+8]=1; r[sb+10]=1; r[sb+12]=0xFFFFF001u; r[sb+13]=0xFFFFF001u; r[sb+14]=0xFFFFF001u; r[sb+15]=0xFFFFF001u;
    i=put_flush(e,i); if(eng_run(e,i)){ efail(e,9,"state base address did not finish"); return; }
    e->step=10; i=0; r[i++]=0x69040302u; r[i++]=0x70000007u; r[i++]=0; r[i++]=0; r[i++]=(31u<<16)|(4u<<8); r[i++]=0; r[i++]=(4u<<16)|4u; r[i++]=0; r[i++]=0; r[i++]=0; i=put_flush(e,i);
    if(eng_run(e,i)){ efail(e,10,"compute front end setup did not finish"); return; }
    e->ok=1; e->step=11; e->why[0]=0; g_rcs_ok=1; klog("gpu: render engine ready (compute pipeline)");
    sh_init(); gfreq_init(); if(g_sh_ok&&q_init()) g_gq_mode=GQM_OFF; }
static void blt_init_inner(void);
static void blt_bench(void){
    if(!EB.ok||!b_swapped)return;
    u8* bk=g_igpu_back; b_cpu_us=(u32)cpu_copy_us(bk);
    u64 t0=hr_ns(); for(int k=0;k<5;k++){ u64 dst=(bk==g_igpu_back)?G.back_off:G.front_off; if(b_flush)clf(SCENE,(u64)SW*H*4); u32 j=bput_copy(0,dst,PITCHB,0,0,(int)W,(int)H,b_gs_g,SW*4,0,0); j=put_flush(&EB,j); if(eng_run(&EB,j)){ efail(&EB,10,"full-screen copy did not finish"); return; } }
    b_gpu_us=(u32)((hr_ns()-t0)/5000ULL); EB.step=11; g_blt_ok=1; g_blt_present=b_gpu_us<b_cpu_us;
    char m[110],*p=m; p=scopy(p,"gpu: blitter ready, screen copy CPU "); p=udec(p,b_cpu_us); p=scopy(p," us, GPU "); p=udec(p,b_gpu_us); p=scopy(p,b_flush?" us (with cache flush)":" us (snooped)"); *p=0; klog(m); }
void igpu_blt_init(void){
    for(int b=0;b<3;b++)rt_res[b][0]=rt_res[b][1]=-2;
    blt_init_inner();
    if(b_ready&&g_igpu_flip){ if(ER.step==0)rcs_init(); if(!s_hung)scene_swap(); blt_bench(); q_start(); }
    if(!EB.ok&&!ER.ok)fw_put(); }
static void blt_init_inner(void){
    for(int i=0;i<512;i++)b_ptv[i]=0; EB.step=1; ER.step=0;
    if(G.found!=1){ efail(&EB,1,"no Intel graphics"); return; }
    if(G.gen!=9){ efail(&EB,1,"only gen 9 (Skylake to Comet Lake, Apollo/Gemini Lake) so far"); return; }
    if(!g_igpu_flip){ efail(&EB,1,"needs tear-free double buffering"); return; }
    EB.step=2; if(fw_get(0xA278,0x0D84)||fw_get(0xA188,0x130044)||fw_get(0xA270,0x0D88)){ ww(0xA278,0x00010000u); ww(0xA188,0x00010000u); ww(0xA270,0x00010000u); efail(&EB,2,"GPU did not wake up (forcewake)"); return; } b_fw=1;
    EB.step=3; b_ppat_lo=rr(0x40E0); b_ppat_hi=rr(0x40E4); int lp=((G.did>>8)==0x31||(G.did>>8)==0x5A);
    if(lp){ ww(0x40E0,0x00000040u); ww(0x40E4,0x40404040u); } b_ppat_lo2=rr(0x40E0); b_ppat_hi2=rr(0x40E4);
    if(lp){ u32 f2=rr(0x9120), ed=rr(0x9134); u32 ssm=(~(f2>>20))&7u; b_ss=0; b_eu=0; for(int k=0;k<3;k++) if(ssm&(1u<<k)){ b_ss++; u32 n=0; u32 bm=(ed>>(k*8))&0xFFu, z=0; while(bm){ z+=bm&1u; bm>>=1; } n=8u-z; if(n>b_eu)b_eu=n; }
        if(b_ss>=1&&b_ss<=3&&b_eu>=1&&b_eu<=8)b_rpcs=(1u<<31)|(1u<<11)|(b_ss<<8)|(b_eu<<4)|b_eu; }
    EB.step=4; b_gtt_next=(g_bback0>g_bfront0?g_bback0:g_bfront0)+(((u64)PITCHB*H+0x3FFFFULL)&~0x3FFFFULL); b_gtt_next=(b_gtt_next+0xFFFFULL)&~0xFFFFULL; if(b_gtt_next+0x3000000ULL>(256ULL<<20)){ efail(&EB,4,"no room in the graphics address space"); return; }
    b_pd=(u64*)dma_alloc_pages(1); if(!b_pd){ efail(&EB,4,"out of memory for GPU page tables"); return; } for(int i=0;i<512;i++)b_pd[i]=0; b_pd_p=kva_to_pa((u64)b_pd)&PTE_ADDR;
    { u32* sc; b_scr_g=gobj(16,&sc,0); b_scr=sc; } if(!b_scr_g){ efail(&EB,4,"out of memory for GPU objects"); return; }
    { u32 pg=(u32)(((u64)MAXW*MAXH*4+4095)>>12); u32* gs=0; b_gs_g=gobj(pg,&gs,1); b_gs=b_gs_g?gs:0; if(b_gs){ b_gs_pa=kva_to_pa((u64)b_gs); } }
    b_old_pa=kva_to_pa((u64)SCENE_STATIC); b_old_g=gmap((u8*)SCENE_STATIC,(u64)SW*H*4);
    q_alloc();
    { u32* p; s_st_g=gobj(4,&p,0); s_st=s_st_g?p:0; s_a_g=gobj(SA_W*SA_H*4/4096,&p,1); s_a=s_a_g?p:0; s_b_g=gobj(SA_W*SA_H*4/4096,&p,1); s_b=s_b_g?p:0; }
    if(!b_gs){ efail(&EB,4,"out of memory for the GPU screen buffer"); return; }
    EB.step=5;
    { volatile u64* gtt=(volatile u64*)G.gtt; u64 sz=((u64)PITCHB*H+0xFFFULL)&~0xFFFULL;
      for(u64 o=0;o<sz;o+=4096){ if(ppg_map(g_bfront0+o,gtt[(g_bfront0+o)>>12]&PTE_ADDR,0x1BULL)||ppg_map(g_bback0+o,gtt[(g_bback0+o)>>12]&PTE_ADDR,0x1BULL)){ efail(&EB,5,"could not map the screen buffers"); return; } } }
    ppg_flush(); b_ready=1;
    if(eng_setup(&EB,BCS,0,3))return;
    EB.step=7; u32 i=bput_fill(0,b_scr_g,256,0,0,64,64,0xFF3366CCu); i=put_flush(&EB,i);
    if(eng_run(&EB,i)){ char m[64],*p=m; p=scopy(p,"first command did not finish (head 0x"); p=hx(p,rr(BCS+0x34),4); p=scopy(p," status 0x"); p=hx(p,rr(BCS+0x234),8); *p++=')'; *p=0; efail(&EB,7,m); return; }
    EB.step=8; clf(b_scr,64*256); for(int y=0;y<64;y++) for(int x=0;x<64;x++) if(b_scr[y*64+x]!=0xFF3366CCu){ efail(&EB,8,"fill test wrote wrong data"); return; }
    i=bput_copy(0,b_scr_g+32768,256,0,0,64,64,b_scr_g,256,0,0); i=put_flush(&EB,i);
    if(eng_run(&EB,i)){ efail(&EB,8,"copy test did not finish"); return; }
    clf(b_scr,65536); for(int y=0;y<64;y++) for(int x=0;x<64;x++) if(b_scr[8192+y*64+x]!=0xFF3366CCu){ efail(&EB,8,"copy test wrote wrong data"); return; }
    EB.step=9; rd_tests();
    if(rt_res[0][0]==-1||rt_res[0][1]==-1||rt_res[1][0]==-1||rt_res[1][1]==-1||rt_res[2][0]==-1||rt_res[2][1]==-1){ efail(&EB,9,"read test did not finish"); return; }
    if(rt_res[1][0]!=0){ efail(&EB,9,rt_res[0][0]==0?"GPU reads wrong data from the new screen buffer":"GPU reads wrong data from memory the CPU wrote"); return; }
    b_snoop=(rt_res[1][1]==0); b_flush=!b_snoop;
    EB.ok=1; g_blt_ok=1; EB.step=10; EB.why[0]=0; }
static void tms(char** pp,u32 us){ char* p=*pp; p=udec(p,us/1000); *p++='.'; p=udec(p,(us%1000)/100); p=scopy(p," ms"); *pp=p; }
static char* rtw(char* p,int r,u32 got){ if(r==0)return scopy(p,"ok"); if(r==-2)return scopy(p,"-"); if(r==-1)return scopy(p,"no answer"); p=scopy(p,"wrong (0x"); p=hx(p,got,6); *p++=')'; return p; }
static char* phys(char* p,u64 a){ p=scopy(p,"0x"); if(a>>32)p=hx(p,(u32)(a>>32),2); return hx(p,(u32)a,8); }
void igpu_blt_info(void){ char b[220],*p=b;
    p=scopy(p,"2D blitter: "); if(g_blt_ok&&EB.step>=11){ p=scopy(p,"ready  screen copy CPU "); tms(&p,b_cpu_us); p=scopy(p,", GPU "); tms(&p,b_gpu_us); p=scopy(p,b_flush?" (needs cache flush)":" (snooped)"); p=scopy(p,"  presents by "); p=scopy(p,g_blt_present?"GPU":"CPU"); p=scopy(p,"  GPU frames "); p=udec(p,b_frames); p=scopy(p,"  failed "); p=udec(p,b_fail); }
    else { p=scopy(p,"off at step "); p=udec(p,(u32)EB.step); p=scopy(p," - "); p=scopy(p,EB.why[0]?EB.why:"not started"); }
    *p=0; gl(b);
    if(G.found==1&&G.gen==9){
        p=b; p=scopy(p,"  Screen buffer: "); if(b_swapped){ p=scopy(p,"GPU-shared memory at "); p=phys(p,b_gs_pa); } else p=scopy(p,"kernel buffer (not moved)"); p=scopy(p,"  old kernel buffer at "); p=phys(p,b_old_pa);
        p=scopy(p,b_old_pa!=(u64)SCENE_STATIC?" (moved at boot)":" (not moved)"); *p=0; gl(b);
        p=b; p=scopy(p,"  GPU reads, flushed/unflushed: test "); p=rtw(p,rt_res[0][0],rt_got[0][0]); *p++='/'; p=rtw(p,rt_res[0][1],rt_got[0][1]);
        p=scopy(p,"  new screen "); p=rtw(p,rt_res[1][0],rt_got[1][0]); *p++='/'; p=rtw(p,rt_res[1][1],rt_got[1][1]);
        p=scopy(p,"  old screen "); p=rtw(p,rt_res[2][0],rt_got[2][0]); *p++='/'; p=rtw(p,rt_res[2][1],rt_got[2][1]); *p=0; gl(b);
        p=b; p=scopy(p,"  GPU memory types before 0x"); p=hx(p,b_ppat_hi,8); p=hx(p,b_ppat_lo,8); p=scopy(p," after 0x"); p=hx(p,b_ppat_hi2,8); p=hx(p,b_ppat_lo2,8);
        p=scopy(p,"  blitter head 0x"); p=hx(p,rr(BCS+0x34),4); p=scopy(p," status 0x"); p=hx(p,rr(BCS+0x234),8); p=scopy(p," seq "); p=udec(p,EB.seq); *p=0; gl(b); }
    p=b; p=scopy(p,"Render engine: "); if(g_rcs_ok){ p=scopy(p,"ready, compute pipeline selected  commands "); p=udec(p,ER.seq); if(b_rpcs){ p=scopy(p,"  subslices "); p=udec(p,b_ss); p=scopy(p," x "); p=udec(p,b_eu); p=scopy(p," EUs powered"); } p=scopy(p,"  fuses 0x"); p=hx(p,rr(0x9120),8); p=scopy(p," 0x"); p=hx(p,rr(0x9134),8); }
    else { p=scopy(p,"off at step "); p=udec(p,(u32)ER.step); p=scopy(p," - "); p=scopy(p,ER.why[0]?ER.why:(ER.step?"stopped":"not started")); if(ER.step>=7){ p=scopy(p,"  head 0x"); p=hx(p,rr(RCS+0x34),4); p=scopy(p," status 0x"); p=hx(p,rr(RCS+0x234),8); } }
    *p=0; gl(b);
    p=b; p=scopy(p,"Shaders: "); if(g_sh_ok){ p=scopy(p,"ready  fill test "); p=udec(p,SA_W); *p++='x'; p=udec(p,SA_H); p=scopy(p," ok in "); p=udec(p,s_fill_us); p=scopy(p," us, copy test ok in "); p=udec(p,s_copy_us); p=scopy(p," us");
            if(s_groups){ p=scopy(p,", full-screen gradient "); tms(&p,s_scr_us); p=scopy(p," ("); p=udec(p,s_groups); p=scopy(p," blocks)"); } }
    else { p=scopy(p,"off at step "); p=udec(p,(u32)s_step); p=scopy(p," - "); p=scopy(p,s_why[0]?s_why:(s_step?"stopped":"render engine not ready")); }
    *p=0; gl(b);
    p=b; p=scopy(p,"GPU drawing: "); if(g_gfx==GFX_GPU)p=scopy(p,"on"); else if(g_gfx==GFX_EMU)p=scopy(p,"emulated on the CPU (test mode)"); else { p=scopy(p,"off"); if(q_step&&!q_ready){ p=scopy(p," at step "); p=udec(p,(u32)q_step); } p=scopy(p," - "); p=scopy(p,q_why[0]?q_why:(g_gfx_why[0]?g_gfx_why:"not started")); }
    if(g_gfx){ p=scopy(p,"  last frame "); tms(&p,g_gfx_frame_us); p=scopy(p,": CPU "); tms(&p,g_gfx_cpu_us); p=scopy(p,", GPU drawing "); tms(&p,g_gfx_gpu_us); p=scopy(p,", screen copy "); tms(&p,g_gfx_pres_us); p=scopy(p," ("); p=udec(p,g_gfx_subs); p=scopy(p," submissions)"); }
    *p=0; gl(b);
    if(g_gfx){ p=b; p=scopy(p,"  "); p=udec(p,g_gq_frame_ops); p=scopy(p," operations, "); p=udec(p,g_gq_frame_stalls); p=scopy(p," ordering waits, text "); p=udec(p,g_gfx_text_ops); p=scopy(p," operations for "); p=udec(p,g_gfx_text_glyphs); p=scopy(p," characters"); *p=0; gl(b);
        p=b; p=scopy(p,"  windows redrawn: "); p=udec(p,g_lay_full); p=scopy(p," full, "); p=udec(p,g_lay_part); p=scopy(p," partly, "); p=udec(p,g_lay_same); p=scopy(p," unchanged, "); p=udec(p,g_lay_abort); p=scopy(p," by the CPU   blur cache "); p=udec(p,g_blur_hit); p=scopy(p," hit / "); p=udec(p,g_blur_miss); p=scopy(p," miss   shadows "); p=udec(p,g_shd_fast); p=scopy(p," fast / "); p=udec(p,g_shd_slow); p=scopy(p," slow"); *p=0; gl(b);
        { extern int g_dmg_ok, g_dmw, g_dmh; extern u32 g_frames_skipped, g_frames_part; extern char g_fc_msg[]; extern int g_gfx_srec;
          p=b; p=scopy(p,"  screen rebuilt: "); p=udec(p,(u32)g_sr_part); p=scopy(p," partly, "); p=udec(p,(u32)g_sr_full); p=scopy(p," fully, "); p=udec(p,(u32)g_sr_none); p=scopy(p," unchanged ("); p=udec(p,g_frames_skipped); p=scopy(p," frames skipped)");
          p=scopy(p,"   last: "); if(!g_gfx_srec)p=scopy(p,"partial redraws off"); else if(g_dmg_ok){ p=udec(p,(u32)g_dmw); *p++='x'; p=udec(p,(u32)g_dmh); p=scopy(p," pixels, "); p=udec(p,(u32)g_sr_run); *p++='/'; p=udec(p,(u32)g_sr_ops); p=scopy(p," operations run"); } else p=scopy(p,"full");
          *p=0; gl(b);
          p=b; p=scopy(p,"  screen copies by the "); p=scopy(p,g_pres_eng?"shader":"blitter"); p=scopy(p,": "); p=udec(p,g_pres_part); p=scopy(p," partial, "); p=udec(p,g_pres_full); p=scopy(p," full, last "); p=udec(p,g_pres_px); p=scopy(p," pixels");
          if(g_sr_why){ static const char* WY[8]={"","the CPU used the screen while it was drawn","","too many operations","","a buffer changed after it was used","first frame or something changed","too many dependent areas"}; p=scopy(p,"   last full rebuild because "); p=scopy(p,(char*)WY[g_sr_why<8?g_sr_why:0]); }
          *p=0; gl(b);
          if(g_fc_msg[0]){ p=b; p=scopy(p,"  "); p=scopy(p,g_fc_msg); *p=0; gl(b); } }
        { extern int g_wall_fx, g_wall_light; extern u32 g_wall_fx_us; p=b; p=scopy(p,"  wallpaper effects (smooth colours, glow, depth blur, vignette, light): "); p=scopy(p,g_wall_fx?"on":"off"); if(g_wall_fx&&g_wall_fx_us){ p=scopy(p,", made in "); tms(&p,g_wall_fx_us); } p=scopy(p,"   drifting light: "); p=scopy(p,g_wall_light?"on":"off"); *p=0; gl(b); }
        if(g_lay_abort_win>=0){ static const char* WHY[5]={"?","it drew into another buffer (","it read its own window image (","too many operations (","the CPU touched its pixels ("}; p=b; p=scopy(p,"  last CPU-style redraw: window "); p=udec(p,(u32)g_lay_abort_win); p=scopy(p," because "); p=scopy(p,(char*)WHY[g_lay_abort_why<5?g_lay_abort_why:0]); p=scopy(p,(char*)gk_name(g_lay_abort_k)); *p++=')'; *p=0; gl(b); } }
    if(f_ok){ p=b; p=scopy(p,"  GPU clock: now "); p=udec(p,fmhz(gfreq_now())); p=scopy(p," MHz, requested "); p=udec(p,fmhz(f_req)); p=scopy(p," MHz, range "); p=udec(p,fmhz(f_min)); p=scopy(p,"-"); p=udec(p,fmhz(f_max)); p=scopy(p," MHz"); *p=0; gl(b); }
    if(q_ready||q_step>=20){ p=b; p=scopy(p,"  self-test: 400 random operations on "); p=udec(p,GK_N); p=scopy(p," drawing shaders ("); p=udec(p,q_kinsn); p=scopy(p," GPU instructions) "); p=scopy(p,q_ready?(g_gq_serial?"matched the CPU only with a wait between operations (slower mode)":"matched the CPU"):"failed"); p=scopy(p," in "); tms(&p,q_test_us); *p=0; gl(b);
        if(g_gq_diag[0]){ p=b; p=scopy(p,"  "); p=scopy(p,g_gq_diag); *p=0; gl(b); } }
    if(g_gfx){ p=b; p=scopy(p,"  window layers "); p=udec(p,g_gfx_layers); p=scopy(p," ("); p=udec(p,g_gfx_layer_mb); p=scopy(p," MB)  frames "); p=udec(p,g_gfx_frames); p=scopy(p,"  screen copy by "); p=scopy(p,g_gfx_present==2?"shader":g_gfx_present==1?"blitter":"CPU");
        if(q_pres_us[0]){ p=scopy(p," (CPU "); tms(&p,q_pres_us[0]); p=scopy(p,", blitter "); tms(&p,q_pres_us[1]); p=scopy(p,", shader "); tms(&p,q_pres_us[2]); *p++=')'; } *p=0; gl(b);
        p=b; p=scopy(p,"  effects (shadows, rounded corners, glass, animations): "); p=scopy(p,g_gfx_fx?"on":"off"); *p=0; gl(b); }
    gl("  'gpu draw on|off|emu', 'gpu fx on|off', 'gpu blit gpu|cpu', 'gpu check', 'gpu partial on|off', 'gpu wallfx on|off', 'gpu light on|off', 'gpu present blitter|shader'"); }
void igpu_blt_mode(int gpu){ if(gpu&&g_blt_ok&&EB.step>=11)g_blt_present=1; else g_blt_present=0; }
int igpu_info(const char** name,u32* did,u32* ss,u32* eu,u32* mhz,u32* maxmhz,u32* stolen,u32* tearfree){
    if(!G.found)return 0; *name=G.name; *did=G.did; *ss=b_ss; *eu=b_eu; *mhz=f_ok?fmhz(gfreq_now()):0; *maxmhz=f_ok?fmhz(f_max):0; *stolen=G.stolen_mb; *tearfree=(u32)g_igpu_flip; return 1; }
