#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/graphics/ui/desktop.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/outputs.h"
#include "include/nvx/drivers/video/nvidia/nvsettings.h"
#include "include/nvx/graphics/ui/hwcursor.h"

extern volatile u64 g_ticks;
void gfx_sync(void);
void draw_app_marked(int i);
void ctx_draw(void);
int ctx_click(int px,int py);
extern int g_ctx, ctx_x, ctx_y;
#define S2PAD 16
static u32 *g_s2mem, *g_scene2; static u32 g_s2w, g_s2h; static u64 g_s2t=~0ULL;
static int g_in2, g_c2on, g_c2x, g_c2y, g_ekx, g_eky, g_ext_try, g_ext_want, g_s2_hpd, g_s1_hpd; static u64 g_ext_t0;
int g_ctx2;
static struct{ int on, x, y, w, h, max; } g_w2m[NWIN];
void out_sync(void){ out_layout(W,H,g_fb2_ok,W2,H2,g_disp_extend); }
static int out2_alloc(void){
    if(g_scene2&&g_s2w==W2&&g_s2h==H2)return 0;
    if(g_s2mem){ kfree(g_s2mem); g_s2mem=0; g_scene2=0; }
    if(!W2||!H2||W2>MAXW||H2>MAXH)return -1;
    g_s2mem=(u32*)kmalloc((unsigned long)W2*(2UL*H2+S2PAD)*4UL); if(!g_s2mem)return -1;
    g_scene2=g_s2mem+(u64)W2*S2PAD; g_s2w=W2; g_s2h=H2; return 0; }
static void fb2_put(const u32* s,int ss,int x,int y,int w,int h){
    if(x<0){ s-=x; w+=x; x=0; } if(y<0){ s-=(long)y*ss; h+=y; y=0; }
    if(x+w>(int)W2){ w=(int)W2-x; } if(y+h>(int)H2){ h=(int)H2-y; } if(w<=0||h<=0)return;
    for(int r=0;r<h;r++){ const u32* p=s+(long)r*ss;
        if(BYPP2==4){ u32* d=(u32*)(FBB2+(u64)(y+r)*PITCHB2)+x; if(g_fb2_rgb)copy_swap32(d,p,w); else for(int i=0;i<w;i++)d[i]=p[i]; }
        else { u8* d=FBB2+(u64)(y+r)*PITCHB2+(u64)x*3; for(int i=0;i<w;i++){ u32 c=p[i]; d[0]=(u8)c; d[1]=(u8)(c>>8); d[2]=(u8)(c>>16); d+=3; } } } }
static void win_ghost(int i){ Win* n=&WN[i]; fill_rect(n->x+4,n->y+TITLEH,n->w-8,n->h-TITLEH-4,rgb(28,30,38)); win_frame(i,APPNAME[i]); }
static int win_hits(Win* n,out_rect* o){ return n->x<o->x+o->w&&n->x+n->w>o->x&&n->y<o->y+o->h&&n->y+n->h>o->y; }
int out1_skip_win(int i){
    if(!g_disp_extend||g_out_n<2||i<0||i>=NWIN)return 0;
    Win* n=&WN[i]; if(out_of_rect(n->x,n->y,n->w,n->h)!=1)return 0;
    if(win_hits(n,&g_out[0]))win_ghost(i);
    return 1; }
