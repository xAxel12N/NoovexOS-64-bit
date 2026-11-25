#include "include/nvx/drivers/video/fb/gq.h"

#define VL_TW 64
#define VL_TH 16
#define VL_RMAX 512
typedef struct { u64 ram, va, vram, len, lru; u32 w, h, pitch, tw, th, nd, used_b, dirty_b; u8* d; int on; } vlay;
static vlay VL[GQ_MAXS];
int g_vl_on=1; u64 g_vl_budget=0, g_vl_bytes=0; u32 g_vl_up_tiles=0, g_vl_up_rects=0, g_vl_evicts=0, g_vl_hits=0, g_vl_miss=0, g_vl_fail=0, g_vl_n=0;
static u32 vl_b=1, vl_gen=0xFFFFFFFFu; static u64 vl_clock=0; static dmg_rect vl_r[VL_RMAX];

static void vl_drop(int s,int give){ vlay* v=&VL[s]; if(!v->on)return;
    if(give){ nvgsp_ce_unmap(v->va,v->len); nvv_free(v->vram); }
    if(v->d)kfree(v->d); if(g_vl_bytes>=v->len)g_vl_bytes-=v->len; else g_vl_bytes=0; if(g_vl_n)g_vl_n--;
    u8* p=(u8*)v; for(u32 i=0;i<sizeof(*v);i++)p[i]=0; }
void nvl_drop_all(int give){ for(int s=1;s<GQ_MAXS;s++)vl_drop(s,give&&vl_gen==g_ce_gen); }
void nvl_free(int s){ if(s>0&&s<GQ_MAXS)vl_drop(s,vl_gen==g_ce_gen); }
void nvl_mark(int s,int x,int y,int w,int h,int gpu){
    if(s<=0||s>=GQ_MAXS||!VL[s].on)return; vlay* v=&VL[s];
    if(gpu)v->dirty_b=vl_b; if(v->nd>=v->tw*v->th)return;
    if(w<0||h<0){ x=0; y=0; w=(int)v->w; h=(int)v->h; }
    if(x<0){ w+=x; x=0; } if(y<0){ h+=y; y=0; } if(x+w>(int)v->w)w=(int)v->w-x; if(y+h>(int)v->h)h=(int)v->h-y; if(w<=0||h<=0)return;
    u32 tx0=(u32)x/VL_TW, tx1=(u32)(x+w-1)/VL_TW, ty0=(u32)y/VL_TH, ty1=(u32)(y+h-1)/VL_TH;
    for(u32 ty=ty0;ty<=ty1;ty++) for(u32 tx=tx0;tx<=tx1;tx++){ u8* t=&v->d[ty*v->tw+tx]; if(!*t){ *t=1; v->nd++; } }
    if(gpu)v->dirty_b=vl_b; }
void nvl_cpu_mark(int s,int x,int y,int w,int h){ nvl_mark(s,x,y,w,h,0); }
static u64 vl_cap(void){ if(g_vl_budget)return g_vl_budget; u64 f=(nvv_free_bytes()+g_vl_bytes)/2; return f>0x20000000ULL?0x20000000ULL:f; }
static int vl_evict_one(void){ int best=0; u64 bl=~0ULL;
    for(int s=1;s<GQ_MAXS;s++) if(VL[s].on&&VL[s].used_b!=vl_b&&VL[s].lru<bl){ bl=VL[s].lru; best=s; }
    if(!best)return 0; vl_drop(best,1); g_vl_evicts++; return 1; }
