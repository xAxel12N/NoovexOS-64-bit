/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Derived from Linux drivers/net/wireless/realtek/rtw89/rtw8852be.c, rtw8852ce.c (PCI IDs) and rtw8852b.c, rtw8852c.c (firmware names);
 * fw.c (rtw89_fw_h2c_set_ofld_cfg payload); GPL-2.0 OR BSD-3-Clause, used under BSD-3-Clause.
 * Copyright(c) 2019-2022  Realtek Corporation
 * Licence text: drivers/net/wifi/realtek/rtw89/LICENCE.BSD-3-Clause
 */
#include "drivers/net/wifi/realtek/rtw89/rtw89.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_core.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_pci.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_sta.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_mac.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_chan.h"
#include "include/nvx/net/wifi/wifi_hw.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/fs/vfs/vfs.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"

struct rtw89_softc g_rtw89;
const struct rtw89_pci_id rtw89_ids[]={
    {0x10ec,0xb852,RTW89_CHIP_8852B,"Realtek RTL8852BE (Wi-Fi 6)","rtw89/rtw8852b_fw-1.bin"},
    {0x10ec,0xb85b,RTW89_CHIP_8852B,"Realtek RTL8852BE (Wi-Fi 6)","rtw89/rtw8852b_fw-1.bin"},
    {0x10ec,0xc852,RTW89_CHIP_8852C,"Realtek RTL8852CE (Wi-Fi 6E)","rtw89/rtw8852c_fw-1.bin"},
    {0,0,0,0,0}
};
const struct rtw89_pci_id* rtw89_match(u16 vendor,u16 device){ for(const struct rtw89_pci_id* i=rtw89_ids;i->vendor;i++) if(i->vendor==vendor&&i->device==device)return i; return 0; }

static char* r89_s(char* p,char* e,const char* s){ while(s&&*s&&p<e)*p++=*s++; *p=0; return p; }
static char* r89_x(char* p,char* e,u32 v,int n){ static const char* H="0123456789abcdef"; for(int i=n-1;i>=0&&p<e;i--)*p++=H[(v>>(i*4))&15]; *p=0; return p; }
static char* r89_u(char* p,char* e,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n&&p<e)*p++=t[--n]; *p=0; return p; }
static void r89_path(char* o,int cap,const char* nm){ char* e=o+cap-1; char* p=o; if(nm[0]!='/')p=r89_s(p,e,"/lib/firmware/"); r89_s(p,e,nm); }

static int r89_vfs_read(const char* nm,u8** out,u32* len){
    char path[128]; r89_path(path,sizeof path,nm); *out=0; *len=0;
    const vfs_fs_ops* fs=vfs_lookup(path); int sz=(fs&&fs->size&&fs->read)?fs->size(path):-1;
    if(sz<64||sz>4*1024*1024)return -1;
    u8* b=(u8*)kmalloc((unsigned long)sz); if(!b)return -2;
    int got=0; while(got<sz){ int r=fs->read(path,(char*)b+got,got,sz-got); if(r<=0)break; got+=r; }
    if(got!=sz){ kfree(b); return -1; }
    *out=b; *len=(u32)sz; return 0; }
int (*rtw89_firmware_read)(const char* path,u8** out,u32* len)=r89_vfs_read;
static int r89_fw_present(const char* nm){ char path[128]; r89_path(path,sizeof path,nm); const vfs_fs_ops* fs=vfs_lookup(path); return fs&&fs->size&&fs->size(path)>=64; }

static u32 r89_bar_lim(struct rtw89_softc* sc){ return sc->mmio_size?sc->mmio_size:RTW89_BAR_MIN; }
static u32 r89_mmio_rd(struct rtw89_softc* sc,u32 o,int w){ if(o+(u32)w>r89_bar_lim(sc))return 0xFFFFFFFFu; unsigned long a=(unsigned long)sc->mmio+o;
    return w==1?*(volatile u8*)a:w==2?*(volatile u16*)a:*(volatile u32*)a; }
static void r89_mmio_wr(struct rtw89_softc* sc,u32 o,u32 v,int w){ if(o+(u32)w>r89_bar_lim(sc))return; unsigned long a=(unsigned long)sc->mmio+o;
    if(w==1)*(volatile u8*)a=(u8)v; else if(w==2)*(volatile u16*)a=(u16)v; else *(volatile u32*)a=v; }
static const struct rtw89_io r89_mmio_io={ r89_mmio_rd,r89_mmio_wr,hr_delay_us };

