#include "include/nvx/base.h"
#define wpa_gtk_status old_wpa_gtk_status
#define g_gtk_state old_g_gtk_state
#define g_gtk_keyid old_g_gtk_keyid
#define g_gtk_len old_g_gtk_len
#define g_wifi_rekeys old_g_wifi_rekeys
#define g_wifi_ptk_rekey old_g_wifi_ptk_rekey
#define wpa_rx_eapol old_wpa_rx_eapol
#define wpa_rx_eapol_p old_wpa_rx_eapol_p
#define rtw_tx_data_eapol old_rtw_tx_data_eapol
#define rtw_rx_wait_eapol old_rtw_rx_wait_eapol
#define rtw_sec_write_cam old_rtw_sec_write_cam
#define rtw_sec_enable_engine old_rtw_sec_enable_engine
#define rtw_jlog old_rtw_jlog
#define wnd_setup old_wnd_setup
#define wnd_set_gtk old_wnd_set_gtk
#define wnd_send_eapol old_wnd_send_eapol
#define csprng_bytes test_rand
#define wpa_gtk_extract old_wpa_gtk_extract
#define g_gtk old_g_gtk
#define g_wpa_replay old_g_wpa_replay
#define g_m4 old_g_m4
#define g_m4len old_g_m4len
#define g_m4mp old_g_m4mp
#define wpa_rc old_wpa_rc
extern u8 g_ptk[48], g_pmk[32]; extern int g_wpa_ok, g_wifi_assoc_qos;
typedef struct { u8 bssid[6]; } scan_ap;
void wpa_mic(const u8* frame,int len,u8 out[32]); int wpa_key_ver(void); int wpa_rsn_ie(u8* b);
void wpa_derive_ptk_akm(const u8* pmk,const u8* aa,const u8* spa,const u8* an,const u8* sn,u8* ptk);
int aes_key_unwrap(const u8 kek[16],const u8* in,int inlen,u8* out); void csprng_bytes(u8* b,int n);
void rtw_tx_data_eapol(u32 m,const u8* bssid,const u8* mac,const u8* eapol,int elen);
int rtw_rx_wait_eapol(u32 m,const u8* bssid,u8* out,int maxout,int ms);
void rtw_sec_write_cam(u32 m,int hw_key_idx,int keyidx,int hw_key_type,int group,const u8* addr6,const u8* key16);
void rtw_sec_enable_engine(u32 m); void rtw_jlog(const char* a,const char* b,int v);
void wnd_setup(u32 m,const u8* bssid,const u8* mac,const u8* tk,int qos); void wnd_set_gtk(const u8* gtk,int keyid);
static u8 g_gtk[32]; int g_gtk_state=0, g_gtk_keyid=0, g_gtk_len=0;
const char* wpa_gtk_status(void){ switch(g_gtk_state){ case 1: return "group key (GTK) installed"; case 2: return "no GTK in msg3"; case 3: return "GTK unwrap failed";
    case 4: return "msg3 MIC mismatch - GTK skipped"; case 5: return "group cipher is not CCMP - GTK skipped"; case 6: return "GTK key index 0 clashes with the pairwise slot - skipped"; default: return "GTK not processed"; } }
