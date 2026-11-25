#include "include/nvx/base.h"
#include "include/nvx/lib/crypto/nvxwpa.h"
#include "include/nvx/lib/crypto/nvxcrypto.h"
#include "include/nvx/lib/crypto/nvxcrypto2.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/net/wifi/wpa2.h"
#include "include/nvx/net/wifi/wpa3.h"
#include "include/nvx/net/wifi/eap.h"
#include "include/nvx/net/wifi/wifi_hw.h"
#include "include/nvx/net/wifi/wifi_netdev.h"
#include "include/nvx/net/wifi/wpa_sm.h"

void rtw_jlog(const char* a,const char* b,int v);
void ui_pump(void);
static u8 g_gtk[32]; int g_gtk_state=0, g_gtk_keyid=0, g_gtk_len=0;
static u8 g_igtk[16]; int g_igtk_state=0, g_igtk_keyid=0, g_wifi_sae_ext=1;
int g_wifi_rekeys=0, g_wifi_ptk_rekey=0, g_wifi_prefer_sae=0;
static u64 g_wpa_replay=0; static const struct wpa_io* wsm_io=0;
static u8 g_m4[128]; static int g_m4len=0, g_m4mp=0;
static u8 wsm_stash[512]; static int wsm_stashlen=0;
const char* wpa_gtk_status(void){ switch(g_gtk_state){ case 1: return "group key (GTK) installed"; case 2: return "no GTK in msg3"; case 3: return "GTK unwrap failed";
    case 4: return "msg3 MIC mismatch - GTK skipped"; case 5: return "group cipher is not CCMP - GTK skipped"; case 6: return "GTK key index 0 clashes with the pairwise slot - skipped"; default: return "GTK not processed"; } }
