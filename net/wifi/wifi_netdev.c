#include "include/nvx/base.h"
#include "include/nvx/lib/crypto/nvxwpa.h"
#include "include/nvx/lib/crypto/nvxcrypto2.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/drivers/net/ethernet/nic.h"
#include "include/nvx/drivers/storage/ahci.h"
#include "include/nvx/net/stack/tcpip.h"
#include "include/nvx/drivers/net/wifi/rtw_efuse.h"
#include "include/nvx/drivers/net/wifi/rtw8821ce.h"
#include "include/nvx/drivers/net/wifi/rtw_chip.h"
#include "include/nvx/net/wifi/wifi_netdev.h"
#include "include/nvx/net/wifi/wifi_hw.h"
#include "include/nvx/net/wifi/wifi_ampdu.h"
#include "include/nvx/net/wifi/wifi_ps.h"
static u8  g_wnd_mac[6], g_wnd_bssid[6];
int g_wnd_up=0; static int g_wnd_qos=0;
static u64 g_wnd_tx_pn=1;
static u64 g_wnd_rx_pn_uni[17], g_wnd_rx_pn_grp[4];
extern volatile u64 g_ticks;
u64 g_wnd_beacon_t=0; int g_wnd_lost=0, g_wnd_beacons=0; u32 g_wnd_bcn_int=100;
static int g_wsq_on;
static int wnd_gbatx(void);
int wpa_rx_eapol(const u8* eap,int el); int wpa_rx_eapol_p(const u8* eap,int el,int prot);
int rtw_tx_mgmt(u32 m,const u8* frame,int fl);
static u8  g_wnd_tk[16], g_wnd_gtk[16]; int g_wnd_have_gtk=0, g_wnd_gtk_id=1;
static u32 g_wnd_mmio=0; int g_wnd_open=0; void (*g_wnd_pre_tx)(void)=0;
static int g_wnd_txfail=0;
u32 g_wnd_rx_frames=0, g_wnd_rx_drop=0, g_wnd_tx_frames=0, g_wnd_rx_uc=0;

static u64 g_wnd_mpn=0;
static int g_wnd_htp=-1, g_wnd_ra_seen=0, g_wnd_barf_set=0; int g_wnd_barf=1;
void wnd_setup(u32 m,const u8* bssid,const u8* mac,const u8* tk,int qos){
    g_wnd_mpn=0;
    g_wnd_mmio=m; for(int i=0;i<6;i++){ g_wnd_bssid[i]=bssid[i]; g_wnd_mac[i]=mac[i]; }
    for(int i=0;i<16;i++)g_wnd_tk[i]=tk[i];
    g_wnd_tx_pn=1; for(int i=0;i<17;i++)g_wnd_rx_pn_uni[i]=0; for(int i=0;i<4;i++)g_wnd_rx_pn_grp[i]=0; g_wnd_qos=qos; g_wnd_up=1; g_wnd_txfail=0; g_wnd_lost=0; g_wnd_beacon_t=g_ticks; g_wnd_beacons=0;
    g_wnd_rx_frames=g_wnd_rx_drop=g_wnd_tx_frames=g_wnd_rx_uc=0; g_wnd_open=0; ampdu_reset(); ampdu_tx_reset(g_ticks); g_wnd_htp=-1; g_wnd_ra_seen=0;
}
void wnd_setup_open(u32 m,const u8* bssid,const u8* mac,int qos){ u8 z[16]={0}; wnd_setup(m,bssid,mac,z,qos); g_wnd_open=1; }
void wnd_set_gtk(const u8* gtk,int keyid){ int same=g_wnd_have_gtk&&g_wnd_gtk_id==keyid; for(int i=0;i<16;i++){ if(g_wnd_gtk[i]!=gtk[i])same=0; g_wnd_gtk[i]=gtk[i]; } g_wnd_gtk_id=keyid; g_wnd_have_gtk=1; if(!same)g_wnd_rx_pn_grp[keyid&3]=0; }
void wnd_down(void){ g_wnd_up=0; g_wnd_have_gtk=0; g_wsq_on=0; ampdu_reset(); ampdu_tx_stop(); if(g_wnd_barf_set&&g_wnd_mmio){ u32 r=rtw_r32(g_wnd_mmio,0x0608); if(r!=0xFFFFFFFFu)rtw_w32(g_wnd_mmio,0x0608,r&~(1u<<12)); } g_wnd_barf_set=0; }
int wnd_is_up(void){ return g_wnd_up; }
const u8* wnd_bssid(void){ return g_wnd_bssid; }

static void ccmp_nonce(u8* nonce,const u8* a2,u64 pn,u8 prio){
    nonce[0]=prio&0x0F; for(int i=0;i<6;i++)nonce[1+i]=a2[i];
    nonce[7]=(u8)(pn>>40); nonce[8]=(u8)(pn>>32); nonce[9]=(u8)(pn>>24);
    nonce[10]=(u8)(pn>>16); nonce[11]=(u8)(pn>>8); nonce[12]=(u8)pn;
}
static void ccmp_hdr(u8* h,u64 pn,int keyid){
    h[0]=(u8)pn; h[1]=(u8)(pn>>8); h[2]=0; h[3]=(u8)(0x20|((keyid&3)<<6));
    h[4]=(u8)(pn>>16); h[5]=(u8)(pn>>24); h[6]=(u8)(pn>>32); h[7]=(u8)(pn>>40);
}