int rtw89_probe(void){
    struct rtw89_softc* sc=&g_rtw89; if(sc->state)return 1;
    if(!pci_n)pci_scan();
    for(int k=0;k<pci_n;k++){ const struct rtw89_pci_id* id=rtw89_match(pci_ven[k],pci_dev[k]); if(!id)continue;
        sc->bus=pci_bus[k]; sc->slot=pci_slot[k]; sc->fn=0; sc->vendor=id->vendor; sc->device=id->device; sc->chip=id->chip; sc->id=id; if(!sc->io)sc->io=&r89_mmio_io;
        u32 bar=pci_cfg(sc->bus,sc->slot,0,0x18);
        if(bar&1){ sc->err="BAR2 is not memory"; sc->state=RTW89_ST_HWFAIL; return 1; }
        u32 hi=(((bar>>1)&3u)==2u)?pci_cfg(sc->bus,sc->slot,0,0x1C):0;
        if(hi||(bar&0xFFFFFFF0u)<0x100000u){ sc->err="card registers above 4 GB or unassigned - not supported yet"; sc->state=RTW89_ST_HWFAIL; return 1; }
        sc->mmio=bar&0xFFFFFFF0u; sc->state=RTW89_ST_FOUND;
        char m[120]; char* p=r89_s(m,m+sizeof m-1,"rtw89: found "); p=r89_s(p,m+sizeof m-1,id->name); r89_s(p,m+sizeof m-1," (starts automatically; Settings or 'wifi rtw89 off' to stop)"); klog(m);
        return 1; }
    return 0; }

static void r89_efuse(struct rtw89_softc* sc){
    if(sc->state>=RTW89_ST_PWRON&&!sc->efuse_ok&&rtw89_efuse_read(sc)==0){ char m[64]; char* p=r89_s(m,m+sizeof m-1,"rtw89: MAC "); for(int i=0;i<6;i++){ if(i)p=r89_s(p,m+sizeof m-1,":"); p=r89_x(p,m+sizeof m-1,sc->mac[i],2); } klog(m); } }
static int r89_bring_up(struct rtw89_softc* sc){
    if(rtw89_mac_init_post(sc)<0){ if(!sc->err)sc->err="MAC init failed"; return -1; }
    if(r89_bar_lim(sc)<RTW89_PHY_ADDR_OFFSET*2u&&sc->io==&r89_mmio_io){ sc->err="card register window too small for BB/RF tables"; return -1; }
    u8* t=0; u32 tl=0; int cc=sc->chip==RTW89_CHIP_8852C; if(rtw89_firmware_read(cc?RTW89_PHY_FILE_C:RTW89_PHY_FILE,&t,&tl)<0){ sc->err=cc?"PHY table file missing (/lib/firmware/rtw89/rtw8852c_phy.bin)":"PHY table file missing (/lib/firmware/rtw89/rtw8852b_phy.bin)"; return -1; }
    int r=rtw89_phy_load_8852b(sc,t,tl); kfree(t); if(r<0){ if(!sc->err)sc->err="BB/RF table load failed"; return -1; }
    if(cc)rtw8852c_bb_sethw(sc);
    rtw89_mac_set_rx_fltr(sc,RTW89_DEFAULT_RX_FLTR); sc->phy_ok=1;
    static const u8 ofld[8]={0x09,0,0,0,0x5e,0,0,0}; rtw89_h2c_send(sc,RTW89_H2C_CAT_MAC,H2C_CL_MAC_FW_OFLD,H2C_FUNC_OFLD_CFG,0,1,ofld,sizeof ofld);
    rtw89_chan_hook=rtw89_set_channel; rtw89_scan_pre_hook=rtw89_sta_vif_init;
    return 0; }

