#include "include/nvx/drivers/usb/ucsi.h"

static u32 uc_le32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static u32 uc_cci(const ucsi_ops* o){ u8 b[4]={0,0,0,0}; if(o->rd(o->c,UCSI_CCI,b,4)!=0)return 0; return uc_le32(b); }
static int uc_send(const ucsi_ops* o,u64 ctl){ u8 b[8]; for(int i=0;i<8;i++)b[i]=(u8)(ctl>>(8*i)); if(o->wr(o->c,UCSI_CONTROL,b,8)!=0)return -1; return o->dsm(o->c,1); }
static int uc_wait(ucsi_t* u,const ucsi_ops* o,u32 want,u32* cci){
    u32 lim=o->poll_ms?o->poll_ms:1000;
    for(u32 t=0;t<=lim;t++){ if(o->dsm(o->c,2)!=0)return -1; u32 c=uc_cci(o); u->lastcci=c;
        if((c&want)&&!(c&UCSI_CCI_BUSY)){ *cci=c; return 0; }
        if((c&(UCSI_CCI_ERR|UCSI_CCI_NOTSUP))&&!(c&UCSI_CCI_BUSY)&&want!=UCSI_CCI_RESET){ *cci=c; return 0; }
        if(o->delay_ms)o->delay_ms(o->c,1); }
    u->timeouts++; return -2; }
static int uc_ack(ucsi_t* u,const ucsi_ops* o,u32 cci){
    u64 a=UCSI_ACK_CC_CI|(1ull<<17)|(UCSI_CCI_CHG(cci)?(1ull<<16):0); if(uc_send(o,a)!=0)return -1;
    u32 c=0; return uc_wait(u,o,UCSI_CCI_ACK,&c); }
int ucsi_core_cmd(ucsi_t* u,const ucsi_ops* o,u64 ctl,u8* in,int inmax){
    u->cmds++; if(uc_send(o,ctl)!=0){ u->errs++; return -1; }
    u32 cci=0; int r=uc_wait(u,o,(ctl&0xFF)==UCSI_PPM_RESET?UCSI_CCI_RESET:UCSI_CCI_DONE,&cci); if(r)return r;
    if((ctl&0xFF)==UCSI_PPM_RESET)return 0;
    if(cci&(UCSI_CCI_ERR|UCSI_CCI_NOTSUP)){ u->errs++; uc_ack(u,o,cci); return (cci&UCSI_CCI_NOTSUP)?-3:-4; }
    int n=(int)UCSI_CCI_LEN(cci); if(n>u->msgin)n=u->msgin; if(n>inmax)n=inmax; if(n<0)n=0;
    if(n&&in&&o->rd(o->c,UCSI_MSGIN,in,(u32)n)!=0)n=0;
    if(uc_ack(u,o,cci)!=0)return -2;
    return n; }
static void uc_why(ucsi_t* u,const char* s){ int i=0; while(s[i]&&i<47){ u->why[i]=s[i]; i++; } u->why[i]=0; }
int ucsi_core_init(ucsi_t* u,const ucsi_ops* o){
    u8* z=(u8*)u; for(u32 i=0;i<sizeof *u;i++)z[i]=0;
    u8 v[2]={0,0}; if(o->rd(o->c,UCSI_VERSION,v,2)!=0){ uc_why(u,"mailbox not readable"); u->state=UCSI_ST_FAILED; return -1; }
    u->ver=(u16)(v[0]|(v[1]<<8)); if(!u->ver||u->ver==0xFFFF||u->ver>0x0400){ uc_why(u,"no UCSI version in the mailbox"); u->state=UCSI_ST_FAILED; return -1; }
    u->msgin=u->ver>=0x0200?32:16;
    if(ucsi_core_cmd(u,o,UCSI_PPM_RESET,0,0)<0){ uc_why(u,"PPM reset not answered"); u->state=UCSI_ST_FAILED; return -1; }
    u8 b[32]; for(int i=0;i<32;i++)b[i]=0;
    int n=ucsi_core_cmd(u,o,UCSI_GET_CAPABILITY,b,16); if(n<9){ uc_why(u,"GET_CAPABILITY failed"); u->state=UCSI_ST_FAILED; return -1; }
    u->attr=uc_le32(b); u->nconn=b[4]; u->nalt=b[8]; u->pdver=(u16)(b[12]|(b[13]<<8)); u->tcver=(u16)(b[14]|(b[15]<<8));
    if(!u->nconn){ uc_why(u,"no USB-C connectors reported"); u->state=UCSI_ST_FAILED; return -1; }
    if(u->nconn>UCSI_MAXC)u->nconn=UCSI_MAXC;
    for(int i=0;i<u->nconn;i++){ ucsi_conn* c=&u->c[i]; for(int k=0;k<4;k++)b[k]=0;
        if(ucsi_core_cmd(u,o,UCSI_GET_CONNECTOR_CAPABILITY|((u64)(i+1)<<16),b,16)>=2){ c->altcap=(b[0]>>6)&1; c->provider=b[1]&1; c->consumer=(b[1]>>1)&1; } }
    u->state=UCSI_ST_OK; ucsi_core_refresh(u,o); return 0; }
