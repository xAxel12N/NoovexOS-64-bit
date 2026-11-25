#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "include/nvx/base.h"
void gfx_sync(void){}
const char* gk_name(int id){ (void)id; return "op"; }
#include "drivers/video/fb/gq.c"
#include "drivers/video/nvidia/nv2dq.c"
#include "assets/fontaa.c"
#include "drivers/video/fb/gtext.c"
static int fails=0, marks=0, mark_atl=0, atl_sid=0;
static void chk(const char* t,int ok){ printf("  %s %s\n",t,ok?"ok":"FAIL"); if(!ok)fails++; }
static void mh(int s,int x,int y,int w,int h){ (void)x; (void)y; (void)w; (void)h; marks++; if(s==atl_sid)mark_atl++; }
#define TW 640
#define TH 200
static u32 D1[TW*TH], D2[TW*TH], STM[TW*TH], STC[TW*TH], AT[NV2Q_GW*NV2Q_GH];
static gtatl GA;
static void cpu_run(int d,int sm,int sc,const gly* g,int n){
    int x0=1<<30,y0=1<<30,x1=-(1<<30),y1=-(1<<30), mono=1; for(int k=0;k<n;k++){ if(g[k].x<x0)x0=g[k].x; if(g[k].y<y0)y0=g[k].y; if(g[k].x+8*g[k].s>x1)x1=g[k].x+8*g[k].s; if(g[k].y+14*g[k].s>y1)y1=g[k].y+14*g[k].s; if((g[k].c^g[0].c)&0xFFFFFFu)mono=0; }
    int w=x1-x0, h=y1-y0; u32 key=gt_stage(STM,TW,STC,TW,0,0,w,h,g,n,x0,y0,mono); gq_set_key(key|1u);
    if(mono)gq_mask(d,x0,y0,w,h,g[0].c&0xFFFFFFu,256,sm,0,0); else gq_copym(d,x0,y0,w,h,sc,0,0,sm,0,0,256,0); }
int main(void){
    g_gq_mode=GQM_CPU; g_gq_mark_hook=mh;
    int s1=gq_surf(D1,0,TW,TH,TW), s2=gq_surf(D2,0,TW,TH,TW), sm=gq_surf(STM,0,TW,TH,TW), sc=gq_surf(STC,0,TW,TH,TW); atl_sid=gq_surf(AT,0,NV2Q_GW,NV2Q_GH,NV2Q_GW);
    u32 rs=0xBEEF1u; for(int i=0;i<TW*TH;i++){ rs^=rs<<13; rs^=rs>>17; rs^=rs<<5; D1[i]=D2[i]=rs; }
    g_gfx_atlas=1; gt_init(&GA,atl_sid,AT,NV2Q_GW); static gly g[GT_MAXN]; int runs=0, used=0, glyphs=0;
    for(int t=0;t<400;t++){ rs^=rs<<13; rs^=rs>>17; rs^=rs<<5; int s=1+(int)(rs%3u==0), mode=(int)((rs>>3)%3u), n=1+(int)((rs>>6)%30u); int x=(int)((rs>>11)%(TW+40))-20, y=(int)((rs>>21)%(TH+20))-14; u32 c=rs*2654435761u; int multi=(rs>>30)&1;
        for(int k=0,cx=x;k<n;k++){ rs^=rs<<13; rs^=rs>>17; rs^=rs<<5; g[k].x=(short)cx; cx+=8*s+(int)(rs&1); g[k].y=(short)y; g[k].ch=(char)(32+(rs>>4)%96u); g[k].mode=(u8)mode; g[k].s=(u8)s; g[k].c=multi?(rs*40503u):c; }
        cpu_run(s1,sm,sc,g,n); GA.ops=0; int u=gt_run(&GA,s2,g,n); if(u)used++; else cpu_run(s2,sm,sc,g,n); runs++; glyphs+=n;
        if(getenv("GTDBG")&&memcmp(D1,D2,sizeof D1)){ for(int i=0;i<TW*TH;i++) if(D1[i]!=D2[i]){ printf("run %d used %d mode %d s %d first diff %d,%d %08x %08x\n",t,u,mode,s,i%TW,i/TW,D1[i],D2[i]); break; } return 1; } }
    printf("  %d runs, %d glyphs, %d drawn from the atlas, %u atlas glyphs, %u hits\n",runs,glyphs,used,GA.a.n,GA.a.hits);
    chk("400 random text runs (3 coverage modes, scale 1-2, mixed colours, clipped at the edges): atlas glyph ops are pixel-exact against the CPU text renderer",!memcmp(D1,D2,sizeof D1)&&used>300);
    chk("each glyph is rasterised into the atlas once and new cells are marked for the VRAM copy",GA.a.miss==GA.a.n&&GA.a.hits>GA.a.n&&mark_atl==(int)GA.a.n);
    { gly o[2]={{10,10,'A',0,1,0xFFFFFF},{14,10,'B',0,1,0xFFFFFF}}; u32 f0=g_gt_fall; chk("overlapping glyph cells are refused, so the run goes through the CPU staging path",!gt_run(&GA,s2,o,2)&&g_gt_fall==f0+1); }
    { gly o[1]={{0,0,'A',0,1,0xFFFFFF}}; GA.ops=GT_LAYOPS; chk("past the per-layer op budget the run goes through the staging path, so layer capture never overflows",!gt_run(&GA,s2,o,1)); GA.ops=0; }
    { nv2qa_reset(&GA.a); int full=0; for(int s=1;s<=8&&!full;s++) for(int ch=33;ch<127&&!full;ch++){ gly o[1]={{0,0,(char)ch,0,(u8)s,0xFFFFFF}}; if(!gt_run(&GA,s2,o,1))full=1; }
      u32 e0=GA.ep; int fl=GA.reset; gt_frame(&GA); chk("a full atlas falls back per run and is reset at the next frame start under a new key epoch",full&&fl&&GA.ep==e0+1&&GA.a.n==0&&!GA.reset); }
    { g_gfx_atlas=0; gly o[1]={{0,0,'A',0,1,0xFFFFFF}}; chk("with the atlas off every run goes through the staging path",!gt_run(&GA,s2,o,1)); }
    printf(fails?"FAILURES\n":"ALL PASS\n"); return fails?1:0; }
