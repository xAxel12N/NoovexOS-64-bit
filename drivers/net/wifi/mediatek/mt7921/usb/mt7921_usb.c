/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Derived from Linux drivers/net/wireless/mediatek/mt76 (usb.c, mt792x_usb.c, mt792x.h, mt7921/usb.c, mt7921/mac.c).
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 * Copyright (C) 2022-2023 MediaTek Inc.
 * Licence text: drivers/net/wifi/mediatek/mt7921/LICENCE.BSD-3-Clause-Clear
 */
#include "drivers/net/wifi/mediatek/mt7921/usb/mt7921_usb.h"
#include "drivers/net/wifi/mediatek/mt7921/mcu/mcu.h"
#include "include/nvx/net/wifi/wifi_hw.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/net/wifi/wifi_settings.h"

void hr_delay_us(u32 us);
u64 mt_ms(void);
int mt7921_fw_load(struct mt7921_softc* sc);
struct mt7921_softc g_mt7921u;
int mt7921u_enable=1;
static struct mt7921u_dev mtu_dev;
static u8 mtu_obuf[MTU_BUF],mtu_ibuf[MTU_BUF];
static int mtu_fails;

const struct mt_usb_id mt7921u_ids[]={
    {0x0e8d,0x7961,"MediaTek MT7921AU (Wi-Fi 6 USB)"},
    {0x3574,0x6211,"Comfast CF-952AX (MT7921AU)"},
    {0x0846,0x9060,"Netgear A8000 / AXE3000 (MT7921AU)"},
    {0x0846,0x9065,"Netgear A7500 (MT7921AU)"},
    {0x35bc,0x0107,"TP-Link TXE50UH (MT7921AU)"},
    {0,0,0}
};
static const struct mt_pci_id mtu_pid={0x0e8d,0x7961,MT_CHIP_7961,"MediaTek MT7921AU (Wi-Fi 6 USB)","mediatek/WIFI_MT7961_patch_mcu_1_2_hdr.bin","mediatek/WIFI_RAM_CODE_MT7961_1.bin"};
const struct mt_usb_id* mt7921u_match(u16 vid,u16 pid){ for(const struct mt_usb_id* i=mt7921u_ids;i->vendor;i++) if(i->vendor==vid&&i->device==pid)return i; return 0; }

int mt7921u_parse_eps(const u8* c,int n,struct mt7921u_hc* hc,int* iface){
    int mine=0,ni=0,no=0,done=0;
    for(int i=0;i+2<=n;){ int bl=c[i]; if(bl<2||i+bl>n)break; int ty=c[i+1];
        if(ty==4&&bl>=9){ if(mine&&ni==MTU_NIN&&no==MTU_NOUT)done=1; if(!done){ mine=c[i+3]==0&&c[i+5]==0xff&&c[i+6]==0xff&&c[i+7]==0xff; if(mine){ ni=no=0; if(iface)*iface=c[i+2]; } else mine=0; } else mine=0; }
        else if(ty==5&&bl>=7&&mine&&(c[i+3]&3)==2){ u8 a=c[i+2]; if(a&0x80){ if(ni<MTU_NIN)hc->in_ep[ni++]=a; } else if(no<MTU_NOUT)hc->out_ep[no++]=a; }
        i+=bl; }
    return (done||(mine&&ni==MTU_NIN&&no==MTU_NOUT))?0:-1; }

static void mtu_hung(const char* why){ if(!mtu_dev.hung){ mtu_dev.hung=1; g_mt7921u.err=why; klog2("mt7921u: ",why); } }
static int mtu_vreq(u8 rt,u8 req,u16 val,u16 idx,u8* buf,u16 len){
    if(mtu_dev.hung||!mtu_dev.hc)return -1;
    for(int t=0;t<3;t++){ int r=mtu_dev.hc->ctrl(mtu_dev.hc->ctx,rt,req,val,idx,buf,len); if(r>=0&&r==(int)len)return r; mtu_dev.ctrl_err++; }
    mtu_hung("USB control transfer failed - adapter stopped (unplug and replug it)"); return -1; }
