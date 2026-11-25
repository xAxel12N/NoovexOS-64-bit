#include <stdio.h>
#include <string.h>
typedef unsigned int u32; typedef unsigned char u8; typedef unsigned long long u64;
static int bad=0;
#define T(c) do{ if(!(c)){ printf("FAIL %s line %d\n",#c,__LINE__); bad++; } }while(0)
typedef struct { int x,y,w,h,open,min; } Win;
enum { W_SNAKE, W_FLAPPY, W_PAINT, NWIN };
Win WN[NWIN]; u64 g_ticks=1000;
#define TITLEH 28
#define rgb(r,g,b) ((((u32)(r))<<16)|(((u32)(g))<<8)|((u32)(b)))
#include "smallapps_ext.c"
static int painted(int x,int y){ return paint_buf[y*PCW+x]==PPAL[0]; }
int main(void){
    WN[W_SNAKE]=(Win){380,110,446,462,1,0}; WN[W_FLAPPY]=(Win){400,110,420,470,1,0}; WN[W_PAINT]=(Win){300,110,620,440,1,0};
    snake_reset(); T(snake_state==0); T(snlen==4); T(snake_score==0);
    snake_key('x'); T(snake_state==1); T(sdx==1&&sdy==0);
    snake_reset(); snake_key(19); T(snake_state==1); T(sdx==0&&sdy==-1);
    snake_reset(); snake_key(20); T(snake_state==1); T(sdx==0&&sdy==1);
    snake_reset(); snake_start(); for(int k=0;k<SN_GW+2 && snake_state==1;k++) snake_step(); T(snake_state==2);
    snake_key(17); T(snake_state==1); T(snake_score==0); T(snlen==4);
    snake_reset(); snake_start(); snake_state=2; snake_start(); T(snake_state==1); T(snlen==4);
    flappy_reset(); T(fb_state==0); T(fb_score==0);
    flappy_key('a'); T(fb_state==1); T(fb_vy==-8);
    flappy_reset(); flappy_key('a'); T(fb_state==1);
    flappy_reset(); flappy_key(' '); T(fb_state==1);
    fb_state=2; fb_score=5; flappy_flap(); T(fb_state==1); T(fb_score==0); T(fb_vy==-8);
    fb_state=1; fb_vy=0; flappy_key('a'); T(fb_vy==0); flappy_key(' '); T(fb_vy==-8);
    { int bw,bh; fb_dims(&bw,&bh); int oy=WN[W_FLAPPY].y+TITLEH+40; T(oy+bh+8+14 <= WN[W_FLAPPY].y+WN[W_FLAPPY].h); }
    for(int i=0;i<PCW*PCH;i++)paint_buf[i]=rgb(255,255,255); pcx=0; pcy=0; paint_size=0; paint_col=0;
    paint_end(); paint_at(10,10); paint_at(100,10);
    { int ok=1; for(int x=10;x<=100;x++) if(!painted(x,10)) ok=0; T(ok); }
    T(!painted(10,20)); T(!painted(100,20));
    paint_at(100,100);
    { int ok=1; for(int k=0;k<=90;k++) if(!painted(100,10+k)) ok=0; T(ok); }
    paint_end(); paint_at(10,100);
    T(!painted(55,100)); T(painted(10,100));
    paint_end(); paint_at(-50,200); T(!painted(0,200));
    paint_at(50,200); T(!painted(0,200)); T(painted(50,200));
    paint_at(20,200); T(painted(35,200));
    paint_end(); paint_at(PCW+10,300); paint_at(PCW-5,300); T(painted(PCW-5,300)); T(!painted(PCW-1,300));
    if(!bad)printf("smallapps: all ok\n"); return bad?1:0; }
