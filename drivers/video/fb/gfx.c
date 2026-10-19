#include "include/nvx/base.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/assets/fontaa.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/drivers/video/fb/gfx.h"
#include "include/nvx/graphics/ui/desktop.h"
#include "include/nvx/mem/heap.h"

int g_gfx=GFX_OFF, g_gfx_fx=1, g_gfx_ok=0, g_rt=0, gs_scene=0, gs_bg=0, gs_nc=0, g_gfx_present=0;
u32 g_gfx_frames=0, g_gfx_frame_us=0, g_gfx_layers=0, g_gfx_layer_mb=0; char g_gfx_why[64];
u32 g_scene_acc=0; u64 g_gfx_pres_ns=0; u32 g_gfx_cpu_us=0, g_gfx_gpu_us=0, g_gfx_pres_us=0, g_gfx_subs=0, g_gfx_text_ops=0, g_gfx_text_glyphs=0;
int g_lay_abort_win=-1, g_lay_abort_why=0, g_lay_abort_k=0;
u32 g_lay_full=0, g_lay_part=0, g_lay_same=0, g_lay_abort=0, g_blur_hit=0, g_blur_miss=0, g_shd_fast=0, g_shd_slow=0;
static u32 f_lay_full=0, f_lay_part=0, f_lay_same=0, f_lay_abort=0, f_blur_hit=0, f_blur_miss=0, f_shd_fast=0, f_shd_slow=0; static u64 f_w0=0, f_p0=0; static u32 f_s0=0, f_acc0=0;
volatile int g_gfx_run=0;
static int gs_tmp=0, gs_tmp2=0, gs_stc=0, gs_stm=0, gs_mc=0, snap_fail=0, lay_fail=0, own_scene=0; static u32 *p_bg=0, *p_nc=0;
static gfx_alloc_fn g_alloc=0;
static u32 *p_tmp=0, *p_stc=0, *p_stm=0, *p_mc=0;
static u64 f_t0=0; static u32 g_mw=0, g_mh=0, g_ms=0;
extern int drag, focus, g_resize; extern volatile u64 g_ticks;

static void wcp(char* d,const char* s){ int i=0; for(;s[i]&&i<63;i++)d[i]=s[i]; d[i]=0; }
static int imin(int a,int b){ return a<b?a:b; }
static int imax(int a,int b){ return a>b?a:b; }

#define PBMAX 4096
static int pb_on=0;
#define RUNMAX 4096
#define RUNAREA (1920*640)
#define RUNH 40
typedef struct { short x, y; char ch; u8 mode, s; u32 c; } gly;
static struct { int t, n, x0, y0, x1, y1, mono; u32 c; gly g[RUNMAX]; } RN;
static int st_x=0, st_y=0, st_h=0;
static u32 g_text_ops=0, g_text_glyphs=0;
static u32 hsh(u32 h,u32 v){ h^=v; h*=16777619u; return h; }
static int st_alloc(int w,int h,int* ox,int* oy){
    int SWd=(int)g_gs[gs_stm].w, SHd=(int)g_gs[gs_stm].h; if(w>SWd||h>SHd)return -1;
    if(st_x+w>SWd){ st_y+=st_h; st_x=0; st_h=0; }
    if(st_y+h>SHd){ gq_sync(); st_x=0; st_y=0; st_h=0; }
    *ox=st_x; *oy=st_y; st_x+=w; if(h>st_h)st_h=h; return 0; }
static void run_flush(void){
    if(RN.n<=0)return; int n=RN.n; RN.n=0; g_gfx_run=pb_on;
    int w=RN.x1-RN.x0, h=RN.y1-RN.y0, sx, sy; if(w<=0||h<=0)return;
    if(st_alloc(w,h,&sx,&sy))return;
    u32 mp=g_gs[gs_stm].pitch, cp=g_gs[gs_stc].pitch; u32 key=2166136261u;
    for(int j=0;j<h;j++){ u32* m=p_stm+(u64)(sy+j)*mp+sx; for(int i=0;i<w;i++)m[i]=0; }
    for(int k=0;k<n;k++){ gly* G=&RN.g[k]; int idx=(u8)G->ch-32; if(idx<0||idx>=95)continue; const unsigned char* gp=fontaa[idx]; int bx=G->x-RN.x0, by=G->y-RN.y0, s=G->s;
        key=hsh(key,(u32)(u16)G->x|((u32)(u16)G->y<<16)); key=hsh(key,(u32)(u8)G->ch|((u32)G->mode<<8)|((u32)s<<16)); key=hsh(key,G->c&0x00FFFFFFu);
        for(int r=0;r<14;r++) for(int col=0;col<8;col++){ int a=gp[r*8+col]; u32 cov;
            if(G->mode==0){ if(a<6)continue; cov=a>=250?256u:(u32)a; } else if(G->mode==1){ if(a<6)continue; cov=(u32)a; } else { if(a<8)continue; cov=(u32)a; }
            for(int dy=0;dy<s;dy++) for(int dx=0;dx<s;dx++){ int px=bx+col*s+dx, py=by+r*s+dy; if(px<0||px>=w||py<0||py>=h)continue;
                u32* m=p_stm+(u64)(sy+py)*mp+sx+px; u32 o=*m; *m=o+cov-((o*cov)>>8); if(*m>256)*m=256;
                if(!RN.mono) p_stc[(u64)(sy+py)*cp+sx+px]=G->c&0x00FFFFFFu; } } }
    int t=RN.t; g_text_ops++; g_text_glyphs+=(u32)n; gq_set_key(key|1u);
    if(RN.mono) gq_mask(t,RN.x0,RN.y0,w,h,RN.c&0x00FFFFFFu,256,gs_stm,sx,sy);
    else gq_copym(t,RN.x0,RN.y0,w,h,gs_stc,sx,sy,gs_stm,sx,sy,256,0); }
static struct { int t, n, x0, y0, x1, y1; struct { short x, y; u32 c; u16 a; } p[PBMAX]; } PB;
static void pb_flush(void){
    if(!pb_on)return; pb_on=0; g_gfx_run=RN.n>0; int n=PB.n; PB.n=0; if(n<=0)return;
    int w=PB.x1-PB.x0, h=PB.y1-PB.y0, sx, sy; if(st_alloc(w,h,&sx,&sy)){ return; }
    u32 mp=g_gs[gs_stm].pitch, cp=g_gs[gs_stc].pitch; u32 key=2166136261u;
    for(int j=0;j<h;j++){ u32* m=p_stm+(u64)(sy+j)*mp+sx; for(int i=0;i<w;i++)m[i]=0; }
    for(int k=0;k<n;k++){ int px=PB.p[k].x-PB.x0, py=PB.p[k].y-PB.y0; u32* m=p_stm+(u64)(sy+py)*mp+sx+px; u32 a=PB.p[k].a, o=*m;
        key=hsh(key,(u32)(u16)PB.p[k].x|((u32)(u16)PB.p[k].y<<16)); key=hsh(key,PB.p[k].c^((u32)a<<24));
        if(a>=256||!o){ *m=a; } else { *m=o+a-((o*a)>>8); }
        p_stc[(u64)(sy+py)*cp+sx+px]=PB.p[k].c; }
    int t=PB.t; gq_set_key(key|1u); gq_copym(t,PB.x0,PB.y0,w,h,gs_stc,sx,sy,gs_stm,sx,sy,256,0); }