static u32 mtu_rr(struct mt7921_softc* sc,u32 a){ (void)sc; u8 b[4]; if(mtu_vreq(0x80|MTU_RT_VENDOR,MTU_VEND_READ_EXT,(u16)(a>>16),(u16)a,b,4)<0)return 0xFFFFFFFFu; return mt_get32(b); }
static void mtu_wr(struct mt7921_softc* sc,u32 a,u32 v){ (void)sc; u8 b[4]; mt_put32(b,v); mtu_vreq(MTU_RT_VENDOR,MTU_VEND_WRITE_EXT,(u16)(a>>16),(u16)a,b,4); }
static u32 mtu_uhw_rr(u32 a){ u8 b[4]; if(mtu_vreq(0x80|MTU_RT_UHW,MTU_VEND_DEV_MODE,(u16)(a>>16),(u16)a,b,4)<0)return 0xFFFFFFFFu; return mt_get32(b); }
static void mtu_uhw_wr(u32 a,u32 v){ u8 b[4]; mt_put32(b,v); mtu_vreq(MTU_RT_UHW,MTU_VEND_WRITE,(u16)(a>>16),(u16)a,b,4); }
static void mtu_rmw(u32 a,u32 clr,u32 set){ mtu_wr(0,a,(mtu_rr(0,a)&~clr)|set); }
static int mtu_poll(u32 a,u32 m,u32 v,u32 ms){ u64 t0=mt_ms(); for(;;){ u32 x=mtu_rr(0,a); if(mtu_dev.hung)return 0; if((x&m)==v)return 1; if(mt_ms()-t0>=ms)return 0; hr_delay_us(200); } }

u32 mt7921u_frame(int qid,const u8* buf,u32 len,u8* out,u32 cap){
    u32 body;
    if(qid==MT_Q_TX){
        if(len<=MT_TXD_SIZE){ return 0; } u32 fl=len-MT_TXD_SIZE; body=MTU_TXD+fl;
        if(MTU_HDR+body+8>cap)return 0;
        u8* t=out+MTU_HDR; mt_copy(t,buf,MT_TXD_SIZE); mt_zero(t+MT_TXD_SIZE,MTU_TXD-MT_TXD_SIZE);
        u32 d0=mt_get32(t); d0=(d0&~0xffffu&~(3u<<23))|(body&0xffffu)|((u32)MT_TX_TYPE_SF<<23); mt_put32(t,d0); mt_put32(t+28,0);
        mt_copy(t+MTU_TXD,buf+MT_TXD_SIZE,fl);
    } else { body=len; if(!len||MTU_HDR+body+8>cap)return 0; mt_copy(out+MTU_HDR,buf,len); }
    mt_put32(out,body&0xffffu);
    u32 n=MTU_HDR+body,pad=((n+3)&~3u)+4-n; mt_zero(out+n,pad);
    return n+pad; }

static int mtu_tx(struct mt7921_softc* sc,int qid,const u8* buf,u32 len){
    if(mtu_dev.hung||!mtu_dev.hc||qid<0||qid>MT_Q_FWDL||!buf)return -1;
    u32 n=mt7921u_frame(qid,buf,len,mtu_obuf,sizeof mtu_obuf); if(!n)return -1;
    u8 ep=mtu_dev.hc->out_ep[qid==MT_Q_MCU?MTU_EP_OUT_CMD:MTU_EP_OUT_BE];
    int r=mtu_dev.hc->bulk_out(mtu_dev.hc->ctx,ep,mtu_obuf,n,1000);
    if(r!=(int)n){ mtu_dev.bulk_err++; if(mtu_dev.bulk_err>=8)mtu_hung("USB bulk transfers keep failing - adapter stopped"); return -1; }
    if(qid==MT_Q_TX)sc->tx_pkts++;
    return 0; }
static int mtu_tx_idle(struct mt7921_softc* sc,int qid,u32 ms){ (void)sc; (void)qid; (void)ms; return !mtu_dev.hung; }