static u64 wpa_rc(const u8* eap){ u64 r=0; for(int i=0;i<8;i++)r=(r<<8)|eap[9+i]; return r; }
static int wsm_m3x=0, wsm_bcnx=0;
static void wpa_gtk_extract(const u8* eap,int el){
    g_gtk_state=0; g_gtk_len=0; wsm_m3x=0;
    int flen=4+((eap[2]<<8)|eap[3]); if(flen>el)flen=el; if(flen<99){ g_gtk_state=2; return; }
    static u8 cp[512]; if(flen>512){ g_gtk_state=2; return; } for(int i=0;i<flen;i++)cp[i]=eap[i]; for(int i=81;i<97;i++)cp[i]=0;
    u8 mic[32]; wpa_mic(cp,flen,mic); for(int i=0;i<16;i++) if(mic[i]!=eap[81+i]){ g_gtk_state=4; return; }
    u16 ki=(u16)((eap[5]<<8)|eap[6]); int kdl=(eap[97]<<8)|eap[98]; if(kdl<=0||99+kdl>flen){ g_gtk_state=2; return; }
    static u8 kd[512]; int n=kdl;
    if(ki&(1u<<12)){ n=aes_key_unwrap(&g_ptk[16],eap+99,kdl,kd); if(n<0){ g_gtk_state=3; return; } }
    else for(int i=0;i<kdl;i++)kd[i]=eap[99+i];
    g_igtk_state=0; int gf=0;
    for(int p=0;p+2<=n;){ u8 t=kd[p], l=kd[p+1]; if(t==0xDD&&l==0)break;
        if(!gf&&t==0xDD&&l>=6&&kd[p+2]==0x00&&kd[p+3]==0x0F&&kd[p+4]==0xAC&&kd[p+5]==1&&p+2+l<=n){
            g_gtk_keyid=kd[p+6]&3; g_gtk_len=l-6; if(g_gtk_len!=16){ g_gtk_state=5; return; }
            for(int i=0;i<16;i++)g_gtk[i]=kd[p+8+i]; gf=1; }
        else if(t==0xDD&&l>=28&&kd[p+2]==0x00&&kd[p+3]==0x0F&&kd[p+4]==0xAC&&kd[p+5]==9&&p+2+l<=n){
            g_igtk_keyid=kd[p+6]|(kd[p+7]<<8); for(int i=0;i<16;i++)g_igtk[i]=kd[p+14+i]; g_igtk_state=(g_igtk_keyid==4||g_igtk_keyid==5)?1:2; }
        else if(t==244&&l>=1&&p+2+l<=n)wsm_m3x=0x100|kd[p+2];
        p+=2+l; }
    g_gtk_state=gf?1:2;
}
int wpa_rx_eapol(const u8* eap,int el){ return wpa_rx_eapol_p(eap,el,1); }
int wpa_rx_eapol_p(const u8* eap,int el,int prot){
    if(el<99||eap[1]!=3||eap[4]!=2)return 0;
    u16 ki=(u16)((eap[5]<<8)|eap[6]); u64 rc=wpa_rc(eap);
    if(rc<=g_wpa_replay&&g_wpa_replay)return 1;
    if((ki&(1u<<3))&&(ki&(1u<<8))&&(ki&(1u<<6))&&g_m4len&&el<=512){ static u8 cp[512]; int flen=4+((eap[2]<<8)|eap[3]); if(flen>el||flen<99)return 1; for(int i=0;i<flen;i++)cp[i]=eap[i]; for(int i=81;i<97;i++)cp[i]=0; u8 mc[32]; wpa_mic(cp,flen,mc); for(int i=0;i<16;i++) if(mc[i]!=eap[81+i])return 1;
        g_wpa_replay=rc; for(int i=0;i<8;i++)g_m4[9+i]=eap[9+i]; for(int i=0;i<16;i++)g_m4[g_m4mp+i]=0; wpa_mic(g_m4,g_m4len,mc); for(int i=0;i<16;i++)g_m4[g_m4mp+i]=mc[i]; if(wsm_io)wsm_io->tx_eapol(wsm_io->ctx,g_m4,g_m4len); return 1; }
    if(!prot)return 1;
    if(ki&(1u<<3)){ g_wifi_ptk_rekey=1; return 1; }
    if(!(ki&(1u<<8))||!(ki&(1u<<7)))return 1;
    wpa_gtk_extract(eap,el); if(g_gtk_state!=1)return 1;
    g_wpa_replay=rc;
    if(g_gtk_keyid>0&&g_gtk_keyid<4&&wsm_io)wsm_io->set_key(wsm_io->ctx,g_gtk_keyid,WIFI_KEY_CCMP,g_gtk,16,0);
    if(g_wpa_mfp&&g_igtk_state==1&&wsm_io)wsm_io->set_key(wsm_io->ctx,g_igtk_keyid,WIFI_KEY_BIP,g_igtk,16,0);
    wnd_set_gtk(g_gtk,g_gtk_keyid);
    u8 m2[100]; int ml=0; m2[ml++]=0x01; m2[ml++]=0x03; int lp=ml; m2[ml++]=0; m2[ml++]=0; m2[ml++]=0x02;
    u16 k2=(u16)((1u<<8)|(1u<<9)|(ki&7u)); m2[ml++]=(u8)(k2>>8); m2[ml++]=(u8)k2; m2[ml++]=0; m2[ml++]=0;
    for(int i=0;i<8;i++)m2[ml++]=eap[9+i]; for(int i=0;i<32+16+8+8;i++)m2[ml++]=0;
    int mp=ml; for(int i=0;i<16;i++)m2[ml++]=0; m2[ml++]=0; m2[ml++]=0;
    int b=ml-4; m2[lp]=(u8)(b>>8); m2[lp+1]=(u8)b;
    u8 mic[32]; wpa_mic(m2,ml,mic); for(int i=0;i<16;i++)m2[mp+i]=mic[i];
    wnd_send_eapol(m2,ml); g_wifi_rekeys++; return 1; }
