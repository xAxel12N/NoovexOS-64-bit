#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lib/crypto/nvxcrypto.c"
#include "lib/crypto/nvxtls.c"
#include "lib/crypto/nvxcrypto2.c"
#include "lib/crypto/nvxx509.c"
#include "lib/crypto/nvxroots.c"
void* kmalloc(unsigned long n){ return malloc(n); }
void kfree(void* p){ free(p); }
void tls_prepare(void){}
i64 tls_clock_now(void){ return 0; }
#include CMAC_SRC
#include "net/bluetooth/bt_sec.h"
u64 hr_ns(void){ return 0; }
void notify(const char* t,const char* b){ (void)t; (void)b; }
void klog2(const char* a,const char* b){ (void)a; (void)b; }
const u8* bt_hci_addr(void){ static u8 a[6]; return a; }
static u8 hq[28]; static int hqn;
int bt_hci_send(u16 op,const u8* p,int n){ if(op==0x2019&&n==28){ memcpy(hq,p,28); hqn++; } return 0; }
static u8 oq[8][80]; static int oql[8], oqn;
int l2cap_fixed_send(btconn_t* c,u16 cid,const u8* p,int n){ (void)c; (void)cid; if(oqn<8&&n<=80){ memcpy(oq[oqn],p,(size_t)n); oql[oqn++]=n; } return 0; }
static struct { int on, kind; u32 v; btp_cb cb; void* ctx; u8 a[6]; } tp;
int bt_sec_prompt_open(int kind,int le,const u8* a,u32 v,btp_cb cb,void* ctx){ (void)le; if(tp.on)return -1; tp.on=1; tp.kind=kind; tp.v=v%1000000u; tp.cb=cb; tp.ctx=ctx; memcpy(tp.a,a,6); return 0; }
void bt_sec_prompt_drop(void* ctx,const u8* a){ (void)a; if(tp.on&&tp.ctx==ctx)tp.on=0; }
static void tp_answer(int yes,u32 v){ tp.on=0; tp.cb(tp.ctx,tp.a,yes,v); }
int l2cap_fixed(u16 cid,bt_fixed_cb rx){ (void)cid; (void)rx; return 1; }
btconn_t* bt_conn_by_handle(u16 h){ (void)h; return 0; }
int bt_conn_wait(btconn_t* c,volatile u8* f,u8 w,int ms){ (void)c; (void)ms; return *f==w; }
int bt_mgr_accept(const u8* a,u32 cod,int le){ (void)a; (void)cod; (void)le; return 0; }
int bt_sec_pair_ok(const u8* a){ (void)a; return 1; }
btkey_t* bt_keys_find_le(const u8* a,int t){ (void)a; (void)t; return 0; }
static btkey_t tk_last; static int tk_adds;
btkey_t* bt_keys_add(const u8* a,int le,int t){ (void)a; (void)le; (void)t; tk_adds++; memset(&tk_last,0,sizeof tk_last); return &tk_last; }
void bt_keys_save(void){}
char g_bt_secmsg[96]; u32 g_bt_passkey;
char* scopy(char* p,const char* s){ while(*s)*p++=*s++; return p; }
char* hx(char* p,u32 v,int dg){ for(int i=dg-1;i>=0;i--)*p++="0123456789abcdef"[(v>>(i*4))&15]; return p; }
#include "net/bluetooth/smp.c"
static int fails;
static void hexr(const char* s,u8* o,int n){ u8 t[80]; for(int i=0;i<n;i++){ unsigned v; sscanf(s+2*i,"%2x",&v); t[i]=(u8)v; } for(int i=0;i<n;i++)o[i]=t[n-1-i]; }
static void chk(const char* name,const u8* got,const char* want,int n){ u8 w[80]; hexr(want,w,n); int ok=!memcmp(got,w,n); printf("%s %s\n",ok?"PASS":"FAIL",name); if(!ok){ for(int i=n-1;i>=0;i--)printf("%02x",got[i]); printf("\n"); fails++; } }
static int lfails, lchecks;
#define LCHK(c,m) do{ lchecks++; if(c)printf("PASS %s\n",m); else { lfails++; printf("FAIL %s (line %d)\n",m,__LINE__); } }while(0)
static btconn_t lc;
static u8 rb[32], rpk[64], rdh[32], rna[16], rnb[16], rmk[16], rltk[16];
static int pop(u8* o){ if(!oqn)return -1; int n=oql[0]; memcpy(o,oq[0],(size_t)n); for(int i=1;i<oqn;i++){ memcpy(oq[i-1],oq[i],80); oql[i-1]=oql[i]; } oqn--; return n; }
static void rx(const u8* p,int n){ u8 t[80]; memcpy(t,p,(size_t)n); smp_rx(&lc,t,n); }
static void resp_keys(const u8* pka){ u8 be[65],o[32]; for(int i=0;i<32;i++)rb[i]=(u8)(0x11*i+5); rb[0]&=0x7F; p256_mul(rb,0,be); sm_rev(rpk,be+1,32); sm_rev(rpk+32,be+33,32);
    be[0]=4; sm_rev(be+1,pka,32); sm_rev(be+33,pka+32,32); x509_p256_ecdh(rb,be,65,o); sm_rev(rdh,o,32); }