static void out2_draw(void){
    for(u32 y=0;y<H2;y++){ u32 sy=(u32)((u64)y*H/H2); if(sy>=H)sy=H-1; u32* s=BG+(u64)sy*SW; u32* d=g_scene2+(u64)y*W2;
        for(u32 x=0;x<W2;x++){ u32 sx=(u32)((u64)x*W/W2); if(sx>=W)sx=W-1; d[x]=s[sx]; } }
    if(g_gfx)gfx_sync();
    int ord[NWIN], own[NWIN], k=0;
    for(int i=0;i<NWIN;i++){ Win* n=&WN[i]; if(!n->open||n->min||n->ws!=g_ws||!win_hits(n,&g_out[1]))continue;
        int j=k++; while(j>0&&WN[ord[j-1]].z>n->z){ ord[j]=ord[j-1]; j--; } ord[j]=i; }
    for(int q=0;q<k;q++){ Win* n=&WN[ord[q]]; own[q]=out_of_rect(n->x,n->y,n->w,n->h); }
    if(drag>=0&&drag<NWIN&&g_snap&&g_snap_o==1){ int bx,by,bw,bh; out_snap_rect(0,0,0,0,1,(int)W,(int)H,&bx,&by,&bw,&bh); int hw=(bw-6)/2, hh=(bh-6)/2, x=bx, y=by, w=bw, h=bh;
        if(g_snap!=3){ w=hw; if(g_snap==2||g_snap==5||g_snap==7)x=bx+bw-hw; if(g_snap>=4){ h=hh; if(g_snap>=6)y=by+hh+6; } }
        x-=g_out[1].x; y-=g_out[1].y;
        for(int j=y<0?0:y;j<y+h&&j<(int)H2;j++)for(int i=x<0?0:x;i<x+w&&i<(int)W2;i++){ u32* p=&g_scene2[(u64)j*W2+i]; *p=((*p>>1)&0x7F7F7Fu)+((0x78AAFAu>>1)&0x7F7F7Fu); } }
    u32* s0=g_scene; u32 w0=W,h0=H,sw0=SW; int gf=g_gfx, rt=g_rt, ox=g_out[1].x, oy=g_out[1].y, mx0=mx, my0=my;
    g_in2=1; g_gfx=0; g_rt=0; g_scene_quiet++; g_scene=g_scene2; W=W2; H=H2; SW=W2; mx-=ox; my-=oy;
    for(int q=0;q<k;q++){ int i=ord[q]; Win* n=&WN[i]; n->x-=ox; n->y-=oy; if(own[q]==1)draw_app_marked(i); else win_ghost(i); n->x+=ox; n->y+=oy; }
    if(g_ctx&&out_ctx_on2()){ ctx_x-=ox; ctx_y-=oy; g_ctx2=1; ctx_draw(); g_ctx2=0; ctx_x+=ox; ctx_y+=oy; }
    g_scene=s0; W=w0; H=h0; SW=sw0; g_scene_quiet--; g_rt=rt; g_gfx=gf; mx=mx0; my=my0; g_in2=0; }
int out_ctx_on2(void){ return g_disp_extend&&g_out_n>1&&out_on2(ctx_x,ctx_y); }
int out_ctx_click(int px,int py){
    int ox=g_out[1].x, oy=g_out[1].y; ctx_x-=ox; ctx_y-=oy; g_ctx2=1;
    int r=ctx_click(px-ox,py-oy);
    g_ctx2=0; ctx_x+=ox; ctx_y+=oy; g_s2t=~0ULL; return r; }
void out_ctx_bounds(int* w,int* h){ if(g_ctx2&&g_out_n>1){ *w=g_out[1].w; *h=g_out[1].h; } }
void out2_cursor(void){
    if(!g_disp_extend||g_out_n<2||!g_scene2||!FBB2||g_in2)return;
    int b=CURBB/2;
    if(g_c2on){ g_c2on=0; int x=g_c2x-b, y=g_c2y-b, x0=x<0?0:x, y0=y<0?0:y; fb2_put(g_scene2+(u64)y0*W2+x0,(int)W2,x0,y0,x+CURBB-x0,y+CURBB-y0); }
    if(out_at(mx,my)!=1)return;
    static u32 tmp[CURBB*CURBB];
    int lx=mx-g_out[1].x, ly=my-g_out[1].y;
    for(int r=0;r<CURBB;r++){ int sy=ly-b+r; for(int c=0;c<CURBB;c++){ int sx=lx-b+c; tmp[r*CURBB+c]=((u32)sx<W2&&(u32)sy<H2)?g_scene2[(u64)sy*W2+sx]:0; } }
    if(g_gfx)gfx_sync();
    u32* s0=g_scene; u32 w0=W,h0=H,sw0=SW; int gf=g_gfx, rt=g_rt, mx0=mx, my0=my;
    g_gfx=0; g_rt=0; g_scene_quiet++; g_hwcur_drawing++; g_scene=tmp; W=CURBB; H=CURBB; SW=CURBB; mx=b; my=b;
    draw_cursor();
    g_scene=s0; W=w0; H=h0; SW=sw0; g_hwcur_drawing--; g_scene_quiet--; g_rt=rt; g_gfx=gf; mx=mx0; my=my0;
    fb2_put(tmp,CURBB,lx-b,ly-b,CURBB,CURBB); g_c2on=1; g_c2x=lx; g_c2y=ly; }
