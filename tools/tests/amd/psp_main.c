#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "drivers/video/amd/sdma/sdma_core.c"
#include "drivers/video/amd/psp/psp_core.c"

#define MP0 0x16000u
#define SEG0 0x1260u
#define SEG1 0xA000u
#define MCB 0xF400000000ULL
#define MEMSZ (16u<<20)
#define PA 0x30000u
static u32 RG[0x20000]; static u8* M; static u64 T; static int bad;
static int dead, noready, refuse, stall, ringst, ring_on, destroyed, pspfw, direct_w, ncmd, wrapped;
static u32 cmdst, toc_tmr, tmr_lo, tmr_hi, tmr_sz, tmr_fl, tmr_plo, tmr_phi, ld_type[8], ld_size[8], nld; static u8 ld_data[8][65536]; static u64 ring_mc; static u32 ring_sz, last_wp;
static const asd_gen_t* G; static u32 srp;
static void ck(const char* g,const char* n,int c){ printf("  %s %s %s\n",g,n,c?"ok":"FAIL"); if(!c)bad++; }
static u8* mp(u64 mc,u32 n){ return (mc>=MCB&&mc+n<=MCB+MEMSZ)?M+(mc-MCB):0; }
static u32 sr(u32 off){ return RG[asd_reg(&(asd_t){.seg0=SEG0,.seg1=SEG1},off)]; }
static void psp_frames(u32 wp){
    u32* ring=(u32*)mp(ring_mc,ring_sz); if(!ring)return;
    while(last_wp!=wp){ u32* f=ring+last_wp; last_wp=(last_wp+16)%(ring_sz/4); if(last_wp==0)wrapped=1;
        u32* c=(u32*)mp(((u64)f[1]<<32)|f[0],1024); if(!c)continue; ncmd++;
        u32 id=c[2];
        if(id==APS_CMD_SETUP_TMR){ tmr_lo=c[7]; tmr_hi=c[8]; tmr_sz=c[9]; tmr_fl=c[10]; tmr_plo=c[11]; tmr_phi=c[12]; }
        else if(id==APS_CMD_LOAD_IP_FW&&nld<8){ u8* d=mp(((u64)c[8]<<32)|c[7],c[9]); ld_type[nld]=c[10]; ld_size[nld]=c[9]; if(d&&c[9]<=65536)memcpy(ld_data[nld],d,c[9]);
            nld++; if(!cmdst&&(c[10]==APS_FW_SDMA0||c[10]==APS_FW_SDMA_TH1))pspfw=1; }
        else if(id==APS_CMD_LOAD_TOC)c[APS_RESP/4+4]=toc_tmr;
        c[APS_RESP/4]=cmdst;
        if(!stall){ u32* fe=(u32*)mp(((u64)f[4]<<32)|f[3],4); if(fe)*fe=f[5]; } } }
static void sdma_run(u32 wp){
    u32* ring=(u32*)mp(((u64)sr(G->rb_base+1)<<40)|((u64)sr(G->rb_base)<<8),16384); if(!ring)return;
    while(srp!=wp){ u32 i=srp/4, h=ring[i], len=1; if((h&0xFF)==5){ u32* d=(u32*)mp(((u64)ring[i+2]<<32)|ring[i+1],4); if(d)*d=ring[i+3]; len=4; } srp=((i+len)%ASD_RING_DW)*4; } }
static u32 rd(void* c,u32 dw){ (void)c; return dead?0xFFFFFFFFu:RG[dw]; }
static void wr(void* c,u32 dw,u32 v){ (void)c; asd_t z={.seg0=SEG0,.seg1=SEG1};
    if(G&&dw==asd_reg(&z,G->udata)){ direct_w++; return; }
    RG[dw]=v;
    if(dw==MP0+APS_C64){ if(v==(2u<<16)){ ring_mc=((u64)RG[MP0+APS_C70]<<32)|RG[MP0+APS_C69]; ring_sz=RG[MP0+APS_C71]; last_wp=0; RG[MP0+APS_C67]=0;
            RG[dw]=refuse?0x80000000u|ringst:noready==2?0:0x80000000u; if(!refuse&&noready!=2)ring_on=1; }
        else if(v==(3u<<16)){ ring_on=0; destroyed++; RG[dw]=0x80000000u; } }
    if(dw==MP0+APS_C67&&ring_on)psp_frames(v);
    if(G&&dw==asd_reg(&z,G->wptr)&&pspfw&&(sr(G->rb_cntl)&1)&&!(sr(G->f32)&1))sdma_run(v); }
