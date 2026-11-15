#include "include/nvx/base.h"
#include "include/nvx/lib/crypto/nvxwpa.h"
#include "include/nvx/lib/crypto/nvxcrypto2.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/fs/fat/fat_ata.h"
#include "include/nvx/drivers/net/wifi/rtw_efuse.h"
#include "include/nvx/drivers/net/wifi/rtw8821ce.h"
#include "include/nvx/net/wifi/wpa2.h"
#include "include/nvx/drivers/net/wifi/rtw_sec.h"
#include "include/nvx/drivers/net/wifi/rtw_chip.h"
#include "include/nvx/net/wifi/wifi_netdev.h"
#include "include/nvx/net/wifi/wpa3.h"
#include "include/nvx/net/wifi/eap.h"
#include "include/nvx/lib/crypto/nvxcrypto.h"

#define SEC_CMD_REG    0x670u
#define SEC_WRITE_REG  0x674u
#define SEC_CONFIG     0x680u
#define SEC_CMD_WRITE_ENABLE (1u<<16)
#define SEC_CMD_POLLING      (1u<<31)
#define SEC_CAM_ENTRY_SHIFT  3
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
static void rtw_sec_write_cam(u32 m,int hw_key_idx,int keyidx,int hw_key_type,
                              int group,const u8* addr6,const u8* key16){
    u32 write_cmd=SEC_CMD_WRITE_ENABLE|SEC_CMD_POLLING;
    u32 addr=(u32)hw_key_idx<<SEC_CAM_ENTRY_SHIFT;
    for(int i=7;i>=0;i--){
        u32 content;
        if(i==0){
            content=((u32)(keyidx&3)) | (((u32)hw_key_type&7)<<2) | ((u32)(group&1)<<6) |
                    (1u<<15) | ((u32)addr6[0]<<16) | ((u32)addr6[1]<<24);
        } else if(i==1){
            content=(u32)addr6[2] | ((u32)addr6[3]<<8) | ((u32)addr6[4]<<16) | ((u32)addr6[5]<<24);
        } else if(i==6||i==7){
            content=0;
        } else {
            int j=(i-2)<<2;
            content=(u32)key16[j] | ((u32)key16[j+1]<<8) | ((u32)key16[j+2]<<16) | ((u32)key16[j+3]<<24);
        }
        u32 command=write_cmd | (addr + (u32)i);
        rtw_w32(m,SEC_WRITE_REG,content);
        rtw_w32(m,SEC_CMD_REG,command);
        rtw_delay_us(100);
    }
}
static void rtw_sec_enable_engine(u32 m){
    u16 cr=rtw_r16(m,0x0100); cr|=(1u<<9); rtw_w16(m,0x0100,cr);
    u16 cfg=rtw_r16(m,SEC_CONFIG);
    cfg|=(1u<<2)|(1u<<3);
    cfg|=(1u<<0)|(1u<<1)|(1u<<6)|(1u<<7);
    rtw_w16(m,SEC_CONFIG,cfg);
}
void ui_pump(void);
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
static u32 eap_m; static const u8* eap_bssid; static const u8* eap_mac;
static int eap_tx(void* c,const u8* e,int n){ (void)c; static u8 b[1600]; if(n>1500)return -1; b[0]=1; b[1]=0; b[2]=(u8)(n>>8); b[3]=(u8)n; for(int i=0;i<n;i++)b[4+i]=e[i]; rtw_tx_data_eapol(eap_m,eap_bssid,eap_mac,b,4+n); return n; }
static int eap_rx(void* c,u8* o,int cap){ (void)c; static u8 b[1700];
    for(int t=0;t<6;t++){ int n=rtw_rx_wait_eapol(eap_m,eap_bssid,b,(int)sizeof(b),3000); if(n<4)return -1;
        if(b[1]==3){ int k=n<512?n:512; for(int i=0;i<k;i++)g_eap_stash[i]=b[i]; g_eap_stashlen=k; o[0]=3; o[1]=0; o[2]=0; o[3]=4; return 4; }
        if(b[1]!=0)continue; int L=(b[2]<<8)|b[3]; if(L>n-4)L=n-4; if(L>cap)L=cap; for(int i=0;i<L;i++)o[i]=b[4+i]; return L; }
    return -1; }