static int ovl(int ax,int ay,int aw,int ah,int bx0,int by0,int bx1,int by1){ return ax<bx1&&bx0<ax+aw&&ay<by1&&by0<ay+ah; }
void gfx_px(int x,int y,u32 c,int a){
    if((u32)x>=W||(u32)y>=H||a<=0)return; if(a>256)a=256;
    if(RN.n&&RN.t==g_rt&&ovl(x,y,1,1,RN.x0,RN.y0,RN.x1,RN.y1))run_flush();
    if(pb_on){ int nx0=imin(PB.x0,x), ny0=imin(PB.y0,y), nx1=imax(PB.x1,x+1), ny1=imax(PB.y1,y+1);
        if(PB.t!=g_rt||PB.n>=PBMAX||(nx1-nx0)*(ny1-ny0)>65536||(RN.n&&RN.t==g_rt&&ovl(nx0,ny0,nx1-nx0,ny1-ny0,RN.x0,RN.y0,RN.x1,RN.y1)))pb_flush(); else { PB.x0=nx0; PB.y0=ny0; PB.x1=nx1; PB.y1=ny1; } }
    if(!pb_on){ PB.t=g_rt; PB.n=0; PB.x0=x; PB.y0=y; PB.x1=x+1; PB.y1=y+1; pb_on=1; g_gfx_run=1; }
    PB.p[PB.n].x=(short)x; PB.p[PB.n].y=(short)y; PB.p[PB.n].c=c&0x00FFFFFFu; PB.p[PB.n].a=(u16)a; PB.n++; }
static void pre_hook(void){ if(pb_on)pb_flush(); if(RN.n)run_flush(); g_gfx_run=0; }
static void pre_op(int d,int x,int y,int w,int h,int s,int m){
    if(d<0){ pre_hook(); return; }
    if(pb_on&&(PB.t==s||PB.t==m||(PB.t==d&&ovl(x,y,w,h,PB.x0,PB.y0,PB.x1,PB.y1))))pb_flush();
    if(RN.n&&(RN.t==s||RN.t==m||(RN.t==d&&ovl(x,y,w,h,RN.x0,RN.y0,RN.x1,RN.y1))))run_flush();
    g_gfx_run=(RN.n>0)||pb_on; }
void gfx_glyph(int x,int y,char ch,u32 c,int s,int mode){
    int idx=(u8)ch-32; if(idx<0||idx>=95)return; if(s<1)s=1;
    if(x+8*s<=0||y+14*s<=0||x>=(int)W||y>=(int)H)return;
    int gw=8*s, gh=14*s;
    if(pb_on&&PB.t==g_rt&&ovl(x,y,gw,gh,PB.x0,PB.y0,PB.x1,PB.y1))pb_flush();
    if(RN.n){ int nx0=imin(RN.x0,x), ny0=imin(RN.y0,y), nx1=imax(RN.x1,x+gw), ny1=imax(RN.y1,y+gh);
        if(RN.t!=g_rt||RN.n>=RUNMAX||(nx1-nx0)>(int)g_gs[gs_stm].w||(ny1-ny0)>(int)g_gs[gs_stm].h/2||(ny1-ny0)>RUNH||(long)(nx1-nx0)*(ny1-ny0)>RUNAREA)run_flush();
        else { RN.x0=nx0; RN.y0=ny0; RN.x1=nx1; RN.y1=ny1; } }
    if(!RN.n){ RN.t=g_rt; RN.x0=x; RN.y0=y; RN.x1=x+gw; RN.y1=y+gh; RN.mono=1; RN.c=c; }
    gly* G=&RN.g[RN.n++]; G->x=(short)x; G->y=(short)y; G->ch=ch; G->mode=(u8)mode; G->s=(u8)s; G->c=c; if((c&0x00FFFFFFu)!=(RN.c&0x00FFFFFFu))RN.mono=0; g_gfx_run=1; }

#define MCMAX 160
static struct { u8 type; u8 v; u16 r; u16 x, y; } MC[MCMAX]; static int mc_n=0, mc_x=0, mc_y=0, mc_h=0;
static int mc_find(int type,int r,int v,int* x,int* y){
    for(int i=0;i<mc_n;i++) if(MC[i].type==type&&MC[i].r==r&&MC[i].v==v){ *x=MC[i].x; *y=MC[i].y; return 1; } return 0; }
static int mc_new(int type,int r,int v,int sz,int* x,int* y){
    int MW=(int)g_gs[gs_mc].w, MH=(int)g_gs[gs_mc].h; if(sz>MW||sz>MH)return -1;
    if(mc_x+sz>MW){ mc_y+=mc_h; mc_x=0; mc_h=0; }
    if(mc_y+sz>MH||mc_n>=MCMAX){ gq_sync(); mc_n=0; mc_x=0; mc_y=0; mc_h=0; gq_bump(gs_mc); }
    *x=mc_x; *y=mc_y; mc_x+=sz; if(sz>mc_h)mc_h=sz; MC[mc_n].type=(u8)type; MC[mc_n].r=(u16)r; MC[mc_n].v=(u8)v; MC[mc_n].x=(u16)*x; MC[mc_n].y=(u16)*y; mc_n++; return 0; }
static u32 covf(int r,int dx,int dy){ u32 dd=isqrt32(((u32)(dx*dx+dy*dy))<<16); int cov=r*256-(int)dd+128; if(cov<=0)return 0; if(cov>=256)return 256; return (u32)cov; }
static int mc_corner(int r,int v,int* x,int* y){
    if(mc_find(1,r,v,x,y))return 0; if(mc_new(1,r,v,r,x,y))return -1; u32 p=g_gs[gs_mc].pitch;
    for(int j=0;j<r;j++) for(int i=0;i<r;i++){ int dx=(v&1)?i+1:r-i, dy=(v&2)?j+1:r-j; p_mc[(u64)(*y+j)*p+*x+i]=covf(r,dx,dy); }
    return 0; }
