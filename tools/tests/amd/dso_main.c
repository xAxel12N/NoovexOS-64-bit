#include <stdio.h>
#include <string.h>
#include "drivers/video/amd/dc/dcn_so.c"

#define S2 0x34C0u
typedef struct { u32 r[0x8000]; u64 t; int vbl_every, calls, dead, order_ok, hi_seen; u32 otg_ctl, otg_fc, inuse, last_hi[4]; } sim_t;
static sim_t S;
static int bad;
static void ck(const char* g,const char* n,int c){ printf("  %s %s %s\n",g,n,c?"ok":"FAIL"); if(!c)bad++; }

static int hp(u32 dw,u32 reg){ if(dw<S2+reg)return -1; u32 d=dw-S2-reg; if(d%0xDC)return -1; d/=0xDC; return d<4?(int)d:-1; }
static void vblank(void){
    for(int p=0;p<4;p++){ u32* f=&S.r[S2+0x061B+p*0xDC]; if(*f&0x100){ *f&=~0x100u; S.r[S2+S.inuse+p*0xDC]=S.r[S2+0x060A+p*0xDC]; S.r[S2+S.inuse+1+p*0xDC]=S.r[S2+0x060B+p*0xDC]; } }
    for(int p=0;p<4;p++)S.r[S2+S.otg_fc+p*0x80]++; }
static u32 rd(void* c,u32 dw){ (void)c; if(S.dead)return 0xFFFFFFFFu; return dw<0x8000?S.r[dw]:0xFFFFFFFFu; }
static void wr(void* c,u32 dw,u32 v){ (void)c; if(dw>=0x8000)return; int p;
    if((p=hp(dw,0x060B))>=0){ S.hi_seen|=1<<p; S.last_hi[p]=v; }
    if((p=hp(dw,0x060A))>=0){ if(!(S.hi_seen&(1<<p)))S.order_ok=0; S.hi_seen&=~(1<<p); S.r[dw]=v;
        u32* f=&S.r[S2+0x061B+p*0xDC]; if(!(*f&1)){ if(*f&2){ S.r[S2+S.inuse+p*0xDC]=v; S.r[S2+S.inuse+1+p*0xDC]=S.last_hi[p]; } else *f|=0x100; } return; }
    S.r[dw]=v; }
static u64 ns(void* c){ (void)c; S.t+=1000; if(S.vbl_every&&++S.calls%S.vbl_every==0)vblank(); return S.t; }
static const dso_io_t IO={rd,wr,ns,0};

static void gop(const dso_gen_t* g,int p,u64 a){
    memset(&S,0,sizeof S); S.otg_ctl=g->otg_ctl; S.otg_fc=g->otg_fc; S.inuse=g->inuse; S.order_ok=1; S.vbl_every=7;
    for(int i=0;i<4;i++)S.r[S2+0x05F3+i*0xDC]=1;
    u32 h=p*0xDC; S.r[S2+0x05E5+h]=8; S.r[S2+0x05E7+h]=0; S.r[S2+0x05F3+h]=0; S.r[S2+0x0607+h]=1919; S.r[S2+0x05EA+h]=(1080u<<16)|1920u;
    S.r[S2+0x060A+h]=(u32)a; S.r[S2+0x060B+h]=(u32)(a>>32); S.r[S2+0x061B+h]=2; S.r[S2+g->otg_ctl+p*0x80]=1; }

