#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "drivers/video/amd/sdma/sdma_core.c"

#define SEG0 0x1260u
#define SEG1 0xA000u
#define MCB 0xF400000000ULL
#define MEMSZ (16u<<20)
static u32 RG[0x20000]; static u8* M; static u64 T; static int dead, stall, uw0, uw1, uat; static u32 rp;
static const asd_gen_t* G;
static int bad;
static void ck(const char* g,const char* n,int c){ printf("  %s %s %s\n",g,n,c?"ok":"FAIL"); if(!c)bad++; }
static u32* mp(u64 mc){ return (mc>=MCB&&mc+4<=MCB+MEMSZ)?(u32*)(M+(mc-MCB)):0; }
static u32 rr(u32 off){ return RG[asd_reg(&(asd_t){.seg0=SEG0,.seg1=SEG1},off)]; }
static void run(u32 wp){
    u32* ring=mp(((u64)rr(G->rb_base+1)<<40)|((u64)rr(G->rb_base)<<8)); u32 n=1u<<((rr(G->rb_cntl)>>1)&0x1F);
    while(rp!=wp){ u32 i=rp/4; u32 h=ring[i]; u32 op=h&0xFF, sub=(h>>8)&0xFF; u32* q=ring+i; u32 len=1;
        if(op==5){ u32* d=mp(((u64)q[2]<<32)|q[1]); if(d)*d=q[3]; len=4; }
        else if(op==11){ u32* d=mp(((u64)q[2]<<32)|q[1]); u32 c=(q[4]+1)/4; if(d)for(u32 k=0;k<c;k++)d[k]=q[3]; len=5; }
        else if(op==1&&sub==0){ u8* s=(u8*)mp(((u64)q[4]<<32)|q[3]); u8* d=(u8*)mp(((u64)q[6]<<32)|q[5]); if(s&&d)memmove(d,s,q[1]+1); len=7; }
        else if(op==1&&sub==4){ u32* s=mp(((u64)q[2]<<32)|q[1]); u32* d=mp(((u64)q[7]<<32)|q[6]); u32 sp=(q[4]>>13)+1, dp=(q[9]>>13)+1, w=(q[11]&0x3FFF)+1, hh=(q[11]>>16)+1;
            len=13; if(s&&d)for(u32 y=0;y<hh;y++)memcpy(d+(u64)y*dp,s+(u64)y*sp,w*4); }
        rp=((i+len)%n)*4; }
    RG[asd_reg(&(asd_t){.seg0=SEG0,.seg1=SEG1},G->rptr)]=rp; u32* wb=mp(((u64)rr(G->rpa_hi)<<32)|rr(G->rpa_lo)); if(wb)*wb=rp; }
static u32 rd(void* c,u32 dw){ (void)c; return dead?0xFFFFFFFFu:RG[dw]; }
static void wr(void* c,u32 dw,u32 v){ (void)c; asd_t z={.seg0=SEG0,.seg1=SEG1};
    if(dw==asd_reg(&z,G->uaddr)){ uat=(int)v; RG[dw]=v; return; }
    if(dw==asd_reg(&z,G->udata)){ if(uat==0x8000)uw1++; else uw0++; return; }
    RG[dw]=v;
    if(dw==asd_reg(&z,G->wptr)&&!stall&&(rr(G->rb_cntl)&1)&&!(rr(G->f32)&1)&&uw0>0)run(v); }
static u64 ns(void* c){ (void)c; T+=1000; return T; }
static const asd_io_t IO={rd,wr,ns,0};

static u8 FW1[64+4096], FW2[64+8192];
static void mkfw(void){
    memset(FW1,0,sizeof FW1); u32* h=(u32*)FW1; h[0]=sizeof FW1; h[1]=48; FW1[8]=1; h[4]=52; h[5]=4096; h[6]=64;
    memset(FW2,0,sizeof FW2); h=(u32*)FW2; h[0]=sizeof FW2; h[1]=64; FW2[8]=2; h[4]=24; h[5]=4096; h[6]=64;
    h[9]=4096; h[10]=960; h[11]=32; h[12]=64+4096; h[13]=4096; h[14]=1000; h[15]=0; }