static int mc_circle(int r,int* x,int* y){
    if(mc_find(2,r,0,x,y))return 0; int sz=2*r+3; if(mc_new(2,r,0,sz,x,y))return -1; u32 p=g_gs[gs_mc].pitch;
    for(int j=0;j<sz;j++) for(int i=0;i<sz;i++){ int di=i-r-1, dj=j-r-1; int d2=di*di+dj*dj; u32 v=0; if(d2<=(r+1)*(r+1))v=covf(r,di,dj); if(!v){ p_mc[(u64)(*y+j)*p+*x+i]=0; continue; }
        u32 dd=isqrt32(((u32)d2)<<16); int cov=r*256-(int)dd+128; p_mc[(u64)(*y+j)*p+*x+i]=cov>=256?256u:(u32)cov; }
    return 0; }
static void rect_a(int x,int y,int w,int h,u32 c,int a){ if(w<=0||h<=0)return; if(a>=256)gq_fill(g_rt,x,y,w,h,c&0x00FFFFFFu); else gq_blend(g_rt,x,y,w,h,c,a); }
void gfx_round(int x,int y,int w,int h,int r,int m,u32 c,int a){
    if(w<=0||h<=0||a<=0)return; if(r>w/2)r=w/2; if(r>h/2)r=h/2; if(r<=0||!(m&15)){ rect_a(x,y,w,h,c,a); return; }
    rect_a(x+r,y,w-2*r,h,c,a); rect_a(x,y+r,r,h-2*r,c,a); rect_a(x+w-r,y+r,r,h-2*r,c,a);
    int cx[4]={x,x+w-r,x,x+w-r}, cy[4]={y,y,y+h-r,y+h-r}, bit[4]={1,2,4,8};
    for(int k=0;k<4;k++){ if(!(m&bit[k])){ rect_a(cx[k],cy[k],r,r,c,a); continue; } int mx,my; if(mc_corner(r,k,&mx,&my)){ rect_a(cx[k],cy[k],r,r,c,a); continue; } gq_mask(g_rt,cx[k],cy[k],r,r,c&0x00FFFFFFu,a,gs_mc,mx,my); } }
static const int SE[4]={12,8,5,2}, SS[4]={14,10,6,3}, SA[4]={6,8,10,12};
#define SHK 64
#define SHT 12
#define SHB 28
#define SHTW (SHK+8+12)
#define SHTH (SHK+8+SHB)
#define SHPW 1024
static int gs_shd=0, shd_n=0, shd_top_nz=0; static u32* p_shd=0; static int shd_r[64], shd_x[64], shd_y[64];
static u32 rrcov(int px,int py,int x0,int y0,int w,int h,int R){ if(px<x0||py<y0||px>=x0+w||py>=y0+h)return 0; int i=px-x0, j=py-y0;
    if(i<R&&j<R)return covf(R,R-i,R-j); if(i>=w-R&&j<R)return covf(R,i-(w-R)+1,R-j); if(i<R&&j>=h-R)return covf(R,R-i,j-(h-R)+1); if(i>=w-R&&j>=h-R)return covf(R,i-(w-R)+1,j-(h-R)+1); return 256; }
static u32 shcov(int px,int py,int ww,int wh,int r){ if(rrcov(px,py,0,0,ww,wh,r)>=256)return 0; u32 T=256;
    for(int k=0;k<4;k++){ int e=SE[k]; u32 c=rrcov(px,py,-e,-e+SS[k],ww+2*e,wh+2*e,r+e); u32 a=(c*(u32)SA[k])>>8; T=T*(256u-a)>>8; } return 256u-T; }
static void shd_init(void){
    if(gs_shd<=0||!p_shd)return; u32 P=g_gs[gs_shd].pitch; int SWd=(int)g_gs[gs_shd].w, SHd=(int)g_gs[gs_shd].h; shd_n=0; shd_top_nz=0;
    for(int j=0;j<SHT;j++){ u32 v=shcov(SHPW/2,-SHT+j,SHPW,SHPW,8); if(v)shd_top_nz=1; for(int x=0;x<SWd-24;x++)p_shd[(u64)j*P+x]=v; }
    for(int j=0;j<SHB;j++){ u32 v=shcov(SHPW/2,SHPW+j,SHPW,SHPW,8); for(int x=0;x<SWd-24;x++)p_shd[(u64)(SHT+j)*P+x]=v; }
    for(int i=0;i<12;i++){ u32 vl=shcov(-12+i,SHPW/2,SHPW,SHPW,8), vr=shcov(SHPW+i,SHPW/2,SHPW,SHPW,8); for(int y=0;y<SHd;y++){ p_shd[(u64)y*P+SWd-24+i]=vl; p_shd[(u64)y*P+SWd-12+i]=vr; } } }
static int shd_slot(int r){
    for(int q=0;q<shd_n;q++) if(shd_r[q]==r)return q; if(shd_n>=64||gs_shd<=0)return -1;
    int SWd=(int)g_gs[gs_shd].w, SHd=(int)g_gs[gs_shd].h; int nb=(SWd-24)/(4*SHTW); if(nb<1)return -1; int q=shd_n; int ox=(q%nb)*4*SHTW, oy=SHT+SHB+4+(q/nb)*SHTH; if(oy+SHTH>SHd)return -1;
    u32 P=g_gs[gs_shd].pitch; int b0=SHPW-(SHK+8);
    for(int j=0;j<SHTH;j++) for(int i=0;i<SHTW;i++){ u64 row=(u64)(oy+j)*P;
        p_shd[row+ox+i]=j<SHTW?shcov(-12+i,-12+j,SHPW,SHPW,r):0; p_shd[row+ox+SHTW+i]=j<SHTW?shcov(b0+i,-12+j,SHPW,SHPW,r):0;
        p_shd[row+ox+2*SHTW+i]=shcov(-12+i,b0+j,SHPW,SHPW,r); p_shd[row+ox+3*SHTW+i]=shcov(b0+i,b0+j,SHPW,SHPW,r); }
    shd_r[q]=r; shd_x[q]=ox; shd_y[q]=oy; shd_n++; return q; }