static void mtu_rx_one(struct mt7921_softc* sc,int which){
    for(int k=0;k<16;k++){
        int got=mtu_dev.hc->bulk_in(mtu_dev.hc->ctx,mtu_dev.hc->in_ep[which],mtu_ibuf,sizeof mtu_ibuf,0);
        if(got<=0)return;
        u32 l=mt_get16(mtu_ibuf);
        if(got<8||l<8||l>(u32)got){ mtu_dev.rx_bad++; continue; }
        if(which==MTU_EP_IN_EVT)sc->mcu_events++; else sc->rx_pkts++;
        mt7921_mcu_rx_event(sc,mtu_ibuf,l);
    } }
static void mtu_rx_poll(struct mt7921_softc* sc){ static int busy; if(busy||mtu_dev.hung||!mtu_dev.hc)return; busy=1; mtu_rx_one(sc,MTU_EP_IN_EVT); mtu_rx_one(sc,MTU_EP_IN_DATA); busy=0; }

static int mtu_fw_pre(struct mt7921_softc* sc){ (void)sc; mtu_rmw(MTU_UDMA_TX_QSEL,0,MTU_FW_DL_EN); return mtu_dev.hung?-1:0; }
static void mtu_fw_post(struct mt7921_softc* sc){ (void)sc; mtu_rmw(MTU_UDMA_TX_QSEL,MTU_FW_DL_EN,0); }
static const struct mt_bus_ops mtu_bus={ mtu_rr,mtu_wr,mtu_tx,mtu_rx_poll,mtu_tx_idle,mtu_fw_pre,mtu_fw_post };

static void mtu_epctl(int rst){ u32 v=mtu_uhw_rr(MTU_EPCTL_RST_OPT),m=(0x3fu<<4)|(7u<<20); mtu_uhw_wr(MTU_EPCTL_RST_OPT,rst?(v|m):(v&~m)); }
static int mtu_wfsys_reset(void){
    mtu_rmw(MTU_UDMA_WLCFG_0,0,MTU_WL_RX_FLUSH);
    if(!mtu_poll(MTU_UDMA_WLCFG_0,MTU_WL_RX_BUSY|MTU_WL_TX_BUSY,0,1000))klog("mt7921u: UDMA busy before WFSYS reset");
    mtu_epctl(0);
    mtu_uhw_wr(MTU_CBTOP_RGU_WF_RST,mtu_uhw_rr(MTU_CBTOP_RGU_WF_RST)|1u); hr_delay_us(20);
    mtu_uhw_wr(MTU_CBTOP_RGU_WF_RST,mtu_uhw_rr(MTU_CBTOP_RGU_WF_RST)&~1u);
    mtu_uhw_wr(MTU_CONN_INFRA_STATUS_SEL,0);
    for(int i=0;i<2&&!mtu_dev.hung;i++){ if(mtu_uhw_rr(MTU_CONN_INFRA_STATUS)&MTU_WFSYS_INIT_DONE)return 0; u64 t0=mt_ms(); while(mt_ms()-t0<100)hr_delay_us(1000); }
    return -1; }
static int mtu_power_on(void){
    if(mtu_vreq(MTU_RT_VENDOR,MTU_VEND_POWER_ON,0,1,0,0)<0)return -1;
    return mtu_poll(MTU_CONN_ON_MISC,MTU_FW_PWR_ON,MTU_FW_PWR_ON,500)?0:-1; }
