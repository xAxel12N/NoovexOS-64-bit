#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "drivers/video/amd/sdma/sdma_core.c"
#include "drivers/video/amd/sdma/sdma_gart.c"

#define SEG0 0x1260u
#define SEG1 0xA000u
#define MCB 0xF400000000ULL
#define FBOFF 0x0000000080000000ULL
#define MEMSZ (16u<<20)
#define PT_OFF 0x200000u
#define PB 0x0000000123400000ULL
#define NSP 64u
static u32 RG[0x20000]; static u8* M; static u8* S; static u64 T; static int noack, dead, faults, uw0; static u32 rp;
static const asd_gen_t* G; static const agt_gen_t* GG; static int bad;
static void ck(const char* g,const char* n,int c){ printf("  %s %s %s\n",g,n,c?"ok":"FAIL"); if(!c)bad++; }
static u32 rr(u32 off){ return RG[asd_reg(&(asd_t){.seg0=SEG0,.seg1=SEG1},off)]; }
static u32 gr(u32 off){ return RG[SEG0+off]; }
static u32* tr(u64 mc){
    u64 fb=(u64)(gr(GG->fbb)&0xFFFFFF)<<24, ft=((u64)(gr(GG->fbt)&0xFFFFFF)<<24)|0xFFFFFF, fo=(u64)(gr(GG->fbo)&0xFFFFFF)<<24;
    if(mc>=fb&&mc<=ft){ if(mc-fb+4>MEMSZ)return 0; return (u32*)(M+(mc-fb)); }
    if(!(gr(GG->ctx)&1)){ faults++; return 0; }
    u64 st=((u64)(gr(GG->stlo+1)&0xF)<<44)|((u64)gr(GG->stlo)<<12), en=((u64)(gr(GG->enlo+1)&0xF)<<44)|((u64)gr(GG->enlo)<<12)|0xFFF;
    if(mc<st||mc>en){ faults++; return 0; }
    u64 pd=((u64)gr(GG->ptlo+1)<<32)|gr(GG->ptlo); if(!(pd&1)){ faults++; return 0; }
    u64 po=(pd&~0xFFFULL)-fo+((mc-st)>>12)*8; if(po+8>MEMSZ){ faults++; return 0; }
    u64 pte; memcpy(&pte,M+po,8); if((pte&0x67)!=0x67||((pte>>48)&7)!=3){ faults++; return 0; }
    u64 pa=(pte&0x0000FFFFFFFFF000ULL)+(mc&0xFFF); if(pa<PB||pa+4>PB+NSP*4096ULL){ faults++; return 0; }
    return (u32*)(S+(pa-PB)); }
static void run(u32 wp){
    u32* ring=tr(((u64)rr(G->rb_base+1)<<40)|((u64)rr(G->rb_base)<<8)); u32 n=1u<<((rr(G->rb_cntl)>>1)&0x1F);
    while(rp!=wp){ u32 i=rp/4; u32 h=ring[i]; u32 op=h&0xFF, sub=(h>>8)&0xFF; u32* q=ring+i; u32 len=1;
        if(op==5){ u32* d=tr(((u64)q[2]<<32)|q[1]); if(d)*d=q[3]; len=4; }
        else if(op==11){ u64 d=((u64)q[2]<<32)|q[1]; u32 c=(q[4]+1)/4; for(u32 k=0;k<c;k++){ u32* p=tr(d+k*4ULL); if(p)*p=q[3]; } len=5; }
        else if(op==1&&sub==4){ u64 s=((u64)q[2]<<32)|q[1], d=((u64)q[7]<<32)|q[6]; u32 sp=(q[4]>>13)+1, dp=(q[9]>>13)+1, w=(q[11]&0x3FFF)+1, hh=(q[11]>>16)+1;
            len=13; for(u32 y=0;y<hh;y++)for(u32 x=0;x<w;x++){ u32* a=tr(s+((u64)y*sp+x)*4); u32* b=tr(d+((u64)y*dp+x)*4); if(a&&b)*b=*a; } }
        rp=((i+len)%n)*4; }
    RG[asd_reg(&(asd_t){.seg0=SEG0,.seg1=SEG1},G->rptr)]=rp; }
