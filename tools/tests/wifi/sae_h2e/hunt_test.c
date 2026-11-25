#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lib/crypto/nvxcrypto.c"
#include "lib/crypto/nvxtls.c"
#include "lib/crypto/nvxcrypto2.c"
#include "lib/crypto/nvxx509.c"
#include "lib/crypto/nvxroots.c"
#include "lib/crypto/nvxwpa.c"
u8 g_ptk[48]; u8 g_pmk[32]; int g_wpa_ok=0, g_wpa_mfp=0, g_wifi_assoc_qos=0;
#include "net/wifi/wpa3.c"
#include "net/wifi/eap.c"
void* kmalloc(unsigned long n){ return malloc(n); }
void kfree(void* p){ free(p); }
void tls_prepare(void){}
i64 tls_clock_now(void){ return 0; }
int wpa_rsn_ie(u8* b){ int n=0; b[n++]=48; b[n++]=2; b[n++]=1; b[n++]=0; return n; }
static char lastlog[200];
void rtw_jlog(const char* a,const char* b,int v){ (void)v; snprintf(lastlog,sizeof lastlog,"%s%s",a,b); }
void wnd_setup(u32 m,const u8* bssid,const u8* mac,const u8* tk,int qos){ (void)m;(void)bssid;(void)mac;(void)tk;(void)qos; }
void wnd_set_gtk(const u8* g,int id){ (void)g;(void)id; }
int wnd_send_eapol(const u8* e,int n){ (void)e; return n; }
void ui_pump(void){}
void klog(const char* s){ (void)s; }
u64 hr_ns(void){ static u64 t; return t+=1000000; }
const struct wifi_hw_ops* wifi_hw_ops_get(void){ return 0; }
void* wifi_hw_ctx(void){ return 0; }
int wifi_hw_tx(const u8* f,int n){ (void)f;(void)n; return 0; }
void wifi_hw_poll(void){}
int wifi_hw_set_key(int i,int t,const u8* k,int l,int p){ (void)i;(void)t;(void)k;(void)l;(void)p; return 0; }
int wifi_hw_mac(u8* m){ memset(m,0,6); return 0; }
int wifi_hw_join(const u8* b,u8 c,const char* s,u8 a,u8 h){ (void)b;(void)c;(void)s;(void)a;(void)h; return 0; }
static const u8 SN[32]={0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f};
void test_rand(u8* b,int n){ for(int i=0;i<n;i++)b[i]=SN[i%32]; }
#define csprng_bytes test_rand
#include "net/wifi/wpa_sm.c"
#undef csprng_bytes
static int fails=0, passes=0;
static void ck(int c,const char* t){ if(c){ passes++; printf("PASS %s\n",t); } else { fails++; printf("FAIL %s\n",t); } }
static int ref_pwe(const u8* a1,const u8* a2,const char* pw,u8 pwe[65]){
    u8 key[12]; const u8 *hi=a1,*lo=a2; if(be_lt(a1,a2,6)){ hi=a2; lo=a1; } for(int i=0;i<6;i++){ key[i]=hi[i]; key[6+i]=lo[i]; }
    int pl=0; while(pw[pl]&&pl<128)pl++; u8 base[140]; for(int i=0;i<pl;i++)base[i]=(u8)pw[i];
    u8 prime[32]; p256_prime(prime); int found=0;
    for(int ctr=1;ctr<=40;ctr++){ base[pl]=(u8)ctr; u8 seed[32]; hmac_sha256(key,12,base,pl+1,seed);
        u8 val[32]; kdf_sha256(seed,32,"SAE Hunting and Pecking",prime,32,val,256);
        if(!be_lt(val,prime,32))continue; u8 y[32]; if(!p256_y_from_x(val,y))continue;
        if(!found){ int want=seed[31]&1; if((y[31]&1)!=want){ u8 pt[65]; pt[0]=4; memcpy(pt+1,val,32); memcpy(pt+33,y,32); p256_neg(pt); memcpy(y,pt+33,32); }
            pwe[0]=4; memcpy(pwe+1,val,32); memcpy(pwe+33,y,32); found=1; } }
    return found?0:-1; }
