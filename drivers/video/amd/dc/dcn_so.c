#include "drivers/video/amd/dc/dcn_so.h"

#define DSO_V(a,b,c) (((u32)(a)<<16)|((u32)(b)<<8)|(u32)(c))
enum { DSR_CFG=0x05E5, DSR_TILE=0x05E7, DSR_VPS=0x05E9, DSR_VPD=0x05EA, DSR_CNTL=0x05F3, DSR_PITCH=0x0607, DSR_ADDR=0x060A, DSR_ADDRH=0x060B, DSR_FLIP=0x061B,
       DSR_HSTR=0xDC, DSR_OSTR=0x80, DSF_LOCK=1, DSF_IMM=2, DSF_PEND=0x100 };

static const dso_gen_t dso_gens[]={
    {DSO_V(2,1,0),"DCN 2.1 (Renoir/Lucienne/Cezanne)",0x1B41,0x1B4C,0x0621,0x3240,0x0493,0x1B68,2,6},
    {DSO_V(3,0,1),"DCN 3.0.1 (Van Gogh)",0x1B41,0x1B4C,0x0621,0x0196,0x0493,0x1B68,2,6},
    {DSO_V(3,1,2),"DCN 3.1.2 (Yellow Carp/Rembrandt)",0x1B41,0x1B4C,0x0621,0x0196,0x0475,0x1B68,2,6},
    {DSO_V(3,1,3),"DCN 3.1.3 (Rembrandt)",0x1B41,0x1B4C,0x0621,0x0196,0x0475,0x1B68,2,6},
    {DSO_V(3,1,4),"DCN 3.1.4 (Phoenix)",0x1B41,0x1B4C,0x0621,0x0196,0x0475,0x1B68,1,5},
    {DSO_V(3,1,5),"DCN 3.1.5 (Raphael)",0x1B41,0x1B4C,0x0621,0x0196,0x0475,0x1B68,2,6},
    {DSO_V(3,1,6),"DCN 3.1.6 (Mendocino)",0x1B41,0x1B4C,0x0621,0x0196,0x0475,0x1B68,2,6},
    {DSO_V(3,5,0),"DCN 3.5 (Strix Point)",0x1B43,0x1B4D,0x0620,0x0196,0x0475,0x1B65,1,5},
    {DSO_V(3,5,1),"DCN 3.5.1",0x1B43,0x1B4D,0x0620,0x0196,0x0475,0x1B65,1,5},
};

u32 dso_ver_for_did(u16 d){
    switch(d){
    case 0x1636: case 0x164C: case 0x1638: case 0x15E7: return DSO_V(2,1,0);
    case 0x163F: return DSO_V(3,0,1);
    case 0x1681: case 0x164D: return DSO_V(3,1,2);
    case 0x15BF: case 0x15C8: return DSO_V(3,1,4);
    case 0x1506: return DSO_V(3,1,6);
    case 0x164E: return DSO_V(3,1,5);
    case 0x150E: return DSO_V(3,5,0); }
    return 0; }

const dso_gen_t* dso_gen(u32 v,u16 did){
    if(!v)v=dso_ver_for_did(did);
    for(u32 i=0;i<sizeof dso_gens/sizeof dso_gens[0];i++)if(dso_gens[i].ver==v)return &dso_gens[i];
    return 0; }

static u32 dso_rd(const dso_io_t* io,u32 dw){ return io->rd(io->c,dw); }
static void dso_wr(const dso_io_t* io,u32 dw,u32 v){ io->wr(io->c,dw,v); }
static u32 dso_h(u32 s,int p,u32 r){ return s+r+(u32)p*DSR_HSTR; }
static u32 dso_o(u32 s,int p,u32 r){ return s+r+(u32)p*DSR_OSTR; }
static u32 dso_bpp(u32 f){ if(f>=8&&f<=13)return 4; if(f>=1&&f<=4)return 2; if(f>=24&&f<=27)return 8; return 0; }