u32 g_wnd_dead=0, g_wnd_txfull=0;
static int wnd_tx_eth(const u8* eth,int len){
    if(!g_wnd_up||len<14||len>1514) return -1;
    if(g_wnd_pre_tx)g_wnd_pre_tx();
    int gen=!g_wnd_mmio&&wifi_hw_generic();
    if(!gen&&!g_benum)return -1;
    if(!gen){ u32 raw=rtw_r32(g_wnd_mmio,0x03A8); if(raw==0xFFFFFFFFu){ g_wnd_dead++; return -1; }
      u32 nx=(g_beidx+1u)%g_benum; int full=1; for(int w=0;w<200;w++){ u32 hr=((rtw_r32(g_wnd_mmio,0x03A8)>>16)&0x0FFFu)%g_benum; if(nx!=hr){ full=0; break; } io_wait(); io_wait(); io_wait(); io_wait(); io_wait(); }
      if(full){ g_wnd_txfull++; return -1; } }
    static u8* pkts[256]; u8* pkt=0; if(!gen){ u32 slot=(g_beidx%g_benum)&255u; if(!pkts[slot]){ pkts[slot]=(u8*)dma_alloc(2048); if(!pkts[slot])return -1; } pkt=pkts[slot]; }
    const u8* dst=eth; const u8* src=eth+6; u16 etype=((u16)eth[12]<<8)|eth[13];
    const u8* payload=eth+14; int plen=len-14;

    static u8 fr[2048]; int fl=0;
    int qos=g_wnd_qos; int swc=!g_wnd_open&&!(gen&&(wifi_hw_ops_get()->flags&WIFI_HW_TXENC));
    fr[fl++]=qos?0x88:0x08; fr[fl++]=swc?0x41:0x01;
    fr[fl++]=0; fr[fl++]=0;
    for(int i=0;i<6;i++)fr[fl++]=g_wnd_bssid[i];
    for(int i=0;i<6;i++)fr[fl++]=g_wnd_mac[i];
    for(int i=0;i<6;i++)fr[fl++]=dst[i];
    fr[fl++]=0; fr[fl++]=0;
    u8 prio=0; if(qos){ fr[fl++]=0; fr[fl++]=0; }
    int agg=qos&&ampdu_tx_active()&&g_wnd_htp>=0&&(!gen||wnd_gbatx()); u16 aseq=0; if(agg){ aseq=ampdu_tx_seq(); fr[22]=(u8)(aseq<<4); fr[23]=(u8)(aseq>>4); }
    int hdrlen=fl;

    u64 pn=0; if(swc){ pn=g_wnd_tx_pn++; u8 cc[8]; ccmp_hdr(cc,pn,0); for(int i=0;i<8;i++)fr[fl++]=cc[i]; }

    int dataoff=fl;
    fr[fl++]=0xAA; fr[fl++]=0xAA; fr[fl++]=0x03; fr[fl++]=0x00; fr[fl++]=0x00; fr[fl++]=0x00;
    fr[fl++]=(u8)(etype>>8); fr[fl++]=(u8)etype;
    for(int i=0;i<plen;i++)fr[fl++]=payload[i];
    int dlen=fl-dataoff;

    if(swc){ u8 aad[32]; int al=0;
    aad[al++]=fr[0]&0x8F; aad[al++]=(fr[0]&0x80)?(fr[1]&0x47):(fr[1]&0xC7);
    for(int i=0;i<6;i++)aad[al++]=fr[4+i];
    for(int i=0;i<6;i++)aad[al++]=fr[10+i];
    for(int i=0;i<6;i++)aad[al++]=fr[16+i];
    aad[al++]=fr[22]&0x0F; aad[al++]=0;
    if(qos){ aad[al++]=prio; aad[al++]=0; }
    u8 nonce[13]; ccmp_nonce(nonce,g_wnd_mac,pn,prio);
    u8 mic[8]; aes_ccm_auth_encrypt(g_wnd_tk,nonce,aad,al,fr+dataoff,dlen,mic);
    for(int i=0;i<8;i++)fr[fl++]=mic[i]; }

    if(gen){ if(wifi_hw_tx(fr,fl)<0){ if(agg)ampdu_tx_unseq(aseq); g_wnd_txfull++; return -1; } g_wnd_tx_frames++; return len; }
    for(u32 i=0;i<rtw_txd_sz();i++)pkt[i]=0;
    u32 w0=((u32)fl&0xFFFFu)|((rtw_txd_sz()&0xFFu)<<16)|(1u<<26);
    *(volatile u32*)(pkt+0)=w0;
    *(volatile u32*)(pkt+4)=((u32)0u<<8)|((u32)8u<<16);
    *(volatile u32*)(pkt+12)=(1u<<8)|(1u<<10);
    *(volatile u32*)(pkt+16)=0x00u;
    rtw_txdesc_rate(pkt,0);
    *(volatile u32*)(pkt+32)=(1u<<15);
    { int fw=!(*(volatile u32*)(pkt+12)&(1u<<8)); g_wnd_ra_seen=fw;
      if(agg){ u32 f=(4u<<(g_wnd_htp&3))-1u, w=ampdu_tx_win(); if(w&&f>w)f=w;
        if(fw){ *(volatile u32*)(pkt+8)|=(1u<<12)|((u32)((g_wnd_htp>>2)&7)<<20); *(volatile u32*)(pkt+12)|=(f&0x1Fu)<<17; }
        *(volatile u32*)(pkt+32)=0; *(volatile u32*)(pkt+36)=(u32)aseq<<12; } }
    rtw_desc_csum(pkt);
    for(int i=0;i<fl;i++)pkt[rtw_txd_sz()+i]=fr[i];

    u32 total=rtw_txd_sz()+(u32)fl;
    u32 idx=g_beidx % g_benum; u8* bd=g_bering+idx*16;
    u32 psb=((total-1)/128)+1;
    *(volatile u16*)(bd+0)=(u16)rtw_txd_sz(); *(volatile u16*)(bd+2)=(u16)psb; *(volatile u32*)(bd+4)=(u32)(u64)pkt;
    *(volatile u16*)(bd+8)=(u16)fl; *(volatile u16*)(bd+10)=0; *(volatile u32*)(bd+12)=(u32)(u64)(pkt+rtw_txd_sz());
    cpu_wmb();
    g_beidx=(g_beidx+1)%g_benum; rtw_w16(g_wnd_mmio,0x03A8,(u16)g_beidx);
    g_wnd_tx_frames++;
    return len;
}

