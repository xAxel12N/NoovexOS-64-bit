#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/crypto/nvxtls.h"
#include "include/nvx/lib/crypto/nvxcrypto2.h"
#include "include/nvx/lib/crypto/nvxx509.h"
#include "include/nvx/net/wifi/wpa3.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/graphics/ui/wm.h"
#include "net/bluetooth/bt_sec.h"
enum { SM_IDLE, SM_RSP, SM_PK, SM_CONF, SM_RAND, SM_DHK, SM_ENC, SM_KEYS, SM_UWAIT };
enum { MJW, MNC, MPK, MPI };
#define SM_PKM(s) ((s)->meth==MPK||(s)->meth==MPI)
typedef struct { btconn_t* c; u16 h; u8 st, sc, meth, keysz, local, reenc, round, rdist, got, bond; volatile u8 fin;
    u8 preq[7], pres[7], tk[16], na[16], nb[16], cb[16], priv[32], pka[64], pkb[64], dh[32], mk[16], ltk[16]; u32 pass; u64 t0; btkey_t nk; u8 q[5][17], ql[5], nq; } smpc_t;
static smpc_t sx[BT_MAX_CONN];
static int smp_reg;
extern char g_bt_secmsg[96];
extern u32 g_bt_passkey;
#define smp_m g_bt_secmsg
#define smp_pk_shown g_bt_passkey
static void sm_rev(u8* d,const u8* s,int n){ for(int i=0;i<n;i++)d[i]=s[n-1-i]; }
static void sm_cpy(u8* d,const u8* s,int n){ for(int i=0;i<n;i++)d[i]=s[i]; }
static void sm_zero(void* p,int n){ volatile u8* z=(volatile u8*)p; for(int i=0;i<n;i++)z[i]=0; }
static int sm_neq(const u8* a,const u8* b,int n){ u8 d=0; for(int i=0;i<n;i++)d|=a[i]^b[i]; return d!=0; }
void smp_e(const u8 k[16],const u8 in[16],u8 out[16]){ u8 rk[176],kb[16],ib[16],ob[16]; sm_rev(kb,k,16); sm_rev(ib,in,16); aes128_expand(kb,rk); aes128_enc(rk,ib,ob); sm_rev(out,ob,16); sm_zero(rk,176); sm_zero(kb,16); }
static void sm_cmac(const u8 k[16],const u8* m,int n,u8 out[16]){ u8 kb[16],o[16]; sm_rev(kb,k,16); aes_cmac(kb,m,n,o); sm_rev(out,o,16); sm_zero(kb,16); }
void smp_c1(const u8 k[16],const u8 r[16],const u8 preq[7],const u8 pres[7],u8 iat,const u8 ia[6],u8 rat,const u8 ra[6],u8 out[16]){
    u8 p1[16],p2[16],t[16]; p1[0]=iat; p1[1]=rat; sm_cpy(p1+2,preq,7); sm_cpy(p1+9,pres,7);
    sm_cpy(p2,ra,6); sm_cpy(p2+6,ia,6); p2[12]=p2[13]=p2[14]=p2[15]=0;
    for(int i=0;i<16;i++){ t[i]=r[i]^p1[i]; } smp_e(k,t,t); for(int i=0;i<16;i++){ t[i]^=p2[i]; } smp_e(k,t,out); }
void smp_s1(const u8 k[16],const u8 r1[16],const u8 r2[16],u8 out[16]){ u8 t[16]; sm_cpy(t,r2,8); sm_cpy(t+8,r1,8); smp_e(k,t,out); }
void smp_f4(const u8 u[32],const u8 v[32],const u8 x[16],u8 z,u8 out[16]){ u8 m[65]; sm_rev(m,u,32); sm_rev(m+32,v,32); m[64]=z; sm_cmac(x,m,65,out); }
void smp_f5(const u8 w[32],const u8 n1[16],const u8 n2[16],const u8 a1[7],const u8 a2[7],u8 mackey[16],u8 ltk[16]){
    static const u8 salt[16]={0xBE,0x83,0x60,0x5A,0xDB,0x0B,0x37,0x60,0x38,0xA5,0xF5,0xAA,0x91,0x83,0x88,0x6C};
    u8 wb[32],t[16],m[53]; sm_rev(wb,w,32); sm_cmac(salt,wb,32,t);
    m[0]=0; m[1]=0x62; m[2]=0x74; m[3]=0x6C; m[4]=0x65; sm_rev(m+5,n1,16); sm_rev(m+21,n2,16); sm_rev(m+37,a1,7); sm_rev(m+44,a2,7); m[51]=1; m[52]=0;
    sm_cmac(t,m,53,mackey); m[0]=1; sm_cmac(t,m,53,ltk); sm_zero(wb,32); sm_zero(t,16); sm_zero(m,53); }