static void setup(asd_t* e,const asd_gen_t* g){
    memset(RG,0,sizeof RG); memset(M,0,MEMSZ); dead=stall=uw0=uw1=0; uat=-1; rp=0; G=g;
    *e=(asd_t){0}; e->io=&IO; e->g=g; e->seg0=SEG0; e->seg1=SEG1; e->mem=(volatile u32*)M; e->mc=MCB;
    RG[asd_reg(e,g->f32)]=1; RG[asd_reg(e,g->db)]=1u<<28; RG[asd_reg(e,g->utcl1)]=0xFFFFFFFFu&~0x1000u; }

static void gen_tests(const char* nm,u32 ver,const u8* fw,u32 n){
    asd_t e; asd_fw_t f; const asd_gen_t* g=asd_gen(ver); ck(nm,"generation known",g!=0); if(!g)return;
    ck(nm,"firmware header parses",asd_fw_parse(&f,fw,n)==0);
    setup(&e,g); int r=asd_start(&e,&f); ck(nm,"engine starts and answers the ring test fence",r==0&&e.up);
    ck(nm,"ucode words written",g->v6?(uw0==(int)f.n0&&uw1==(int)f.n1):(uw0==(int)f.n0));
    if(!g->v6)ck(nm,"ucode address ends at firmware version",rr(g->uaddr)==f.ver);
    ck(nm,"ring base 256-byte units",rr(g->rb_base)==(u32)(MCB>>8)&&rr(g->rb_base+1)==(u32)(MCB>>40));
    ck(nm,"ring size order 12 and enabled",((rr(g->rb_cntl)>>1)&0x1F)==12&&(rr(g->rb_cntl)&1)&&(rr(g->rb_cntl)&(1u<<12)));
    ck(nm,"doorbell off, engine unhalted, IB on",!(rr(g->db)&(1u<<28))&&!(rr(g->f32)&1)&&(rr(g->ib_cntl)&1));
    ck(nm,"rptr writeback in VRAM slot",rr(g->rpa_lo)==(u32)(MCB+ASD_RPTR_OFF)&&rr(g->rpa_hi)==(u32)((MCB+ASD_RPTR_OFF)>>32));
    ck(nm,"wptr poll address set",rr(g->poll_lo)==(u32)(MCB+ASD_WPTR_OFF));
    if(g->v6)ck(nm,"v6 RB_PRIV and F32 wptr poll",(rr(g->rb_cntl)&(1u<<23))&&(rr(g->rb_cntl)&(1u<<11))&&!(rr(g->rb_cntl)&(1u<<8)));
    else ck(nm,"utcl1 resp mode 3 redo 9",(rr(g->utcl1)&0xE00)==(3u<<9)&&(rr(g->utcl1)&0x3E)==(9u<<1));
    u64 A=MCB+0x100000, B=MCB+0x400000; u32 pb=1000*4;
    ck(nm,"pitched fill",asd_fill_rect(&e,A,pb,640,300,0x11223344u,10000)==0);
    int ok=1; for(u32 y=0;y<300;y++){ u32* p=mp(A+(u64)y*pb); if(p[0]!=0x11223344u||p[639]!=0x11223344u||p[640]!=0)ok=0; } ck(nm,"fill stays inside the rectangle",ok);
    for(u32 y=0;y<300;y++)for(u32 x=0;x<640;x++)mp(A+(u64)y*pb)[x]=y*7919u+x;
    ck(nm,"sub-window copy",asd_copy_rect(&e,A,pb,B,2560*4,640,300,10000)==0);
    ok=1; for(u32 y=0;y<300;y++){ u32* p=mp(B+(u64)y*2560*4); if(p[0]!=y*7919u||p[639]!=y*7919u+639||p[640]!=0)ok=0; } ck(nm,"copy matches source and pitch",ok);
    ck(nm,"contiguous fill over 4 MiB splits",asd_fill_rect(&e,MCB+0x100000,1920*4,1920,1080,0xABCDEF01u,10000)==0&&mp(MCB+0x100000+1920*4*1080-4)[0]==0xABCDEF01u&&mp(MCB+0x100000+1920*4*1080)[0]!=0xABCDEF01u);
    int good=1; for(int i=0;i<700;i++)if(asd_fill_rect(&e,A,pb,16,1,(u32)i,10000)||mp(A)[0]!=(u32)i)good=0; ck(nm,"700 ops wrap the ring cleanly",good&&e.wp<ASD_RING_DW);
    ck(nm,"many-line pitched fill batches",asd_fill_rect(&e,A,pb,8,1000,7,10000)==0&&mp(A+999ULL*pb)[7]==7);
    stall=1; u64 t0=T; r=asd_fill_rect(&e,A,pb,8,8,1,5000); ck(nm,"stalled engine times out, bounded",r==-1&&e.timeouts==1&&T-t0<10000000ULL);
    dead=1; r=asd_fill_rect(&e,A,pb,8,8,1,5000); ck(nm,"dead registers reported",r==-2);
    ck(nm,"bad args refused",asd_copy_rect(&e,A+2,pb,B,pb,8,8,10)==-1&&asd_fill_rect(&e,A,8,8,1,0,10)==-1);
    setup(&e,g); dead=1; ck(nm,"start refuses all-ones registers",asd_start(&e,&f)==-2);
    setup(&e,g); stall=1; ck(nm,"start fails and halts when the ring test never lands",asd_start(&e,&f)==-3&&!e.up&&(rr(g->f32)&1));
    asd_fw_t o; asd_fw_parse(&o,g->v6?FW1:FW2,g->v6?sizeof FW1:sizeof FW2); setup(&e,g); ck(nm,"firmware of the other layout refused",asd_start(&e,&o)==-1&&uw0==0); }