int dso_read(const dso_io_t* io,const dso_gen_t* g,u32 s,int p,dso_pipe_t* o){
    if(!io||!g||!s||!o||p<0||p>=DSO_PIPES)return -1;
    u32 cfg=dso_rd(io,dso_h(s,p,DSR_CFG)); if(cfg==0xFFFFFFFFu)return -2;
    o->pipe=p; o->fmt=cfg&0x7F; o->bpp=dso_bpp(o->fmt); o->sw=dso_rd(io,dso_h(s,p,DSR_TILE))&0x1F;
    o->blank=dso_rd(io,dso_h(s,p,DSR_CNTL))&1; o->pitch=(dso_rd(io,dso_h(s,p,DSR_PITCH))&0x3FFF)+1;
    u32 vd=dso_rd(io,dso_h(s,p,DSR_VPD)); o->vpw=vd&0x3FFF; o->vph=(vd>>16)&0x3FFF;
    o->flipctl=dso_rd(io,dso_h(s,p,DSR_FLIP));
    o->addr=((u64)(dso_rd(io,dso_h(s,p,DSR_ADDRH))&0xFFFF)<<32)|dso_rd(io,dso_h(s,p,DSR_ADDR));
    u32 oc=dso_rd(io,dso_o(s,p,g->otg_ctl)); o->on=oc!=0xFFFFFFFFu&&(oc&1);
    return 0; }

int dso_active(const dso_io_t* io,const dso_gen_t* g,u32 s,dso_pipe_t* o){
    for(int p=0;p<DSO_PIPES;p++){ if(dso_read(io,g,s,p,o))continue; if(!o->blank&&o->on&&o->addr&&!(o->addr&0xFF)&&o->bpp&&o->vpw&&o->vph)return p; }
    return -1; }

int dso_dmcub_on(const dso_io_t* io,const dso_gen_t* g,u32 s){
    u32 dmc=g->ver==DSO_V(2,1,0)?0x32A0:0x01F6; u32 v=dso_rd(io,s+dmc); return v!=0xFFFFFFFFu&&((v>>16)&1); }

static void dso_add(dso_resv_t* r,u64 lo,u64 n){ if(!n||r->n>=DSO_NREG)return; r->lo[r->n]=lo; r->hi[r->n]=lo+n; r->n++; }

void dso_resv(const dso_io_t* io,const dso_gen_t* g,u32 s,dso_resv_t* r){
    r->n=0; u32 b=s+g->dmr;
    for(u32 k=0;k<8;k++){ u32 t=dso_rd(io,b+0x17+k); if(t==0xFFFFFFFFu||!(t>>31))continue; u32 lo=dso_rd(io,b+0x0F+k)&0x1FFFFFFFu; t&=0x1FFFFFFFu; if(t<lo)continue;
        u64 off=(u64)dso_rd(io,b+0x1F+2*k)|((u64)(dso_rd(io,b+0x20+2*k)&0xFFFF)<<32); dso_add(r,off,(u64)(t-lo)+1); }
    for(u32 k=0;k<2;k++){ u32 t=dso_rd(io,b+0x0B+k); if(t==0xFFFFFFFFu||!(t>>31))continue;
        u64 off=(u64)dso_rd(io,b+2*k)|((u64)(dso_rd(io,b+1+2*k)&0xFFFF)<<32); dso_add(r,off,(u64)(t&0x1FFFFFFFu)+1); } }

u64 dso_place(u64 front,u64 len,u64 lim,const dso_resv_t* r){
    if(!len||front+len<front)return 0;
    u64 c=(front+len+0xFFFFULL)&~0xFFFFULL;
    for(int it=0;it<=DSO_NREG;it++){ int moved=0;
        for(int i=0;r&&i<r->n;i++){ if(c<r->hi[i]&&r->lo[i]<c+len){ c=(r->hi[i]+0xFFFFULL)&~0xFFFFULL; moved=1; } }
        if(!moved)return c+len<=lim?c:0; }
    return 0; }

int dso_flip(const dso_io_t* io,const dso_gen_t* g,u32 s,int p,u64 a){
    if(!io||!g||!s||p<0||p>=DSO_PIPES||!a||(a&0xFF)||(a>>48))return -1;
    u32 f=dso_rd(io,dso_h(s,p,DSR_FLIP)); if(f==0xFFFFFFFFu)return -2;
    if(f&DSF_LOCK)return -3;
    if(f&DSF_IMM)dso_wr(io,dso_h(s,p,DSR_FLIP),f&~(u32)DSF_IMM);
    dso_wr(io,dso_h(s,p,DSR_ADDRH),(u32)(a>>32)&0xFFFF);
    dso_wr(io,dso_h(s,p,DSR_ADDR),(u32)a);
    return 0; }