static void shd_slow(int wx,int wy,int ww,int wh,int wr){ f_shd_slow++; for(int k=0;k<4;k++){ int e=SE[k]; gfx_round(wx-e,wy-e+SS[k],ww+2*e,wh+2*e,wr+e,15,0,SA[k]); } }
static int shd_alpha=256;
void gfx_shadow(int wx,int wy,int ww,int wh,int wr){
    if(gs_shd<=0||shd_alpha<256||wr<0||wr>30||ww<2*SHK+16||wh<2*SHK+16||ww>(int)g_gs[gs_shd].w-24+2*SHK||wh>(int)g_gs[gs_shd].h+2*SHK){ shd_slow(wx,wy,ww,wh,wr); return; }
    int q=shd_slot(wr); if(q<0){ shd_slow(wx,wy,ww,wh,wr); return; }
    int X0=wx, Y0=wy, X1=wx+ww, Y1=wy+wh; int ax=(X0+SHK+7)&~7, bx=(X1-SHK)&~7, ay=(Y0+SHK+7)&~7, by=(Y1-SHK)&~7; if(bx<=ax||by<=ay){ shd_slow(wx,wy,ww,wh,wr); return; }
    int tx=shd_x[q], ty=shd_y[q], SWd=(int)g_gs[gs_shd].w; int t=g_rt; f_shd_fast++;
    gq_mask(t,X0-12,Y0-12,ax-(X0-12),ay-(Y0-12),0,256,gs_shd,tx,ty);
    gq_mask(t,bx,Y0-12,X1+12-bx,ay-(Y0-12),0,256,gs_shd,tx+SHTW+(bx-(X1-(SHK+8))),ty);
    gq_mask(t,X0-12,by,ax-(X0-12),Y1+SHB-by,0,256,gs_shd,tx+2*SHTW,ty+(by-(Y1-(SHK+8))));
    gq_mask(t,bx,by,X1+12-bx,Y1+SHB-by,0,256,gs_shd,tx+3*SHTW+(bx-(X1-(SHK+8))),ty+(by-(Y1-(SHK+8))));
    if(shd_top_nz) gq_mask(t,ax,Y0-SHT,bx-ax,SHT,0,256,gs_shd,0,0);
    gq_mask(t,ax,Y1,bx-ax,SHB,0,256,gs_shd,0,SHT);
    gq_mask(t,X0-12,ay,12,by-ay,0,256,gs_shd,SWd-24,0);
    gq_mask(t,X1,ay,12,by-ay,0,256,gs_shd,SWd-12,0); }
void gfx_circle(int cx,int cy,int r,u32 c,int a){ if(r<0)return; int mx,my; if(mc_circle(r,&mx,&my)){ rect_a(cx-r,cy-r,2*r+1,2*r+1,c,a); return; } gq_mask(g_rt,cx-r-1,cy-r-1,2*r+3,2*r+3,c&0x00FFFFFFu,a,gs_mc,mx,my); }
static void cpy(int ov,int dst,int x,int y,int w,int h,int src,int sx,int sy,int a,int key){ if(ov)gq_overc(dst,x,y,w,h,src,sx,sy,a); else gq_copy(dst,x,y,w,h,src,sx,sy,a,key); }
static void cpym(int ov,int dst,int x,int y,int w,int h,int src,int sx,int sy,int m,int mx,int my,int a,int key){ if(ov)gq_overm(dst,x,y,w,h,src,sx,sy,m,mx,my,a); else gq_copym(dst,x,y,w,h,src,sx,sy,m,mx,my,a,key); }
static void copy_round(int dst,int x,int y,int w,int h,int r,int m,int src,int sx,int sy,int a,int key){
    int ov=key; if(w<=0||h<=0||a<=0)return; if(r>w/2)r=w/2; if(r>h/2)r=h/2;
    if(r<=0||!(m&15)){ cpy(ov,dst,x,y,w,h,src,sx,sy,a,0); return; }
    cpy(ov,dst,x+r,y,w-2*r,h,src,sx+r,sy,a,0); cpy(ov,dst,x,y+r,r,h-2*r,src,sx,sy+r,a,0); cpy(ov,dst,x+w-r,y+r,r,h-2*r,src,sx+w-r,sy+r,a,0);
    int ox[4]={0,w-r,0,w-r}, oy[4]={0,0,h-r,h-r};
    for(int k=0;k<4;k++){ int mx,my; if(!(m&(1<<k))||mc_corner(r,k,&mx,&my)){ cpy(ov,dst,x+ox[k],y+oy[k],r,r,src,sx+ox[k],sy+oy[k],a,0); continue; } cpym(ov,dst,x+ox[k],y+oy[k],r,r,src,sx+ox[k],sy+oy[k],gs_mc,mx,my,a,0); } }
static void blur_m(int x,int y,int w,int h,int r,int m,int iters,int cache){
    if(w<=0||h<=0||gs_tmp<=0||gs_tmp2<=0||iters<=0)return; int t=g_rt, src=t;
    for(int it=0;it<iters;it++){ int mg=4*(iters-1-it);
        gq_blur1(gs_tmp,x-mg,y-mg-4,w+2*mg,h+2*mg+8,src,x-mg,y-mg-4,0);
        gq_blur1(gs_tmp2,x-mg,y-mg,w+2*mg,h+2*mg,gs_tmp,x-mg,y-mg,1); src=gs_tmp2; }
    if(cache>0) gq_copy(cache,x,y,w,h,gs_tmp2,x,y,256,0);
    copy_round(t,x,y,w,h,r,m,gs_tmp2,x,y,256,0); }
void gfx_blur(int x,int y,int w,int h,int r,int iters){ blur_m(x,y,w,h,r,15,iters,0); }
static int g_cur_layer=-1; static void glass_rec(int x,int y,int w,int h,int r,int it);
void gfx_glass(int x,int y,int w,int h,int r,u32 tint,int a,int iters){ if(g_rt!=gs_scene&&g_cur_layer>=0){ if(iters>0)glass_rec(x,y,w,h,r,iters); gfx_round(x,y,w,h,r,15,tint,a); return; } if(iters>0)gfx_blur(x,y,w,h,r,iters); gfx_round(x,y,w,h,r,15,tint,a); }

#define NL 32
typedef struct { int sid; u32* px; int dx, dy, dw, dh; int bx0, by0, bx1, by1; int valid; int anim, akind; u64 at0; int ghost; int gx, gy, gw, gh; int ng; short G[8][6]; int wheel, sanim, so0, sdy, sx, sy, sw, sh, spos, sset; u64 st0; int nx, ny, nw, nh, npos, nset; void *opA, *opB; int nold, bc; u64 bkey; int bvalid; } layer;
static layer LY[NL];
static void glass_rec(int x,int y,int w,int h,int r,int it){ layer* L=&LY[g_cur_layer]; if(L->ng>=8)return; short* g=L->G[L->ng++]; g[0]=(short)x; g[1]=(short)y; g[2]=(short)w; g[3]=(short)h; g[4]=(short)r; g[5]=(short)it; }
int gfx_layer_of(int i){ if(i<0||i>=NL)return 0; return LY[i].sid; }
static int lay_get(int i){
    if(i<0||i>=NL)return 0; if(LY[i].sid)return LY[i].sid; if(!g_alloc||lay_fail)return 0;
    u32* px=0; int s=g_alloc(W,H,SW,&px); if(s<=0){ lay_fail=1; return 0; } LY[i].sid=s; LY[i].px=px; LY[i].valid=0; LY[i].bx0=LY[i].by0=0; LY[i].bx1=LY[i].by1=0;
    gq_fill(s,0,0,(int)W,(int)H,GQ_EMPTY); g_gfx_layers++; g_gfx_layer_mb=(u32)((u64)g_gfx_layers*SW*H*4>>20); return s; }