static const char* g_eap_user=0; static const char* g_eap_pass=0; static int g_eap_on=0;
int wifi_eap_join(u32 m,const scan_ap* ap,const char* ssid,const char* user,const char* pass){ g_eap_user=user; g_eap_pass=pass; g_eap_on=1; int r=rtw_wpa2_join(m,ap,ssid,pass); g_eap_on=0; return r; }
int g_wifi_prefer_sae=0;
static int rtw_sae_auth(u32 m,const scan_ap* ap,const u8* mac,const char* pass){
    static sae_ctx sc; if(sae_commit(&sc,mac,ap->bssid,pass)<0)return 2;
    u8 f[320]; int fl; u8 body[400]; int blen=0; u8 tok[128]; int tl=0;
    int sdone=0;
    for(int round=0;round<3;round++){
        fl=0; f[fl++]=0xB0; f[fl++]=0; f[fl++]=0; f[fl++]=0; for(int i=0;i<6;i++)f[fl++]=ap->bssid[i]; for(int i=0;i<6;i++)f[fl++]=mac[i]; for(int i=0;i<6;i++)f[fl++]=ap->bssid[i];
        f[fl++]=(u8)(g_seq<<4); f[fl++]=(u8)(g_seq>>4); g_seq++;
        f[fl++]=3; f[fl++]=0; f[fl++]=1; f[fl++]=0; f[fl++]=0; f[fl++]=0; f[fl++]=19; f[fl++]=0;
        for(int i=0;i<tl;i++)f[fl++]=tok[i]; for(int i=0;i<32;i++)f[fl++]=sc.scalar[i]; for(int i=0;i<64;i++)f[fl++]=sc.elem[1+i];
        int got=0;
        for(int ph=0;ph<2&&!got;ph++){ if(ph&&g_mg_via_be)break;   /* v11.10: second phase forces the data queue */
            rtw_tx_mgmt_path(m,f,fl,ph); rtw_jlog("  SAE commit sent via ",rtw_mg_last_name(),round+1);
            for(int w=0;w<(ph||g_mg_via_be?4:2)&&!got;w++){ if(!rtw_rx_wait_mgmt(m,0xB0,ap->bssid,body,&blen,600))continue; if(blen>=6&&body[0]==3&&body[2]==1)got=1; }
            if(got&&ph){ g_mg_via_be=1; rtw_jlog("  only the data queue got an answer - using it for management frames from now on","",-1); } }
        if(!got){ rtw_jlog("  no SAE commit answer, round","",round+1); return 1; }
        int st=body[4]|(body[5]<<8); rtw_jlog("  SAE commit answer, status","",st);
        if(st==76&&blen>8){ tl=blen-8; if(tl>128)tl=128; for(int i=0;i<tl;i++)tok[i]=body[8+i]; continue; }
        if(st!=0)return 2;
        if(blen<8+96||(body[6]|(body[7]<<8))!=19)return 2;
        if(sae_process_commit(&sc,body+8,body+40)<0)return 2;
        sdone=1; break; }
    if(!sdone)return 1;
    u8 cf[32]; sae_confirm(&sc,1,cf);
    fl=0; f[fl++]=0xB0; f[fl++]=0; f[fl++]=0; f[fl++]=0; for(int i=0;i<6;i++)f[fl++]=ap->bssid[i]; for(int i=0;i<6;i++)f[fl++]=mac[i]; for(int i=0;i<6;i++)f[fl++]=ap->bssid[i];
    f[fl++]=(u8)(g_seq<<4); f[fl++]=(u8)(g_seq>>4); g_seq++;
    f[fl++]=3; f[fl++]=0; f[fl++]=2; f[fl++]=0; f[fl++]=0; f[fl++]=0; f[fl++]=1; f[fl++]=0; for(int i=0;i<32;i++)f[fl++]=cf[i];
    rtw_tx_mgmt(m,f,fl);
    int got=0; for(int w=0;w<4&&!got;w++){ if(!rtw_rx_wait_mgmt(m,0xB0,ap->bssid,body,&blen,600))continue; if(blen>=6&&body[0]==3&&body[2]==2)got=1; }
    if(!got)return 6; if((body[4]|(body[5]<<8))!=0||blen<8+32)return 6;
    if(sae_check_confirm(&sc,(u16)(body[6]|(body[7]<<8)),body+8)<0)return 6;
    for(int i=0;i<32;i++)g_pmk[i]=sc.pmk[i];
    for(int i=0;i<(int)sizeof(sc);i++)((u8*)&sc)[i]=0; return 0; }