static int g_wnd_mdec;
static int wnd_eq6(const u8* a,const u8* b){ for(int i=0;i<6;i++) if(a[i]!=b[i])return 0; return 1; }
static void wnd_tx_mgmt(const u8* f,int n){ if(g_wnd_mmio)rtw_tx_mgmt(g_wnd_mmio,f,n); else if(wifi_hw_generic())wifi_hw_tx(f,n); }
extern int iwx_ba_rx(int tid,u16 ssn,u16 win,int start) __attribute__((weak));
extern int iwx_ba_tx(int tid,u16 ssn,u16 win,int start) __attribute__((weak));
int wnd_tx_null(int pm){ if(!g_wnd_up)return -1; u8 f[24]; wps_null(f,g_wnd_bssid,g_wnd_mac,pm); wnd_tx_mgmt(f,24); return 0; }
static int wnd_mfp(void){ extern int g_wpa_mfp; return g_wpa_mfp&&!g_wnd_open; }
static u16 g_wsq_id; static int g_wsq_on=0, g_wsq_reason=0; static u64 g_wsq_t0=0, g_wsq_last=0; u32 g_wnd_saq_ans=0, g_wnd_saq_req=0, g_wnd_saq_ok=0, g_wnd_saq_lost=0;
static void wnd_tx_action(const u8* b,int bl){
    u8 f[96]; int n=24; if(bl<2||bl>48||!g_wnd_up)return; int p=wnd_mfp(); f[0]=0xD0; f[1]=p?0x40:0; f[2]=0; f[3]=0;
    for(int i=0;i<6;i++){ f[4+i]=g_wnd_bssid[i]; f[10+i]=g_wnd_mac[i]; f[16+i]=g_wnd_bssid[i]; } f[22]=0; f[23]=0;
    if(p){ u64 pn=g_wnd_tx_pn++; ccmp_hdr(f+24,pn,0); n=32; for(int i=0;i<bl;i++)f[n+i]=b[i];
        u8 aad[22]; int al=0; aad[al++]=f[0]; aad[al++]=f[1]&0xC7; for(int i=0;i<18;i++)aad[al++]=f[4+i]; aad[al++]=0; aad[al++]=0;
        u8 nonce[13]; ccmp_nonce(nonce,g_wnd_mac,pn,0); nonce[0]=0x10; u8 mic[8]; aes_ccm_auth_encrypt(g_wnd_tk,nonce,aad,al,f+n,bl,mic); n+=bl; for(int i=0;i<8;i++)f[n++]=mic[i]; g_ampdu_prot++; }
    else { for(int i=0;i<bl;i++)f[n+i]=b[i]; n+=bl; }
    wnd_tx_mgmt(f,n); }