void smp_f6(const u8 w[16],const u8 n1[16],const u8 n2[16],const u8 r[16],const u8 io[3],const u8 a1[7],const u8 a2[7],u8 out[16]){
    u8 m[65]; sm_rev(m,n1,16); sm_rev(m+16,n2,16); sm_rev(m+32,r,16); sm_rev(m+48,io,3); sm_rev(m+51,a1,7); sm_rev(m+58,a2,7); sm_cmac(w,m,65,out); }
u32 smp_g2(const u8 u[32],const u8 v[32],const u8 x[16],const u8 y[16]){ u8 m[80],o[16]; sm_rev(m,u,32); sm_rev(m+32,v,32); sm_rev(m+64,y,16); sm_cmac(x,m,80,o); return (u32)o[0]|((u32)o[1]<<8)|((u32)o[2]<<16)|((u32)o[3]<<24); }
u32 smp_ah(const u8 k[16],const u8 r[3]){ u8 t[16]={0}; t[0]=r[0]; t[1]=r[1]; t[2]=r[2]; smp_e(k,t,t); return (u32)t[0]|((u32)t[1]<<8)|((u32)t[2]<<16); }
static const u8 dbg_pkx[32]={0xE6,0x9D,0x35,0x0E,0x48,0x01,0x03,0xCC,0xDB,0xFD,0xF4,0xAC,0x11,0x91,0xF4,0xEF,0xB9,0xA5,0xF9,0xE9,0xA7,0x83,0x2C,0x5E,0x2C,0xBE,0x97,0xF2,0xD2,0x03,0xB0,0x20};
static void sm_msg(const char* a,btconn_t* c,const char* b){ char* p=smp_m; p=scopy(p,a); if(c){ static const char h[]="0123456789ABCDEF"; *p++=' '; for(int i=5;i>=0;i--){ *p++=h[c->addr[i]>>4]; *p++=h[c->addr[i]&15]; if(i)*p++=':'; } } if(b)p=scopy(p,b); *p=0; klog2("bluetooth: ",smp_m); }
static smpc_t* sm_get(btconn_t* c,int mk){
    for(int i=0;i<BT_MAX_CONN;i++) if(sx[i].c==c&&sx[i].h==c->handle)return &sx[i];
    if(!mk)return 0;
    for(int i=0;i<BT_MAX_CONN;i++){ smpc_t* s=&sx[i]; if(!s->c||s->c->state==BTC_FREE||s->c->handle!=s->h){ sm_zero(s,(int)sizeof *s); s->c=c; s->h=c->handle; return s; } }
    return 0;
}
static void sm_send(smpc_t* s,const u8* p,int n){ l2cap_fixed_send(s->c,BT_CID_SMP,p,n); }
static void sm_end(smpc_t* s,int ok){ bt_sec_prompt_drop(s,0); sm_zero(s->priv,32); sm_zero(s->dh,32); sm_zero(s->tk,16); sm_zero(s->mk,16); sm_zero(s->ltk,16); s->st=SM_IDLE; s->fin=ok?2:0; }
static void sm_fail(smpc_t* s,u8 why,const char* m){ if(why){ u8 p[2]={0x05,why}; sm_send(s,p,2); } sm_msg("LE pairing failed with",s->c,m); sm_end(s,0); }
static void sm_rand(u8* p,int n){ csprng_bytes(p,n); }
static void sm_addr(smpc_t* s,u8 a[7],u8 b[7]){ const u8* o=bt_hci_addr(); sm_cpy(a,o,6); a[6]=0; sm_cpy(b,s->c->addr,6); b[6]=s->c->atype; }
static void sm_io(const u8* pr,u8 io[3]){ io[0]=pr[1]; io[1]=pr[2]; io[2]=pr[3]; }
static int sm_encrypt(smpc_t* s,const u8 ltk[16],u16 ediv,const u8 rnd[8]){
    u8 p[28]; p[0]=(u8)s->c->handle; p[1]=(u8)(s->c->handle>>8); sm_cpy(p+2,rnd,8); p[10]=(u8)ediv; p[11]=(u8)(ediv>>8); sm_cpy(p+12,ltk,16);
    int r=bt_hci_send(0x2019,p,28); sm_zero(p,28); s->st=SM_ENC; return r; }
