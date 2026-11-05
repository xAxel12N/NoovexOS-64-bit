#include "include/nvx/base.h"
#include "include/nvx/lib/crypto/nvxwpa.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/drivers/net/ethernet/nic.h"
#include "include/nvx/drivers/storage/ahci.h"
#include "include/nvx/net/stack/tcpip.h"
#include "include/nvx/drivers/net/wifi/rtw_efuse.h"
#include "include/nvx/drivers/net/wifi/rtw8821ce.h"
#include "include/nvx/net/wifi/wifi_netdev.h"
static u8  g_wnd_mac[6], g_wnd_bssid[6];
int g_wnd_up=0; static int g_wnd_qos=0;
static u64 g_wnd_tx_pn=1;
static u64 g_wnd_rx_pn_uni[17], g_wnd_rx_pn_grp=0;
extern volatile u64 g_ticks;
u64 g_wnd_beacon_t=0; int g_wnd_lost=0, g_wnd_beacons=0; u32 g_wnd_bcn_int=100;
int wpa_rx_eapol(const u8* eap,int el); int wpa_rx_eapol_p(const u8* eap,int el,int prot);
int rtw_tx_mgmt(u32 m,const u8* frame,int fl);
static u8  g_wnd_tk[16], g_wnd_gtk[16]; int g_wnd_have_gtk=0, g_wnd_gtk_id=1;
static u32 g_wnd_mmio=0; int g_wnd_open=0; void (*g_wnd_pre_tx)(void)=0;
static int g_wnd_txfail=0;
u32 g_wnd_rx_frames=0, g_wnd_rx_drop=0, g_wnd_tx_frames=0;