static int le_run(u8 rio,u8 rauth,int answer,u32 typed,u32 rpass,int* kind,u32* shown){
    u8 o[80]; int n; memset(&lc,0,sizeof lc); lc.state=BTC_UP; lc.le=1; lc.handle=0x41; lc.atype=1; for(int i=0;i<6;i++)lc.addr[i]=(u8)(0xC0+i); lc.addr[5]|=0xC0;
    memset(sx,0,sizeof sx); oqn=0; hqn=0; tp.on=0; tk_adds=0; *kind=0; *shown=0;
    smpc_t* s=sm_get(&lc,1); sm_start(s,1);
    n=pop(o); u8 preq[7]; memcpy(preq,o,7); if(n!=7||o[0]!=1)return -1; if(o[1]!=0x04)return -2;
    u8 pres[7]={0x02,rio,0,rauth,16,0,0}; rx(pres,7);
    n=pop(o); if(n!=65||o[0]!=0x0C)return -3; u8 pka[64]; memcpy(pka,o+1,64); resp_keys(pka);
    u8 q[65]; q[0]=0x0C; memcpy(q+1,rpk,64); rx(q,65);
    u8 a1[7],a2[7],ioa[3],iob[3],r[16]={0}; sm_addr(s,a1,a2); sm_io(preq,ioa); sm_io(pres,iob);
    if(s->meth==MPI||s->meth==MPK){
        if(s->meth==MPI){ if(!tp.on)return -4; *kind=tp.kind; if(!answer){ tp_answer(0,0); n=pop(o); return n==2&&o[0]==5&&o[1]==1?10:-5; } tp_answer(1,typed); }
        else { *shown=g_bt_passkey; rpass=g_bt_passkey; }
        for(int i=0;i<20;i++){ u8 ca[16],x[16],cb[17],nr[17]; n=pop(o); if(n!=17||o[0]!=3)return -6; memcpy(ca,o+1,16);
            for(int k=0;k<16;k++)rnb[k]=(u8)(i*7+k); cb[0]=3; smp_f4(rpk,pka,rnb,(u8)(0x80|((rpass>>i)&1)),cb+1); rx(cb,17);
            n=pop(o); if(n!=17||o[0]!=4)return -7; memcpy(rna,o+1,16); smp_f4(pka,rpk,rna,(u8)(0x80|((rpass>>i)&1)),x);
            if(memcmp(x,ca,16)){ return 20+i; }
            nr[0]=4; memcpy(nr+1,rnb,16); rx(nr,17); }
        r[0]=(u8)rpass; r[1]=(u8)(rpass>>8); r[2]=(u8)(rpass>>16);
    } else {
        for(int k=0;k<16;k++)rnb[k]=(u8)(0xA0+k); u8 cb[17]; cb[0]=3; smp_f4(rpk,pka,rnb,0,cb+1); rx(cb,17);
        n=pop(o); if(n!=17||o[0]!=4)return -8; memcpy(rna,o+1,16); u8 nr[17]; nr[0]=4; memcpy(nr+1,rnb,16); rx(nr,17);
        if(s->meth==MNC){ if(!tp.on)return -9; *kind=tp.kind; *shown=tp.v; if(tp.v!=smp_g2(pka,rpk,rna,rnb)%1000000u)return -10;
            if(!answer){ tp_answer(0,0); n=pop(o); return n==2&&o[0]==5&&o[1]==0x0C?11:-11; } tp_answer(1,0); }
    }
    n=pop(o); if(n!=17||o[0]!=0x0D)return -12;
    smp_f5(rdh,rna,rnb,a1,a2,rmk,rltk); u8 ea[16]; smp_f6(rmk,rna,rnb,r,ioa,a1,a2,ea); if(memcmp(ea,o+1,16))return -13;
    u8 eb[17]; eb[0]=0x0D; smp_f6(rmk,rnb,rna,r,iob,a2,a1,eb+1); rx(eb,17);
    if(hqn!=1||memcmp(hq+12,rltk,16))return -14;
    smp_enc_evt(&lc,0,1); if(tk_adds!=1||s->st!=SM_IDLE||s->fin!=2)return -15;
    return 0; }