int g_wpa_sae_h2e=0, g_wifi_h2e=1, g_wifi_h2e_fail=0; static char wsm_ssid[33]; static int wsm_ssidl=0, wsm_h2e_only=0;
int wpa_sm_select(u8 sec,u8 grp,u8 akm,u8 pmf,int eap,int* sae){
    if(sec==2&&grp&&grp!=4)return 9;
    wsm_h2e_only=(akm&32)?1:0; wsm_bcnx=(akm&16)?1:0; g_wpa_sae_h2e=(akm&16)&&(wsm_h2e_only||(g_wifi_h2e&&g_wifi_h2e_fail<3));
    int s=!eap&&(akm&2)&&(!(akm&1)||g_wifi_prefer_sae||wsm_h2e_only);
    g_wpa_akm=eap?1:s?8:(!(akm&1)&&(akm&8))?6:2; g_wpa_mfp=s||(pmf&2); *sae=s; return 0; }
int g_wpa_pmk_hit=0;
void wpa_sm_pmk(const char* ssid,const char* pass,int sae,int eap){
    int slen=0; while(ssid[slen]&&slen<32)slen++; int plen2=0; while(pass[plen2]&&plen2<64)plen2++;
    for(int i=0;i<slen;i++)wsm_ssid[i]=ssid[i]; wsm_ssid[slen]=0; wsm_ssidl=slen; if(!sae)g_wpa_sae_h2e=0;
    int rawpsk=0; if(plen2==64&&!sae&&!eap){ rawpsk=1; for(int i=0;i<64;i++){ char c=pass[i]; if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))rawpsk=0; } if(rawpsk)for(int i=0;i<32;i++){ int a=pass[2*i],b=pass[2*i+1]; a=a<='9'?a-'0':(a|32)-'a'+10; b=b<='9'?b-'0':(b|32)-'a'+10; g_pmk[i]=(u8)(a*16+b); } }
    if(!sae&&!eap&&!rawpsk){ static u8 ch[32], cpmk[32]; static int cok=0; u8 h[32]; { u8 t[100]; int n=0; for(int i=0;i<slen;i++)t[n++]=(u8)ssid[i]; t[n++]=0; for(int i=0;i<plen2;i++)t[n++]=(u8)pass[i]; sha256_hash(t,n,h); for(int i=0;i<n;i++)t[i]=0; }
        int hit=cok; for(int i=0;i<32&&hit;i++) if(ch[i]!=h[i])hit=0;
        if(hit){ for(int i=0;i<32;i++)g_pmk[i]=cpmk[i]; g_wpa_pmk_hit=1; }
        else { pbkdf2_sha1((const u8*)pass,plen2,(const u8*)ssid,slen,4096,g_pmk,32); for(int i=0;i<32;i++){ ch[i]=h[i]; cpmk[i]=g_pmk[i]; } cok=1; } } }
static int wsm_auth_hdr(const struct wpa_io* io,u8* f,const u8* bssid,const u8* mac){
    int fl=0; f[fl++]=0xB0; f[fl++]=0; f[fl++]=0; f[fl++]=0; for(int i=0;i<6;i++)f[fl++]=bssid[i]; for(int i=0;i<6;i++)f[fl++]=mac[i]; for(int i=0;i<6;i++)f[fl++]=bssid[i];
    f[fl++]=(u8)(*io->seq<<4); f[fl++]=(u8)(*io->seq>>4); (*io->seq)++; return fl; }
