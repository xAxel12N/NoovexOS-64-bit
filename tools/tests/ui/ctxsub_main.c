#include <stdio.h>
typedef unsigned int u32;
static int bad=0;
#define T(c) do{ if(!(c)){ printf("FAIL %s line %d\n",#c,__LINE__); bad++; } }while(0)
u32 W=1280, H=800; int g_dstyle=1; int g_sub_x, g_sub_y;
#define CTX_W 196
#define CTX_IHV (g_dstyle==1?24:30)
#define CTX_PADV (g_dstyle==1?3:5)
static void out_ctx_bounds(int* w,int* h){ *w=(int)W; *h=(int)H; }
#include "ctxsub_ext.c"
int main(void){
    ctx_sub_place(100,200,3,6); T(g_sub_x==100+CTX_W-4); T(g_sub_y==197);
    ctx_sub_place(1280-6-CTX_W,200,3,6); T(g_sub_x==1280-6-CTX_W-130+4); T(g_sub_x+130<=1280-6);
    g_dstyle=0; ctx_sub_place(1100,300,4,6); T(g_sub_x+130<=1280-6); T(g_sub_x<1100);
    ctx_sub_place(100,780,4,6); T(g_sub_y+6*30+10<=800-70);
    ctx_sub_place(100,2,4,2); T(g_sub_y>=6);
    printf(bad?"CTXSUB %d failures\n":"CTXSUB all ok\n",bad); return bad?1:0;
}
