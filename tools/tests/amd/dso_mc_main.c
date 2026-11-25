#include <stdio.h>
#include <string.h>
#include "drivers/video/amd/dc/dcn_so.c"

#define S2 0x34C0u
#define VB 0xF400000000ULL
#define VSZ (512ULL<<20)
typedef struct { u32 r[0x8000]; u64 t; int calls, gpint_dead, psr, fw_rb, wrong_map; const dso_gen_t* g; u32 ring[1024]; u8* mem; } sim_t;
static sim_t S;
static int bad;
static void ck(const char* g,const char* n,int c){ printf("  %s %s %s\n",g,n,c?"ok":"FAIL"); if(!c)bad++; }

static u32 crc_of(u64 mc){ u64 o=mc-VB; if(S.wrong_map)o+=0x100000; u32 h=2166136261u; const u32* p=(const u32*)(S.mem+o); for(int i=0;i<4096;i++){ h^=p[i]; h*=16777619u; } return h; }
static void vblank(void){
    const dso_gen_t* g=S.g;
    for(int p=0;p<4;p++){ u32* f=&S.r[S2+0x061B+p*0xDC]; if(*f&0x100){ *f&=~0x100u; S.r[S2+g->inuse+p*0xDC]=S.r[S2+0x060A+p*0xDC]; S.r[S2+g->inuse+1+p*0xDC]=S.r[S2+0x060B+p*0xDC]; } }
    for(int p=0;p<4;p++){ S.r[S2+g->otg_fc+p*0x80]++; u32 c=S2+g->crc+p*0x80;
        if(S.r[c]&1){ u64 a=((u64)S.r[S2+g->inuse+1+p*0xDC]<<32)|S.r[S2+g->inuse+p*0xDC]; u32 h=crc_of(a); S.r[c+g->crcd]=h; S.r[c+g->crcd+1]=h>>16&0xFFFF; } } }
static u32 rd(void* c,u32 dw){ (void)c; return dw<0x8000?S.r[dw]:0xFFFFFFFFu; }
static void wr(void* c,u32 dw,u32 v){ (void)c; if(dw>=0x8000)return; const dso_gen_t* g=S.g; u32 dm=S2+g->dmr;
    if(dw==dm+0x62){ if(S.gpint_dead){ S.r[dw]=v; return; } u32 cmd=(v>>16)&0xFFF; S.r[dw]=v&~(0xFu<<28); if(cmd==7)S.r[dm+0x54]=S.psr?0x20:0; return; }
    if(dw==dm+0x40){ S.r[dw]=v; if(S.fw_rb){ u32 w0=S.ring[0]; if((w0&0xFF)==64&&((w0>>8)&0xFF)==3)S.psr=0; S.r[dm+0x41]=v; } return; }
    for(int p=0;p<4;p++)if(dw==S2+0x060A+p*0xDC){ S.r[dw]=v; S.r[S2+0x061B+p*0xDC]|=0x100; return; }
    S.r[dw]=v; }
static u64 ns(void* c){ (void)c; S.t+=1000; if(++S.calls%7==0)vblank(); return S.t; }
static const dso_io_t IO={rd,wr,ns,0};

static void setup(const dso_gen_t* g,int p,u64 a){
    u8* m=S.mem; memset(&S,0,sizeof S); S.mem=m; S.g=g;
    for(int i=0;i<4;i++)S.r[S2+0x05F3+i*0xDC]=1;
    u32 h=p*0xDC; S.r[S2+0x05E5+h]=8; S.r[S2+0x05F3+h]=0; S.r[S2+0x0607+h]=1919; S.r[S2+0x05EA+h]=(1080u<<16)|1920u;
    S.r[S2+0x060A+h]=(u32)a; S.r[S2+0x060B+h]=(u32)(a>>32); S.r[S2+g->inuse+h]=(u32)a; S.r[S2+g->inuse+1+h]=(u32)(a>>32); S.r[S2+g->otg_ctl+p*0x80]=1;
    u32 v=S2+g->vmfb; S.r[v]=(u32)(VB>>24); S.r[v+1]=(u32)((VB+VSZ-1)>>24); S.r[v+2]=(u32)(0x80000000ULL>>24); }