static void sm_confirm_out(smpc_t* s){
    u8 p[17]; p[0]=0x03; sm_rand(s->na,16);
    if(s->sc)smp_f4(s->pka,s->pkb,s->na,(u8)(0x80|((s->pass>>s->round)&1)),p+1);
    else smp_c1(s->tk,s->na,s->preq,s->pres,0,bt_hci_addr(),s->c->atype,s->c->addr,p+1);
    sm_send(s,p,17); s->st=SM_CONF; }
static void sm_show_pass(smpc_t* s){
    char b[96],*p=b; p=scopy(p,"Type "); char d[8]; u32 v=s->pass; for(int i=5;i>=0;i--){ d[i]=(char)('0'+v%10); v/=10; } d[6]=0; p=scopy(p,d); p=scopy(p," on the Bluetooth keyboard, then press Enter"); *p=0;
    smp_pk_shown=s->pass; notify("Bluetooth pairing",b); sm_msg("passkey shown for",s->c,0); }
static void sm_start(smpc_t* s,int local){
    u8 p[7]={0x01,0x04,0x00,0x0D,0x10,0x00,0x07}; sm_cpy(s->preq,p,7); s->local=(u8)local; s->reenc=0; s->round=0; s->got=0; s->t0=hr_ns(); s->fin=1;
    sm_send(s,p,7); s->st=SM_RSP; }
static int sm_method(smpc_t* s){
    int mitm=(s->preq[3]|s->pres[3])&4, r=s->pres[1];
    if(r>4)return -1;
    if(!mitm||r==3)return MJW;
    if(r==2)return MPK;
    if(r==0)return MPI;
    return s->sc?MNC:(r==1?MPI:MPK); }
static int sm_pubkey(smpc_t* s){
    u8 be[65]; for(int t=0;t<8;t++){ sm_rand(s->priv,32); s->priv[0]&=0x7F; if(!p256_mul(s->priv,0,be))break; if(t==7)return -1; }
    sm_rev(s->pka,be+1,32); sm_rev(s->pka+32,be+33,32); sm_zero(be,65); return 0; }
static void sm_dhcheck(smpc_t* s){
    u8 a[7],b[7],io[3],r[16]={0},p[17]; sm_addr(s,a,b);
    if(SM_PKM(s)){ r[0]=(u8)s->pass; r[1]=(u8)(s->pass>>8); r[2]=(u8)(s->pass>>16); }
    smp_f5(s->dh,s->na,s->nb,a,b,s->mk,s->ltk); sm_io(s->preq,io); p[0]=0x0D; smp_f6(s->mk,s->na,s->nb,r,io,a,b,p+1); sm_send(s,p,17); s->st=SM_DHK; }