void wnd_setup(u32 m,const u8* bssid,const u8* mac,const u8* tk,int qos){
    g_wnd_mmio=m; for(int i=0;i<6;i++){ g_wnd_bssid[i]=bssid[i]; g_wnd_mac[i]=mac[i]; }
    for(int i=0;i<16;i++)g_wnd_tk[i]=tk[i];
    g_wnd_tx_pn=1; for(int i=0;i<17;i++)g_wnd_rx_pn_uni[i]=0; g_wnd_rx_pn_grp=0; g_wnd_qos=qos; g_wnd_up=1; g_wnd_txfail=0; g_wnd_lost=0; g_wnd_beacon_t=g_ticks; g_wnd_beacons=0;
    g_wnd_rx_frames=g_wnd_rx_drop=g_wnd_tx_frames=0; g_wnd_open=0;
}
void wnd_setup_open(u32 m,const u8* bssid,const u8* mac,int qos){ u8 z[16]={0}; wnd_setup(m,bssid,mac,z,qos); g_wnd_open=1; }
void wnd_set_gtk(const u8* gtk,int keyid){ int same=g_wnd_have_gtk&&g_wnd_gtk_id==keyid; for(int i=0;i<16;i++){ if(g_wnd_gtk[i]!=gtk[i])same=0; g_wnd_gtk[i]=gtk[i]; } g_wnd_gtk_id=keyid; g_wnd_have_gtk=1; if(!same)g_wnd_rx_pn_grp=0; }
void wnd_down(void){ g_wnd_up=0; g_wnd_have_gtk=0; }
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
    if(!g_benum)return -1;
    { u32 raw=rtw_r32(g_wnd_mmio,0x03A8); if(raw==0xFFFFFFFFu){ g_wnd_dead++; return -1; }
      u32 nx=(g_beidx+1u)%g_benum; int full=1; for(int w=0;w<200;w++){ u32 hr=((rtw_r32(g_wnd_mmio,0x03A8)>>16)&0x0FFFu)%g_benum; if(nx!=hr){ full=0; break; } io_wait(); io_wait(); io_wait(); io_wait(); io_wait(); }
      if(full){ g_wnd_txfull++; return -1; } }
    static u8* pkts[256]; u32 slot=(g_beidx%g_benum)&255u; if(!pkts[slot]){ pkts[slot]=(u8*)dma_alloc(2048); if(!pkts[slot])return -1; } u8* pkt=pkts[slot];
    const u8* dst=eth; const u8* src=eth+6; u16 etype=((u16)eth[12]<<8)|eth[13];
    const u8* payload=eth+14; int plen=len-14;

    static u8 fr[2048]; int fl=0;
    int qos=g_wnd_qos;
    fr[fl++]=qos?0x88:0x08; fr[fl++]=g_wnd_open?0x01:0x41;        /* data/QoS-data, ToDS=1, Protected=1 */
    fr[fl++]=0; fr[fl++]=0;                         /* duration */
    for(int i=0;i<6;i++)fr[fl++]=g_wnd_bssid[i];    /* addr1 = RA = BSSID */
    for(int i=0;i<6;i++)fr[fl++]=g_wnd_mac[i];      /* addr2 = TA = us */
    for(int i=0;i<6;i++)fr[fl++]=dst[i];            /* addr3 = DA */
    fr[fl++]=0; fr[fl++]=0;                         /* seq (hw fills) */
    u8 prio=0; if(qos){ fr[fl++]=0; fr[fl++]=0; }   /* QoS control, TID 0 */
    int hdrlen=fl;

    u64 pn=0; if(!g_wnd_open){ pn=g_wnd_tx_pn++; u8 cc[8]; ccmp_hdr(cc,pn,0); for(int i=0;i<8;i++)fr[fl++]=cc[i]; }

    int dataoff=fl;
    fr[fl++]=0xAA; fr[fl++]=0xAA; fr[fl++]=0x03; fr[fl++]=0x00; fr[fl++]=0x00; fr[fl++]=0x00;
    fr[fl++]=(u8)(etype>>8); fr[fl++]=(u8)etype;
    for(int i=0;i<plen;i++)fr[fl++]=payload[i];
    int dlen=fl-dataoff;

    if(!g_wnd_open){ u8 aad[32]; int al=0;
    aad[al++]=fr[0]&0x8F; aad[al++]=(fr[0]&0x80)?(fr[1]&0x47):(fr[1]&0xC7);
    for(int i=0;i<6;i++)aad[al++]=fr[4+i];
    for(int i=0;i<6;i++)aad[al++]=fr[10+i];
    for(int i=0;i<6;i++)aad[al++]=fr[16+i];
    aad[al++]=fr[22]&0x0F; aad[al++]=0;
    if(qos){ aad[al++]=prio; aad[al++]=0; }
    u8 nonce[13]; ccmp_nonce(nonce,g_wnd_mac,pn,prio);
    u8 mic[8]; aes_ccm_auth_encrypt(g_wnd_tk,nonce,aad,al,fr+dataoff,dlen,mic);
    for(int i=0;i<8;i++)fr[fl++]=mic[i]; }

    for(u32 i=0;i<48u;i++)pkt[i]=0;
    u32 w0=((u32)fl&0xFFFFu)|((48u&0xFFu)<<16)|(1u<<26);
    *(volatile u32*)(pkt+0)=w0;
    *(volatile u32*)(pkt+4)=((u32)0u<<8)|((u32)8u<<16);
    *(volatile u32*)(pkt+12)=(1u<<8)|(1u<<10);
    *(volatile u32*)(pkt+16)=0x00u;
    rtw_txdesc_rate(pkt,0);
    *(volatile u32*)(pkt+32)=(1u<<15);
    for(int i=0;i<fl;i++)pkt[48+i]=fr[i];

    u32 total=48u+(u32)fl;
    u32 idx=g_beidx % g_benum; u8* bd=g_bering+idx*16;
    u32 psb=((total-1)/128)+1;
    *(volatile u16*)(bd+0)=48; *(volatile u16*)(bd+2)=(u16)psb; *(volatile u32*)(bd+4)=(u32)(u64)pkt;
    *(volatile u16*)(bd+8)=(u16)fl; *(volatile u16*)(bd+10)=0; *(volatile u32*)(bd+12)=(u32)(u64)(pkt+48);
    cpu_wmb();
    rtw_w32(g_wnd_mmio,0x0328,(u32)(u64)g_bering); rtw_w16(g_wnd_mmio,0x0388,(u16)(g_benum&0x0FFF));
    g_beidx=(g_beidx+1)%g_benum; rtw_w16(g_wnd_mmio,0x03A8,(u16)g_beidx);
    g_wnd_tx_frames++;
    return len;
}

static int wnd_eq6(const u8* a,const u8* b){ for(int i=0;i<6;i++) if(a[i]!=b[i])return 0; return 1; }
static void wnd_addba_decline(const u8* fr,int len){
    if(len<24+9)return; u8 f[64]; int n=0; f[n++]=0xD0; f[n++]=0; f[n++]=0; f[n++]=0;
    for(int i=0;i<6;i++)f[n++]=g_wnd_bssid[i]; for(int i=0;i<6;i++)f[n++]=g_wnd_mac[i]; for(int i=0;i<6;i++)f[n++]=g_wnd_bssid[i]; f[n++]=0; f[n++]=0;
    f[n++]=3; f[n++]=1; f[n++]=fr[26]; f[n++]=37; f[n++]=0; f[n++]=fr[27]; f[n++]=fr[28]; f[n++]=0; f[n++]=0;
    rtw_tx_mgmt(g_wnd_mmio,f,n); }