static int vl_attach(int s){
    gsurf* g=&g_gs[s]; vlay* v=&VL[s]; u64 len=((u64)g->pitch*g->h*4+0xFFFFULL)&~0xFFFFULL; if(!g->w||!g->h||!g->gfx||len>vl_cap())return 0;
    while(g_vl_bytes+len>vl_cap()) if(!vl_evict_one())return 0;
    u64 vram; while(!(vram=nvv_alloc_raw(len,0x10000))) if(!vl_evict_one())return 0;
    u32 tw=(g->w+VL_TW-1)/VL_TW, th=(g->h+VL_TH-1)/VL_TH; u8* d=(u8*)kmalloc((unsigned long)tw*th); if(!d){ nvv_free(vram); return 0; }
    u64 va=va_take(len,0x10000); if(!nvm_map(&g_uv,va,vram,len,0,0,0)){ nvm_unmap(&g_uv,va,len); nvv_free(vram); kfree(d); return 0; }
    nvm_tlb_flush(g_uv.pd3,0);
    for(u32 i=0;i<tw*th;i++)d[i]=1;
    v->ram=g->gfx; v->va=va; v->vram=vram; v->len=len; v->w=g->w; v->h=g->h; v->pitch=g->pitch; v->tw=tw; v->th=th; v->nd=tw*th; v->d=d; v->used_b=0; v->dirty_b=0; v->on=1;
    g_vl_bytes+=len; g_vl_n++; return 1; }
static int vl_upload(vlay* v){
    int n=0;
    for(u32 ty=0;ty<v->th;ty++){
        for(u32 tx=0;tx<v->tw;){ if(!v->d[ty*v->tw+tx]){ tx++; continue; } u32 t0=tx; while(tx<v->tw&&v->d[ty*v->tw+tx])tx++;
            int x=(int)(t0*VL_TW), w=(int)(tx*VL_TW>v->w?v->w:tx*VL_TW)-x, y=(int)(ty*VL_TH), h=(int)((ty+1)*VL_TH>v->h?v->h:(ty+1)*VL_TH)-y, joined=0;
            for(int k=0;k<n;k++) if(vl_r[k].x==x&&vl_r[k].w==w&&vl_r[k].y+vl_r[k].h==y){ vl_r[k].h+=h; joined=1; break; }
            if(joined)continue;
            if(n>=VL_RMAX){ n=1; vl_r[0].x=0; vl_r[0].y=0; vl_r[0].w=(int)v->w; vl_r[0].h=(int)v->h; goto go; }
            vl_r[n].x=x; vl_r[n].y=y; vl_r[n].w=w; vl_r[n].h=h; n++; } }
    go:
    if(n&&!ce_rects(v->ram,v->pitch*4,v->va,v->pitch*4,vl_r,n))return 0;
    g_vl_up_tiles+=v->nd; g_vl_up_rects+=(u32)n; for(u32 i=0;i<v->tw*v->th;i++)v->d[i]=0; v->nd=0; return 1; }
u64 nvl_src(int s){
    if(s<=0||s>=GQ_MAXS)return 0; gsurf* g=&g_gs[s];
    if(!g_vl_on||!nvgsp_ce_ready()||G.state<7||!g_uv.ok)return g->gfx;
    if(vl_gen!=g_ce_gen){ nvl_drop_all(0); vl_gen=g_ce_gen; }
    vlay* v=&VL[s];
    if(v->on&&(v->ram!=g->gfx||v->w!=g->w||v->h!=g->h||v->pitch!=g->pitch))vl_drop(s,1);
    if(!v->on&&(g_vl_fail>=3||!vl_attach(s))){ g_vl_miss++; return g->gfx; }
    v->lru=++vl_clock;
    if(v->nd){ if(v->used_b==vl_b||v->dirty_b==vl_b){ g_vl_miss++; return g->gfx; }
        if(!vl_upload(v)){ g_vl_fail++; g_vl_miss++; vl_drop(s,1); return g->gfx; } }
    v->used_b=vl_b; g_vl_hits++; return v->va; }
void nvl_batch_end(int ok){ if(!ok) for(int s=1;s<GQ_MAXS;s++) if(VL[s].on)nvl_mark(s,0,0,-1,-1,0); vl_b++; if(!vl_b)vl_b=1; }
int nvl_dirty_tiles(int s){ return s>0&&s<GQ_MAXS&&VL[s].on?(int)VL[s].nd:-1; }
