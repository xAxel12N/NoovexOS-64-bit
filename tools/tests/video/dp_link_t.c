#include <stdio.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;
#define NVX_BASE_H
#include "drivers/video/dp_link.c"

typedef struct { u8 d[0x800]; int good_lanes, good_bw, eq_bw, cr_vs, eq_pe, dead, aux_fail_at, ops, hw, wrn; u8 vs[4], pe[4]; } sink;
static int fails, checks;
static void ck(const char* n,int c){ checks++; if(!c)fails++; printf("  %s %s\n",n,c?"ok":"FAIL"); }
static void sink_status(sink* s){
    int tp=s->d[0x102]&3, ln=s->d[0x101]&0x1F, bw=s->d[0x100]; u8 st[2]={0,0}, adj[2]={0,0}; int all=1;
    for(int i=0;i<ln&&i<4;i++){ u8 l=s->d[0x103+i]; int v=l&3, p=(l>>3)&3; int cr=!s->dead&&i<s->good_lanes&&bw<=s->good_bw&&v>=s->cr_vs; int eq=cr&&tp>=2&&p>=s->eq_pe&&bw<=s->eq_bw;
        u8 nb=(u8)(cr|(eq?6:0)); if(!eq)all=0; st[i>>1]|=(u8)(nb<<((i&1)*4));
        int rv=cr?v:(v<3?v+1:3), rp=tp>=2&&cr?s->eq_pe:p; if(rv+rp>3)rp=3-rv; adj[i>>1]|=(u8)((rv|(rp<<2))<<((i&1)*4)); }
    s->d[0x202]=st[0]; s->d[0x203]=st[1]; s->d[0x204]=(u8)(all&&tp>=2&&ln?1:0); s->d[0x206]=adj[0]; s->d[0x207]=adj[1]; }
