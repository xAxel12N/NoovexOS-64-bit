#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/graphics/ui/wm.h"
#include "net/bluetooth/bt_sec.h"
void smp_init(void);
int smp_event(const u8* e,int n);
char g_bt_secmsg[96];
u32 g_bt_passkey;
enum { SS_IDLE, SS_AUTH, SS_ENC };
typedef struct { btconn_t* c; u16 h; u8 st, rio, rauth, allowed, status; volatile u8 fin; } secc_t;
static secc_t sc[BT_MAX_CONN];
static u8 sec_win[6], sec_win_on;
static int sec_eq(const u8* a,const u8* b){ for(int i=0;i<6;i++) if(a[i]!=b[i])return 0; return 1; }
static void sec_cpy(u8* d,const u8* s,int n){ for(int i=0;i<n;i++)d[i]=s[i]; }
static u16 sec_g16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
static void sec_msg(const char* a,const u8* ad,const char* b,u32 v,int hexv){
    static const char h[]="0123456789ABCDEF"; char* p=g_bt_secmsg; p=scopy(p,a);
    if(ad){ *p++=' '; for(int i=5;i>=0;i--){ *p++=h[ad[i]>>4]; *p++=h[ad[i]&15]; if(i)*p++=':'; } }
    if(b){ p=scopy(p,b); } if(hexv){ p=scopy(p," 0x"); p=hx(p,v,2); } *p=0; klog2("bluetooth: ",g_bt_secmsg); }
const char* bt_sec_msg(void){ return g_bt_secmsg; }
u32 bt_sec_passkey(void){ return g_bt_passkey; }
static secc_t* sec_get(btconn_t* c,int mk){
    if(!c)return 0;
    for(int i=0;i<BT_MAX_CONN;i++) if(sc[i].c==c&&sc[i].h==c->handle)return &sc[i];
    if(!mk)return 0;
    for(int i=0;i<BT_MAX_CONN;i++){ secc_t* s=&sc[i]; if(!s->c||s->c->state==BTC_FREE||s->c->handle!=s->h){ u8* z=(u8*)s; for(int k=0;k<(int)sizeof *s;k++)z[k]=0; s->c=c; s->h=c->handle; s->rio=0xFF; c->sec=s; return s; } }
    return 0;
}
static secc_t* sec_by_addr(const u8* a){ return sec_get(bt_conn_by_addr(a,0),1); }
void bt_sec_pairing(const u8* a,int on){ if(a)sec_cpy(sec_win,a,6); sec_win_on=(u8)(on&&a); }
int bt_sec_pair_ok(const u8* a){ return sec_win_on&&sec_eq(sec_win,a); }
static int sec_local(const u8* a){ return bt_sec_pair_ok(a)&&!bt_keys_find(a,0); }
static int sec_allowed(const u8* a,secc_t* s){ (void)s; return sec_local(a); }
static int sec_kbd(const u8* a){ btconn_t* c=bt_conn_by_addr(a,0); u32 cod=c?c->cod:0; return ((cod>>8)&0x1F)==5&&(cod&0x40); }
static void sec_reply(u16 op,const u8* a){ bt_hci_send(op,a,6); }
static void sec_digits(char* d,u32 v){ for(int i=5;i>=0;i--){ d[i]=(char)('0'+v%10); v/=10; } d[6]=0; }
extern volatile unsigned char kdown[128];
__attribute__((weak)) int bt_prompt_ui(const btprompt_t* p);
static btprompt_t g_btp;
static u8 btp_prev[15];
static const u8 btp_sc[15]={0x15,0x1C,0x31,0x01,0x0E,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B};
const btprompt_t* bt_sec_prompt(void){ return &g_btp; }
int bt_sec_prompt_open(int kind,int le,const u8* a,u32 v,btp_cb cb,void* ctx){
    if(g_btp.on||!a||!cb||(kind!=BTP_CONFIRM&&kind!=BTP_ENTRY))return -1;
    u8* z=(u8*)&g_btp; for(int i=0;i<(int)sizeof g_btp;i++)z[i]=0;
    g_btp.kind=(u8)kind; g_btp.le=(u8)le; sec_cpy(g_btp.addr,a,6); g_btp.value=v%1000000u; g_btp.cb=cb; g_btp.ctx=ctx; g_btp.t0=hr_ns();
    for(int i=0;i<15;i++)btp_prev[i]=kdown[btp_sc[i]];
    g_btp.on=1; sec_msg(kind==BTP_CONFIRM?"confirm the pairing code for":"type the passkey shown by",a,0,0,0);
    notify("Bluetooth pairing",kind==BTP_CONFIRM?"Check the code on the device, then choose Yes or No":"Type the 6-digit passkey shown on the device");
    return 0; }