static void out_restore2(void);
static void out_fail(const char* why){ int t=g_ext_try; g_ext_try=0; display_mode_set(0); if(t)nvset_try_end(NVSET_EXTEND,0,why); else nvset_note(NVSET_EXTEND,NVSET_ST_FALLBACK,why); }
int blit_extend2(void){
    if(!g_disp_extend)return -1;
    if(!g_fb2_ok||!FBB2){ display_hotplug(0); return -1; }
    out_sync(); if(out_count()<2||out2_alloc()){ out_fail("not enough memory for the second screen - it mirrors screen 1"); return -1; }
    if(g_in2)return 0;
    if(g_state!=ST_DESKTOP){ g_s2t=~0ULL; g_c2on=0; blit_mirror2(); return 0; }
    if(g_s2t!=~0ULL&&g_ticks-g_s2t<3){ out2_cursor(); return 0; }
    g_s2t=g_ticks; out2_draw();
    fb2_put(g_scene2,(int)W2,0,0,(int)W2,(int)H2); g_c2on=0; out2_cursor();
    if(g_ext_try&&g_ticks-g_ext_t0>=300){ g_ext_try=0; nvset_try_end(NVSET_EXTEND,1,0); }
    return 0; }
int out_enter(int i,int* px,int* py){
    if(!g_disp_extend||i<0||i>=NWIN)return 0;
    Win* n=&WN[i]; int dx,dy; if(!out_local_shift(n->x,n->y,n->w,n->h,&dx,&dy))return 0;
    n->x-=dx; n->y-=dy; mx-=dx; my-=dy; if(px)*px-=dx; if(py)*py-=dy; g_ekx=dx; g_eky=dy; return 1; }
void out_leave(int i,int k,int* px,int* py){
    if(!k||i<0||i>=NWIN)return;
    WN[i].x+=g_ekx; WN[i].y+=g_eky; mx+=g_ekx; my+=g_eky; if(px)*px+=g_ekx; if(py)*py+=g_eky; }
int display_mode_set(int extend){
    if(!extend){
        if(g_ext_try){ g_ext_try=0; nvset_try_end(NVSET_EXTEND,1,0); }
        if(g_disp_extend&&g_out_n>1)for(int i=0;i<NWIN;i++){ Win* n=&WN[i]; if(!n->open||out_of_rect(n->x,n->y,n->w,n->h)!=1)continue;
            g_w2m[i].on=1; g_w2m[i].max=n->max; g_w2m[i].x=n->x-g_out[1].x; g_w2m[i].y=n->y-g_out[1].y; g_w2m[i].w=n->w; g_w2m[i].h=n->h;
            if(n->max&&n->sw>0&&n->sh>0){ n->max=0; n->x=n->sx; n->y=n->sy; n->w=n->sw; n->h=n->sh; g_w2m[i].x=n->x-g_out[1].x; g_w2m[i].y=n->y-g_out[1].y; g_w2m[i].w=n->w; g_w2m[i].h=n->h; }
            if(out_of_rect(n->x,n->y,n->w,n->h)==1)out_move_rect(&n->x,&n->y,n->w,n->h,1,0); }
        g_disp_extend=0; g_mirror2=1; g_ext_try=0; g_ext_want=0; g_c2on=0; out_sync(); out_mouse_clamp(&mx,&my,(int)W,(int)H);
        return 0; }
    if(!g_fb2_ok)return -1;
    g_disp_extend=1; out_sync(); if(out_count()<2||out2_alloc()){ g_disp_extend=0; out_sync(); return -1; }
    g_mirror2=0; g_s2t=~0ULL; g_c2on=0; g_ext_want=0; out_restore2(); return 0; }
static void out_restore2(void){
    out_rect* r=&g_out[1];
    for(int i=0;i<NWIN;i++){ Win* n=&WN[i]; if(!g_w2m[i].on)continue; g_w2m[i].on=0; if(!n->open||n->max)continue;
        n->x=r->x+g_w2m[i].x; n->y=r->y+g_w2m[i].y; n->w=g_w2m[i].w<r->w?g_w2m[i].w:r->w; n->h=g_w2m[i].h<r->h?g_w2m[i].h:r->h; out_place(&n->x,&n->y,n->w,n->h,1);
        if(g_w2m[i].max){ n->sx=n->x; n->sy=n->y; n->sw=n->w; n->sh=n->h; n->x=r->x+6; n->y=r->y+6; n->w=r->w-12; n->h=r->h-12; n->max=1; } } }
