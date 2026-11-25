#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/drivers/video/fb/gtext.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/drivers/video/fb/gq.h"
#include "include/nvx/drivers/video/fb/gfx.h"
#include "include/nvx/drivers/video/nvidia/nvgsp.h"
#include "include/nvx/drivers/video/nvidia/nvvram.h"
#include "include/nvx/drivers/video/nvidia/nv2dq.h"
#include "include/nvx/drivers/video/nvidia/nvgsp_gen.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/drivers/video/nvidia/nvsettings.h"

int g_nvd_on=0; char g_nvd_why[80];
static u32 nd_gen=0xFFFFFFFFu;
static void nd_cp(char* d,const char* s,int max){ int i=0; while(s[i]&&i<max-1){ d[i]=s[i]; i++; } d[i]=0; }
static void nd_surf(nvcsurf* o,int s){ gsurf* g=&g_gs[s]; o->va=g->gfx; o->pitch=g->pitch; o->w=g->w; o->h=g->h; }
extern int g_nvd_vram; static u8 nd_isv[GQ_MAXS];
static int nd_lay(void){ return g_nvd_on&&g_nvd_vram&&g_vl_on; }
static int nd_src_k(u32 k){ return k==GK_COPY||k==GK_COPYM||k==GK_BLURH||k==GK_BLURV||k==GK_OVER||k==GK_OVERM; }
static int nd_msk_k(u32 k){ return k==GK_MASK||k==GK_COPYM||k==GK_OVERM; }
static void nd_lay_src(const gqop_t* o,nvcop* c){ if(!nd_lay())return; if(nd_src_k(o->k)&&o->s!=o->d&&!nd_isv[o->s])c->s.va=nvl_src(o->s); if(nd_msk_k(o->k)&&o->m!=o->d&&!nd_isv[o->m])c->m.va=(o->m==o->s&&nd_src_k(o->k))?c->s.va:nvl_src(o->m); }
static void nd_lay_dst(const gqop_t* o){ if(nd_lay()&&!nd_isv[o->d])nvl_mark(o->d,(int)o->x,(int)o->y,(int)o->w,(int)o->h,1); }
int g_nvd_2d=0; u32 g_nvd_2d_ops=0, g_nvd_2d_fall=0; static nv2q nd_q; static int nd_last=0;
static void nd_qs(nv2qs* q,int s){ gsurf* g=&g_gs[s]; q->va=g->gfx; q->pitchb=g->pitch*4u; q->w=g->w; q->h=g->h; }
static int nd_2d(const gqop_t* o,int wait){
    if(!g_nvd_2d||!nv2d_ready())return 0;
    int fill=o->k==GK_FILL||(o->k==GK_BLEND&&o->a>=256u), copy=o->k==GK_COPY&&o->a>=256u&&!o->key;
    if(!fill&&!copy)return 0;
    u32 m[NV2Q_OPDW]; nv2qs d; nd_qs(&d,o->d); int n, w=wait||nd_last==1;
    nv2q sv=nd_q;
    if(fill)n=nv2q_fill(&nd_q,m,&d,(int)o->x,(int)o->y,(int)o->w,(int)o->h,o->k==GK_FILL?o->c0:(o->c0&0x00FFFFFFu),w);
    else { nv2qs s; nd_qs(&s,o->s); n=nv2q_copy(&nd_q,m,&d,(int)o->x,(int)o->y,(int)o->w,(int)o->h,&s,(int)o->x+o->sdx,(int)o->y+o->sdy,o->s==o->d,w); }
    if(n<=0||nvc_raw(m,(u32)n)){ nd_q=sv; if(n>0)g_nvd_2d_fall++; return 0; }
    nd_last=2; g_nvd_2d_ops++; return 1; }
static int nd_op(const gqop_t* o,int wait){
    if(o->d>0&&o->d<GQ_MAXS&&o->s>0&&o->s<GQ_MAXS&&g_gs[o->d].gfx&&g_gs[o->s].gfx&&nd_2d(o,wait)){ nd_lay_dst(o); return 0; }
    nvcop c; c.k=o->k; c.x=o->x; c.y=o->y; c.w=o->w; c.h=o->h; c.sdx=o->sdx; c.sdy=o->sdy; c.mdx=o->mdx; c.mdy=o->mdy; c.c0=o->c0; c.c1=o->c1; c.a=o->a; c.inv=o->inv; c.key=o->key; c.goff=o->goff;
    if(o->d<=0||o->d>=GQ_MAXS||o->s<=0||o->s>=GQ_MAXS||o->m<=0||o->m>=GQ_MAXS){ nvc_abort(); return -1; }
    nd_surf(&c.d,o->d); nd_surf(&c.s,o->s); nd_surf(&c.m,o->m); nd_lay_src(o,&c);
    if(!c.d.va||!c.s.va||!c.m.va||nvc_op(&c,wait||nd_last==2)){ nvc_abort(); nvl_batch_end(0); nv2q_reset(&nd_q); nd_last=0; return -1; }
    nd_last=1; nd_lay_dst(o); return 0; }
