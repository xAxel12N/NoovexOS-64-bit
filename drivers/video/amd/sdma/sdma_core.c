#include "drivers/video/amd/sdma/sdma_core.h"

#define ASV(a,b,c) (((u32)(a)<<16)|((u32)(b)<<8)|(u32)(c))
#define ASD52(v,n,f) {v,n,f,0,0x80,0x81,0x83,0x85,0x87,0xB2,0xB3,0x88,0x89,0x8A,0x92,0xAB,0xB5,0x2A,0x1C,0x3C,0x21,0x25,0x5880,0x5881,0xE00,0x3E}
#define ASD60(v,n,f) {v,n,f,1,0x80,0x81,0x83,0x85,0,0xB2,0xB3,0x88,0x89,0x8A,0x92,0xAB,0xB5,0x589A,0x1C,0x3C,0x22,0x25,0x5886,0x5887,0x600,0x1F}

static const asd_gen_t asd_tab[]={
    ASD52(ASV(5,2,1),"SDMA 5.2.1 (Van Gogh)","amdgpu/vangogh_sdma.bin"),
    ASD52(ASV(5,2,3),"SDMA 5.2.3 (Rembrandt)","amdgpu/yellow_carp_sdma.bin"),
    ASD52(ASV(5,2,6),"SDMA 5.2.6 (Mendocino)","amdgpu/sdma_5_2_6.bin"),
    ASD52(ASV(5,2,7),"SDMA 5.2.7 (Raphael)","amdgpu/sdma_5_2_7.bin"),
    ASD60(ASV(6,0,1),"SDMA 6.0.1 (Phoenix)","amdgpu/sdma_6_0_1.bin"),
};

const asd_gen_t* asd_gen(u32 v){ for(unsigned i=0;i<sizeof asd_tab/sizeof asd_tab[0];i++)if(asd_tab[i].ver==v)return &asd_tab[i]; return 0; }

static u32 asd_le(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }

int asd_fw_parse(asd_fw_t* f,const u8* b,u32 n){
    *f=(asd_fw_t){0};
    if(!b||n<64){ f->why="file too small"; return -1; }
    u32 sz=asd_le(b),hs=asd_le(b+4),hv=b[8]|(b[9]<<8),usz=asd_le(b+20),uoff=asd_le(b+24);
    if(sz!=n||hs<44||hs>n){ f->why="bad common header"; return -1; }
    if(uoff<hs||uoff>n||usz>n-uoff||(usz&3)||!usz){ f->why="ucode outside file"; return -1; }
    f->ver=asd_le(b+16); f->hv=(int)hv;
    if(hv==1){ f->th0=b+uoff; f->n0=usz/4; f->b0=usz; f->why="ok"; return 0; }
    if(hv==2){ if(hs<64){ f->why="short v2 header"; return -1; }
        u32 csz=asd_le(b+36),cjo=asd_le(b+40),cjs=asd_le(b+44),loff=asd_le(b+48),lsz=asd_le(b+52),ljo=asd_le(b+56),ljs=asd_le(b+60);
        u32 n0=cjo+cjs, n1=ljo+ljs;
        if(n0>csz/4||!n0||csz>n-uoff||loff<hs||loff>n||lsz>n-loff||n1>lsz/4||!n1){ f->why="v2 thread sizes outside file"; return -1; }
        f->th0=b+uoff; f->n0=n0; f->th1=b+loff; f->n1=n1; f->b0=csz; f->b1=lsz; f->why="ok"; return 0; }
    f->why="unknown SDMA header version"; return -1; }

u32 asd_reg(const asd_t* e,u32 off){ return (off>=0x5880u&&off<=0x589Au?e->seg1:e->seg0)+off; }
static u32 asd_r(const asd_t* e,u32 off){ return e->io->rd(e->io->c,asd_reg(e,off)); }
static void asd_w(const asd_t* e,u32 off,u32 v){ e->io->wr(e->io->c,asd_reg(e,off),v); }

void asd_stop(asd_t* e){
    const asd_gen_t* g=e->g; if(!g)return;
    asd_w(e,g->rb_cntl,asd_r(e,g->rb_cntl)&~1u); asd_w(e,g->ib_cntl,asd_r(e,g->ib_cntl)&~1u);
    asd_w(e,g->f32,asd_r(e,g->f32)|1u); e->up=0; }

static void asd_load(asd_t* e,const u8* d,u32 n,u32 at){
    asd_w(e,e->g->uaddr,at); for(u32 i=0;i<n;i++)asd_w(e,e->g->udata,asd_le(d+i*4)); }

