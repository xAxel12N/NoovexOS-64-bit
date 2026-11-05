#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/drivers/video/fb/gq.h"
#include "include/nvx/drivers/video/fb/gfx.h"
#include "include/nvx/drivers/video/nvidia/nvgsp.h"
#include "include/nvx/drivers/usb/xhci.h"

int g_nvd_on=0; char g_nvd_why[80];
static u32 nd_gen=0xFFFFFFFFu;
static void nd_cp(char* d,const char* s,int max){ int i=0; while(s[i]&&i<max-1){ d[i]=s[i]; i++; } d[i]=0; }
static void nd_surf(nvcsurf* o,int s){ gsurf* g=&g_gs[s]; o->va=g->gfx; o->pitch=g->pitch; o->w=g->w; o->h=g->h; }
static int nd_op(const gqop_t* o,int wait){
    nvcop c; c.k=o->k; c.x=o->x; c.y=o->y; c.w=o->w; c.h=o->h; c.sdx=o->sdx; c.sdy=o->sdy; c.mdx=o->mdx; c.mdy=o->mdy; c.c0=o->c0; c.c1=o->c1; c.a=o->a; c.inv=o->inv; c.key=o->key; c.goff=o->goff;
    if(o->d<=0||o->d>=GQ_MAXS||o->s<=0||o->s>=GQ_MAXS||o->m<=0||o->m>=GQ_MAXS){ nvc_abort(); return -1; }
    nd_surf(&c.d,o->d); nd_surf(&c.s,o->s); nd_surf(&c.m,o->m);
    if(!c.d.va||!c.s.va||!c.m.va||nvc_op(&c,wait)){ nvc_abort(); return -1; }
    return 0; }
static int nd_submit(u32 n){ (void)n; return nvc_submit(); }
static void nd_hw(void){ u8* p=(u8*)&g_gqhw; for(u32 i=0;i<sizeof(g_gqhw);i++)p[i]=0; g_gqhw.op=nd_op; g_gqhw.submit=nd_submit; g_gqhw.now=hr_ns; g_gqhw.ring_cap=0x40000000u; }
static u8* nd_mem(u64 bytes,void** raw){ u8* r=(u8*)kmalloc((unsigned long)(bytes+8192ULL)); *raw=r; if(!r)return 0; u8* p=(u8*)(((u64)r+4095ULL)&~4095ULL); for(u64 k=0;k<(bytes+7)/8;k++)((u64*)p)[k]=0; return p; }
static struct { void* raw; u64 va, len; } nd_s[GQ_MAXS]; u32 g_nvd_freed=0, g_nvd_live=0;
static void nd_free(int s){ if(s<=0||s>=GQ_MAXS||!nd_s[s].raw)return; gq_sync(); nvgsp_ce_unmap(nd_s[s].va,nd_s[s].len); kfree(nd_s[s].raw); nd_s[s].raw=0; nd_s[s].va=0; nd_s[s].len=0; g_nvd_freed++; if(g_nvd_live)g_nvd_live--; }
static int nd_alloc(u32 w,u32 h,u32 pitch,u32** px){
    gq_sync(); g_gq_free_hook=nd_free; u64 bytes=(u64)pitch*h*4; if(!bytes)bytes=4; void* raw; u8* p=nd_mem(bytes,&raw); if(!p){ if(raw)kfree(raw); return -1; }
    u64 va=nvgsp_ce_map((u64)p,bytes); if(!va){ kfree(raw); return -1; }
    int s=gq_surf((u32*)p,va,w,h,pitch); if(s<0){ nvgsp_ce_unmap(va,bytes); kfree(raw); return -1; } nd_s[s].raw=raw; nd_s[s].va=va; nd_s[s].len=bytes; g_nvd_live++; *px=(u32*)p; return s; }