static int nd_submit(u32 n){ (void)n; nv2q_reset(&nd_q); nd_last=0; int r=nvc_submit(); nvl_batch_end(r==0); return r; }
static void nd_hw(void){ nv2q_reset(&nd_q); nd_last=0; u8* p=(u8*)&g_gqhw; for(u32 i=0;i<sizeof(g_gqhw);i++)p[i]=0; g_gqhw.op=nd_op; g_gqhw.submit=nd_submit; g_gqhw.now=hr_ns; g_gqhw.ring_cap=0x40000000u; }
static u8* nd_mem(u64 bytes,void** raw){ u8* r=(u8*)kmalloc((unsigned long)(bytes+8192ULL)); *raw=r; if(!r)return 0; u8* p=(u8*)(((u64)r+4095ULL)&~4095ULL); for(u64 k=0;k<(bytes+7)/8;k++)((u64*)p)[k]=0; return p; }
static struct { void* raw; u64 va, len; } nd_s[GQ_MAXS]; u32 g_nvd_freed=0, g_nvd_live=0;
static void nd_free(int s){ nvl_free(s); if(s<=0||s>=GQ_MAXS||!nd_s[s].raw)return; gq_sync(); nvgsp_ce_unmap(nd_s[s].va,nd_s[s].len); kfree(nd_s[s].raw); nd_s[s].raw=0; nd_s[s].va=0; nd_s[s].len=0; g_nvd_freed++; if(g_nvd_live)g_nvd_live--; }
static int nd_alloc(u32 w,u32 h,u32 pitch,u32** px){
    gq_sync(); g_gq_free_hook=nd_free; u64 bytes=(u64)pitch*h*4; if(!bytes)bytes=4; void* raw; u8* p=nd_mem(bytes,&raw); if(!p){ if(raw)kfree(raw); return -1; }
    u64 va=nvgsp_ce_map((u64)p,bytes); if(!va){ kfree(raw); return -1; }
    int s=gq_surf((u32*)p,va,w,h,pitch); if(s<0){ nvgsp_ce_unmap(va,bytes); kfree(raw); return -1; } nd_s[s].raw=raw; nd_s[s].va=va; nd_s[s].len=bytes; g_nvd_live++; *px=(u32*)p; return s; }
int g_nvd_vram=0, g_nvd_vram_n=0; static u32 nd_vgen=0xFFFFFFFFu;
static int nd_alloc_vram(u32 w,u32 h,u32 pitch,u32** px){
    if(!g_nvd_vram)return nd_alloc(w,h,pitch,px);
    gq_sync(); u64 va=0; u32* cp=0; u64 bytes=(u64)pitch*h*4;
    if(!nvgsp_vram_surface(bytes,&va,&cp))return nd_alloc(w,h,pitch,px);
    int s=gq_surf(cp,va,w,h,pitch); if(s<0)return nd_alloc(w,h,pitch,px);
    nd_isv[s]=1; g_nvd_vram_n++; nd_vgen=nvgsp_ce_gen(); *px=cp; return s; }
static int nd_vram_stale(void){ int any=0; for(int i=1;i<GQ_MAXS;i++){ if(nd_isv[i]&&!g_gs[i].used){ nd_isv[i]=0; if(g_nvd_vram_n>0)g_nvd_vram_n--; } if(nd_isv[i])any=1; } return any&&nd_vgen!=nvgsp_ce_gen(); }
static int nd_remap(void){
    u32 gen=nvgsp_ce_gen(); if(gen==nd_gen)return 1;
    for(int i=1;i<GQ_MAXS;i++){ gsurf* g=&g_gs[i]; if(!g->used||!g->px||nd_isv[i])continue; u64 va=nvgsp_ce_map((u64)g->px,(u64)g->pitch*g->h*4); if(!va)return 0; g->gfx=va; nd_s[i].va=va; nd_s[i].len=(u64)g->pitch*g->h*4; }
    nd_gen=gen; return 1; }
