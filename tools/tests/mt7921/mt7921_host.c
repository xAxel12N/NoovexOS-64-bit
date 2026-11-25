#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "drivers/net/wifi/mediatek/mt7921/mt7921.h"
#include "include/nvx/net/wifi/wifi_hw.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/fs/vfs/vfs.h"
static const char* FWDIR;
u16 pci_ven[PCI_MAX], pci_dev[PCI_MAX]; u8 pci_cls[PCI_MAX], pci_sub[PCI_MAX], pci_bus[PCI_MAX], pci_slot[PCI_MAX]; int pci_n; static int scans; static u32 bar0=0xfe000004u, bar1;
void pci_scan(void){ scans++; }
u32 pci_cfg(int b,int s,int f,int off){ (void)b;(void)s;(void)f; return off==0x10?bar0:off==0x14?bar1:0; }
void klog(const char* s){ printf("klog: %s\n",s); }
void* kmalloc(unsigned long n){ return malloc(n); }
void kfree(void* p){ free(p); }
static int hide_ram;
static const char* hpath(const char* p,char* o){ if(strncmp(p,"/lib/firmware/",14))return 0; if(hide_ram&&strstr(p,"RAM_CODE"))return 0; snprintf(o,512,"%s/%s",FWDIR,p+14); return o; }
static int h_size(const char* p){ char b[512]; if(!hpath(p,b))return -1; FILE* f=fopen(b,"rb"); if(!f)return -1; fseek(f,0,SEEK_END); int n=(int)ftell(f); fclose(f); return n; }
static int h_read(const char* p,char* buf,int off,int len){ char b[512]; if(!hpath(p,b))return -1; FILE* f=fopen(b,"rb"); if(!f)return -1; fseek(f,off,SEEK_SET); int n=(int)fread(buf,1,(size_t)len,f); fclose(f); return n; }
static const vfs_fs_ops HFS={"host",0,h_read,h_size};
const vfs_fs_ops* vfs_lookup(const char* p){ (void)p; return &HFS; }
static const struct wifi_hw_ops* reg_ops; static void* reg_ctx; static int rx_frames;
int g_wpa_akm=2;
static u64 h_now=1000; u64 mt_ms(void){ return h_now; }
int mt7921_has_6g,mt7921_6g_on=1,mt_assoc_eht_res=-1,g_wifi_scanning; static int sp_n,sp_ch;
int wifi_scan_parse(const u8* f,int l,u8 ch,int r,int o){ (void)f;(void)l;(void)r;(void)o; sp_n++; sp_ch=ch; return 0; }
int wifi_hw_register(const struct wifi_hw_ops* o,void* c){ reg_ops=o; reg_ctx=c; return 0; }
void wifi_hw_rx(const u8* f,int len,int rssi){ (void)f;(void)len;(void)rssi; rx_frames++; }
static int fw_rc, pci_rc, calls_fw, calls_mcu, scan_n; static u8* fwp; static u32 fwpl; static u8* fwr; static u32 fwrl;
int mt7921_pci_init(struct mt7921_softc* sc){ (void)sc; return pci_rc; }
int mt7921_dma_init(struct mt7921_softc* sc){ (void)sc; return 0; }
int mt7921_fw_load(struct mt7921_softc* sc){ calls_fw++; if(fw_rc)return fw_rc; free(fwp); free(fwr); fwp=fwr=0;
    if(mt7921_firmware_read(sc->id->patch,&fwp,&fwpl)||mt7921_firmware_read(sc->id->ram,&fwr,&fwrl)){ sc->err="firmware file missing"; return -1; } return 0; }