static int live_app(int i){ return i==W_GPU3D||i==W_QUAKE||i==W_CAMERA||i==W_BENCH||i==W_RECORDER||i==W_VIDEO||i==W_DOOM||i==W_SNAKE||i==W_FLAPPY||i==W_CLOCK||i==W_SYSMON||i==W_XDISP; }
#define ANIM_MS 160
static int anim_t(layer* L){ if(!L->anim)return 256; u64 dt=(g_ticks-L->at0)*10; if(dt>=ANIM_MS){ L->anim=0; return 256; } int t=(int)(dt*256/ANIM_MS); return 256-((256-t)*(256-t)>>8); }
int g_wall_light=1; static int gs_light=0, light_fail=0, light_step=-1;
static int light_pos(int* x,int* y){ u32 t=(u32)(g_ticks/10); u32 a=t%1200, b=t%1700; int tx=(int)(a<600?a:1200-a), tyv=(int)(b<850?b:1700-b); *x=(int)W/5+tx*(int)W*3/5/600; *y=(int)H/8+tyv*(int)H/4/850; return (int)t; }
void gfx_wall_light(void){
    if(!g_wall_light||g_gfx==GFX_OFF||!g_gfx_fx)return;
    if(!gs_light&&!light_fail&&g_alloc){ u32* p; gs_light=g_alloc(512,512,512,&p); if(gs_light<=0){ gs_light=0; light_fail=1; return; }
        for(int j=0;j<512;j++)for(int i=0;i<512;i++){ int dx=i-256, dy=j-256; u32 d2=(u32)(dx*dx+dy*dy); u32 v=d2>=65536u?0u:(u32)((65536u-d2)>>8); p[(u64)j*512+i]=(v*v)>>8; } gq_bump(gs_light); }
    if(!gs_light)return; int x,y; light_step=light_pos(&x,&y); gq_mask(g_rt,x-256,y-256,512,512,0xFFF2DC,44,gs_light,0,0); }
int gfx_anim_active(void){ if(g_gfx==GFX_OFF||!g_gfx_fx)return 0; if(g_wall_light){ int x,y; if(light_pos(&x,&y)!=light_step)return 1; } for(int i=0;i<NL;i++) if(LY[i].anim||LY[i].ghost||LY[i].sanim)return 1; return 0; }
void gfx_win_event(int i,int kind){
    if(g_gfx==GFX_OFF||!g_gfx_fx||i<0||i>=NL)return; layer* L=&LY[i];
    if(kind==3){ L->wheel=1; return; }
    if(kind==0){ L->anim=1; L->akind=0; L->at0=g_ticks; L->ghost=0; }
    else if(L->sid&&L->valid){ L->ghost=1; L->akind=kind; L->at0=g_ticks; L->anim=1; L->gx=WN[i].x; L->gy=WN[i].y; L->gw=WN[i].w; L->gh=WN[i].h; } }
static int gs_snap=0, g_snap_owner=-1, blur_caches=0, opbuf_fail=0;
#define OPMAX 2048
static void body(int dst,int x,int y,int w,int h,int r,int m,int src,int ox,int oy,int a){ if(w>0&&h>0) copy_round(dst,x,y,w,h,r,m,src,x-ox,y-oy,a,1); }
static int scroll_o(layer* L){ if(!L->sanim)return 0; u64 dt=(g_ticks-L->st0)*10; if(dt>=140){ L->sanim=0; return 0; } int t=(int)(dt*256/140); int e=256-((256-t)*(256-t)>>8); return L->so0*(256-e)/256; }
static void composite(int i,int x,int y,int w,int h,int alpha,int yoff){
    layer* L=&LY[i]; int ox=x-L->dx, oy=y+yoff-L->dy; int r=g_gfx_fx?8:0; int Y=y+yoff;
    if(g_gfx_fx&&alpha>=64&&i!=W_TERM){ shd_alpha=alpha; gfx_shadow(x,Y,w,h,r); shd_alpha=256; }
    int th=(g_gfx_fx&&alpha>=200&&h>TITLEH+2*r)?TITLEH:0; int nglass=alpha>=128?L->ng:0;
    if(nglass||th){ pre_hook(); u64 k=g_ophash; k=(k^(u64)(u32)x^((u64)(u32)Y<<32))*0x100000001B3ULL; k=(k^(u64)(u32)w^((u64)(u32)h<<32))*0x100000001B3ULL; k=(k^(u64)th^((u64)r<<16)^((u64)nglass<<32))*0x100000001B3ULL;
        for(int q=0;q<nglass;q++){ short* g=L->G[q]; k=(k^((u64)(u16)g[0]|((u64)(u16)g[1]<<16)|((u64)(u16)g[2]<<32)|((u64)(u16)g[3]<<48)))*0x100000001B3ULL; k=(k^((u64)(u16)g[4]|((u64)(u16)g[5]<<16)|((u64)(u32)ox<<32)))*0x100000001B3ULL; }
        k=(k^(u64)(u32)oy)*0x100000001B3ULL;
        int cpu_touched=(g_scene_acc!=f_acc0);
        if(!L->bc&&g_alloc&&blur_caches<4){ u32* bp; int bs=g_alloc(W,H,SW,&bp); if(bs>0){ L->bc=bs; blur_caches++; } else blur_caches=4; }
        int hit=(L->bc>0&&L->bvalid&&L->bkey==k&&!cpu_touched); int sv=g_rt; g_rt=gs_scene;
        if(hit){ f_blur_hit++; for(int q=0;q<nglass;q++){ short* g=L->G[q]; copy_round(gs_scene,g[0]+ox,g[1]+oy,g[2],g[3],g[4],15,L->bc,g[0]+ox,g[1]+oy,256,0); } if(th) copy_round(gs_scene,x,Y,w,th,r,3,L->bc,x,Y,256,0); }
        else { f_blur_miss++; for(int q=0;q<nglass;q++){ short* g=L->G[q]; blur_m(g[0]+ox,g[1]+oy,g[2],g[3],g[4],15,g[5],L->bc); } if(th) blur_m(x,Y,w,th,r,3,1,L->bc); L->bkey=k; L->bvalid=(L->bc>0&&!cpu_touched); }
        g_rt=sv; }
    int so=0, ex=0, ey=0, ew=0, eh=0, sc=0;
    if(L->sanim&&g_snap_owner==i&&ox==0&&oy==0&&gs_snap){ so=scroll_o(L); if(L->sanim){ sc=1; ex=L->sx; ey=L->sy; ew=L->sw; eh=L->sh; if(ex<x||ey<Y+TITLEH||ex+ew>x+w||ey+eh>Y+h||ew<=0||eh<=0)sc=0; } }
    if(th){ body(gs_scene,x,Y,w,th,r,3,L->sid,ox,oy,alpha*232>>8); }
    if(!sc){ if(th) body(gs_scene,x,Y+th,w,h-th,r,12,L->sid,ox,oy,alpha); else body(gs_scene,x,Y,w,h,r,15,L->sid,ox,oy,alpha); }
    else { int top=Y+th; body(gs_scene,x,top,w,ey-top,r,th?0:3,L->sid,ox,oy,alpha); body(gs_scene,x,ey+eh,w,Y+h-(ey+eh),r,12,L->sid,ox,oy,alpha);
        body(gs_scene,x,ey,ex-x,eh,0,0,L->sid,ox,oy,alpha); body(gs_scene,ex+ew,ey,x+w-(ex+ew),eh,0,0,L->sid,ox,oy,alpha);
        int on=so, oo=so-L->sdy;
        { int s0=on>0?ey:ey+eh+on, s1=on>0?ey+on:ey+eh; int a0=imax(s0,ey+oo), a1=imin(s1,ey+eh+oo); if(on&&a1>a0) gq_overc(gs_scene,ex,a0,ew,a1-a0,gs_snap,ex,a0-oo,alpha); }
        { int a0=ey+imax(on,0), a1=ey+eh+imin(on,0); if(a1>a0) gq_overc(gs_scene,ex,a0,ew,a1-a0,L->sid,ex,a0-on,alpha); } }
    if(L->bx1>L->bx0){ int X0=L->bx0+ox, Y0=L->by0+oy, X1=L->bx1+ox, Y1=L->by1+oy, wx0=x, wy0=Y, wx1=x+w, wy1=Y+h;
        if(Y0<wy0) gq_overc(gs_scene,X0,Y0,X1-X0,wy0-Y0,L->sid,X0-ox,Y0-oy,alpha);
        if(Y1>wy1) gq_overc(gs_scene,X0,wy1,X1-X0,Y1-wy1,L->sid,X0-ox,wy1-oy,alpha);
        int a0=imax(Y0,wy0), a1=imin(Y1,wy1);
        if(a1>a0&&X0<wx0) gq_overc(gs_scene,X0,a0,wx0-X0,a1-a0,L->sid,X0-ox,a0-oy,alpha);
        if(a1>a0&&X1>wx1) gq_overc(gs_scene,wx1,a0,X1-wx1,a1-a0,L->sid,wx1-ox,a0-oy,alpha); } }