static void btp_clear(void){ volatile u8* z=(volatile u8*)&g_btp; for(int i=0;i<(int)sizeof g_btp;i++)z[i]=0; }
void bt_sec_prompt_drop(void* ctx,const u8* a){ if(g_btp.on&&((ctx&&g_btp.ctx==ctx)||(a&&!g_btp.le&&sec_eq(g_btp.addr,a))))btp_clear(); }
void bt_sec_answer(int yes){
    if(!g_btp.on)return;
    u32 v=g_btp.value; if(g_btp.kind==BTP_ENTRY){ if(yes&&g_btp.nd!=6)return; v=0; for(int i=0;i<g_btp.nd;i++)v=v*10+(u32)(g_btp.dig[i]-'0'); }
    btp_cb cb=g_btp.cb; void* ctx=g_btp.ctx; u8 a[6]; sec_cpy(a,g_btp.addr,6); btp_clear();
    cb(ctx,a,yes?1:0,v); v=0; }
int bt_sec_prompt_key(int k){
    if(!g_btp.on)return 0;
    if(k==0x15||k==0x1C){ bt_sec_answer(1); return 1; }
    if(k==0x31||k==0x01){ bt_sec_answer(0); return 1; }
    if(g_btp.kind!=BTP_ENTRY)return 0;
    if(k==0x0E){ if(g_btp.nd)g_btp.dig[--g_btp.nd]=0; return 1; }
    if(k>=0x02&&k<=0x0B&&g_btp.nd<6){ g_btp.dig[g_btp.nd++]=(char)(k==0x0B?'0':'1'+k-2); g_btp.dig[g_btp.nd]=0; return 1; }
    return 0; }
void bt_sec_ui(void){
    if(!g_btp.on)return;
    if(hr_ns()-g_btp.t0>BTP_TIMEOUT_NS){ sec_msg("pairing prompt timed out for",g_btp.addr,0,0,0); bt_sec_answer(0); return; }
    for(int i=0;i<15&&g_btp.on;i++){ u8 d=kdown[btp_sc[i]]; if(d&&!btp_prev[i])bt_sec_prompt_key(btp_sc[i]); btp_prev[i]=d; }
    if(g_btp.on&&bt_prompt_ui){ int r=bt_prompt_ui(&g_btp); if(r==1||r==2)bt_sec_answer(r==1); }
}
void bt_sec_tick(void){ if(g_btp.on&&hr_ns()-g_btp.t0>BTP_TIMEOUT_NS){ sec_msg("pairing prompt timed out for",g_btp.addr,0,0,0); bt_sec_answer(0); } }
static u8 btp_k33, btp_k34;
static void sec_ucb(void* ctx,const u8* a,int yes,u32 v){
    if(ctx==&btp_k33){ sec_reply(yes?0x042C:0x042D,a); sec_msg(yes?"pairing code confirmed for":"pairing code rejected for",a,0,0,0); return; }
    if(!yes){ sec_reply(0x042F,a); return; }
    u8 q[10]; sec_cpy(q,a,6); q[6]=(u8)v; q[7]=(u8)(v>>8); q[8]=(u8)(v>>16); q[9]=(u8)(v>>24); bt_hci_send(0x042E,q,10); for(int i=0;i<10;i++)((volatile u8*)q)[i]=0; }
