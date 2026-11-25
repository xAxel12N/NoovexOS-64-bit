#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#define UCSI_HOST
#include UCSI_SRC

typedef struct { u16 svid; u32 mid; } sim_am;
typedef struct { u16 flags; u32 rdo; u8 pwr; u32 src_pdo[4], ptn_src_pdo[4]; sim_am ptn[3]; int nptn; sim_am port[3]; int nport; int cam; u8 cap0, cap1; } sim_conn;
typedef struct { u8 m[600]; u16 ver; int nconn; sim_conn c[10]; int busy_reads, busy_left, never, no_reset, fail_cmd, cmds, dsm_calls, acks, pending_ack, bad_rd; u32 cci; } sim_t;
static sim_t S;
static void put32(u8* p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); }
static int s_rd(void* c,u32 off,void* b,u32 n){ (void)c; if(S.bad_rd)return -1; if(off+n>sizeof S.m)return -1; memcpy(b,S.m+off,n); return 0; }
static int s_wr(void* c,u32 off,const void* b,u32 n){ (void)c; if(off+n>sizeof S.m)return -1; memcpy(S.m+off,b,n); return 0; }
static void s_delay(void* c,u32 ms){ (void)c; (void)ms; }
static void reply(const u8* d,int n,u32 extra){ memset(S.m+16,0,256); if(d&&n)memcpy(S.m+16,d,(size_t)n); S.cci=UCSI_CCI_DONE|((u32)n<<8)|extra; }
static void exec(void){
    u64 ctl=0; for(int i=0;i<8;i++)ctl|=(u64)S.m[8+i]<<(8*i); int cmd=(int)(ctl&0xFF); S.cmds++;
    u8 b[32]; memset(b,0,sizeof b);
    if(S.fail_cmd==cmd){ reply(0,0,UCSI_CCI_ERR); return; }
    if(cmd==UCSI_PPM_RESET){ S.cci=S.no_reset?0:UCSI_CCI_RESET; return; }
    if(cmd==UCSI_ACK_CC_CI){ S.acks++; S.cci=UCSI_CCI_ACK; return; }
    if(cmd==UCSI_GET_CAPABILITY){ put32(b,0x44); b[4]=(u8)S.nconn; b[8]=2; b[12]=0x00; b[13]=0x03; b[14]=0x00; b[15]=0x02; reply(b,16,0); return; }
    int n=(int)((ctl>>16)&0x7F);
    if(cmd==UCSI_GET_ALTERNATE_MODES){ int rcp=(int)((ctl>>16)&7); n=(int)((ctl>>24)&0x7F); int off=(int)((ctl>>32)&0xFF); if(n<1||n>S.nconn){ reply(0,0,UCSI_CCI_ERR); return; } sim_conn* c=&S.c[n-1];
        sim_am* L=rcp==1?c->ptn:c->port; int ln=rcp==1?c->nptn:c->nport; if(off>=ln){ reply(0,0,0); return; } b[0]=(u8)L[off].svid; b[1]=(u8)(L[off].svid>>8); put32(b+2,L[off].mid); reply(b,6,0); return; }
    if(n<1||n>S.nconn){ reply(0,0,UCSI_CCI_ERR); return; } sim_conn* c=&S.c[n-1];
    if(cmd==UCSI_GET_CONNECTOR_CAPABILITY){ b[0]=c->cap0; b[1]=c->cap1; reply(b,2,0); return; }
    if(cmd==UCSI_GET_CONNECTOR_STATUS){ b[0]=1; b[2]=(u8)c->flags; b[3]=(u8)(c->flags>>8); put32(b+4,c->rdo); b[8]=c->pwr; reply(b,S.ver>=0x200?19:9,UCSI_CCI_DONE|(2u<<1)); return; }
    if(cmd==UCSI_GET_PDOS){ int ptn=(int)((ctl>>23)&1); int off=(int)((ctl>>24)&0xFF); u32* L=ptn?c->ptn_src_pdo:c->src_pdo; if(off>3){ reply(0,0,UCSI_CCI_ERR); return; } put32(b,L[off]); reply(b,4,0); return; }
    if(cmd==UCSI_GET_CURRENT_CAM){ if(c->cam<0){ b[0]=0xFF; reply(b,1,0); return; } b[0]=(u8)c->cam; reply(b,1,0); return; }
    reply(0,0,UCSI_CCI_NOTSUP); }
static int s_dsm(void* c,int fn){ (void)c; S.dsm_calls++; if(S.never)return 0;
    if(fn==1){ exec(); S.busy_left=S.busy_reads; put32(S.m+4,0); return 0; }
    if(fn==2){ if(S.busy_left>0){ S.busy_left--; put32(S.m+4,UCSI_CCI_BUSY); return 0; } put32(S.m+4,S.cci); return 0; }
    return -1; }