static void wpa_gtk_extract(const u8* eap,int el){
    g_gtk_state=0; g_gtk_len=0;
    int flen=4+((eap[2]<<8)|eap[3]); if(flen>el)flen=el; if(flen<99){ g_gtk_state=2; return; }
    static u8 cp[512]; if(flen>512){ g_gtk_state=2; return; } for(int i=0;i<flen;i++)cp[i]=eap[i]; for(int i=81;i<97;i++)cp[i]=0;
    u8 mic[32]; wpa_mic(cp,flen,mic); for(int i=0;i<16;i++) if(mic[i]!=eap[81+i]){ g_gtk_state=4; return; }
    u16 ki=(u16)((eap[5]<<8)|eap[6]); int kdl=(eap[97]<<8)|eap[98]; if(kdl<=0||99+kdl>flen){ g_gtk_state=2; return; }
    static u8 kd[512]; int n=kdl;
    if(ki&(1u<<12)){ n=aes_key_unwrap(&g_ptk[16],eap+99,kdl,kd); if(n<0){ g_gtk_state=3; return; } }
    else for(int i=0;i<kdl;i++)kd[i]=eap[99+i];
    for(int p=0;p+2<=n;){ u8 t=kd[p], l=kd[p+1]; if(t==0xDD&&l==0)break;
        if(t==0xDD&&l>=6&&kd[p+2]==0x00&&kd[p+3]==0x0F&&kd[p+4]==0xAC&&kd[p+5]==1&&p+2+l<=n){
            g_gtk_keyid=kd[p+6]&3; g_gtk_len=l-6; if(g_gtk_len!=16){ g_gtk_state=5; return; }
            for(int i=0;i<16;i++)g_gtk[i]=kd[p+8+i]; g_gtk_state=1; return; }
        p+=2+l; }
    g_gtk_state=2;
}
static u64 g_wpa_replay=0; static u32 g_wpa_mmio=0; static u8 g_wpa_bssid[6], g_wpa_mac[6];
int g_wifi_rekeys=0, g_wifi_ptk_rekey=0;
int wnd_send_eapol(const u8* eap,int el);
static u64 wpa_rc(const u8* eap){ u64 r=0; for(int i=0;i<8;i++)r=(r<<8)|eap[9+i]; return r; }
static u8 g_m4[128]; static int g_m4len=0, g_m4mp=0;
int wpa_rx_eapol_p(const u8* eap,int el,int prot);
int wpa_rx_eapol(const u8* eap,int el){ return wpa_rx_eapol_p(eap,el,1); }
int wpa_rx_eapol_p(const u8* eap,int el,int prot){
    if(el<99||eap[1]!=3||eap[4]!=2)return 0;
    u16 ki=(u16)((eap[5]<<8)|eap[6]); u64 rc=wpa_rc(eap);
    if(rc<=g_wpa_replay&&g_wpa_replay)return 1;
    if((ki&(1u<<3))&&(ki&(1u<<8))&&(ki&(1u<<6))&&g_m4len&&el<=512){ static u8 cp[512]; int flen=4+((eap[2]<<8)|eap[3]); if(flen>el||flen<99)return 1; for(int i=0;i<flen;i++)cp[i]=eap[i]; for(int i=81;i<97;i++)cp[i]=0; u8 mc[32]; wpa_mic(cp,flen,mc); for(int i=0;i<16;i++) if(mc[i]!=eap[81+i])return 1;
        g_wpa_replay=rc; for(int i=0;i<8;i++)g_m4[9+i]=eap[9+i]; for(int i=0;i<16;i++)g_m4[g_m4mp+i]=0; wpa_mic(g_m4,g_m4len,mc); for(int i=0;i<16;i++)g_m4[g_m4mp+i]=mc[i]; if(g_wpa_mmio)rtw_tx_data_eapol(g_wpa_mmio,g_wpa_bssid,g_wpa_mac,g_m4,g_m4len); return 1; }
    if(!prot)return 1;
    if(ki&(1u<<3)){ g_wifi_ptk_rekey=1; return 1; }
    if(!(ki&(1u<<8))||!(ki&(1u<<7)))return 1;
    wpa_gtk_extract(eap,el); if(g_gtk_state!=1)return 1;
    g_wpa_replay=rc;
    if(g_gtk_keyid>0&&g_gtk_keyid<4&&g_wpa_mmio)rtw_sec_write_cam(g_wpa_mmio,g_gtk_keyid,g_gtk_keyid,4,1,g_wpa_bssid,g_gtk);
    wnd_set_gtk(g_gtk,g_gtk_keyid);
    u8 m2[100]; int ml=0; m2[ml++]=0x01; m2[ml++]=0x03; int lp=ml; m2[ml++]=0; m2[ml++]=0; m2[ml++]=0x02;
    u16 k2=(u16)((1u<<8)|(1u<<9)|(ki&7u)); m2[ml++]=(u8)(k2>>8); m2[ml++]=(u8)k2; m2[ml++]=0; m2[ml++]=0;
    for(int i=0;i<8;i++)m2[ml++]=eap[9+i]; for(int i=0;i<32+16+8+8;i++)m2[ml++]=0;
    int mp=ml; for(int i=0;i<16;i++)m2[ml++]=0; m2[ml++]=0; m2[ml++]=0;
    int b=ml-4; m2[lp]=(u8)(b>>8); m2[lp+1]=(u8)b;
    u8 mic[32]; wpa_mic(m2,ml,mic); for(int i=0;i<16;i++)m2[mp+i]=mic[i];
    wnd_send_eapol(m2,ml); g_wifi_rekeys++; return 1; }