int mt7921_mcu_init(struct mt7921_softc* sc){ calls_mcu++; u8 m[6]={0x00,0x0c,0xe7,1,2,3}; memcpy(sc->mac,m,6); return 0; }
int mt7921_hw_scan(struct mt7921_softc* sc,const u8* ch,int n){ (void)ch; sc->scanning=1; scan_n=n; return 0; }
int mt7921_scan_busy(struct mt7921_softc* sc){ return sc->scanning; }
int mt7921_connect(struct mt7921_softc* sc,const u8* b,u8 ch,const char* s,u8 a,u8 h){ (void)b;(void)s;(void)a;(void)h; sc->connected=1; sc->chan=ch; return 0; }
void mt7921_disconnect(struct mt7921_softc* sc){ sc->connected=0; }
int mt7921_set_key(struct mt7921_softc* sc,int i,int t,const u8* k,int l,int p){ (void)sc;(void)i;(void)k;(void)p; return t==WIFI_KEY_CCMP&&l==16?0:-1; }
int mt7921_tx_data(struct mt7921_softc* sc,const u8* f,u32 l){ (void)f; sc->tx_pkts++; return (int)l; }
int mt7921_roc_enable=1,mt7921_tok_enable=1;
int mt7921_tokens_busy(struct mt7921_softc* sc){ (void)sc; return 0; }
static int polls; void mt7921_rx_poll(struct mt7921_softc* sc){ (void)sc; polls++; }
static int drains; void mt_ba_drain(struct mt7921_softc* sc){ (void)sc; drains++; }
#include "drivers/net/wifi/mediatek/mt7921/glue.c"

static u8* slurp(const char* rel,u32* n){ char b[512]; snprintf(b,sizeof b,"%s/%s",FWDIR,rel); FILE* f=fopen(b,"rb"); assert(f); fseek(f,0,SEEK_END); *n=(u32)ftell(f); fseek(f,0,SEEK_SET); u8* d=malloc(*n); assert(fread(d,1,*n,f)==*n); fclose(f); return d; }
static void check_patch(const char* rel,const char* plat_chip){
    u32 n; u8* d=slurp(rel,&n); const struct mt_patch_hdr* h=(const void*)d;
    u32 nr=mt_be32(h->desc.n_region); assert(n>sizeof *h); assert(nr>=1&&nr<=8);
    char plat[5]={0}; memcpy(plat,h->platform,4); char bd[17]={0}; memcpy(bd,h->build_date,16);
    assert(sizeof *h+nr*sizeof(struct mt_patch_sec)<=n);
    int info=0; for(u32 i=0;i<nr;i++){ const struct mt_patch_sec* s=(const void*)(d+sizeof *h+i*sizeof(struct mt_patch_sec));
        if((mt_be32(s->type)&PATCH_SEC_TYPE_MASK)!=PATCH_SEC_TYPE_INFO)continue; info++;
        u32 o=mt_be32(s->offs), l=mt_be32(s->info.len); assert(o<n&&l<=n-o&&l>0); assert(mt_be32(s->info.addr)); }
    assert(info>=1);
    printf("patch %s: platform %s build %s hw_sw 0x%08x regions %u\n",rel,plat,bd,mt_be32(h->hw_sw_ver),nr); (void)plat_chip; free(d); }
static void check_ram(const char* rel,u8 chip){
    u32 n; u8* d=slurp(rel,&n); assert(n>sizeof(struct mt_fw_trailer));
    const struct mt_fw_trailer* t=(const void*)(d+n-sizeof *t);
    assert(t->n_region>=1&&t->n_region<=16); assert(t->chip_id==chip);
    u32 rs=sizeof *t+t->n_region*sizeof(struct mt_fw_region); assert(rs<n);
    u32 sum=0; for(int i=0;i<t->n_region;i++){ const struct mt_fw_region* r=(const void*)(d+n-rs+i*sizeof(struct mt_fw_region)); sum+=r->len; assert(r->addr||r->type==2||!r->len); }
    assert(sum<=n-rs);
    char v[11]={0},bd[16]={0}; memcpy(v,t->fw_ver,10); memcpy(bd,t->build_date,15);
    printf("ram %s: chip 0x%02x eco %u regions %u ver %s build %s data %u/%u\n",rel,t->chip_id,t->eco_code,t->n_region,v,bd,sum,n-rs); free(d); }