static const u8 AP[6]={0x02,0x11,0x22,0x33,0x44,0x55}, STA[6]={0x02,0xaa,0xbb,0xcc,0xdd,0xee};
static u8 q[8][400]; static int ql[8], qh=0, qn=0;
static void push(const u8* b,int n){ memcpy(q[qn&7],b,n); ql[qn&7]=n; qn++; }
static int hostile_n=0; static u8 hostile[400];
static int ap_tx(void* c,const u8* f,int len,int phase,int round){ (void)c;(void)f;(void)len;(void)phase;(void)round; if(hostile_n>0)push(hostile,hostile_n); return 1; }
static int ap_rx(void* c,u8 want,u8* body,int* blen,int ms){ (void)c;(void)want;(void)ms; if(qh>=qn)return 0; int s=qh&7; memcpy(body,q[s],ql[s]); *blen=ql[s]; qh++; return 1; }
static u16 seqv=0;
static const struct wpa_io sio={ 0,0,0,0,ap_tx,ap_rx,0,&seqv,0,0 };
static int ctx_zero(void){ const u8* z=(const u8*)&wsm_sc; for(unsigned i=0;i<sizeof wsm_sc;i++) if(z[i])return 0; return 1; }
static int run_hostile(int h2e,const u8* b,int n){ qh=qn=0; memcpy(hostile,b,n); hostile_n=n; int sae=0; wpa_sm_select(2,4,2|(h2e?16:0),3,0,&sae); wpa_sm_pmk("net","password1",sae,0); return wpa_sm_sae(&sio,AP,STA,"password1"); }
static int mkcommit(u8* r,int st,const u8* sc,const u8* el){ int n=0; r[n++]=3;r[n++]=0;r[n++]=1;r[n++]=0;r[n++]=(u8)st;r[n++]=0;r[n++]=19;r[n++]=0; memcpy(r+n,sc,32); n+=32; memcpy(r+n,el,64); n+=64; return n; }
static void kwrap(const u8* kek,const u8* p,int n,u8* o){ u8 rk[176]; aes128_expand(kek,rk); int nb=n/8; u8 A[8]; memset(A,0xA6,8); u8 Rr[64][8]; for(int i=0;i<nb;i++)memcpy(Rr[i],p+8*i,8);
    for(int j=0;j<6;j++)for(int i=0;i<nb;i++){ u8 B[16]; memcpy(B,A,8); memcpy(B+8,Rr[i],8); aes128_enc(rk,B,B); u64 t=(u64)(nb*j+i+1); memcpy(A,B,8); for(int k=0;k<8;k++)A[7-k]^=(u8)(t>>(8*k)); memcpy(Rr[i],B+8,8); }
    memcpy(o,A,8); for(int i=0;i<nb;i++)memcpy(o+8+8*i,Rr[i],8); }
static u8 ANONCE[32], apptk[48]; static u8 rxq[4][600]; static int rxl[4], rxh=0, rxn=0;
static int mkkey(u8* f,u16 ki,u64 rc,const u8* nonce,int rsnx){
    int n=0; f[n++]=1; f[n++]=3; n+=2; f[n++]=2; f[n++]=(u8)(ki>>8); f[n++]=(u8)ki; f[n++]=0; f[n++]=16;
    for(int i=7;i>=0;i--)f[n++]=(u8)(rc>>(8*i)); for(int i=0;i<32;i++)f[n++]=nonce[i]; for(int i=0;i<16+8+8+16;i++)f[n++]=0;
    if(ki&(1u<<12)){ u8 kd[64]; int k=0; kd[k++]=0xDD; kd[k++]=22; kd[k++]=0; kd[k++]=0x0F; kd[k++]=0xAC; kd[k++]=1; kd[k++]=1; kd[k++]=0; for(int i=0;i<16;i++)kd[k++]=(u8)(0x50+i);
        if(rsnx){ kd[k++]=244; kd[k++]=1; kd[k++]=(u8)rsnx; } kd[k++]=0xDD; kd[k++]=0; while(k%8)kd[k++]=0;
        f[n++]=0; f[n++]=(u8)(k+8); kwrap(apptk+16,kd,k,f+n); n+=k+8; } else { f[n++]=0; f[n++]=0; }
    f[2]=(u8)((n-4)>>8); f[3]=(u8)(n-4);
    if(ki&(1u<<8)){ u8 sv[48]; memcpy(sv,g_ptk,48); memcpy(g_ptk,apptk,48); u8 m[32]; wpa_mic(f,n,m); memcpy(f+81,m,16); memcpy(g_ptk,sv,48); }
    return n; }