static sae_ctx wsm_sc;
static int wsm_sae_run(const struct wpa_io* io,const u8* bssid,const u8* mac,const char* pass){
    if(!io->sae_tx||!io->rx_mgmt||!io->seq)return 4;
    int h2e=g_wpa_sae_h2e;
    if(h2e){ static u8 pt[65], ptk[32]; static int ptok=0; u8 key[32]; { u8 t[100]; int n=0; for(int i=0;i<wsm_ssidl;i++)t[n++]=(u8)wsm_ssid[i]; t[n++]=0; for(int i=0;pass[i]&&i<64;i++)t[n++]=(u8)pass[i]; sha256_hash(t,n,key); for(int i=0;i<n;i++)t[i]=0; }
        int hit=ptok; for(int i=0;i<32&&hit;i++) if(ptk[i]!=key[i])hit=0;
        if(!hit){ int pl=0; while(pass[pl]&&pl<64)pl++; ptok=0; if(sae_derive_pt((const u8*)wsm_ssid,wsm_ssidl,(const u8*)pass,pl,0,0,pt)<0)return 2; for(int i=0;i<32;i++)ptk[i]=key[i]; ptok=1; }
        if(sae_commit_pt(&wsm_sc,mac,bssid,pt)<0)return 2; rtw_jlog("  SAE hash-to-element (H2E)","",-1); }
    else if(sae_commit(&wsm_sc,mac,bssid,pass)<0)return 2;
    u8 f[320]; int fl; u8 body[400]; int blen=0; u8 tok[128]; int tl=0;
    int sdone=0;
    for(int round=0;round<3;round++){
        fl=wsm_auth_hdr(io,f,bssid,mac);
        f[fl++]=3; f[fl++]=0; f[fl++]=1; f[fl++]=0; f[fl++]=h2e?126:0; f[fl++]=0; f[fl++]=19; f[fl++]=0;
        if(!h2e)for(int i=0;i<tl;i++)f[fl++]=tok[i]; for(int i=0;i<32;i++)f[fl++]=wsm_sc.scalar[i]; for(int i=0;i<64;i++)f[fl++]=wsm_sc.elem[1+i];
        if(h2e&&tl){ f[fl++]=255; f[fl++]=(u8)(tl+1); f[fl++]=93; for(int i=0;i<tl;i++)f[fl++]=tok[i]; }
        int got=0;
        for(int ph=0;ph<2&&!got;ph++){ int nw=io->sae_tx(io->ctx,f,fl,ph,round+1); if(nw<=0)break;
            for(int w=0;w<nw&&!got;w++){ if(!io->rx_mgmt(io->ctx,0xB0,body,&blen,600))continue; if(blen>=6&&body[0]==3&&body[2]==1)got=1; }
            if(got&&ph&&io->sae_phase_ok)io->sae_phase_ok(io->ctx,ph); }
        if(!got){ rtw_jlog("  no SAE commit answer, round","",round+1); return 1; }
        int st=body[4]|(body[5]<<8); rtw_jlog("  SAE commit answer, status","",st);
        if(st==76&&blen>8){ int o=8; tl=blen-8; if(h2e){ if(blen<11||body[8]!=255||body[10]!=93||body[9]<2||11+body[9]-1>blen)return 2; o=11; tl=body[9]-1; } if(tl>128)tl=128; for(int i=0;i<tl;i++)tok[i]=body[o+i]; continue; }
        if(st!=(h2e?126:0)){ if(h2e&&!wsm_h2e_only&&++g_wifi_h2e_fail==3)klog("wifi: SAE hash-to-element failed 3 times; using hunting-and-pecking on networks that allow it (wifi h2e on to retry)"); return 2; }
        if(blen<8+96||(body[6]|(body[7]<<8))!=19)return 2;
        if(sae_process_commit(&wsm_sc,body+8,body+40)<0)return 2;
        sdone=1; break; }
    if(!sdone)return 1;
    u8 cf[32]; sae_confirm(&wsm_sc,1,cf);
    fl=wsm_auth_hdr(io,f,bssid,mac);
    f[fl++]=3; f[fl++]=0; f[fl++]=2; f[fl++]=0; f[fl++]=0; f[fl++]=0; f[fl++]=1; f[fl++]=0; for(int i=0;i<32;i++)f[fl++]=cf[i];
    io->sae_tx(io->ctx,f,fl,-1,0);
    int got=0; for(int w=0;w<4&&!got;w++){ if(!io->rx_mgmt(io->ctx,0xB0,body,&blen,600))continue; if(blen>=6&&body[0]==3&&body[2]==2)got=1; }
    if(!got)return 6; if((body[4]|(body[5]<<8))!=0||blen<8+32)return 6;
    if(sae_check_confirm(&wsm_sc,(u16)(body[6]|(body[7]<<8)),body+8)<0)return 6;
    for(int i=0;i<32;i++)g_pmk[i]=wsm_sc.pmk[i]; if(h2e)g_wifi_h2e_fail=0;
    return 0; }