static int mtu_dma_init(void){
    static const u8 ring[7]={0,1,2,3,4,16,17}; static const u16 base[7]={0x080,0x0c0,0x100,0x140,0x180,0x280,0x2c0};
    for(int i=0;i<7;i++)mtu_rmw(MTU_UWFDMA0_TX_EXT(ring[i]),0xffff00ffu,((u32)base[i]<<16)|4u);
    mtu_rmw(MTU_UWFDMA0_GLO_CFG,MTU_GLO_OMIT_RX_INFO,0);
    mtu_rmw(MTU_UWFDMA0_GLO_CFG,0,MTU_GLO_OMIT_TX_INFO|MTU_GLO_OMIT_RX_INFO_PFET2|MTU_GLO_FW_DWLD_BYPASS_DMASHDL|MTU_GLO_TX_DMA_EN|MTU_GLO_RX_DMA_EN);
    mtu_rmw(MTU_DMASHDL(0x010),0xffff0000u,0xffe00000u);
    mtu_rmw(MTU_DMASHDL(0x00c),1u<<16,0);
    mtu_rmw(MTU_DMASHDL(0x01c),0x0fff0fffu,1u);
    for(int i=0;i<16;i++)mtu_wr(0,MTU_DMASHDL(0x020+((u32)i<<2)),i<5?(0x3u|(0xfffu<<16)):0);
    mtu_wr(0,MTU_DMASHDL(0x060),0x32013201u); mtu_wr(0,MTU_DMASHDL(0x064),0x32013201u); mtu_wr(0,MTU_DMASHDL(0x068),0x55555444u); mtu_wr(0,MTU_DMASHDL(0x06c),0x55555444u);
    mtu_wr(0,MTU_DMASHDL(0x070),0x76540132u); mtu_wr(0,MTU_DMASHDL(0x074),0xFEDCBA98u);
    mtu_rmw(MTU_WFDMA_DUMMY_CR,0,MTU_WFDMA_NEED_REINIT);
    mtu_rmw(MTU_UDMA_WLCFG_0,MTU_WL_RX_FLUSH,0);
    mtu_rmw(MTU_UDMA_WLCFG_0,0,MTU_WL_RX_EN|MTU_WL_TX_EN|MTU_WL_RX_MPSZ_PAD0|MTU_TICK_1US_EN);
    mtu_rmw(MTU_UDMA_WLCFG_1,MTU_WL_TX_TMOUT_LMT,50000u<<8);
    mtu_rmw(MTU_UDMA_WLCFG_0,0,MTU_WL_TX_TMOUT_FUNC_EN);
    mtu_rmw(MTU_UDMA_WLCFG_0,MTU_WL_RX_AGG_TO|MTU_WL_RX_AGG_LMT,0);
    mtu_rmw(MTU_UDMA_WLCFG_1,MTU_WL_RX_AGG_PKT_LMT,0);
    if(!mtu_poll(MTU_UWFDMA0_GLO_CFG,MTU_GLO_RX_DMA_BUSY,0,1000))return -1;
    mtu_rmw(MTU_UWFDMA0_GLO_CFG,MTU_GLO_RX_DMA_EN,0);
    mtu_rmw(MTU_WFDMA_HOST_CONFIG,0,MTU_HOST_RXEVT_EP4_EN);
    mtu_rmw(MTU_UWFDMA0_GLO_CFG,0,MTU_GLO_RX_DMA_EN);
    mtu_epctl(0);
    return mtu_dev.hung?-1:0; }

int mt7921u_attach(const struct mt7921u_hc* hc,u16 vid,u16 pid){
    struct mt7921_softc* sc=&g_mt7921u;
    const struct mt_usb_id* id=mt7921u_match(vid,pid); if(!id||!hc||!hc->ctrl||!hc->bulk_out||!hc->bulk_in)return -1;
    mt_zero(sc,sizeof *sc); mt_zero(&mtu_dev,sizeof mtu_dev); mtu_dev.hc=hc;
    sc->vendor=vid; sc->device=pid; sc->chip=MT_CHIP_7961; sc->id=&mtu_pid; sc->hif=&mtu_bus; sc->hif_priv=&mtu_dev; sc->rssi=-100; sc->state=MT_ST_FOUND;
    klog2("mt7921u: found ",id->name);
    return 0; }
void mt7921u_detach(void){ struct mt7921_softc* sc=&g_mt7921u; if(sc->state==MT_ST_NONE)return; sc->connected=0; sc->state=MT_ST_NONE; mtu_dev.hc=0; mtu_dev.hung=1; klog("mt7921u: adapter removed"); }

static int mtu_bringup(struct mt7921_softc* sc){
    sc->hw_rev=(mtu_rr(sc,MTU_HW_CHIPID)<<16)|(mtu_rr(sc,MTU_HW_REV)&0xffu);
    if(mtu_dev.hung)return -1;
    if((mtu_rr(sc,MTU_CONN_ON_MISC)&MTU_FW_N9_RDY)&&mtu_wfsys_reset()<0){ sc->err="WFSYS reset timed out"; return -1; }
    if(mtu_power_on()<0){ if(!sc->err)sc->err="MCU power-on timed out"; return -1; }
    if(mtu_dma_init()<0){ if(!sc->err)sc->err="USB DMA setup failed"; return -1; }
    return 0; }