static void etx(void* c,const u8* e,int n){ (void)c;(void)e;(void)n; }
static int erx(void* c,u8* o,int max,int ms){ (void)c;(void)ms; if(rxh>=rxn)return 0; int n=rxl[rxh]<max?rxl[rxh]:max; memcpy(o,rxq[rxh],n); rxh++; return n; }
static int ekey(void* c,int idx,int ty,const u8* key,int len,int pw){ (void)c;(void)idx;(void)ty;(void)key;(void)len;(void)pw; return 0; }
static u16 eseq; static struct wpa_io eio={ 0,etx,erx,ekey,0,0,0,&eseq,7,0 };
static int fourway(int bcn_akm,int m3_rsnx){
    int sae=0; wpa_sm_select(2,4,(u8)bcn_akm,3,0,&sae); g_wpa_akm=8; g_wpa_mfp=1;
    for(int i=0;i<32;i++){ ANONCE[i]=(u8)(0xA0+i); g_pmk[i]=(u8)(0x33+i); }
    wpa_derive_ptk_akm(g_pmk,AP,STA,ANONCE,SN,apptk); rxh=rxn=0; u8 f[600]; int n;
    n=mkkey(f,(u16)((1u<<3)|(1u<<7)),1,ANONCE,0); memcpy(rxq[rxn],f,n); rxl[rxn++]=n;
    n=mkkey(f,(u16)((1u<<3)|(1u<<6)|(1u<<7)|(1u<<8)|(1u<<9)|(1u<<12)),2,ANONCE,m3_rsnx); memcpy(rxq[rxn],f,n); rxl[rxn++]=n;
    return wpa_sm_4way(&eio,AP,STA); }