static u64 ns(void* c){ (void)c; T+=1000; return T; }
static const asd_io_t IO={rd,wr,ns,0};

static void reset(void){ memset(RG,0,sizeof RG); memset(M,0,MEMSZ); dead=noready=refuse=stall=ringst=ring_on=destroyed=pspfw=direct_w=ncmd=wrapped=0; cmdst=0; toc_tmr=0; nld=0; ring_mc=0; ring_sz=0; last_wp=0; srp=0; G=0;
    RG[MP0+APS_C81]=0x00350011u; RG[MP0+APS_C64]=0x80000000u; }
static void mkp(aps_t* p,u32 ver){ *p=(aps_t){0}; p->io=&IO; p->g=aps_gen(ver); p->base=MP0; p->mem=M+PA; p->mc=MCB+PA; }
static void sdset(asd_t* e,const asd_gen_t* g){ G=g; *e=(asd_t){0}; e->io=&IO; e->g=g; e->seg0=SEG0; e->seg1=SEG1; e->mem=(volatile u32*)(M+0x10000); e->mc=MCB+0x10000;
    RG[asd_reg(e,g->f32)]=1; RG[asd_reg(e,g->db)]=1u<<28; }

static u8 FW1[64+4096], FW2[64+8192+4096];
static void mkfw(void){
    memset(FW1,0,sizeof FW1); u32* h=(u32*)FW1; h[0]=sizeof FW1; h[1]=48; FW1[8]=1; h[4]=52; h[5]=4096; h[6]=64; for(u32 i=64;i<sizeof FW1;i++)FW1[i]=(u8)(i*7);
    memset(FW2,0,sizeof FW2); h=(u32*)FW2; h[0]=sizeof FW2; h[1]=64; FW2[8]=2; h[4]=24; h[5]=8192; h[6]=64;
    h[9]=8192; h[10]=960; h[11]=32; h[12]=64+8192; h[13]=4096; h[14]=1000; h[15]=0; for(u32 i=64;i<sizeof FW2;i++)FW2[i]=(u8)(i*13+1); }

static void flow(const char* nm,u32 mp0,u32 sver,const u8* fw,u32 n){
    reset(); aps_t p; mkp(&p,mp0); asd_t e; asd_fw_t f; const asd_gen_t* g=asd_gen(sver);
    ck(nm,"PSP generation known",p.g!=0); ck(nm,"SDMA firmware parses",asd_fw_parse(&f,fw,n)==0&&f.b0>0);
    ck(nm,"secure OS alive",aps_alive(&p)==1);
    ck(nm,"KM ring created",aps_ring_up(&p,100000)==0&&p.ring&&ring_on);
    ck(nm,"ring address in C2PMSG_69/70 and size 4 KB in C2PMSG_71",RG[MP0+APS_C69]==(u32)(MCB+PA)&&RG[MP0+APS_C70]==(u32)((MCB+PA)>>32)&&RG[MP0+APS_C71]==0x1000);
    u64 tm=MCB+0x100000; int r=aps_tmr(&p,tm,0x80100000ULL,APS_TMR_SZ,1000000);
    ck(nm,"SETUP_TMR sent with virt+phys address",r==0&&p.tmr&&tmr_lo==(u32)tm&&tmr_hi==(u32)(tm>>32)&&tmr_sz==APS_TMR_SZ&&tmr_fl==2&&tmr_plo==0x80100000u&&tmr_phi==0);
    sdset(&e,g); e.psp=0; int d=asd_start(&e,&f);
    ck(nm,"direct load ignored by PSP-owned engine, ring test fails in bounded time",d==-3&&!e.up);
    r=aps_sdma(&p,g,&f,1000000);
    if(!g->v6)ck(nm,"LOAD_IP_FW SDMA0 with ucode bytes",r==0&&nld==1&&ld_type[0]==APS_FW_SDMA0&&ld_size[0]==f.b0&&!memcmp(ld_data[0],f.th0,f.b0));
    else ck(nm,"LOAD_IP_FW SDMA TH0 then TH1 with ctx/ctl sizes",r==0&&nld==2&&ld_type[0]==APS_FW_SDMA_TH0&&ld_type[1]==APS_FW_SDMA_TH1&&ld_size[0]==f.b0&&ld_size[1]==f.b1&&!memcmp(ld_data[0],f.th0,f.b0)&&!memcmp(ld_data[1],f.th1,f.b1));
    ck(nm,"fence values and write pointer advance per frame",p.fence==(g->v6?3u:2u)&&RG[MP0+APS_C67]==(g->v6?48u:32u)&&*(u32*)(M+PA+APS_FENCE)==p.fence);
    sdset(&e,g); e.psp=1; direct_w=0; d=asd_start(&e,&f);
    ck(nm,"SDMA starts on PSP-loaded firmware without direct ucode writes",d==0&&e.up&&direct_w==0);
    ck(nm,"SDMA fence round trip after PSP load",asd_kick(&e,100000)==0);
    aps_ring_down(&p,100000); ck(nm,"ring destroy sends DESTROY_RINGS",destroyed==1&&!p.ring&&RG[MP0+APS_C64]==0x80000000u); }

