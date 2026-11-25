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
int wpa_rsn_ie(u8* b){ int n=0; b[n++]=48; b[n++]=20; b[n++]=1; b[n++]=0; b[n++]=0;b[n++]=0x0F;b[n++]=0xAC;b[n++]=4; b[n++]=1;b[n++]=0; b[n++]=0;b[n++]=0x0F;b[n++]=0xAC;b[n++]=4; b[n++]=1;b[n++]=0; b[n++]=0;b[n++]=0x0F;b[n++]=0xAC;b[n++]=(u8)g_wpa_akm; b[n++]=(u8)(g_wpa_mfp?0x80:0); b[n++]=0; return n; }
static u8 rec[2][8192]; static int recn[2], cu=0;
static void R(u8 tag,const u8* d,int n){ u8* r=rec[cu]; int* k=&recn[cu]; if(*k+n+3>8192)return; r[(*k)++]=tag; r[(*k)++]=(u8)(n>>8); r[(*k)++]=(u8)n; if(n)memcpy(r+*k,d,n); *k+=n; }
static u8 rxq[8][600]; static int rxl[8], rxh=0, rxn=0;
static int rx_pop(u8* o,int max){ if(rxh>=rxn)return 0; int n=rxl[rxh]; if(n>max)n=max; memcpy(o,rxq[rxh],n); rxh++; return n; }
static const u8 SN[32]={0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f};
void test_rand(u8* b,int n){ for(int i=0;i<n;i++)b[i]=SN[i%32]; }
void old_rtw_tx_data_eapol(u32 m,const u8* bssid,const u8* mac,const u8* e,int n){ (void)m;(void)bssid;(void)mac; R('T',e,n); }
int old_rtw_rx_wait_eapol(u32 m,const u8* bssid,u8* out,int max,int ms){ (void)m;(void)bssid;(void)ms; return rx_pop(out,max); }
void old_rtw_sec_write_cam(u32 m,int hw,int ki,int ty,int grp,const u8* a,const u8* key){ (void)m; u8 b[24]; b[0]=(u8)hw; b[1]=(u8)ki; b[2]=(u8)ty; b[3]=(u8)grp; memcpy(b+4,a,6); memcpy(b+10,key,14); R('K',b,24); R('k',key+14,2); }
void old_rtw_sec_enable_engine(u32 m){ (void)m; R('N',0,0); }
void old_rtw_jlog(const char* a,const char* b,int v){ char t[200]; snprintf(t,sizeof t,"%s%s%d",a,b,v); R('J',(const u8*)t,(int)strlen(t)); }
void old_wnd_setup(u32 m,const u8* bssid,const u8* mac,const u8* tk,int qos){ u8 b[32]; b[0]=(u8)m; b[1]=(u8)qos; memcpy(b+2,bssid,6); memcpy(b+8,mac,6); memcpy(b+14,tk,16); R('W',b,30); }
void old_wnd_set_gtk(const u8* g,int id){ u8 b[17]; memcpy(b,g,16); b[16]=(u8)id; R('G',b,17); }
int old_wnd_send_eapol(const u8* e,int n){ R('E',e,n); return n; }
void rtw_jlog(const char* a,const char* b,int v){ old_rtw_jlog(a,b,v); }
void wnd_setup(u32 m,const u8* bssid,const u8* mac,const u8* tk,int qos){ old_wnd_setup(m,bssid,mac,tk,qos); }
void wnd_set_gtk(const u8* g,int id){ old_wnd_set_gtk(g,id); }
int wnd_send_eapol(const u8* e,int n){ return old_wnd_send_eapol(e,n); }
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
#define csprng_bytes test_rand
#include "net/wifi/wpa_sm.c"
#undef csprng_bytes
#include "old_rtw_sec.c"
#undef wpa_gtk_status
#undef g_gtk_state
#undef g_gtk_keyid
#undef g_gtk_len
#undef g_wifi_rekeys
#undef g_wifi_ptk_rekey
#undef wpa_rx_eapol
#undef wpa_rx_eapol_p
#undef rtw_tx_data_eapol
#undef rtw_rx_wait_eapol
#undef rtw_sec_write_cam
#undef rtw_sec_enable_engine
#undef rtw_jlog
#undef wnd_setup
#undef wnd_set_gtk
#undef wnd_send_eapol
#undef csprng_bytes
#undef wpa_gtk_extract
#undef g_gtk
#undef g_wpa_replay
#undef g_m4
#undef g_m4len
#undef g_m4mp
#undef wpa_rc
static const u8 AA[6]={0x00,0x11,0x22,0x33,0x44,0x55}, SPA[6]={0x02,0x00,0x00,0xab,0xcd,0xef};
static void n_txe(void* c,const u8* e,int n){ (void)c; R('T',e,n); }
static int n_rxe(void* c,u8* o,int max,int ms){ (void)c;(void)ms; return rx_pop(o,max); }
static int n_key(void* c,int idx,int ty,const u8* key,int len,int pw){ (void)c; if(ty!=WIFI_KEY_CCMP||len!=16)return -1; if(pw){ old_rtw_sec_enable_engine(7); old_rtw_sec_write_cam(7,0,0,4,0,AA,key); } else old_rtw_sec_write_cam(7,idx,idx,4,1,AA,key); return 0; }
static u16 tseq; static struct wpa_io nio={ 0,n_txe,n_rxe,n_key,0,0,0,&tseq,7,0 };
static void kwrap(const u8* kek,const u8* p,int n,u8* o){ u8 rk[176]; aes128_expand(kek,rk); int nb=n/8; u8 A[8]; memset(A,0xA6,8); u8 Rr[64][8]; for(int i=0;i<nb;i++)memcpy(Rr[i],p+8*i,8);
    for(int j=0;j<6;j++)for(int i=0;i<nb;i++){ u8 B[16]; memcpy(B,A,8); memcpy(B+8,Rr[i],8); aes128_enc(rk,B,B); u64 t=(u64)(nb*j+i+1); memcpy(A,B,8); for(int q=0;q<8;q++)A[7-q]^=(u8)(t>>(8*q)); memcpy(Rr[i],B+8,8); }
    memcpy(o,A,8); for(int i=0;i<nb;i++)memcpy(o+8+8*i,Rr[i],8); }