/* v11.10: send a management frame and wait for the answer, logging every step for 'wifi diag'.
   Tries 1-2 use the normal path (mgmt queue, checked on each send); tries 3-4 force the proven data queue.
   If only the data queue gets an answer, the driver keeps using it for management frames. */
static void rtw_jl3(const char* a,const char* b,const char* c,int v){ char t[RTW_JLOG_W]; int n=0; for(const char* s=a;s&&*s&&n<RTW_JLOG_W-1;s++)t[n++]=*s; for(const char* s=b;s&&*s&&n<RTW_JLOG_W-1;s++)t[n++]=*s; t[n]=0; rtw_jlog(t,c,v); }
int rtw_mgmt_xchg(u32 m,u8* f,int fl,u8 want,const u8* bssid,u8* body,int* blen,const char* what){
    for(int t=0;t<4;t++){
        if(t)f[1]|=0x08;
        int fb=(t>=2)&&!g_mg_via_be;
        rtw_tx_mgmt_path(m,f,fl,fb);
        rtw_jl3("  ",what," sent via ",-1); rtw_jl3("    ",rtw_mg_last_name(),", try",t+1);
        if(rtw_rx_wait_mgmt(m,want,bssid,body,blen,t<3?400:900)){
            f[1]&=(u8)~0x08;
            if(fb){ g_mg_via_be=1; rtw_jlog("  only the data queue got an answer - using it for management frames from now on","",-1); }
            return 1; }
        rtw_jl3("    no ",what," answer, waited ms",t<3?400:900);
    }
    f[1]&=(u8)~0x08; return 0; }
