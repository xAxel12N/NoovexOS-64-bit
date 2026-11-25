/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Derived from Linux drivers/net/wireless/mediatek/mt76/mt7925/pci.c (PCI ID table) and mt792x.h (firmware names).
 * Copyright (C) 2023 MediaTek Inc.
 * Licence text: drivers/net/wifi/mediatek/mt7921/LICENCE.BSD-3-Clause-Clear
 */
#include "drivers/net/wifi/mediatek/mt7925/mt7925.h"
#include "include/nvx/net/wifi/wifi_hw.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/fs/vfs/vfs.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/net/wifi/regdb.h"

struct mt7921_softc g_mt7925;
#define MT_P7925 "mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin"
#define MT_R7925 "mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin"
const struct mt_pci_id mt7925_ids[]={
    {0x14c3,0x7925,MT_CHIP_7925,"MediaTek MT7925 (Wi-Fi 7)",MT_P7925,MT_R7925},
    {0x14c3,0x0717,MT_CHIP_7925,"MediaTek MT7925 / 0717 (Wi-Fi 7)",MT_P7925,MT_R7925},
    {0,0,0,0,0,0}
};
const struct mt_pci_id* mt7925_match(u16 vendor,u16 device){ for(const struct mt_pci_id* i=mt7925_ids;i->vendor;i++) if(i->vendor==vendor&&i->device==device)return i; return 0; }
static char* m5g_s(char* p,char* e,const char* s){ while(s&&*s&&p<e)*p++=*s++; *p=0; return p; }
static char* m5g_x(char* p,char* e,u32 v,int n){ static const char* H="0123456789abcdef"; for(int i=n-1;i>=0&&p<e;i--)*p++=H[(v>>(i*4))&15]; *p=0; return p; }
static char* m5g_u(char* p,char* e,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n&&p<e)*p++=t[--n]; *p=0; return p; }
static int m5g_fw_present(const char* nm){ char path[128]; char* p=m5g_s(path,path+sizeof path-1,"/lib/firmware/"); m5g_s(p,path+sizeof path-1,nm); const vfs_fs_ops* fs=vfs_lookup(path); return fs&&fs->size&&fs->size(path)>=64; }

int mt7925_probe(void){
    struct mt7921_softc* sc=&g_mt7925; if(sc->state)return 1;
    if(!pci_n)pci_scan();
    for(int k=0;k<pci_n;k++){ const struct mt_pci_id* id=mt7925_match(pci_ven[k],pci_dev[k]); if(!id)continue;
        sc->bus=pci_bus[k]; sc->slot=pci_slot[k]; sc->fn=0; sc->vendor=id->vendor; sc->device=id->device; sc->chip=id->chip; sc->id=id;
        sc->gen=3; sc->mcu_send=mt7925_mcu_send; sc->rx_fix=mt7925_rx_fix; mt7925_mac_attach(sc);
        u32 bar=pci_cfg(sc->bus,sc->slot,0,0x10);
        if(bar&1){ sc->err="BAR0 is not memory"; sc->state=MT_ST_HWFAIL; return 1; }
        u32 hi=(((bar>>1)&3u)==2u)?pci_cfg(sc->bus,sc->slot,0,0x14):0;
        if(hi||(bar&0xFFFFFFF0u)<0x100000u){ sc->err="card registers above 4 GB or unassigned - not supported yet"; sc->state=MT_ST_HWFAIL; return 1; }
        sc->mmio=bar&0xFFFFFFF0u; sc->state=MT_ST_FOUND; sc->rssi=-100;
        klog2("mt7925: found ",id->name);
        return 1; }
    return 0; }

static void m5g_rx(const u8* f,u32 len,int rssi){ extern int g_wifi_scanning; g_mt7925.rssi=rssi; u8 rc=g_mt7925.rx_ch; if(g_wifi_scanning)mt7925_eht_note(f,len);
    if(rc>180&&len>=24&&((f[0]&0xFC)==0x80||(f[0]&0xFC)==0x50)){ if(!g_wifi_scanning)return; if(mt7925_6g_on)wifi_scan_parse(f,(int)len,(u8)(rc-181+MT7925_VCH6),rssi,0); return; }
    wifi_hw_rx(f,(int)len,rssi); }
int mt7925_start(void){
    struct mt7921_softc* sc=&g_mt7925;
    if(!mt7925_probe())return -1;
    if(sc->state==MT_ST_READY)return 0;
    if(sc->state==MT_ST_HWFAIL&&!sc->mmio)return -1;
    if(g_mt7921.state>MT_ST_FOUND){ sc->err="another MediaTek Wi-Fi card is active"; return -1; }
    mt7921_rx_deliver=m5g_rx; sc->err=0;
    if(sc->state<MT_ST_FWOK){
        if(mt7921_pci_init(sc)<0||mt7921_dma_init(sc)<0){ if(!sc->err)sc->err="PCIe/DMA setup failed"; sc->state=MT_ST_HWFAIL; return -1; }
        if(mt7921_fw_load(sc)<0){
            if(!sc->err)sc->err="firmware download failed";
            sc->state=(!m5g_fw_present(sc->id->patch)||!m5g_fw_present(sc->id->ram))?MT_ST_NOFW:MT_ST_HWFAIL;
            if(sc->state==MT_ST_NOFW)klog("mt7925: firmware missing: /lib/firmware/" MT_P7925 " and /lib/firmware/" MT_R7925);
            return -1; }
        sc->state=MT_ST_FWOK; mt7925_clc_reset(); klog("mt7925: firmware is running"); }
    if(mt7925_mcu_init(sc)<0){ if(!sc->err)sc->err="MCU init (NIC capability) failed"; return -1; }
    return 0; }