static ucsi_ops O={s_rd,s_wr,s_dsm,s_delay,0,50};
static int fails, checks;
#define CHECK(c,msg) do{ checks++; if(c)printf("  %s ok\n",msg); else { printf("  %s FAIL\n",msg); fails++; } }while(0)
static u32 fixed_pdo(u32 mv,u32 ma){ return ((mv/50)<<10)|(ma/10); }
static u32 rdo_fixed(int pos,u32 ma){ return ((u32)pos<<28)|((ma/10)<<10)|(ma/10); }
static void laptop(u16 ver){
    memset(&S,0,sizeof S); S.ver=ver; S.m[0]=(u8)ver; S.m[1]=(u8)(ver>>8); S.nconn=3;
    sim_conn* a=&S.c[0]; a->cap0=0x40; a->cap1=3; a->flags=(u16)(3|(1<<3)|(1<<5)|(1<<13)); a->ptn_src_pdo[0]=fixed_pdo(5000,3000); a->ptn_src_pdo[2]=fixed_pdo(20000,3250); a->rdo=rdo_fixed(3,3250); a->pwr=1; a->cam=-1;
    sim_conn* b=&S.c[1]; b->cap0=0x40; b->cap1=3; b->flags=(u16)(5|(1<<3)|(1<<4)|(3<<5)|(2<<13)); b->ptn[0].svid=0xFF01; b->ptn[0].mid=0x1C46; b->nptn=1; b->port[0].svid=0x8087; b->port[1].svid=0xFF01; b->port[1].mid=0x0C05; b->nport=2; b->cam=1;
    sim_conn* c=&S.c[2]; c->cap0=0; c->cap1=1; c->flags=0; c->cam=-1; }