static u8 ANONCE[32], apptk[48];
static int mkkey(u8* f,u16 ki,u64 rc,const u8* nonce,const u8* gtk,int gid){
    int n=0; f[n++]=1; f[n++]=3; n+=2; f[n++]=2; f[n++]=(u8)(ki>>8); f[n++]=(u8)ki; f[n++]=0; f[n++]=16;
    for(int i=7;i>=0;i--)f[n++]=(u8)(rc>>(8*i)); for(int i=0;i<32;i++)f[n++]=nonce?nonce[i]:0; for(int i=0;i<16+8+8+16;i++)f[n++]=0;
    if(gtk){ u8 kd[32]; int k=0; kd[k++]=0xDD; kd[k++]=22; kd[k++]=0; kd[k++]=0x0F; kd[k++]=0xAC; kd[k++]=1; kd[k++]=(u8)gid; kd[k++]=0; memcpy(kd+k,gtk,16); k+=16; kd[k++]=0xDD; kd[k++]=0; while(k%8)kd[k++]=0;
        f[n++]=0; f[n++]=(u8)(k+8); kwrap(apptk+16,kd,k,f+n); n+=k+8; } else { f[n++]=0; f[n++]=0; }
    f[2]=(u8)((n-4)>>8); f[3]=(u8)(n-4);
    if(ki&(1u<<8)){ u8 sv[48]; memcpy(sv,g_ptk,48); memcpy(g_ptk,apptk,48); u8 m[32]; wpa_mic(f,n,m); memcpy(f+81,m,16); memcpy(g_ptk,sv,48); }
    return n; }