int rtw89_start(void){
    struct rtw89_softc* sc=&g_rtw89;
    if(!rtw89_probe())return -1;
    if(sc->state==RTW89_ST_READY)return 0;
    if(!sc->mmio)return -1;
    sc->err=0;
    if(sc->state<RTW89_ST_PWRON){
        u32 cmd=pci_cfg(sc->bus,sc->slot,0,0x04);
        if(!sc->mmio_size){ u32 bar=pci_cfg(sc->bus,sc->slot,0,0x18); pci_cfgw(sc->bus,sc->slot,0,0x04,cmd&0xFFFFu&~2u); pci_cfgw(sc->bus,sc->slot,0,0x18,0xFFFFFFFFu);
            u32 pr=pci_cfg(sc->bus,sc->slot,0,0x18); pci_cfgw(sc->bus,sc->slot,0,0x18,bar); pci_cfgw(sc->bus,sc->slot,0,0x04,cmd&0xFFFFu); u32 sz=rtw89_bar_size(pr); sc->mmio_size=sz?sz:RTW89_BAR_MIN; }
        if((cmd&6u)!=6u)pci_cfgw(sc->bus,sc->slot,0,0x04,(cmd&0xFFFFu)|6u);
        if(rtw89_read_cv(sc)<0){ sc->err="card does not answer register reads"; sc->state=RTW89_ST_HWFAIL; return -1; }
        if(sc->chip==RTW89_CHIP_8852C&&!r89_fw_present(sc->id->fw)){ sc->err="firmware file missing or unreadable"; sc->state=RTW89_ST_NOFW; return -1; }
        if((sc->chip==RTW89_CHIP_8852C?rtw8852c_pwr_on(sc):rtw8852b_pwr_on(sc))<0){ if(!sc->err)sc->err="power-on failed"; sc->state=RTW89_ST_HWFAIL; return -1; }
        else { sc->state=RTW89_ST_PWRON; klog("rtw89: MAC powered on"); } }
    u8* fw=0; u32 len=0;
    if(rtw89_firmware_read(sc->id->fw,&fw,&len)<0){ sc->err="firmware file missing or unreadable"; if(sc->state<RTW89_ST_PWRON)sc->state=RTW89_ST_NOFW; return -1; }
    int r=rtw89_fw_parse(fw,len,sc->cv,&sc->fw); sc->fwlen=len;
    if(r<0){ kfree(fw); sc->err="firmware file has no usable image for this chip revision"; return -1; }
    if(sc->state==RTW89_ST_PWRON&&!rtw89_hci)rtw89_pci_attach(sc);
    if(sc->state==RTW89_ST_PWRON&&!sc->err&&rtw89_hci){
        if(rtw89_mac_dmac_pre_init(sc)==0&&(!rtw89_hci->pre_fwdl||rtw89_hci->pre_fwdl(sc)==0)){ r=rtw89_fw_download(sc,fw,len); if(r==0){ sc->state=RTW89_ST_FWOK; klog("rtw89: firmware running"); } } }
    kfree(fw);
    if(sc->state==RTW89_ST_PWRON){ const char* e=sc->err; u32 er=sc->err_reg; r89_efuse(sc); if(e){ sc->err=e; sc->err_reg=er; } }
    if(sc->state==RTW89_ST_FWOK&&!sc->err){ r89_efuse(sc); if(sc->efuse_ok&&r89_bring_up(sc)==0){ sc->state=RTW89_ST_READY; klog("rtw89: MAC/BB/RF initialised, ready to scan"); return 0; } }
    if(!sc->err)sc->err=sc->state==RTW89_ST_FWOK?"efuse unreadable after firmware download":"firmware download needs the PCIe H2C ring (not set up yet)";
    return -1; }

static int r89g_init(void* ctx){ struct rtw89_softc* sc=(struct rtw89_softc*)ctx; return sc&&sc->state==RTW89_ST_READY?0:-1; }
static int r89g_scan_start(void* ctx,const u8* ch,int n){ return rtw89_scan_begin((struct rtw89_softc*)ctx,ch,n); }
static int r89g_scan_done(void* ctx){ return rtw89_scan_finished((struct rtw89_softc*)ctx); }
static void r89g_poll(void* ctx){ rtw89_scan_poll((struct rtw89_softc*)ctx,32); }
static int r89g_rssi(void* ctx){ (void)ctx; return -127; }
static int r89g_mac(void* ctx,u8* mac){ struct rtw89_softc* sc=(struct rtw89_softc*)ctx; if(!sc||sc->state!=RTW89_ST_READY)return -1; for(int i=0;i<6;i++)mac[i]=sc->mac[i]; return 0; }
static int r89g_join(void* ctx,const u8* b,u8 ch,const char* ssid,u8 akm,u8 ht){ return rtw89_sta_join((struct rtw89_softc*)ctx,b,ch,ssid,akm,ht); }
static void r89g_leave(void* ctx){ rtw89_sta_leave((struct rtw89_softc*)ctx); }
static int r89g_tx(void* ctx,const u8* f,int len){ return rtw89_sta_tx((struct rtw89_softc*)ctx,f,len); }
static int r89g_key(void* ctx,int idx,int type,const u8* k,int len,int pw){ return rtw89_sta_set_key((struct rtw89_softc*)ctx,idx,type,k,len,pw); }
static const struct wifi_hw_ops r89g_ops={ "rtw89",0,r89g_init,r89g_scan_start,r89g_scan_done,r89g_join,r89g_leave,r89g_tx,r89g_key,r89g_rssi,r89g_poll,r89g_mac,0 };
int wifi_feat_begin(int f); void wifi_feat_ok(int f); void wifi_feat_unsup(int f,const char* why); int wifi_feat_on(int f);
static int r89_tried=0;
void rtw89_wifi_hw_probe(void){ if(!rtw89_probe())return;
    if(g_rtw89.state!=RTW89_ST_READY&&!r89_tried&&wifi_feat_begin(15)){ r89_tried=1; int r=rtw89_start(); if(r==0&&g_rtw89.state==RTW89_ST_READY)wifi_feat_ok(15); else wifi_feat_unsup(15,g_rtw89.err?g_rtw89.err:"bring-up not finished"); }
    if(g_rtw89.state==RTW89_ST_READY)wifi_hw_register(&r89g_ops,&g_rtw89); }