int wpa_sm_sae(const struct wpa_io* io,const u8* bssid,const u8* mac,const char* pass){ int r=wsm_sae_run(io,bssid,mac,pass); volatile u8* z=(volatile u8*)&wsm_sc; for(int i=0;i<(int)sizeof wsm_sc;i++)z[i]=0; return r; }
void wpa_sm_stash_clear(void){ wsm_stashlen=0; }
static int wsm_rx(const struct wpa_io* io,u8* out,int max,int ms){
    if(wsm_stashlen>0){ int n=wsm_stashlen; if(n>max)n=max; for(int c=0;c<n;c++)out[c]=wsm_stash[c]; wsm_stashlen=0; return n; }
    return io->rx_eapol(io->ctx,out,max,ms); }
static const struct wpa_io* eap_io;
static int wsm_eap_tx(void* c,const u8* e,int n){ (void)c; static u8 b[1600]; if(n>1500)return -1; b[0]=1; b[1]=0; b[2]=(u8)(n>>8); b[3]=(u8)n; for(int i=0;i<n;i++)b[4+i]=e[i]; eap_io->tx_eapol(eap_io->ctx,b,4+n); return n; }
static int wsm_eap_rx(void* c,u8* o,int cap){ (void)c; static u8 b[1700];
    for(int t=0;t<6;t++){ int n=wsm_rx(eap_io,b,(int)sizeof(b),3000); if(n<4)return -1;
        if(b[1]==3){ int k=n<512?n:512; for(int i=0;i<k;i++)wsm_stash[i]=b[i]; wsm_stashlen=k; o[0]=3; o[1]=0; o[2]=0; o[3]=4; return 4; }
        if(b[1]!=0)continue; int L=(b[2]<<8)|b[3]; if(L>n-4)L=n-4; if(L>cap)L=cap; for(int i=0;i<L;i++)o[i]=b[4+i]; return L; }
    return -1; }
int wpa_sm_eap(const struct wpa_io* io,const char* user,const char* pass){
    eap_io=io; u8 msk[64]; wsm_stashlen=0; int er=eap_peap_run(wsm_eap_tx,wsm_eap_rx,0,user,pass,msk);
    if(er<0)return er==-1?1:er==-3?4:er==-4?8:er==-12?10:7;
    for(int i=0;i<32;i++)g_pmk[i]=msk[i];
    for(int i=0;i<64;i++)msk[i]=0;
    return 0; }