static void sm_store(smpc_t* s){
    btkey_t* nk=&s->nk; if(!s->bond){ sm_msg("LE paired (no bond) with",s->c,0); return; }
    const u8* ad=s->c->addr; int at=s->c->atype; if(s->got&2&&(nk->rsv[0]==0||(nk->addr[5]&0xC0)==0xC0)){ ad=nk->addr; at=nk->rsv[0]; }
    btkey_t* k=bt_keys_add(ad,1,at);
    k->keysz=s->keysz; k->flags=(u8)((s->sc?BTKF_SC:0)|(s->meth!=MJW?BTKF_AUTH:0));
    if(s->sc){ sm_cpy(k->key,s->ltk,16); k->flags|=BTKF_LTK; }
    else if((s->got&1)==1){ sm_cpy(k->key,nk->key,16); k->ediv=nk->ediv; sm_cpy(k->rand,nk->rand,8); k->flags|=BTKF_LTK; }
    if(s->got&2){ sm_cpy(k->irk,nk->irk,16); k->flags|=BTKF_IRK; }
    if(s->got&4){ sm_cpy(k->csrk,nk->csrk,16); k->flags|=BTKF_CSRK; }
    k->ktype=(u8)(s->sc?(s->meth!=MJW?8:7):(s->meth!=MJW?5:4));
    sm_zero(nk,(int)sizeof *nk); bt_keys_save(); sm_msg("LE bonded with",s->c,0); }
static void sm_ucb(void* ctx,const u8* a,int yes,u32 v){
    smpc_t* s=(smpc_t*)ctx; (void)a; if(s->st!=SM_UWAIT||!s->c||s->c->state!=BTC_UP||s->c->handle!=s->h)return;
    s->t0=hr_ns();
    if(!yes){ sm_fail(s,s->meth==MNC?0x0C:0x01,": cancelled on this computer"); return; }
    if(s->meth==MNC){ sm_dhcheck(s); return; }
    s->pass=v%1000000u; if(!s->sc){ sm_zero(s->tk,16); s->tk[0]=(u8)s->pass; s->tk[1]=(u8)(s->pass>>8); s->tk[2]=(u8)(s->pass>>16); }
    sm_confirm_out(s); }
static void sm_ask(smpc_t* s,int kind,u32 v){
    if(bt_sec_prompt_open(kind,1,s->c->addr,v,sm_ucb,s)<0){ sm_fail(s,kind==BTP_CONFIRM?0x0C:0x01,": another pairing prompt is open"); return; }
    s->st=SM_UWAIT; s->t0=hr_ns(); }