static int s_rd(void* c,u32 a,u8* b,int n){ sink* s=c; if(s->aux_fail_at&&++s->ops>=s->aux_fail_at)return -1; if(a+n>0x800)return -1; if(a==0x202&&(s->d[0x102]&3))sink_status(s); memcpy(b,s->d+a,n); return n; }
static int s_wr(void* c,u32 a,const u8* b,int n){ sink* s=c; if(s->aux_fail_at&&++s->ops>=s->aux_fail_at)return -1; if(a+n>0x800)return -1; s->wrn++; memcpy(s->d+a,b,n); return n; }
static int s_drive(void* c,int lanes,const u8* vs,const u8* pe){ sink* s=c; for(int i=0;i<lanes;i++){ s->vs[i]=vs[i]; s->pe[i]=pe[i]; } return 1; }
static u64 waited; static void s_delay(void* c,u32 us){ (void)c; waited+=us; }
static int s_hw(void* c,int lanes,int bw,int enh){ sink* s=c; s->hw++; s->d[0x100]=(u8)bw; s->d[0x101]=(u8)(lanes|(enh?0x80:0)); for(int i=0;i<lanes;i++)s->d[0x103+i]=(u8)(3|(3<<3)); s->d[0x102]=2; sink_status(s); int ok=(s->d[0x204]&1); s->d[0x102]=0; return ok; }
static const dp_link_ops sw_ops={s_rd,s_wr,0,s_drive,0,s_delay};
static const dp_link_ops hw_ops={s_rd,s_wr,0,0,s_hw,s_delay};
static void mk(sink* s,int rate,int lanes){ memset(s,0,sizeof(*s)); s->d[0]=0x12; s->d[1]=(u8)rate; s->d[2]=(u8)(lanes|0x80|0x40); s->good_lanes=4; s->good_bw=0x1E; s->eq_bw=0x1E; s->cr_vs=2; s->eq_pe=1; }
static int run(sink* s,const dp_link_ops* o,dp_link_t* l,int need){ memset(l,0,sizeof(*l)); l->o=o; l->c=s; l->need_kbs=need; return dp_link_train(l); }
int main(void){
    sink s; dp_link_t l;
    mk(&s,0x14,4); ck("caps: HBR2 x4, enhanced framing, TPS3",dp_link_caps(&(dp_link_t){.o=&sw_ops,.c=&s})==1);
    mk(&s,0x14,4); ck("pass: trains HBR2 x4 first time",run(&s,&sw_ops,&l,0)&&l.bw==0x14&&l.lanes==4&&l.tries==1&&l.ok);
    ck("pass: DPCD shows trained rate/lanes, pattern off, sink in D0",s.d[0x100]==0x14&&(s.d[0x101]&0x1F)==4&&(s.d[0x101]&0x80)&&s.d[0x102]==0&&s.d[0x600]==1);
    ck("pass: drive levels from sink adjust requests (vs 2, pe 1)",s.vs[0]==2&&s.pe[0]==1&&s.vs[3]==2);
    mk(&s,0x14,4); s.dead=1; waited=0; ck("fail: dead sink fails cleanly",!run(&s,&sw_ops,&l,0)&&!l.ok&&l.why);
    ck("fail: bounded (tries, steps, wait)",l.tries<=DP_LT_MAX_TRIES&&l.cr_loops<=10*DP_LT_MAX_TRIES&&waited<2000000);
    mk(&s,0x14,4); s.good_lanes=2; ck("lane reduction: 2 good lanes -> HBR2 x2",run(&s,&sw_ops,&l,0)&&l.lanes==2&&l.bw==0x14);
    mk(&s,0x14,4); s.good_lanes=1; ck("lane reduction: 1 good lane -> HBR2 x1",run(&s,&sw_ops,&l,0)&&l.lanes==1&&l.bw==0x14);
    mk(&s,0x1E,4); s.good_bw=0x0A; ck("rate reduction (CR): HBR3 sink stuck at HBR -> HBR x4",run(&s,&sw_ops,&l,0)&&l.lanes==4&&l.bw==0x0A&&l.tries==3);
    mk(&s,0x14,4); s.eq_bw=0x06; ck("rate reduction (EQ): equalises only at RBR -> RBR x4",run(&s,&sw_ops,&l,0)&&l.lanes==4&&l.bw==0x06);
    mk(&s,0x14,4); s.good_bw=0x06; memset(&l,0,sizeof(l)); l.o=&sw_ops; l.c=&s; l.src_bw=0x0A; l.src_lanes=2; ck("source limits clamp (HBR x2 max)",dp_link_train(&l)&&l.max_bw==0x0A&&l.max_lanes==2&&l.bw==0x06&&l.lanes==2);
    mk(&s,0x14,4); s.good_lanes=1; ck("bandwidth floor: 1080p60 needs 2 lanes, refuses x1",!run(&s,&sw_ops,&l,148500*3)&&l.why);
    mk(&s,0x14,4); s.aux_fail_at=9; ck("AUX failure mid-training stops, no retry storm",!run(&s,&sw_ops,&l,0)&&l.tries==1);
    mk(&s,0x00,0); ck("no sink: zero caps rejected",!run(&s,&sw_ops,&l,0));
    mk(&s,0x14,4); s.eq_pe=0; ck("firmware training path (GSP-RM style): HBR2 x4",run(&s,&hw_ops,&l,0)&&l.bw==0x14&&l.lanes==4&&s.hw==1);
    mk(&s,0x14,4); s.eq_pe=0; s.good_lanes=2; s.good_bw=0x0A; ck("firmware training path falls back to HBR x2",run(&s,&hw_ops,&l,0)&&l.bw==0x0A&&l.lanes==2);
    mk(&s,0x14,4); s.d[0x100]=0x14; s.d[0x101]=0x84; s.d[0x202]=0x77; s.d[0x203]=0x77; s.d[0x204]=1; memset(&l,0,sizeof(l)); l.o=&sw_ops; l.c=&s; l.need_kbs=148500*3;
    ck("keep link: firmware-trained HBR2 x4 is reused, nothing written to the sink",dp_link_inherit(&l)&&l.kept&&l.ok&&l.bw==0x14&&l.lanes==4&&l.enh&&s.wrn==0);
    ck("keep link: status check on a healthy link does not retrain",dp_link_recover(&l,3,100000)==1&&l.retrains==0&&l.losses==0&&s.wrn==0);
    mk(&s,0x14,4); s.d[0x100]=0x06; s.d[0x101]=0x81; s.d[0x202]=0x07; s.d[0x204]=1; memset(&l,0,sizeof(l)); l.o=&sw_ops; l.c=&s; l.need_kbs=148500*3;
    ck("keep link: a firmware link too slow for the native mode is not reused",!dp_link_inherit(&l)&&!l.kept&&strstr(l.why,"too slow")&&s.wrn==0);
    mk(&s,0x14,4); s.d[0x100]=0x14; s.d[0x101]=0x84; s.d[0x202]=0x11; s.d[0x204]=0; memset(&l,0,sizeof(l)); l.o=&sw_ops; l.c=&s;
    ck("keep link: a lost firmware link is reported, not kept",!dp_link_inherit(&l)&&!l.ok&&strstr(l.why,"lost"));
    ck("HPD/link loss: retrained once at full rate",dp_link_recover(&l,3,100000)==1&&l.losses==2&&l.retrains==1&&l.bw==0x14&&l.lanes==4&&s.d[0x102]==0);
    s.d[0x202]=0; s.d[0x203]=0; s.d[0x204]=0; s.good_lanes=2;
    ck("HPD/link loss with 2 lanes left: retrain falls back to x2",dp_link_recover(&l,3,100000)==1&&l.losses==3&&l.lanes==2);
    s.d[0x202]=0; s.aux_fail_at=1; s.ops=0; waited=0; int rr=l.retrains;
    ck("HPD: sink unplugged (AUX dead) - bounded retries then gives up",dp_link_recover(&l,9,100000)==0&&l.retrains-rr<=DP_HPD_MAX_TRIES&&waited<=3*100000&&l.why);
    mk(&s,0x14,4); memset(&l,0,sizeof(l)); l.o=&sw_ops; l.c=&s;
    ck("keep link: sink without a trained configuration is not kept",!dp_link_inherit(&l)&&!l.lanes&&l.why);
    printf("%s (%d checks)\n",fails?"FAILED":"ALL PASS",checks); return fails?1:0; }