static void wnd_delba(int tid,int init){ u16 pr=(u16)((init?0x0800u:0)|((u32)tid<<12)); u8 b[6]={3,2,(u8)pr,(u8)(pr>>8),37,0}; wnd_tx_action(b,6); }
void wnd_delba_rx(int tid){ if(g_wnd_up&&tid>=0&&tid<16){ wnd_delba(tid,0); g_ampdu_decl++; } }
void wnd_delba_tx(void){ if(g_wnd_up&&ampdu_tx_stop())wnd_delba(0,1); }
void wnd_delba_all(void){ if(!g_wnd_up)return; if(ampdu_tx_stop())wnd_delba(0,1); for(int t=0;t<16;t++)if(ampdu_active(t)){ wnd_delba(t,0); ampdu_delba(t); } }
static void wnd_barf(void){ if(!g_wnd_mmio||!g_wnd_barf||g_wnd_barf_set)return; u32 r=rtw_r32(g_wnd_mmio,0x0608); if(r==0xFFFFFFFFu)return; rtw_w16(g_wnd_mmio,0x06A2,0x0100); rtw_w32(g_wnd_mmio,0x0608,r|(1u<<12)); g_wnd_barf_set=1; }
static void wnd_rx_bar(const u8* fr,int len){ if(len<20||!g_wnd_up||!wnd_eq6(fr+4,g_wnd_mac)||!wnd_eq6(fr+10,g_wnd_bssid))return; u16 c=(u16)(fr[16]|(fr[17]<<8)); if(c&2)return; ampdu_bar(c>>12,(u16)((fr[18]|(fr[19]<<8))>>4)); }
static int wnd_gbatx(void){ int (*volatile f)(int,u16,u16,int)=iwx_ba_tx; return !g_wnd_mmio&&f&&wifi_hw_generic(); }
static void wnd_batx(void){
    int gb=wnd_gbatx();
    if(!g_wnd_up||(!g_wnd_mmio&&!gb)||!g_wnd_qos)return;
    ampdu_tx_tick(g_ticks);
    if(ampdu_tx_watch(g_wnd_tx_frames,g_wnd_rx_frames,g_ticks)){ wnd_delba_tx(); ampdu_tx_bad(g_ticks); return; }
    if(!gb){ extern int g_wifi_assoc_ht; if(!g_wifi_assoc_ht)return; }
    if(g_wnd_htp<0||(!gb&&!g_wnd_ra_seen)||!ampdu_tx_want(g_ticks))return;
    u32 win=AMPDU_WIN; if(gb){ int w=iwx_ba_tx(0,0,AMPDU_WIN,1); if(w<0){ ampdu_tx_bad(g_ticks); return; } if(!w)return; if((u32)w<win)win=(u32)w; }
    u8 tok=ampdu_tx_sent(g_ticks); u16 pr=(u16)(2u|(win<<6)); u8 b[9]={3,0,tok,(u8)pr,(u8)(pr>>8),0,0,0,0}; wnd_tx_action(b,9); }
static void wnd_addba(const u8* fr,int len){
    u16 prm=(u16)(fr[27]|(fr[28]<<8)); int tid=(prm>>2)&15, win=0;
    if(len<24+9)return;
    if(g_ampdu_on&&g_wnd_qos&&(prm&2))win=ampdu_addba(tid,(u16)((fr[31]|(fr[32]<<8))>>4),(u16)(prm>>6),g_ticks);
    else g_ampdu_decl++;
    if(win&&iwx_ba_rx&&iwx_ba_rx(tid,(u16)((fr[31]|(fr[32]<<8))>>4),(u16)win,1)<0){ ampdu_delba(tid); g_ampdu_decl++; win=0; }
    if(win)wnd_barf();
    u16 rp=win?(u16)(2u|((u32)tid<<2)|((u32)win<<6)):prm;
    u8 b[9]={3,1,fr[26],(u8)(win?0:37),0,(u8)rp,(u8)(rp>>8),(u8)(win?fr[29]:0),(u8)(win?fr[30]:0)};
    wnd_tx_action(b,9); }
static void wnd_rx_action(const u8* fr,int len){
    static u8 pf[96]; int p=(fr[1]&0x40)!=0;
    if(wnd_mfp()!=p)return;
    if(p){ if(len<24+8+2+8)return; const u8* cc=fr+24; if(!(cc[3]&0x20))return;
        u64 pn=((u64)cc[0])|((u64)cc[1]<<8)|((u64)cc[4]<<16)|((u64)cc[5]<<24)|((u64)cc[6]<<32)|((u64)cc[7]<<40); if(pn<=g_wnd_mpn)return;
        if(g_wnd_mdec){ int dl=len-24-8; if(dl<2||dl>64)return; g_wnd_mpn=pn; for(int i=0;i<24;i++)pf[i]=fr[i]; for(int i=0;i<dl;i++)pf[24+i]=fr[32+i]; pf[1]&=(u8)~0x40; fr=pf; len=24+dl; goto plain; }
        int dl=len-24-8-8; if(dl<2||dl>64)return;
        u8 aad[22]; int al=0; aad[al++]=fr[0]; aad[al++]=fr[1]&0xC7; for(int i=0;i<18;i++)aad[al++]=fr[4+i]; aad[al++]=fr[22]&0x0F; aad[al++]=0;
        u8 nonce[13]; ccmp_nonce(nonce,fr+10,pn,0); nonce[0]=0x10; for(int i=0;i<dl;i++)pf[24+i]=fr[32+i]; u8 mic[8]; for(int i=0;i<8;i++)mic[i]=fr[32+dl+i];
        if(aes_ccm_decrypt_verify(g_wnd_tk,nonce,aad,al,pf+24,dl,mic)!=1)return;
        g_wnd_mpn=pn; g_ampdu_prot++;
        for(int i=0;i<24;i++){ pf[i]=fr[i]; } pf[1]&=(u8)~0x40; fr=pf; len=24+dl; }
    plain:
    if(len>=24+4&&fr[24]==8){ if(!p)return; if(fr[25]==0){ u8 b[4]={8,1,fr[26],fr[27]}; wnd_tx_action(b,4); g_wnd_saq_ans++; } else if(fr[25]==1&&g_wsq_on&&(u16)(fr[26]|(fr[27]<<8))==g_wsq_id){ g_wsq_on=0; g_wnd_saq_ok++; } return; }
    if(len<24+2||fr[24]!=3)return;
    if(fr[25]==0&&len>=24+9)wnd_addba(fr,len);
    else if(fr[25]==1&&len>=24+9)ampdu_tx_resp(fr[26],(u16)(fr[27]|(fr[28]<<8)),(u16)(fr[29]|(fr[30]<<8)),g_ticks);
    else if(fr[25]==2&&len>=24+6){ if(fr[27]&0x08){ int t=fr[27]>>4; int a=ampdu_active(t); ampdu_delba(t); if(a&&iwx_ba_rx&&t<8)iwx_ba_rx(t,0,0,0); } else if((fr[27]>>4)==0)ampdu_tx_peer_delba(g_ticks); } }
