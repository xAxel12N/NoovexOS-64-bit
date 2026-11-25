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
int wpa_rsn_ie(u8* b){ (void)b; return 0; }
void rtw_jlog(const char* a,const char* b,int v){ (void)a;(void)b;(void)v; }
void wnd_setup(u32 m,const u8* bssid,const u8* mac,const u8* tk,int qos){ (void)m;(void)bssid;(void)mac;(void)tk;(void)qos; }
void wnd_set_gtk(const u8* g,int id){ (void)g;(void)id; }
int wnd_send_eapol(const u8* e,int n){ (void)e; return n; }
void ui_pump(void){}
static int nklog=0; void klog(const char* s){ (void)s; nklog++; }
u64 hr_ns(void){ static u64 t; return t+=1000000; }
const struct wifi_hw_ops* wifi_hw_ops_get(void){ return 0; }
void* wifi_hw_ctx(void){ return 0; }
int wifi_hw_tx(const u8* f,int n){ (void)f;(void)n; return 0; }
void wifi_hw_poll(void){}
int wifi_hw_set_key(int i,int t,const u8* k,int l,int p){ (void)i;(void)t;(void)k;(void)l;(void)p; return 0; }
int wifi_hw_mac(u8* m){ memset(m,0,6); return 0; }
int wifi_hw_join(const u8* b,u8 c,const char* s,u8 a,u8 h){ (void)b;(void)c;(void)s;(void)a;(void)h; return 0; }
#include "net/wifi/wpa_sm.c"
static int fails=0, passes=0;
static void ck(int c,const char* t){ if(c){ passes++; printf("PASS %s\n",t); } else { fails++; printf("FAIL %s\n",t); } }
static const u8 AP[6]={0x02,0x11,0x22,0x33,0x44,0x55}, STA[6]={0x02,0xaa,0xbb,0xcc,0xdd,0xee};
static const char* ap_ssid="h2e-net"; static const char* ap_pass="correct horse"; static int ap_h2e=1, ap_token=0, ap_status_override=-1, ap_tok_seen=0, sta_status=-1;
static sae_ctx A; static u8 q[4][400]; static int ql[4], qh=0, qn=0;
static void push(const u8* b,int n){ memcpy(q[qn&3],b,n); ql[qn&3]=n; qn++; }
static int ap_tx(void* c,const u8* f,int len,int phase,int round){ (void)c;(void)phase;(void)round; const u8* b=f+24; int bl=len-24; u16 seq=b[2]|(b[3]<<8); u8 r[400]; int n=0;
    if(seq==1){ sta_status=b[4]|(b[5]<<8); int o=8;
        if(ap_token&&!ap_tok_seen){ int ok=0; if(ap_h2e&&sta_status==126){ int e=8+96; if(bl>=e+3&&b[e]==255&&b[e+2]==93&&b[e+1]==5&&!memcmp(b+e+3,"TOKN",4))ok=1; } if(!ap_h2e&&bl>=12+96&&!memcmp(b+8,"TOKN",4)){ ok=1; o=12; }
            if(!ok){ r[n++]=3;r[n++]=0;r[n++]=1;r[n++]=0;r[n++]=76;r[n++]=0;r[n++]=19;r[n++]=0; if(ap_h2e){ r[n++]=255;r[n++]=5;r[n++]=93; } memcpy(r+n,"TOKN",4); n+=4; push(r,n); return 1; } ap_tok_seen=1; }
        u8 pt[65]; if(ap_h2e){ sae_derive_pt((const u8*)ap_ssid,(int)strlen(ap_ssid),(const u8*)ap_pass,(int)strlen(ap_pass),0,0,pt); sae_commit_pt(&A,AP,STA,pt); } else sae_commit(&A,AP,STA,ap_pass);
        if(sae_process_commit(&A,b+o,b+o+32)<0)return 1;
        int st=ap_status_override>=0?ap_status_override:(ap_h2e?126:0);
        r[n++]=3;r[n++]=0;r[n++]=1;r[n++]=0;r[n++]=(u8)st;r[n++]=0;r[n++]=19;r[n++]=0; memcpy(r+n,A.scalar,32); n+=32; memcpy(r+n,A.elem+1,64); n+=64; push(r,n); return 1; }
    if(seq==2){ if(sae_check_confirm(&A,(u16)(b[6]|(b[7]<<8)),b+8)<0)return 1; u8 cf[32]; sae_confirm(&A,1,cf); r[n++]=3;r[n++]=0;r[n++]=2;r[n++]=0;r[n++]=0;r[n++]=0;r[n++]=1;r[n++]=0; memcpy(r+n,cf,32); n+=32; push(r,n); return 1; }
    return 0; }