int old_4way(u32 m,const scan_ap* ap,const u8* mac){
    u8 eap[512];
    int el=rtw_rx_wait_eapol(m,ap->bssid,eap,512,1500);
    if(el<95){ rtw_jlog(el>0?"  key message 1/4 too short":"  no key message 1/4 within 1.5 s","",el>0?el:-1); return 5; }
    rtw_jlog("  key message 1/4 received","",-1);
    const u8* kf=eap;
    u8 anonce[32]; for(int i=0;i<32;i++)anonce[i]=kf[4+1+2+2+8+i]; u64 rc1=wpa_rc(eap);
    u8 snonce[32]; csprng_bytes(snonce,32);
    wpa_derive_ptk_akm(g_pmk,ap->bssid,mac,anonce,snonce,g_ptk);
    u8 m2[128]; int i,ml=0;
    m2[ml++]=0x01; m2[ml++]=0x03;
    int lenpos=ml; m2[ml++]=0; m2[ml++]=0;
    m2[ml++]=0x02;
    u16 ki;
    ki = (u16)((1u<<3)|(1u<<8)|(u32)wpa_key_ver());
    m2[ml++]=(u8)(ki>>8); m2[ml++]=(u8)ki;
    m2[ml++]=0x00; m2[ml++]=0x00;
    for(i=0;i<8;i++)m2[ml++]=kf[4+1+2+2+i];
    for(i=0;i<32;i++)m2[ml++]=snonce[i];
    for(i=0;i<16;i++)m2[ml++]=0;
    for(i=0;i<8;i++)m2[ml++]=0;
    for(i=0;i<8;i++)m2[ml++]=0;
    int micpos=ml; for(i=0;i<16;i++)m2[ml++]=0;
    u8 rsn[32]; int rl=wpa_rsn_ie(rsn);
    m2[ml++]=(u8)(rl>>8); m2[ml++]=(u8)rl;
    for(i=0;i<rl;i++)m2[ml++]=rsn[i];
    int body2=ml-4; m2[lenpos]=(u8)(body2>>8); m2[lenpos+1]=(u8)body2;
    u8 mic[32]; wpa_mic(m2,ml,mic);
    for(i=0;i<16;i++)m2[micpos+i]=mic[i];
    rtw_tx_data_eapol(m,ap->bssid,mac,m2,ml); rtw_jlog("  key message 2/4 sent","",-1);
    for(int tries=0;;tries++){
        el=rtw_rx_wait_eapol(m,ap->bssid,eap,512,1500);
        if(el<99){ rtw_jlog("  no key message 3/4 (wrong password is the usual cause)","",-1); return 6; }
        if(wpa_rc(eap)<=rc1){ if(tries<6)continue; return 5; }
        u16 k3=(u16)((eap[5]<<8)|eap[6]);
        if(!(k3&(1u<<8))&&(k3&(1u<<7))&&(k3&(1u<<3))&&tries<3){ { int nw=0; for(i=0;i<32;i++) if(eap[17+i]!=anonce[i])nw=1; if(nw){ for(i=0;i<32;i++)anonce[i]=eap[17+i]; wpa_derive_ptk_akm(g_pmk,ap->bssid,mac,anonce,snonce,g_ptk); } } for(i=0;i<8;i++)m2[9+i]=eap[9+i]; for(i=0;i<16;i++)m2[micpos+i]=0; wpa_mic(m2,ml,mic); for(i=0;i<16;i++)m2[micpos+i]=mic[i]; rtw_tx_data_eapol(m,ap->bssid,mac,m2,ml); continue; }
        if((k3&((1u<<8)|(1u<<7)|(1u<<6)|(1u<<9)))!=((1u<<8)|(1u<<7)|(1u<<6)|(1u<<9))) return 6;
        { int same=1; for(i=0;i<32;i++) if(eap[17+i]!=anonce[i])same=0; if(!same) return 6; }
        break; }
    wpa_gtk_extract(eap,el);
    if(g_gtk_state==4) return 6;
    if(g_gtk_state==2||g_gtk_state==3) return 11;
    if(g_gtk_state==5) return 9;
    g_wpa_replay=wpa_rc(eap); g_wpa_mmio=m; for(int q=0;q<6;q++){ g_wpa_bssid[q]=ap->bssid[q]; g_wpa_mac[q]=mac[q]; }
    u8 m4[128]; ml=0;
    m4[ml++]=0x01;m4[ml++]=0x03; int lp=ml; m4[ml++]=0;m4[ml++]=0;
    m4[ml++]=0x02;
    u16 ki4=(u16)((1u<<3)|(1u<<8)|(1u<<9)|(u32)wpa_key_ver());
    m4[ml++]=(u8)(ki4>>8);m4[ml++]=(u8)ki4;
    m4[ml++]=0x00;m4[ml++]=0x00;
    for(i=0;i<8;i++)m4[ml++]=eap[4+1+2+2+i];
    for(i=0;i<32;i++)m4[ml++]=0;
    for(i=0;i<16;i++)m4[ml++]=0; for(i=0;i<8;i++)m4[ml++]=0; for(i=0;i<8;i++)m4[ml++]=0;
    int mp=ml; for(i=0;i<16;i++)m4[ml++]=0;
    m4[ml++]=0;m4[ml++]=0;
    int b4=ml-4; m4[lp]=(u8)(b4>>8); m4[lp+1]=(u8)b4;
    wpa_mic(m4,ml,mic); for(i=0;i<16;i++)m4[mp+i]=mic[i];
    rtw_tx_data_eapol(m,ap->bssid,mac,m4,ml); rtw_jlog("  key message 3/4 ok, 4/4 sent, keys installed","",-1);
    for(i=0;i<ml;i++)g_m4[i]=m4[i]; g_m4len=ml; g_m4mp=mp;
    rtw_sec_enable_engine(m);
    rtw_sec_write_cam(m, 0  , 0  , 4  , 0  , ap->bssid, &g_ptk[32]);
    int gtk_ok=g_gtk_state==1;
    if(gtk_ok){ if(g_gtk_keyid>0&&g_gtk_keyid<4) rtw_sec_write_cam(m, g_gtk_keyid, g_gtk_keyid, 4, 1, ap->bssid, g_gtk); else g_gtk_state=6; }
    wnd_setup(m,ap->bssid,mac,&g_ptk[32],g_wifi_assoc_qos);
    if(gtk_ok) wnd_set_gtk(g_gtk,g_gtk_keyid);
    g_wpa_ok=1;
    return 0;
}
