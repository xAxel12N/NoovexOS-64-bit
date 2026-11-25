/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Derived from Linux drivers/net/wireless/mediatek/mt76/mt7921/pci.c (PCI ID table) and mt792x.h (firmware names).
 * Copyright (C) 2020 MediaTek Inc.
 * Licence text: drivers/net/wifi/mediatek/mt7921/LICENCE.BSD-3-Clause-Clear
 */
#include "drivers/net/wifi/mediatek/mt7921/mt7921.h"
#include "include/nvx/net/wifi/wifi_hw.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/fs/vfs/vfs.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/core/klog.h"

struct mt7921_softc g_mt7921;
#define MT_P7961 "mediatek/WIFI_MT7961_patch_mcu_1_2_hdr.bin"
#define MT_R7961 "mediatek/WIFI_RAM_CODE_MT7961_1.bin"
#define MT_P7920 "mediatek/WIFI_MT7961_patch_mcu_1a_2_hdr.bin"
#define MT_R7920 "mediatek/WIFI_RAM_CODE_MT7961_1a.bin"
#define MT_P7922 "mediatek/WIFI_MT7922_patch_mcu_1_1_hdr.bin"
#define MT_R7922 "mediatek/WIFI_RAM_CODE_MT7922_1.bin"
const struct mt_pci_id mt7921_ids[]={
    {0x14c3,0x7961,MT_CHIP_7961,"MediaTek MT7921 (Wi-Fi 6)",MT_P7961,MT_R7961},
    {0x14c3,0x0608,MT_CHIP_7961,"MediaTek MT7921K / RZ608 (Wi-Fi 6E)",MT_P7961,MT_R7961},
    {0x14c3,0x7920,MT_CHIP_7920,"MediaTek MT7920 (Wi-Fi 6)",MT_P7920,MT_R7920},
    {0x14c3,0x7922,MT_CHIP_7922,"MediaTek MT7922 (Wi-Fi 6E)",MT_P7922,MT_R7922},
    {0x14c3,0x0616,MT_CHIP_7922,"MediaTek MT7922 / RZ616 (Wi-Fi 6E)",MT_P7922,MT_R7922},
    {0x0b48,0x7922,MT_CHIP_7922,"ITTIM MT7922 (Wi-Fi 6E)",MT_P7922,MT_R7922},
    {0,0,0,0,0,0}
};
const struct mt_pci_id* mt7921_match(u16 vendor,u16 device){ for(const struct mt_pci_id* i=mt7921_ids;i->vendor;i++) if(i->vendor==vendor&&i->device==device)return i; return 0; }

static char* mtg_s(char* p,char* e,const char* s){ while(s&&*s&&p<e)*p++=*s++; *p=0; return p; }
static char* mtg_x(char* p,char* e,u32 v,int n){ static const char* H="0123456789abcdef"; for(int i=n-1;i>=0&&p<e;i--)*p++=H[(v>>(i*4))&15]; *p=0; return p; }
static char* mtg_u(char* p,char* e,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n&&p<e)*p++=t[--n]; *p=0; return p; }
static void mtg_path(char* o,int cap,const char* nm){ char* e=o+cap-1; char* p=o; if(nm[0]!='/')p=mtg_s(p,e,"/lib/firmware/"); mtg_s(p,e,nm); }

static int mtg_vfs_read(const char* nm,u8** out,u32* len){
    char path[128]; mtg_path(path,sizeof path,nm); *out=0; *len=0;
    const vfs_fs_ops* fs=vfs_lookup(path); int sz=(fs&&fs->size&&fs->read)?fs->size(path):-1;
    if(sz<64||sz>4*1024*1024)return -1;
    u8* b=(u8*)kmalloc((unsigned long)sz); if(!b)return -2;
    int got=0; while(got<sz){ int r=fs->read(path,(char*)b+got,got,sz-got); if(r<=0)break; got+=r; }
    if(got!=sz){ kfree(b); return -1; }
    *out=b; *len=(u32)sz; return 0; }
int (*mt7921_firmware_read)(const char* path,u8** out,u32* len)=mtg_vfs_read;
static int mtg_fw_present(const char* nm){ char path[128]; mtg_path(path,sizeof path,nm); const vfs_fs_ops* fs=vfs_lookup(path); return fs&&fs->size&&fs->size(path)>=64; }