int main(void){
    static const struct { u32 ver; u16 did; } G[]={{0x020100,0x1636},{0x020100,0x164C},{0x030001,0x163F},{0x030102,0x1681},{0x030102,0x164D},{0x030104,0x15BF},{0x030104,0x15C8},{0x030106,0x1506},{0x030500,0x150E}};
    ck("gen","unknown DCN rejected",!dso_gen(0x090909,0x1234)&&!dso_gen(0,0x1234));
    for(unsigned i=0;i<sizeof G/sizeof G[0];i++){
        const dso_gen_t* g=dso_gen(G[i].ver,0); char nm[48]; snprintf(nm,sizeof nm,"%s/%04x",g?g->name:"?",G[i].did);
        ck(nm,"picked by IP discovery version",g&&g->ver==G[i].ver);
        ck(nm,"picked by PCI ID without discovery",dso_gen(0,G[i].did)==g);
        if(!g)continue;
        int pipe=(int)(i%2); u64 a=0xF400000000ULL+0x100000ULL*i;
        gop(g,pipe,a); dso_pipe_t p; int pi=dso_active(&IO,g,S2,&p);
        ck(nm,"finds the firmware pipe",pi==pipe&&p.addr==a&&p.pitch==1920&&p.vpw==1920&&p.vph==1080&&p.bpp==4&&!p.sw&&p.on);
        S.r[S2+g->otg_ctl+pipe*0x80]=0; ck(nm,"ignores a pipe whose OTG is off",dso_active(&IO,g,S2,&p)<0); S.r[S2+g->otg_ctl+pipe*0x80]=1;
        u64 len=((u64)1920*4*1080+0xFFFF)&~0xFFFFULL; dso_resv_t rv; u32 dm=S2+g->dmr;
        S.r[dm+0x0F]=0x1000; S.r[dm+0x17]=0x80000000u|(0x1000+0x3FFFF); S.r[dm+0x1F]=(u32)(a+len+0x10000); S.r[dm+0x20]=(u32)((a+len)>>32);
        S.r[dm+0x0B]=0x80000000u|0xFFFF; S.r[dm+0]=(u32)(a+3*len); S.r[dm+1]=(u32)((a+3*len)>>32);
        dso_resv(&IO,g,S2,&rv);
        ck(nm,"reads DMCUB cache window and inbox regions",rv.n==2&&rv.lo[0]==a+len+0x10000&&rv.hi[0]==a+len+0x10000+0x40000&&rv.lo[1]==a+3*len&&rv.hi[1]==a+3*len+0x10000);
        u64 b=dso_place(a,len,a+(256ULL<<20),&rv);
        int clear=b&&!(b&0xFFFF)&&b>=a+len; for(int k=0;k<rv.n;k++)if(b<rv.hi[k]&&rv.lo[k]<b+len)clear=0;
        ck(nm,"second buffer placed clear of the screen and DMCUB",clear);
        ck(nm,"placement respects the memory window",dso_place(a,len,a+len+len/2,&rv)==0);
        ck(nm,"flip refused for unaligned address",dso_flip(&IO,g,S2,pipe,b+4)==-1);
        ck(nm,"flip writes high then low, vsync type",dso_flip(&IO,g,S2,pipe,b)==0&&S.order_ok&&!(S.r[S2+0x061B+pipe*0xDC]&2)&&dso_pending(&IO,g,S2,pipe)==1);
        ck(nm,"flip latches at vertical blank",dso_wait(&IO,g,S2,pipe,100000)==0&&dso_inuse(&IO,g,S2,pipe)==b);
        u32 fc0=S.r[S2+g->otg_fc+pipe*0x80];
        for(int k=0;k<6;k++){ u64 t=(k&1)?b:a; if(dso_flip(&IO,g,S2,pipe,t)||dso_wait(&IO,g,S2,pipe,100000)||dso_inuse(&IO,g,S2,pipe)!=t){ fc0=~0u; break; } }
        ck(nm,"six alternating flips each latch once per frame",fc0!=~0u&&S.r[S2+g->otg_fc+pipe*0x80]>=fc0+6);
        S.vbl_every=0; dso_flip(&IO,g,S2,pipe,a); u64 t0=S.t; int to=dso_wait(&IO,g,S2,pipe,50000);
        ck(nm,"wait is bounded when vblank never comes",to==-2&&S.t-t0<=60000000ULL);
        S.vbl_every=7; dso_wait(&IO,g,S2,pipe,100000);
        S.r[S2+0x061B+pipe*0xDC]|=1; ck(nm,"locked surface update refused",dso_flip(&IO,g,S2,pipe,b)==-3); S.r[S2+0x061B+pipe*0xDC]&=~1u;
        S.dead=1; ck(nm,"dead registers reported",dso_read(&IO,g,S2,pipe,&p)==-2&&dso_wait(&IO,g,S2,pipe,1000)==-1&&dso_flip(&IO,g,S2,pipe,b)==-2&&dso_active(&IO,g,S2,&p)<0); S.dead=0;
        if(g->ver>=0x030500){ gop(g,pipe,a); S.r[S2+0x1B41+pipe*0x80]=1; S.r[S2+g->otg_ctl+pipe*0x80]=0; ck(nm,"DCN 3.5 uses its own OTG_CONTROL offset",dso_active(&IO,g,S2,&p)<0); }
    }
    printf(bad?"FAIL %d\n":"ALL PASS\n",bad); return bad!=0; }