int dso_pending(const dso_io_t* io,const dso_gen_t* g,u32 s,int p){
    (void)g; u32 f=dso_rd(io,dso_h(s,p,DSR_FLIP)); if(f==0xFFFFFFFFu)return -1; return (f&DSF_PEND)?1:0; }

int dso_wait(const dso_io_t* io,const dso_gen_t* g,u32 s,int p,u32 us){
    u64 t0=io->ns(io->c);
    for(;;){ int r=dso_pending(io,g,s,p); if(r<=0)return r; if(io->ns(io->c)-t0>(u64)us*1000ULL)return -2; } }

u64 dso_inuse(const dso_io_t* io,const dso_gen_t* g,u32 s,int p){
    return ((u64)(dso_rd(io,dso_h(s,p,g->inuse+1))&0xFFFF)<<32)|dso_rd(io,dso_h(s,p,g->inuse)); }

static u64 dso_h24(u32 v){ return (u64)(v&0xFFFFFFu)<<24; }
static int dso_mc_ok(const dso_mc_t* m){ return m->fb_base<m->fb_top; }
int dso_mc_dcn(const dso_io_t* io,const dso_gen_t* g,u32 s,dso_mc_t* m){
    u32 r[6]; for(int i=0;i<6;i++){ r[i]=dso_rd(io,s+g->vmfb+(u32)i); if(r[i]==0xFFFFFFFFu)return -2; }
    m->fb_base=dso_h24(r[0]); m->fb_top=dso_h24(r[1])|0xFFFFFFULL; m->fb_off=dso_h24(r[2]); m->agp_bot=dso_h24(r[3]); m->agp_top=dso_h24(r[4])|0xFFFFFFULL; m->agp_base=dso_h24(r[5]);
    return (!(r[0]|r[1])||!dso_mc_ok(m))?-1:0; }
int dso_mc_hub(const dso_io_t* io,u32 loc,u32 off,u32 agp,dso_mc_t* m){
    u32 b=dso_rd(io,loc), t=dso_rd(io,loc+1), o=dso_rd(io,off), at=dso_rd(io,agp), ab=dso_rd(io,agp+1), aa=dso_rd(io,agp+2);
    if(b==0xFFFFFFFFu||t==0xFFFFFFFFu||o==0xFFFFFFFFu)return -2;
    m->fb_base=dso_h24(b); m->fb_top=dso_h24(t)|0xFFFFFFULL; m->fb_off=dso_h24(o); m->agp_top=dso_h24(at)|0xFFFFFFULL; m->agp_bot=dso_h24(ab); m->agp_base=dso_h24(aa);
    return (!(b|t)||!dso_mc_ok(m))?-1:0; }
int dso_mc_same(const dso_mc_t* a,const dso_mc_t* b){ return a->fb_base==b->fb_base&&a->fb_top==b->fb_top&&a->fb_off==b->fb_off; }

const char* dso_map(const dso_mc_t* m,u64 scan,u64 fbb,u64 bar,u64 barlen,u64 margin,dso_map_t* o){
    if(!dso_mc_ok(m))return "the GPU memory controller window is not set";
    if(scan<m->fb_base||scan>m->fb_top)return "the scanned-out surface is outside video memory";
    u64 x=scan-m->fb_base, size=m->fb_top-m->fb_base+1;
    o->scan_off=x;
    if(size<=margin)return "video memory is too small";
    o->lim=m->fb_base+size-margin;
    if(bar&&barlen&&fbb==bar+x&&x<barlen){ o->via=1; o->cpu0=bar; if(m->fb_base+barlen<o->lim)o->lim=m->fb_base+barlen; return 0; }
    if(m->fb_off&&fbb==m->fb_off+x){ o->via=2; o->cpu0=m->fb_off; return 0; }
    return "the firmware framebuffer is not where the memory controller puts the scanned-out surface"; }