static void le_tests(void){
    int kind; u32 shown; int r;
    r=le_run(0x01,0x0D,1,0,0,&kind,&shown); LCHK(r==0&&kind==BTP_CONFIRM&&(tk_last.flags&BTKF_AUTH)&&tk_last.ktype==8&&lc.auth,"LE SC numeric comparison (peer DisplayYesNo): g2 code prompted, Yes -> DHKey checks, LTK, authenticated bond");
    printf("     code shown %06u\n",shown);
    r=le_run(0x04,0x0D,0,0,0,&kind,&shown); LCHK(r==11&&kind==BTP_CONFIRM&&!tk_adds&&!hqn,"LE SC numeric comparison (peer KeyboardDisplay): No -> Pairing Failed 0x0C, no key");
    r=le_run(0x00,0x0D,1,424242,424242,&kind,&shown); LCHK(r==0&&kind==BTP_ENTRY&&(tk_last.flags&BTKF_AUTH),"LE SC passkey entry (peer DisplayOnly): typed passkey, 20 rounds, DHKey check with r=passkey, authenticated bond");
    r=le_run(0x00,0x0D,1,424243,424242,&kind,&shown); LCHK(r==20&&!hqn,"LE SC passkey entry: wrong passkey caught in round 0 by the responder");
    r=le_run(0x00,0x0D,0,0,0,&kind,&shown); LCHK(r==10&&!tk_adds,"LE SC passkey entry: Cancel -> Pairing Failed 0x01");
    r=le_run(0x02,0x0D,1,0,0,&kind,&shown); LCHK(r==0&&!kind&&shown<1000000u&&(tk_last.flags&BTKF_AUTH),"LE SC passkey display (peer KeyboardOnly): shown passkey verified over 20 rounds, authenticated bond");
    r=le_run(0x03,0x0D,1,0,0,&kind,&shown); LCHK(r==0&&!kind&&!(tk_last.flags&BTKF_AUTH)&&tk_last.ktype==7,"LE SC NoInputNoOutput peer -> Just Works, unauthenticated bond, no prompt");
    r=le_run(0x01,0x09,1,0,0,&kind,&shown); LCHK(r==0&&kind==BTP_CONFIRM,"LE SC peer DisplayYesNo without MITM flag -> still numeric comparison (we always request MITM)");
    { smpc_t t; static const struct { u8 rio, sc, m; } T[10]={{0,1,MPI},{1,1,MNC},{2,1,MPK},{3,1,MJW},{4,1,MNC},{0,0,MPI},{1,0,MPI},{2,0,MPK},{3,0,MJW},{4,0,MPK}}; int bad=0;
      for(int i=0;i<10;i++){ memset(&t,0,sizeof t); t.preq[1]=4; t.preq[3]=0x0D; t.pres[1]=T[i].rio; t.pres[3]=0x05; t.sc=T[i].sc; if(sm_method(&t)!=T[i].m){ bad++; printf("     io %d sc %d -> %d want %d\n",T[i].rio,T[i].sc,sm_method(&t),T[i].m); } }
      memset(&t,0,sizeof t); t.preq[1]=4; t.pres[1]=5; if(sm_method(&t)!=-1)bad++;
      LCHK(!bad,"method table for initiator KeyboardDisplay (Core v5.4 Vol 3 Part H 2.3.5.1) incl. invalid IO capability"); }
}
int main(void){
    u8 k[16]={0},r[16],preq[7],pres[7],ia[6],ra[6],o[16],o2[16];
    hexr("5783d52156ad6f0e6388274ec6702ee0",r,16); hexr("07071000000101",preq,7); hexr("05000800000302",pres,7); hexr("a1a2a3a4a5a6",ia,6); hexr("b1b2b3b4b5b6",ra,6);
    smp_c1(k,r,preq,pres,1,ia,0,ra,o); chk("c1",o,"1e1e3fef878988ead2a74dc5bef13b86",16);
    u8 r1[16],r2[16]; hexr("000f0e0d0c0b0a091122334455667788",r1,16); hexr("010203040506070899aabbccddeeff00",r2,16); smp_s1(k,r1,r2,o); chk("s1",o,"9a1fe1f0e8b0f49b5b4216ae796da062",16);
    u8 u[32],v[32],x[16],y[16],w[32],n1[16],n2[16],a1[7],a2[7],R[16],io[3];
    hexr("20b003d2f297be2c5e2c83a7e9f9a5b9eff49111acf4fddbcc0301480e359de6",u,32); hexr("55188b3d32f6bb9a900afcfbeed4e72a59cb9ac2f19d7cfb6b4fdd49f47fc5fd",v,32); hexr("d5cb8454d177733effffb2ec712baeab",x,16);
    smp_f4(u,v,x,0,o); chk("f4",o,"f2c916f107a9bd1cf1eda1bea974872d",16);
    hexr("ec0234a357c8ad05341010a60a397d9b99796b13b4f866f1868d34f373bfa698",w,32); hexr("d5cb8454d177733effffb2ec712baeab",n1,16); hexr("a6e8e7cc25a75f6e216583f7ff3dc4cf",n2,16);
    hexr("0056123737bfce",a1,7); hexr("00a713702dcfc1",a2,7);
    smp_f5(w,n1,n2,a1,a2,o,o2); chk("f5 mackey",o,"2965f176a1084a02fd3f6a20ce636e20",16); chk("f5 ltk",o2,"6986791169d7cd23980522b594750a38",16);
    hexr("12a3343bb453bb5408da42d20c2d0fc8",R,16); hexr("010102",io,3); smp_f6(o,n1,n2,R,io,a1,a2,o2); chk("f6",o2,"e3c473989cd0e8c5d26c0b09da958f61",16);
    hexr("a6e8e7cc25a75f6e216583f7ff3dc4cf",y,16); u32 g=smp_g2(u,v,x,y); printf("%s g2 %08x\n",g==0x2f9ed5baU?"PASS":"FAIL",g); if(g!=0x2f9ed5baU)fails++;
    u8 irk[16],pr[3]; hexr("ec0234a357c8ad05341010a60a397d9b",irk,16); hexr("708194",pr,3); u32 h=smp_ah(irk,pr); printf("%s ah %06x\n",h==0x0dfbaa?"PASS":"FAIL",h); if(h!=0x0dfbaa)fails++;
    u8 dpriv[32],be[65],pk[64]; { u8 t[32]; hexr("3f49f6d4a3c55f3874c9b3e3d2103f504aff607beb40b7995899b8a6cd3c1abd",t,32); sm_rev(dpriv,t,32); }
    if(p256_mul(dpriv,0,be)){ printf("FAIL p256\n"); fails++; } else { sm_rev(pk,be+1,32); sm_rev(pk+32,be+33,32); chk("debug pub X",pk,"20b003d2f297be2c5e2c83a7e9f9a5b9eff49111acf4fddbcc0301480e359de6",32); chk("debug pub Y",pk+32,"dc809c49652aeb6d63329abf5a52155c766345c28fed3024741c8ed01589d28b",32); int dbg=!sm_neq(pk,dbg_pkx,32); printf("%s debug key constant\n",dbg?"PASS":"FAIL"); if(!dbg)fails++; }
    u8 bad[65]; memcpy(bad,be,65); bad[64]^=1; u8 dh[32]; int rj=x509_p256_ecdh(dpriv,bad,65,dh); printf("%s off-curve key rejected\n",rj?"PASS":"FAIL"); if(!rj)fails++;
    le_tests(); fails+=lfails; printf("%d of %d LE pairing checks passed\n",lchecks-lfails,lchecks);
    printf(fails?"SMP CRYPTO TEST: %d FAIL\n":"SMP CRYPTO TEST: ALL PASS\n",fails); return fails!=0;
}
