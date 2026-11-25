#include <stdio.h>
#include <string.h>
#include "drivers/video/nvidia/nvcursor.c"
#include "graphics/ui/curshape.c"
#include "graphics/ui/hwcursor.c"
u64 g_nv_bar0=1; u32 g_nv_boot0=0x16000000u;
static u32 lastpt; static int npt;
u32 nvr32(u32 off){ return (off>=NVC57A_STAT_BASE&&off<NVC57A_STAT_BASE+0x200)?0x00040000u:0; }
void nvw32(u32 off,u32 v){ if(off==nvcur_user(0)+NVC37A_SET_CURSOR_HOT_SPOT_POINT_OUT(0)){ lastpt=v; npt++; } }
void nv_udelay(u32 us){ (void)us; }
static int st_head=0, st_nm, st_ups, st_off; static u32 st_mv[16]; static u32 st_vb[0x4000];
int nvflip_head(void){ return st_head; }
int nvflip_cursor_buf(u32 len,u64* v,volatile u32** c){ if(len>sizeof st_vb)return 0; *v=0x40000000ULL; *c=st_vb; return 1; }
int nvflip_core_methods(const u32* mv,int n){ if(n>8)return 0; memcpy(st_mv,mv,(size_t)n*8); st_nm=n; if(n==2)st_off++; return 1; }
int nvflip_core_update(u32 m){ (void)m; st_ups++; return 1; }
volatile u64 g_ticks=1000; int g_gfx=0, g_ws=0, g_state=ST_DESKTOP, mx=10, my=10, drag=-1, g_dstyle=0, g_cursor=0, g_rz_hover=0, g_doom_mcap=0, g_quake_mcap=0, g_rt=0, g_scene_quiet=0;
int DOCK_x=100, DOCK_y=560, DOCK_w=400, DOCK_h=40; u32 ACC=0x3C78E0u;
int g_clip_y0=-0x40000000, g_clip_y1=0x40000000, g_clip_maxy;
u32 W=1280, H=720, SW=1280; static u32 S1[16]; u32* g_scene=S1; u32 g_scene_acc, g_scene_wr; volatile int g_gq_inb, g_gfx_run, g_gq_capon;
Win WN[NWIN];
static unsigned long wbusy; unsigned long web_busy_ms(void){ return wbusy; }
void gfx_px(int x,int y,u32 c,int a){ (void)x; (void)y; (void)c; (void)a; }
void gfx_sync(void){}
static int bad_rt, bad_quiet;
void fill_rect(int x,int y,int w,int h,u32 c){ if(g_rt)bad_rt++; if(g_scene==S1&&!g_scene_quiet)bad_quiet++; for(int j=0;j<h;j++)for(int i=0;i<w;i++)putpx(x+i,y+j,c); }
static int allow=1, on2, tries, lastok; static const char* lasterr;
int nvset_allowed(int f){ (void)f; return allow; } int nvset_try_begin(int f){ (void)f; tries++; return 1; } void nvset_try_end(int f,int ok,const char* e){ (void)f; lastok=ok; lasterr=e; }
int out_on2(int x,int y){ (void)x; (void)y; return on2; }
void draw_cursor(void){
    if(!g_hwcur_drawing)g_cur_shape=cur_shape_pick();
    if(hwcur_step())return;
    if(!cur_shape_draw(mx,my,g_cur_shape)){ fill_rect(mx,my,2,12,0x121418u); fill_rect(mx+1,my+1,1,10,0xFFFFFFu); } }
static int fails, checks;
#define CK(c,msg) do{ checks++; if(!(c)){ fails++; printf("FAIL %s\n",msg); } }while(0)
static u32 bk[128*128], wh[128*128], im[CUR_N][64*64]; static int iw[CUR_N], ihx[CUR_N], ihy[CUR_N];
static int build(int s){
    u32* s0=g_scene; u32 w0=W,h0=H,sw0=SW; int q=g_scene_quiet; g_scene_quiet=1;
    for(int i=0;i<128*128;i++){ bk[i]=0; wh[i]=0xFFFFFFu; }
    W=H=SW=128; g_scene=bk; if(!cur_shape_draw(64,64,s)){ fill_rect(64,64,2,12,0x121418u); } g_scene=wh; if(!cur_shape_draw(64,64,s)){ fill_rect(64,64,2,12,0x121418u); }
    g_scene=s0; W=w0; H=h0; SW=sw0; g_scene_quiet=q;
    return nvcur_img_build(bk,wh,128,128,64,64,im[s],&iw[s],&ihx[s],&ihy[s]); }