static void wnd_rx_mgmt(const u8* base,int rssi,const u8* fr,int len){
    u8 st=fr[0]&0xF0; if(len<24)return; const u8* a1=fr+4; const u8* a2=fr+10;
    if(!wnd_eq6(a2,g_wnd_bssid))return;
    if(st==0x80){ g_wnd_beacon_t=g_ticks; g_wnd_beacons++; int r=base?rtw_rx_rssi(base):rssi; if(r>-127)g_wifi_rssi=g_wifi_rssi<=-127?r:(g_wifi_rssi*7+r)/8; if(len>=24+12){ u32 bi=(u32)(fr[32]|(fr[33]<<8)); if(bi>=10&&bi<=1000)g_wnd_bcn_int=bi; } if(g_wnd_htp<0)for(int o=36;o+2<=len;){ int tl=fr[o+1]; if(o+2+tl>len)break; if(fr[o]==45&&tl>=3){ g_wnd_htp=fr[o+4]&0x1F; break; } o+=2+tl; } wps_rx_beacon(fr,len); return; }
    int tome=wnd_eq6(a1,g_wnd_mac)||(a1[0]==0xFF&&a1[1]==0xFF);
    if((st==0xC0||st==0xA0)&&tome){ extern int g_wpa_mfp;
        if(g_wpa_mfp&&!g_wnd_open){ if(!(fr[1]&0x40)&&wnd_eq6(a1,g_wnd_mac)&&len>=26&&!g_wsq_on){ int rc=fr[24]|(fr[25]<<8); if(rc==6||rc==7){ u8 id[2]; csprng_bytes(id,2); g_wsq_id=(u16)(id[0]|(id[1]<<8)); g_wsq_on=1; g_wsq_reason=rc; g_wsq_t0=g_wsq_last=g_ticks; u8 b[4]={8,0,id[0],id[1]}; wnd_tx_action(b,4); g_wnd_saq_req++; } return; }
            if(!(fr[1]&0x40)||!wnd_eq6(a1,g_wnd_mac)||len<24+8+2+(g_wnd_mdec?0:8))return;
            const u8* cc=fr+24; if(!(cc[3]&0x20))return; u64 pn=((u64)cc[0])|((u64)cc[1]<<8)|((u64)cc[4]<<16)|((u64)cc[5]<<24)|((u64)cc[6]<<32)|((u64)cc[7]<<40); u64 mpn=g_wnd_mpn; if(pn<=mpn)return;
            if(g_wnd_mdec){ g_wnd_mpn=pn; g_wnd_lost=fr[32]|(fr[33]<<8); if(!g_wnd_lost)g_wnd_lost=1; wnd_down(); return; }
            u8 aad[22]; int al=0; aad[al++]=fr[0]; aad[al++]=fr[1]&0xC7; for(int i=0;i<18;i++)aad[al++]=fr[4+i]; aad[al++]=fr[22]&0x0F; aad[al++]=0;
            u8 nonce[13]; ccmp_nonce(nonce,a2,pn,0); nonce[0]=0x10; int dl=len-24-8-8; if(dl<2||dl>64)return; u8 pt[64]; for(int i=0;i<dl;i++)pt[i]=fr[32+i]; u8 mic[8]; for(int i=0;i<8;i++)mic[i]=fr[32+dl+i];
            if(aes_ccm_decrypt_verify(g_wnd_tk,nonce,aad,al,pt,dl,mic)!=1)return; g_wnd_mpn=pn; g_wnd_lost=pt[0]|(pt[1]<<8); if(!g_wnd_lost)g_wnd_lost=1; wnd_down(); return; } g_wnd_lost=len>=26?(fr[24]|(fr[25]<<8)):1; if(!g_wnd_lost)g_wnd_lost=1; wnd_down(); return; }
    if(st==0xD0&&wnd_eq6(a1,g_wnd_mac))wnd_rx_action(fr,len); }
void wnd_rx_mgmt_hw(const u8* fr,int len,int rssi){ if(g_wnd_up)wnd_rx_mgmt(0,rssi,fr,len); }
void wnd_rx_mgmt_hw2(const u8* fr,int len,int rssi,int dec){ if(!g_wnd_up)return; g_wnd_mdec=dec?1:0; wnd_rx_mgmt(0,rssi,fr,len); g_wnd_mdec=0; }
void wnd_saq_tick(void){ if(!g_wsq_on)return; if(!g_wnd_up||!wnd_mfp()){ g_wsq_on=0; return; }
    if(g_ticks-g_wsq_t0>=100){ g_wsq_on=0; g_wnd_saq_lost++; g_wnd_lost=g_wsq_reason?g_wsq_reason:7; wnd_down(); return; }
    if(g_ticks-g_wsq_last>=20){ g_wsq_last=g_ticks; u8 b[4]={8,0,(u8)g_wsq_id,(u8)(g_wsq_id>>8)}; wnd_tx_action(b,4); g_wnd_saq_req++; } }