static void nd_stopped(void){ nvc_abort(); nvl_drop_all(1); g_gq_mark_hook=0; g_gfx_atlas=0; g_nvd_on=0; g_gfx_alloc_gpu=0; }
static const u32 TW[4]={160,96,136,64}, TH[4]={120,72,100,64}, TP[4]={160,96,144,64};
static int nd_selftest1(char* why,int max,u32* us){
    *us=0; why[0]=0;
    if(g_gfx==GFX_GPU&&!g_nvd_on){ nd_cp(why,"another GPU is drawing the desktop",max); return 0; }
    if(g_gfx!=GFX_OFF){ gfx_stop("the NVIDIA GPU was restarted"); }
    g_nvd_on=0; nvc_set_ok(1); nd_hw(); gq_set_pre(0);
    void* raw[4]={0,0,0,0}; u64 gva[4]={0,0,0,0}, glen[4]={0,0,0,0}; u32* gp[4]; u32* tc[4]={0,0,0,0}; int sid[4]={-1,-1,-1,-1}; int ok=0; const char* w=0; char sw[64]; sw[0]=0;
    for(int k=0;k<4;k++){ u64 b=(u64)TP[k]*TH[k]*4; gp[k]=(u32*)nd_mem(b,&raw[k]); tc[k]=(u32*)kmalloc((unsigned long)b); if(!gp[k]||!tc[k]){ w="out of memory for test images"; goto done; }
        u64 va=nvgsp_ce_map((u64)gp[k],b); if(!va){ w="could not map the test images"; goto done; } gva[k]=va; glen[k]=b;
        sid[k]=gq_surf(gp[k],va,TW[k],TH[k],TP[k]); if(sid[k]<0){ w="no free surface slots"; goto done; }
        u32 rs=0x9E3779B9u*(u32)(k+1); for(u32 i=0;i<TP[k]*TH[k];i++){ rs^=rs<<13; rs^=rs>>17; rs^=rs<<5; u32 v=rs; if(k==3)v=rs%257u; else if((rs>>8)%7u==0)v|=0xFF000000u; else v&=0x00FFFFFFu; gp[k][i]=v; tc[k][i]=v; } }
    g_gq_mode=GQM_GPU; u64 t0=hr_ns();
    gq_fill(sid[0],0,0,8,8,0x00123456u); if(gq_sync()){ w="the first compute batch did not finish"; goto done; }
    if(gp[0][0]!=0x00123456u||gp[0][7*160+7]!=0x00123456u){ char* q=sw; q=scopy(q,"the first fill wrote nothing (0x"); q=hx(q,gp[0][0],8); *q++=')'; *q=0; w=sw; goto done; }
    for(int pass=0;pass<2;pass++){ u32 base=pass?0x00508000u:0x00A01000u; for(int i=0;i<64;i++)gp[1][i]=base+(u32)i;
        gq_copy(sid[2],0,0,64,1,sid[1],0,0,256,0); if(gq_sync()){ w="the cache test did not finish"; goto done; }
        for(int i=0;i<64;i++) if(gp[2][i]!=base+(u32)i){ w=pass?"the GPU kept old data in its cache":"the GPU did not read what the CPU wrote"; goto done; } }
    for(int k=0;k<3;k++) for(u32 i=0;i<TP[k]*TH[k];i++) tc[k][i]=gp[k][i];
    for(int i=0;i<64;i++)gp[0][i]=0x00ABCDEFu; gq_fill(sid[0],0,0,64,1,0x00FEDCBAu); if(gq_sync()){ w="the coherency test did not finish"; goto done; }
    for(int i=0;i<64;i++) if(gp[0][i]!=0x00FEDCBAu){ w="the CPU does not see what the GPU drew"; goto done; }
    for(int i=0;i<64;i++) tc[0][i]=0x00FEDCBAu;
    { char t[64]; int r=gq_selftest(sid,tc,3,sid[3],0xC0FFEEu,400,t); if(r<0){ char* q=sw; q=scopy(q,r==-2?"":"wrong pixel: "); q=scopy(q,t); *q=0; w=sw; goto done; } }
    *us=(u32)((hr_ns()-t0)/1000ULL); ok=1;
    done:
    gq_abort(); nvc_abort(); g_gq_mode=GQM_OFF;
    for(int k=0;k<4;k++){ if(sid[k]>0)gq_surf_free(sid[k]); if(raw[k]&&ok){ if(gva[k])nvgsp_ce_unmap(gva[k],glen[k]); kfree(raw[k]); } if(tc[k])kfree(tc[k]); }
    if(!ok){ nd_cp(why,w?w:nvc_why(),max); nvc_set_ok(0); }
    return ok; }