int rtw_wpa2_join(u32 m,const scan_ap* ap,const char* ssid,const char* pass){
    u8 mac[6]; for(int i=0;i<6;i++)mac[i]=rtw_r8(m,0x0610+i);
    { int z=1; for(int i=0;i<6;i++) if(mac[i])z=0; if(z&&rtw_efuse_valid){ for(int i=0;i<6;i++){mac[i]=g_rtw_efuse.mac[i]; rtw_w8(m,0x0610+i,mac[i]);} } }
    if(ap->sec==2&&ap->grp&&ap->grp!=4)return 9;
    int sae=!g_eap_on&&(ap->akm&AKM_SAE)&&(!(ap->akm&AKM_PSK)||g_wifi_prefer_sae);
    g_wpa_akm=g_eap_on?1:sae?8:(!(ap->akm&AKM_PSK)&&(ap->akm&AKM_PSK256))?6:2; g_wpa_mfp=sae||(ap->pmf&2);
    int slen=0; while(ssid[slen]&&slen<32)slen++; int plen2=0; while(pass[plen2]&&plen2<64)plen2++;
    int rawpsk=0; if(plen2==64&&!sae&&!g_eap_on){ rawpsk=1; for(int i=0;i<64;i++){ char c=pass[i]; if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))rawpsk=0; } if(rawpsk)for(int i=0;i<32;i++){ int a=pass[2*i],b=pass[2*i+1]; a=a<='9'?a-'0':(a|32)-'a'+10; b=b<='9'?b-'0':(b|32)-'a'+10; g_pmk[i]=(u8)(a*16+b); } }
    if(!sae&&!g_eap_on&&!rawpsk){ static u8 ch[32], cpmk[32]; static int cok=0; u8 h[32]; { u8 t[100]; int n=0; for(int i=0;i<slen;i++)t[n++]=(u8)ssid[i]; t[n++]=0; for(int i=0;i<plen2;i++)t[n++]=(u8)pass[i]; sha256_hash(t,n,h); for(int i=0;i<n;i++)t[i]=0; }
        int hit=cok; for(int i=0;i<32&&hit;i++) if(ch[i]!=h[i])hit=0;
        if(hit){ for(int i=0;i<32;i++)g_pmk[i]=cpmk[i]; }
        else { pbkdf2_sha1((const u8*)pass,plen2,(const u8*)ssid,slen,4096,g_pmk,32); for(int i=0;i<32;i++){ ch[i]=h[i]; cpmk[i]=g_pmk[i]; } cok=1; } }
    for(int i=0;i<6;i++)rtw_w8(m,0x0618+i,ap->bssid[i]);
    rtw_set_nettype(m,0,1);
    rtw_set_channel_2g(m,ap->ch?ap->ch:6); rtw_edca_defaults(m,ap->ch>14); rtw_chip_calibrate(m);
    rtw_rx_ring_up(m); g_eap_stashlen=0;
    u8 f[256]; int fl; u8 body[256]; int blen=0;
    if(sae){ int r=rtw_sae_auth(m,ap,mac,pass); if(r)return r; }
    else {
    fl=0; f[fl++]=0xB0;f[fl++]=0x00;f[fl++]=0;f[fl++]=0;
    for(int i=0;i<6;i++)f[fl++]=ap->bssid[i]; for(int i=0;i<6;i++)f[fl++]=mac[i]; for(int i=0;i<6;i++)f[fl++]=ap->bssid[i];
    f[fl++]=(u8)(g_seq<<4);f[fl++]=(u8)(g_seq>>4);g_seq++;
    f[fl++]=0;f[fl++]=0; f[fl++]=1;f[fl++]=0; f[fl++]=0;f[fl++]=0;
    if(!rtw_mgmt_xchg(m,f,fl,0xB0,ap->bssid,body,&blen,"auth")) return 1;
    rtw_jlog("  auth answer, status","",blen>=6?(body[4]|(body[5]<<8)):-1);
    if(blen>=6 && (body[4]|(body[5]<<8))!=0) return 2; }
    fl=0; f[fl++]=0x00;f[fl++]=0x00;f[fl++]=0;f[fl++]=0;
    for(int i=0;i<6;i++)f[fl++]=ap->bssid[i]; for(int i=0;i<6;i++)f[fl++]=mac[i]; for(int i=0;i<6;i++)f[fl++]=ap->bssid[i];
    f[fl++]=(u8)(g_seq<<4);f[fl++]=(u8)(g_seq>>4);g_seq++;
    f[fl++]=0x31;f[fl++]=0x00;
    f[fl++]=0x0A;f[fl++]=0x00;
    f[fl++]=0; f[fl++]=(u8)slen; for(int i=0;i<slen;i++)f[fl++]=(u8)ssid[i];
    { u8 rsn[32]; int rl=wpa_rsn_ie(rsn); fl+=rtw_assoc_ies_mid(f+fl,ap,rsn,rl); }
    if(!rtw_mgmt_xchg(m,f,fl,0x10,ap->bssid,body,&blen,"assoc")) return 3;
    rtw_jlog("  assoc answer, status","",blen>=4?(body[2]|(body[3]<<8)):-1);
    for(int cb=0;cb<3&&blen>=4&&(body[2]|(body[3]<<8))==30;cb++){ u32 tu=1000; for(int o=6;o+2<=blen;){ int t=body[o], l=body[o+1]; if(o+2+l>blen)break; if(t==56&&l>=5&&body[o+2]==3){ tu=(u32)body[o+3]|((u32)body[o+4]<<8)|((u32)body[o+5]<<16)|((u32)body[o+6]<<24); break; } o+=2+l; }
        if(tu<100)tu=100; if(tu>3000)tu=3000; u32 us=tu*1024u+50000u; while(us){ u32 d=us>200000u?200000u:us; rtw_delay_us(d); us-=d; ui_pump(); }
        rtw_jlog("  router asked to come back later (ms)","",(int)tu);
        rtw_tx_mgmt(m,f,fl); if(!rtw_rx_wait_mgmt(m,0x10,ap->bssid,body,&blen,900)){ rtw_jlog("    no assoc answer after coming back","",-1); return 3; }
        rtw_jlog("  assoc answer, status","",blen>=4?(body[2]|(body[3]<<8)):-1); }
    if(blen>=4 && (body[2]|(body[3]<<8))!=0) return 4;
    if(blen>=6){ rtw_w16(m,0x06A8,(u16)((rtw_r16(m,0x06A8)&~0x7FFu)|((body[4]|(body[5]<<8))&0x7FFu))); rtw_jlog("  associated, AID","",(body[4]|(body[5]<<8))&0x7FF); }
    rtw_set_nettype(m,2,1);
    if(g_eap_on){ eap_m=m; eap_bssid=ap->bssid; eap_mac=mac; u8 msk[64]; g_eap_stashlen=0; int er=eap_peap_run(eap_tx,eap_rx,0,g_eap_user,g_eap_pass,msk); if(er<0)return er==-1?1:er==-3?4:er==-4?8:er==-12?10:7; for(int i=0;i<32;i++)g_pmk[i]=msk[i]; for(int i=0;i<64;i++)msk[i]=0; }
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
int rtw_h2c_send(u32 m,int box,u32 w0,u32 w1){
    u32 boxreg,boxex;
    switch(box){ case 0:boxreg=0x01D0;boxex=0x01F0;break; case 1:boxreg=0x01D4;boxex=0x01F4;break;
                 case 2:boxreg=0x01D8;boxex=0x01F8;break; default:boxreg=0x01DC;boxex=0x01FC;break; }
    int ok=0; for(int t=0;t<300;t++){ u8 st=rtw_r8(m,0x01CC); if(!((st>>box)&1)){ok=1;break;} rtw_delay_us(100); }
    if(!ok)return -1;
    rtw_w32(m,boxex,w1);
    rtw_w32(m,boxreg,w0);
    return 0;
}
int g_wifi_ra=1, g_wifi_rssi=-127; static int g_ra_on=0, g_ra_rateid=8; int g_h2c_box=0; static u32 g_ra_mask=0xF;
static int rtw_h2c(u32 m,u32 w0,u32 w1){ int r=rtw_h2c_send(m,g_h2c_box,w0,w1); if(r==0)g_h2c_box=(g_h2c_box+1)&3; return r; }
int rtw_h2c_cmd(u32 m,u32 w0,u32 w1){ return rtw_h2c(m,w0,w1); }
void rtw_ra_rssi(u32 m,int rssi){ if(!g_ra_on)return; int v=rssi+100; if(v<0)v=0; if(v>100)v=100; rtw_h2c(m,0x42u|((u32)v<<24),0); }
void rtw_ra_connect(u32 m,int ht,const u8* rates,int nr){
    static const u8 RV[12]={2,4,11,22,12,18,24,36,48,72,96,108}; u32 mask=0;
    for(int i=0;i<nr;i++){ u8 v=rates[i]&0x7F; for(int k=0;k<12;k++) if(RV[k]==v)mask|=1u<<k; }
    if(ht)mask|=0xFFu<<12; if(g_wifi_band5)mask&=~0xFu; if(!mask)mask=g_wifi_band5?0xFF0u:0xFFFu;
    int rid=g_wifi_band5?(ht?5:7):(ht?3:((mask&0xFF0u)?6:8));
    if(rtw_h2c(m,0x01u|(1u<<8),0)<0||rtw_h2c(m,0x40u|((u32)rid<<16)|(1u<<21)|((ht&2)?(1u<<23):0)|(1u<<30),mask)<0){ g_ra_on=0; return; }
    g_ra_on=g_wifi_ra; g_ra_rateid=rid; g_ra_mask=mask; }