int main(void){
    WN[W_EDITOR]=(Win){100,100,400,300,1,5,0,0,0,0,0,0,0}; WN[W_FILES]=(Win){600,100,300,300,1,4,0,0,0,0,0,0,0}; WN[W_TERM]=(Win){650,150,300,300,1,3,0,0,0,0,0,0,0};
    mx=200; my=200; CK(cur_shape_pick()==CUR_TEXT,"text beam over the editor's text area");
    my=105; CK(cur_shape_pick()==CUR_ARROW,"arrow over the editor's title bar");
    mx=700; my=200; CK(cur_shape_pick()==CUR_ARROW,"arrow over Files (Files on top of the terminal)");
    mx=920; my=420; CK(cur_shape_pick()==CUR_TEXT,"text beam over the uncovered terminal");
    WN[W_EDITOR].min=1; mx=200; my=200; CK(cur_shape_pick()==CUR_ARROW,"minimised window gives no text beam"); WN[W_EDITOR].min=0;
    mx=200; my=570; CK(cur_shape_pick()==CUR_HAND,"hand over the dock");
    g_dstyle=1; CK(cur_shape_pick()==CUR_ARROW,"classic style has no dock hand"); g_dstyle=0;
    mx=50; my=50; CK(cur_shape_pick()==CUR_ARROW,"arrow on the desktop");
    wbusy=900; CK(cur_shape_pick()==CUR_BUSY,"arrow with busy ring while a page loads"); mx=200; my=200; CK(cur_shape_pick()==CUR_TEXT,"text beam wins over background busy"); wbusy=0;
    drag=W_FILES; CK(cur_shape_pick()==CUR_MOVE,"move cursor while dragging a window"); drag=-1;
    cur_busy_note(); CK(cur_shape_pick()==CUR_WAIT,"wait cursor while the UI is busy (busy_pump/ui_pump)");
    g_ticks+=20; CK(cur_shape_pick()==CUR_TEXT,"wait cursor ends after the busy wait");
    g_state=ST_LOGIN; CK(cur_shape_pick()==CUR_ARROW,"arrow outside the desktop"); g_state=ST_DESKTOP;
    g_cur_force=CUR_NO; CK(cur_shape_pick()==CUR_NO,"forced shape"); g_cur_force=-1;
    int okb=1, okd=1; for(int s=0;s<CUR_N;s++){ int r=build(s); if(r<=0||iw[s]>64)okb=0; }
    CK(okb,"every shape builds a hardware cursor image of at most 64x64");
    for(int a=0;a<CUR_N;a++)for(int b=a+1;b<CUR_N;b++)if(iw[a]==iw[b]&&ihx[a]==ihx[b]&&ihy[a]==ihy[b]&&!memcmp(im[a],im[b],(size_t)iw[a]*iw[a]*4))okd=0;
    CK(okd,"all shapes look different");
    CK(ihx[CUR_TEXT]>2&&ihy[CUR_TEXT]>6&&ihx[CUR_WAIT]>4&&ihy[CUR_WAIT]>4,"text beam and wait ring have their hotspot in the middle");
    { u32 a0[64*64]; int w0=iw[CUR_WAIT]; memcpy(a0,im[CUR_WAIT],sizeof a0); g_ticks+=12; build(CUR_WAIT); CK(iw[CUR_WAIT]==w0&&memcmp(a0,im[CUR_WAIT],sizeof a0),"wait ring turns"); }
    g_nvcur_enable=1; g_rt=7; bad_rt=0; bad_quiet=0; mx=50; my=50;
    CK(hwcur_step()==1&&g_hwcur_shapes==1&&st_nm==4,"hardware cursor on: arrow uploaded");
    CK(!bad_rt&&!bad_quiet&&g_rt==7&&g_scene==S1&&W==1280,"shape rendering does not mark the GPU scene and restores the globals");
    int u0=g_hwcur_shapes; mx=60; draw_cursor(); CK(g_hwcur_shapes==u0,"moving over the same shape uploads nothing");
    mx=200; my=200; draw_cursor(); CK(g_hwcur_shapes==u0+1&&g_cur_shape==CUR_TEXT,"compositor switches to the text beam over a text field and uploads it");
    { int hx=NCUR.hx; CK(lastpt==nvcur_point_word(200-hx,200-NCUR.hy),"text beam placed by its hotspot"); }
    mx=50; my=50; draw_cursor(); CK(g_hwcur_shapes==u0+2&&g_cur_shape==CUR_ARROW,"back to the arrow");
    cur_busy_note(); draw_cursor(); int u1=g_hwcur_shapes; CK(g_cur_shape==CUR_WAIT&&u1==u0+3,"wait cursor uploaded while busy");
    cur_busy_note(); draw_cursor(); CK(g_hwcur_shapes==u1,"same wait phase: no upload");
    g_ticks+=12; cur_busy_note(); draw_cursor(); CK(g_hwcur_shapes==u1+1,"wait ring animates through the hardware cursor");
    g_ticks+=20; draw_cursor();
    on2=1; st_off=0; CK(hwcur_step()==0&&st_off==1,"mouse on screen 2: head-0 hardware cursor hidden, screen 2 draws in software");
    hwcur_step(); CK(st_off==1,"hidden once");
    on2=0; u0=g_hwcur_shapes; CK(hwcur_step()==1&&g_hwcur_shapes==u0+1,"back on screen 1: shape uploaded again");
    W=1280; H=720; mx=1279; my=719; draw_cursor(); { int hx=NCUR.hx, hy=NCUR.hy;
        CK(lastpt==nvcur_point_word(1279-hx,719-hy),"scaled mode (1280x720 desktop on a 1920x1080 head): point in desktop (viewport-in) pixels, not scaled");
        nvcur_hw_move(1500,900,(int)W,(int)H); CK(lastpt==nvcur_point_word(1279,719),"scaled mode: clipped at the viewport-in edge (1279,719), not the 1920x1080 raster");
        nvcur_hw_move(640,360,(int)W,(int)H); CK(lastpt==nvcur_point_word(640-hx,360-hy),"scaled mode: desktop centre maps to the cursor point unchanged"); }
    CK(iw[CUR_ARROW]<=64&&NCUR.w<=64,"image size does not grow with scaling (head scaler scales it with the desktop)");
    allow=0; CK(hwcur_step()==0,"guard off: software cursor");
    printf("%d checks, %s\n",checks,fails?"FAIL":"ALL PASS"); return fails!=0; }
