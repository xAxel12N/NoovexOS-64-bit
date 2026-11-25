#include "include/nvx/base.h"
#include "include/nvx/graphics/ui/outputs.h"

int g_disp_extend=0, g_out_n=1, g_out_arr=OUT_RIGHT, g_snap_o=0;
out_rect g_out[OUT_MAX];
int g_out_mg=6, g_out_top=32, g_out_bot=86;
void out_work_reserve(int mg,int top,int bot){ if(mg<0)mg=0; if(top<0)top=0; if(bot<0)bot=0; g_out_mg=mg; g_out_top=top; g_out_bot=bot; }
void out_layout(u32 w1,u32 h1,int have2,u32 w2,u32 h2,int extend){
    g_out[0].x=0; g_out[0].y=0; g_out[0].w=(int)w1; g_out[0].h=(int)h1; g_out_n=1;
    g_out[1].x=0; g_out[1].y=0; g_out[1].w=0; g_out[1].h=0;
    if(!(extend&&have2&&w2&&h2&&w1&&h1))return;
    out_rect* o=&g_out[1]; o->w=(int)w2; o->h=(int)h2; g_out_n=2;
    switch(g_out_arr){ case OUT_LEFT: o->x=-(int)w2; break; case OUT_ABOVE: o->y=-(int)h2; break; case OUT_BELOW: o->y=(int)h1; break; default: o->x=(int)w1; break; } }
int out_count(void){ return g_out_n; }
int out_desk_x0(void){ int r=0; for(int i=0;i<g_out_n;i++)if(g_out[i].x<r)r=g_out[i].x; return r; }
int out_desk_y0(void){ int r=0; for(int i=0;i<g_out_n;i++)if(g_out[i].y<r)r=g_out[i].y; return r; }
int out_desk_w(void){ int r=0; for(int i=0;i<g_out_n;i++)if(g_out[i].x+g_out[i].w>r)r=g_out[i].x+g_out[i].w; return r-out_desk_x0(); }
int out_desk_h(void){ int r=0; for(int i=0;i<g_out_n;i++)if(g_out[i].y+g_out[i].h>r)r=g_out[i].y+g_out[i].h; return r-out_desk_y0(); }
int out_at(int x,int y){ for(int i=0;i<g_out_n;i++){ out_rect* o=&g_out[i]; if(x>=o->x&&y>=o->y&&x<o->x+o->w&&y<o->y+o->h)return i; } return -1; }
int out_on2(int x,int y){ return g_out_n>1&&out_at(x,y)==1; }
int out_of_rect(int x,int y,int w,int h){
    int best=0; long ba=-1;
    for(int i=0;i<g_out_n;i++){ out_rect* o=&g_out[i]; int x0=x>o->x?x:o->x, y0=y>o->y?y:o->y, x1=x+w<o->x+o->w?x+w:o->x+o->w, y1=y+h<o->y+o->h?y+h:o->y+o->h;
        long a=(x1>x0&&y1>y0)?(long)(x1-x0)*(y1-y0):0; if(a>ba){ ba=a; best=i; } }
    return best; }
void out_clamp_pt(int* x,int* y){
    if(out_at(*x,*y)>=0)return;
    int o=0; long bd=-1;
    for(int i=0;i<g_out_n;i++){ out_rect* r=&g_out[i]; int cx=*x<r->x?r->x:*x>=r->x+r->w?r->x+r->w-1:*x, cy=*y<r->y?r->y:*y>=r->y+r->h?r->y+r->h-1:*y;
        long d=(long)(cx-*x)*(cx-*x)+(long)(cy-*y)*(cy-*y); if(bd<0||d<bd){ bd=d; o=i; } }
    out_rect* r=&g_out[o];
    if(*x<r->x)*x=r->x;
    if(*x>=r->x+r->w)*x=r->x+r->w-1;
    if(*y<r->y)*y=r->y;
    if(*y>=r->y+r->h)*y=r->y+r->h-1; }
void out_mouse_clamp(int* x,int* y,int w,int h){
    if(g_out_n>1){ out_clamp_pt(x,y); return; }
    *x=*x<0?0:*x>=w?w-1:*x; *y=*y<0?0:*y>=h?h-1:*y; }
void out_drag_clamp(int* nx,int* ny,int ww,int px,int py,int w,int h,int th){
    int x0=0, x1=w, y0=0, y1=h;
    if(g_out_n>1){ int o=out_at(px,py); if(o<0)o=out_of_rect(px,py,1,1); y0=g_out[o].y; y1=g_out[o].y+g_out[o].h; x0=out_desk_x0(); x1=x0+out_desk_w(); }
    *ny=*ny<y0?y0:*ny; *ny=*ny>y1-th?y1-th:*ny;
    *nx=*nx>x1-90?x1-90:*nx; *nx=*nx<x0+90-ww?x0+90-ww:*nx; }
void out_area(int x,int y,int w,int h,int dw,int dh,out_rect* r){
    if(g_out_n>1){ *r=g_out[out_of_rect(x,y,w,h)]; return; }
    r->x=0; r->y=0; r->w=dw; r->h=dh; }
int out_snap_ok(int px,int py,int s){
    if(g_out_n<2||!s)return 1;
    if(out_at(px,py)!=0)return 0;
    if(s==3)return g_out_arr!=OUT_ABOVE;
    if(s==1||s==4||s==6)return g_out_arr!=OUT_LEFT;
    if(s==2||s==5||s==7)return g_out_arr!=OUT_RIGHT;
    return 1; }