void rtw89_status(char* out,int cap){
    struct rtw89_softc* sc=&g_rtw89; if(cap<2)return; char* p=out; char* e=out+cap-1; *p=0; rtw89_probe();
    if(sc->state==RTW89_ST_NONE){ r89_s(p,e,"rtw89: no Realtek RTL8852BE/RTL8852CE card found\n"); return; }
    static const char* ST[]={"none","found (detect only)","firmware missing","failed","powered on","firmware running","ready"};
    p=r89_s(p,e,"Adapter="); p=r89_s(p,e,sc->id->name); p=r89_s(p,e," ["); p=r89_x(p,e,sc->vendor,4); p=r89_s(p,e,":"); p=r89_x(p,e,sc->device,4); p=r89_s(p,e,"]\nState="); p=r89_s(p,e,ST[sc->state<=RTW89_ST_READY?sc->state:0]);
    if(sc->state>=RTW89_ST_HWFAIL){ p=r89_s(p,e,"\nChip revision (CV)="); p=r89_u(p,e,sc->cv); }
    if(sc->efuse_ok){ p=r89_s(p,e,"\nMAC="); for(int i=0;i<6;i++){ if(i)p=r89_s(p,e,":"); p=r89_x(p,e,sc->mac[i],2); } p=r89_s(p,e," RFE type="); p=r89_u(p,e,sc->rfe_type); p=r89_s(p,e,sc->efuse_valid?" (efuse autoload ok)":" (efuse autoload flag not set)"); }
    if(sc->fwdl_tries){ p=r89_s(p,e,"\nFirmware download: "); p=r89_s(p,e,sc->fw_rdy?"ready":"failed"); p=r89_s(p,e,", tries "); p=r89_u(p,e,sc->fwdl_tries); p=r89_s(p,e,", status "); p=r89_u(p,e,sc->fwdl_sts);
        if(!sc->fw_rdy){ p=r89_s(p,e,", 0x1e0=0x"); p=r89_x(p,e,sc->fw_ctrl_dump,8); p=r89_s(p,e," boot_dbg=0x"); p=r89_x(p,e,sc->boot_dbg_dump,8); } }
    if(sc->phy_ok){ p=r89_s(p,e,"\nMAC/BB/RF init: done, register window "); p=r89_u(p,e,r89_bar_lim(sc)>>10); p=r89_s(p,e," KB"); }
    if(g_r89ch.switches){ p=r89_s(p,e,"\nChannel: "); p=r89_u(p,e,g_r89ch.ch); p=r89_s(p,e,g_r89ch.ok?" (20 MHz)":" (switch failed)"); }
    if(g_r89scan.events||g_r89scan.active){ p=r89_s(p,e,"\nScan: "); p=r89_s(p,e,g_r89scan.active?"running":"done"); p=r89_s(p,e,", channels entered "); p=r89_u(p,e,g_r89scan.enter); p=r89_s(p,e," of "); p=r89_u(p,e,g_r89scan.nch); }
    if(sc->fwlog[0]){ p=r89_s(p,e,"\nFirmware log: "); p=r89_s(p,e,sc->fwlog); }
    p=r89_s(p,e,"\nFirmware=/lib/firmware/"); p=r89_s(p,e,sc->id->fw); p=r89_s(p,e,r89_fw_present(sc->id->fw)?" (present)":" (MISSING)");
    if(sc->fw.nsec){ p=r89_s(p,e," version "); p=r89_u(p,e,sc->fw.major); p=r89_s(p,e,"."); p=r89_u(p,e,sc->fw.minor); p=r89_s(p,e,"."); p=r89_u(p,e,sc->fw.sub); p=r89_s(p,e,"."); p=r89_u(p,e,sc->fw.idx);
        p=r89_s(p,e," built "); p=r89_u(p,e,sc->fw.year); p=r89_s(p,e,"-"); p=r89_u(p,e,sc->fw.month); p=r89_s(p,e,"-"); p=r89_u(p,e,sc->fw.day); p=r89_s(p,e,", image for CV "); p=r89_u(p,e,sc->fw.cv); p=r89_s(p,e,", "); p=r89_u(p,e,(u32)sc->fw.nsec); p=r89_s(p,e," sections"); }
    if(sc->err){ p=r89_s(p,e,"\nLast error: "); p=r89_s(p,e,sc->err); if(sc->err_reg){ p=r89_s(p,e," (register 0x"); p=r89_x(p,e,sc->err_reg,4); p=r89_s(p,e,")"); } }
    r89_s(p,e,"\n"); }
void rtw89_up(char* out,int cap){
    if(cap<2)return;
    out[0]=0;
    if(rtw89_probe())rtw89_start();
    rtw89_status(out,cap); }