static u32 rd(void* c,u32 dw){ (void)c; if(dead)return 0xFFFFFFFFu; if(GG&&dw==SEG0+GG->iack)return noack?0:(RG[SEG0+GG->ireq]&1); return RG[dw]; }
static void wr(void* c,u32 dw,u32 v){ (void)c; asd_t z={.seg0=SEG0,.seg1=SEG1};
    if(dw==asd_reg(&z,G->udata)){ uw0++; return; }
    RG[dw]=v; if(dw==asd_reg(&z,G->wptr)&&(rr(G->rb_cntl)&1)&&!(rr(G->f32)&1)&&uw0>0)run(v); }
static u64 ns(void* c){ (void)c; T+=1000; return T; }
static const asd_io_t IO={rd,wr,ns,0};
static u8 FW1[64+4096], FW2[64+8192];
static void mkfw(void){
    memset(FW1,0,sizeof FW1); u32* h=(u32*)FW1; h[0]=sizeof FW1; h[1]=48; FW1[8]=1; h[4]=52; h[5]=4096; h[6]=64;
    memset(FW2,0,sizeof FW2); h=(u32*)FW2; h[0]=sizeof FW2; h[1]=64; FW2[8]=2; h[4]=24; h[5]=4096; h[6]=64; h[9]=4096; h[10]=960; h[11]=32; h[12]=64+4096; h[13]=4096; h[14]=1000; }
static void regs(u64 fb,u32 fbmb){ memset(RG,0,sizeof RG); RG[SEG0+GG->fbb]=(u32)(fb>>24); RG[SEG0+GG->fbt]=(u32)((fb+((u64)fbmb<<20)-1)>>24); RG[SEG0+GG->fbo]=(u32)(FBOFF>>24);
    RG[SEG0+GG->agpb]=0xFFFFFF; RG[SEG0+GG->agpt]=0; RG[SEG0+GG->sal]=(u32)(fb>>18); RG[SEG0+GG->sah]=(u32)((fb+((u64)fbmb<<20)-1)>>18); RG[SEG0+GG->l1]=0x8000u; RG[SEG0+GG->ctx]=0x1F0u; }
static void agt_init(agt_t* a){ *a=(agt_t){0}; a->io=&IO; a->g=GG; a->seg0=SEG0; a->pt=(volatile u64*)(M+PT_OFF); a->pt_mc=MCB+PT_OFF; a->npte=NSP; }