void dso_resv_xl(dso_resv_t* r,const dso_mc_t* m){
    int n=r->n; for(int i=0;i<n;i++){ u64 lo=r->lo[i], hi=r->hi[i]; if(m->fb_off&&lo>=m->fb_off){ u64 l=lo-m->fb_off+m->fb_base; dso_add(r,l,hi-lo); } } }

u32 dso_frame(const dso_io_t* io,const dso_gen_t* g,u32 s,int p){ return dso_rd(io,dso_o(s,p,g->otg_fc)); }
int dso_frames(const dso_io_t* io,const dso_gen_t* g,u32 s,int p,u32 n,u32 us){
    u32 f0=dso_frame(io,g,s,p); if(f0==0xFFFFFFFFu)return -1; u64 t0=io->ns(io->c);
    for(;;){ u32 f=dso_frame(io,g,s,p); if(f==0xFFFFFFFFu)return -1; if(((f-f0)&0xFFFFFFu)>=n)return 0; if(io->ns(io->c)-t0>(u64)us*1000ULL)return -2; } }

int dso_crc_on(const dso_io_t* io,const dso_gen_t* g,u32 s,int p,u32 w,u32 h,u32* saved){
    if(!g->crc||!w||!h)return -1;
    u32 c=dso_o(s,p,g->crc), v=dso_rd(io,c); if(v==0xFFFFFFFFu)return -2;
    *saved=v;
    u32 x=(w&0x7FFF)<<16, y=(h&0x7FFF)<<16;
    dso_wr(io,c,v&~1u); dso_wr(io,c+g->crcw,x); dso_wr(io,c+g->crcw+1,y); dso_wr(io,c+g->crcw+2,x); dso_wr(io,c+g->crcw+3,y);
    dso_wr(io,c,(v&~(0xFu<<20))|0x10u|1u); return 0; }
void dso_crc_restore(const dso_io_t* io,const dso_gen_t* g,u32 s,int p,u32 saved){ if(g->crc)dso_wr(io,dso_o(s,p,g->crc),saved); }
int dso_crc(const dso_io_t* io,const dso_gen_t* g,u32 s,int p,u32 c[3]){
    if(!g->crc)return -1;
    u32 a=dso_o(s,p,g->crc), v=dso_rd(io,a); if(v==0xFFFFFFFFu||!(v&1))return -1;
    u32 rg=dso_rd(io,a+g->crcd), b=dso_rd(io,a+g->crcd+1); if(rg==0xFFFFFFFFu||b==0xFFFFFFFFu)return -1;
    c[0]=rg&0xFFFF; c[1]=rg>>16; c[2]=b&0xFFFF; return 0; }
static int dso_ceq(const u32* a,const u32* b){ return a[0]==b[0]&&a[1]==b[1]&&a[2]==b[2]; }

int dso_verify(const dso_io_t* io,const dso_gen_t* g,u32 s,int p,u64 front,u64 back,volatile u32* px,u32 w,u32 h,dso_vfy_t* v){
    v->crc_live=0; v->crc_same=0; v->inuse_ok=0; v->latched=0; u32 sv=0, t[3];
    int crc=!dso_crc_on(io,g,s,p,w,h,&sv);
    if(crc&&(dso_frames(io,g,s,p,2,100000)||dso_crc(io,g,s,p,v->c0)||dso_frames(io,g,s,p,1,100000)||dso_crc(io,g,s,p,t)||!dso_ceq(t,v->c0)))crc=0;
    if(dso_wait(io,g,s,p,50000)<0||dso_flip(io,g,s,p,back)){ if(crc)dso_crc_restore(io,g,s,p,sv); return -1; }
    v->latched=dso_wait(io,g,s,p,100000)==0;
    v->inuse_ok=v->latched&&dso_inuse(io,g,s,p)==back;
    if(v->inuse_ok&&crc&&!dso_frames(io,g,s,p,2,100000)&&!dso_crc(io,g,s,p,v->c1)){
        v->crc_same=dso_ceq(v->c1,v->c0);
        if(v->crc_same&&px){ u32 o=px[0]; px[0]=o^0x00FFFFFFu; if(!dso_frames(io,g,s,p,2,100000)&&!dso_crc(io,g,s,p,v->c2))v->crc_live=!dso_ceq(v->c2,v->c1); px[0]=o; dso_frames(io,g,s,p,2,100000); }
        else if(!v->crc_same)v->crc_live=1; }
    if(crc)dso_crc_restore(io,g,s,p,sv);
    int ok=v->inuse_ok&&(!v->crc_live||v->crc_same);
    if(!ok){ dso_wait(io,g,s,p,50000); dso_flip(io,g,s,p,front); dso_wait(io,g,s,p,100000); return -2; }
    return 0; }