static void wnd_rx_mgmt(const u8* base,const u8* fr,int len){
    u8 st=fr[0]&0xF0; if(len<24)return; const u8* a1=fr+4; const u8* a2=fr+10;
    if(!wnd_eq6(a2,g_wnd_bssid))return;
    if(st==0x80){ g_wnd_beacon_t=g_ticks; g_wnd_beacons++; int r=rtw_rx_rssi(base); if(r>-127)g_wifi_rssi=g_wifi_rssi<=-127?r:(g_wifi_rssi*7+r)/8; if(len>=24+12){ u32 bi=(u32)(fr[32]|(fr[33]<<8)); if(bi>=10&&bi<=1000)g_wnd_bcn_int=bi; } return; }
    int tome=wnd_eq6(a1,g_wnd_mac)||(a1[0]==0xFF&&a1[1]==0xFF);
    if((st==0xC0||st==0xA0)&&tome){ extern int g_wpa_mfp;
        if(g_wpa_mfp&&!g_wnd_open){ if(!(fr[1]&0x40)||!wnd_eq6(a1,g_wnd_mac)||len<24+8+2+8)return;
            const u8* cc=fr+24; if(!(cc[3]&0x20))return; u64 pn=((u64)cc[0])|((u64)cc[1]<<8)|((u64)cc[4]<<16)|((u64)cc[5]<<24)|((u64)cc[6]<<32)|((u64)cc[7]<<40); static u64 mpn=0; if(pn<=mpn)return;
            u8 aad[22]; int al=0; aad[al++]=fr[0]; aad[al++]=fr[1]&0xC7; for(int i=0;i<18;i++)aad[al++]=fr[4+i]; aad[al++]=fr[22]&0x0F; aad[al++]=0;
            u8 nonce[13]; ccmp_nonce(nonce,a2,pn,0x10); int dl=len-24-8-8; if(dl<2||dl>64)return; u8 pt[64]; for(int i=0;i<dl;i++)pt[i]=fr[32+i]; u8 mic[8]; for(int i=0;i<8;i++)mic[i]=fr[32+dl+i];
            if(aes_ccm_decrypt_verify(g_wnd_tk,nonce,aad,al,pt,dl,mic)!=0)return; mpn=pn; g_wnd_lost=pt[0]|(pt[1]<<8); if(!g_wnd_lost)g_wnd_lost=1; wnd_down(); return; } g_wnd_lost=len>=26?(fr[24]|(fr[25]<<8)):1; if(!g_wnd_lost)g_wnd_lost=1; wnd_down(); return; }
    if(st==0xD0&&wnd_eq6(a1,g_wnd_mac)&&len>=24+9&&fr[24]==3&&fr[25]==0)wnd_addba_decline(fr,len); }
