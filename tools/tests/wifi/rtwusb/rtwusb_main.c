
static int bad=0, checks=0;
#define CK(c,m) do{ checks++; if(!(c)){ bad++; printf("FAIL %s (line %d)\n",m,__LINE__); } }while(0)
volatile u64 g_ticks=0; u8 g_scan_ch=0;
int rtw_efuse_valid=1; rtw_efuse_info g_rtw_efuse; int g_rtw_rfe=0, g_rtw_rf_path=0; u16 g_rtw_pwr_fail_off=0;
static u8 dummy_r8(u32 h,u32 o){ (void)h;(void)o; return 0; } static u16 dummy_r16(u32 h,u32 o){ (void)h;(void)o; return 0; } static u32 dummy_r32(u32 h,u32 o){ (void)h;(void)o; return 0; }
static void dummy_w8(u32 h,u32 o,u8 v){ (void)h;(void)o;(void)v; } static void dummy_w16(u32 h,u32 o,u16 v){ (void)h;(void)o;(void)v; } static void dummy_w32(u32 h,u32 o,u32 v){ (void)h;(void)o;(void)v; }
const rtw_bus rtw_bus_pcie={ "PCIe", RTW_BUS_PCIE, dummy_r8, dummy_r16, dummy_r32, dummy_w8, dummy_w16, dummy_w32 };
const rtw_bus* g_rtw_bus=&rtw_bus_pcie;
static int n_chan=0, n_phy=0, n_mac=0, last_ch=0, n_fw=0, n_trx=0, nettype=0, fw_fail=0;
static void st_phy(u32 m){ (void)m; n_phy++; } static int st_mac(u32 m){ (void)m; n_mac++; return 0; } static void st_ch(u32 m,u8 ch,u8 bw){ (void)m;(void)bw; n_chan++; last_ch=ch; }
static const unsigned char fwb[64];
const rtw_chip rtw8821c_chip={ .name="RTL8821C", .vendor=0x10EC, .device=0xC821, .tx_desc_sz=48, .wcpu=RTW_WCPU_3081, .fw_start=fwb, .fw_end=fwb+64, .phy_init=st_phy, .mac_init=st_mac, .set_channel=st_ch, .n_paths=1 };
const rtw_chip rtw8822b_chip={ .name="RTL8822B", .tx_desc_sz=48, .wcpu=RTW_WCPU_3081, .fw_start=fwb, .fw_end=fwb+64, .phy_init=st_phy, .mac_init=st_mac, .set_channel=st_ch, .n_paths=2 };
const rtw_chip rtw8822c_chip={ .name="RTL8822C", .tx_desc_sz=48, .wcpu=RTW_WCPU_3081, .fw_start=fwb, .fw_end=fwb+64, .phy_init=st_phy, .mac_init=st_mac, .set_channel=st_ch, .n_paths=2 };
const rtw_chip* g_rtw_chip=&rtw8821c_chip;
void rtw_delay_us(u32 us){ (void)us; }
void klog(const char* s){ if(getenv("V"))printf("klog: %s\n",s); }
char* scopy(char* p,const char* s){ while(*s)*p++=*s++; *p=0; return p; }
char* udec(char* p,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)(48+v%10); v/=10; }while(v); while(n)*p++=t[--n]; *p=0; return p; }
char* hx(char* p,u32 v,int dg){ for(int i=dg-1;i>=0;i--)*p++="0123456789ABCDEF"[(v>>(i*4))&15]; *p=0; return p; }
static int fgf=0, fgo=0;
void fg_boot(struct featguard* g){ (void)g; } int fg_begin(struct featguard* g){ return g->on&&!g->dead; } void fg_ok(struct featguard* g){ (void)g; fgo++; } void fg_fail(struct featguard* g,const char* w){ (void)w; fgf++; if(++g->fails>=g->maxfail)g->dead=1; }
void rtw_txdesc_rate(u8* pkt,int mgmt){ pkt[4+2]=(u8)((pkt[6]&~0x1F)|(mgmt?8:9)); pkt[13]|=1; }
int rtw_rx_rssi(const u8* base){ (void)base; return -40; }
int rtw_fw_download_iddma(u32 m,const u8* fw,long sz){ (void)m;(void)fw; n_fw++; return sz==64&&!fw_fail?0:1; }
int rtw_mac_trx_init(u32 m){ rtw_w16(m,0x010C,0x0000); n_trx++; return 0; }
int rtw_efuse_load(u32 m){ (void)m; return 1; } void rtw_efuse_apply(void){} void rtw_cond_setup(unsigned m){ (void)m; }
int rtw_apply_cond(unsigned m,const unsigned* t,int n,unsigned char sz,const char* l,int v){ (void)m;(void)t;(void)n;(void)sz;(void)l;(void)v; return 0; }
int rtw_rf_paths(u32 m){ (void)m; return 1; }
int rtw_rx_enable(u32 m){ rtw_w32(m,0x0608,0xF400220Eu); return 1; }
void rtw_set_nettype(u32 m,int t,int l){ (void)m;(void)l; nettype=t; }
static const struct wifi_hw_ops* reg_ops=0; static int nrx=0, nrx_mgmt=0, lastrx_len=0; static u8 lastrx[2400];
int wifi_hw_register(const struct wifi_hw_ops* o,void* c){ (void)c; reg_ops=o; return 0; }
void wifi_hw_rx(const u8* f,int len,int rssi){ (void)rssi; nrx++; if((f[0]&0x0C)==0)nrx_mgmt++; lastrx_len=len; memcpy(lastrx,f,(size_t)len); }
static int xopen_calls=0; int rtwusb_xhci_open(void){ xopen_calls++; return 0; }