static void mtu_deliver(const u8* f,u32 len,int rssi){ g_mt7921u.rssi=rssi; wifi_hw_rx(f,(int)len,rssi); }
int mt7921u_start(void){
    struct mt7921_softc* sc=&g_mt7921u;
    if(!mt7921u_enable||!wifi_feat_on(WSET_MT7921U)){ sc->err="turned off in Settings > Wi-Fi > Extra Wi-Fi chips"; return -1; }
    if(sc->state==MT_ST_NONE||!mtu_dev.hc)return -1;
    if(sc->state==MT_ST_READY)return 0;
    if(g_mt7921.state>MT_ST_FOUND){ sc->err="a MediaTek PCIe Wi-Fi card is already active"; return -1; }
    if(mtu_fails>=3){ sc->err="start failed 3 times - turned off until reboot"; return -1; }
    if(mtu_dev.hung){ sc->state=MT_ST_HWFAIL; return -1; }
    if(!wifi_feat_begin(WSET_MT7921U)){ sc->err="turned off after failures - see Settings"; return -1; }
    mt7921_rx_deliver=mtu_deliver; sc->err=0;
    if(sc->state<MT_ST_FWOK){
        if(mtu_bringup(sc)<0){ sc->state=MT_ST_HWFAIL; mtu_fails++; wifi_feat_fail(WSET_MT7921U,sc->err?sc->err:"bring-up failed"); return -1; }
        if(mt7921_fw_load(sc)<0){ if(sc->state==MT_ST_NOFW){ wifi_feat_ok(WSET_MT7921U); mtu_fails++; return -1; } sc->state=MT_ST_HWFAIL; mtu_fails++; wifi_feat_fail(WSET_MT7921U,sc->err?sc->err:"firmware download failed"); return -1; }
        sc->state=MT_ST_FWOK; klog("mt7921u: firmware is running"); }
    if(mt7921_mcu_init(sc)<0){ if(!sc->err)sc->err="MCU init failed"; mtu_fails++; wifi_feat_fail(WSET_MT7921U,sc->err); return -1; }
    sc->state=MT_ST_READY; mtu_fails=0; wifi_feat_ok(WSET_MT7921U); return 0; }

static int mtu_g_init(void* ctx){ (void)ctx; return mt7921u_start(); }
static int mtu_g_scan_start(void* ctx,const u8* ch,int n){ u8 c[64]; int k=mt7921_scan_list(ch,n,c,64,0); return mt7921_hw_scan((struct mt7921_softc*)ctx,c,k); }
static int mtu_g_scan_done(void* ctx){ return !mt7921_scan_busy((struct mt7921_softc*)ctx); }
static int mtu_g_join(void* ctx,const u8* bssid,u8 ch,const char* ssid,u8 akm,u8 ht){ extern int g_wpa_akm; struct mt7921_softc* sc=(struct mt7921_softc*)ctx;
    if(ch>=MT7921_VCH6){ sc->err="6 GHz is not supported on the USB adapter yet"; return -1; }
    return mt7921_connect(sc,bssid,ch,ssid,(u8)(akm?g_wpa_akm:0),ht); }
