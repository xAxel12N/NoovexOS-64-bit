#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
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
#include "include/nvx/drivers/net/wifi/rtw_chip.h"
#include "include/nvx/drivers/net/wifi/rtw_bw.h"
#include "include/nvx/drivers/net/wifi/rtw_sec.h"
#include "include/nvx/drivers/net/wifi/rtw_chip.h"
#include "include/nvx/drivers/net/wifi/rtw_ra.h"
#include "include/nvx/net/wifi/wifi_netdev.h"
#include "include/nvx/net/wifi/wpa3.h"
#include "include/nvx/net/wifi/eap.h"
#include "include/nvx/net/wifi/wpa_sm.h"
#include "include/nvx/net/wifi/wifi_hw.h"
#include "include/nvx/lib/crypto/nvxcrypto.h"

#define SEC_CMD_REG    0x670u
#define SEC_WRITE_REG  0x674u
#define SEC_CONFIG     0x680u
#define SEC_CMD_WRITE_ENABLE (1u<<16)
#define SEC_CMD_POLLING      (1u<<31)
#define SEC_CAM_ENTRY_SHIFT  3
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
struct rtw_wio { u32 m; u8 bssid[6], mac[6]; };
static struct rtw_wio rtw_wio_c;
static void rtwio_txe(void* c,const u8* e,int n){ struct rtw_wio* w=(struct rtw_wio*)c; rtw_tx_data_eapol(w->m,w->bssid,w->mac,e,n); }
static int rtwio_rxe(void* c,u8* o,int max,int ms){ struct rtw_wio* w=(struct rtw_wio*)c; return rtw_rx_wait_eapol(w->m,w->bssid,o,max,ms); }
static int rtwio_key(void* c,int idx,int type,const u8* key,int len,int pw){ struct rtw_wio* w=(struct rtw_wio*)c; if(type!=WIFI_KEY_CCMP||len!=16)return -1;
    if(pw){ rtw_sec_enable_engine(w->m); rtw_sec_write_cam(w->m,0,0,4,0,w->bssid,key); } else rtw_sec_write_cam(w->m,idx,idx,4,1,w->bssid,key); return 0; }
static int rtwio_sae_tx(void* c,const u8* f,int n,int ph,int round){ struct rtw_wio* w=(struct rtw_wio*)c;
    if(ph<0){ rtw_tx_mgmt(w->m,f,n); return 1; }
    if(ph&&g_mg_via_be)return 0;
    rtw_tx_mgmt_path(w->m,f,n,ph); rtw_jlog("  SAE commit sent via ",rtw_mg_last_name(),round);
    return (ph||g_mg_via_be)?4:2; }
