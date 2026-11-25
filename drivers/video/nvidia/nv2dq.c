#include "include/nvx/base.h"
#include "include/nvx/drivers/video/nvidia/nv2dq.h"

static u32* q_h(u32* p,u32 m,int n){ *p++=(1u<<29)|((u32)n<<16)|(3u<<13)|(m>>2); return p; }
static u32* q_1(u32* p,u32 m,u32 v){ p=q_h(p,m,1); *p++=v; return p; }
void nv2q_reset(nv2q* q){ q->init=0; q->dva=q->sva=0; q->dpb=q->dw=q->dh=q->spb=q->sw=q->sh=0; q->rop=0; }
int nv2q_surf_ok(const nv2qs* s){ return s&&s->va&&!(s->va&255ULL)&&s->pitchb&&!(s->pitchb&63u)&&s->w&&s->h&&s->w<=NV2Q_MAXW&&s->h<=NV2Q_MAXW&&s->pitchb>=s->w*4u; }
static int q_in(const nv2qs* s,int x,int y,int w,int h){ return w>0&&h>0&&x>=0&&y>=0&&(u32)x+(u32)w<=s->w&&(u32)y+(u32)h<=s->h; }
static u32* q_pre(nv2q* q,u32* p,int wfi){
    if(wfi)p=q_1(p,0x0110,0);
    if(!q->init){ p=q_1(p,0x0000,0x902Du); p=q_1(p,0x0290,0); p=q_1(p,0x029C,0); p=q_1(p,0x0884,0x3F); p=q_1(p,0x0888,1); p=q_1(p,0x026C,1); p=q_1(p,0x088C,0); q->init=1; q->rop=0; q->dva=q->sva=0; }
    if(q->rop!=3){ p=q_1(p,0x02AC,3); q->rop=3; }
    return p; }
static u32* q_dst(nv2q* q,u32* p,const nv2qs* d){
    if(q->dva==d->va&&q->dpb==d->pitchb&&q->dw==d->w&&q->dh==d->h)return p;
    p=q_h(p,0x0200,2); *p++=NV2Q_FMT; *p++=1; p=q_h(p,0x0214,5); *p++=d->pitchb; *p++=d->w; *p++=d->h; *p++=(u32)(d->va>>32); *p++=(u32)d->va;
    q->dva=d->va; q->dpb=d->pitchb; q->dw=d->w; q->dh=d->h; return p; }
static u32* q_src(nv2q* q,u32* p,const nv2qs* s){
    if(q->sva==s->va&&q->spb==s->pitchb&&q->sw==s->w&&q->sh==s->h)return p;
    p=q_h(p,0x0230,2); *p++=NV2Q_FMT; *p++=1; p=q_h(p,0x0244,5); *p++=s->pitchb; *p++=s->w; *p++=s->h; *p++=(u32)(s->va>>32); *p++=(u32)s->va;
    q->sva=s->va; q->spb=s->pitchb; q->sw=s->w; q->sh=s->h; return p; }
int nv2q_fill(nv2q* q,u32* out,const nv2qs* d,int x,int y,int w,int h,u32 c,int wfi){
    if(!nv2q_surf_ok(d)||!q_in(d,x,y,w,h))return 0;
    u32* p=q_pre(q,out,wfi); p=q_dst(q,p,d);
    p=q_1(p,0x0580,4); p=q_1(p,0x0584,NV2Q_FMT); p=q_1(p,0x0588,c);
    p=q_h(p,0x0600,4); *p++=(u32)x; *p++=(u32)y; *p++=(u32)(x+w); *p++=(u32)(y+h);
    q->fills++; return (int)(p-out); }
int nv2q_copy(nv2q* q,u32* out,const nv2qs* d,int dx,int dy,int w,int h,const nv2qs* s,int sx,int sy,int same,int wfi){
    if(!nv2q_surf_ok(d)||!nv2q_surf_ok(s)||!q_in(d,dx,dy,w,h)||!q_in(s,sx,sy,w,h))return 0;
    if(same&&dx<sx+w&&sx<dx+w&&dy<sy+h&&sy<dy+h)return 0;
    u32* p=q_pre(q,out,wfi); p=q_dst(q,p,d); p=q_src(q,p,s);
    p=q_h(p,0x08B0,12); *p++=(u32)dx; *p++=(u32)dy; *p++=(u32)w; *p++=(u32)h; *p++=0; *p++=1; *p++=0; *p++=1; *p++=0; *p++=(u32)sx; *p++=0; *p++=(u32)sy;
    q->copies++; return (int)(p-out); }
void nv2qa_reset(nv2qa* a){ for(int i=0;i<NV2Q_GN;i++)a->e[i].key=0; a->n=0; a->sx=0; a->sy=0; a->sh=0; }
void nv2q_glyph_raster(u32* px,u32 pitch,const unsigned char* g,int mode,int s){
    for(int r=0;r<14*s;r++) for(int c=0;c<8*s;c++) px[(u64)r*pitch+(u32)c]=0;
    for(int r=0;r<14;r++) for(int col=0;col<8;col++){ int a=g[r*8+col]; u32 cov;
        if(mode==0){ if(a<6)continue; cov=a>=250?256u:(u32)a; } else if(mode==1){ if(a<6)continue; cov=(u32)a; } else { if(a<8)continue; cov=(u32)a; }
        for(int dy=0;dy<s;dy++) for(int dx=0;dx<s;dx++) px[(u64)(r*s+dy)*pitch+(u32)(col*s+dx)]=cov; } }
int nv2qa_get(nv2qa* a,u32* px,u32 pitch,const unsigned char* g,int ch,int mode,int s,int* ox,int* oy){
    if(s<1||s>8||mode<0||mode>2)return -1;
    u32 key=0x80000000u|((u32)(u8)ch)|((u32)mode<<8)|((u32)s<<16); u32 h=(key*2654435761u)>>22;
    for(int k=0;k<NV2Q_GN;k++){ nv2qg* e=&a->e[(h+(u32)k)%NV2Q_GN];
        if(e->key==key){ *ox=e->x; *oy=e->y; a->hits++; return 0; }
        if(!e->key){ int w=8*s, gh=14*s; if(a->n>=NV2Q_GN*3/4){ a->full++; return -1; }
            if(a->sx+w>NV2Q_GW){ a->sy+=a->sh; a->sx=0; a->sh=0; }
            if(a->sy+gh>NV2Q_GH){ a->full++; return -1; }
            e->key=key; e->x=(u16)a->sx; e->y=(u16)a->sy; a->sx+=w; if(gh>a->sh)a->sh=gh; a->n++; a->miss++;
            nv2q_glyph_raster(px+(u64)e->y*pitch+e->x,pitch,g,mode,s); *ox=e->x; *oy=e->y; return 0; } }
    a->full++; return -1; }
