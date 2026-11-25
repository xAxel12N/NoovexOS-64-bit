// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation */
#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/sysfeat.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/net/wifi/rtw_efuse.h"
#include "include/nvx/drivers/net/wifi/rtw8821ce.h"
#include "include/nvx/drivers/net/wifi/rtw_chip.h"
#include "include/nvx/drivers/net/wifi/rtw_usb.h"
#include "include/nvx/net/wifi/wifi_hw.h"

extern const rtw_chip rtw8821c_chip;
extern const rtw_chip rtw8822b_chip __attribute__((weak));
extern const rtw_chip rtw8822c_chip __attribute__((weak));
extern volatile u64 g_ticks;
extern u8 g_scan_ch;
int rtwusb_xhci_open(void);

const rtwu_lo* g_rtwu=0;
u32 g_rtwu_err=0, g_rtwu_rx=0, g_rtwu_rxdrop=0, g_rtwu_c2h=0, g_rtwu_tx=0, g_rtwu_txagg=0;
static struct featguard rtwu_fg={"Realtek USB Wi-Fi (RTL8821CU/8811CU)","feat-rtwusb.cfg",1,0,3,0,0};

static const struct { u16 vid, pid; u8 k; } rtwu_ids[]={
    {0x0BDA,0x2006,0},{0x0BDA,0x8731,0},{0x0BDA,0x8811,0},{0x0BDA,0xB820,0},{0x0BDA,0xB82B,0},{0x0BDA,0xC80C,0},{0x0BDA,0xC811,0},{0x0BDA,0xC820,0},
    {0x0BDA,0xC821,0},{0x0BDA,0xC82A,0},{0x0BDA,0xC82B,0},{0x2001,0x331D,0},{0x7392,0xC811,0},{0x7392,0xD811,0},{0x2C4E,0x0105,0},
    {0x0BDA,0xB812,1},{0x0BDA,0xB82C,1},{0x0BDA,0x2102,1},{0x0BDA,0xB81A,1},{0x2357,0x012D,1},{0x2357,0x0138,1},{0x2357,0x0115,1},{0x0B05,0x1841,1},{0x0B05,0x184C,1},
    {0x0BDA,0xC82C,2},{0x0BDA,0xC812,2},{0x0BDA,0xC82E,2},{0x0BDA,0xD820,2},{0x0BDA,0xD82B,2},{0x2001,0x3329,2},
};
const rtw_chip* rtwu_chip_for(u16 vid,u16 pid){
    for(u32 i=0;i<sizeof(rtwu_ids)/sizeof(rtwu_ids[0]);i++) if(rtwu_ids[i].vid==vid&&rtwu_ids[i].pid==pid){
        u8 k=rtwu_ids[i].k; return k==0?&rtw8821c_chip:k==1?&rtw8822b_chip:&rtw8822c_chip; }
    return 0;
}

static void rtwu_sec(u32 o,u8 v){ if(o<=0xFFu||(o>=0x1000u&&o<=0x10FFu))g_rtwu->vreq(0x40,0x04E0,0,&v,1); }
static u32 rtwu_rd(u32 o,int n){
    u8 b[4]={0xFF,0xFF,0xFF,0xFF};
    if(!g_rtwu||g_rtwu->vreq(0xC0,(u16)o,0,b,(u16)n)<0){ g_rtwu_err++; return 0xFFFFFFFFu; }
    rtwu_sec(o,b[0]);
    u32 v=(u32)b[0]|((u32)b[1]<<8)|((u32)b[2]<<16)|((u32)b[3]<<24);
    return n==1?v&0xFFu:n==2?v&0xFFFFu:v;
}
static void rtwu_wr(u32 o,u32 v,int n){
    u8 b[4]={(u8)v,(u8)(v>>8),(u8)(v>>16),(u8)(v>>24)};
    if(!g_rtwu)return;
    if(g_rtwu->vreq(0x40,(u16)o,0,b,(u16)n)<0)g_rtwu_err++;
    rtwu_sec(o,b[0]);
}
static u8  rtwu_r8 (u32 h,u32 o){ (void)h; return (u8)rtwu_rd(o,1); }
static u16 rtwu_r16(u32 h,u32 o){ (void)h; return (u16)rtwu_rd(o,2); }
static u32 rtwu_r32(u32 h,u32 o){ (void)h; return rtwu_rd(o,4); }
static void rtwu_w8 (u32 h,u32 o,u8 v){ (void)h; rtwu_wr(o,v,1); }
static void rtwu_w16(u32 h,u32 o,u16 v){ (void)h; rtwu_wr(o,v,2); }
static void rtwu_w32(u32 h,u32 o,u32 v){ (void)h; rtwu_wr(o,v,4); }
const rtw_bus rtw_bus_usb={ "USB", RTW_BUS_USB, rtwu_r8, rtwu_r16, rtwu_r32, rtwu_w8, rtwu_w16, rtwu_w32 };