void gfx_scroll_area(int i,int x,int y,int w,int h,int pos){ if(g_gfx==GFX_OFF||i<0||i>=NL||g_cur_layer!=i)return; layer* L=&LY[i]; L->nx=x; L->ny=y; L->nw=w; L->nh=h; L->npos=pos; L->nset=1; }
void gfx_window(int i){
    extern void draw_app(int); int s=lay_get(i);
    if(!s){ draw_app(i); return; }
    layer* L=&LY[i]; int x=WN[i].x, y=WN[i].y, w=WN[i].w, h=WN[i].h;
    int moving=(drag>=0)&&!live_app(i)&&L->valid&&L->dw==w&&L->dh==h;
    if(!moving||g_resize==i){
        int cx0=imin(L->bx0,x)-64, cy0=imin(L->by0,y)-64, cx1=imax(L->bx1,x+w)+64, cy1=imax(L->by1,y+h)+64; if(!L->valid){ cx0=0; cy0=0; cx1=(int)W; cy1=(int)H; }
        int snapped=0, cur_o=scroll_o(L);
        if(L->wheel&&L->sset&&L->valid&&g_gfx_fx){ if(!gs_snap&&g_alloc&&!snap_fail){ u32* sp; gs_snap=g_alloc(W,H,SW,&sp); if(gs_snap<=0){ gs_snap=0; snap_fail=1; } }
            if(gs_snap){ gq_copy(gs_snap,L->sx,L->sy,L->sw,L->sh,s,L->sx,L->sy,256,0); snapped=1; } }
        L->wheel=0;
        if(!L->opA&&!L->opB&&!opbuf_fail){ L->opA=kmalloc((unsigned long)OPMAX*(unsigned long)gq_op_size()); L->opB=kmalloc((unsigned long)OPMAX*(unsigned long)gq_op_size()); if(!L->opA||!L->opB){ opbuf_fail=1; L->opA=L->opB=0; } L->nold=-1; }
        int full=(!L->valid||L->nold<0||x!=L->dx||y!=L->dy||w!=L->dw||h!=L->dh||snapped);
        int capt=(L->opA&&L->opB);
        if(!capt) gq_fill(s,cx0,cy0,cx1-cx0,cy1-cy0,GQ_EMPTY);
        pre_hook(); int ort=g_rt; u32* osc=g_scene; g_rt=s; g_scene=L->px; gq_touch_reset(s); L->ng=0; g_cur_layer=i; L->nset=0;
        if(capt) gq_cap_begin(s,L->opB,OPMAX,cx0,cy0,cx1-cx0,cy1-cy0);
        draw_app(i); pre_hook();
        int ncap=capt?gq_cap_end():-1;
        g_rt=ort; g_scene=osc; g_cur_layer=-1;
        int usebb=0, nb0=0, nb1=0, nb2=0, nb3=0;
        if(capt&&ncap>=0){
            int ax0=1<<30, ay0=1<<30, ax1=-(1<<30), ay1=-(1<<30); int sz=gq_op_size();
            for(int k=0;k<ncap;k++){ int ox_,oy_,ow_,oh_; gq_op_rect((u8*)L->opB+(u64)k*sz,&ox_,&oy_,&ow_,&oh_); if(ox_<ax0)ax0=ox_; if(oy_<ay0)ay0=oy_; if(ox_+ow_>ax1)ax1=ox_+ow_; if(oy_+oh_>ay1)ay1=oy_+oh_; }
            if(full){ gq_fill(s,cx0,cy0,cx1-cx0,cy1-cy0,GQ_EMPTY); gq_replay(L->opB,ncap,0,0,(int)W,(int)H); f_lay_full++; }
            else { int dx0=1<<30, dy0=1<<30, dx1=-(1<<30), dy1=-(1<<30); int nmax=ncap>L->nold?ncap:L->nold;
                for(int k=0;k<nmax;k++){ const u8* a=(const u8*)L->opA+(u64)k*sz; const u8* b=(const u8*)L->opB+(u64)k*sz; int same=(k<ncap&&k<L->nold)?gq_op_same(a,b):0; if(same)continue;
                    for(int q=0;q<2;q++){ const u8* o=q?b:a; if(q==0&&k>=L->nold)continue; if(q==1&&k>=ncap)continue; int rx,ry,rw,rh; gq_op_rect(o,&rx,&ry,&rw,&rh); if(rx<dx0)dx0=rx; if(ry<dy0)dy0=ry; if(rx+rw>dx1)dx1=rx+rw; if(ry+rh>dy1)dy1=ry+rh; } }
                if(dx1>dx0&&dy1>dy0){ gq_fill(s,dx0,dy0,dx1-dx0,dy1-dy0,GQ_EMPTY); gq_replay(L->opB,ncap,dx0,dy0,dx1-dx0,dy1-dy0); f_lay_part++; } else f_lay_same++; }
            void* t2=L->opA; L->opA=L->opB; L->opB=t2; L->nold=ncap;
            if(ncap>0){ usebb=1; nb0=ax0; nb1=ay0; nb2=ax1; nb3=ay1; } }
        else if(capt){ L->nold=-1; f_lay_abort++; g_lay_abort_win=i; g_lay_abort_why=g_cap_why; g_lay_abort_k=g_cap_wk; }
        if(L->nset){ int d=L->npos-L->spos;
            if(snapped&&L->sset&&d&&L->nx==L->sx&&L->ny==L->sy&&L->nw==L->sw&&L->nh==L->sh&&d<L->nh&&-d<L->nh&&x==L->dx&&y==L->dy){ L->sanim=1; L->st0=g_ticks; L->sdy=d; L->so0=d+cur_o; if(L->so0>L->nh)L->so0=L->nh; if(L->so0<-L->nh)L->so0=-L->nh; g_snap_owner=i; }
            else if(!snapped&&d){ L->sanim=0; }
            L->sx=L->nx; L->sy=L->ny; L->sw=L->nw; L->sh=L->nh; L->spos=L->npos; L->sset=1; } else L->sset=0;
        int bx0,by0,bx1,by1; if(usebb){ L->bx0=nb0; L->by0=nb1; L->bx1=nb2; L->by1=nb3; } else if(capt&&ncap==0){ L->bx0=x; L->by0=y; L->bx1=x+w; L->by1=y+h; } else if(gq_touch_get(s,&bx0,&by0,&bx1,&by1)){ L->bx0=bx0; L->by0=by0; L->bx1=bx1; L->by1=by1; } else { L->bx0=x; L->by0=y; L->bx1=x+w; L->by1=y+h; }
        L->dx=x; L->dy=y; L->dw=w; L->dh=h; L->valid=1; }
    int t=anim_t(L); int alpha=t; int yoff=L->anim&&L->akind==0?(256-t)*24/256:0;
    if(drag==i&&g_gfx_fx)alpha=alpha*236>>8;
    composite(i,x,y,w,h,alpha,yoff); }
