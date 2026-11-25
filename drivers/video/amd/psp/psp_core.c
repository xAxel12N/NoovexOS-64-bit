#include "drivers/video/amd/psp/psp_core.h"

#define APV(a,b,c) (((u32)(a)<<16)|((u32)(b)<<8)|(u32)(c))
static const aps_gen_t aps_tab[]={
    {APV(11,5,0),"PSP 11.5.0 (Van Gogh)",0,"amdgpu/vangogh_toc.bin"},
    {APV(13,0,1),"PSP 13.0.1 (Yellow Carp)",0,"amdgpu/yellow_carp_toc.bin"},
    {APV(13,0,3),"PSP 13.0.3 (Rembrandt)",0,"amdgpu/yellow_carp_toc.bin"},
    {APV(13,0,4),"PSP 13.0.4 (Phoenix)",1,"amdgpu/psp_13_0_4_toc.bin"},
    {APV(13,0,5),"PSP 13.0.5 (Raphael)",0,"amdgpu/psp_13_0_5_toc.bin"},
    {APV(13,0,8),"PSP 13.0.8 (Mendocino)",0,"amdgpu/psp_13_0_8_toc.bin"},
    {APV(13,0,11),"PSP 13.0.11 (Phoenix 2)",0,"amdgpu/psp_13_0_11_toc.bin"},
};

const aps_gen_t* aps_gen(u32 v){ for(unsigned i=0;i<sizeof aps_tab/sizeof aps_tab[0];i++)if(aps_tab[i].ver==v)return &aps_tab[i]; return 0; }

static u32 aps_r(const aps_t* p,u32 o){ return p->io->rd(p->io->c,p->base+o); }
static void aps_w(const aps_t* p,u32 o,u32 v){ p->io->wr(p->io->c,p->base+o,v); }
static u64 aps_now(const aps_t* p){ return p->io->ns(p->io->c); }
static void aps_delay(const aps_t* p,u32 us){ u64 t=aps_now(p)+(u64)us*1000ULL; while(aps_now(p)<t){} }

static int aps_wait(aps_t* p,u32 o,u32 us,u32* v){
    u64 t=aps_now(p)+(u64)us*1000ULL;
    for(;;){ u32 x=aps_r(p,o); *v=x; if(x==0xFFFFFFFFu)return -2; if(x&0x80000000u)return 0; if(aps_now(p)>t)return -1; } }

int aps_alive(aps_t* p){ if(!p->g)return 0; u32 x=aps_r(p,APS_C81); return x&&x!=0xFFFFFFFFu; }

int aps_ring_up(aps_t* p,u32 us){
    if(p->ring)return 0;
    if(!p->g||!p->mem||!p->mc||(p->mc&0xFFFULL)){ p->why="no PSP generation or ring memory"; return -1; }
    u32 s=aps_r(p,APS_C81), v=0;
    if(s==0xFFFFFFFFu){ p->why="PSP registers read all ones"; return -2; }
    if(!s){ p->why="the PSP secure OS is not running (system firmware did not start it)"; return -3; }
    int r=aps_wait(p,APS_C64,us,&v);
    if(r==-2){ p->why="PSP registers read all ones"; return -2; }
    if(r){ p->timeouts++; p->why="the PSP is held by the system firmware: it never offered a driver ring"; return -4; }
    if(v&0xFFFFu){ p->last=v&0xFFFFu; p->why="the PSP reported an error before ring creation"; return -4; }
    for(u32 i=0;i<APS_FW;i+=4)*(volatile u32*)(p->mem+i)=0;
    u64 rm=p->mc+APS_RING;
    aps_w(p,APS_C69,(u32)rm); aps_w(p,APS_C70,(u32)(rm>>32)); aps_w(p,APS_C71,APS_RING_SZ); aps_w(p,APS_C64,2u<<16);
    aps_delay(p,20000);
    r=aps_wait(p,APS_C64,us,&v);
    if(r==-2){ p->why="PSP registers read all ones"; return -2; }
    if(r){ p->timeouts++; p->why="the PSP did not answer the ring creation"; return -5; }
    if(v&0xFFFFu){ p->last=v&0xFFFFu; p->why="the PSP refused the driver ring"; return -5; }
    p->ring=1; p->fence=0; p->why="ok"; return 0; }

void aps_ring_down(aps_t* p,u32 us){
    if(!p->ring)return;
    u32 v; aps_w(p,APS_C64,3u<<16); aps_delay(p,20000); aps_wait(p,APS_C64,us,&v); p->ring=0; }

u32 aps_resp(const aps_t* p,int dw){ return ((volatile u32*)(p->mem+APS_CMD))[APS_RESP/4+(u32)dw]; }