static const u8 rtwu_rq[5][6]={ {2,2,1,1,0,3},{2,2,1,1,0,3},{2,2,2,3,3,3},{2,2,1,1,3,3},{2,2,1,1,0,3} };
static int rtwu_nout(void){ int n=g_rtwu?g_rtwu->nout:1; return n<1?1:n>4?4:n; }
u16 rtwu_pqmap(int nout){ if(nout<1)nout=1; if(nout>4)nout=4; const u8* q=rtwu_rq[nout];
    return (u16)(((u32)q[0]<<4)|((u32)q[1]<<6)|((u32)q[2]<<8)|((u32)q[3]<<10)|((u32)q[4]<<12)|((u32)q[5]<<14)); }
int rtwu_ep_for(int qsel){
    static const u8 tid_ac[8]={2,3,3,2,1,1,0,0};
    int ac=qsel<8?tid_ac[qsel&7]:qsel==RTWU_QS_MGMT?4:(qsel==RTWU_QS_BEACON||qsel==RTWU_QS_HIGH||qsel==RTWU_QS_H2C)?5:-1;
    if(ac<0)return -1;
    int n=rtwu_nout(); u8 map=rtwu_rq[n][ac];
    int ep=map==3?0:map==2?1:map==1?2:3;
    return ep>=n?n-1:ep;
}
int rtwu_qsel_frame(const u8* f,int n){
    if(n<24)return -1;
    u8 t=f[0]&0x0C;
    if(t==0x00)return RTWU_QS_MGMT;
    if(t!=0x08)return -1;
    if((f[0]&0x80)&&n>=26){ int ds=(f[1]&3)==3; int qo=ds?30:24; if(n>=qo+2)return f[qo]&7; }
    return 0;
}
void rtwu_csum(u8* d,int agg){
    d[31]=(u8)agg; d[28]=0; d[29]=0;
    u16 ck=0; for(int i=0;i<16;i++)ck^=(u16)(d[i*2]|(d[i*2+1]<<8));
    d[28]=(u8)ck; d[29]=(u8)(ck>>8);
}
int rtwu_txdesc(u8* d,const u8* f,int n,int qsel,int rsvd){
    u32 ds=rtw_txd_sz(); if(n<0||n>0xFFFF||qsel<0)return -1;
    for(u32 i=0;i<ds;i++)d[i]=0;
    int bmc=!rsvd&&n>=10&&(f[4]&1);
    u32 w0=((u32)n&0xFFFFu)|((ds&0xFFu)<<16)|(bmc?(1u<<24):0u)|(1u<<26)|(rsvd?(1u<<31):0u);
    d[0]=(u8)w0; d[1]=(u8)(w0>>8); d[2]=(u8)(w0>>16); d[3]=(u8)(w0>>24);
    u32 w1=((u32)qsel&0x1Fu)<<8;
    d[4]=(u8)w1; d[5]=(u8)(w1>>8); d[6]=0; d[7]=0;
    if(!rsvd){ d[6]=8; rtw_txdesc_rate(d,qsel>=0x10); d[33]|=0x80; }
    rtwu_csum(d,0);
    return (int)ds;
}
int rtwu_agg_flush(rtwu_agg* a){
    if(!a->n)return 0;
    if(a->n>1){ rtwu_csum(a->buf,a->n); g_rtwu_txagg++; }
    int r=g_rtwu&&g_rtwu->bulk_out?g_rtwu->bulk_out(a->ep,a->buf,a->len):-1;
    if(r<0)g_rtwu_err++; else g_rtwu_tx+=(u32)a->n;
    a->n=0; a->len=0;
    return r<0?-1:0;
}
int rtwu_agg_add(rtwu_agg* a,const u8* f,int n,int qsel){
    int ep=rtwu_ep_for(qsel); u32 ds=rtw_txd_sz();
    if(ep<0||n<1||(int)ds+n>a->cap)return -1;
    int off=(a->len+7)&~7;
    if(a->n&&(a->ep!=ep||a->n>=RTWU_AGG_MAX||off+(int)ds+n>a->cap)){ rtwu_agg_flush(a); off=0; }
    for(int i=a->len;i<off;i++)a->buf[i]=0;
    rtwu_txdesc(a->buf+off,f,n,qsel,0);
    for(int i=0;i<n;i++)a->buf[off+(int)ds+i]=f[i];
    if(!a->n)a->ep=ep;
    a->len=off+(int)ds+n; a->n++;
    return 0;
}
int rtwu_rx_parse(const u8* b,int n,void (*cb)(const u8* desc,const u8* fr,int len)){
    int off=0, got=0;
    while(off+(int)RTWU_RXD<=n){
        const u8* d=b+off;
        u32 w0=(u32)d[0]|((u32)d[1]<<8)|((u32)d[2]<<16)|((u32)d[3]<<24), w2=(u32)d[8]|((u32)d[9]<<8)|((u32)d[10]<<16)|((u32)d[11]<<24);
        int plen=(int)(w0&0x3FFFu), drv=(int)((w0>>16)&0xFu)*8, sh=(int)((w0>>24)&3u);
        int po=(int)RTWU_RXD+drv+sh, tot=po+plen;
        if(off+tot>n){ g_rtwu_rxdrop++; break; }
        if(w2&(1u<<28))g_rtwu_c2h++;
        else if((w0&(1u<<14))||plen<14)g_rtwu_rxdrop++;
        else { g_rtwu_rx++; got++; if(cb)cb(d,d+po,plen); }
        off+=(tot+7)&~7;
    }
    return got;
}