int wnd_send_eapol(const u8* eap,int el){ static u8 f[600]; if(el>580||!g_wnd_up)return -1; for(int i=0;i<6;i++){ f[i]=g_wnd_bssid[i]; f[6+i]=g_wnd_mac[i]; } f[12]=0x88; f[13]=0x8E; for(int i=0;i<el;i++)f[14+i]=eap[i]; return wnd_tx_eth(f,14+el); }
static int wnd_rx_eth(u8* out,int max){
    if(!g_wnd_up||!g_rxnum)return 0;
    u32 m=g_wnd_mmio;
    { u32 raw=rtw_r32(m,0x03B4); if(raw==0xFFFFFFFFu){ g_wnd_dead++; return 0; } }
    int wp=rtw_rx_hw_wp(m); if(wp<0||(u32)wp>=0x1000u)return 0;
    while((int)(g_rxidx%g_rxnum)!=(wp%(int)g_rxnum)){
        u32 ri=g_rxidx % g_rxnum; u8* base=g_rxbuf+ri*2048;
        u32 rw0=*(volatile u32*)(u64)base; u32 plen=rw0&0x3FFFu;
        u32 drvsz=((rw0>>16)&0xFu)*8u; u32 shift=(rw0>>24)&3u; u32 crc=(rw0>>14)&1u;
        u32 hoff=24u+drvsz+shift; u32 fe=hoff+plen; if(fe>2048u)fe=2048u;
        int outlen=0;
        if(plen>=24 && !crc && (base[hoff]&0x0C)==0x00) wnd_rx_mgmt(base,base+hoff,(int)(fe-hoff));
        if(plen>=32 && !crc){
            u8* fr=base+hoff; u8 fc=fr[0]; u8 fc2=fr[1];
            if((fc&0x0C)==0x08 && (fc2&0x02)){     /* data frame, FromDS=1 */
                u32 hlen=24u; if((fc&0x80))hlen+=2u;
                if((fc&0x80)&&(fc2&0x80))hlen+=4u;
                u8 prio=(fc&0x80)?(fr[24]&0x0F):0;
                u32 sec=hoff+hlen;
                if(!g_wnd_open&&!(fc2&0x40)){ u32 d=sec; int dl2=(int)fe-(int)d; if(dl2>=12&&base[d]==0xAA&&base[d+1]==0xAA&&base[d+2]==0x03&&base[d+6]==0x88&&base[d+7]==0x8E&&wnd_eq6(fr+10,g_wnd_bssid))wpa_rx_eapol_p(base+d+8,dl2-8,0); goto next; }
                if(g_wnd_open&&!(fc2&0x40)){ u32 d=sec; int dl2=(int)fe-(int)d; if(dl2>=8&&base[d]==0xAA&&base[d+1]==0xAA&&base[d+2]==0x03){ u16 et=((u16)base[d+6]<<8)|base[d+7]; int need=14+dl2-8;
                        if(et!=0x888E&&need<=max){ for(int i=0;i<6;i++){ out[i]=fr[4+i]; out[6+i]=fr[16+i]; } out[12]=(u8)(et>>8); out[13]=(u8)et; for(int i=0;i<dl2-8;i++)out[14+i]=base[d+8+i]; outlen=need; g_wnd_rx_frames++; } } goto next; }
                if((fc2&0x40) && sec+8u<=fe){       /* Protected */
                    u8* cc=base+sec;
                    if(cc[3]&0x20){                 /* ExtIV = CCMP */
                        int keyid=(cc[3]>>6)&3;
                        u64 pn=((u64)cc[0])|((u64)cc[1]<<8)|((u64)cc[4]<<16)|((u64)cc[5]<<24)|((u64)cc[6]<<32)|((u64)cc[7]<<40);
                        u32 doff=sec+8; int dlen=(int)fe-(int)doff-8;
                        if(dlen>=8){
                            const u8* a1=fr+4; int bcast=(a1[0]&1);
                            const u8* key; u64* pnslot;
                            if(bcast){ if(!g_wnd_have_gtk){ g_wnd_rx_drop++; goto next; } key=g_wnd_gtk; pnslot=&g_wnd_rx_pn_grp; }
                            else { key=g_wnd_tk; pnslot=&g_wnd_rx_pn_uni[(fc&0x80)?(prio&15):16]; }
                            if(pn<=*pnslot){ g_wnd_rx_drop++; goto next; }
                            u8 aad[32]; int al=0;
                            aad[al++]=fr[0]&0x8F; aad[al++]=(fr[0]&0x80)?(fr[1]&0x47):(fr[1]&0xC7);
                            for(int i=0;i<6;i++)aad[al++]=fr[4+i];
                            for(int i=0;i<6;i++)aad[al++]=fr[10+i];
                            for(int i=0;i<6;i++)aad[al++]=fr[16+i];
                            aad[al++]=fr[22]&0x0F; aad[al++]=0;
                            if(fc&0x80){ aad[al++]=prio; aad[al++]=0; }
                            const u8* a2=fr+10;
                            u8 nonce[13]; ccmp_nonce(nonce,a2,pn,prio);
                            int hwdec=!((rw0>>27)&1u)&&((rw0>>20)&7u)==4u&&!(rw0&(1u<<15)); if(hwdec&&!(base[doff]==0xAA&&base[doff+1]==0xAA&&base[doff+2]==0x03))hwdec=0; 
                            static u8 pt[2048]; for(int i=0;i<dlen;i++)pt[i]=base[doff+i];
                            u8 mic[8]; for(int i=0;i<8;i++)mic[i]=hwdec?0:base[doff+dlen+i];
                            if(hwdec||aes_ccm_decrypt_verify(key,nonce,aad,al,pt,dlen,mic)==0){
                                *pnslot=pn;
                                if(dlen>=8 && pt[0]==0xAA && pt[1]==0xAA && pt[2]==0x03){
                                    u16 etype=((u16)pt[6]<<8)|pt[7];
                                    if(etype==0x888E){ wpa_rx_eapol(pt+8,dlen-8); goto next; }
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
        }
    next:
        *(volatile u32*)(u64)base=0;
        *(volatile u16*)(u64)(g_rxring+ri*8)=2048; *(volatile u32*)(u64)(g_rxring+ri*8+4)=(u32)(u64)base;
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