int mt7921_probe(void){
    struct mt7921_softc* sc=&g_mt7921; if(sc->state)return 1;
    if(!pci_n)pci_scan();
    for(int k=0;k<pci_n;k++){ const struct mt_pci_id* id=mt7921_match(pci_ven[k],pci_dev[k]); if(!id)continue;
        sc->bus=pci_bus[k]; sc->slot=pci_slot[k]; sc->fn=0; sc->vendor=id->vendor; sc->device=id->device; sc->chip=id->chip; sc->id=id;
        u32 bar=pci_cfg(sc->bus,sc->slot,0,0x10);
        if(bar&1){ sc->err="BAR0 is not memory"; sc->state=MT_ST_HWFAIL; return 1; }
        u32 hi=(((bar>>1)&3u)==2u)?pci_cfg(sc->bus,sc->slot,0,0x14):0;
        if(hi||(bar&0xFFFFFFF0u)<0x100000u){ sc->err="card registers above 4 GB or unassigned - not supported yet"; sc->state=MT_ST_HWFAIL; return 1; }
        sc->mmio=bar&0xFFFFFFF0u; sc->state=MT_ST_FOUND; sc->rssi=-100;
        char m[120]; char* p=mtg_s(m,m+sizeof m-1,"mt7921: found "); mtg_s(p,m+sizeof m-1,id->name); klog(m);
        return 1; }
    return 0; }

void (*mt7921_rx_deliver)(const u8* frame,u32 len,int rssi);
int mt7921_scan_list(const u8* ch,int n,u8* out,int cap,int six){ static const u8 psc[15]={5,21,37,53,69,85,101,117,133,149,165,181,197,213,229}; int k=0;
    int ex=0; for(int i=0;i<n&&k<cap;i++){ if(ch[i]<MT7921_VCH6)out[k++]=ch[i]; else if(six&&ch[i]<MT7921_VCH6+59){ out[k++]=ch[i]; ex=1; } }
    for(int i=0;six&&!ex&&i<15&&k<cap;i++)out[k++]=(u8)(MT7921_VCH6+(psc[i]-1)/4);
    return k; }
u64 mt_ms(void);
static struct { u8 b[6], ch; u64 t; } mtg_b6[8]; static int mtg_b6n;
static void mtg_b6_seen(const u8* b,u8 ch){ int k=-1; for(int i=0;i<8;i++){ int m=mtg_b6[i].ch==ch; for(int j=0;j<6&&m;j++)m=mtg_b6[i].b[j]==b[j]; if(m){ k=i; break; } }
    if(k<0){ k=mtg_b6n; mtg_b6n=(mtg_b6n+1)&7; for(int j=0;j<6;j++)mtg_b6[k].b[j]=b[j]; mtg_b6[k].ch=ch; } mtg_b6[k].t=mt_ms(); }
int mt7921_b6_heard(const u8* b,u8 ch){ u64 now=mt_ms(); for(int i=0;i<8;i++){ int m=ch&&mtg_b6[i].ch==ch; for(int j=0;j<6&&m;j++)m=mtg_b6[i].b[j]==b[j]; if(m&&now-mtg_b6[i].t<30000)return 1; } return 0; }
static void mtg_glue_rx(const u8* f,u32 len,int rssi){ extern int g_wifi_scanning; g_mt7921.rssi=rssi; u8 rc=g_mt7921.rx_ch;
    if(rc>180&&len>=24&&((f[0]&0xFC)==0x80||(f[0]&0xFC)==0x50)){ if(g_wifi_scanning&&mt7921_has_6g&&mt7921_6g_on){ u8 v=(u8)(rc-181+MT7921_VCH6); mtg_b6_seen(f+16,v); wifi_scan_parse(f,(int)len,v,rssi,0); } return; }
    wifi_hw_rx(f,(int)len,rssi); }