int out_local_shift(int x,int y,int w,int h,int* dx,int* dy){
    *dx=0; *dy=0; if(g_out_n<2||out_of_rect(x,y,w,h)!=1)return 0;
    *dx=g_out[1].x; *dy=g_out[1].y; return 1; }
int out_place(int* x,int* y,int w,int h,int o){
    if(o<0||o>=g_out_n)return -1;
    out_rect* r=&g_out[o];
    if(*x+w>r->x+r->w)*x=r->x+r->w-w;
    if(*y+h>r->y+r->h)*y=r->y+r->h-h;
    if(*x<r->x)*x=r->x;
    if(*y<r->y)*y=r->y;
    return 0; }
int out_move_rect(int* x,int* y,int w,int h,int from,int to){
    if(from<0||from>=g_out_n||to<0||to>=g_out_n)return -1;
    *x=*x-g_out[from].x+g_out[to].x; *y=*y-g_out[from].y+g_out[to].y; return out_place(x,y,w,h,to); }
void out_max_rect(int o,int top,int bot,int* x,int* y,int* w,int* h){
    if(o<0||o>=g_out_n)o=0;
    out_rect* r=&g_out[o]; *x=r->x; *y=r->y+top; *w=r->w; *h=r->h-top-bot; if(*h<0)*h=0; }
static int out_edge_shared(int o,int e){
    if(g_out_n<2)return 0;
    out_rect* r=&g_out[o]; out_rect* q=&g_out[1-o];
    int xo=q->x<r->x+r->w&&q->x+q->w>r->x, yo=q->y<r->y+r->h&&q->y+q->h>r->y;
    if(e==0)return xo&&q->y+q->h==r->y;
    if(e==1)return yo&&q->x+q->w==r->x;
    return yo&&q->x==r->x+r->w; }
int out_snap_zone(int px,int py,int w,int h,int* o){
    int k=0, x0=0, y0=0; if(g_out_n>1){ k=out_at(px,py); if(k<0)k=0; x0=g_out[k].x; y0=g_out[k].y; w=g_out[k].w; h=g_out[k].h; }
    if(o)*o=k;
    px-=x0; py-=y0; int s=0;
    if(py<=3)s=3; else if(px<=3)s=(py<h/3)?4:(py>2*h/3)?6:1; else if(px>=w-4)s=(py<h/3)?5:(py>2*h/3)?7:2;
    if(s==3&&out_edge_shared(k,0))s=0;
    if((s==1||s==4||s==6)&&out_edge_shared(k,1))s=0;
    if((s==2||s==5||s==7)&&out_edge_shared(k,2))s=0;
    return s; }
void out_snap_rect(int x,int y,int ww,int wh,int o,int w,int h,int* bx,int* by,int* bw,int* bh){
    if(o<0||o>=g_out_n)o=g_out_n>1?out_of_rect(x,y,ww,wh):0;
    if(o==0||g_out_n<2){ *bx=g_out_mg; *by=g_out_top; *bw=w-2*g_out_mg; *bh=h-g_out_top-g_out_bot; return; }
    out_rect* r=&g_out[o]; *bx=r->x+6; *by=r->y+6; *bw=r->w-12; *bh=r->h-12; }
void out_desk_drop(int* x,int* y,int w,int h){
    if(g_out_n<2||out_at(*x,*y)==0)return;
    *x=*x<0?0:*x>=w?w-1:*x; *y=*y<0?0:*y>=h?h-1:*y; }
static int out_ci(int v,int lo,int hi){ return v>hi?(hi>lo?hi:lo):v<lo?lo:v; }
static int out_shift2(int* x,int* y,int w,int h,u32 ow,u32 oh,u32 nw,u32 nh){
    if(g_out_n<2||out_of_rect(*x,*y,w,h)!=1)return 0;
    if(g_out_arr==OUT_RIGHT)*x+=(int)nw-(int)ow; else if(g_out_arr==OUT_BELOW)*y+=(int)nh-(int)oh; return 1; }
void out_resize_win(int* x,int* y,int* w,int* h,int max,int rest,u32 ow,u32 oh,u32 nw,u32 nh){
    if(out_shift2(x,y,*w,*h,ow,oh,nw,nh))return;
    int mw=(int)nw-2*g_out_mg, mh=(int)nh-g_out_top-g_out_bot;
    if(rest){ *w=out_ci(*w,1,mw); *h=out_ci(*h,1,mh); *x=out_ci(*x,0,(int)nw-*w); *y=out_ci(*y,0,(int)nh-g_out_bot-*h); return; }
    if(max){ *x=g_out_mg; *y=g_out_top; *w=mw; *h=mh; return; }
    *x=(int)((long)*x*(long)nw/(long)(ow?ow:nw)); *y=(int)((long)*y*(long)nh/(long)(oh?oh:nh));
    *w=out_ci(*w,1,mw); *h=out_ci(*h,1,mh); *x=out_ci(*x,g_out_mg,(int)nw-g_out_mg-*w); *y=out_ci(*y,g_out_top,(int)nh-g_out_bot-*h); }
int out_resize_pt(int* x,int* y,u32 ow,u32 oh,u32 nw,u32 nh){ int o=g_out_n>1?out_at(*x,*y):0; if(o==1)out_shift2(x,y,1,1,ow,oh,nw,nh); return o; }