static int rtwio_rxm(void* c,u8 want,u8* body,int* blen,int ms){ struct rtw_wio* w=(struct rtw_wio*)c; return rtw_rx_wait_mgmt(w->m,want,w->bssid,body,blen,ms); }
static void rtwio_phok(void* c,int ph){ (void)c; (void)ph; g_mg_via_be=1; rtw_jlog("  only the data queue got an answer - using it for management frames from now on","",-1); }
static struct wpa_io rtw_wio={ &rtw_wio_c,rtwio_txe,rtwio_rxe,rtwio_key,rtwio_sae_tx,rtwio_rxm,rtwio_phok,&g_seq,0,0 };
static const char* g_eap_user=0; static const char* g_eap_pass=0; static int g_eap_on=0;
int wifi_eap_join(u32 m,const scan_ap* ap,const char* ssid,const char* user,const char* pass){ g_eap_user=user; g_eap_pass=pass; g_eap_on=1; int r=rtw_wpa2_join(m,ap,ssid,pass); g_eap_on=0; return r; }
/* v11.22: send a management frame and wait for the answer, logging every step for 'wifi diag'.
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
static int rtw_wpa2_join_do(u32 m,const scan_ap* ap,const char* ssid,const char* pass);
int rtw_wpa2_join(u32 m,const scan_ap* ap,const char* ssid,const char* pass){ int r=rtw_wpa2_join_do(m,ap,ssid,pass); rtw_bw_join_done(ap->bssid,r); return r; }
static int rtw_wpa2_join_do(u32 m,const scan_ap* ap,const char* ssid,const char* pass){
    u8 mac[6]; for(int i=0;i<6;i++)mac[i]=rtw_r8(m,0x0610+i);
    { int z=1; for(int i=0;i<6;i++) if(mac[i])z=0; if(z&&rtw_efuse_valid){ for(int i=0;i<6;i++){mac[i]=g_rtw_efuse.mac[i]; rtw_w8(m,0x0610+i,mac[i]);} } }
    int sae=0; { int r=wpa_sm_select(ap->sec,ap->grp,ap->akm,ap->pmf,g_eap_on,&sae); if(r)return r; }
    int slen=0; while(ssid[slen]&&slen<32)slen++;
    wpa_sm_pmk(ssid,pass,sae,g_eap_on);
    for(int i=0;i<6;i++)rtw_w8(m,0x0618+i,ap->bssid[i]);
    rtw_set_nettype(m,0,1);
    rtw_bw_join_start(ap->bssid,ap->ch?ap->ch:6,g_rtw_chip->device==0xC821); rtw_set_channel_2g(m,ap->ch?ap->ch:6); rtw_edca_defaults(m,ap->ch>14); rtw_chip_calibrate(m);
    rtw_rx_ring_up(m); g_eap_stashlen=0; wpa_sm_stash_clear();
    rtw_wio_c.m=m; for(int i=0;i<6;i++){ rtw_wio_c.bssid[i]=ap->bssid[i]; rtw_wio_c.mac[i]=mac[i]; } rtw_wio.wnd_m=m;
    u8 f[256]; int fl; u8 body[256]; int blen=0;
    if(sae){ int r=wpa_sm_sae(&rtw_wio,ap->bssid,mac,pass); if(r)return r; }
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
    if(blen>6)rtw_ra_peer_ies(body+6,blen-6);
    if(blen>=6){ rtw_w16(m,0x06A8,(u16)((rtw_r16(m,0x06A8)&~0x7FFu)|((body[4]|(body[5]<<8))&0x7FFu))); rtw_jlog("  associated, AID","",(body[4]|(body[5]<<8))&0x7FF); }
    rtw_set_nettype(m,2,1);
    if(g_eap_on){ int er=wpa_sm_eap(&rtw_wio,g_eap_user,g_eap_pass); if(er)return er; }
    rtw_wio.qos=g_wifi_assoc_qos;
    return wpa_sm_4way(&rtw_wio,ap->bssid,mac);
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
static int g_ra_ht=0, g_ra_nr=0; static u8 g_ra_rates[12];
void rtw_ra_connect(u32 m,int ht,const u8* rates,int nr){ u32 mask=0; g_ra_ht=ht; g_ra_nr=rates&&nr>0?(nr>12?12:nr):0; for(int i=0;i<g_ra_nr;i++)g_ra_rates[i]=rates[i]; int rid=rtw_ra_fw_setup(m,ht,rates,nr,&mask); if(rid<0){ g_ra_on=0; return; } g_ra_on=g_wifi_ra; g_ra_rateid=rid; g_ra_mask=mask; }
void rtw_sec_clear_all(u32 m){ for(int k=0;k<8;k++){ rtw_w32(m,SEC_WRITE_REG,0); rtw_w32(m,SEC_CMD_REG,SEC_CMD_WRITE_ENABLE|SEC_CMD_POLLING|((u32)k<<SEC_CAM_ENTRY_SHIFT)); } rtw_w16(m,0x06A8,(u16)(rtw_r16(m,0x06A8)&~0x7FFu)); rtw_set_nettype(m,0,0); }
void rtw_ra_disconnect(u32 m){ if(g_ra_on)rtw_h2c(m,0x01u,0); g_ra_on=0; rtw_sec_clear_all(m); rtw_bw_leave(); }
int rtw_bw_tick(u32 m,u64 now,u32 rx){
    int a=rtw_bw_drop_step(now,rx);
    if(a==RTW_BWD_NARROW){ u8 f[32], sa[6]; for(int i=0;i<6;i++)sa[i]=rtw_r8(m,0x0610+i); int c80=g_rtw_bw_cur==RTW_BW_80;
        rtw_tx_mgmt(m,f,rtw_bw_action(f,g_rtw_bw_bssid,sa,0)); if(c80)rtw_tx_mgmt(m,f,rtw_bw_action(f,g_rtw_bw_bssid,sa,1)); rtw_tx_mgmt_drain(m);
        rtw_bw_narrowed(now,rx); rtw_set_channel_2g(m,g_rtw_bw_jch);
        if(g_ra_on){ rtw_ra_ies_restore(); rtw_ra_connect(m,g_ra_ht,g_ra_nr?g_ra_rates:0,g_ra_nr); }
        klog("wifi: only low rates on a wide channel - told the access point and switched to 20 MHz without reconnecting"); return 1; }
    if(a==RTW_BWD_OK){ klog("wifi: 20 MHz in session works, staying connected"); return 0; }
    if(a==RTW_BWD_REJOIN){ klog("wifi: no data after switching to 20 MHz - reconnecting at 20 MHz"); return -1; }
    return 0; }
void rtw_txdesc_rate(u8* pkt,int mgmt){
    u32 w1=*(volatile u32*)(pkt+4); w1&=~((0x1Fu<<16)|0xFFu);
    if(mgmt||!g_ra_on){ w1|=(u32)(g_wifi_band5?7u:8u)<<16; *(volatile u32*)(pkt+4)=w1; *(volatile u32*)(pkt+12)=(1u<<8)|(1u<<10); *(volatile u32*)(pkt+16)=g_wifi_band5?0x04u:0x00u; return; }
    w1|=(u32)g_ra_rateid<<16; *(volatile u32*)(pkt+4)=w1; *(volatile u32*)(pkt+12)=0; *(volatile u32*)(pkt+16)=g_wifi_band5?0x04u:0x02u; *(volatile u32*)(pkt+20)=(*(volatile u32*)(pkt+20)&~(3u<<5))|((u32)(g_rtw_bw_cur&3u)<<5); }
/* v11.22: per-queue contention settings (AIFS, CWmin, CWmax) in 0x500-0x50C, plus the response rates. rtw88 never writes these
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