int display_hotplug(int present){
    if(!present){ if(!g_disp_extend)return 0; display_mode_set(0); g_ext_want=1; return 1; }
    if(!g_ext_want||g_disp_extend||!g_fb2_ok||!FBB2||!nvset_allowed(NVSET_EXTEND))return 0;
    g_ext_want=0; if(!nvset_try_begin(NVSET_EXTEND))return 0;
    if(display_mode_set(1)){ nvset_try_end(NVSET_EXTEND,0,"screen 2 came back but could not be its own desktop - it mirrors screen 1"); return -1; }
    g_ext_try=1; g_ext_t0=g_ticks; return 1; }
int display_hpd(int present){
    if(!present){ if(!g_fb2_ok||!FBB2)return 0; g_fb2_ok=0; g_s2_hpd=1; display_hotplug(0); return 1; }
    if(!g_s2_hpd||!FBB2)return 0; g_s2_hpd=0; g_fb2_ok=1; display_hotplug(1); return 2; }
int display_hpd_s1(int present){
    if(!present){ if(!g_fb2_ok||!FBB2)return 0; g_s1_hpd=1; display_hotplug(0); return 1; }
    if(!g_s1_hpd)return 0;
    g_s1_hpd=0; display_hotplug(1); return 2; }
void display_sizes(u32* w,u32* h,u32* w2,u32* h2){ *w=W; *h=H; *w2=g_fb2_ok&&FBB2?W2:0; *h2=g_fb2_ok&&FBB2?H2:0; }
int display_move_win(int i,int o){
    if(i<0||i>=NWIN||!WN[i].open)return -1;
    out_sync(); int from=out_of_rect(WN[i].x,WN[i].y,WN[i].w,WN[i].h);
    if(WN[i].max)return -1;
    if(out_move_rect(&WN[i].x,&WN[i].y,WN[i].w,WN[i].h,from,o))return -1;
    g_s2t=~0ULL; return 0; }
static const char* out_cfg(void){ extern int fs_find_name(const char*); extern char* fsd(int); int k=fs_find_name("display-extend.cfg"); return k<0?0:fsd(k); }
void display_mode_remember(int on){ extern int fs_save_bin(const char*,const u8*,int,int); extern void nvx3_sync_all(void); u8 b[3]={(u8)(on?'1':'0'),(u8)('0'+g_out_arr),'\n'}; fs_save_bin("display-extend.cfg",b,3,0); nvx3_sync_all(); }
int display_arrange(int a){
    if(a<0||a>=OUT_ARR_N)return -1;
    int on2[NWIN]; out_sync(); int ext=g_disp_extend&&g_out_n>1, ox=g_out[1].x, oy=g_out[1].y, m2=ext&&out_on2(mx,my);
    for(int i=0;i<NWIN;i++){ Win* n=&WN[i]; on2[i]=ext&&n->open&&out_of_rect(n->x,n->y,n->w,n->h)==1; if(on2[i]){ n->x-=ox; n->y-=oy; n->sx-=ox; n->sy-=oy; } }
    if(m2){ mx-=ox; my-=oy; }
    g_out_arr=a; out_sync(); ox=g_out[1].x; oy=g_out[1].y;
    for(int i=0;i<NWIN;i++)if(on2[i]){ Win* n=&WN[i]; n->x+=ox; n->y+=oy; n->sx+=ox; n->sy+=oy; }
    if(m2){ mx+=ox; my+=oy; }
    out_mouse_clamp(&mx,&my,(int)W,(int)H);
    { const char* d=out_cfg(); display_mode_remember(d?d[0]!='0':1); }
    g_s2t=~0ULL; return 0; }
int display_extend_apply(int on){
    if(!on){ display_mode_set(0); display_mode_remember(0); return 1; }
    if(!g_fb2_ok)return -1;
    if(display_mode_set(1))return 0;
    display_mode_remember(1); return 1; }
void display_cfg_tick(void){
    static int done; if(g_ext_want)display_hotplug(1); if(done||g_ticks<500)return; done=1;
    const char* d=out_cfg(); int c=d?d[0]:-1; if(d&&c&&d[1]>='0'&&d[1]<'0'+OUT_ARR_N)g_out_arr=d[1]-'0';
    if(c=='0'||!nvset_try_begin(NVSET_EXTEND))return;
    if(display_mode_set(1)){ nvset_try_end(NVSET_EXTEND,0,"could not make screen 2 its own desktop - it mirrors screen 1"); return; }
    g_ext_try=1; g_ext_t0=g_ticks; }