void gfx_ghosts(void){
    for(int i=0;i<NL;i++){ layer* L=&LY[i]; if(!L->ghost)continue; if(!L->sid||!L->valid){ L->ghost=0; continue; }
        int t=anim_t(L); if(!L->anim){ L->ghost=0; continue; }
        int alpha=256-t, yoff=L->akind==2?t*80/256:t*16/256;
        if(alpha>0) composite(i,L->gx,L->gy,L->gw,L->gh,alpha,yoff); } }
void gfx_sync(void){ if(g_gfx==GFX_OFF){ g_gfx_run=0; return; } pre_hook(); int r=gq_sync(); if(g_gfx==GFX_GPU&&(r||g_gq_mode!=GQM_GPU)) gfx_stop("GPU drawing stopped answering"); }
#define SROPS 4096
static void *sr_a=0, *sr_b=0; static int sr_fail=0; static u32 sr_wr0=0; int g_gfx_srec=1;
void gfx_scene_settle(void){ sr_wr0=g_scene_wr; }
void gfx_invalidate_all(void){ for(int i=0;i<NL;i++){ LY[i].valid=0; LY[i].bvalid=0; LY[i].nold=-1; } gq_srec_invalidate(); }
int gfx_scene_end(int* x,int* y,int* w,int* h){ *x=0; *y=0; *w=(int)W; *h=(int)H; if(g_gfx==GFX_OFF)return 0; pre_hook(); return gq_srec_end(x,y,w,h); }
void gfx_frame_begin(void){ if(g_gfx&&(W!=g_mw||H!=g_mh||SW!=g_ms)){ gfx_stop("the screen mode changed"); return; } gfx_sync(); st_x=0; st_y=0; st_h=0;
    if(!sr_a&&!sr_fail){ sr_a=kmalloc((unsigned long)SROPS*(unsigned long)gq_op_size()); sr_b=kmalloc((unsigned long)SROPS*(unsigned long)gq_op_size()); if(!sr_a||!sr_b){ sr_fail=1; sr_a=sr_b=0; } }
    if(g_scene_wr!=sr_wr0||!g_gfx_srec){ gq_srec_invalidate(); sr_wr0=g_scene_wr; }
    if(sr_a&&g_gfx_srec&&gs_scene>0) gq_srec_begin(gs_scene,sr_a,sr_b,SROPS); f_t0=hr_ns(); g_rt=gs_scene; f_w0=g_gq_wait_ns; f_p0=g_gfx_pres_ns; f_s0=g_gq_submits; f_acc0=g_scene_acc; g_hash_d=gs_scene; g_ophash=1469598103934665603ULL;
    f_lay_full=f_lay_part=f_lay_same=f_lay_abort=f_blur_hit=f_blur_miss=f_shd_fast=f_shd_slow=0; g_text_ops=0; g_text_glyphs=0; }
void gfx_frame_end(void){ gfx_sync(); g_gfx_frames++; u64 tot=hr_ns()-f_t0, wt=g_gq_wait_ns-f_w0, pr=g_gfx_pres_ns-f_p0; g_gfx_frame_us=(u32)(tot/1000ULL);
    g_gfx_gpu_us=(u32)((wt>pr?wt-pr:0)/1000ULL); g_gfx_pres_us=(u32)(pr/1000ULL); g_gfx_cpu_us=(u32)((tot>wt?tot-wt:0)/1000ULL); g_gfx_subs=g_gq_submits-f_s0;
    g_lay_full=f_lay_full; g_lay_part=f_lay_part; g_lay_same=f_lay_same; g_lay_abort=f_lay_abort; g_blur_hit=f_blur_hit; g_blur_miss=f_blur_miss; g_shd_fast=f_shd_fast; g_shd_slow=f_shd_slow; g_gfx_text_ops=g_text_ops; g_gfx_text_glyphs=g_text_glyphs; g_hash_d=0; gq_frame_mark(); }