static const rtw_pwr_cmd rtwu_pwr_on[]={
    {0x004A,0x01,0x00,RTW_PWR_WRITE}, {0x0005,0x98,0x00,RTW_PWR_WRITE},
    {0x0020,0x01,0x01,RTW_PWR_WRITE}, {0x0001,0x00,0x00,RTW_PWR_DELAY}, {0x0000,0x20,0x00,RTW_PWR_WRITE}, {0x0005,0x1C,0x00,RTW_PWR_WRITE},
    {0x0006,0x02,0x02,RTW_PWR_POLL}, {0x0006,0x01,0x01,RTW_PWR_WRITE}, {0x0005,0x80,0x00,RTW_PWR_WRITE}, {0x0005,0x18,0x00,RTW_PWR_WRITE},
    {0x10C3,0x01,0x01,RTW_PWR_WRITE}, {0x0005,0x01,0x01,RTW_PWR_WRITE}, {0x0005,0x01,0x00,RTW_PWR_POLL}, {0x0020,0x08,0x08,RTW_PWR_WRITE},
    {0x007C,0x02,0x00,RTW_PWR_WRITE},
};
static const rtw_pwr_cmd rtwu_pwr_off[]={
    {0x0093,0x08,0x00,RTW_PWR_WRITE}, {0x001F,0xFF,0x00,RTW_PWR_WRITE}, {0x0049,0x02,0x00,RTW_PWR_WRITE}, {0x0006,0x01,0x01,RTW_PWR_WRITE},
    {0x0002,0x02,0x00,RTW_PWR_WRITE}, {0x10C3,0x01,0x00,RTW_PWR_WRITE}, {0x0005,0x02,0x02,RTW_PWR_WRITE}, {0x0005,0x02,0x00,RTW_PWR_POLL},
};
static int rtwu_seq(u32 m,const rtw_pwr_cmd* s,int n){
    int fails=0;
    for(int i=0;i<n;i++){ u16 o=s[i].off;
        if(s[i].cmd==RTW_PWR_DELAY){ rtw_delay_us(1000); continue; }
        if(s[i].cmd==RTW_PWR_POLL){ int ok=0; for(int t=0;t<500&&!ok;t++){ u8 v=rtw_r8(m,o); if(g_rtwu_err>200)break; if((v&s[i].mask)==s[i].val)ok=1; else rtw_delay_us(50); }
            if(!ok){ fails++; g_rtw_pwr_fail_off=o; } continue; }
        u8 v=rtw_r8(m,o); rtw_w8(m,o,(u8)((v&~s[i].mask)|(s[i].val&s[i].mask)));
    }
    return fails;
}
int rtwu_power_on(u32 m){
    rtw_w8(m,0x001C,0);
    if(rtw_r8(m,0x0100)!=0xEA&&!(rtw_r8(m,0x00F4)&1))rtwu_seq(m,rtwu_pwr_off,(int)(sizeof(rtwu_pwr_off)/sizeof(rtwu_pwr_off[0])));
    return rtwu_seq(m,rtwu_pwr_on,(int)(sizeof(rtwu_pwr_on)/sizeof(rtwu_pwr_on[0])));
}
void rtwu_intf_cfg(u32 m){
    int sp=g_rtwu?g_rtwu->speed:3; u8 bs=sp>=4?0:sp==3?1:2;
    rtw_w8(m,0x0290,(u8)((3u<<2)|(1u<<1)|((u32)bs<<4)));
    rtw_w16(m,0x020C,(u16)(rtw_r16(m,0x020C)|(1u<<9)));
    rtw_w8(m,0x010C,(u8)(rtw_r8(m,0x010C)|(1u<<2)));
    rtw_w8(m,0x0283,(u8)(rtw_r8(m,0x0283)&~0x80u));
    rtw_w16(m,0x0280,(u16)(0x05u|(0x20u<<8)));
}
static u8 rtwu_pk[4096+64];
int rtwu_bcnq_stage(u32 m,const u8* payload,u32 plen,u32 pghead){
    if(!g_rtwu||!g_rtwu->bulk_out)return -1;
    if(plen>4096)plen=4096;
    u32 ds=(u32)rtwu_txdesc(rtwu_pk,payload,(int)plen,RTWU_QS_BEACON,1);
    for(u32 i=0;i<plen;i++)rtwu_pk[ds+i]=payload[i];
    u16 bk204=rtw_r16(m,0x0204); u8 bk101=rtw_r8(m,0x0101), bk550=rtw_r8(m,0x0550), bk422=rtw_r8(m,0x0422);
    rtw_w16(m,0x0204,(u16)((pghead&0x0FFFu)|(1u<<15)));
    rtw_w8(m,0x0101,(u8)(bk101|1u));
    rtw_w8(m,0x0550,(u8)((bk550&~(1u<<3))|(1u<<4)));
    rtw_w8(m,0x0422,(u8)(bk422&~(1u<<6)));
    int r=g_rtwu->bulk_out(rtwu_ep_for(RTWU_QS_BEACON),rtwu_pk,(int)(ds+plen));
    int ok=0; for(int t=0;t<200&&r>=0;t++){ if(rtw_r16(m,0x0204)&(1u<<15)){ ok=1; break; } rtw_delay_us(20); }
    rtw_w16(m,0x0204,(u16)((bk204&0x0FFFu)|(1u<<15))); rtw_w8(m,0x0550,bk550); rtw_w8(m,0x0422,bk422); rtw_w8(m,0x0101,bk101);
    return r<0?-1:ok?0:-2;
}
static void rtwu_log(const char* a,const char* b,int v){ char t[120],*p=t; p=scopy(p,"wifi usb: "); p=scopy(p,a); if(b)p=scopy(p,b); if(v>=0){ *p++=' '; p=udec(p,(u32)v); } *p=0; klog(t); }
int rtwu_bringup(u32 m){
    const rtw_chip* c=g_rtw_chip;
    if(!c||!c->fw_start||c->wcpu!=RTW_WCPU_3081)return -1;
    int pf=rtwu_power_on(m); if(pf)rtwu_log("power-on steps that did not finish:",0,pf);
    if(g_rtwu_err>20)return -2;
    int r=rtw_fw_download_iddma(m,c->fw_start,(long)(c->fw_end-c->fw_start));
    if(r){ rtwu_log("firmware did not start, code",0,r<0?-r:r); return -3; }
    rtw_mac_trx_init(m);
    rtw_w16(m,0x010C,(u16)((rtw_r16(m,0x010C)&0x000Fu)|rtwu_pqmap(rtwu_nout())));
    rtwu_intf_cfg(m);
    int tip=c->tables_in_phy_init;
    if(tip){ if(!rtw_efuse_valid)rtw_efuse_load(m); else rtw_efuse_apply(); rtw_cond_setup(m); c->mac_init(m); if(c->mac_tbl.t&&c->mac_tbl.n)rtw_apply_cond(m,c->mac_tbl.t,c->mac_tbl.n,c->mac_tbl.sz,"mac",0); }
    c->phy_init(m);
    if(!tip){ if(!rtw_efuse_valid)rtw_efuse_load(m); else rtw_efuse_apply(); rtw_cond_setup(m);
        c->mac_init(m);
        rtw_apply_cond(m,c->mac_tbl.t,c->mac_tbl.n,c->mac_tbl.sz,"mac",0);
        rtw_apply_cond(m,c->bb_tbl.t,c->bb_tbl.n,c->bb_tbl.sz,"bb",0);
        rtw_apply_cond(m,c->agc_tbl.t,c->agc_tbl.n,c->agc_tbl.sz,"agc",0);
        if(c->agc_btg_tbl.t&&(g_rtw_rfe==2||g_rtw_rfe==4))rtw_apply_cond(m,c->agc_btg_tbl.t,c->agc_btg_tbl.n,c->agc_btg_tbl.sz,"agc-btg",0);
        for(int pa=0,np=rtw_rf_paths(m);pa<np;pa++) if(c->rf_tbl[pa].t){ g_rtw_rf_path=pa; rtw_apply_cond(m,c->rf_tbl[pa].t,c->rf_tbl[pa].n,c->rf_tbl[pa].sz,pa?"rf-b":"rf-a",0); }
        g_rtw_rf_path=0; }
    if(rtw_efuse_valid)for(int i=0;i<6;i++)rtw_w8(m,0x0610+i,g_rtw_efuse.mac[i]);
    if(c->phy_post)c->phy_post(m);
    rtw_rx_enable(m);
    if(c->coex_wifi_only)c->coex_wifi_only(m);
    c->set_channel(m,1,RTW_BW_20);
    rtw_set_nettype(m,0,0);
    return g_rtwu_err>50?-4:0;
}