static int ap_rx(void* c,u8 want,u8* body,int* blen,int ms){ (void)c;(void)want;(void)ms; if(qh>=qn)return 0; int s=qh&3; memcpy(body,q[s],ql[s]); *blen=ql[s]; qh++; return 1; }
static u16 seqv=0;
static const struct wpa_io io={ 0,0,0,0,ap_tx,ap_rx,0,&seqv,0,0 };
static int run(const char* pass){ qh=qn=0; ap_tok_seen=0; memset(&A,0,sizeof A); int sae=0; wpa_sm_select(2,4,2|(ap_h2e?16:0),3,0,&sae); wpa_sm_pmk(ap_ssid,pass,sae,0); return wpa_sm_sae(&io,AP,STA,pass); }
int main(void){
    ck(run(ap_pass)==0&&sta_status==126&&g_wpa_sae_h2e==1,"H2E network: commit sent with status 126 (SAE_HASH_TO_ELEMENT)");
    ck(!memcmp(g_pmk,A.pmk,32),"H2E: STA and AP agree on PMK");
    ap_token=1; ck(run(ap_pass)==0&&ap_tok_seen,"H2E: anti-clogging token echoed in a Token Container element"); ap_token=0;
    ck(run("wrong pass")!=0,"H2E: wrong password fails at confirm");
    ap_h2e=0; ck(run(ap_pass)==0&&sta_status==0&&g_wpa_sae_h2e==0,"no RSNXE H2E bit: hunting-and-pecking, status 0"); ck(!memcmp(g_pmk,A.pmk,32),"HnP: PMK agrees");
    ap_token=1; ck(run(ap_pass)==0&&ap_tok_seen,"HnP: anti-clogging token raw before scalar still works"); ap_token=0;
    ap_h2e=1; g_wifi_h2e_fail=0; ap_status_override=0; int k0=nklog; for(int i=0;i<3;i++)run(ap_pass); ck(g_wifi_h2e_fail==3&&nklog==k0+1,"H2E: AP answering status 0 counts as failure, 3 failures logged once");
    int sae=0; wpa_sm_select(2,4,2|16,3,0,&sae); ck(g_wpa_sae_h2e==0,"after 3 failures H2E is off for networks that allow hunting-and-pecking");
    wpa_sm_select(2,4,2|16|32,3,0,&sae); ck(g_wpa_sae_h2e==1&&sae==1,"H2E-only network (BSS membership selector 123) keeps H2E");
    wpa_sm_select(2,4,1|2|16|32,3,0,&sae); ck(sae==1,"H2E-only selector forces SAE on a mixed network");
    ap_status_override=-1; g_wifi_h2e_fail=0; g_wifi_h2e=0; wpa_sm_select(2,4,2|16,3,0,&sae); ck(g_wpa_sae_h2e==0,"wifi h2e off: not used"); g_wifi_h2e=1;
    wpa_sm_select(2,4,1|16,3,0,&sae); wpa_sm_pmk(ap_ssid,ap_pass,sae,0); ck(sae==0&&g_wpa_sae_h2e==0,"WPA2-PSK join never sends RSNXE H2E");
    printf("%d passed, %d failed\n",passes,fails); if(!fails)printf("h2e: all ok\n"); return fails?1:0; }