static void sec_done(secc_t* s,int ok,u8 st){ s->status=st; s->st=SS_IDLE; s->fin=ok?2:0; }
static void sec_bredr(const u8* e,int n){
    int code=e[0], pl=e[1]; const u8* p=e+2; if(pl+2>n)return; secc_t* s;
    switch(code){
    case 0x17: if(pl<6)break; { btkey_t* k=bt_keys_find(p,0);
        if(k&&k->ktype!=3){ u8 q[22]; sec_cpy(q,p,6); sec_cpy(q+6,k->key,16); bt_hci_send(0x040B,q,22); for(int i=0;i<22;i++)((volatile u8*)q)[i]=0; }
        else sec_reply(0x040C,p); } break;
    case 0x18: if(pl<23)break;
        if(p[22]==3){ sec_msg("debug link key not stored for",p,0,0,0); break; }
        if(p[22]>8||p[22]==1||p[22]==2)break;
        { btkey_t* k=bt_keys_add(p,0,0); sec_cpy(k->key,p+6,16); k->ktype=p[22]; k->keysz=16; k->flags=(u8)((p[22]==5||p[22]==8)?BTKF_AUTH:0)|(p[22]>=7?BTKF_SC:0); bt_keys_save(); sec_msg("bonded with",p,0,0,0); }
        break;
    case 0x16: if(pl<6)break; { btconn_t* c=bt_conn_by_addr(p,0); u32 cod=c?c->cod:0;
        if(sec_local(p)&&((cod>>8)&0x1F)==5&&!(cod&0x40)){ u8 q[23]={0}; sec_cpy(q,p,6); q[6]=4; q[7]=q[8]=q[9]=q[10]='0'; bt_hci_send(0x040D,q,23); sec_msg("legacy PIN 0000 sent to",p,0,0,0); }
        else { sec_reply(0x040E,p); sec_msg("legacy PIN pairing refused for",p,0,0,0); } } break;
    case 0x31: if(pl<6)break; s=sec_by_addr(p);
        if(!sec_allowed(p,s)){ u8 q[7]; sec_cpy(q,p,6); q[6]=0x18; bt_hci_send(0x0434,q,7); sec_msg("pairing not allowed for",p,0,0,0); break; }
        { u8 q[9]; sec_cpy(q,p,6); q[6]=0x01; q[7]=0; int mitm=!(s&&s->rio==3); q[8]=(u8)(0x02|(mitm?1:0)); bt_hci_send(0x042B,q,9); } break;
    case 0x32: if(pl<9)break; s=sec_by_addr(p); if(s){ s->rio=p[6]; s->rauth=p[8]; } break;
    case 0x33: if(pl<10)break; { s=sec_by_addr(p); int rio=s?s->rio:0xFF; u32 v=(u32)p[6]|((u32)p[7]<<8)|((u32)p[8]<<16)|((u32)p[9]<<24);
        if(!sec_allowed(p,s)){ sec_reply(0x042D,p); break; }
        if(rio>3){ sec_reply(0x042D,p); sec_msg("pairing refused, no valid IO capability from",p,0,0,0); break; }
        if(rio==1){ if(bt_sec_prompt_open(BTP_CONFIRM,0,p,v,sec_ucb,&btp_k33)<0)sec_reply(0x042D,p); break; }
        (void)v; if(sec_kbd(p)){ sec_reply(0x042D,p); sec_msg("keyboard without passkey refused:",p,0,0,0); break; }
        sec_reply(0x042C,p); } break;
    case 0x34: if(pl<6)break; if(!sec_allowed(p,sec_by_addr(p))||bt_sec_prompt_open(BTP_ENTRY,0,p,0,sec_ucb,&btp_k34)<0)sec_reply(0x042F,p); break;
    case 0x3B: if(pl<10)break; { u32 v=((u32)p[6]|((u32)p[7]<<8)|((u32)p[8]<<16)|((u32)p[9]<<24))%1000000u; char b[96],*q=b,d[8]; sec_digits(d,v); g_bt_passkey=v;
        q=scopy(q,"Type "); q=scopy(q,d); q=scopy(q," on the Bluetooth keyboard, then press Enter"); *q=0; notify("Bluetooth pairing",b); sec_msg("passkey shown for",p,0,0,0); } break;
    case 0x36: if(pl<7)break; if(p[0])bt_sec_prompt_drop(0,p+1); if(p[0])sec_msg("Simple Pairing failed with",p+1,0,p[0],1); break;
    case 0x06: if(pl<3)break; { btconn_t* c=bt_conn_by_handle(sec_g16(p+1)&0xFFF); s=sec_get(c,0); if(!s||s->st!=SS_AUTH||c->le)break;
        if(p[0]){ sec_done(s,0,p[0]); break; }
        u8 q[3]={p[1],p[2],1}; s->st=SS_ENC; if(bt_hci_send(0x0413,q,3)<0)sec_done(s,0,0xFF); } break;
    case 0x08: case 0x59: if(pl<4)break; { btconn_t* c=bt_conn_by_handle(sec_g16(p+1)&0xFFF); s=sec_get(c,0); if(!s||s->st!=SS_ENC||c->le)break;
        if(p[0]||!p[3])sec_done(s,0,p[0]?p[0]:0xFE); else sec_done(s,1,0); } break;
    case 0x0F: if(pl<4||!p[0])break; { u16 op=sec_g16(p+2); if(op!=0x0413)break; for(int i=0;i<BT_MAX_CONN;i++) if(sc[i].c&&sc[i].st==SS_ENC){ sec_done(&sc[i],0,p[0]); break; } } break;
    case 0x05: if(pl<4)break; { u16 h=sec_g16(p+1)&0xFFF; btconn_t* c=bt_conn_by_handle(h); if(c&&!c->le)bt_sec_prompt_drop(0,c->addr); for(int i=0;i<BT_MAX_CONN;i++) if(sc[i].c&&sc[i].h==h&&sc[i].st!=SS_IDLE)sec_done(&sc[i],0,p[3]); } break;
    }
}
void bt_sec_init(void){ smp_init(); bt_keys_count(); }
int bt_sec_event(const u8* e,int n){
    if(n<2){ return 0; } smp_init();
    int r=smp_event(e,n); sec_bredr(e,n); return r;
}
void bt_sec_conn_down(btconn_t* c){ secc_t* s=sec_get(c,0); if(s&&s->st!=SS_IDLE)sec_done(s,0,0x08); }
static int sec_try(btconn_t* c,secc_t* s,int ms){
    u8 h[2]={(u8)c->handle,(u8)(c->handle>>8)}, rp[8];
    s->fin=1; s->status=0; s->st=SS_AUTH;
    int r=bt_hci_cmd(0x0411,h,2,rp,(int)sizeof rp,2000);
    if(r<1||rp[0]){ s->st=SS_IDLE; s->fin=0; s->status=r<1?0xFF:rp[0]; return 0; }
    return bt_conn_wait(c,&s->fin,2,ms);
}
static int sec_fin(btconn_t* c){
    c->auth=1; c->enc=1;
    u8 h[2]={(u8)c->handle,(u8)(c->handle>>8)}, rp[8]; int r=bt_hci_cmd(0x1408,h,2,rp,(int)sizeof rp,2000);
    c->keysz=r>=4&&!rp[0]?rp[3]:0;
    if(c->keysz<BT_BREDR_KEYSZ){ sec_msg("key too short, refused:",c->addr,0,c->keysz,1); c->enc=0; c->auth=0; bt_disconnect(c,0x05,2000); return 0; }
    btkey_t* k=bt_keys_find(c->addr,0); if(k&&(k->flags&BTKF_AUTH))c->auth=2;
    sec_msg("encrypted link with",c->addr,0,0,0);
    return 1;
}
static void sec_fail(btconn_t* c,secc_t* s){
    if(s->status==0x06)sec_msg("saved key rejected, run bt pair:",c->addr,0,0,0);
    if(g_btp.on&&!g_btp.le&&sec_eq(g_btp.addr,c->addr))bt_sec_answer(0);
    if(s->st!=SS_IDLE)sec_done(s,0,0xFD);
    sec_msg("authentication failed with",c->addr,", status",s->status,1);
}
int bt_sec_auth(btconn_t* c,int ms){
    if(!c||c->le||c->state!=BTC_UP)return -1;
    bt_sec_init(); if(ms<2000)ms=2000; if(ms>60000)ms=60000;
    secc_t* s=sec_get(c,1); if(!s)return -1;
    if(s->st!=SS_IDLE)return 0;
    if(!bt_keys_find(c->addr,0)&&!bt_sec_pair_ok(c->addr)){ sec_msg("not bonded, run bt pair:",c->addr,0,0,0); return 0; }
    int ok=c->enc?1:sec_try(c,s,ms);
    if(!ok){ sec_fail(c,s); return 0; }
    return sec_fin(c);
}
int bt_sec_auth_start(btconn_t* c){
    if(!c||c->le||c->state!=BTC_UP)return -1;
    bt_sec_init(); secc_t* s=sec_get(c,1); if(!s||s->st!=SS_IDLE)return -1;
    if(!bt_keys_find(c->addr,0)){ sec_msg("not bonded, run bt pair:",c->addr,0,0,0); return -1; }
    if(c->enc){ s->fin=2; return 0; }
    u8 h[2]={(u8)c->handle,(u8)(c->handle>>8)}, rp[8];
    s->fin=1; s->status=0; s->st=SS_AUTH;
    int r=bt_hci_cmd(0x0411,h,2,rp,(int)sizeof rp,500);
    if(r<1||rp[0]){ s->st=SS_IDLE; s->fin=0; s->status=r<1?0xFF:rp[0]; return -1; }
    return 0;
}
int bt_sec_auth_poll(btconn_t* c){
    secc_t* s=sec_get(c,0); if(!s)return -1;
    if(c->state!=BTC_UP){ if(s->st!=SS_IDLE)sec_done(s,0,0x08); return -1; }
    if(s->fin==1&&s->st!=SS_IDLE)return 0;
    if(s->fin!=2){ sec_fail(c,s); return -1; }
    s->fin=0; return sec_fin(c)?1:-1;
}
void bt_sec_auth_abort(btconn_t* c){ secc_t* s=sec_get(c,0); if(s&&s->st!=SS_IDLE){ s->status=0xFD; sec_fail(c,s); } }