u64 g_wnd_eapol_t=0;
int wnd_send_eapol(const u8* eap,int el){ static u8 f[600]; if(el>580||!g_wnd_up)return -1; g_wnd_eapol_t=g_ticks; for(int i=0;i<6;i++){ f[i]=g_wnd_bssid[i]; f[6+i]=g_wnd_mac[i]; } f[12]=0x88; f[13]=0x8E; for(int i=0;i<el;i++)f[14+i]=eap[i]; return wnd_tx_eth(f,14+el); }
static u8 g_amq[8][1600]; static int g_amql[8], g_amqh=0, g_amqn=0;
static int wnd_amq_pop(u8* out,int max){ if(!g_amqn)return 0; int L=g_amql[g_amqh]; int r=0; if(L>max)g_wnd_rx_drop++; if(L<=max){ for(int i=0;i<L;i++)out[i]=g_amq[g_amqh][i]; r=L; } g_amqh=(g_amqh+1)&7; g_amqn--; return r; }
static int wnd_amsdu(const u8* p,int n,u8* out,int max){
    int off=0;
    if(n>=6&&p[0]==0xAA&&p[1]==0xAA&&p[2]==0x03&&p[3]==0&&p[4]==0&&p[5]==0){ g_wnd_rx_drop++; return 0; }
    while(off+14<=n){
        int ln=(p[off+12]<<8)|p[off+13]; if(ln<8||off+14+ln>n)break;
        const u8* sub=p+off+14;
        if(g_amqn>=8||14+ln-8>1600)g_wnd_rx_drop++;
        else if(sub[0]==0xAA&&sub[1]==0xAA&&sub[2]==0x03){
            u16 et=(u16)((sub[6]<<8)|sub[7]);
            if(et!=0x888E){ int t=(g_amqh+g_amqn)&7; u8* q=g_amq[t]; for(int i=0;i<12;i++)q[i]=p[off+i]; q[12]=(u8)(et>>8); q[13]=(u8)et; for(int i=0;i<ln-8;i++)q[14+i]=sub[8+i]; g_amql[t]=14+ln-8; g_amqn++; g_wnd_rx_frames++; } }
        off+=14+ln; off=(off+3)&~3;
    }
    return wnd_amq_pop(out,max);
}
static int wnd_ba_pop(u8* out,int max){ u64 pn; int tid,r;
    while((r=ampdu_pop(out,max,&pn,&tid))>=0){ if(pn){ u64* ps=&g_wnd_rx_pn_uni[tid&15]; if(pn<=*ps){ g_wnd_rx_drop++; continue; } *ps=pn; } if(r>0){ g_wnd_rx_frames++; return r; } }
    return 0; }
static u8 g_wnd_bae[AMPDU_SLOT];
static int wnd_ba_put(int tid,u16 seq,const u8* fr,const u8* llc,int dl,u64 pn,u8* out,int max){ int el=0;
    if(llc&&dl>=8&&llc[0]==0xAA&&llc[1]==0xAA&&llc[2]==0x03){ u16 et=(u16)((llc[6]<<8)|llc[7]);
        if(et==0x888E){ if(ampdu_rx(tid,seq,0,0,pn,g_ticks)!=2&&pn)wpa_rx_eapol(llc+8,dl-8); return wnd_ba_pop(out,max); }
        else if(14+dl-8<=AMPDU_SLOT){ for(int i=0;i<6;i++){ g_wnd_bae[i]=fr[4+i]; g_wnd_bae[6+i]=fr[16+i]; } g_wnd_bae[12]=(u8)(et>>8); g_wnd_bae[13]=(u8)et; for(int i=0;i<dl-8;i++)g_wnd_bae[14+i]=llc[8+i]; el=14+dl-8; } }
    ampdu_rx(tid,seq,g_wnd_bae,el,pn,g_ticks); return wnd_ba_pop(out,max); }