void rtw_sec_clear_all(u32 m){ for(int k=0;k<8;k++){ rtw_w32(m,SEC_WRITE_REG,0); rtw_w32(m,SEC_CMD_REG,SEC_CMD_WRITE_ENABLE|SEC_CMD_POLLING|((u32)k<<SEC_CAM_ENTRY_SHIFT)); } rtw_w16(m,0x06A8,(u16)(rtw_r16(m,0x06A8)&~0x7FFu)); rtw_set_nettype(m,0,0); }
void rtw_ra_disconnect(u32 m){ if(g_ra_on)rtw_h2c(m,0x01u,0); g_ra_on=0; rtw_sec_clear_all(m); }
void rtw_txdesc_rate(u8* pkt,int mgmt){
    u32 w1=*(volatile u32*)(pkt+4); w1&=~((0x1Fu<<16)|0xFFu);
    if(mgmt||!g_ra_on){ w1|=(u32)(g_wifi_band5?7u:8u)<<16; *(volatile u32*)(pkt+4)=w1; *(volatile u32*)(pkt+12)=(1u<<8)|(1u<<10); *(volatile u32*)(pkt+16)=g_wifi_band5?0x04u:0x00u; return; }
    w1|=(u32)g_ra_rateid<<16; *(volatile u32*)(pkt+4)=w1; *(volatile u32*)(pkt+12)=0; *(volatile u32*)(pkt+16)=g_wifi_band5?0x04u:0x02u; }