u64 mt_ms(void);
u32 mt7925_fw_restarts; static int m5g_rst; static u64 m5g_rt;
static void m5g_lost(struct mt7921_softc* sc){ if(sc!=&g_mt7925||sc->state!=MT_ST_READY)return; sc->state=MT_ST_FOUND; sc->connected=0; sc->scanning=0; m5g_rst=1; m5g_rt=0; mt7925_clc_reset(); klog("mt7925: firmware stopped answering - restarting it before the next scan or join"); }
static int m5g_revive(struct mt7921_softc* sc){
    if(sc->state==MT_ST_READY||!m5g_rst)return sc->state==MT_ST_READY;
    u64 now=mt_ms(); if(m5g_rst>3||(m5g_rt&&now-m5g_rt<2000))return 0; m5g_rt=now; m5g_rst++;
    if(mt7925_start()!=0){ if(m5g_rst>3)klog("mt7925: firmware restart failed 3 times - Wi-Fi stays off until the computer restarts"); return 0; }
    m5g_rst=0; mt7925_fw_restarts++; mt7925_clc_sync(sc,regdb_country()); klog("mt7925: firmware restarted, country tables sent again"); return 1; }
static int m5g_init(void* ctx){ struct mt7921_softc* sc=(struct mt7921_softc*)ctx; if(!sc)return -1; if(sc->state==MT_ST_READY)return 0; if(m5g_rst)return m5g_revive(sc)?0:-1; int r=mt7925_start(); if(!r)mt7925_clc_sync(sc,regdb_country()); return r; }
int mt7925_6g_on=1;
static int m5g_scan_start(void* ctx,const u8* ch,int n){ if(m5g_rst&&!m5g_revive((struct mt7921_softc*)ctx))return -1; u8 c[MT7925_SCAN_MAX_CH],b[MT7925_SCAN_MAX_CH]; int k=mt7925_scan_list(ch,n,c,b,MT7925_SCAN_MAX_CH,mt7925_has_6g&&mt7925_6g_on); return mt7925_hw_scan((struct mt7921_softc*)ctx,c,b,k); }
static int m5g_scan_done(void* ctx){ return !mt7925_scan_busy((struct mt7921_softc*)ctx); }
void wifi_feat_unsup(int f,const char* why); int wifi_feat_begin(int f); void wifi_feat_ok(int f); void wifi_feat_fail(int f,const char* why);
static int m5g_join(void* ctx,const u8* bssid,u8 ch,const char* ssid,u8 akm,u8 ht){ struct mt7921_softc* sc=(struct mt7921_softc*)ctx; if(m5g_rst&&!m5g_revive(sc))return -1; mt7925_clc_sync(sc,regdb_country());
    if(ch>=MT7925_VCH6&&ch<MT7925_VCH6+59){ extern int g_wpa_akm; if(g_wpa_akm!=8){ klog("mt7925: 6 GHz needs WPA3-SAE; not joining"); return 2; } return mt7925_connect(sc,bssid,(u8)((ch-MT7925_VCH6)*4+1),MT7925_BAND_6G,ssid,8,ht); }
    { extern int g_wpa_akm; return mt7925_connect(sc,bssid,ch,ch<=14?MT7925_BAND_2G:MT7925_BAND_5G,ssid,(u8)(akm?g_wpa_akm:0),ht); } }
static void m5g_leave(void* ctx){ struct mt7921_softc* sc=(struct mt7921_softc*)ctx; mt7921_disconnect(sc); sc->band6=0; }
static int m5g_tx(void* ctx,const u8* f,int len){ return len<=0?-1:mt7921_tx_data((struct mt7921_softc*)ctx,f,(u32)len); }
static int m5g_set_key(void* ctx,int idx,int type,const u8* key,int len,int pw){ return mt7921_set_key((struct mt7921_softc*)ctx,idx,type,key,len,pw); }
static int m5g_rssi(void* ctx){ return ((struct mt7921_softc*)ctx)->rssi; }
static void m5g_poll(void* ctx){ struct mt7921_softc* sc=(struct mt7921_softc*)ctx; if(sc&&sc->state>=MT_ST_FWOK){ mt7921_rx_poll(sc); mt_ba_drain(sc); } }
static int m5g_mac(void* ctx,u8* mac){ struct mt7921_softc* sc=(struct mt7921_softc*)ctx; if(!sc||sc->state!=MT_ST_READY)return -1; for(int i=0;i<6;i++)mac[i]=sc->mac[i]; return 0; }
static const struct wifi_hw_ops m5g_ops={ "mt7925",WIFI_HW_SAE,m5g_init,m5g_scan_start,m5g_scan_done,m5g_join,m5g_leave,m5g_tx,m5g_set_key,m5g_rssi,m5g_poll,m5g_mac,0 };
static int m5g_tries=0;
void mt7925_wifi_hw_probe(void){ if(!mt7925_probe())return; if(g_mt7925.state!=MT_ST_READY){ if(m5g_tries>=3||!wifi_feat_begin(16))return; m5g_tries++; if(mt7925_start()!=0){ wifi_feat_fail(16,g_mt7925.err?g_mt7925.err:"start failed"); return; } wifi_feat_ok(16); }
    if(g_mt7925.state==MT_ST_READY){ mt7925_fw_lost=m5g_lost; mt7925_clc_sync(&g_mt7925,regdb_country()); wifi_hw_register(&m5g_ops,&g_mt7925); } }