int asd_start(asd_t* e,const asd_fw_t* f){
    const asd_gen_t* g=e->g; if(!g||!e->mem||!e->mc||(e->mc&0xFF))return -1;
    if(asd_r(e,g->f32)==0xFFFFFFFFu)return -2;
    if(g->v6!=(f->hv==2))return -1;
    asd_stop(e);
    if(!e->psp){ if(g->v6){ asd_load(e,f->th0,f->n0,0); asd_load(e,f->th1,f->n1,0x8000u); }
        else { asd_load(e,f->th0,f->n0,0); asd_w(e,g->uaddr,f->ver); } }
    for(u32 i=0;i<ASD_RING_DW;i++)e->mem[i]=0;
    e->mem[ASD_FENCE_OFF/4]=0; e->mem[ASD_RPTR_OFF/4]=0; e->mem[ASD_RPTR_OFF/4+1]=0; e->mem[ASD_WPTR_OFF/4]=0; e->mem[ASD_WPTR_OFF/4+1]=0;
    asd_w(e,g->sem,0);
    u32 rb=(asd_r(e,g->rb_cntl)&~(0x1Fu<<1)&~1u)|(ASD_RING_ORDER<<1);
    if(g->v6)rb|=1u<<23;
    asd_w(e,g->rb_cntl,rb);
    asd_w(e,g->rptr,0); asd_w(e,g->rptr+1,0); asd_w(e,g->wptr,0); asd_w(e,g->wptr+1,0);
    u64 wm=e->mc+ASD_WPTR_OFF, rm=e->mc+ASD_RPTR_OFF;
    asd_w(e,g->poll_lo,(u32)wm); asd_w(e,g->poll_hi,(u32)(wm>>32));
    if(g->poll_cntl)asd_w(e,g->poll_cntl,asd_r(e,g->poll_cntl)|(1u<<2));
    asd_w(e,g->rpa_hi,(u32)(rm>>32)); asd_w(e,g->rpa_lo,(u32)rm&~3u);
    rb|=1u<<12; if(g->v6){ rb&=~(1u<<8); rb|=1u<<11; }
    asd_w(e,g->rb_base,(u32)(e->mc>>8)); asd_w(e,g->rb_base+1,(u32)(e->mc>>40));
    asd_w(e,g->minor,1); asd_w(e,g->wptr,0); asd_w(e,g->wptr+1,0);
    asd_w(e,g->db,asd_r(e,g->db)&~(1u<<28)); asd_w(e,g->minor,0);
    if(!g->v6)asd_w(e,g->cntl,asd_r(e,g->cntl)|(1u<<1));
    u32 u=asd_r(e,g->utcl1); u=(u&~g->resp_m&~g->redo_m)|(g->resp_m&(3u<<9))|(g->redo_m&(g->redo_m==0x3Eu?(9u<<1):9u)); asd_w(e,g->utcl1,u);
    asd_w(e,g->f32,asd_r(e,g->f32)&~1u&~(g->v6?(1u<<13):0u));
    asd_w(e,g->rb_cntl,rb|1u); asd_w(e,g->ib_cntl,asd_r(e,g->ib_cntl)|1u);
    e->wp=0; e->seq=0x5D000000u; e->up=1;
    u32 pk[8]; int k=asd_pk_fence(pk,e->mc+ASD_FENCE_OFF,0);
    if(asd_emit(e,pk,k)<0||asd_kick(e,100000)<0){ asd_stop(e); return -3; }
    return 0; }

int asd_pk_nop(u32* p,u32 n){ for(u32 i=0;i<n;i++)p[i]=0; return (int)n; }
int asd_pk_fence(u32* p,u64 mc,u32 v){ p[0]=5u|(3u<<16); p[1]=(u32)mc&~3u; p[2]=(u32)(mc>>32); p[3]=v; return 4; }
int asd_pk_copy(u32* p,u64 s,u64 d,u32 n){ p[0]=1u; p[1]=n-1; p[2]=0; p[3]=(u32)s; p[4]=(u32)(s>>32); p[5]=(u32)d; p[6]=(u32)(d>>32); return 7; }
int asd_pk_fill(u32* p,u64 d,u32 v,u32 n){ p[0]=11u|(2u<<30); p[1]=(u32)d; p[2]=(u32)(d>>32); p[3]=v; p[4]=n-1; return 5; }
int asd_pk_subwin(u32* p,u64 s,u32 sp,u64 d,u32 dp,u32 w,u32 h){
    p[0]=1u|(4u<<8)|(2u<<29); p[1]=(u32)s; p[2]=(u32)(s>>32); p[3]=0; p[4]=(sp-1)<<13; p[5]=sp*h-1;
    p[6]=(u32)d; p[7]=(u32)(d>>32); p[8]=0; p[9]=(dp-1)<<13; p[10]=dp*h-1; p[11]=(w-1)|((h-1)<<16); p[12]=0; return 13; }