static void mtu_g_leave(void* ctx){ mt7921_disconnect((struct mt7921_softc*)ctx); }
static int mtu_g_tx(void* ctx,const u8* f,int len){ return len<=0?-1:mt7921_tx_data((struct mt7921_softc*)ctx,f,(u32)len); }
static int mtu_g_set_key(void* ctx,int idx,int type,const u8* key,int len,int pw){ return mt7921_set_key((struct mt7921_softc*)ctx,idx,type,key,len,pw); }
static int mtu_g_rssi(void* ctx){ return ((struct mt7921_softc*)ctx)->rssi; }
static void mtu_g_poll(void* ctx){ struct mt7921_softc* sc=(struct mt7921_softc*)ctx; if(sc->state>=MT_ST_FWOK){ mt7921_rx_poll(sc); mt_ba_drain(sc); } }
static int mtu_g_mac(void* ctx,u8* mac){ struct mt7921_softc* sc=(struct mt7921_softc*)ctx; if(sc->state!=MT_ST_READY)return -1; mt_copy(mac,sc->mac,6); return 0; }
static const struct wifi_hw_ops mtu_hw_ops={ "mt7921u",WIFI_HW_SAE,mtu_g_init,mtu_g_scan_start,mtu_g_scan_done,mtu_g_join,mtu_g_leave,mtu_g_tx,mtu_g_set_key,mtu_g_rssi,mtu_g_poll,mtu_g_mac,0 };
int mt7921u_xhci_open(void);
void mt7921u_wifi_hw_probe(void){ if(!mt7921u_enable||!wifi_feat_on(WSET_MT7921U))return; if(g_mt7921u.state==MT_ST_NONE&&!mt7921u_xhci_open())return; if(mt7921u_start()==0)wifi_hw_register(&mtu_hw_ops,&g_mt7921u); }

static char* mtu_s(char* p,char* e,const char* s){ while(s&&*s&&p<e)*p++=*s++; *p=0; return p; }
static char* mtu_x(char* p,char* e,u32 v,int n){ for(int i=n-1;i>=0&&p<e;i--)*p++="0123456789abcdef"[(v>>(i*4))&15]; *p=0; return p; }
static char* mtu_u(char* p,char* e,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n&&p<e)*p++=t[--n]; *p=0; return p; }
void mt7921u_status(char* out,int cap){
    struct mt7921_softc* sc=&g_mt7921u; if(cap<2)return; char* p=out; char* e=out+cap-1; *p=0;
    if(sc->state==MT_ST_NONE){ mtu_s(p,e,mt7921u_enable?"mt7921u: no MediaTek MT7921AU USB adapter found\n":"mt7921u: turned off\n"); return; }
    static const char* ST[]={"none","found","firmware missing","failed","firmware running","ready"};
    const struct mt_usb_id* id=mt7921u_match(sc->vendor,sc->device);
    p=mtu_s(p,e,"Adapter="); p=mtu_s(p,e,id?id->name:"MT7921AU"); p=mtu_s(p,e," [USB "); p=mtu_x(p,e,sc->vendor,4); p=mtu_s(p,e,":"); p=mtu_x(p,e,sc->device,4); p=mtu_s(p,e,"]\nState="); p=mtu_s(p,e,ST[sc->state<=MT_ST_READY?sc->state:0]);
    if(sc->hw_rev){ p=mtu_s(p,e," rev 0x"); p=mtu_x(p,e,sc->hw_rev,8); }
    if(sc->fwver[0]){ p=mtu_s(p,e,"\nFirmware="); p=mtu_s(p,e,sc->fwver); }
    if(sc->state==MT_ST_READY){ p=mtu_s(p,e,"\nMAC="); for(int i=0;i<6;i++){ if(i)p=mtu_s(p,e,":"); p=mtu_x(p,e,sc->mac[i],2); } }
    if(sc->err){ p=mtu_s(p,e,"\nLast error: "); p=mtu_s(p,e,sc->err); }
    p=mtu_s(p,e,"\nMCU commands="); p=mtu_u(p,e,sc->mcu_cmds); p=mtu_s(p,e," events="); p=mtu_u(p,e,sc->mcu_events); p=mtu_s(p,e," timeouts="); p=mtu_u(p,e,sc->mcu_timeouts);
    p=mtu_s(p,e,"\nTX packets="); p=mtu_u(p,e,sc->tx_pkts); p=mtu_s(p,e," RX packets="); p=mtu_u(p,e,sc->rx_pkts);
    p=mtu_s(p,e,"\nUSB control errors="); p=mtu_u(p,e,mtu_dev.ctrl_err); p=mtu_s(p,e," bulk errors="); p=mtu_u(p,e,mtu_dev.bulk_err); p=mtu_s(p,e," bad RX="); p=mtu_u(p,e,mtu_dev.rx_bad);
    mtu_s(p,e,"\n"); }