int nvd_selftest(char* why,int max,u32* us){ g_nvd_2d=nv2d_ready()&&nvset_allowed(NVSET_2D); u32 o0=g_nvd_2d_ops; int r=nd_selftest1(why,max,us); if(r&&g_nvd_2d&&g_nvd_2d_ops==o0)g_nvd_2d=0; if(r||!g_nvd_2d){ if(!r&&g_nvd_2d)nvset_note(NVSET_2D,NVSET_ST_ACTIVE,0); return r; } g_nvd_2d=0; nvset_try_end(NVSET_2D,0,"the drawing self-test failed with the 2D engine"); klog("nvgsp: GPU drawing self-test failed with 2D engine fills and copies - trying compute shaders only"); return nd_selftest1(why,max,us); }
void nvd_halt(const char* why){ if(g_nvd_on&&g_gfx==GFX_GPU)gfx_stop(why); g_nvd_on=0; nvc_abort(); nvl_drop_all(1); g_gq_mark_hook=0; }
int nvd_start(void){
    g_nvd_why[0]=0;
    if(!nvc_ready()){ nd_cp(g_nvd_why,nvc_why()[0]?nvc_why():"run nvgsp all first",80); return -1; }
    if(g_nvd_on&&g_gfx==GFX_GPU)return 0;
    if(g_gfx==GFX_GPU){ nd_cp(g_nvd_why,"another GPU is drawing the desktop",80); return -1; }
    if(g_gfx==GFX_EMU)gfx_stop("switching");
    if(nd_vram_stale()){ gfx_forget(); for(int i=1;i<GQ_MAXS;i++)nd_isv[i]=0; g_nvd_vram_n=0; }
    nd_hw(); if(!nd_remap()){ nd_cp(g_nvd_why,"could not map the drawing buffers for the GPU",80); return -1; }
    g_gfx_alloc_gpu=nd_alloc_vram; g_gfx_atlas=1;
    if(gfx_start(GFX_GPU,nd_alloc,0,0)){ g_gfx_alloc_gpu=0; g_gfx_atlas=0; nd_cp(g_nvd_why,g_gfx_why,80); return -1; }
    nd_gen=nvgsp_ce_gen(); g_nvd_on=1; g_gq_mark_hook=nvl_cpu_mark; { extern void (*g_gfx_on_stop)(void); g_gfx_on_stop=nd_stopped; }
    klog("nvgsp: the desktop is drawn by the NVIDIA GPU (compute shaders)"); return 0; }
int nvd_desktop(void){ int r=nvd_start(); if(r){ char b[120],*p=b; p=scopy(p,"nvgsp: GPU drawing not started - "); p=scopy(p,g_nvd_why); *p=0; klog(b); } return r; }
static int nd_pending=0, nd_trial=0, nd_auto_done=0; static u32 nd_f0=0; static u64 nd_t_desk=0;
int g_nvd_auto=0, g_nvd_auto_skipped=0;
void nvd_request(void){ nd_pending=1; }
static int nd_flag_get(const char* nm){ extern int fs_find_name(const char*); extern char* fsd(int); int k=fs_find_name(nm); if(k<0)return -1; char* d=fsd(k); return d?d[0]:-1; }
static void nd_flag_set(const char* nm,char v){ extern int fs_save_bin(const char*,const u8*,int,int); extern void nvx3_sync_all(void); u8 b[2]={(u8)v,'\n'}; fs_save_bin(nm,b,2,0); nvx3_sync_all(); }
static int nd_usb_flag(void){ extern int msd_ok, ufat_ok, ufs_n; extern int msd_autoinit_safe(void); extern int ufat_read(int,char*,int); extern struct nvx_ufs_t ufs[32];
    if(!(msd_ok||msd_autoinit_safe())||!ufat_ok)return -1;
    for(int i=0;i<ufs_n;i++){ const char* a=ufs[i].name; const char* b="NVSTART.TXT"; int k=0; while(a[k]&&b[k]&&(a[k]|32)==(b[k]|32))k++; if(!a[k]&&!b[k]){ char c[4]; if(ufat_read(i,c,2)>=1)return c[0]; return -1; } }
    return -1; }