/* v11.10: per-queue contention settings (AIFS, CWmin, CWmax) in 0x500-0x50C, plus the response rates. rtw88 never writes these
   itself - mac80211 does it through conf_tx with its default WMM values as soon as the interface is up -
   so a bare port leaves them at the power-on values. Same values as mac80211, short slot (9 us),
   SIFS 10 us on 2.4 GHz / 16 us on 5 GHz. The TXOP bits (26:16) are left as the chip mac_init set them. */
void rtw_edca_defaults(u32 m,int band5){
    static const u8 aifsn[4]={2,2,3,7}, ecwmin[4]={2,3,4,4}, ecwmax[4]={3,4,10,10};
    u32 sifs=band5?16u:10u;
    for(int q=0;q<4;q++){ u32 reg=0x0500u+(u32)q*4u; u32 v=rtw_r32(m,reg); if(v==0xFFFFFFFFu)return;
        v=(v&0xFFFF0000u)|((u32)ecwmax[q]<<12)|((u32)ecwmin[q]<<8)|((aifsn[q]*9u+sifs)&0xFFu); rtw_w32(m,reg,v); }
    /* response (ACK) rates, rtw88 RRSR_INIT_2G / RRSR_INIT_5G: the MAC table leaves 0x15D (no 2 Mb/s, CCK on 5 GHz) */
    { u32 rr=rtw_r32(m,0x0440); if(rr!=0xFFFFFFFFu)rtw_w32(m,0x0440,(rr&~0xFFFFFu)|(band5?0x150u:0x15Fu)); } }
int g_rtw_cut=15, g_rtw_pkg=15, g_rtw_rfe=0, g_rtw_cond_set=0;
void rtw_cond_setup(u32 m){
    g_rtw_cond_set=1;
    int cm=g_rtw_chip->cond_mode;
    if(cm==RTW_COND_CHIP)return;
    u32 v=(rtw_r32(m,0x00F0)>>12)&0xFu; g_rtw_cut=v?(int)v:15;
    if(cm==RTW_COND_FULL_RFE){ g_rtw_rfe=rtw_efuse_valid?g_rtw_efuse.rfe_option:0; g_rtw_pkg=15; return; }
    g_rtw_rfe=rtw_efuse_valid?(g_rtw_efuse.rfe_option&0x1F):0;
    g_rtw_pkg=(rtw_efuse_valid&&(rtw_logmap[0xCA]&0x20))?1:15;
}
static int rtw_cond_ok(u32 c){
    u32 cut=(c>>24)&0xFu, pkg=(c>>12)&0xFu, itf=(c>>8)&0xFu, rfe=c&0xFFu;
    if(cut&&cut!=(u32)g_rtw_cut)return 0;
    if(pkg&&pkg!=(u32)g_rtw_pkg)return 0;
    if(itf&&itf!=1u)return 0;
    return rfe==(u32)g_rtw_rfe;
}
int rtw_apply_cond(u32 m,const unsigned int* t,int n,u8 sz,const char* lbl,int verify){
    char b[120],*p;
    p=b;p=scopy(p,"  [wifi ");p=scopy(p,lbl);p=scopy(p,"] table for PCIe, cut ");p=udec(p,(u32)g_rtw_cut);p=scopy(p,", RFE ");p=udec(p,(u32)g_rtw_rfe);p=scopy(p,", package ");p=udec(p,(u32)g_rtw_pkg);*p=0;
    tcol(rgb(255,210,90)); term_puts(b); term_putc('\n'); tcol(WHT);
    u32 pc=0; int matched=1, skipped=0, wr=0, mism=0;
    for(int i=0;i<n;i++){
        u32 a=t[i*2], d=t[i*2+1];
        if(a>>31){ u32 br=(a>>28)&3u; if(br==3){ matched=1; skipped=0; } else if(br==2) matched=skipped?0:1; else pc=a; continue; }
        if((a>>30)&1u){ if(!skipped){ if(rtw_cond_ok(pc)){ matched=1; skipped=1; } else { matched=0; skipped=0; } } else matched=0; continue; }
        if(!matched)continue;
        if(sz==5){ if(a!=0xFFEu&&a!=0xFEu)wr++; rtw_apply_one(m,a,sz,d); rtw_delay_us(1); continue; }
        if(rtw_apply_one(m,a,sz,d))continue;
        wr++;
        if(verify){ rtw_delay_us(50); u32 rb=rtw_rdsz(m,a,sz); if(rb!=d){ mism++;
            if(mism<=4){ tcol(rgb(230,160,60)); p=b;p=scopy(p,"    0x");p=hx(p,a,4);p=scopy(p," wrote 0x");p=hx(p,d,8);p=scopy(p," read 0x");p=hx(p,rb,8);*p=0; term_puts(b); term_putc('\n'); tcol(WHT); } } }
    }
    p=b;p=scopy(p,"  applied ");p=udec(p,(u32)wr); if(verify){p=scopy(p,"  mismatch ");p=udec(p,(u32)mism);} *p=0;
    tcol((verify&&mism)?rgb(230,160,60):GREEN); term_puts(b); term_putc('\n'); tcol(WHT);
    return mism;
}
int rtw_apply_built(u32 m,const rtw_bent* t,int n,const char* lbl,int verify){
    char b[96],*p;
    p=b;p=scopy(p,"  [wifi ");p=scopy(p,lbl);p=scopy(p,"] applying built-in table (");p=udec(p,(u32)n);p=scopy(p," writes)");*p=0;
    tcol(rgb(255,210,90)); term_puts(b); term_putc('\n'); tcol(WHT);
    int mism=0,delays=0,rfw=0;
    for(int i=0;i<n;i++){
        u32 off=t[i].off, val=t[i].val; u8 sz=t[i].sz;
        int wasdelay=rtw_apply_one(m,off,sz,val);
        if(wasdelay){ delays++; continue; }
        if(sz==5){ rfw++; continue; }
        rtw_delay_us(50);
        if(verify){ u32 rb=rtw_rdsz(m,off,sz);
            if(rb!=val){ mism++;
                if(mism<=8){ tcol(rgb(230,160,60));
                    p=b;p=scopy(p,"    0x");p=hx(p,off,4);p=scopy(p," wrote 0x");p=hx(p,val,8);p=scopy(p," read 0x");p=hx(p,rb,8);*p=0;
                    term_puts(b); term_putc('\n'); tcol(WHT); } } }
    }
    p=b;p=scopy(p,"  applied ");p=udec(p,(u32)n);
    if(rfw){p=scopy(p,"  rf ");p=udec(p,(u32)rfw);}
    if(delays){p=scopy(p,"  delays ");p=udec(p,(u32)delays);}
    if(verify){p=scopy(p,"  mismatch ");p=udec(p,(u32)mism);}
    *p=0;
    tcol((verify&&mism)?rgb(230,160,60):GREEN); term_puts(b); term_putc('\n'); tcol(WHT);
    return mism;
}