enum { DSD_SCR7=0x54, DSD_CNTL=0x60, DSD_GPINT=0x62, DSD_IBB=0x3E, DSD_IBS=0x3F, DSD_IBW=0x40, DSD_IBR=0x41 };
int dso_gpint(const dso_io_t* io,const dso_gen_t* g,u32 s,u32 cmd,u32 prm,u32 us,u32* resp){
    u32 r=s+g->dmr+DSD_GPINT, v=((cmd&0xFFF)<<16)|(prm&0xFFFF);
    if(!dso_dmcub_on(io,g,s))return -1;
    dso_wr(io,r,v|(1u<<28)); u64 t0=io->ns(io->c);
    for(;;){ u32 x=dso_rd(io,r); if(x==0xFFFFFFFFu)return -1; if(x==v)break; if(io->ns(io->c)-t0>(u64)us*1000ULL)return -2; }
    u32 x=dso_rd(io,s+g->dmr+DSD_SCR7); if(resp)*resp=x;
    return 0; }
int dso_psr_state(const dso_io_t* io,const dso_gen_t* g,u32 s,u32* raw){
    u32 x=0; int r=dso_gpint(io,g,s,7,0,2000,&x); if(r)return r; if(raw)*raw=x; return x==0?0:1; }
int dso_inbox(const dso_io_t* io,const dso_gen_t* g,u32 s,u64* mc,u32* size){
    u32 b=s+g->dmr, ib=dso_rd(io,b+DSD_IBB), sz=dso_rd(io,b+DSD_IBS); if(ib==0xFFFFFFFFu||sz==0xFFFFFFFFu||!sz||sz>0x100000u||(sz&63))return -1;
    for(u32 k=0;k<8;k++){ u32 t=dso_rd(io,b+0x17+k); if(t==0xFFFFFFFFu||!(t>>31))continue; u32 lo=dso_rd(io,b+0x0F+k)&0x1FFFFFFFu; t&=0x1FFFFFFFu;
        u32 i=ib&0x1FFFFFFFu; if(i<lo||i+sz>t+1u)continue;
        *mc=((u64)dso_rd(io,b+0x1F+2*k)|((u64)(dso_rd(io,b+0x20+2*k)&0xFFFF)<<32))+(i-lo); *size=sz; return (int)k; }
    return -1; }
int dso_rb_send(const dso_io_t* io,const dso_gen_t* g,u32 s,volatile u8* ring,u32 size,const u32* cmd,u32 us){
    u32 b=s+g->dmr, w=dso_rd(io,b+DSD_IBW), r=dso_rd(io,b+DSD_IBR);
    if(!ring||w==0xFFFFFFFFu||r==0xFFFFFFFFu||w>=size||r>=size||(w&63))return -1;
    u32 nw=(w+64)%size; if(nw==r)return -3;
    volatile u32* d=(volatile u32*)(ring+w); for(int i=0;i<16;i++)d[i]=cmd[i];
    __sync_synchronize(); dso_wr(io,b+DSD_IBW,nw); u64 t0=io->ns(io->c);
    for(;;){ u32 x=dso_rd(io,b+DSD_IBR); if(x==0xFFFFFFFFu)return -1; if(x==nw)return 0; if(io->ns(io->c)-t0>(u64)us*1000ULL)return -2; } }
void dso_cmd_psr_off(u32 c[16]){ for(int i=0;i<16;i++)c[i]=0; c[0]=64u|(3u<<8); c[1]=1u; }
void dso_cmd_abm_bl(u32 c[16],u32 lvl){ for(int i=0;i<16;i++)c[i]=0; c[0]=66u|(2u<<8)|(32u<<24); c[1]=0; c[2]=lvl; c[3]=1u|(1u<<8); }