int wpa_sm_4way(const struct wpa_io* io,const u8* aa,const u8* mac){
    u8 eap[512];
    int el=wsm_rx(io,eap,512,1500);
    if(el<95){ rtw_jlog(el>0?"  key message 1/4 too short":"  no key message 1/4 within 1.5 s","",el>0?el:-1); return 5; }
    rtw_jlog("  key message 1/4 received","",-1);
    const u8* kf=eap;
    u8 anonce[32]; for(int i=0;i<32;i++)anonce[i]=kf[4+1+2+2+8+i]; u64 rc1=wpa_rc(eap);
    u8 snonce[32]; csprng_bytes(snonce,32);
    wpa_derive_ptk_akm(g_pmk,aa,mac,anonce,snonce,g_ptk);
    u8 m2[128]; int i,ml=0;
    m2[ml++]=0x01; m2[ml++]=0x03;
    int lenpos=ml; m2[ml++]=0; m2[ml++]=0;
    m2[ml++]=0x02;
    u16 ki=(u16)((1u<<3)|(1u<<8)|(u32)wpa_key_ver());
    m2[ml++]=(u8)(ki>>8); m2[ml++]=(u8)ki;
    m2[ml++]=0x00; m2[ml++]=0x00;
    for(i=0;i<8;i++)m2[ml++]=kf[4+1+2+2+i];
    for(i=0;i<32;i++)m2[ml++]=snonce[i];
    for(i=0;i<16+8+8;i++)m2[ml++]=0;
    int micpos=ml; for(i=0;i<16;i++)m2[ml++]=0;
    u8 rsn[32]; int rl=wpa_rsn_ie(rsn);
    m2[ml++]=(u8)(rl>>8); m2[ml++]=(u8)rl;
    for(i=0;i<rl;i++)m2[ml++]=rsn[i];
    int body2=ml-4; m2[lenpos]=(u8)(body2>>8); m2[lenpos+1]=(u8)body2;
    u8 mic[32]; wpa_mic(m2,ml,mic);
    for(i=0;i<16;i++)m2[micpos+i]=mic[i];
    io->tx_eapol(io->ctx,m2,ml); rtw_jlog("  key message 2/4 sent","",-1);
    for(int tries=0;;tries++){
        el=wsm_rx(io,eap,512,1500);
        if(el<99){ rtw_jlog("  no key message 3/4 (wrong password is the usual cause)","",-1); return 6; }
        if(wpa_rc(eap)<=rc1){ if(tries<6)continue; return 5; }
        u16 k3=(u16)((eap[5]<<8)|eap[6]);
        if(!(k3&(1u<<8))&&(k3&(1u<<7))&&(k3&(1u<<3))&&tries<3){ { int nw=0; for(i=0;i<32;i++) if(eap[17+i]!=anonce[i])nw=1; if(nw){ for(i=0;i<32;i++)anonce[i]=eap[17+i]; wpa_derive_ptk_akm(g_pmk,aa,mac,anonce,snonce,g_ptk); } } for(i=0;i<8;i++)m2[9+i]=eap[9+i]; for(i=0;i<16;i++)m2[micpos+i]=0; wpa_mic(m2,ml,mic); for(i=0;i<16;i++)m2[micpos+i]=mic[i]; io->tx_eapol(io->ctx,m2,ml); continue; }
        if((k3&((1u<<8)|(1u<<7)|(1u<<6)|(1u<<9)))!=((1u<<8)|(1u<<7)|(1u<<6)|(1u<<9))) return 6;
        { int same=1; for(i=0;i<32;i++) if(eap[17+i]!=anonce[i])same=0; if(!same) return 6; }
        break; }
    wpa_gtk_extract(eap,el);
    if(g_gtk_state==4) return 6;
    if(g_gtk_state==2||g_gtk_state==3) return 11;
    if(g_gtk_state==5) return 9;
    if(g_wpa_akm==8&&(wsm_m3x&0x120)==0x120&&!wsm_bcnx){ rtw_jlog("  key message 3/4 says the network supports SAE hash-to-element but its beacon did not (downgrade attempt?)","",-1); return 12; }
    g_wpa_replay=wpa_rc(eap); wsm_io=io;
    u8 m4[128]; ml=0;
    m4[ml++]=0x01;m4[ml++]=0x03; int lp=ml; m4[ml++]=0;m4[ml++]=0;
    m4[ml++]=0x02;
    u16 ki4=(u16)((1u<<3)|(1u<<8)|(1u<<9)|(u32)wpa_key_ver());
    m4[ml++]=(u8)(ki4>>8);m4[ml++]=(u8)ki4;
    m4[ml++]=0x00;m4[ml++]=0x00;
    for(i=0;i<8;i++)m4[ml++]=eap[4+1+2+2+i];
    for(i=0;i<32+16+8+8;i++)m4[ml++]=0;
    int mp=ml; for(i=0;i<16;i++)m4[ml++]=0;
    m4[ml++]=0;m4[ml++]=0;
    int b4=ml-4; m4[lp]=(u8)(b4>>8); m4[lp+1]=(u8)b4;
    wpa_mic(m4,ml,mic); for(i=0;i<16;i++)m4[mp+i]=mic[i];
    io->tx_eapol(io->ctx,m4,ml); rtw_jlog("  key message 3/4 ok, 4/4 sent, keys installed","",-1);
    for(i=0;i<ml;i++)g_m4[i]=m4[i]; g_m4len=ml; g_m4mp=mp;
    io->set_key(io->ctx,0,WIFI_KEY_CCMP,&g_ptk[32],16,1);
    int gtk_ok=g_gtk_state==1;
    if(gtk_ok){ if(g_gtk_keyid>0&&g_gtk_keyid<4) io->set_key(io->ctx,g_gtk_keyid,WIFI_KEY_CCMP,g_gtk,16,0); else g_gtk_state=6; }
    if(g_wpa_mfp&&g_igtk_state==1) io->set_key(io->ctx,g_igtk_keyid,WIFI_KEY_BIP,g_igtk,16,0);
    wnd_setup(io->wnd_m,aa,mac,&g_ptk[32],io->qos);
    if(gtk_ok) wnd_set_gtk(g_gtk,g_gtk_keyid);
    g_wpa_ok=1;
    return 0;
}
static u8 wg_bssid[6], wg_mac[6]; static int wg_on=0;
static u8 wg_q[4][600]; static int wg_ql[4], wg_qh=0, wg_qn=0;
int wifi_hw_eapol_rx(const u8* fr,int len){
    if(!wg_on||len<24+8||(fr[0]&0x0C)!=0x08||(fr[1]&0x40))return 0;
    int h=24; if(fr[0]&0x80)h+=2; if((fr[0]&0x80)&&(fr[1]&0x80))h+=4;
    if(len<h+8||fr[h]!=0xAA||fr[h+1]!=0xAA||fr[h+2]!=0x03||fr[h+6]!=0x88||fr[h+7]!=0x8E)return 0;
    for(int k=0;k<6;k++) if(fr[10+k]!=wg_bssid[k])return 0;
    int n=len-h-8; if(n>600)n=600; if(wg_qn>=4)return 1; int s=(wg_qh+wg_qn)&3; for(int i=0;i<n;i++)wg_q[s][i]=fr[h+8+i]; wg_ql[s]=n; wg_qn++; return 1; }