static void gen(const char* nm,u32 gcv,u32 sdv,const u8* fw,u32 fwn){
    GG=agt_gen(gcv); G=asd_gen(sdv); ck(nm,"GART layout known",GG!=0&&G!=0); if(!GG||!G)return;
    int r; agt_t a; memset(M,0,MEMSZ); memset(S,0,NSP*4096);
    regs(MCB,512); agt_init(&a); ck(nm,"placement below the frame buffer window",agt_place(&a)==0&&a.start==0&&a.end==NSP*4096ULL-1&&a.fb_base==MCB&&a.fb_off==FBOFF);
    regs(0,512); agt_init(&a); a.pt_mc=PT_OFF; ck(nm,"placement at the top when the window starts at 0",agt_place(&a)==0&&a.start==(AGT_MCMAX+1-(1ULL<<32))&&!(a.start&0xFFFFFFFFULL));
    regs(MCB,512); RG[SEG0+GG->agpb]=0; RG[SEG0+GG->agpt]=0xFFFFFF; agt_init(&a); ck(nm,"no room when apertures fill the address space",agt_place(&a)==-1&&a.why);
    regs(MCB,512); RG[SEG0+GG->fbb]=0; RG[SEG0+GG->fbt]=0; agt_init(&a); ck(nm,"unprogrammed window refused",agt_place(&a)==-1);
    regs(MCB,512); dead=1; agt_init(&a); ck(nm,"all-ones registers reported",agt_place(&a)==-2); dead=0;
    regs(MCB,512); agt_init(&a); a.pt_mc=MCB+512ULL*1048576; ck(nm,"page table outside video memory refused",agt_place(&a)==-1);
    regs(MCB,512); agt_init(&a); noack=1; u64 t0=T; ck(nm,"enable fails bounded when the TLB flush never acks",agt_place(&a)==0&&agt_enable(&a,5000)==-1&&!a.up&&!(gr(GG->ctx)&1)&&T-t0<20000000ULL); noack=0;
    regs(MCB,512); agt_init(&a); r=agt_place(&a)|agt_enable(&a,5000);
    u64 pd=((u64)gr(GG->ptlo+1)<<32)|gr(GG->ptlo);
    ck(nm,"enable programs context 0",r==0&&a.up&&pd==(FBOFF+PT_OFF+0)+1&&gr(GG->stlo)==0&&gr(GG->enlo)==NSP-1&&(gr(GG->ctx)&0x87)==1&&(gr(GG->ctx)&0x170)==0x170);
    ck(nm,"L1 TLB advanced driver mode, UC, L2 on",(gr(GG->l1)&0x387F)==(1u|(3u<<3)|(1u<<6)|(3u<<11))&&(gr(GG->l1)&0x8000)&&(gr(GG->l2)&1));
    ck(nm,"invalidate request covers vmid 0 L1/L2",gr(GG->ireq)==0xF80001u&&a.flushes==1);
    u64 pa[NSP]; for(u32 i=0;i<NSP;i++)pa[i]=PB+(u64)((i*37u)%NSP)*4096;
    ck(nm,"map writes snooped system PTEs",agt_map(&a,0,pa,NSP,5000)==0&&a.pt[5]==((PB+(5*37%NSP)*4096ULL)|AGT_PTE_FLAGS)&&a.flushes==2);
    ck(nm,"unaligned page and overflow refused",agt_map(&a,0,(u64[]){PB+8},1,5000)==-1&&agt_map(&a,NSP-1,pa,2,5000)==-1);
    asd_t e={0}; asd_fw_t f; asd_fw_parse(&f,fw,fwn); e.io=&IO; e.g=G; e.seg0=SEG0; e.seg1=SEG1; e.mem=(volatile u32*)M; e.mc=MCB; rp=0; uw0=0;
    RG[asd_reg(&e,G->f32)]=1; ck(nm,"SDMA ring starts with GART on",asd_start(&e,&f)==0);
    faults=0; ck(nm,"fill through GART across scattered pages",asd_fill_rect(&e,agt_mc(&a,0)+64,1024*4,1000,20,0xFEEDu,10000)==0&&faults==0);
    int ok=1; for(u32 y=0;y<20;y++)for(u32 x=0;x<1001;x++){ u64 m=64+((u64)y*1024+x)*4; u32 v=*(u32*)(S+(pa[m>>12]-PB)+(m&0xFFF)); if(v!=(x<1000?0xFEEDu:0))ok=0; } ck(nm,"fill landed at the right physical pages only",ok);
    for(u32 i=0;i<NSP*1024;i++){ u64 m=(u64)i*4; *(u32*)(S+(pa[m>>12]-PB)+(m&0xFFF))=i*2654435761u; }
    u64 dst=MCB+0x400000; ck(nm,"damage rectangle copied from RAM to VRAM",asd_copy_rect(&e,agt_mc(&a,0)+(5*1024+7)*4,1024*4,dst,2048*4,300,40,10000)==0&&faults==0);
    ok=1; for(u32 y=0;y<40;y++)for(u32 x=0;x<301;x++){ u32 v=*(u32*)(M+0x400000+((u64)y*2048+x)*4); u32 ex=x<300?((5+y)*1024+7+x)*2654435761u:0; if(v!=ex)ok=0; } ck(nm,"copied pixels match the scattered source",ok);
    agt_map(&a,3,(u64[]){0},1,5000); a.pt[3]=0; agt_flush(&a,5000); faults=0; asd_fill_rect(&e,agt_mc(&a,3),4096,1024,1,1,10000); ck(nm,"invalid PTE faults and writes nothing",faults==1024);
    agt_disable(&a,5000); ck(nm,"disable restores context and L1",!(gr(GG->ctx)&1)&&gr(GG->l1)==0x8000u&&!a.up);
    faults=0; asd_fill_rect(&e,agt_mc(&a,0),4096,16,1,1,10000); ck(nm,"GART addresses fault after disable",faults==16); }

int main(void){
    M=malloc(MEMSZ); S=malloc(NSP*4096); mkfw();
    ck("gen","GC 10.1 and 11.5 not claimed",!agt_gen(0x0A0100)&&!agt_gen(0x0B0500)&&agt_gen(0x0A0302)&&agt_gen(0x0B0001));
    ck("pte","PTE keeps address bits 47:12 and UC mtype",agt_pte(0xFFFF123456789FFFULL)==(0x0000123456789000ULL|AGT_PTE_FLAGS)&&((AGT_PTE_FLAGS>>48)&7)==3);
    gen("gc10.3",0x0A0302,ASV(5,2,3),FW1,sizeof FW1);
    gen("gc11.0",0x0B0001,ASV(6,0,1),FW2,sizeof FW2);
    printf(bad?"FAILED %d\n":"ALL PASS\n",bad); return bad?1:0; }