static void nd_usb_set(char v){ extern int msd_ok, ufat_ok; extern int ufat_write_file(const char*,const char*,int); if(msd_ok&&ufat_ok){ char b[2]={v,'\n'}; ufat_write_file("NVSTART.TXT",b,2); } }
static int nd_turing(void){ extern u16 g_nv_dev; nvd_ampere(); return nvgsp_gen_pci_ok(g_nv_dev); }
static int nd_amp_loaded=0;
int nvd_ampere(void){ if(!nd_amp_loaded){ nd_amp_loaded=1; int c=nd_flag_get("nvgsp-ampere.cfg"); g_nv_ampere_on=c=='1'||(c!='0'&&nvset_allowed(NVSET_AMPERE)); } return g_nv_ampere_on; }
const char* nvd_ampere_cmd(const char* a){
    while(*a&&*a!=' '){ a++; } while(*a==' '){ a++; }
    nvd_ampere();
    if(a[0]=='o'&&a[1]=='n'){ nvset_set(NVSET_AMPERE,1); g_nv_ampere_on=1; nd_flag_set("nvgsp-ampere.cfg",'1'); }
    else if(a[0]=='o'&&a[1]=='f'){ nvset_set(NVSET_AMPERE,0); g_nv_ampere_on=0; nd_flag_set("nvgsp-ampere.cfg",'0'); }
    return g_nv_ampere_on?"gpu ampere: on - NVIDIA RTX 30 (GA102-GA107) GSP boot is allowed (untested hardware path; nvgsp boot, and nvgsp auto if it is on)\n":"gpu ampere: off - RTX 30 cards are left alone (gpu ampere on to try the untested GSP boot)\n"; }
void nvd_flip_remember(int on){ if(on&&nvset_auto_off(NVSET_MODESET))nvset_reset(NVSET_MODESET); if(nd_flag_get("nvgsp-flip.cfg")!=(on?'1':'0'))nd_flag_set("nvgsp-flip.cfg",on?'1':'0'); }
void nvd_mode_remember(int on){ if(nd_flag_get("nvgsp-mode.cfg")!=(on?'1':'0'))nd_flag_set("nvgsp-mode.cfg",on?'1':'0'); }
void nvd_set_auto(int on){ g_nvd_auto=on; nd_flag_set("nvgsp-auto.cfg",on?'1':'0'); }
static int nd_mode_due=0; int g_nvd_boot=0;
extern int nvcur_chan_init(int) __attribute__((weak)); extern int g_nvcur_enable __attribute__((weak));
static int nd_flip_wanted(void){ int c=nd_flag_get("nvgsp-flip.cfg"); return c=='1'||(c!='0'&&nvset_allowed(NVSET_MODESET)); }
static int nd_ada(void){ extern u16 g_nv_dev; return g_nv_dev>=0x2680u&&g_nv_dev<=0x28FFu; }
static int nd_ada_start(void){ extern int nvgsp_cmd(const char*); extern int g_nv_ada_on __attribute__((weak));
    if(!&g_nv_ada_on||!nvset_try_begin(NVSET_ADA)){ klog("nvgsp: RTX 40 (Ada) GSP start is off - the card stays on the firmware picture"); return 0; }
    if(nd_flag_get("nvgsp-start.flag")=='1'||nd_usb_flag()=='1'){ g_nvd_auto_skipped=1; nd_flag_set("nvgsp-start.flag",'0'); nd_usb_set('0'); nvset_try_end(NVSET_ADA,0,"the RTX 40 start-up did not finish last time"); klog("nvgsp: the RTX 40 start-up did not finish last time - skipped this boot"); return 0; }
    g_nv_ada_on=1; nd_flag_set("nvgsp-start.flag",'1'); nd_usb_set('1');
    klog("nvgsp: trying the untested RTX 40 (Ada) GSP start (booter, GSP-RM, copy engine and 2D; native mode, page flips and cursor follow if gpu modeset is on)"); int r=nvgsp_cmd("all");
    nvset_try_end(NVSET_ADA,r>0,"Ada GSP start stopped (see nvgsp log) - the firmware picture stays");
    if(r<=0)klog("nvgsp: Ada GSP start stopped - the screen stays on the firmware picture");
    if(r>0&&(nd_flag_get("nvgsp-mode.cfg")=='1'||nd_flip_wanted()||nvset_allowed(NVSET_CURSOR)))nd_mode_due=1;
    nd_flag_set("nvgsp-start.flag",'0'); nd_usb_set('0'); return r>0?1:-1; }