int rtw_read_table_file(const char* name,u8* out,int cap){
    if(msd_autoinit()){
        ufat_mount();
        if(ufat_ok){
            char up[16]; int k=0; while(name[k]&&k<15){ char c=name[k]; if(c>='a'&&c<='z')c-=32; up[k]=c; k++; } up[k]=0;
            for(int i=0;i<ufs_n;i++){
                char fn[16]; int j=0; while(ufs[i].name[j]&&j<15){ char c=ufs[i].name[j]; if(c>='a'&&c<='z')c-=32; fn[j]=c; j++; } fn[j]=0;
                int eq=1; for(int z=0;z<16;z++){ if(fn[z]!=up[z]){eq=0;break;} if(!fn[z]&&!up[z])break; }
                if(eq){ int r=ufat_read(i,(char*)out,cap); if(r>0)return r; }
            }
        }
    }
    return fat_read_file(name,out,cap);
}
u8 fat_filebuf[65536];
void fatls_cb(const u8* ent){ char nm[14]; int o=0; for(int i=0;i<8;i++) if(ent[i]!=' ')nm[o++]=(char)ent[i]; if(ent[8]!=' '){ nm[o++]='.'; for(int i=8;i<11;i++) if(ent[i]!=' ')nm[o++]=(char)ent[i]; } nm[o]=0;
    u32 sz=fr32(ent+28); int dir=(ent[11]&0x10)?1:0; char b[64],*p=b; p=scopy(p,"  "); p=scopy(p,nm); while((p-b)<26)*p++=' '; if(dir)p=scopy(p,"<DIR>"); else { p=udec(p,sz); p=scopy(p," B"); } *p=0; tcol(dir?ACC:WHT); term_puts(b); tcol(WHT); term_putc('\n'); }