extern u32 SCENE_NC_STATIC[MAXW*MAXH];
static int sfail(const char* why){ wcp(g_gfx_why,why); g_gq_mode=GQM_OFF; return -1; }
static void drop_all(void){ gq_srec_invalidate();
    int* ids[11]={&gs_bg,&gs_nc,&gs_tmp,&gs_tmp2,&gs_stm,&gs_stc,&gs_mc,&gs_snap,&gs_shd,&gs_light,0}; for(int k=0;ids[k];k++){ if(*ids[k]>0)gq_surf_free(*ids[k]); *ids[k]=0; }
    for(int i=0;i<NL;i++){ if(LY[i].sid>0)gq_surf_free(LY[i].sid); if(LY[i].bc>0)gq_surf_free(LY[i].bc); LY[i].sid=0; LY[i].bc=0; LY[i].bvalid=0; LY[i].nold=-1; LY[i].px=0; LY[i].valid=0; LY[i].anim=0; LY[i].ghost=0; LY[i].sanim=0; }
    blur_caches=0; shd_n=0; p_shd=0;
    if(own_scene&&gs_scene>0)gq_surf_free(gs_scene); gs_scene=0; own_scene=0; g_snap_owner=-1; p_bg=p_nc=0; mc_n=0; mc_x=mc_y=mc_h=0; st_x=st_y=st_h=0; }
int gfx_start(int mode,gfx_alloc_fn al,u32* scene_px,int scene_sid){
    if(mode==GFX_OFF){ gfx_stop("turned off"); return 0; }
    if(W>MAXW||H>MAXH){ wcp(g_gfx_why,"screen larger than the drawing buffers"); return -1; }
    if(al!=g_alloc||W!=g_mw||H!=g_mh||SW!=g_ms)drop_all();
    g_mw=W; g_mh=H; g_ms=SW; g_alloc=al; snap_fail=0; lay_fail=0; g_gq_mode=(mode==GFX_GPU)?GQM_GPU:GQM_CPU; gq_set_pre(pre_op);
    if(!gs_bg){ gs_bg=al(W,H,SW,&p_bg); if(gs_bg<=0){ gs_bg=0; return sfail("out of memory for the wallpaper buffer"); } }
    if(!gs_nc){ gs_nc=al(W,H,SW,&p_nc); if(gs_nc<=0){ gs_nc=0; return sfail("out of memory for the cursor buffer"); } }
    if(gs_tmp<=0)gs_tmp=al(W,H,SW,&p_tmp); if(gs_tmp2<=0)gs_tmp2=al(W,H,SW,&p_tmp);
    if(gs_stm<=0)gs_stm=al(W,H,SW,&p_stm); if(gs_stc<=0)gs_stc=al(W,H,SW,&p_stc); if(gs_mc<=0)gs_mc=al(1024,1024,1024,&p_mc);
    if(gs_tmp<=0||gs_tmp2<=0||gs_stm<=0||gs_stc<=0||gs_mc<=0)return sfail("out of memory for GPU work buffers");
    if(gs_shd<=0){ gs_shd=al(W,H,SW,&p_shd); if(gs_shd<=0){ gs_shd=0; p_shd=0; } else { shd_n=0; shd_init(); } }
    if(scene_sid>0){ if(gs_scene!=scene_sid){ if(own_scene&&gs_scene>0)gq_surf_free(gs_scene); own_scene=0; gs_scene=scene_sid; } if(scene_px&&g_scene!=scene_px){ for(u64 k=0;k<(u64)SW*H;k++)scene_px[k]=g_scene[k]; g_scene=scene_px; } }
    else if(gs_scene>0&&own_scene){ u32* p=g_gs[gs_scene].px; if(g_scene!=p){ for(u64 k=0;k<(u64)SW*H;k++)p[k]=g_scene[k]; g_scene=p; } }
    else if(!gs_scene){ u32* p; gs_scene=al(W,H,SW,&p); if(gs_scene<=0){ gs_scene=0; return sfail("out of memory for the screen buffer"); } own_scene=1; for(u64 k=0;k<(u64)SW*H;k++)p[k]=g_scene[k]; g_scene=p; }
    if(BG!=p_bg){ for(u64 k=0;k<(u64)SW*H;k++)p_bg[k]=BG[k]; BG=p_bg; }
    if(SCENE_NC!=p_nc){ for(u64 k=0;k<(u64)SW*H;k++)p_nc[k]=SCENE_NC[k]; SCENE_NC=p_nc; }
    gq_srec_invalidate(); g_rt=gs_scene; g_gfx=mode; g_gfx_why[0]=0; for(int i=0;i<NL;i++){ LY[i].valid=0; LY[i].anim=0; LY[i].ghost=0; LY[i].sanim=0; LY[i].nold=-1; LY[i].bvalid=0; } return 0; }
void (*g_gfx_on_stop)(void)=0;
void gfx_stop(const char* why){
    if(g_gfx==GFX_OFF)return; if(g_gfx_on_stop)g_gfx_on_stop(); g_gfx_run=0; RN.n=0; PB.n=0; pb_on=0; gq_abort(); g_gfx=GFX_OFF; g_gq_mode=GQM_OFF; g_rt=gs_scene; g_gfx_present=0; wcp(g_gfx_why,why);
    if(BG!=BG_STATIC){ for(u64 k=0;k<(u64)SW*H;k++)BG_STATIC[k]=BG[k]; BG=BG_STATIC; }
    if(SCENE_NC!=SCENE_NC_STATIC){ for(u64 k=0;k<(u64)SW*H;k++)SCENE_NC_STATIC[k]=SCENE_NC[k]; SCENE_NC=SCENE_NC_STATIC; }
    if(own_scene&&g_scene!=SCENE_STATIC){ for(u64 k=0;k<(u64)SW*H;k++)SCENE_STATIC[k]=g_scene[k]; g_scene=SCENE_STATIC; } }
int gfx_app_surf(u32** px,int w,int h){ static int sid=0, sw=0, sh=0; static u32* sp=0; if(sid>0&&sw==w&&sh==h&&g_gs[sid].px==sp){ *px=sp; return sid; } if(sid>0){ gfx_sync(); gq_surf_free(sid); sid=0; } if(!g_alloc||w<=0||h<=0)return 0; u32* p=0; int s=g_alloc((u32)w,(u32)h,(u32)w,&p); if(s<=0)return 0; sid=s; sw=w; sh=h; sp=p; *px=p; return s; }
int gfx_bench_surf(u32** px,int w,int h){ static int sid=0, sw=0, sh=0; static u32* sp=0; if(sid>0&&sw==w&&sh==h&&g_gs[sid].px==sp){ *px=sp; return sid; } if(!g_alloc)return 0; u32* p=0; int s=g_alloc((u32)w,(u32)h,(u32)w,&p); if(s<=0)return 0; sid=s; sw=w; sh=h; sp=p; *px=p; return s; }