static void push(const u8* f,int n){ memcpy(rxq[rxn],f,n); rxl[rxn++]=n; }
static int fails=0, passes=0;
static void ck(int c,const char* t){ if(c)passes++; else { fails++; printf("FAIL %s\n",t); } }
static void scenario(int akm,int gid){
    g_wpa_akm=akm; g_wpa_mfp=akm==8; u16 v=(u16)wpa_key_ver();
    for(int i=0;i<32;i++)ANONCE[i]=(u8)(0xA0+i);
    pbkdf2_sha1((const u8*)"correct horse",13,(const u8*)"HomeNet",7,4096,g_pmk,32);
    wpa_derive_ptk_akm(g_pmk,AA,SPA,ANONCE,SN,apptk);
    u8 gtk[16], gtk2[16]; for(int i=0;i<16;i++){ gtk[i]=(u8)(0x50+i); gtk2[i]=(u8)(0x70+i); }
    u8 ptk_res[2][48]; int ret[2]; u8 f[600]; int n;
    for(cu=0;cu<2;cu++){
        recn[cu]=0; rxh=rxn=0; g_wpa_ok=0;
        n=mkkey(f,(u16)(v|(1u<<3)|(1u<<7)),1,ANONCE,0,0); push(f,n);
        n=mkkey(f,(u16)(v|(1u<<3)|(1u<<7)),2,ANONCE,0,0); push(f,n);
        n=mkkey(f,(u16)(v|(1u<<3)|(1u<<6)|(1u<<7)|(1u<<8)|(1u<<9)|(1u<<12)),3,ANONCE,gtk,gid); push(f,n);
        scan_ap ap; memcpy(ap.bssid,AA,6);
        ret[cu]=cu==0?old_4way(7,&ap,SPA):wpa_sm_4way(&nio,AA,SPA);
        memcpy(ptk_res[cu],g_ptk,48); R('S',(const u8*)&g_wpa_ok,1);
        int gs=cu==0?old_g_gtk_state:g_gtk_state; R('s',(const u8*)&gs,1);
        n=mkkey(f,(u16)(v|(1u<<7)|(1u<<8)|(1u<<9)|(1u<<12)),4,0,gtk2,2);
        if(cu==0)old_wpa_rx_eapol(f,n); else wpa_rx_eapol(f,n);
        n=mkkey(f,(u16)(v|(1u<<7)|(1u<<8)|(1u<<9)|(1u<<12)),4,0,gtk2,2);
        if(cu==0)old_wpa_rx_eapol(f,n); else wpa_rx_eapol(f,n);
        n=mkkey(f,(u16)(v|(1u<<3)|(1u<<6)|(1u<<7)|(1u<<8)|(1u<<9)|(1u<<12)),5,ANONCE,gtk,gid);
        if(cu==0)old_wpa_rx_eapol(f,n); else wpa_rx_eapol(f,n);
        n=mkkey(f,(u16)(v|(1u<<3)|(1u<<7)|(1u<<8)|(1u<<9)),6,0,0,0);
        if(cu==0)old_wpa_rx_eapol_p(f,n,1); else wpa_rx_eapol_p(f,n,1);
        int pr=cu==0?old_g_wifi_ptk_rekey:g_wifi_ptk_rekey; R('P',(const u8*)&pr,1);
        int rk=cu==0?old_g_wifi_rekeys:g_wifi_rekeys; R('r',(const u8*)&rk,1);
    }
    char t[96]; snprintf(t,sizeof t,"akm %d gtk id %d: 4-way returns 0 in both",akm,gid); ck(ret[0]==0&&ret[1]==0,t);
    snprintf(t,sizeof t,"akm %d gtk id %d: PTK identical and equals the AP's",akm,gid); ck(!memcmp(ptk_res[0],ptk_res[1],48)&&!memcmp(ptk_res[0],apptk,48),t);
    snprintf(t,sizeof t,"akm %d gtk id %d: every frame, key write, log line and state identical (%d bytes)",akm,gid,recn[0]); ck(recn[0]==recn[1]&&!memcmp(rec[0],rec[1],recn[0])&&recn[0]>400,t);
    if(recn[0]!=recn[1]||memcmp(rec[0],rec[1],recn[0])){ int i=0; while(i<recn[0]&&i<recn[1]&&rec[0][i]==rec[1][i])i++; printf("  first difference at byte %d of %d/%d\n",i,recn[0],recn[1]); }
}
static void badpw(void){
    g_wpa_akm=2; g_wpa_mfp=0; u16 v=2; u8 f[600]; int n; int ret[2];
    for(cu=0;cu<2;cu++){ recn[cu]=0; rxh=rxn=0;
        n=mkkey(f,(u16)(v|(1u<<3)|(1u<<7)),10,ANONCE,0,0); push(f,n);
        u8 bad[48]; memcpy(bad,apptk,48); apptk[0]^=1; n=mkkey(f,(u16)(v|(1u<<3)|(1u<<6)|(1u<<7)|(1u<<8)|(1u<<9)|(1u<<12)),11,ANONCE,ANONCE,1); push(f,n); memcpy(apptk,bad,48);
        scan_ap ap; memcpy(ap.bssid,AA,6); ret[cu]=cu==0?old_4way(7,&ap,SPA):wpa_sm_4way(&nio,AA,SPA); }
    ck(ret[0]==6&&ret[1]==6&&recn[0]==recn[1]&&!memcmp(rec[0],rec[1],recn[0]),"wrong password (msg3 MIC mismatch): both return 6 with identical frames");
    for(cu=0;cu<2;cu++){ recn[cu]=0; rxh=rxn=0; scan_ap ap; memcpy(ap.bssid,AA,6); ret[cu]=cu==0?old_4way(7,&ap,SPA):wpa_sm_4way(&nio,AA,SPA); }
    ck(ret[0]==5&&ret[1]==5&&recn[0]==recn[1]&&!memcmp(rec[0],rec[1],recn[0]),"no msg1: both return 5 with identical log");
}
int main(void){
    scenario(2,1); scenario(2,2); scenario(2,0); scenario(6,1); scenario(8,1); badpw();
    printf("%d passed, %d failed\n",passes,fails); return fails?1:0; }