static void uc_power(ucsi_conn* c){
    c->mv=0; c->ma=0; c->mw=0; if(!c->connected)return;
    if(c->opmode==3&&c->pdo){ u32 p=c->pdo, r=c->rdo, t=p>>30;
        if(t==0||t==2){ c->mv=((p>>10)&0x3FF)*50; c->ma=((r>>10)&0x3FF)*10; }
        else if(t==1){ c->mv=((p>>10)&0x3FF)*50; c->mw=((r>>10)&0x3FF)*250; if(c->mv)c->ma=c->mw*1000/c->mv; }
        else { c->mv=((r>>9)&0x7FF)*20; c->ma=(r&0x7F)*50; }
        if(!c->mw)c->mw=(u32)((u64)c->mv*c->ma/1000);
        return; }
    if(c->opmode==3){ c->ma=((c->rdo>>10)&0x3FF)*10; return; }
    c->mv=5000; c->ma=c->opmode==5?3000:c->opmode==4||c->opmode==2?1500:c->opmode==1?500:0; c->mw=c->mv*c->ma/1000; }
static int uc_altmode(ucsi_t* u,const ucsi_ops* o,int n,int rcp,int off,u16* svid,u32* mid){
    u8 b[16]; for(int i=0;i<16;i++)b[i]=0;
    int r=ucsi_core_cmd(u,o,UCSI_GET_ALTERNATE_MODES|((u64)rcp<<16)|((u64)n<<24)|((u64)off<<32),b,16); if(r<2)return -1;
    *svid=(u16)(b[0]|(b[1]<<8)); *mid=r>=6?uc_le32(b+2):0; return *svid?0:-1; }
int ucsi_core_conn(ucsi_t* u,const ucsi_ops* o,int n){
    if(u->state!=UCSI_ST_OK||n<1||n>u->nconn)return -1;
    ucsi_conn* c=&u->c[n-1];
    u8 b[32]; for(int i=0;i<32;i++)b[i]=0;
    int r=ucsi_core_cmd(u,o,UCSI_GET_CONNECTOR_STATUS|((u64)n<<16),b,u->msgin); c->ok=0; if(r<4)return -1;
    for(int i=0;i<20;i++)c->raw[i]=b[i];
    u16 f=(u16)(b[2]|(b[3]<<8)); c->change=(u16)(b[0]|(b[1]<<8)); c->opmode=f&7; c->connected=(f>>3)&1; c->source=(f>>4)&1; c->pflags=(u8)((f>>5)&0xFF); c->ptype=(u8)((f>>13)&7);
    c->rdo=r>=8?uc_le32(b+4):0; c->chg=r>=9?(b[8]&3):0; c->pdo=0; c->dp=0; c->tbt=0; c->nsvid=0; c->ncam=0; c->dp_vdo=0; c->ok=1;
    if(!c->connected){ c->ptype=0; c->pflags=0; c->rdo=0; uc_power(c); return 0; }
    u32 pos=(c->rdo>>28)&7;
    if(c->opmode==3&&pos){ for(int i=0;i<4;i++)b[i]=0;
        u64 ctl=UCSI_GET_PDOS|((u64)n<<16)|((u64)(c->source?0:1)<<23)|((u64)(pos-1)<<24)|(1ull<<34);
        if(ucsi_core_cmd(u,o,ctl,b,16)>=4)c->pdo=uc_le32(b); }
    uc_power(c);
    for(int k=0;k<4;k++){ u16 s; u32 m; if(uc_altmode(u,o,n,1,k,&s,&m))break; c->svid[c->nsvid++]=s; if(s==UCSI_SVID_DP)c->dp_vdo=m; }
    if(c->pflags&2){ for(int i=0;i<16;i++)b[i]=0; int m=ucsi_core_cmd(u,o,UCSI_GET_CURRENT_CAM|((u64)n<<16),b,16);
        for(int i=0;i<m&&c->ncam<4;i++){ if(b[i]==0xFF)continue; u16 s; u32 mid; if(uc_altmode(u,o,n,0,b[i],&s,&mid))continue; c->cam[c->ncam++]=s; if(s==UCSI_SVID_DP)c->dp=1; if(s==UCSI_SVID_TBT)c->tbt=1; } }
    return 0; }