int mt7925_present(void){ return g_mt7925.state!=MT_ST_NONE; }
void mt7925_status(char* out,int cap){
    struct mt7921_softc* sc=&g_mt7925; if(cap<2)return; char* p=out; char* e=out+cap-1; *p=0; mt7925_probe();
    if(sc->state==MT_ST_NONE){ m5g_s(p,e,"mtwifi: no MediaTek MT7925 card found\n"); return; }
    static const char* ST[]={"none","found","firmware missing","failed","firmware running","ready"};
    p=m5g_s(p,e,"Adapter="); p=m5g_s(p,e,sc->id->name); p=m5g_s(p,e," ["); p=m5g_x(p,e,sc->vendor,4); p=m5g_s(p,e,":"); p=m5g_x(p,e,sc->device,4); p=m5g_s(p,e,"]\nState="); p=m5g_s(p,e,ST[sc->state<=MT_ST_READY?sc->state:0]);
    p=m5g_s(p,e,"\nPatch=/lib/firmware/"); p=m5g_s(p,e,sc->id->patch); p=m5g_s(p,e,m5g_fw_present(sc->id->patch)?" (present)":" (MISSING)");
    p=m5g_s(p,e,"\nRAM code=/lib/firmware/"); p=m5g_s(p,e,sc->id->ram); p=m5g_s(p,e,m5g_fw_present(sc->id->ram)?" (present)":" (MISSING)");
    if(sc->fwver[0]){ p=m5g_s(p,e," version "); p=m5g_s(p,e,sc->fwver); }
    if(sc->state==MT_ST_READY){ p=m5g_s(p,e,"\nMAC="); for(int i=0;i<6;i++){ if(i)p=m5g_s(p,e,":"); p=m5g_x(p,e,sc->mac[i],2); } p=m5g_s(p,e,mt7925_has_6g?" 6GHz=yes":" 6GHz=no"); }
    if(sc->connected){ p=m5g_s(p,e,"\nConnected channel "); p=m5g_u(p,e,sc->chan); if(sc->band6)p=m5g_s(p,e," (6 GHz)"); }
    if(sc->state==MT_ST_READY){ char cl[96]; int nc=mt7925_clc_list(cl,sizeof cl); p=m5g_s(p,e,"\nCountry tables=");
        if(!mt7925_clc_on||mt7925_clc_fail>=3){ p=m5g_s(p,e,"off"); if(mt7925_clc_err){ p=m5g_s(p,e," - "); p=m5g_s(p,e,mt7925_clc_err); } }
        else if(mt7925_clc_sent){ p=m5g_s(p,e,"using "); p=m5g_s(p,e,mt7925_clc_cc); p=m5g_s(p,e,", "); p=m5g_u(p,e,(u32)(nc>0?nc:0)); p=m5g_s(p,e," countries: "); p=m5g_s(p,e,cl); }
        else p=m5g_s(p,e,mt7925_clc_err?mt7925_clc_err:"not loaded yet");
        p=m5g_s(p,e,"\nWi-Fi 7 (EHT)="); p=m5g_s(p,e,!mt7925_eht_on?"off":mt7925_eht_fail>=3?"off after 3 failures, using Wi-Fi 6 (HE)":mt7925_peer_eht.ok?"on, access point supports it":"on when the access point supports it"); }
    if(sc->err){ p=m5g_s(p,e,"\nLast error: "); p=m5g_s(p,e,sc->err); }
    p=m5g_s(p,e,"\nMCU commands="); p=m5g_u(p,e,sc->mcu_cmds); p=m5g_s(p,e," events="); p=m5g_u(p,e,sc->mcu_events); p=m5g_s(p,e," timeouts="); p=m5g_u(p,e,sc->mcu_timeouts); p=m5g_s(p,e," firmware restarts="); p=m5g_u(p,e,mt7925_fw_restarts); if(m5g_rst)p=m5g_s(p,e,m5g_rst>3?" (restart failed)":" (restart pending)");
    m5g_s(p,e,"\n"); }