int aps_cmd(aps_t* p,u32 id,const u32* body,int n,u32 us){
    if(!p->ring){ p->why="no PSP ring"; return -1; }
    if(n<0||n>32){ p->why="bad PSP command"; return -1; }
    volatile u32* c=(volatile u32*)(p->mem+APS_CMD); for(u32 i=0;i<256;i++)c[i]=0;
    c[2]=id; for(int i=0;i<n;i++)c[7+i]=body[i];
    u32 wp=aps_r(p,APS_C67); if(wp==0xFFFFFFFFu){ p->why="PSP registers read all ones"; return -2; }
    wp%=APS_RING_SZ/4; if(wp%16u){ p->why="the PSP ring write pointer is not on a frame"; return -1; }
    volatile u32* f=(volatile u32*)(p->mem+APS_RING)+wp; for(u32 i=0;i<16;i++)f[i]=0;
    u64 cm=p->mc+APS_CMD, fm=p->mc+APS_FENCE; u32 fv=++p->fence;
    f[0]=(u32)cm; f[1]=(u32)(cm>>32); f[3]=(u32)fm; f[4]=(u32)(fm>>32); f[5]=fv;
    __sync_synchronize();
    aps_w(p,APS_C67,(wp+16u)%(APS_RING_SZ/4));
    volatile u32* fb=(volatile u32*)(p->mem+APS_FENCE); u64 t=aps_now(p)+(u64)us*1000ULL;
    while(fb[0]!=fv){ if(aps_now(p)>t){ p->timeouts++; p->why="the PSP did not finish the command in time"; return -1; } }
    p->cmds++; p->last=c[APS_RESP/4];
    if(p->last){ p->why="the PSP answered the command with an error status"; return 1; }
    p->why="ok"; return 0; }

static int aps_stage(aps_t* p,const u8* d,u32 n){
    if(!d||!n||n>APS_FWMAX){ p->why="firmware image does not fit the PSP staging buffer"; return -1; }
    volatile u32* o=(volatile u32*)(p->mem+APS_FW); u32 w=(n+3)/4;
    for(u32 i=0;i<w;i++){ u32 x=0; for(u32 k=0;k<4;k++){ u32 j=i*4+k; if(j<n)x|=(u32)d[j]<<(8*k); } o[i]=x; }
    return 0; }

static u32 aps_le(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
int aps_toc_file(const u8* b,u32 n,const u8** d,u32* sz){
    if(!b||n<44)return -1;
    u32 fs=aps_le(b), hs=aps_le(b+4), us=aps_le(b+20), uo=aps_le(b+24);
    if(fs!=n||hs>n||uo<hs||uo>n||!us||us>n-uo||us>APS_FWMAX)return -1;
    *d=b+uo; *sz=us; return 0; }

int aps_toc(aps_t* p,const u8* toc,u32 n,u32* tmr_sz,u32 us){
    if(aps_stage(p,toc,n))return -1;
    u64 m=p->mc+APS_FW; u32 b[3]={(u32)m,(u32)(m>>32),n};
    int r=aps_cmd(p,APS_CMD_LOAD_TOC,b,3,us); if(!r)*tmr_sz=aps_resp(p,4); return r; }

int aps_tmr(aps_t* p,u64 mc,u64 pa,u32 sz,u32 us){
    if(!sz||(sz&0xFFFu)||(mc&(APS_TMR_AL-1))||(pa&0xFFFULL)){ p->why="the TMR is not aligned"; return -1; }
    u32 b[6]={(u32)mc,(u32)(mc>>32),sz,2u,(u32)pa,(u32)(pa>>32)};
    int r=aps_cmd(p,APS_CMD_SETUP_TMR,b,6,us); if(r>=0){ p->tmr=1; p->tmr_mc=mc; p->tmr_sz=sz; } return r; }

int aps_load(aps_t* p,u32 type,const u8* d,u32 n,u32 us){
    if(aps_stage(p,d,n))return -1;
    u64 m=p->mc+APS_FW; u32 b[4]={(u32)m,(u32)(m>>32),n,type};
    return aps_cmd(p,APS_CMD_LOAD_IP_FW,b,4,us); }

int aps_sdma(aps_t* p,const asd_gen_t* g,const asd_fw_t* f,u32 us){
    if(!g||!f||!f->th0||!f->b0||g->v6!=(f->hv==2)){ p->why="the SDMA firmware does not match this SDMA layout"; return -1; }
    if(!g->v6)return aps_load(p,APS_FW_SDMA0,f->th0,f->b0,us);
    if(!f->th1||!f->b1){ p->why="the SDMA firmware has no second thread"; return -1; }
    int r=aps_load(p,APS_FW_SDMA_TH0,f->th0,f->b0,us); if(r<0)return r;
    u32 st=p->last; int r2=aps_load(p,APS_FW_SDMA_TH1,f->th1,f->b1,us); if(r2<0)return r2;
    if(r&&!r2)p->last=st;
    return r|r2; }
