#include "drivers/video/amd/sdma/sdma_gart.h"

static const agt_gen_t agt_tab[]={
    {0x0A03,0x16FC,0x16FD,0x16E7,0x16FE,0x16FF,0x1701,0x1702,0x1703,0x15BC,0x15BD,0x15FC,0x1667,0x1687,0x16A7,0x1630,0x1642},
    {0x0B00,0x1678,0x1679,0x15A7,0x167A,0x167B,0x167D,0x167E,0x167F,0x15BC,0x15BD,0x1688,0x16F3,0x1713,0x1733,0x16BC,0x16CE},
};

const agt_gen_t* agt_gen(u32 v){ u32 k=(v>>8)&0xFFFFu; if(k==0x0A03)return &agt_tab[0]; if(k==0x0B00)return &agt_tab[1]; return 0; }
u64 agt_pte(u64 pa){ return (pa&0x0000FFFFFFFFF000ULL)|AGT_PTE_FLAGS; }
static u32 agt_r(const agt_t* a,u32 o){ return a->io->rd(a->io->c,a->seg0+o); }
static void agt_w(const agt_t* a,u32 o,u32 v){ a->io->wr(a->io->c,a->seg0+o,v); }
static int agt_hit(u64 s,u64 e,u64 lo,u64 hi){ return lo<=hi&&s<=hi&&e>=lo; }

int agt_place(agt_t* a){
    const agt_gen_t* g=a->g; a->why=0; if(!g||!a->npte||a->npte>0x100000u){ a->why="no GART layout for this graphics version"; return -1; }
    u32 b=agt_r(a,g->fbb), t=agt_r(a,g->fbt);
    if(b==0xFFFFFFFFu){ a->why="memory controller registers read all ones"; return -2; }
    if(!b&&!t){ a->why="the graphics memory window is not programmed"; return -1; }
    a->fb_base=(u64)(b&0xFFFFFFu)<<24; a->fb_top=((u64)(t&0xFFFFFFu)<<24)|0xFFFFFFULL; a->fb_off=(u64)(agt_r(a,g->fbo)&0xFFFFFFu)<<24;
    if(a->fb_top<a->fb_base){ a->why="the graphics memory window is inverted"; return -1; }
    a->agp_bot=(u64)(agt_r(a,g->agpb)&0xFFFFFFu)<<24; a->agp_top=((u64)(agt_r(a,g->agpt)&0xFFFFFFu)<<24)|0xFFFFFFULL;
    a->sys_lo=(u64)(agt_r(a,g->sal)&0x3FFFFFFFu)<<18; a->sys_hi=((u64)(agt_r(a,g->sah)&0x3FFFFFFFu)<<18)|0x3FFFFULL;
    u64 lo=a->fb_base, hi=a->fb_top; if(a->agp_bot<=a->agp_top){ if(a->agp_bot<lo)lo=a->agp_bot; if(a->agp_top>hi)hi=a->agp_top; }
    if(a->sys_lo<=a->sys_hi){ if(a->sys_lo<lo)lo=a->sys_lo; if(a->sys_hi>hi)hi=a->sys_hi; }
    u64 sz=(u64)a->npte<<12, g4=1ULL<<32, bf=lo, af=hi+1>AGT_MCMAX?0:AGT_MCMAX+1-((hi+g4)&~(g4-1));
    if(bf>=sz&&bf<af)a->start=0; else if(af>=sz)a->start=(AGT_MCMAX+1-sz)&~(g4-1); else { a->why="no room for the GART in the GPU address space"; return -1; }
    a->end=a->start+sz-1;
    if(agt_hit(a->start,a->end,a->fb_base,a->fb_top)||(a->agp_bot<=a->agp_top&&agt_hit(a->start,a->end,a->agp_bot,a->agp_top))||(a->sys_lo<=a->sys_hi&&agt_hit(a->start,a->end,a->sys_lo,a->sys_hi))){ a->why="the GART range overlaps a memory aperture"; return -1; }
    if(a->pt_mc<a->fb_base||a->pt_mc+(u64)a->npte*8>a->fb_top+1||(a->pt_mc&0xFFF)){ a->why="the page table is not inside video memory"; return -1; }
    return 0; }

int agt_flush(agt_t* a,u32 us){
    const agt_gen_t* g=a->g; u32 req=1u|(1u<<19)|(1u<<20)|(1u<<21)|(1u<<22)|(1u<<23);
    agt_w(a,g->ireq,req); u64 t0=a->io->ns(a->io->c);
    for(;;){ u32 k=agt_r(a,g->iack); if(k==0xFFFFFFFFu)return -2; if(k&1u){ a->flushes++; return 0; }
        if(a->io->ns(a->io->c)-t0>(u64)us*1000ULL)return -1; } }

int agt_enable(agt_t* a,u32 us){
    const agt_gen_t* g=a->g; if(!a->pt||!a->end){ a->why="GART not placed"; return -1; }
    for(u32 i=0;i<a->npte;i++)a->pt[i]=0;
    if(a->pt[a->npte-1]!=0){ a->why="the page table memory did not keep data"; return -1; }
    u64 pd=(a->pt_mc-a->fb_base+a->fb_off)|1ULL;
    a->ctx_old=agt_r(a,g->ctx); a->l1_old=agt_r(a,g->l1);
    agt_w(a,g->ctx,a->ctx_old&~1u);
    agt_w(a,g->ptlo,(u32)pd); agt_w(a,g->ptlo+1,(u32)(pd>>32));
    agt_w(a,g->stlo,(u32)(a->start>>12)); agt_w(a,g->stlo+1,(u32)(a->start>>44)&0xFu);
    agt_w(a,g->enlo,(u32)(a->end>>12)); agt_w(a,g->enlo+1,(u32)(a->end>>44)&0xFu);
    agt_w(a,g->l1,(a->l1_old&~0x387Fu)|1u|(3u<<3)|(1u<<6)|(3u<<11));
    agt_w(a,g->l2,agt_r(a,g->l2)|1u);
    agt_w(a,g->ctx,(a->ctx_old&~0x87u)|1u);
    int r=agt_flush(a,us); if(r){ agt_w(a,g->ctx,a->ctx_old&~1u); a->why=r==-2?"memory controller registers read all ones":"the GPU TLB flush did not finish"; return r; }
    a->up=1; return 0; }

void agt_disable(agt_t* a,u32 us){
    if(!a->g||!a->up)return;
    agt_w(a,a->g->ctx,a->ctx_old&~1u); agt_w(a,a->g->l1,a->l1_old); agt_flush(a,us); a->up=0; }

int agt_map(agt_t* a,u32 first,const u64* pa,u32 n,u32 us){
    if(!a->up||first>a->npte||n>a->npte-first)return -1;
    for(u32 i=0;i<n;i++){ if(pa[i]&0xFFFULL)return -1; a->pt[first+i]=agt_pte(pa[i]); }
    if(n&&a->pt[first+n-1]!=agt_pte(pa[n-1]))return -1;
    return agt_flush(a,us); }

u64 agt_mc(const agt_t* a,u32 page){ return a->start+((u64)page<<12); }