static void wg_tx(void* c,const u8* e,int n){ (void)c;
    const struct wifi_hw_ops* o=wifi_hw_ops_get(); if(n<0||n>1500)return;
    if(o&&o->tx_eapol){ o->tx_eapol(wifi_hw_ctx(),wg_bssid,e,n); return; }
    static u8 fr[1600]; int fl=0; fr[fl++]=0x08; fr[fl++]=0x01; fr[fl++]=0; fr[fl++]=0;
    for(int i=0;i<6;i++){ fr[fl+i]=wg_bssid[i]; fr[fl+6+i]=wg_mac[i]; fr[fl+12+i]=wg_bssid[i]; }
    fl+=18;
    fr[fl++]=0; fr[fl++]=0; fr[fl++]=0xAA; fr[fl++]=0xAA; fr[fl++]=0x03; fr[fl++]=0; fr[fl++]=0; fr[fl++]=0; fr[fl++]=0x88; fr[fl++]=0x8E;
    for(int i=0;i<n;i++){ fr[fl++]=e[i]; }
    wifi_hw_tx(fr,fl); }
static int wg_rx(void* c,u8* out,int max,int ms){ (void)c;
    u64 t0=hr_ns(), lim=(u64)ms*1000000ull;
    for(;;){ if(wg_qn){ int s=wg_qh, n=wg_ql[s]; wg_qh=(wg_qh+1)&3; wg_qn--; if(n>max)n=max; for(int i=0;i<n;i++)out[i]=wg_q[s][i]; return n; }
        if(hr_ns()-t0>=lim){ return 0; }
        wifi_hw_poll(); ui_pump(); } }