int asd_emit(asd_t* e,const u32* p,int n){
    if(!e->up||n<=0||(u32)n>ASD_RING_DW/2)return -1;
    if(e->wp+(u32)n>ASD_RING_DW){ while(e->wp<ASD_RING_DW)e->mem[e->wp++]=0; e->wp=0; }
    for(int i=0;i<n;i++)e->mem[e->wp++]=p[i];
    if(e->wp>=ASD_RING_DW)e->wp=0;
    return 0; }

int asd_kick(asd_t* e,u32 us){
    if(!e->up)return -1;
    u32 v=++e->seq; u32 pk[4]; asd_pk_fence(pk,e->mc+ASD_FENCE_OFF,v);
    if(e->wp+4>ASD_RING_DW){ while(e->wp<ASD_RING_DW)e->mem[e->wp++]=0; e->wp=0; }
    for(int i=0;i<4;i++)e->mem[e->wp++]=pk[i];
    if(e->wp>=ASD_RING_DW)e->wp=0;
    (void)e->mem[e->wp?e->wp-1:ASD_RING_DW-1]; __atomic_thread_fence(__ATOMIC_SEQ_CST);
    e->mem[ASD_WPTR_OFF/4]=e->wp<<2; e->mem[ASD_WPTR_OFF/4+1]=0;
    asd_w(e,e->g->wptr,e->wp<<2); asd_w(e,e->g->wptr+1,0);
    u64 t0=e->io->ns(e->io->c);
    for(;;){ if(e->mem[ASD_FENCE_OFF/4]==v){ e->ops++; e->last_rptr=asd_r(e,e->g->rptr); return 0; }
        if(e->io->ns(e->io->c)-t0>(u64)us*1000ULL)break; }
    e->timeouts++; e->last_rptr=asd_r(e,e->g->rptr); return asd_r(e,e->g->f32)==0xFFFFFFFFu?-2:-1; }

int asd_copy_rect(asd_t* e,u64 s,u32 spb,u64 d,u32 dpb,u32 w,u32 h,u32 us){
    if(!e->up||!w||!h||((s|d|spb|dpb)&3)||w>16384||spb/4<w||dpb/4<w||spb/4>0x80000u||dpb/4>0x80000u)return -1;
    u32 pk[16]; for(u32 y=0;y<h;){ u32 n=h-y; if(n>16384)n=16384; if((u64)(spb/4)*n>0x10000000ULL||(u64)(dpb/4)*n>0x10000000ULL)n=0x10000000u/(spb>dpb?spb/4:dpb/4);
        int k=asd_pk_subwin(pk,s+(u64)y*spb,spb/4,d+(u64)y*dpb,dpb/4,w,n); if(asd_emit(e,pk,k)<0)return -1; y+=n; }
    return asd_kick(e,us); }

int asd_fill_rect(asd_t* e,u64 d,u32 dpb,u32 w,u32 h,u32 v,u32 us){
    if(!e->up||!w||!h||((d|dpb)&3)||dpb/4<w)return -1;
    u32 pk[8];
    if(dpb==w*4){ u64 n=(u64)dpb*h, o=0; u32 q=0; while(o<n){ u32 c=n-o>ASD_MAXLIN?ASD_MAXLIN:(u32)(n-o); int k=asd_pk_fill(pk,d+o,v,c); if(asd_emit(e,pk,k)<0)return -1; o+=c;
            if(++q==256&&o<n){ q=0; int r=asd_kick(e,us); if(r<0)return r; } } return asd_kick(e,us); }
    if((u64)w*4>ASD_MAXLIN)return -1;
    for(u32 y=0;y<h;y++){ int k=asd_pk_fill(pk,d+(u64)y*dpb,v,w*4); if(asd_emit(e,pk,k)<0)return -1;
        if((y&255)==255&&y+1<h){ int r=asd_kick(e,us); if(r<0)return r; } }
    return asd_kick(e,us); }