int mt7921_start(void){
    struct mt7921_softc* sc=&g_mt7921;
    if(!mt7921_probe())return -1;
    if(sc->state==MT_ST_READY)return 0;
    if(sc->state==MT_ST_HWFAIL&&!sc->mmio)return -1;
    mt7921_rx_deliver=mtg_glue_rx; sc->err=0;
    if(sc->state<MT_ST_FWOK){
        if(mt7921_pci_init(sc)<0||mt7921_dma_init(sc)<0){ if(!sc->err)sc->err="PCIe/DMA setup failed"; sc->state=MT_ST_HWFAIL; return -1; }
        int r=mt7921_fw_load(sc);
        if(r<0){ if(!sc->err)sc->err="firmware download failed"; sc->state=(!mtg_fw_present(sc->id->patch)||!mtg_fw_present(sc->id->ram))?MT_ST_NOFW:MT_ST_HWFAIL; return -1; }
        sc->state=MT_ST_FWOK; klog("mt7921: firmware is running"); }
    if(mt7921_mcu_init(sc)<0){ if(!sc->err)sc->err="MCU init (NIC capability) failed"; return -1; }
    sc->state=MT_ST_READY; return 0; }

static int mtg_g_init(void* ctx){ struct mt7921_softc* sc=(struct mt7921_softc*)ctx; if(!sc)return -1; return sc->state==MT_ST_READY?0:mt7921_start(); }
static int mtg_g_scan_start(void* ctx,const u8* ch,int n){ u8 c[64]; int k=mt7921_scan_list(ch,n,c,64,mt7921_has_6g&&mt7921_6g_on); return mt7921_hw_scan((struct mt7921_softc*)ctx,c,k); }
static int mtg_g_scan_done(void* ctx){ return !mt7921_scan_busy((struct mt7921_softc*)ctx); }
static int mtg_g_join(void* ctx,const u8* bssid,u8 ch,const char* ssid,u8 akm,u8 ht){ extern int g_wpa_akm; struct mt7921_softc* sc=(struct mt7921_softc*)ctx;
    if(ch>=MT7921_VCH6&&ch<MT7921_VCH6+59){ if(!mt7921_has_6g||!mt7921_6g_on){ sc->err="6 GHz not available on this card"; return -1; } if(g_wpa_akm!=8){ klog("mt7921: 6 GHz needs WPA3-SAE; not joining"); return 2; }
        if(!mt7921_b6_heard(bssid,ch)){ sc->err="no beacon from this 6 GHz network in the last 30 s - scan again"; klog("mt7921: 6 GHz network not heard in the last 30 s; not transmitting there before a new scan"); return -1; }
        sc->band6=1; int r=mt7921_connect(sc,bssid,(u8)((ch-MT7921_VCH6)*4+1),ssid,8,ht); if(r<0)sc->band6=0; return r; }
    sc->band6=0; return mt7921_connect(sc,bssid,ch,ssid,(u8)(akm?g_wpa_akm:0),ht); }
static void mtg_g_leave(void* ctx){ struct mt7921_softc* sc=(struct mt7921_softc*)ctx; mt7921_disconnect(sc); sc->band6=0; }
static int mtg_g_tx(void* ctx,const u8* f,int len){ return len<=0?-1:mt7921_tx_data((struct mt7921_softc*)ctx,f,(u32)len); }
static int mtg_g_set_key(void* ctx,int idx,int type,const u8* key,int len,int pw){ return mt7921_set_key((struct mt7921_softc*)ctx,idx,type,key,len,pw); }
static int mtg_g_rssi(void* ctx){ return ((struct mt7921_softc*)ctx)->rssi; }
static void mtg_g_poll(void* ctx){ struct mt7921_softc* sc=(struct mt7921_softc*)ctx; if(sc&&sc->state>=MT_ST_FWOK){ mt7921_rx_poll(sc); mt_ba_drain(sc); } }
static int mtg_g_mac(void* ctx,u8* mac){ struct mt7921_softc* sc=(struct mt7921_softc*)ctx; if(!sc||sc->state!=MT_ST_READY)return -1; for(int i=0;i<6;i++)mac[i]=sc->mac[i]; return 0; }
static const struct wifi_hw_ops mtg_hw_ops={ "mt7921",WIFI_HW_SAE,mtg_g_init,mtg_g_scan_start,mtg_g_scan_done,mtg_g_join,mtg_g_leave,mtg_g_tx,mtg_g_set_key,mtg_g_rssi,mtg_g_poll,mtg_g_mac };
static int mtg_register(void){ return g_mt7921.state==MT_ST_READY?wifi_hw_register(&mtg_hw_ops,&g_mt7921):-1; }
void mt7921_wifi_hw_probe(void){ if(!mt7921_probe())return; if(mt7921_start()==0)mtg_register(); }