static int wg_key(void* c,int idx,int type,const u8* key,int len,int pw){ (void)c; return wifi_hw_set_key(idx,type,key,len,pw); }
static const struct wpa_io wg_io={ 0,wg_tx,wg_rx,wg_key,0,0,0,0,0,0 };
static int wg_sec=0, wg_sae=0; int g_wifi_sae_fail=0, g_wifi_sae_fallback=0; static const char* wg_pass=0;
int wifi_gen_sae_active(void){ return wg_sae&&wg_on; }
int wifi_gen_sae_auth(const struct wpa_io* io){ if(!wifi_gen_sae_active())return -2; int r=wpa_sm_sae(io,wg_bssid,wg_mac,wg_pass?wg_pass:"");
    if(r){ if(++g_wifi_sae_fail==3)klog(g_wifi_sae_fallback?"wifi: SAE failed 3 times in a row; WPA2/WPA3 mixed networks now use WPA2 (wifi saefallback off to stop)":"wifi: SAE failed 3 times in a row; staying on WPA3 (wifi saefallback on allows WPA2 on mixed networks, wifi sae off forces WPA2)"); } else g_wifi_sae_fail=0; return r; }
int wifi_hw_assoc_ie(u8* out,int cap){ if(!wg_sec||cap<32)return 0; return wpa_rsn_ie(out); }
int wifi_gen_join_secure(const u8* bssid,u8 ch,const char* ssid,u8 sec,u8 grp,u8 akm,u8 pmf,u8 ht,const char* user,const char* pass){
    int eap=(akm&4)&&!(akm&(1|2)); int sae=0; int r=wpa_sm_select(sec,grp,akm,pmf,eap,&sae); if(r)return r;
    if(sae){ const struct wifi_hw_ops* ho=wifi_hw_ops_get(); int ext=g_wifi_sae_ext&&ho&&(ho->flags&WIFI_HW_SAE)&&!(g_wifi_sae_fallback&&(akm&1)&&g_wifi_sae_fail>=3);
        if(ext){ g_wpa_akm=8; g_wpa_mfp=1; }
        else if(akm&1){ g_wpa_akm=2; g_wpa_mfp=(pmf&2)?1:0; sae=0; } else { klog(g_wifi_sae_ext?"wifi: WPA3-only (SAE) networks need SAE support in this adapter driver":"wifi: WPA3-only (SAE) network but SAE is turned off (wifi sae on)"); return 4; } }
    if(!(akm&(1|8))&&!eap&&!sae){ klog("wifi: unsupported key management on this network"); return 4; }
    wpa_sm_pmk(ssid,pass,sae,eap);
    if(wifi_hw_mac(wg_mac)<0){ return 3; }
    for(int i=0;i<6;i++){ wg_bssid[i]=bssid[i]; }
    wg_qn=0; wg_qh=0; wsm_stashlen=0; wg_sec=1; wg_on=1; wg_sae=sae; wg_pass=pass;
    r=wifi_hw_join(bssid,ch,ssid,akm&15,ht); wg_sec=0; wg_sae=0; wg_pass=0;
    if(r){ wg_on=0; return r<0?3:r; }
    if(eap){ r=wpa_sm_eap(&wg_io,user?user:"",pass); if(r){ wg_on=0; return r; } }
    r=wpa_sm_4way(&wg_io,bssid,wg_mac); wg_on=0; return r; }