static int wnd_rx_frame(const u8* fr,int fe,int hwhint,u8* out,int max){
    int outlen=0;
    u8 fc=fr[0]; u8 fc2=fr[1];
    if((fc&0x0C)==0x08 && (fc2&0x02)){
        if(!wnd_eq6(fr+10,g_wnd_bssid))return outlen;   /* v10.99: another network's frame - no decrypt attempt */
        u32 hlen=24u; if((fc&0x80))hlen+=2u;
        if((fc&0x80)&&(fc2&0x80))hlen+=4u;
        u8 prio=(fc&0x80)?(fr[24]&0x0F):0;
        int ba=(fc&0xF0)==0x80&&!(fr[4]&1)&&ampdu_active(prio&15); u16 bseq=(u16)((fr[22]|(fr[23]<<8))>>4);
        u32 sec=hlen;
        if(!g_wnd_open&&!(fc2&0x40)){ u32 d=sec; int dl2=fe-(int)d; if(dl2>=12&&fr[d]==0xAA&&fr[d+1]==0xAA&&fr[d+2]==0x03&&fr[d+6]==0x88&&fr[d+7]==0x8E&&wnd_eq6(fr+10,g_wnd_bssid))wpa_rx_eapol_p(fr+d+8,dl2-8,0); return outlen; }
        if(g_wnd_open&&!(fc2&0x40)){ u32 d=sec; int dl2=fe-(int)d; if(dl2>0&&wnd_ap_uc(fr,g_wnd_mac,g_wnd_bssid))g_wnd_rx_uc++; if(ba&&!(fr[24]&0x80)){ outlen=wnd_ba_put(prio&15,bseq,fr,dl2>0?fr+d:0,dl2,0,out,max); return outlen; } if((fc&0x80)&&(fr[24]&0x80)&&dl2>0){ if(ba&&ampdu_rx(prio&15,bseq,0,0,0,g_ticks)==2){ g_wnd_rx_drop++; return wnd_ba_pop(out,max); } outlen=wnd_amsdu(fr+d,dl2,out,max); if(ba&&!outlen)outlen=wnd_ba_pop(out,max); return outlen; } if(dl2>=8&&fr[d]==0xAA&&fr[d+1]==0xAA&&fr[d+2]==0x03){ u16 et=((u16)fr[d+6]<<8)|fr[d+7]; int need=14+dl2-8;
                if(et!=0x888E&&need<=max){ for(int i=0;i<6;i++){ out[i]=fr[4+i]; out[6+i]=fr[16+i]; } out[12]=(u8)(et>>8); out[13]=(u8)et; for(int i=0;i<dl2-8;i++)out[14+i]=fr[d+8+i]; outlen=need; g_wnd_rx_frames++; } } return outlen; }
        if((fc2&0x40) && sec+8u<=(u32)fe){
            const u8* cc=fr+sec;
            if(cc[3]&0x20){
                int keyid=(cc[3]>>6)&3;
                u64 pn=((u64)cc[0])|((u64)cc[1]<<8)|((u64)cc[4]<<16)|((u64)cc[5]<<24)|((u64)cc[6]<<32)|((u64)cc[7]<<40);
                u32 doff=sec+8; int dlen=fe-(int)doff-(hwhint==2?0:8);
                if(dlen>=8){
                    const u8* a1=fr+4; int bcast=(a1[0]&1);
                    const u8* key; u64* pnslot;
                    if(bcast){ if(!g_wnd_have_gtk){ g_wnd_rx_drop++; return outlen; } key=g_wnd_gtk; pnslot=&g_wnd_rx_pn_grp[keyid]; }
                    else { key=g_wnd_tk; pnslot=&g_wnd_rx_pn_uni[(fc&0x80)?(prio&15):16]; }
                    if(pn<=*pnslot){ g_wnd_rx_drop++; return outlen; }
                    u8 aad[32]; int al=0;
                    aad[al++]=fr[0]&0x8F; aad[al++]=(fr[0]&0x80)?(fr[1]&0x47):(fr[1]&0xC7);
                    for(int i=0;i<6;i++)aad[al++]=fr[4+i];
                    for(int i=0;i<6;i++)aad[al++]=fr[10+i];
                    for(int i=0;i<6;i++)aad[al++]=fr[16+i];
                    aad[al++]=fr[22]&0x0F; aad[al++]=0;
                    if(fc&0x80){ aad[al++]=prio; aad[al++]=0; }
                    const u8* a2=fr+10;
                    u8 nonce[13]; ccmp_nonce(nonce,a2,pn,prio);
                    int hwdec=hwhint; if(hwdec&&!(fr[doff]==0xAA&&fr[doff+1]==0xAA&&fr[doff+2]==0x03))hwdec=0;
                    static u8 pt[RTW_RXBUF]; if(dlen>(int)sizeof(pt)){ g_wnd_rx_drop++; return outlen; } for(int i=0;i<dlen;i++)pt[i]=fr[doff+i];
                    u8 mic[8]; for(int i=0;i<8;i++)mic[i]=hwdec?0:fr[doff+dlen+i];
                    if(hwdec||aes_ccm_decrypt_verify(key,nonce,aad,al,pt,dlen,mic)==1){
                        g_wnd_beacon_t=g_ticks; if(wnd_ap_uc(fr,g_wnd_mac,g_wnd_bssid))g_wnd_rx_uc++;
                        if(ba){ if(fr[24]&0x80){ if(ampdu_rx(prio&15,bseq,0,0,pn,g_ticks)==2){ g_wnd_rx_drop++; return wnd_ba_pop(out,max); } outlen=wnd_amsdu(pt,dlen,out,max); if(!outlen)outlen=wnd_ba_pop(out,max); return outlen; } outlen=wnd_ba_put(prio&15,bseq,fr,pt,dlen,pn,out,max); return outlen; }
                        *pnslot=pn;
                        if((fc&0x80)&&(fr[24]&0x80)){ outlen=wnd_amsdu(pt,dlen,out,max); return outlen; }
                        if(dlen>=8 && pt[0]==0xAA && pt[1]==0xAA && pt[2]==0x03){
                            u16 etype=((u16)pt[6]<<8)|pt[7];
                            if(etype==0x888E){ wpa_rx_eapol(pt+8,dlen-8); return outlen; }
                            const u8* da=fr+4; const u8* sa=fr+16;
                            int need=14+(dlen-8);
                            if(need<=max && need>=14){
                                for(int i=0;i<6;i++)out[i]=da[i];
                                for(int i=0;i<6;i++)out[6+i]=sa[i];
                                out[12]=(u8)(etype>>8); out[13]=(u8)etype;
                                for(int i=0;i<dlen-8;i++)out[14+i]=pt[8+i];
                                outlen=need; g_wnd_rx_frames++;
                            }
                        }
                    } else g_wnd_rx_drop++;
                }
            }
        }
    }
    return outlen;
}
static int wnd_rx_eth(u8* out,int max){
    if(!g_wnd_mmio&&wifi_hw_generic()){ if(!g_wnd_up){ g_amqn=0; return 0; } { int r=wnd_amq_pop(out,max); if(r)return r; ampdu_tick(g_ticks); wnd_saq_tick(); if(wnd_gbatx())wnd_batx(); r=wnd_ba_pop(out,max); if(r)return r; } wifi_hw_poll(); static u8 hf[2400]; int rs, n;
        int dec=0; while((n=wifi_hw_rx_pop_dec(hf,(int)sizeof(hf),&rs,&dec))!=0){ if(n>=20&&(hf[0]&0xFC)==0x84){ wnd_rx_bar(hf,n); int o=wnd_ba_pop(out,max); if(o)return o; continue; } if(n<32)continue; int o=wnd_rx_frame(hf,n,dec?2:0,out,max); if(o)return o; } return 0; }
    if(!g_wnd_up||!g_rxnum){ g_amqn=0; return 0; }
    { int r=wnd_amq_pop(out,max); if(r)return r; ampdu_tick(g_ticks); wnd_saq_tick(); wnd_batx(); r=wnd_ba_pop(out,max); if(r)return r; }
    u32 m=g_wnd_mmio;
    { u32 raw=rtw_r32(m,0x03B4); if(raw==0xFFFFFFFFu){ g_wnd_dead++; return 0; } }
    int wp=rtw_rx_hw_wp(m); if(wp<0||(u32)wp>=0x1000u)return 0;
    while((int)(g_rxidx%g_rxnum)!=(wp%(int)g_rxnum)){
        u32 ri=g_rxidx % g_rxnum; u8* base=g_rxbuf+ri*RTW_RXBUF; if(!*(volatile u32*)(u64)base)break;
        u32 rw0=*(volatile u32*)(u64)base; u32 plen=rw0&0x3FFFu;
        u32 drvsz=((rw0>>16)&0xFu)*8u; u32 shift=(rw0>>24)&3u; u32 crc=((rw0>>14)&1u)|(u32)rtw_rx_bad(base);
        u32 hoff=24u+drvsz+shift; u32 fe=hoff+plen-(plen>=4u?4u:0u); if(fe>RTW_RXBUF)fe=RTW_RXBUF;
        int outlen=0;
        if(plen>=24 && !crc && (base[hoff]&0x0C)==0x00){ extern int g_wifi_scanning; extern u8 g_scan_ch; extern int rtw_scan_frame(const u8*,u8); if(g_wifi_scanning&&g_scan_ch)rtw_scan_frame(base,g_scan_ch); wnd_rx_mgmt(base,0,base+hoff,(int)(fe-hoff)); outlen=wnd_ba_pop(out,max); }
        if(plen>=20 && !crc && (base[hoff]&0xFC)==0x84){ wnd_rx_bar(base+hoff,(int)(fe-hoff)); outlen=wnd_ba_pop(out,max); }
        if(plen>=32 && !crc && (base[hoff]&0x0C)==0x08)outlen=wnd_rx_frame(base+hoff,(int)(fe-hoff),!((rw0>>27)&1u)&&((rw0>>20)&7u)==4u&&!(rw0&(1u<<15)),out,max);
        *(volatile u32*)(u64)base=0;
        *(volatile u16*)(u64)(g_rxring+ri*8)=(u16)RTW_RXBUF; *(volatile u16*)(u64)(g_rxring+ri*8+2)=0; *(volatile u32*)(u64)(g_rxring+ri*8+4)=(u32)(u64)base;
        g_rxidx++; if((int)g_rxidx>=(int)g_rxnum)g_rxidx=0; rtw_w16(m,0x03B4,(u16)g_rxidx);
        if(outlen)return outlen;
    }
    return 0;
}

static int g_netdev=NETDEV_ETH;

static int wnd_bcast_flag(void){ return g_wnd_have_gtk?0x8000:0x0000; }
static void wnd_tx_ptr(const u8* d,int n){ if(wnd_tx_eth(d,n)<0)g_wnd_txfail++; }
static void eth_tx_ptr(const u8* d,int n){ nic_send(d,n); }
static int eth_rx_ptr(u8* o,int m){ return nic_recv(o,m); }
static const u8* eth_mac_ptr(void){ return my_mac; }
static int eth_link_ptr(void){ return net_ok; }
static const u8* wnd_mac_ptr(void){ return g_wnd_mac; }
static int wnd_link_ptr(void){ return g_wnd_up; }

void netdev_set(int d){
    g_netdev_wifi=(d==NETDEV_WIFI);
    g_netdev=d;
    if(d==NETDEV_WIFI){ g_tx=wnd_tx_ptr; g_rx=wnd_rx_eth; g_mac=wnd_mac_ptr; g_linkok=wnd_link_ptr; g_bcastf=wnd_bcast_flag; }
    else { g_tx=eth_tx_ptr; g_rx=eth_rx_ptr; g_mac=eth_mac_ptr; g_linkok=eth_link_ptr; g_bcastf=0; }
}
