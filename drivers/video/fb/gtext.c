#include "include/nvx/base.h"
#include "include/nvx/assets/fontaa.h"
#include "include/nvx/drivers/video/fb/gq.h"
#include "include/nvx/drivers/video/fb/gtext.h"

int g_gfx_atlas=0; u32 g_gt_runs=0, g_gt_glyphs=0, g_gt_fall=0, g_gt_resets=0;
static u32 gt_h(u32 h,u32 v){ h^=v; h*=16777619u; return h; }
u32 gt_stage(u32* stm,u32 mp,u32* stc,u32 cp,int sx,int sy,int w,int h,const gly* g,int n,int x0,int y0,int mono){
    u32 key=2166136261u;
    for(int j=0;j<h;j++){ u32* m=stm+(u64)(sy+j)*mp+sx; for(int i=0;i<w;i++)m[i]=0; }
    for(int k=0;k<n;k++){ const gly* G=&g[k]; int idx=(u8)G->ch-32; if(idx<0||idx>=95)continue; const unsigned char* gp=fontaa[idx]; int bx=G->x-x0, by=G->y-y0, s=G->s;
        key=gt_h(key,(u32)(u16)G->x|((u32)(u16)G->y<<16)); key=gt_h(key,(u32)(u8)G->ch|((u32)G->mode<<8)|((u32)s<<16)); key=gt_h(key,G->c&0x00FFFFFFu);
        for(int r=0;r<14;r++) for(int col=0;col<8;col++){ int a=gp[r*8+col]; u32 cov;
            if(G->mode==0){ if(a<6)continue; cov=a>=250?256u:(u32)a; } else if(G->mode==1){ if(a<6)continue; cov=(u32)a; } else { if(a<8)continue; cov=(u32)a; }
            for(int dy=0;dy<s;dy++) for(int dx=0;dx<s;dx++){ int px=bx+col*s+dx, py=by+r*s+dy; if(px<0||px>=w||py<0||py>=h)continue;
                u32* m=stm+(u64)(sy+py)*mp+sx+px; u32 o=*m; *m=o+cov-((o*cov)>>8); if(*m>256)*m=256;
                if(!mono) stc[(u64)(sy+py)*cp+sx+px]=G->c&0x00FFFFFFu; } } }
    return key; }
void gt_init(gtatl* A,int sid,u32* px,u32 pitch){ A->sid=sid; A->px=px; A->pitch=pitch; A->ops=0; A->reset=0; A->ep++; nv2qa_reset(&A->a); }
void gt_frame(gtatl* A){ A->ops=0; if(!A->reset||A->sid<=0)return; gq_sync(); nv2qa_reset(&A->a); A->reset=0; A->ep++; g_gt_resets++; gq_bump(A->sid); }
int gt_run(gtatl* A,int t,const gly* g,int n){
    if(!g_gfx_atlas||A->sid<=0||!A->px||n<=0||n>GT_MAXN||A->ops+n>GT_LAYOPS){ g_gt_fall++; return 0; }
    static u16 cx[GT_MAXN], cy[GT_MAXN];
    for(int k=0;k<n;k++){ const gly* G=&g[k]; int w=8*G->s, h=14*G->s;
        for(int j=0;j<k;j++){ const gly* P=&g[j]; if(G->x<P->x+8*P->s&&P->x<G->x+w&&G->y<P->y+14*P->s&&P->y<G->y+h){ g_gt_fall++; return 0; } } }
    for(int k=0;k<n;k++){ const gly* G=&g[k]; int idx=(u8)G->ch-32, ax, ay; if(idx<0||idx>=95){ cx[k]=0xFFFF; continue; }
        u32 m0=A->a.miss; if(nv2qa_get(&A->a,A->px,A->pitch,fontaa[idx],G->ch,G->mode,G->s,&ax,&ay)){ A->reset=1; g_gt_fall++; return 0; }
        if(A->a.miss!=m0&&g_gq_mark_hook)g_gq_mark_hook(A->sid,ax,ay,8*G->s,14*G->s);
        cx[k]=(u16)ax; cy[k]=(u16)ay; }
    for(int k=0;k<n;k++){ const gly* G=&g[k]; if(cx[k]==0xFFFF)continue;
        u32 key=gt_h(gt_h(gt_h(2166136261u,A->ep),(u32)(u8)G->ch|((u32)G->mode<<8)|((u32)G->s<<16)),((u32)cx[k]<<16)|cy[k]);
        gq_set_key(key|1u); gq_mask(t,G->x,G->y,8*G->s,14*G->s,G->c&0x00FFFFFFu,256,A->sid,cx[k],cy[k]); }
    A->ops+=n; g_gt_runs++; g_gt_glyphs+=(u32)n; return 1; }