static u8* slurp(const char* p,u32* n){ FILE* f=fopen(p,"rb"); if(!f)return 0; static u8 b[300000]; *n=(u32)fread(b,1,sizeof b,f); fclose(f); return b; }

int main(void){
    M=malloc(MEMSZ); mkfw();
    asd_fw_t f;
    ck("fw","v1 synthetic",asd_fw_parse(&f,FW1,sizeof FW1)==0&&f.n0==1024&&!f.th1&&f.ver==52);
    ck("fw","v2 synthetic uses jump-table ends in dwords",asd_fw_parse(&f,FW2,sizeof FW2)==0&&f.n0==992&&f.n1==1000&&f.th1==FW2+64+4096);
    u8 t[sizeof FW1]; memcpy(t,FW1,sizeof t); t[0]^=1; ck("fw","size mismatch refused",asd_fw_parse(&f,t,sizeof t)<0);
    memcpy(t,FW1,sizeof t); t[8]=3; ck("fw","unknown header refused",asd_fw_parse(&f,t,sizeof t)<0);
    ck("fw","short refused",asd_fw_parse(&f,FW1,40)<0);
    ck("gen","SDMA 4.1.2 (Renoir) not claimed",asd_gen(ASV(4,1,2))==0);
    ck("gen","SDMA 5.2.6 file name",asd_gen(ASV(5,2,6))&&!strcmp(asd_gen(ASV(5,2,6))->fw,"amdgpu/sdma_5_2_6.bin"));
    u32 n; u8* b=slurp("assets/firmware/linux-firmware/amdgpu/sdma_6_0_1.bin",&n);
    if(b){ ck("fw","bundled sdma_6_0_1.bin parses as v2",asd_fw_parse(&f,b,n)==0&&f.hv==2&&f.n0==4224&&f.n1==4096); }
    b=slurp("assets/firmware/linux-firmware/amdgpu/yellow_carp_sdma.bin",&n);
    if(b){ ck("fw","bundled yellow_carp_sdma.bin parses as v1",asd_fw_parse(&f,b,n)==0&&f.hv==1&&f.n0==8448); }
    gen_tests("5.2.3",ASV(5,2,3),FW1,sizeof FW1);
    gen_tests("5.2.6",ASV(5,2,6),FW1,sizeof FW1);
    gen_tests("6.0.1",ASV(6,0,1),FW2,sizeof FW2);
    printf(bad?"%d FAILED\n":"ALL PASS\n",bad); return bad?1:0; }