int g_nvd_vram=0, g_nvd_vram_n=0; static u8 nd_isv[GQ_MAXS]; static u32 nd_vgen=0xFFFFFFFFu;
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
static void nd_stopped(void){ nvc_abort(); g_nvd_on=0; g_gfx_alloc_gpu=0; }
static const u32 TW[4]={160,96,136,64}, TH[4]={120,72,100,64}, TP[4]={160,96,144,64};
int nvd_selftest(char* why,int max,u32* us){
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
void nvd_halt(const char* why){ if(g_nvd_on&&g_gfx==GFX_GPU)gfx_stop(why); g_nvd_on=0; nvc_abort(); }
int nvd_start(void){
    g_nvd_why[0]=0;
    if(!nvc_ready()){ nd_cp(g_nvd_why,nvc_why()[0]?nvc_why():"run nvgsp all first",80); return -1; }
    if(g_nvd_on&&g_gfx==GFX_GPU)return 0;
    if(g_gfx==GFX_GPU){ nd_cp(g_nvd_why,"another GPU is drawing the desktop",80); return -1; }
    if(g_gfx==GFX_EMU)gfx_stop("switching");
    if(nd_vram_stale()){ gfx_forget(); for(int i=1;i<GQ_MAXS;i++)nd_isv[i]=0; g_nvd_vram_n=0; }
    nd_hw(); if(!nd_remap()){ nd_cp(g_nvd_why,"could not map the drawing buffers for the GPU",80); return -1; }
    g_gfx_alloc_gpu=nd_alloc_vram;
    if(gfx_start(GFX_GPU,nd_alloc,0,0)){ g_gfx_alloc_gpu=0; nd_cp(g_nvd_why,g_gfx_why,80); return -1; }
    nd_gen=nvgsp_ce_gen(); g_nvd_on=1; { extern void (*g_gfx_on_stop)(void); g_gfx_on_stop=nd_stopped; }
    klog("nvgsp: the desktop is drawn by the NVIDIA GPU (compute shaders)"); return 0; }
int nvd_desktop(void){ int r=nvd_start(); if(r){ char b[120],*p=b; p=scopy(p,"nvgsp: GPU drawing not started - "); p=scopy(p,g_nvd_why); *p=0; klog(b); } return r; }
static int nd_pending=0, nd_trial=0, nd_auto_done=0; static u32 nd_f0=0; static u64 nd_t_desk=0;
int g_nvd_auto=1, g_nvd_auto_skipped=0;
void nvd_request(void){ nd_pending=1; }
static int nd_flag_get(const char* nm){ extern int fs_find_name(const char*); extern char* fsd(int); int k=fs_find_name(nm); if(k<0)return -1; char* d=fsd(k); return d?d[0]:-1; }
static void nd_flag_set(const char* nm,char v){ extern int fs_save_bin(const char*,const u8*,int,int); extern void nvx3_sync_all(void); u8 b[2]={(u8)v,'\n'}; fs_save_bin(nm,b,2,0); nvx3_sync_all(); }
static int nd_usb_flag(void){ extern int msd_ok, ufat_ok, ufs_n; extern int msd_autoinit_safe(void); extern int ufat_read(int,char*,int); extern struct nvx_ufs_t ufs[32];
    if(!(msd_ok||msd_autoinit_safe())||!ufat_ok)return -1;
    for(int i=0;i<ufs_n;i++){ const char* a=ufs[i].name; const char* b="NVSTART.TXT"; int k=0; while(a[k]&&b[k]&&(a[k]|32)==(b[k]|32))k++; if(!a[k]&&!b[k]){ char c[4]; if(ufat_read(i,c,2)>=1)return c[0]; return -1; } }
    return -1; }
static void nd_usb_set(char v){ extern int msd_ok, ufat_ok; extern int ufat_write_file(const char*,const char*,int); if(msd_ok&&ufat_ok){ char b[2]={v,'\n'}; ufat_write_file("NVSTART.TXT",b,2); } }
static int nd_turing(void){ extern u16 g_nv_dev; u32 d=g_nv_dev; return (d>=0x1E00u&&d<=0x1FFFu)||(d>=0x2180u&&d<=0x21FFu); }
void nvd_set_auto(int on){ g_nvd_auto=on; nd_flag_set("nvgsp-auto.cfg",on?'1':'0'); }
static void nd_auto(void){
    extern int nv_probe(void); extern int nvgsp_cmd(const char*); extern volatile u64 g_ticks;
    if(nd_auto_done)return; if(!nd_t_desk){ nd_t_desk=g_ticks?g_ticks:1; return; } if(g_ticks<nd_t_desk+300)return;
    nd_auto_done=1;
    if(nd_flag_get("nvgsp-auto.cfg")=='0'){ g_nvd_auto=0; return; }
    if(!nv_probe()||!nd_turing())return;
    if(nd_flag_get("nvgsp-start.flag")=='1'||nd_usb_flag()=='1'){ g_nvd_auto_skipped=1; nd_flag_set("nvgsp-start.flag",'0'); nd_usb_set('0'); klog("nvgsp: the NVIDIA start-up did not finish last time - skipped this boot (press G in Gears or run nvgsp all to try again)"); return; }
    nd_flag_set("nvgsp-start.flag",'1'); nd_usb_set('1');
    klog("nvgsp: starting the NVIDIA GPU for 2D acceleration"); nvgsp_cmd("all");
    nd_flag_set("nvgsp-start.flag",'0'); nd_usb_set('0'); }
static int nd_run=0, nd_run_res=0;
void nvd_run_request(void){ if(!nd_run)nd_run=1; }
int nvd_run_state(int* res){ if(nd_run)return 1; *res=nd_run_res; return 0; }
void nvd_poll(void){
    extern int nvgsp_cmd(const char*);
    nvgsp_tick();
    if(nd_run==1){ nd_run=2; nd_run_res=nvgsp_cmd("all"); nd_run=0; return; }
    if(g_nvd_auto)nd_auto();
    if(nd_pending){ nd_pending=0; if(nvd_desktop()==0){ nd_trial=5; nd_f0=g_gfx_frames; } return; }
    if(g_nvd_on&&g_gfx==GFX_GPU&&!nd_trial){ static u32 wf=0; static int slow=0;
        { extern int g_gfx_nomem; if(g_gfx_nomem){ slow=0; gfx_stop("not enough memory for a window layer - the CPU draws"); klog("nvgsp: not enough memory for a window layer - the desktop is drawn by the CPU again"); return; } }
        if(g_gq_mode!=GQM_GPU){ slow=0; gfx_stop("a GPU drawing batch failed - the CPU draws"); klog("nvgsp: a GPU drawing batch failed - the desktop is drawn by the CPU again"); return; }
        if(g_gfx_frames!=wf){ wf=g_gfx_frames; static u32 ring[30]; static int ri=0, rn=0; static u64 sum=0; if(rn==30)sum-=ring[ri]; else rn++; ring[ri]=g_gfx_frame_us; sum+=g_gfx_frame_us; ri=(ri+1)%30;
            if(rn==30&&sum/30>40000u){ char b[100],*p=b; p=scopy(p,"GPU drawing averaged "); p=udec(p,(u32)(sum/30000u)); p=scopy(p," ms per frame over 30 frames - the CPU draws"); *p=0; rn=0; ri=0; sum=0; slow=0; gfx_stop(b); klog(b); return; }
            if(g_gfx_frame_us>150000u){ if(++slow>=3){ char b[100],*p=b; p=scopy(p,"GPU drawing too slow ("); p=udec(p,g_gfx_frame_us/1000u); p=scopy(p," ms per frame) - the CPU draws"); *p=0; gfx_stop(b); klog(b); slow=0; return; } } else slow=0; } }
    if(nd_trial&&g_nvd_on){ if(g_gfx!=GFX_GPU){ nd_trial=0; return; }
        if(g_gfx_frames!=nd_f0){ nd_f0=g_gfx_frames; if(nd_trial<5&&g_gfx_frame_us>150000u){ char b[100],*p=b; p=scopy(p,"GPU drawing too slow on the first frames ("); p=udec(p,g_gfx_frame_us/1000u); p=scopy(p," ms) - the CPU draws"); *p=0; gfx_stop(b); klog(b); nd_trial=0; return; } nd_trial--; } } }