static void nd_mode_restore(void){ extern int nvgsp_mode(int); nd_mode_due=0; nd_flag_set("nvgsp-start.flag",'1'); nd_usb_set('1');
    if(nd_flag_get("nvgsp-mode.cfg")=='1'){ klog("nvgsp: turning the second screen on as it was last time"); nd_flag_set("nvgsp-mode.cfg",'0'); if(nvgsp_mode(0))nd_flag_set("nvgsp-mode.cfg",'1'); }
    if(nd_flip_wanted()&&nvset_try_begin(NVSET_MODESET)){ extern int nvflip_on(void); extern int g_nvflip_want; g_nvflip_want=1; klog("nvgsp: gpu modeset is on - main screen to its native mode"); nd_flag_set("nvgsp-flip.cfg",'0'); int ok=nvflip_on(); if(!ok)klog("nvgsp: main screen stays on the firmware picture"); nvset_try_end(NVSET_MODESET,ok,"the main screen stayed on the firmware picture (see nvgsp log)"); nd_flag_set("nvgsp-flip.cfg",'1'); }
    if(nvcur_chan_init&&&g_nvcur_enable&&nvset_try_begin(NVSET_CURSOR)){ g_nvcur_enable=1; int e=nvcur_chan_init(0); if(e)g_nvcur_enable=0; nvset_try_end(NVSET_CURSOR,!e,"the cursor channel did not start (needs a Turing, RTX 30 or RTX 40 card with its display running)"); }
    nd_flag_set("nvgsp-start.flag",'0'); nd_usb_set('0'); }
static int nd_gsp_start(void){
    extern int nv_probe(void); extern int nvgsp_cmd(const char*);
    nd_auto_done=1;
    nvset_load(); { int c=nd_flag_get("nvgsp-auto.cfg"); if(c=='1'||(c!='0'&&nvset_allowed(NVSET_BOOTSTART)))g_nvd_auto=1; }
    if(!g_nvd_auto){ klog("nvgsp: automatic NVIDIA start is off (run nvgsp all once, or nvgsp auto on to start it at every boot)"); return 0; }
    if(!nv_probe())return 0;
    if(nd_ada())return nd_ada_start();
    if(!nd_turing())return 0;
    if(nd_flag_get("nvgsp-start.flag")=='1'||nd_usb_flag()=='1'){ g_nvd_auto_skipped=1; nd_flag_set("nvgsp-start.flag",'0'); nd_usb_set('0'); klog("nvgsp: the NVIDIA start-up did not finish last time - skipped this boot (press G in Gears or run nvgsp all to try again)"); return 0; }
    nd_flag_set("nvgsp-start.flag",'1'); nd_usb_set('1');
    { extern u16 g_nv_dev; int amp=g_nv_dev>=NVGSP_PCI_GA102_LO&&g_nv_dev<=NVGSP_PCI_GA107_HI; int bs=nvset_try_begin(NVSET_BOOTSTART), at=amp&&nvset_try_begin(NVSET_AMPERE);
    klog("nvgsp: starting the NVIDIA GPU for 2D acceleration"); int r=nvgsp_cmd("all");
    if(bs)nvset_try_end(NVSET_BOOTSTART,r>0,"the GPU start-up stopped - the CPU draws the desktop");
    if(at)nvset_try_end(NVSET_AMPERE,r>0,"Ampere GSP start stopped (the GA102 GSP image must be copied into Files; drawing is not ported yet)");
    if(r>0&&(nd_flag_get("nvgsp-mode.cfg")=='1'||nd_flip_wanted()||nvset_allowed(NVSET_CURSOR)))nd_mode_due=1;
    nd_flag_set("nvgsp-start.flag",'0'); nd_usb_set('0'); return r>0?1:-1; } }
void nvd_boot_start(void){
    extern int drv_begin(const char*); extern void drv_end(void);
    if(nd_auto_done)return;
    if(drv_begin("nvgsp")){ g_nvd_boot=1; klog("nvgsp: NVIDIA start-up during the boot splash"); g_nvd_boot=nd_gsp_start()>0?2:3; } drv_end(); }