int ucsi_core_refresh(ucsi_t* u,const ucsi_ops* o){ if(u->state!=UCSI_ST_OK)return -1; int ok=0; for(int i=1;i<=u->nconn;i++) if(!ucsi_core_conn(u,o,i))ok++; return ok; }

const char* ucsi_ptype_name(int t){ switch(t){ case 1: return "host or charger"; case 2: return "device"; case 3: return "powered cable"; case 4: return "device with powered cable"; case 5: return "debug accessory"; case 6: return "audio adapter"; default: return "unknown"; } }
typedef struct { char* p; char* e; } ucb;
static void ub_s(ucb* b,const char* s){ while(*s&&b->p<b->e)*b->p++=*s++; }
static void ub_u(ucb* b,u32 v){ char d[12]; int n=0; do{ d[n++]=(char)('0'+v%10); v/=10; }while(v); while(n&&b->p<b->e)*b->p++=d[--n]; }
static void ub_f(ucb* b,u32 milli,int dig){ ub_u(b,milli/1000); if(b->p<b->e)*b->p++='.'; u32 f=milli%1000; if(dig==1)ub_u(b,f/100); else { if(b->p<b->e)*b->p++=(char)('0'+f/100); if(b->p<b->e)*b->p++=(char)('0'+f/10%10); } }
int ucsi_conn_text(const ucsi_conn* c,char* t,int tn,char* d,int dn){
    ucb a={t,t+tn-1}, b={d,d+dn-1};
    if(!c->ok){ ub_s(&a,"No answer from the port"); *a.p=0; *b.p=0; return 0; }
    if(!c->connected){ ub_s(&a,"Nothing plugged in"); *a.p=0; ub_s(&b,c->provider&&c->consumer?"Charges and powers devices":c->consumer?"Can charge this computer":"Can power devices"); *b.p=0; return 0; }
    ub_s(&a,c->source?"Powering a ":"Charging from a "); if(c->source&&c->ptype==1)ub_s(&a,"connected computer"); else ub_s(&a,ucsi_ptype_name(c->ptype));
    if(c->dp)ub_s(&a,", DisplayPort"); else if(c->tbt)ub_s(&a,", Thunderbolt");
    *a.p=0;
    if(c->mv){ ub_f(&b,c->mv,1); ub_s(&b," V "); ub_f(&b,c->ma,2); ub_s(&b," A ("); ub_u(&b,c->mw/1000); ub_s(&b," W)"); }
    else if(c->ma){ ub_f(&b,c->ma,2); ub_s(&b," A"); }
    ub_s(&b,c->opmode==3?" USB PD":c->opmode==5?" USB-C 3 A":c->opmode==4?" USB-C 1.5 A":c->opmode==2?" battery charging spec":" default USB power");
    if(!c->source&&c->chg)ub_s(&b,c->chg==1?", charging":c->chg==2?", slow charging":", very slow charging");
    if(c->dp)ub_s(&b,", DP alt mode on"); else for(int i=0;i<c->nsvid;i++) if(c->svid[i]==UCSI_SVID_DP){ ub_s(&b,", DP capable, not entered"); break; }
    *b.p=0; return 1; }