static void reset(void){ memset(&g_mt7921,0,sizeof g_mt7921); reg_ops=0; reg_ctx=0; pci_n=0; fw_rc=pci_rc=0; calls_fw=calls_mcu=0; hide_ram=0; bar0=0xfe000004u; bar1=0; free(fwp); free(fwr); fwp=fwr=0; }
static void dev(u16 v,u16 d){ pci_ven[pci_n]=v; pci_dev[pci_n]=d; pci_bus[pci_n]=3; pci_slot[pci_n]=0; pci_n++; }
int main(int argc,char** argv){
    FWDIR=argc>1?argv[1]:"assets/firmware/linux-firmware"; static char b[1024];
    assert(mt7921_match(0x14c3,0x7961)->chip==MT_CHIP_7961&&mt7921_match(0x14c3,0x0608)->chip==MT_CHIP_7961);
    assert(mt7921_match(0x14c3,0x7920)->chip==MT_CHIP_7920&&mt7921_match(0x14c3,0x7922)->chip==MT_CHIP_7922);
    assert(mt7921_match(0x14c3,0x0616)->chip==MT_CHIP_7922&&mt7921_match(0x0b48,0x7922)->chip==MT_CHIP_7922);
    assert(!mt7921_match(0x10ec,0xc821)&&!mt7921_match(0x8086,0x2723)&&!mt7921_match(0x14c3,0x7925)&&!mt7921_match(0x14c3,0x7902)&&!mt7921_match(0,0));
    for(const struct mt_pci_id* i=mt7921_ids;i->vendor;i++){ check_patch(i->patch,0); check_ram(i->ram,i->chip==MT_CHIP_7922?0x10:0x0d); }
    reset(); dev(0x10ec,0xc821); dev(0x8086,0x1234);
    assert(mt7921_probe()==0&&g_mt7921.state==MT_ST_NONE); mt7921_wifi_hw_probe(); assert(!reg_ops&&!calls_fw);
    mt7921_status(b,sizeof b); assert(strstr(b,"no MediaTek")); mt7921_up(b,sizeof b); assert(strstr(b,"no MediaTek")&&!calls_fw);
    reset(); dev(0x10ec,0xc821); dev(0x14c3,0x7922); mt7921_wifi_hw_probe();
    assert(g_mt7921.state==MT_ST_READY&&g_mt7921.mmio==0xfe000000u&&calls_fw==1&&calls_mcu==1&&reg_ops&&reg_ctx==&g_mt7921);
    assert(fwpl>100000&&fwrl>1000000);
    mt7921_status(b,sizeof b); printf("%s",b); assert(strstr(b,"MT7922")&&strstr(b,"State=ready")&&strstr(b,"(present)")&&strstr(b,"MAC=00:0c:e7:01:02:03"));
    assert(reg_ops->init(reg_ctx)==0&&calls_fw==1);
    u8 ch[3]={1,6,11}; assert(reg_ops->scan_start(reg_ctx,ch,3)==0&&scan_n==3&&!reg_ops->scan_done(reg_ctx)); g_mt7921.scanning=0; assert(reg_ops->scan_done(reg_ctx));
    u8 bs[6]={1,2,3,4,5,6}; assert(reg_ops->join(reg_ctx,bs,6,"home",2,1)==0&&g_mt7921.connected);
    { u8 c6[64]; assert(mt7921_scan_list(ch,3,c6,64,0)==3); assert(mt7921_scan_list(ch,3,c6,64,1)==18&&c6[3]==MT7921_VCH6+1&&c6[17]==MT7921_VCH6+57); assert(mt7921_scan_list(ch,3,c6,5,1)==5); { const u8 f6[1]={MT7921_VCH6+9}; assert(mt7921_scan_list(f6,1,c6,64,1)==1&&c6[0]==MT7921_VCH6+9); assert(mt7921_scan_list(f6,1,c6,64,0)==0); const u8 bad[1]={MT7921_VCH6+59}; assert(mt7921_scan_list(bad,1,c6,64,1)==15); }
      mt7921_has_6g=1; g_mt7921.scanning=0; assert(reg_ops->scan_start(reg_ctx,ch,3)==0&&scan_n==18); g_mt7921.scanning=0;
      mt7921_6g_on=0; assert(reg_ops->scan_start(reg_ctx,ch,3)==0&&scan_n==3); g_mt7921.scanning=0; mt7921_6g_on=1;
      u8 bc[40]={0x80}; g_mt7921.rx_ch=181+9; int rf=rx_frames; g_wifi_scanning=1; mt7921_rx_deliver(bc,40,-50); assert(sp_n==1&&sp_ch==MT7921_VCH6+9&&rx_frames==rf);
      g_wifi_scanning=0; mt7921_rx_deliver(bc,40,-50); assert(sp_n==1&&rx_frames==rf); g_mt7921.rx_ch=36; mt7921_rx_deliver(bc,40,-50); assert(rx_frames==rf+1);
      reg_ops->leave(reg_ctx); g_wpa_akm=2; assert(reg_ops->join(reg_ctx,bs,MT7921_VCH6+9,"six",2,1)==2&&!g_mt7921.band6);
      g_wpa_akm=8; assert(reg_ops->join(reg_ctx,bs,MT7921_VCH6+9,"six",2,1)<0&&!g_mt7921.band6&&!g_mt7921.connected);
      memcpy(bc+16,bs,6); g_mt7921.rx_ch=181+9; g_wifi_scanning=1; mt7921_rx_deliver(bc,40,-50); g_wifi_scanning=0; g_mt7921.rx_ch=36;
      assert(reg_ops->join(reg_ctx,bs,MT7921_VCH6+13,"six",2,1)<0&&!g_mt7921.connected);
      h_now+=31000; assert(reg_ops->join(reg_ctx,bs,MT7921_VCH6+9,"six",2,1)<0&&!g_mt7921.connected); h_now-=31000;
      printf("mt7921 6 GHz: no join (no transmit) before a beacon from that network on that channel within 30 s: ok\n");
      assert(reg_ops->join(reg_ctx,bs,MT7921_VCH6+9,"six",2,1)==0&&g_mt7921.band6&&g_mt7921.chan==37);
      mt7921_status(b,sizeof b); assert(strstr(b,"6GHz=yes")&&strstr(b,"(6 GHz)")); reg_ops->leave(reg_ctx); assert(!g_mt7921.band6);
      mt7921_has_6g=0; assert(reg_ops->join(reg_ctx,bs,MT7921_VCH6+9,"six",2,1)<0); g_wpa_akm=2; mt7921_has_6g=0; assert(reg_ops->join(reg_ctx,bs,6,"home",2,1)==0); rx_frames=0; g_mt7921.rx_ch=0; }
    u8 k[16]={0}; assert(reg_ops->set_key(reg_ctx,0,WIFI_KEY_CCMP,k,16,1)==0);
    u8 fr[64]={0x08}; assert(reg_ops->tx(reg_ctx,fr,64)==64&&reg_ops->tx(reg_ctx,fr,0)<0);
    reg_ops->poll(reg_ctx); assert(polls==1); mt7921_rx_deliver(fr,64,-42); assert(rx_frames==1&&reg_ops->get_rssi(reg_ctx)==-42);
    u8 mac[6]; assert(reg_ops->get_mac(reg_ctx,mac)==0&&mac[2]==0xe7); reg_ops->leave(reg_ctx); assert(!g_mt7921.connected);
    reset(); dev(0x14c3,0x7961); hide_ram=1; mt7921_wifi_hw_probe();
    assert(g_mt7921.state==MT_ST_NOFW&&!reg_ops); mt7921_status(b,sizeof b); printf("%s",b); assert(strstr(b,"WIFI_RAM_CODE_MT7961_1.bin (MISSING)")&&strstr(b,"firmware missing"));
    hide_ram=0; mt7921_up(b,sizeof b); assert(g_mt7921.state==MT_ST_READY&&reg_ops);
    reset(); dev(0x14c3,0x7961); fw_rc=-1; mt7921_wifi_hw_probe(); assert(g_mt7921.state==MT_ST_HWFAIL&&!reg_ops&&g_mt7921.err);
    reset(); dev(0x14c3,0x0616); pci_rc=-1; mt7921_wifi_hw_probe(); assert(g_mt7921.state==MT_ST_HWFAIL&&!calls_fw&&!reg_ops);
    reset(); dev(0x14c3,0x7922); bar0=0x0000000cu; bar1=0x1; mt7921_wifi_hw_probe(); assert(g_mt7921.state==MT_ST_HWFAIL&&!calls_fw); mt7921_status(b,sizeof b); assert(strstr(b,"above 4 GB"));
    reset(); dev(0x14c3,0x7922); bar0=0x1001; mt7921_wifi_hw_probe(); assert(g_mt7921.state==MT_ST_HWFAIL&&!calls_fw);
    reset(); dev(0x14c3,0x7922); mt7921_status(b,4); assert(strlen(b)<=3);
    printf("mt7921 glue ok\n"); return 0; }