int main(void){
    static u8 mem[3<<20]; S.mem=mem;
    static const u32 V[]={0x020100,0x030001,0x030102,0x030104,0x030105,0x030106,0x030500,0x030501};
    for(unsigned i=0;i<sizeof V/sizeof V[0];i++){
        const dso_gen_t* g=dso_gen(V[i],0); if(!g){ ck("gen","known version",0); continue; } const char* nm=g->name; int pipe=(int)(i%2);
        u64 a=VB; setup(g,pipe,a); for(unsigned k=0;k<sizeof mem/4;k++)((u32*)mem)[k]=k*2654435761u;
        dso_mc_t m; ck(nm,"reads DCN_VM_FB_LOCATION/OFFSET",dso_mc_dcn(&IO,g,S2,&m)==0&&m.fb_base==VB&&m.fb_top==VB+VSZ-1&&m.fb_off==0x80000000ULL);
        dso_map_t mp; u64 bar=0xC0000000ULL;
        ck(nm,"BAR mapping derived",!dso_map(&m,a+0x10000,bar+0x10000,bar,256ULL<<20,32ULL<<20,&mp)&&mp.via==1&&mp.cpu0==bar&&mp.lim==VB+(256ULL<<20));
        ck(nm,"carve-out mapping derived",!dso_map(&m,a+0x10000,0x80010000ULL,bar,256ULL<<20,32ULL<<20,&mp)&&mp.via==2&&mp.cpu0==0x80000000ULL&&mp.lim==VB+VSZ-(32ULL<<20));
        ck(nm,"wrong CPU address refused",dso_map(&m,a+0x10000,bar+0x20000,bar,256ULL<<20,32ULL<<20,&mp)!=0);
        ck(nm,"surface outside VRAM refused",dso_map(&m,VB+VSZ,bar,bar,256ULL<<20,32ULL<<20,&mp)!=0);
        dso_mc_t z=m; z.fb_base=z.fb_top=0; ck(nm,"unset window refused",dso_map(&z,a,bar,bar,256ULL<<20,0,&mp)!=0);
        dso_resv_t rv={0}; rv.n=1; rv.lo[0]=0x80200000ULL; rv.hi[0]=0x80210000ULL; dso_resv_xl(&rv,&m);
        ck(nm,"DMCUB window given as physical also avoided in MC space",rv.n==2&&rv.lo[1]==VB+0x200000&&rv.hi[1]==VB+0x210000);
        u64 back=VB+0x100000; memcpy(mem+0x100000,mem,4096*4); S.wrong_map=0;
        dso_vfy_t v; int r=dso_verify(&IO,g,S2,pipe,a,back,(volatile u32*)(mem+0x100000),1920,1080,&v);
        ck(nm,"verify passes, CRC same and proven live",r==0&&v.latched&&v.inuse_ok&&v.crc_same&&v.crc_live&&dso_inuse(&IO,g,S2,pipe)==back&&!(S.r[S2+g->crc+pipe*0x80]&1));
        ck(nm,"verify restored the probe pixel",!memcmp(mem+0x100000,mem,16));
        dso_flip(&IO,g,S2,pipe,a); dso_wait(&IO,g,S2,pipe,100000); memset(mem+0x100000,0x55,4096*4);
        r=dso_verify(&IO,g,S2,pipe,a,back,(volatile u32*)(mem+0x100000),1920,1080,&v);
        ck(nm,"wrong pixels at the flipped address: CRC differs, flipped back",r==-2&&!v.crc_same&&dso_inuse(&IO,g,S2,pipe)==a);
        S.psr=0; u32 raw=9; ck(nm,"PSR inactive via GPINT",(S.r[S2+g->dmr+0x60]=1u<<16,dso_psr_state(&IO,g,S2,&raw))==0&&raw==0);
        S.psr=1; ck(nm,"PSR active via GPINT",dso_psr_state(&IO,g,S2,&raw)==1&&raw==0x20);
        S.gpint_dead=1; u64 t0=S.t; ck(nm,"GPINT unanswered is bounded",dso_psr_state(&IO,g,S2,&raw)==-2&&S.t-t0<=3000000ULL); S.gpint_dead=0;
        S.r[S2+g->dmr+0x60]=0; ck(nm,"GPINT not sent when DMCUB is off",dso_psr_state(&IO,g,S2,&raw)==-1); S.r[S2+g->dmr+0x60]=1u<<16;
        u32 dm=S2+g->dmr; S.r[dm+0x0F+4]=0x04000000u; S.r[dm+0x17+4]=0x80000000u|(0x04000000u+0xFFFF); S.r[dm+0x1F+8]=(u32)(VB+0x300000); S.r[dm+0x20+8]=(u32)((VB+0x300000)>>32);
        S.r[dm+0x3E]=0x04000000u; S.r[dm+0x3F]=4096;
        u64 ib; u32 isz; ck(nm,"inbox ring located in DMCUB window 4",dso_inbox(&IO,g,S2,&ib,&isz)==4&&ib==VB+0x300000&&isz==4096);
        u32 c[16]; dso_cmd_psr_off(c); S.fw_rb=1;
        ck(nm,"PSR disable command sent through inbox and consumed",dso_rb_send(&IO,g,S2,(volatile u8*)S.ring,isz,c,20000)==0&&S.r[dm+0x40]==64&&S.psr==0&&dso_psr_state(&IO,g,S2,&raw)==0);
        S.fw_rb=0; t0=S.t; ck(nm,"inbox send bounded when firmware does not consume",dso_rb_send(&IO,g,S2,(volatile u8*)S.ring,isz,c,20000)==-2&&S.t-t0<=30000000ULL);
        S.r[dm+0x41]=(S.r[dm+0x40]+64)%isz; ck(nm,"full inbox refused",dso_rb_send(&IO,g,S2,(volatile u8*)S.ring,isz,c,20000)==-3);
        dso_cmd_abm_bl(c,0x8000); ck(nm,"ABM backlight command layout",c[0]==(66u|(2u<<8)|(32u<<24))&&c[2]==0x8000&&c[3]==0x101u);
    }
    dso_mc_t h; memset(&S,0,sizeof S); S.r[0x100]=0xF400; S.r[0x101]=0xF5FF; S.r[0x80]=0x80; S.r[0x102]=0; S.r[0x103]=0xFFFFFF; S.r[0x104]=0;
    ck("mmhub","MC_VM_FB_LOCATION + FB_OFFSET",dso_mc_hub(&IO,0x100,0x80,0x102,&h)==0&&h.fb_base==VB&&h.fb_top==0xF5FFFFFFFFULL&&h.fb_off==0x80000000ULL);
    dso_mc_t d=h; ck("mmhub","agrees with DCN copy",dso_mc_same(&d,&h)); d.fb_off+=1ULL<<24; ck("mmhub","disagreement detected",!dso_mc_same(&d,&h));
    printf(bad?"%d FAILED\n":"ALL PASS\n",bad); return bad?1:0; }
