#include "include/nvx/base.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/graphics/ui/desktop.h"
#include "include/nvx/graphics/ui/curshape.h"

extern volatile u64 g_ticks;
extern int mx, my, g_dstyle;
extern unsigned long web_busy_ms(void);
int g_cur_shape=CUR_ARROW, g_cur_force=-1;
static u64 cs_busy_t;
void cur_busy_note(void){ cs_busy_t=g_ticks?g_ticks:1; }
int cur_text_app(int i){ return i==W_EDITOR||i==W_TERM||i==W_WRITER||i==W_CHATGPT||i==W_TFIND; }
int cur_busy_phase(void){ return (int)((g_ticks/12)&7); }
static int cs_top(int px,int py){ int t=-1, tz=-1;
    for(int i=0;i<NWIN;i++){ Win* n=&WN[i]; if(n->open&&!n->min&&n->ws==g_ws&&px>=n->x&&py>=n->y&&px<n->x+n->w&&py<n->y+n->h&&n->z>tz){ tz=n->z; t=i; } }
    return t; }
int cur_shape_pick(void){
    if(g_cur_force>=0&&g_cur_force<CUR_N)return g_cur_force;
    if(g_state!=ST_DESKTOP)return CUR_ARROW;
    if(cs_busy_t&&g_ticks-cs_busy_t<15)return CUR_WAIT;
    if(drag>=0)return CUR_MOVE;
    int s=CUR_ARROW, t=cs_top(mx,my);
    if(t>=0){ Win* n=&WN[t]; if(cur_text_app(t)&&my>=n->y+TITLEH&&mx>=n->x+4&&mx<n->x+n->w-4&&my<n->y+n->h-4)s=CUR_TEXT; }
    else if(g_dstyle!=1&&DOCK_w>0&&mx>=DOCK_x&&mx<DOCK_x+DOCK_w&&my>=DOCK_y&&my<DOCK_y+DOCK_h)s=CUR_HAND;
    if(s==CUR_ARROW&&web_busy_ms()>500)s=CUR_BUSY;
    return s; }
static int cs_oct(int i,int j){ int ax=i<0?-i:i, ay=j<0?-j:j;
    return i>=0&&j<0?(ax<ay?0:1):i>=0?(ax>=ay?2:3):j>=0?(ax<ay?4:5):(ax>=ay?6:7); }
static void cs_ring(int cx,int cy,int r,int ph,u32 c){
    for(int j=-r;j<=r;j++)for(int i=-r;i<=r;i++){ int d=i*i+j*j; if(d>r*r||d<(r-3)*(r-3))continue; if(((cs_oct(i,j)-ph)&7)<2)continue; putpx(cx+i,cy+j,c); } }
static void cs_beam(int px,int py){ u32 d=rgb(18,20,26), w=rgb(255,255,255);
    for(int y=-9;y<=9;y++){ fill_rect(px-1,py+y,3,1,d); putpx(px,py+y,w); }
    fill_rect(px-4,py-10,9,3,d); fill_rect(px-3,py-9,7,1,w); fill_rect(px-4,py+8,9,3,d); fill_rect(px-3,py+9,7,1,w); }
static void cs_hand(int px,int py){ u32 d=rgb(18,20,26), w=rgb(255,255,255);
    fill_rect(px-2,py,6,12,d); fill_rect(px-1,py+1,4,11,w);
    fill_rect(px-2,py+9,16,12,d); fill_rect(px-1,py+10,14,10,w);
    for(int k=0;k<3;k++){ fill_rect(px+4+k*3,py+8,1,6,d); } }
static void cs_move(int px,int py){ u32 d=rgb(18,20,26), w=rgb(255,255,255);
    fill_rect(px-11,py-1,23,3,d); fill_rect(px-1,py-11,3,23,d); fill_rect(px-10,py,21,1,w); fill_rect(px,py-10,1,21,w);
    for(int k=0;k<4;k++){ fill_rect(px-10+k,py-k,1,2*k+1,d); fill_rect(px+10-k,py-k,1,2*k+1,d); fill_rect(px-k,py-10+k,2*k+1,1,d); fill_rect(px-k,py+10-k,2*k+1,1,d); } }
static void cs_no(int px,int py){ u32 r=rgb(220,40,40);
    for(int j=-9;j<=9;j++)for(int i=-9;i<=9;i++){ int d=i*i+j*j; if((d<=81&&d>=49)||(d<=49&&(i-j<=1&&j-i<=1)))putpx(px+i,py+j,r); } }
int cur_shape_draw(int px,int py,int shape){
    switch(shape){
    case CUR_TEXT: cs_beam(px,py); return 1;
    case CUR_WAIT: cs_ring(px,py,9,cur_busy_phase(),rgb(18,20,26)); cs_ring(px,py,7,cur_busy_phase(),ACC); return 1;
    case CUR_BUSY: cs_ring(px+16,py+20,6,cur_busy_phase(),ACC); return 0;
    case CUR_HAND: cs_hand(px,py); return 1;
    case CUR_MOVE: cs_move(px,py); return 1;
    case CUR_NO: cs_no(px,py); return 1;
    default: return 0; } }