static u8 regs[0x10000]; static int nvr=0, nshadow=0, stuck=0, bcn_mode=0, ap_mode=0, n_bo=0, last_ep=-1, last_len=0, vfail=0; static u8 last_bo[RTWU_TXBUF];
static u8 rxq[RTWU_RXBUF]; static int rxq_len=0; static int ep_hist[8];
static int sim_vreq(u8 rt,u16 val,u16 idx,u8* buf,u16 len){
    nvr++; if(idx!=0||len<1||len>4)return -1; if(vfail)return -1;
    if(rt==0xC0){ if(val==0x0006&&!stuck)regs[6]|=2; for(int i=0;i<len;i++)buf[i]=regs[(u16)(val+i)]; return len; }
    if(rt!=0x40)return -1;
    if(val==0x04E0){ nshadow++; regs[0x4E0]=buf[0]; return 1; }
    for(int i=0;i<len;i++)regs[(u16)(val+i)]=buf[i];
    if(val==0x0204&&len>=2&&(buf[1]&0x80))regs[0x205]&=0x7F;
    if(val==0x0205&&(buf[0]&0x80))regs[0x205]&=0x7F;
    if(val==0x0005&&(buf[0]&1)&&!stuck)regs[5]&=~1;
    if(val==0x0005&&(buf[0]&2)&&!stuck)regs[5]&=~2;
    return len;
}
static int mkrx(u8* b,int off,const u8* f,int fl,int drv,int sh,int crc,int c2h){
    u8* d=b+off; memset(d,0,24); int pl=fl+4; u32 w0=(u32)pl|((u32)crc<<14)|((u32)(drv/8)<<16)|((u32)sh<<24)|(drv?(1u<<26):0u);
    d[0]=(u8)w0; d[1]=(u8)(w0>>8); d[2]=(u8)(w0>>16); d[3]=(u8)(w0>>24); if(c2h)d[11]=0x10;
    memset(d+24,0x11,(size_t)(drv+sh)); memcpy(d+24+drv+sh,f,(size_t)fl); memset(d+24+drv+sh+fl,0xEE,4);
    return (off+24+drv+sh+pl+7)&~7;
}
static const u8 AP[6]={0x02,0xAA,0xBB,0xCC,0xDD,0x01}, ME[6]={0x00,0xE0,0x4C,0x88,0x21,0x01};
static void ap_frame(u8 fc,int status){ u8 f[64]; memset(f,0,sizeof f); f[0]=fc; memcpy(f+4,ME,6); memcpy(f+10,AP,6); memcpy(f+16,AP,6); int n=24;
    if(fc==0xB0){ f[n++]=0; f[n++]=0; f[n++]=2; f[n++]=0; f[n++]=(u8)status; f[n++]=0; } else { f[n++]=0x31; f[n++]=0x04; f[n++]=(u8)status; f[n++]=0; f[n++]=1; f[n++]=0xC0; }
    rxq_len=mkrx(rxq,rxq_len,f,n,32,0,0,0); }