int mt7921_present(void){ return g_mt7921.state!=MT_ST_NONE; }
void mt7921_status(char* out,int cap){
    struct mt7921_softc* sc=&g_mt7921; if(cap<2)return; char* p=out; char* e=out+cap-1; *p=0; mt7921_probe();
    if(sc->state==MT_ST_NONE){ mtg_s(p,e,"mtwifi: no MediaTek MT7921/MT7922 card found\n"); return; }
    static const char* ST[]={"none","found","firmware missing","failed","firmware running","ready"};
    p=mtg_s(p,e,"Adapter="); p=mtg_s(p,e,sc->id->name); p=mtg_s(p,e," ["); p=mtg_x(p,e,sc->vendor,4); p=mtg_s(p,e,":"); p=mtg_x(p,e,sc->device,4); p=mtg_s(p,e,"]\nState="); p=mtg_s(p,e,ST[sc->state<=MT_ST_READY?sc->state:0]);
    p=mtg_s(p,e,"\nChip=MT"); p=mtg_x(p,e,sc->chip,4); if(sc->hw_rev){ p=mtg_s(p,e," rev 0x"); p=mtg_x(p,e,sc->hw_rev,8); }
    p=mtg_s(p,e,"\nPatch=/lib/firmware/"); p=mtg_s(p,e,sc->id->patch); p=mtg_s(p,e,mtg_fw_present(sc->id->patch)?" (present)":" (MISSING)");
    if(sc->patchver[0]){ p=mtg_s(p,e," build "); p=mtg_s(p,e,sc->patchver); }
    p=mtg_s(p,e,"\nRAM code=/lib/firmware/"); p=mtg_s(p,e,sc->id->ram); p=mtg_s(p,e,mtg_fw_present(sc->id->ram)?" (present)":" (MISSING)");
    if(sc->fwver[0]){ p=mtg_s(p,e," version "); p=mtg_s(p,e,sc->fwver); }
    if(sc->builddate[0]){ p=mtg_s(p,e," built "); p=mtg_s(p,e,sc->builddate); }
    if(sc->state==MT_ST_READY){ p=mtg_s(p,e,"\nMAC="); for(int i=0;i<6;i++){ if(i)p=mtg_s(p,e,":"); p=mtg_x(p,e,sc->mac[i],2); } p=mtg_s(p,e,!mt7921_has_6g?" 6GHz=no":mt7921_6g_on?" 6GHz=yes":" 6GHz=off"); }
    if(sc->connected){ p=mtg_s(p,e,"\nConnected channel "); p=mtg_u(p,e,sc->chan); if(sc->band6)p=mtg_s(p,e," (6 GHz)"); }
    if(sc->err){ p=mtg_s(p,e,"\nLast error: "); p=mtg_s(p,e,sc->err); }
    p=mtg_s(p,e,"\nMCU commands="); p=mtg_u(p,e,sc->mcu_cmds); p=mtg_s(p,e," events="); p=mtg_u(p,e,sc->mcu_events); p=mtg_s(p,e," timeouts="); p=mtg_u(p,e,sc->mcu_timeouts);
    p=mtg_s(p,e,"\nTX packets="); p=mtg_u(p,e,sc->tx_pkts); p=mtg_s(p,e," RX packets="); p=mtg_u(p,e,sc->rx_pkts);
    p=mtg_s(p,e,"\nTX tokens busy="); p=mtg_u(p,e,(u32)mt7921_tokens_busy(sc)); p=mtg_s(p,e," freed="); p=mtg_u(p,e,sc->tok_freed); p=mtg_s(p,e," forced="); p=mtg_u(p,e,sc->tok_forced); p=mtg_s(p,e," TXFREE events="); p=mtg_u(p,e,sc->txfree_events); if(!mt7921_tok_enable)p=mtg_s(p,e," (token gating off)");
    p=mtg_s(p,e,"\nROC grants="); p=mtg_u(p,e,sc->roc_grants); p=mtg_s(p,e," timeouts="); p=mtg_u(p,e,sc->roc_timeouts); if(!mt7921_roc_enable)p=mtg_s(p,e," (ROC off)");
    mtg_s(p,e,"\n"); }
void mt7921_up(char* out,int cap){
    if(cap<2)return;
    out[0]=0;
    if(mt7921_probe()&&mt7921_start()==0)mtg_register();
    mt7921_status(out,cap); }