static rtwu_agg rtwu_txa; static u8 rtwu_txb[RTWU_TXBUF]; static u8 rtwu_rxb[RTWU_RXBUF];
static u8 rtwu_scl[64]; static int rtwu_scn=0, rtwu_sci=0, rtwu_on=0, rtwu_rssi=-127; static u64 rtwu_sct=0;
static u8 rtwu_bss[6]; static u8 rtwu_want=0; static u8 rtwu_wbody[512]; static int rtwu_wlen=-1;
static void rtwu_rx_cb(const u8* d,const u8* fr,int len){
    if(len>=4)len-=4;
    if(len<24)return;
    int rssi=rtw_rx_rssi(d);
    int frm=fr[10]==rtwu_bss[0]&&fr[11]==rtwu_bss[1]&&fr[12]==rtwu_bss[2]&&fr[13]==rtwu_bss[3]&&fr[14]==rtwu_bss[4]&&fr[15]==rtwu_bss[5];
    if(frm&&rssi>-127)rtwu_rssi=rssi;
    if(rtwu_want&&frm&&(fr[0]&0xFC)==rtwu_want&&rtwu_wlen<0){ int bl=len-24; if(bl>512)bl=512; for(int i=0;i<bl;i++)rtwu_wbody[i]=fr[24+i]; rtwu_wlen=bl; }
    wifi_hw_rx(fr,len,rssi);
}
static void rtwu_set_ch(u8 ch){ if(ch&&g_rtw_chip&&g_rtw_chip->set_channel){ g_rtw_chip->set_channel(RTWU_H,ch,RTW_BW_20); g_scan_ch=ch; } }
static void rtwu_poll(void* c){
    (void)c; if(!rtwu_on||!g_rtwu)return;
    rtwu_agg_flush(&rtwu_txa);
    for(int k=0;k<4;k++){ int n=g_rtwu->bulk_in(rtwu_rxb,RTWU_RXBUF,k?1:3); if(n<=0)break; rtwu_rx_parse(rtwu_rxb,n,rtwu_rx_cb); }
}
static int rtwu_init(void* c){
    (void)c; if(rtwu_on)return 0;
    if(!g_rtwu)return -1;
    const rtw_chip* ch=rtwu_chip_for(g_rtwu->vid,g_rtwu->pid); if(!ch)return -1;
    if(!fg_begin(&rtwu_fg))return -1;
    const rtw_bus* ob=g_rtw_bus; const rtw_chip* oc=g_rtw_chip;
    g_rtw_bus=&rtw_bus_usb; g_rtw_chip=ch; g_rtwu_err=0;
    rtwu_txa.buf=rtwu_txb; rtwu_txa.cap=RTWU_TXBUF; rtwu_txa.len=0; rtwu_txa.n=0;
    int r=rtwu_bringup(RTWU_H);
    if(r){ g_rtw_bus=ob; g_rtw_chip=oc; rtwu_log("adapter bring-up failed, step",0,-r); fg_fail(&rtwu_fg,"USB Wi-Fi bring-up failed"); return -1; }
    fg_ok(&rtwu_fg); rtwu_on=1; rtwu_log(ch->name," USB adapter running, bulk-out endpoints",rtwu_nout());
    return 0;
}
static int rtwu_scan_start(void* c,const u8* ch,int n){
    (void)c; if(!rtwu_on||!ch||n<1)return -1;
    if(n>64){ n=64; }
    for(int i=0;i<n;i++)rtwu_scl[i]=ch[i];
    rtwu_scn=n; rtwu_sci=0; rtwu_sct=g_ticks; rtwu_set_ch(ch[0]); return 0;
}
static int rtwu_scan_done(void* c){
    (void)c; if(!rtwu_scn)return 1;
    if(g_ticks-rtwu_sct<100)return 0;
    if(++rtwu_sci>=rtwu_scn){ rtwu_scn=0; return 1; }
    rtwu_sct=g_ticks; rtwu_set_ch(rtwu_scl[rtwu_sci]); return 0;
}
static int rtwu_mac(void* c,u8* mac){ (void)c; if(!rtw_efuse_valid)return -1; for(int i=0;i<6;i++)mac[i]=g_rtw_efuse.mac[i]; return 0; }
static int rtwu_tx(void* c,const u8* f,int n){
    (void)c; if(!rtwu_on||!f||n<24)return -1;
    int q=rtwu_qsel_frame(f,n); if(q<0)return -1;
    if(rtwu_agg_add(&rtwu_txa,f,n,q)<0)return -1;
    if(q>=0x10)return rtwu_agg_flush(&rtwu_txa);
    return 0;
}
static int rtwu_xchg(const u8* f,int n,u8 want,int ms){
    rtwu_want=want; rtwu_wlen=-1;
    if(rtwu_tx(0,f,n)<0){ rtwu_want=0; return -1; }
    u64 t0=g_ticks; while(rtwu_wlen<0&&g_ticks-t0<(u64)ms){ rtwu_poll(0); if(!g_rtwu)break; }
    rtwu_want=0; return rtwu_wlen;
}
static int rtwu_hdr(u8* f,u8 fc,const u8* bssid,const u8* me){ for(int i=0;i<24;i++)f[i]=0; f[0]=fc; for(int i=0;i<6;i++){ f[4+i]=bssid[i]; f[10+i]=me[i]; f[16+i]=bssid[i]; } return 24; }
static int rtwu_join(void* c,const u8* bssid,u8 ch,const char* ssid,u8 akm,u8 ht){
    (void)c; (void)ht; if(!rtwu_on)return 3;
    u8 me[6]; if(rtwu_mac(0,me)<0)return 3;
    for(int i=0;i<6;i++){ rtwu_bss[i]=bssid[i]; rtw_w8(RTWU_H,0x0618+i,bssid[i]); }
    rtwu_scn=0; rtwu_set_ch(ch);
    u8 f[256]; int n=rtwu_hdr(f,0xB0,bssid,me); f[n++]=0; f[n++]=0; f[n++]=1; f[n++]=0; f[n++]=0; f[n++]=0;
    int bl=-1; for(int t=0;t<3&&bl<0;t++)bl=rtwu_xchg(f,n,0xB0,300);
    if(bl<6||rtwu_wbody[4]||rtwu_wbody[5]){ rtwu_log("authentication not answered or refused","",bl<6?-1:rtwu_wbody[4]); return 3; }
    n=rtwu_hdr(f,0x00,bssid,me); u16 cap=(u16)(0x0001u|0x0400u|(akm?0x0010u:0u)); f[n++]=(u8)cap; f[n++]=(u8)(cap>>8); f[n++]=10; f[n++]=0;
    int sl=0; while(ssid&&ssid[sl]&&sl<32)sl++; f[n++]=0; f[n++]=(u8)sl; for(int i=0;i<sl;i++)f[n++]=(u8)ssid[i];
    static const u8 r2[]={1,8,0x82,0x84,0x8B,0x96,0x0C,0x12,0x18,0x24,50,4,0x30,0x48,0x60,0x6C}, r5[]={1,8,0x8C,0x12,0x98,0x24,0xB0,0x48,0x60,0x6C};
    const u8* rs=ch>14?r5:r2; int rl=ch>14?(int)sizeof(r5):(int)sizeof(r2); for(int i=0;i<rl;i++)f[n++]=rs[i];
    if(akm){ static const u8 rsn[]={0x30,0x14,1,0,0,0x0F,0xAC,4,1,0,0,0x0F,0xAC,4,1,0,0,0x0F,0xAC,2,0,0}; for(u32 i=0;i<sizeof(rsn);i++)f[n++]=rsn[i]; if(!(akm&1)&&(akm&4))f[n-3]=1; }
    bl=-1; for(int t=0;t<3&&bl<0;t++)bl=rtwu_xchg(f,n,0x10,500);
    if(bl<6||rtwu_wbody[2]||rtwu_wbody[3]){ rtwu_log("association not answered or refused","",bl<6?-1:rtwu_wbody[2]); return 3; }
    rtw_set_nettype(RTWU_H,2,0);
    return 0;
}
static void rtwu_leave(void* c){ (void)c; if(!rtwu_on)return; rtw_set_nettype(RTWU_H,0,0); for(int i=0;i<6;i++){ rtwu_bss[i]=0; rtw_w8(RTWU_H,0x0618+i,0); } }
static int rtwu_get_rssi(void* c){ (void)c; return rtwu_rssi; }
static const struct wifi_hw_ops rtwu_ops={ .name="rtw88 USB", .flags=0, .init=rtwu_init, .scan_start=rtwu_scan_start, .scan_done=rtwu_scan_done, .join=rtwu_join, .leave=rtwu_leave,
    .tx=rtwu_tx, .get_rssi=rtwu_get_rssi, .poll=rtwu_poll, .get_mac=rtwu_mac };
int rtwu_attach(const rtwu_lo* lo){ if(!lo||!lo->vreq||!lo->bulk_out||!lo->bulk_in||!rtwu_chip_for(lo->vid,lo->pid))return -1; g_rtwu=lo; return 0; }
void rtwu_detach(void){ if(g_rtw_bus==&rtw_bus_usb)g_rtw_bus=&rtw_bus_pcie; g_rtwu=0; rtwu_on=0; rtwu_scn=0; }
void rtwusb_wifi_hw_probe(void){
    fg_boot(&rtwu_fg); if(!rtwu_fg.on||rtwu_fg.dead)return;
    if(!g_rtwu&&!rtwusb_xhci_open())return;
    if(g_rtwu)wifi_hw_register(&rtwu_ops,0);
}