int main(void){
    srand(12345);
    { int same=1; for(int t=0;t<40;t++){ u8 a1[6],a2[6]; char pw[24]; for(int i=0;i<6;i++){ a1[i]=(u8)rand(); a2[i]=(u8)rand(); } int pl=1+rand()%20; for(int i=0;i<pl;i++)pw[i]=(char)('a'+rand()%26); pw[pl]=0;
        u8 x[65],y[65]; int r1=sae_derive_pwe(a1,a2,pw,x), r2=ref_pwe(a1,a2,pw,y); if(r1!=r2||(r1==0&&memcmp(x,y,65)))same=0; }
      ck(same,"constant-time hunting-and-pecking PWE equals the previous branching implementation (40 random cases)"); }
    sae_ctx A,B; memset(&A,0,sizeof A); memset(&B,0,sizeof B); sae_commit(&A,AP,STA,"pw"); sae_commit(&B,STA,AP,"pw");
    { sae_ctx T=A; u8 s1[32]={0}; s1[31]=1; ck(sae_process_commit(&T,s1,B.elem+1)<0,"peer scalar 1 rejected (802.11 12.4.5.4: 1 < scalar < r)"); }
    { sae_ctx T=A; u8 s0[32]={0}; ck(sae_process_commit(&T,s0,B.elem+1)<0,"peer scalar 0 rejected"); }
    { sae_ctx T=A; u8 n[32]; p256_order(n); ck(sae_process_commit(&T,n,B.elem+1)<0,"peer scalar r rejected"); }
    { sae_ctx T=A; u8 s2[32]={0}; s2[31]=2; ck(sae_process_commit(&T,s2,B.elem+1)==0,"peer scalar 2 accepted"); }
    { sae_ctx T=A; u8 e[64]; memcpy(e,B.elem+1,64); e[63]^=1; ck(sae_process_commit(&T,B.scalar,e)<0,"peer element off the curve rejected (invalid-curve attack)"); }
    { sae_ctx T=A; u8 e[64]; memset(e,0xFF,32); memcpy(e+32,B.elem+33,32); ck(sae_process_commit(&T,B.scalar,e)<0,"peer element x >= p rejected"); }
    { sae_ctx T=A; u8 e[64]; memset(e,0,64); ck(sae_process_commit(&T,B.scalar,e)<0,"peer element (0,0) rejected"); }
    { sae_ctx T=A; ck(sae_process_commit(&T,A.scalar,A.elem+1)==-2,"reflected commit rejected"); }
    { sae_ctx T=A; ck(sae_process_commit(&T,B.scalar,B.elem+1)==0,"valid peer commit accepted"); }
    { u8 r[400]; u8 s1[32]={0}; s1[31]=1; int n=mkcommit(r,0,s1,B.elem+1); ck(run_hostile(0,r,n)!=0&&ctx_zero(),"hostile AP commit with scalar 1 fails the join and the SAE context is wiped"); }
    { u8 r[400]; int n=mkcommit(r,0,B.scalar,B.elem+1); ck(run_hostile(0,r,n-1)!=0&&ctx_zero(),"truncated commit (103-byte body) fails, context wiped"); }
    { u8 r[400]; int n=mkcommit(r,0,B.scalar,B.elem+1); r[6]=20; ck(run_hostile(0,r,n)!=0,"commit for group 20 rejected"); }
    { u8 r[400]; int n=mkcommit(r,0,B.scalar,B.elem+1); ck(run_hostile(1,r,n)!=0,"H2E: peer commit with status 0 rejected"); }
    { u8 r[20]={3,0,1,0,76,0,19,0,255,200,93,1,2}; ck(run_hostile(1,r,13)==2,"H2E: Token Container longer than the frame rejected"); }
    { u8 r[20]={3,0,1,0,76,0,19,0,254,3,93,1,2}; ck(run_hostile(1,r,13)==2,"H2E: anti-clogging answer without a Token Container rejected"); }
    { u8 r[400]; memset(r,0x41,sizeof r); r[0]=3;r[1]=0;r[2]=1;r[3]=0;r[4]=76;r[5]=0;r[6]=19;r[7]=0; ck(run_hostile(0,r,399)==1,"HnP: 391-byte anti-clogging token clamped, three rounds then give up"); }
    { int bad=0; for(int t=0;t<300;t++){ u8 r[400]; int n=1+rand()%399; for(int i=0;i<n;i++)r[i]=(u8)rand(); if(n>=6){ r[0]=3; r[1]=0; r[2]=(u8)(1+(t&1)); } if(t%3==0&&n>=6){ r[4]=(u8)(t%2?126:76); r[5]=0; }
        int rc=run_hostile(t&1,r,n); if(rc==0||!ctx_zero())bad++; } ck(!bad,"fuzz: 300 random commit/confirm answers never authenticate and always wipe the context (run with ASan/UBSan)"); }
    ck(fourway(2|16,0x20)==0,"4-way: RSNXE H2E in msg 3 and in the beacon: connects");
    ck(fourway(2|16,0)==0,"4-way: beacon RSNXE, no RSNXE in msg 3: connects (only the downgrade direction is enforced)");
    ck(fourway(2,0)==0,"4-way: no RSNXE anywhere: connects");
    lastlog[0]=0; ck(fourway(2,0x20)==12&&strstr(lastlog,"downgrade"),"4-way: msg 3 RSNXE says H2E but the beacon did not: refused (H2E downgrade to hunting-and-pecking)");
    ck(fourway(2,0x01)==0,"4-way: msg 3 RSNXE without the H2E bit: connects");
    printf("%d passed, %d failed\n",passes,fails); if(!fails)printf("sae hunt: all ok\n"); return fails?1:0; }