static void nd_auto(void){
    extern volatile u64 g_ticks;
    if(nd_auto_done&&!nd_mode_due)return; if(!nd_t_desk){ nd_t_desk=g_ticks?g_ticks:1; return; }
    if(nd_mode_due){ if(g_ticks>=nd_t_desk+100)nd_mode_restore(); return; }
    if(g_ticks<nd_t_desk+300)return;
    nd_gsp_start(); if(nd_mode_due)nd_mode_restore(); }
static int nd_cm=0; static u32 nd_cm_f0=0, nd_cm_vf=0;
static void nd_cardmem(void){
    int live=g_nvd_on&&g_gfx==GFX_GPU&&nd_lay();
    if(g_vl_fail<nd_cm_vf){ nd_cm_vf=g_vl_fail; nd_cm=0; }
    while(nd_cm_vf<g_vl_fail){ nd_cm_vf++; if(nd_cm==1)nd_cm=2; nvset_try_end(NVSET_CARDMEM,0,"copying a window layer into video memory failed - it is read from RAM"); }
    if(nd_cm==1&&!live){ nvset_try_cancel(NVSET_CARDMEM); nd_cm=0; return; }
    if(nd_cm==1&&g_gfx_frames-nd_cm_f0>=5){ nvset_try_end(NVSET_CARDMEM,1,0); nd_cm=2; return; }
    if(!nd_cm&&live&&nvset_try_begin(NVSET_CARDMEM)){ nd_cm=1; nd_cm_f0=g_gfx_frames; } }
static int nd_run=0, nd_run_res=0;
void nvd_run_request(void){ if(!nd_run)nd_run=1; }
int nvd_run_state(int* res){ if(nd_run)return 1; *res=nd_run_res; return 0; }
void nvd_poll(void){
    extern int nvgsp_cmd(const char*);
    nvgsp_tick();
    if(nd_run==1){ nd_run=2; nd_run_res=nvgsp_cmd("all"); nd_run=0; return; }
    nd_auto(); nd_cardmem();
    if(nd_pending){ nd_pending=0; if(nvd_desktop()==0){ nd_trial=5; nd_f0=g_gfx_frames; } return; }
    if(g_nvd_on&&g_gfx==GFX_GPU&&!nd_trial){ static u32 wf=0; static int slow=0;
        { extern int g_gfx_nomem; if(g_gfx_nomem){ slow=0; gfx_stop("not enough memory for a window layer - the CPU draws"); klog("nvgsp: not enough memory for a window layer - the desktop is drawn by the CPU again"); return; } }
        if(g_gq_mode!=GQM_GPU){ slow=0; gfx_stop("a GPU drawing batch failed - the CPU draws"); klog("nvgsp: a GPU drawing batch failed - the desktop is drawn by the CPU again"); return; }
        if(g_gfx_frames!=wf){ wf=g_gfx_frames; static u32 ring[30]; static int ri=0, rn=0; static u64 sum=0; if(rn==30)sum-=ring[ri]; else rn++; ring[ri]=g_gfx_frame_us; sum+=g_gfx_frame_us; ri=(ri+1)%30;
            if(rn==30&&sum/30>40000u){ char b[100],*p=b; p=scopy(p,"GPU drawing averaged "); p=udec(p,(u32)(sum/30000u)); p=scopy(p," ms per frame over 30 frames - the CPU draws"); *p=0; rn=0; ri=0; sum=0; slow=0; gfx_stop(b); klog(b); return; }
            if(g_gfx_frame_us>150000u){ if(++slow>=3){ char b[100],*p=b; p=scopy(p,"GPU drawing too slow ("); p=udec(p,g_gfx_frame_us/1000u); p=scopy(p," ms per frame) - the CPU draws"); *p=0; gfx_stop(b); klog(b); slow=0; return; } } else slow=0; } }
    if(nd_trial&&g_nvd_on){ if(g_gfx!=GFX_GPU){ nd_trial=0; return; }
        if(g_gfx_frames!=nd_f0){ nd_f0=g_gfx_frames; if(nd_trial<5&&g_gfx_frame_us>150000u){ char b[100],*p=b; p=scopy(p,"GPU drawing too slow on the first frames ("); p=udec(p,g_gfx_frame_us/1000u); p=scopy(p," ms) - the CPU draws"); *p=0; gfx_stop(b); klog(b); nd_trial=0; return; } nd_trial--; } } }