static void sm_keys_done(smpc_t* s){ if((s->got&s->rdist)==s->rdist){ sm_store(s); sm_end(s,1); } }
static void smp_rx(btconn_t* c,const u8* p,int n){
    static const u8 len[15]={0,7,7,17,17,2,17,11,17,8,17,2,65,17,2};
    if(n<1){ return; } u8 op=p[0];
    smpc_t* s=sm_get(c,op==0x0B);
    if(op>=15||n!=len[op]){ if(s&&s->st!=SM_IDLE)sm_fail(s,0x0A,": bad PDU"); else { u8 f[2]={0x05,op>=15?0x07:0x0A}; l2cap_fixed_send(c,BT_CID_SMP,f,2); } return; }
    if(!s){ if(op!=0x05){ u8 f[2]={0x05,0x08}; l2cap_fixed_send(c,BT_CID_SMP,f,2); } return; }
    if(op==0x05){ if(s->st!=SM_IDLE){ char b[24],*q=b; q=scopy(q,": reason 0x"); q=hx(q,p[1],2); *q=0; sm_fail(s,0,b); } return; }
    if(op==0x0E)return;
    switch(s->st){
    case SM_IDLE:
        if(op==0x0B){ if(c->enc)return; btkey_t* k=bt_keys_find_le(c->addr,c->atype);
            if(k&&(k->flags&BTKF_LTK)){ static const u8 z[8]; s->fin=1; s->reenc=1; s->keysz=k->keysz; s->t0=hr_ns(); s->meth=(u8)((k->flags&BTKF_AUTH)?MPK:MJW); sm_encrypt(s,k->key,k->ediv,(k->flags&BTKF_SC)?z:k->rand); return; }
            if(bt_sec_pair_ok(c->addr))sm_start(s,1); else { u8 f[2]={0x05,0x05}; sm_send(s,f,2); } return; }
        if(op==0x01){ u8 f[2]={0x05,0x07}; sm_send(s,f,2); }
        return;
    case SM_RSP:
        if(op!=0x02){ sm_fail(s,0x08,": unexpected PDU"); return; }
        sm_cpy(s->pres,p,7);
        if(p[4]<7||p[4]>16){ sm_fail(s,0x06,": key too short"); return; }
        s->keysz=p[4]; s->sc=(u8)((s->preq[3]&p[3]&8)?1:0); s->bond=(u8)((s->preq[3]&p[3]&1)?1:0);
        { int m=sm_method(s); if(m<0){ sm_fail(s,0x03,": invalid IO capability"); return; } s->meth=(u8)m; }
        s->rdist=(u8)(p[6]&s->preq[6]&(s->sc?6:7));
        if(s->meth==MPK){ u32 v; sm_rand((u8*)&v,4); s->pass=v%1000000u; sm_show_pass(s); }
        sm_zero(s->tk,16); if(s->meth==MPK&&!s->sc){ s->tk[0]=(u8)s->pass; s->tk[1]=(u8)(s->pass>>8); s->tk[2]=(u8)(s->pass>>16); }
        if(s->meth==MPI&&!s->sc){ sm_ask(s,BTP_ENTRY,0); return; }
        if(s->sc){ if(sm_pubkey(s)){ sm_fail(s,0x08,": key generation failed"); return; } u8 q[65]; q[0]=0x0C; sm_cpy(q+1,s->pka,64); sm_send(s,q,65); s->st=SM_PK; }
        else sm_confirm_out(s);
        return;
    case SM_PK: {
        if(op!=0x0C){ sm_fail(s,0x08,": unexpected PDU"); return; }
        sm_cpy(s->pkb,p+1,64);
        if(!sm_neq(s->pkb,s->pka,32)){ sm_fail(s,0x0A,": peer reflected our key"); return; }
        if(!sm_neq(s->pkb,dbg_pkx,32)){ sm_fail(s,0x08,": peer uses the debug key"); return; }
        u8 be[65],o[32]; be[0]=4; sm_rev(be+1,s->pkb,32); sm_rev(be+33,s->pkb+32,32);
        if(x509_p256_ecdh(s->priv,be,65,o)){ sm_fail(s,0x0A,": invalid public key"); return; }
        sm_rev(s->dh,o,32); sm_zero(o,32); sm_zero(s->priv,32);
        if(s->meth==MPK)sm_confirm_out(s); else if(s->meth==MPI)sm_ask(s,BTP_ENTRY,0); else s->st=SM_CONF;
        return; }
    case SM_CONF: {
        if(op!=0x03){ sm_fail(s,0x08,": unexpected PDU"); return; }
        sm_cpy(s->cb,p+1,16);
        if(s->sc&&!SM_PKM(s))sm_rand(s->na,16);
        u8 q[17]; q[0]=0x04; sm_cpy(q+1,s->na,16); sm_send(s,q,17); s->st=SM_RAND; return; }
    case SM_RAND: {
        if(op!=0x04){ sm_fail(s,0x08,": unexpected PDU"); return; }
        sm_cpy(s->nb,p+1,16); u8 x[16];
        if(!s->sc){ smp_c1(s->tk,s->nb,s->preq,s->pres,0,bt_hci_addr(),c->atype,c->addr,x);
            if(sm_neq(x,s->cb,16)){ sm_fail(s,0x04,": confirm value mismatch"); return; }
            u8 stk[16]; smp_s1(s->tk,s->nb,s->na,stk); for(int i=s->keysz;i<16;i++)stk[i]=0; static const u8 z[8]; sm_encrypt(s,stk,0,z); sm_zero(stk,16); return; }
        if(SM_PKM(s)){ smp_f4(s->pkb,s->pka,s->nb,(u8)(0x80|((s->pass>>s->round)&1)),x);
            if(sm_neq(x,s->cb,16)){ sm_fail(s,0x01,": passkey mismatch"); return; }
            if(++s->round<20){ sm_confirm_out(s); return; }
            sm_dhcheck(s); return; }
        smp_f4(s->pkb,s->pka,s->nb,0,x);
        if(sm_neq(x,s->cb,16)){ sm_fail(s,0x04,": confirm value mismatch"); return; }
        if(s->meth==MNC){ u32 v=smp_g2(s->pka,s->pkb,s->na,s->nb)%1000000u;
            if(!s->local){ sm_fail(s,0x0C,": numeric comparison needs a local pair request"); return; }
            sm_ask(s,BTP_CONFIRM,v); return; }
        sm_dhcheck(s); return; }
    case SM_DHK: {
        if(op!=0x0D){ sm_fail(s,0x08,": unexpected PDU"); return; }
        u8 a[7],b[7],io[3],r[16]={0},x[16]; sm_addr(s,a,b);
        if(SM_PKM(s)){ r[0]=(u8)s->pass; r[1]=(u8)(s->pass>>8); r[2]=(u8)(s->pass>>16); }
        sm_io(s->pres,io); smp_f6(s->mk,s->nb,s->na,r,io,b,a,x);
        if(sm_neq(x,p+1,16)){ sm_fail(s,0x0B,": DHKey check failed"); return; }
        u8 k[16]; sm_cpy(k,s->ltk,16); for(int i=s->keysz;i<16;i++)k[i]=0; sm_cpy(s->ltk,k,16); static const u8 z[8]; sm_encrypt(s,k,0,z); sm_zero(k,16); return; }
    case SM_UWAIT: sm_fail(s,0x08,": PDU while waiting for the user"); return;
    case SM_ENC: if(op>=0x06&&op<=0x0A&&!s->reenc&&s->nq<5){ sm_cpy(s->q[s->nq],p,n); s->ql[s->nq]=(u8)n; s->nq++; return; } sm_fail(s,0x08,": PDU before encryption"); return;
    case SM_KEYS: {
        btkey_t* k=&s->nk;
        if(op==0x06&&(s->rdist&1)&&!s->sc){ sm_cpy(k->key,p+1,16); s->got|=0x10; }
        else if(op==0x07&&(s->rdist&1)&&!s->sc&&(s->got&0x10)){ k->ediv=(u16)(p[1]|(p[2]<<8)); sm_cpy(k->rand,p+3,8); s->got|=1; }
        else if(op==0x08&&(s->rdist&2)){ sm_cpy(k->irk,p+1,16); s->got|=0x20; }
        else if(op==0x09&&(s->rdist&2)&&(s->got&0x20)){ k->rsv[0]=p[1]; sm_cpy(k->addr,p+2,6); s->got|=2; }
        else if(op==0x0A&&(s->rdist&4)){ sm_cpy(k->csrk,p+1,16); s->got|=4; }
        else { sm_fail(s,0x08,": unexpected key PDU"); return; }
        sm_keys_done(s); return; }
    }
}
static void smp_enc_evt(btconn_t* c,u8 st,u8 on){
    smpc_t* s=sm_get(c,0); if(!s||s->st!=SM_ENC)return;
    if(st||!on){ if(s->reenc&&st==0x06){ sm_fail(s,0,": stored key rejected, run bt pair"); return; }
        char b[32],*q=b; q=scopy(q,": encryption status 0x"); q=hx(q,st,2); *q=0; sm_fail(s,0,b); return; }
    c->keysz=s->keysz; if(s->meth!=MJW)c->auth=1;
    if(s->reenc){ sm_msg("LE encrypted with stored key for",c,0); sm_end(s,1); return; }
    s->st=SM_KEYS; if(!s->rdist){ sm_store(s); sm_end(s,1); return; }
    u8 nq=s->nq; s->nq=0; for(int i=0;i<nq&&s->st==SM_KEYS;i++){ u8 t[17]; sm_cpy(t,s->q[i],17); smp_rx(c,t,s->ql[i]); }
}
int smp_event(const u8* e,int n){
    if(n<3){ return 0; } const u8* p=e+2; int pl=e[1]; if(pl+2>n)return 0;
    for(int i=0;i<BT_MAX_CONN;i++) if(sx[i].c&&sx[i].st!=SM_IDLE&&hr_ns()-sx[i].t0>30000000000ULL&&sx[i].c->state==BTC_UP&&sx[i].c->handle==sx[i].h)sm_fail(&sx[i],0x08,": SMP timeout");
    if(e[0]==0x08&&pl>=4){ btconn_t* c=bt_conn_by_handle((u16)((p[1]|(p[2]<<8))&0xFFF)); if(c&&c->le)smp_enc_evt(c,p[0],p[3]); return 0; }
    if(e[0]==0x30&&pl>=3){ btconn_t* c=bt_conn_by_handle((u16)((p[1]|(p[2]<<8))&0xFFF)); if(c&&c->le)smp_enc_evt(c,p[0],1); return 0; }
    if(e[0]==0x0F&&pl>=4&&p[0]&&(p[2]|(p[3]<<8))==0x2019){ for(int i=0;i<BT_MAX_CONN;i++) if(sx[i].c&&sx[i].st==SM_ENC&&sx[i].c->state==BTC_UP){ sx[i].reenc=0; sm_fail(&sx[i],0,": LE Enable Encryption refused"); break; } return 0; }
    if(e[0]==0x3E&&pl>=3&&p[0]==0x05){ u8 q[2]={p[1],p[2]}; bt_hci_send(0x201B,q,2); return 1; }
    if(e[0]==0x05&&pl>=4){ u16 h=(u16)((p[1]|(p[2]<<8))&0xFFF); for(int i=0;i<BT_MAX_CONN;i++) if(sx[i].c&&sx[i].h==h&&sx[i].st!=SM_IDLE){ bt_sec_prompt_drop(&sx[i],0); sm_zero(sx[i].priv,32); sm_zero(sx[i].dh,32); sx[i].st=SM_IDLE; sx[i].fin=0; } return 0; }
    return 0;
}
void smp_init(void){ if(smp_reg)return; smp_reg=l2cap_fixed(BT_CID_SMP,smp_rx)?1:0; }
int bt_smp_pair(btconn_t* c,int ms){
    if(!c||!c->le||c->state!=BTC_UP)return -1;
    smp_init(); if(ms<2000)ms=2000; if(ms>60000)ms=60000;
    smpc_t* s=sm_get(c,1); if(!s)return -1;
    if(s->st==SM_IDLE){
        if(c->enc)return 1;
        btkey_t* k=bt_keys_find_le(c->addr,c->atype);
        if(k&&(k->flags&BTKF_LTK)){ static const u8 z[8]; s->fin=1; s->reenc=1; s->local=1; s->keysz=k->keysz; s->t0=hr_ns(); s->meth=(u8)((k->flags&BTKF_AUTH)?MPK:MJW); sm_encrypt(s,k->key,k->ediv,(k->flags&BTKF_SC)?z:k->rand); }
        else if(bt_sec_pair_ok(c->addr))sm_start(s,1);
        else { sm_msg("LE device not bonded, run bt pair:",c,0); return 0; }
    } else s->local=1;
    int ok=bt_conn_wait(c,&s->fin,2,ms);
    if(!ok&&s->st!=SM_IDLE){ sm_msg("LE pairing timed out with",c,0); u8 f[2]={0x05,0x08}; if(s->st!=SM_ENC)sm_send(s,f,2); sm_end(s,0); }
    return ok?1:0;
}
int bt_smp_start(btconn_t* c){
    if(!c||!c->le||c->state!=BTC_UP)return -1;
    smp_init(); smpc_t* s=sm_get(c,1); if(!s||s->st!=SM_IDLE)return -1;
    if(c->enc)return 1;
    btkey_t* k=bt_keys_find_le(c->addr,c->atype);
    if(!k||!(k->flags&BTKF_LTK)){ sm_msg("LE device not bonded, run bt pair:",c,0); return -1; }
    static const u8 z[8]; s->fin=1; s->reenc=1; s->local=1; s->keysz=k->keysz; s->t0=hr_ns(); s->meth=(u8)((k->flags&BTKF_AUTH)?MPK:MJW); sm_encrypt(s,k->key,k->ediv,(k->flags&BTKF_SC)?z:k->rand);
    return s->fin==2?1:s->fin==1?0:-1;
}
int bt_smp_poll(btconn_t* c){
    smpc_t* s=sm_get(c,0); if(!s||c->state!=BTC_UP)return -1;
    if(s->fin==2)return 1;
    return s->fin==1&&s->st!=SM_IDLE?0:-1;
}
void bt_smp_abort(btconn_t* c){ smpc_t* s=sm_get(c,0); if(s&&s->st!=SM_IDLE){ sm_msg("LE encryption timed out with",c,0); sm_end(s,0); } }