int main(void){
    ucsi_t u; char t[96], d[128];
    printf("UCSI core against a simulated PPM mailbox\n");
    laptop(0x0110);
    CHECK(ucsi_core_init(&u,&O)==0&&u.state==UCSI_ST_OK,"v1.1 init: version, PPM reset, capability");
    CHECK(u.nconn==3&&u.pdver==0x0300&&u.tcver==0x0200,"capability fields decoded");
    CHECK(u.c[0].connected&&!u.c[0].source&&u.c[0].opmode==3&&u.c[0].ptype==1,"port 1: sink, PD, partner is a charger");
    CHECK(u.c[0].mv==20000&&u.c[0].ma==3250&&u.c[0].mw==65000,"port 1: PD contract 20 V 3.25 A 65 W from partner source PDO 3");
    CHECK(u.c[0].chg==1&&!u.c[0].dp,"port 1: nominal charging, no DP");
    ucsi_conn_text(&u.c[0],t,sizeof t,d,sizeof d); printf("    %s | %s\n",t,d);
    CHECK(strstr(t,"Charging from a host or charger")&&strstr(d,"20.0 V 3.25 A (65 W) USB PD, charging"),"port 1 text");
    CHECK(u.c[1].connected&&u.c[1].source&&u.c[1].opmode==5&&u.c[1].ptype==2,"port 2: source, USB-C 3 A, partner is a device");
    CHECK(u.c[1].dp&&!u.c[1].tbt&&u.c[1].ncam==1&&u.c[1].cam[0]==0xFF01,"port 2: current CAM resolves to DisplayPort");
    CHECK(u.c[1].nsvid==1&&u.c[1].svid[0]==0xFF01&&u.c[1].dp_vdo==0x1C46,"port 2: partner DP mode and its VDO");
    CHECK(u.c[1].mv==5000&&u.c[1].ma==3000,"port 2: Type-C current 5 V 3 A");
    ucsi_conn_text(&u.c[1],t,sizeof t,d,sizeof d); printf("    %s | %s\n",t,d);
    CHECK(strstr(t,"DisplayPort")&&strstr(d,"DP alt mode on"),"port 2 text says DP alt mode is on");
    CHECK(!u.c[2].connected&&u.c[2].mv==0&&u.c[2].ok,"port 3: empty");
    ucsi_conn_text(&u.c[2],t,sizeof t,d,sizeof d); CHECK(!strcmp(t,"Nothing plugged in")&&!strcmp(d,"Can power devices"),"port 3 text");
    CHECK(S.acks>=8&&u.timeouts==0&&u.errs==0,"every completed command was acknowledged, no timeouts");
    S.c[1].cam=0; ucsi_core_conn(&u,&O,2); CHECK(u.c[1].tbt&&!u.c[1].dp,"CAM 0 resolves to Thunderbolt");
    S.c[1].cam=-1; S.c[1].flags&=(u16)~(2<<5); ucsi_core_conn(&u,&O,2); CHECK(!u.c[1].dp&&!u.c[1].tbt&&u.c[1].ncam==0,"no alt-mode flag: no CAM query result");
    ucsi_conn_text(&u.c[1],t,sizeof t,d,sizeof d); CHECK(strstr(d,"DP capable, not entered")!=0,"DP capable partner without entered mode reported");
    S.c[0].flags&=(u16)~(1<<3); ucsi_core_conn(&u,&O,1); CHECK(!u.c[0].connected&&u.c[0].mv==0&&u.c[0].rdo==0,"unplug clears contract");
    laptop(0x0200); S.busy_reads=5;
    CHECK(ucsi_core_init(&u,&O)==0&&u.msgin==32&&u.c[0].mw==65000,"v2.0 init with busy PPM (5 busy reads per command)");
    laptop(0x0110); S.c[0].ptn_src_pdo[1]=(3u<<30)|1; S.c[0].rdo=(2u<<28)|((9000/20)<<9)|(2000/50); ucsi_core_init(&u,&O);
    CHECK(u.c[0].mv==9000&&u.c[0].ma==2000&&u.c[0].mw==18000,"PPS contract decoded from RDO (9.00 V 2.00 A)");
    laptop(0x0110); S.c[0].ptn_src_pdo[1]=(1u<<30)|((5000/50)<<10); S.c[0].rdo=(2u<<28)|((15000/250)<<10); ucsi_core_init(&u,&O);
    CHECK(u.c[0].mw==15000&&u.c[0].mv==5000&&u.c[0].ma==3000,"battery PDO contract decoded (15 W)");
    laptop(0x0110); S.never=1; int dc0=S.dsm_calls; CHECK(ucsi_core_init(&u,&O)<0&&u.state==UCSI_ST_FAILED&&u.timeouts==1,"PPM that never answers: bounded, fails closed");
    CHECK(S.dsm_calls-dc0<=60,"polling is bounded by poll_ms");
    laptop(0x0110); S.no_reset=1; CHECK(ucsi_core_init(&u,&O)<0&&!strcmp(u.why,"PPM reset not answered"),"no reset-complete: fails with reason");
    laptop(0x0110); S.ver=0; S.m[0]=0; S.m[1]=0; CHECK(ucsi_core_init(&u,&O)<0&&strstr(u.why,"version"),"zero version rejected before any command");
    laptop(0x0110); S.m[0]=0xFF; S.m[1]=0xFF; CHECK(ucsi_core_init(&u,&O)<0,"0xFFFF version (no memory) rejected");
    laptop(0x0110); S.bad_rd=1; CHECK(ucsi_core_init(&u,&O)<0,"unreadable mailbox rejected");
    laptop(0x0110); S.fail_cmd=UCSI_GET_CAPABILITY; CHECK(ucsi_core_init(&u,&O)<0&&u.errs==1,"error on GET_CAPABILITY fails init");
    laptop(0x0110); S.fail_cmd=UCSI_GET_PDOS; CHECK(ucsi_core_init(&u,&O)==0&&u.c[0].pdo==0&&u.c[0].ma==3250&&u.c[0].mv==0,"GET_PDOS error: current kept, voltage unknown");
    laptop(0x0110); S.nconn=9; for(int i=3;i<9;i++)S.c[i].cam=-1; CHECK(ucsi_core_init(&u,&O)==0&&u.nconn==UCSI_MAXC,"connector count clamped");
    laptop(0x0110); S.nconn=0; CHECK(ucsi_core_init(&u,&O)<0,"zero connectors rejected");
    laptop(0x0110); ucsi_core_init(&u,&O); CHECK(ucsi_core_conn(&u,&O,0)<0&&ucsi_core_conn(&u,&O,4)<0,"out-of-range connector refused");
    { ucsi_conn z; memset(&z,0,sizeof z); z.ok=1; z.connected=1; z.opmode=3; z.mv=20000; z.ma=5000; z.mw=100000; z.ptype=1; char tt[8], dd[12]; ucsi_conn_text(&z,tt,sizeof tt,dd,sizeof dd); CHECK(strlen(tt)==7&&strlen(dd)==11,"text output never overruns small buffers"); }
    for(int seed=1;seed<400;seed++){ laptop((u16)(seed&1?0x0110:0x0200)); unsigned x=(unsigned)seed*2654435761u;
        for(int i=0;i<3;i++){ x=x*1103515245u+12345u; S.c[i].flags=(u16)(x>>8); S.c[i].rdo=x*7u; S.c[i].cam=(int)((x>>3)&7)-2; S.c[i].nptn=(int)((x>>5)%4); S.c[i].nport=(int)((x>>7)%4); if(S.c[i].nptn>3)S.c[i].nptn=3; if(S.c[i].nport>3)S.c[i].nport=3; S.c[i].ptn_src_pdo[(x>>9)&3]=x; S.busy_reads=(int)((x>>11)&3); }
        ucsi_core_init(&u,&O); for(int i=0;i<u.nconn;i++)ucsi_conn_text(&u.c[i],t,sizeof t,d,sizeof d); }
    CHECK(1,"400 random connector states decode without overruns (sanitizers)");
    if(fails)printf("FAILURES %d of %d\n",fails,checks); else printf("ALL PASS (%d checks)\n",checks);
    return fails?1:0; }