static int rdfile(const char* path,u8** b,u32* n){ FILE* f=fopen(path,"rb"); if(!f)return -1; fseek(f,0,SEEK_END); long l=ftell(f); fseek(f,0,SEEK_SET); *b=malloc((size_t)l); if(fread(*b,1,(size_t)l,f)!=(size_t)l){ fclose(f); return -1; } fclose(f); *n=(u32)l; return 0; }

int main(void){
    M=calloc(1,MEMSZ); mkfw();
    ck("gen","Phoenix 13.0.4 uses MP0 segment 1",aps_gen(0x0D0004)&&aps_gen(0x0D0004)->seg==1);
    ck("gen","Rembrandt, Raphael, Mendocino, Van Gogh on segment 0",aps_gen(0x0D0003)->seg==0&&aps_gen(0x0D0005)->seg==0&&aps_gen(0x0D0008)->seg==0&&aps_gen(0x0B0500)->seg==0);
    ck("gen","dGPU 13.0.0 and Renoir 12.0.1 not claimed",!aps_gen(0x0D0000)&&!aps_gen(0x0C0001));
    flow("5.2.3",0x0D0003,0x050203,FW1,sizeof FW1);
    flow("6.0.1",0x0D0004,0x060001,FW2,sizeof FW2);
    aps_t p; u64 t0; int r;
    reset(); mkp(&p,0x0D0003); RG[MP0+APS_C81]=0; r=aps_ring_up(&p,100000); ck("err","secure OS not running is reported",r==-3&&!ring_on&&strstr(p.why,"not running"));
    reset(); mkp(&p,0x0D0003); dead=1; ck("err","all-ones registers reported",aps_ring_up(&p,100000)==-2);
    reset(); mkp(&p,0x0D0003); RG[MP0+APS_C64]=0; t0=T; r=aps_ring_up(&p,100000);
    ck("err","PSP held by firmware (never ready) detected within the bound",r==-4&&p.timeouts==1&&T-t0<=110000000ULL&&strstr(p.why,"held by the system firmware")&&RG[MP0+APS_C69]==0);
    reset(); mkp(&p,0x0D0003); refuse=1; ringst=0x13; r=aps_ring_up(&p,100000); ck("err","ring refused with status",r==-5&&p.last==0x13&&!p.ring);
    reset(); mkp(&p,0x0D0003); noready=2; t0=T; r=aps_ring_up(&p,100000); ck("err","no ring answer times out within the bound",r==-5&&T-t0<=140000000ULL);
    reset(); mkp(&p,0x0D0003); aps_ring_up(&p,100000); stall=1; t0=T; r=aps_load(&p,APS_FW_SDMA0,FW1+64,4096,200000);
    ck("err","command without fence times out within the bound",r==-1&&p.timeouts==1&&T-t0<=210000000ULL);
    reset(); mkp(&p,0x0D0003); aps_ring_up(&p,100000); cmdst=0xFFFF000Au; r=aps_load(&p,APS_FW_SDMA0,FW1+64,4096,200000); ck("err","error status returned as soft failure",r==1&&p.last==0xFFFF000Au);
    reset(); mkp(&p,0x0D0003); aps_ring_up(&p,100000); r=aps_load(&p,APS_FW_SDMA0,FW1,APS_FWMAX+4,200000); ck("err","oversized image refused before submit",r==-1&&ncmd==0);
    reset(); mkp(&p,0x0D0003); aps_ring_up(&p,100000); RG[MP0+APS_C67]=8; r=aps_load(&p,APS_FW_SDMA0,FW1+64,4096,200000); ck("err","misaligned ring write pointer refused",r==-1&&ncmd==0);
    reset(); mkp(&p,0x0D0003); ck("err","command without ring refused",aps_load(&p,APS_FW_SDMA0,FW1+64,4096,1000)==-1);
    reset(); mkp(&p,0x0D0003); aps_ring_up(&p,100000); ck("err","unaligned TMR refused",aps_tmr(&p,MCB+0x101000,0,APS_TMR_SZ,1000)==-1&&ncmd==0);
    reset(); mkp(&p,0x0D0003); aps_ring_up(&p,100000); int ok=1; for(int i=0;i<70;i++)if(aps_load(&p,APS_FW_SDMA0,FW1+64,4096,200000)!=0)ok=0;
    ck("ring","70 commands wrap the 64-frame ring",ok&&wrapped&&ncmd==70&&RG[MP0+APS_C67]==(70u*16)%1024);
    u8 toc[64+256]; memset(toc,0,sizeof toc); u32* th=(u32*)toc; th[0]=sizeof toc; th[1]=44; toc[8]=1; th[5]=256; th[6]=64; const u8* td; u32 ts, tz=0;
    ck("toc","TOC file header parses",aps_toc_file(toc,sizeof toc,&td,&ts)==0&&td==toc+64&&ts==256);
    th[5]=4096; ck("toc","TOC payload outside file refused",aps_toc_file(toc,sizeof toc,&td,&ts)==-1);
    reset(); mkp(&p,0x0D0003); aps_ring_up(&p,100000); toc_tmr=0x300000; th[5]=256; r=aps_toc(&p,toc+64,256,&tz,200000); ck("toc","LOAD_TOC returns the TMR size",r==0&&tz==0x300000);
    asd_fw_t f; u8* b; u32 n;
    if(!rdfile("assets/firmware/linux-firmware/amdgpu/sdma_6_0_1.bin",&b,&n)){ ck("file","sdma_6_0_1.bin byte sizes for PSP",asd_fw_parse(&f,b,n)==0&&f.b0==*(u32*)(b+36)&&f.b1==*(u32*)(b+52)&&f.th1==b+*(u32*)(b+48));
        reset(); mkp(&p,0x0D0004); aps_ring_up(&p,100000); r=aps_sdma(&p,asd_gen(0x060001),&f,1000000); ck("file","sdma_6_0_1.bin loads through simulated PSP",r==0&&nld==2&&!memcmp(ld_data[1],f.th1,f.b1)); free(b); }
    else ck("file","sdma_6_0_1.bin present",0);
    if(!rdfile("assets/firmware/linux-firmware/amdgpu/yellow_carp_sdma.bin",&b,&n)){ ck("file","yellow_carp_sdma.bin byte size for PSP",asd_fw_parse(&f,b,n)==0&&f.b0==*(u32*)(b+20));
        reset(); mkp(&p,0x0D0003); aps_ring_up(&p,100000); r=aps_sdma(&p,asd_gen(0x050203),&f,1000000); ck("file","yellow_carp_sdma.bin loads through simulated PSP",r==0&&nld==1&&ld_type[0]==APS_FW_SDMA0); free(b); }
    else ck("file","yellow_carp_sdma.bin present",0);
    reset(); mkp(&p,0x0D0003); aps_ring_up(&p,100000); asd_fw_parse(&f,FW2,sizeof FW2); ck("err","v2 firmware refused for SDMA 5.2",aps_sdma(&p,asd_gen(0x050203),&f,1000)==-1&&ncmd==0);
    free(M);
    printf(bad?"FAILED %d\n":"ALL PASS\n",bad); return bad?1:0; }