static int sim_bulk_out(int ep,const u8* p,int n){
    n_bo++; last_ep=ep; last_len=n; if(ep>=0&&ep<8)ep_hist[ep]++; memcpy(last_bo,p,(size_t)n);
    if(bcn_mode)regs[0x205]|=0x80;
    if(ap_mode&&n>48+24){ u8 fc=p[48]; if(fc==0xB0)ap_frame(0xB0,ap_mode==2?1:0); else if(fc==0x00&&ap_mode!=3)ap_frame(0x10,0); }
    return n;
}
static int sim_bulk_in(u8* p,int cap,int ms){ g_ticks+=(u64)(ms?ms:1); if(!rxq_len)return 0; int n=rxq_len<cap?rxq_len:cap; memcpy(p,rxq,(size_t)n); rxq_len=0; return n; }
static rtwu_lo lo={ sim_vreq, sim_bulk_out, sim_bulk_in, 0x0BDA, 0xC811, 2, 3 };
static int cbn=0, cbl[8]; static u8 cbf0[8];
static void cb(const u8* d,const u8* fr,int len){ (void)d; if(cbn<8){ cbl[cbn]=len; cbf0[cbn]=fr[0]; } cbn++; }
static int x16(const u8* d){ u16 x=0; for(int i=0;i<16;i++)x^=(u16)(d[i*2]|(d[i*2+1]<<8)); return x; }
static u32 rd32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }

int main(void){
    CK(rtwu_chip_for(0x0BDA,0xC811)==&rtw8821c_chip&&rtwu_chip_for(0x0BDA,0xC820)==&rtw8821c_chip&&rtwu_chip_for(0x2C4E,0x0105)==&rtw8821c_chip,"8821CU/8811CU ids");
    CK(rtwu_chip_for(0x0BDA,0xB812)==&rtw8822b_chip&&rtwu_chip_for(0x0BDA,0xC812)==&rtw8822c_chip,"8822BU/8822CU ids");
    CK(!rtwu_chip_for(0x0BDA,0x8179)&&!rtwu_chip_for(0x10EC,0xC821),"unknown ids rejected");
    rtwu_lo badlo=lo; badlo.pid=0x1234; CK(rtwu_attach(&badlo)<0&&!g_rtwu,"attach refuses unknown device");
    CK(rtwu_attach(&lo)==0&&g_rtwu==&lo,"attach");
    g_rtw_bus=&rtw_bus_usb;
    rtw_w32(RTWU_H,0x0610,0x11223344u); CK(regs[0x610]==0x44&&regs[0x613]==0x11,"w32 little endian over vendor request");
    CK(rtw_r32(RTWU_H,0x0610)==0x11223344u&&rtw_r16(RTWU_H,0x0612)==0x1122&&rtw_r8(RTWU_H,0x0611)==0x33,"r8/r16/r32");
    int s0=nshadow; rtw_w8(RTWU_H,0x0610,1); CK(nshadow==s0,"no 0x4E0 write outside the always-on section");
    rtw_w8(RTWU_H,0x0080,0x5A); CK(nshadow==s0+1&&regs[0x4E0]==0x5A,"0x4E0 shadow write for 0x00-0xFF");
    rtw_r8(RTWU_H,0x1080); CK(nshadow==s0+2,"0x4E0 shadow write for 0x1000-0x10FF reads");
    vfail=1; u32 e0=g_rtwu_err; CK(rtw_r32(RTWU_H,0x100)==0xFFFFFFFFu&&g_rtwu_err==e0+1,"failed read returns all ones, counted"); vfail=0;
    CK(rtwu_pqmap(2)==(u16)((2<<4)|(2<<6)|(2<<8)|(3<<10)|(3<<12)|(3<<14)),"queue map, 2 bulk-out");
    CK(rtwu_pqmap(3)==(u16)((2<<4)|(2<<6)|(1<<8)|(1<<10)|(3<<12)|(3<<14)),"queue map, 3 bulk-out");
    lo.nout=2; CK(rtwu_ep_for(0)==1&&rtwu_ep_for(6)==1&&rtwu_ep_for(1)==0&&rtwu_ep_for(RTWU_QS_MGMT)==0&&rtwu_ep_for(RTWU_QS_BEACON)==0,"2 endpoints: data normal, bk/mgmt/beacon high");
    lo.nout=3; CK(rtwu_ep_for(0)==2&&rtwu_ep_for(1)==2&&rtwu_ep_for(4)==1&&rtwu_ep_for(RTWU_QS_MGMT)==0,"3 endpoints: be/bk low, vi normal, mgmt high");
    lo.nout=4; CK(rtwu_ep_for(RTWU_QS_MGMT)==3&&rtwu_ep_for(RTWU_QS_H2C)==0,"4 endpoints: mgmt extra");
    lo.nout=1; CK(rtwu_ep_for(0)==0&&rtwu_ep_for(RTWU_QS_MGMT)==0,"1 endpoint: all on it");
    CK(rtwu_ep_for(9)<0&&rtwu_ep_for(0x1F)<0,"invalid queue select");
    lo.nout=2;
    u8 mg[30]; memset(mg,0,sizeof mg); mg[0]=0xB0; memcpy(mg+4,AP,6);
    u8 qd[40]; memset(qd,0,sizeof qd); qd[0]=0x88; qd[1]=0x01; qd[24]=5;
    u8 dd[40]; memset(dd,0,sizeof dd); dd[0]=0x08; dd[4]=0xFF;
    CK(rtwu_qsel_frame(mg,30)==RTWU_QS_MGMT&&rtwu_qsel_frame(qd,40)==5&&rtwu_qsel_frame(dd,40)==0,"queue select from frame (mgmt, QoS TID, plain data)");
    u8 d[64]; CK(rtwu_txdesc(d,mg,30,RTWU_QS_MGMT,0)==48,"desc size");
    u32 w0=rd32(d), w1=rd32(d+4);
    CK((w0&0xFFFF)==30&&((w0>>16)&0xFF)==48&&(w0&(1u<<26))&&!(w0&(1u<<31))&&!(w0&(1u<<24)),"desc w0: size, offset, LS");
    CK(((w1>>8)&0x1F)==RTWU_QS_MGMT&&(d[33]&0x80)&&x16(d)==0,"desc qsel, HW sequence, checksum");
    rtwu_txdesc(d,dd,40,0,0); CK(rd32(d)&(1u<<24),"broadcast data sets BMC");
    u8 big[RTWU_TXBUF]; rtwu_agg a={big,0,0,0,RTWU_TXBUF}; int bo0=n_bo;
    CK(rtwu_agg_add(&a,qd,37,0)==0&&rtwu_agg_add(&a,qd,40,0)==0&&rtwu_agg_add(&a,qd,33,3)==0&&n_bo==bo0,"three data frames queued");
    CK(a.n==3&&a.len==((48+37+7)&~7)+((48+40+7)&~7)+48+33,"8-byte alignment between aggregated frames");
    rtwu_agg_add(&a,qd,20,0); CK(n_bo==bo0+1&&last_ep==1&&last_bo[31]==3&&x16(last_bo)==0,"fourth frame flushes, first desc carries agg count 3 and checksum");
    int o2=(48+37+7)&~7; CK(rd32(last_bo+o2)&0xFFFF&&(rd32(last_bo+o2)&0xFFFF)==40&&last_bo[o2+31]==0,"second desc at aligned offset, no agg count");
    rtwu_agg_add(&a,mg,30,RTWU_QS_MGMT); CK(n_bo==bo0+2&&last_ep==1&&last_bo[31]==0&&x16(last_bo)==0,"queue change flushes single frame without agg count");
    rtwu_agg_flush(&a); CK(n_bo==bo0+3&&last_ep==0&&((rd32(last_bo+4)>>8)&0x1F)==RTWU_QS_MGMT,"mgmt on high endpoint");
    CK(rtwu_agg_flush(&a)==0&&n_bo==bo0+3,"empty flush sends nothing");
    CK(rtwu_agg_add(&a,qd,RTWU_TXBUF,0)<0,"oversize frame refused");
    u8 rb[4096]; int off=0; u8 fr1[60]; memset(fr1,0,60); fr1[0]=0x80; u8 fr2[100]; memset(fr2,0,100); fr2[0]=0x08;
    off=mkrx(rb,off,fr1,60,32,0,0,0); off=mkrx(rb,off,fr2,100,0,2,0,0); off=mkrx(rb,off,fr2,50,32,0,1,0); off=mkrx(rb,off,fr2,40,0,0,0,1); off=mkrx(rb,off,fr1,30,8,1,0,0);
    u32 dr0=g_rtwu_rxdrop, c0=g_rtwu_c2h; cbn=0;
    CK(rtwu_rx_parse(rb,off,cb)==3&&cbn==3&&cbl[0]==64&&cbf0[0]==0x80&&cbl[1]==104&&cbf0[1]==0x08&&cbl[2]==34,"RX aggregation: 3 good frames with phy status and shift");
    CK(g_rtwu_rxdrop==dr0+1&&g_rtwu_c2h==c0+1,"RX: CRC error dropped, C2H counted");
    cbn=0; CK(rtwu_rx_parse(rb,off-9,cb)==2&&g_rtwu_rxdrop==dr0+3,"RX: truncated last packet dropped");
    cbn=0; CK(rtwu_rx_parse(rb,10,cb)==0&&cbn==0,"RX: short buffer");
    u8 junk[4096]; srand(7); for(int it=0;it<2000;it++){ int n=rand()%4096; for(int i=0;i<n;i++)junk[i]=(u8)rand(); rtwu_rx_parse(junk,n,cb); } CK(1,"RX fuzz 2000 buffers");
    memset(regs,0,sizeof regs); regs[0x100]=0xEA; nvr=0;
    CK(rtwu_power_on(RTWU_H)==0&&(regs[0x10C3]&1)&&(regs[0x20]&9)==9&&!(regs[0x4A]&1)&&!(regs[0x7C]&2),"USB power-on sequence completes");
    memset(regs,0,sizeof regs); regs[0x100]=0x00; regs[0x5]=0; int pv=nvr;
    rtwu_power_on(RTWU_H); CK(nvr-pv>60,"powered chip is switched off first");
    memset(regs,0,sizeof regs); regs[0x100]=0xEA; stuck=1; pv=nvr;
    CK(rtwu_power_on(RTWU_H)==2&&nvr-pv<3000,"stuck power poll fails, bounded"); stuck=0;
    lo.speed=3; rtwu_intf_cfg(RTWU_H); CK(regs[0x290]==((3<<2)|(1<<1)|(1<<4))&&(regs[0x20D]&2)&&(regs[0x10C]&4)&&regs[0x280]==5&&regs[0x281]==0x20,"USB 2 burst size, RX aggregation on");
    lo.speed=4; rtwu_intf_cfg(RTWU_H); CK(regs[0x290]==((3<<2)|(1<<1)),"USB 3 burst size");
    memset(regs,0,sizeof regs); regs[0x101]=0x02; regs[0x550]=0x08; regs[0x422]=0x40; bcn_mode=1;
    u8 pay[300]; for(int i=0;i<300;i++)pay[i]=(u8)i;
    CK(rtwu_bcnq_stage(RTWU_H,pay,300,0x7F0)==0,"reserved page write over bulk accepted");
    CK(last_ep==0&&last_len==348&&((rd32(last_bo+4)>>8)&0x1F)==0x10&&(rd32(last_bo)&(1u<<31))&&(rd32(last_bo)&0xFFFF)==300&&last_bo[48+299]==43&&x16(last_bo)==0,"reserved page: beacon queue desc + payload");
    CK(regs[0x101]==0x02&&regs[0x550]==0x08&&regs[0x422]==0x40,"reserved page: registers restored");
    bcn_mode=0; CK(rtwu_bcnq_stage(RTWU_H,pay,300,0)==-2,"reserved page without BCN_VALID reports -2");
    g_rtw_bus=&rtw_bus_pcie; memset(regs,0,sizeof regs); regs[0x100]=0xEA;
    rtwusb_wifi_hw_probe(); CK(reg_ops&&reg_ops->init&&!(reg_ops->flags&WIFI_HW_NATIVE),"adapter registered as host-MLME (generic) driver");
    lo.nout=2; CK(reg_ops->init(0)==0&&g_rtw_bus==&rtw_bus_usb&&n_fw==1&&n_trx==1&&n_phy==1&&n_mac==1&&last_ch==1&&fgo==1,"bring-up: power, firmware, TRX, PHY, MAC, channel");
    CK((rtw_r16(RTWU_H,0x010C)&0xFFF0)==rtwu_pqmap(2)&&(regs[0x10C]&4),"queue map rewritten for the USB endpoint count");
    memcpy(g_rtw_efuse.mac,ME,6); u8 mac[6]; CK(reg_ops->get_mac(0,mac)==0&&!memcmp(mac,ME,6),"MAC from efuse");
    u8 chl[3]={1,6,11}; CK(reg_ops->scan_start(0,chl,3)==0&&last_ch==1&&g_scan_ch==1,"scan starts on first channel");
    CK(reg_ops->scan_done(0)==0,"scan dwell"); g_ticks+=101; CK(reg_ops->scan_done(0)==0&&last_ch==6,"scan steps to channel 6"); g_ticks+=101; reg_ops->scan_done(0); g_ticks+=101; CK(reg_ops->scan_done(0)==1&&last_ch==11,"scan done after last channel");
    off=mkrx(rxq,0,fr1,60,32,0,0,0); rxq_len=off; nrx=0; reg_ops->poll(0); CK(nrx==1&&lastrx_len==60,"poll delivers RX frames without FCS");
    ap_mode=1; bo0=n_bo; CK(reg_ops->join(0,AP,6,"home",1,0)==0&&nettype==2&&last_ch==6,"join: auth + assoc (WPA2-PSK RSN IE)");
    CK(n_bo==bo0+2,"two management frames sent");
    { const u8* f=last_bo+48; int L=last_len-48; int rsn=0; for(int i=28;i+1<L;i+=2+f[i+1]) if(f[i]==0x30&&f[i+1]==0x14&&f[i+19]==2)rsn=1; CK(f[0]==0x00&&!memcmp(f+4,AP,6)&&!memcmp(f+10,ME,6)&&rsn&&(f[24]&0x10),"assoc request: addresses, privacy bit, RSN PSK"); }
    CK(regs[0x618]==AP[0]&&regs[0x61D]==AP[5],"BSSID register");
    ap_mode=2; u64 t0=g_ticks; CK(reg_ops->join(0,AP,6,"home",0,0)==3&&g_ticks-t0<5000,"refused authentication fails, bounded");
    ap_mode=0; t0=g_ticks; CK(reg_ops->join(0,AP,6,"home",0,0)==3&&g_ticks-t0<5000,"silent AP fails, bounded");
    ap_mode=3; CK(reg_ops->join(0,AP,6,"home",0,0)==3,"association unanswered fails");
    reg_ops->leave(0); CK(nettype==0&&regs[0x618]==0,"leave clears link");
    bo0=n_bo; CK(reg_ops->tx(0,qd,40)==0&&n_bo==bo0,"data frame queued for aggregation"); reg_ops->poll(0); CK(n_bo==bo0+1&&last_ep==1,"poll flushes data");
    CK(reg_ops->tx(0,mg,30)==0&&n_bo==bo0+2&&last_ep==0,"management frame sent at once");
    rtwu_detach(); CK(!g_rtwu&&g_rtw_bus==&rtw_bus_pcie,"detach restores PCIe bus");
    CK(reg_ops->tx(0,mg,30)<0,"tx after detach refused");
    printf("rtw88 usb: %d checks, %d failed\n",checks,bad);
    if(!bad)puts("rtw88 usb: all ok");
    return bad?1:0;
}
