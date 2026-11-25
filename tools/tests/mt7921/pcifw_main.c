#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "drivers/net/wifi/mediatek/mt7921/mt7921.h"
#include "drivers/net/wifi/mediatek/mt7921/pci/mt7921_regs.h"

struct mt7921_softc g_mt7921;
static u8* bar;
static u64 now_ns;
static int fail_n, cmds[64], ncmd, fwdl_chunks, sem_state, rx_events, rx_datas;
static u32 fwdl_bytes, dl_addr[64], dl_len[64], ndl, start_opt, start_addr;
static const char* fw_dir;
#define R(o) (*(volatile u32*)(bar+(o)))
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); fail_n++; } }while(0)

static void tick(void){
    u32 lp=R(0xe0010); if(lp&1)R(0xe0010)=(lp&~3u)|4u; else if(lp&2)R(0xe0010)=lp&~6u;
    if(R(0x40140)&1)R(0x40140)|=0x10; else R(0x40140)&=~0x10u;
    R(0x40010)&=~1u;
}
u64 hr_ns(void){ now_ns+=20000; tick(); return now_ns; }
void hr_delay_us(u32 us){ now_ns+=(u64)us*1000; tick(); }
void hr_delay_ms(u32 ms){ now_ns+=(u64)ms*1000000; tick(); }
void klog(const char* s){ printf("  klog: %s\n",s); }
void klog2(const char* a,const char* b){ printf("  klog: %s%s\n",a,b); }
void* kmalloc(unsigned long n){ return malloc(n); }
void kfree(void* p){ free(p); }
u32 pci_cfg(int b,int s,int f,int off){ (void)b;(void)s;(void)f; if(off==0x10)return (u32)(unsigned long)bar; return 0; }
void pci_cfgw(int b,int s,int f,int off,u32 v){ (void)b;(void)s;(void)f;(void)off;(void)v; }
void* dma_alloc_pages(unsigned long pages){ void* p=mmap(0,pages*4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0); return p==MAP_FAILED?0:p; }

static void hw_complete(int qid){ struct mt_ring* r=&g_mt7921.q[qid]; R(mt7921_reg_addr(&g_mt7921,r->regbase+0xc))=R(mt7921_reg_addr(&g_mt7921,r->regbase+0x8)); }
int mt7921_mcu_send(struct mt7921_softc* sc,u32 cmd,const void* data,u32 len,int wait){
    const u8* d=data; (void)wait;
    if(mt7921_dma_tx(sc,MT_Q_MCU,d,len)<0)return -1;
    hw_complete(MT_Q_MCU);
    if(ncmd<64)cmds[ncmd++]=(int)cmd;
    u32 a=(u32)d[0]|((u32)d[1]<<8)|((u32)d[2]<<16)|((u32)d[3]<<24);
    u32 b=len>=8?((u32)d[4]|((u32)d[5]<<8)|((u32)d[6]<<16)|((u32)d[7]<<24)):0;
    if(cmd==MCU_CMD(MCU_CMD_PATCH_SEM_CONTROL)){ if(d[0]==PATCH_SEM_GET)return sem_state; return PATCH_REL_SEM_SUCCESS; }
    if(cmd==MCU_CMD(MCU_CMD_PATCH_START_REQ)||cmd==MCU_CMD(MCU_CMD_TARGET_ADDRESS_LEN_REQ)){ if(ndl<64){ dl_addr[ndl]=a; dl_len[ndl]=b; ndl++; } fwdl_bytes=0; return 0; }
    if(cmd==MCU_CMD(MCU_CMD_FW_START_REQ)){ start_opt=a; start_addr=b; R(0xe00f0)=3; return 0; }
    if(cmd==MCU_CMD(MCU_CMD_NIC_POWER_CTRL)){ R(0xe00f0)=1; return 0; }
    return 0;
}
int mt7921_mcu_send_fwdl(struct mt7921_softc* sc,const u8* data,u32 len){
    if(!len||len>MT_FWDL_CHUNK)return -1;
    if(mt7921_dma_tx(sc,MT_Q_FWDL,data,len)<0)return -1;
    struct mt_ring* r=&sc->q[MT_Q_FWDL]; int i=(r->head+r->n-1)%r->n;
    struct mt_wfdma_desc* dd=(struct mt_wfdma_desc*)(void*)r->desc.va+i;
    if(MT_DMA_CTL_LEN0_GET(dd->ctrl)!=len||!(dd->ctrl&MT_DMA_CTL_LAST_SEC0)||memcmp((u8*)(unsigned long)dd->buf0,data,len))fail_n++;
    hw_complete(MT_Q_FWDL);
    fwdl_chunks++; fwdl_bytes+=len; return 0;
}
void mt7921_mcu_rx_event(struct mt7921_softc* sc,const u8* buf,u32 len){ (void)sc; if(len==100&&buf[0]==0xaa)rx_events++; }
void mt7921_rx_data(struct mt7921_softc* sc,const u8* buf,u32 len){ (void)sc; if(len==60&&buf[0]==0xbb)rx_datas++; }
static int rd(const char* path,u8** out,u32* len){
    const char* nm=strrchr(path,'/'); char p[512]; snprintf(p,sizeof p,"%s%s",fw_dir,nm);
    FILE* f=fopen(p,"rb"); if(!f)return -1; fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    u8* b=malloc((size_t)n); if(fread(b,1,(size_t)n,f)!=(size_t)n){ fclose(f); free(b); return -1; } fclose(f); *out=b; *len=(u32)n; return 0;
}
int (*mt7921_firmware_read)(const char* path,u8** out,u32* len)=rd;
int mt7921_patch_check(const u8* d,u32 n);
int mt7921_ram_check(const u8* d,u32 n);

static void reset_bar(u32 chipid){
    memset(bar,0,MT_BAR0_SIZE); memset(&g_mt7921,0,sizeof g_mt7921);
    R(0x40200)=chipid; R(0x40204)=0x10;
    g_mt7921.state=MT_ST_FOUND; ncmd=0; ndl=0; fwdl_chunks=0;
}

static void run_chip(u32 chipid,const char* patch,const char* ram){
    printf("chip %04x\n",chipid);
    reset_bar(chipid);
    struct mt7921_softc* sc=&g_mt7921;
    CHECK(mt7921_pci_init(sc)==0);
    CHECK(sc->chip==chipid&&sc->hw_rev==((chipid<<16)|0x10));
    CHECK(mt7921_dma_init(sc)==0);
    CHECK(R(0xd4400+4)==MT_FWDL_RING&&R(0xd4410+4)==MT_MCU_RING&&R(0xd4300+4)==MT_TX_RING);
    CHECK(R(0xd4500+4)==MT_RX_MCU_RING&&R(0xd4520+4)==MT_RX_RING&&R(0xd4540+4)==MT_RX_MCU_RING);
    CHECK(R(0xd4500+8)==MT_RX_MCU_RING-1&&R(0xd4500)==(u32)sc->q[MT_Q_RX_MCU].desc.pa);
    CHECK((R(0xd4208)&5u)==5u);
    CHECK(R(0xd4644)==((0x380u<<16)|4)&&R(0xd4680)==4);
    u8* pb; u32 pn; CHECK(rd(patch,&pb,&pn)==0); int pr=mt7921_patch_check(pb,pn); printf("  patch regions %d\n",pr); CHECK(pr>0);
    u8* rb; u32 rn; CHECK(rd(ram,&rb,&rn)==0); int rr=mt7921_ram_check(rb,rn); printf("  ram regions %d\n",rr); CHECK(rr>0);
    u32 ramdl=0; { const u8* reg0=rb+rn-36-(u32)rr*40; for(int i=0;i<rr;i++) if(!(reg0[i*40+24]&FW_FEATURE_NON_DL))ramdl++; }
    sem_state=PATCH_NOT_DL_SEM_SUCCESS;
    CHECK(mt7921_fw_load(sc)==0);
    printf("  fw %s build %s patch %s, %d cmds, %d fwdl chunks, %u dl regions, start opt %u addr %08x\n",sc->fwver,sc->builddate,sc->patchver,ncmd,fwdl_chunks,ndl,start_opt,start_addr);
    CHECK(sc->state==MT_ST_FWOK&&ndl==(u32)pr+ramdl&&dl_addr[0]==mt_be32(*(u32*)(pb+96+12)));
    CHECK(cmds[0]==MCU_CMD(MCU_CMD_NIC_POWER_CTRL)&&cmds[1]==MCU_CMD(MCU_CMD_PATCH_SEM_CONTROL)&&cmds[2]==MCU_CMD(MCU_CMD_PATCH_START_REQ));
    CHECK(cmds[ncmd-1]==MCU_CMD(MCU_CMD_FW_START_REQ));
    CHECK(fwdl_bytes==dl_len[ndl-1]);
    struct mt_ring* r=&sc->q[MT_Q_RX_MCU]; struct mt_wfdma_desc* d=(struct mt_wfdma_desc*)(void*)r->desc.va;
    for(int k=0;k<3;k++){ int i=(r->tail+k)%r->n; r->buf.va[(u32)i*r->bufsz]=0xaa; d[i].ctrl=MT_DMA_CTL_SD_LEN0(100)|MT_DMA_CTL_LAST_SEC0|MT_DMA_CTL_DMA_DONE; }
    struct mt_ring* q=&sc->q[MT_Q_RX_DATA]; struct mt_wfdma_desc* e=(struct mt_wfdma_desc*)(void*)q->desc.va;
    q->buf.va[0]=0xbb; e[0].ctrl=MT_DMA_CTL_SD_LEN0(60)|MT_DMA_CTL_LAST_SEC0|MT_DMA_CTL_DMA_DONE;
    rx_events=rx_datas=0; mt7921_rx_poll(sc);
    CHECK(rx_events==3&&rx_datas==1&&R(0xd4508)==2&&R(0xd4528)==0&&!(d[0].ctrl&MT_DMA_CTL_DMA_DONE)&&MT_DMA_CTL_LEN0_GET(d[0].ctrl)==MT_RXBUF_SIZE);
    mt7921_rx_poll(sc); CHECK(rx_events==3);
    { int i0=q->tail,i1=(i0+1)%q->n,i2=(i0+2)%q->n; q->buf.va[(u32)i0*q->bufsz]=0xbb; q->buf.va[(u32)i1*q->bufsz]=0xbb; q->buf.va[(u32)i2*q->bufsz]=0xbb;
      e[i0].ctrl=MT_DMA_CTL_SD_LEN0(60)|MT_DMA_CTL_DMA_DONE; e[i1].ctrl=MT_DMA_CTL_SD_LEN0(60)|MT_DMA_CTL_LAST_SEC0|MT_DMA_CTL_DMA_DONE; e[i2].ctrl=MT_DMA_CTL_SD_LEN0(60)|MT_DMA_CTL_LAST_SEC0|MT_DMA_CTL_DMA_DONE;
      rx_datas=0; mt7921_rx_poll(sc); CHECK(rx_datas==1&&q->tail==(i0+3)%q->n); }
    sem_state=PATCH_IS_DL; ncmd=0; ndl=0; CHECK(mt7921_fw_load(sc)==0); CHECK(ndl==ramdl);
    sem_state=PATCH_NOT_DL_SEM_FAIL; CHECK(mt7921_fw_load(sc)<0);
    pb[96+3]=0x40; CHECK(mt7921_patch_check(pb,pn)<0);
    CHECK(mt7921_ram_check(rb,40)<0); rb[rn-36+2]=255; CHECK(mt7921_ram_check(rb,rn)<0);
    for(u32 cut=0;cut<pn;cut+=pn/97+1)mt7921_patch_check(pb,cut);
    for(u32 cut=0;cut<rn;cut+=rn/97+1)mt7921_ram_check(rb+rn-cut,cut);
    free(pb); free(rb);
    sem_state=PATCH_NOT_DL_SEM_SUCCESS;
    for(int k=0;k<MT_TX_RING+5;k++){ u8 x[64]={1}; if(mt7921_dma_tx(sc,MT_Q_TX,x,64)<0){ CHECK(k==MT_TX_RING-1); break; } }
    hw_complete(MT_Q_TX); { u8 x[64]={1}; CHECK(mt7921_dma_tx(sc,MT_Q_TX,x,64)==0); CHECK(mt7921_dma_tx(sc,MT_Q_TX,x,MT_TXBUF_SIZE+1)<0);
      struct mt_ring* tq=&sc->q[MT_Q_TX]; int ti=(tq->head+tq->n-1)%tq->n; u8* tb=tq->buf.va+(u32)ti*tq->bufsz; struct mt_wfdma_desc* td=(struct mt_wfdma_desc*)(void*)tq->desc.va+ti;
      u32 fpa=(u32)(tq->buf.pa+(u64)ti*tq->bufsz)+64;
      CHECK(MT_DMA_CTL_LEN0_GET(td->ctrl)==64&&(td->ctrl&MT_DMA_CTL_LAST_SEC0)&&(tb[32]|(tb[33]<<8))==(ti|0x8000)&&*(u32*)(tb+40)==fpa&&(tb[44]|(tb[45]<<8))==(32|0x8000)&&tb[0]==1&&tb[64]==0);
      CHECK(mt7921_dma_tx(sc,MT_Q_TX,x,MT_TXD_SIZE)<0); }
    { u8 x[64]={1}; u32 f0=sc->tok_forced,r0=sc->tok_freed; struct mt_ring* tq=&sc->q[MT_Q_TX];
      hw_complete(MT_Q_TX); CHECK(mt7921_tokens_busy(sc)==MT_TX_RING&&tq->head==0); CHECK(mt7921_dma_tx(sc,MT_Q_TX,x,64)<0&&sc->tok_forced==f0);
      CHECK(mt7921_tx_token_release(sc,0x8000)==0&&sc->tok_freed==r0+1); CHECK(mt7921_tx_token_release(sc,0x8000)<0&&sc->tok_stale>=1); CHECK(mt7921_tx_token_release(sc,0x7fff)<0);
      CHECK(mt7921_dma_tx(sc,MT_Q_TX,x,64)==0&&tq->head==1);
      hr_delay_ms(1100); hw_complete(MT_Q_TX); CHECK(mt7921_dma_tx(sc,MT_Q_TX,x,64)==0&&sc->tok_forced==f0+1);
      sc->txfree_events=0; sc->tok_forced=31; hr_delay_ms(1100); hw_complete(MT_Q_TX); CHECK(mt7921_dma_tx(sc,MT_Q_TX,x,64)==0&&!mt7921_tok_enable&&mt7921_tokens_busy(sc)==0);
      hw_complete(MT_Q_TX); CHECK(mt7921_dma_tx(sc,MT_Q_TX,x,64)==0&&mt7921_tokens_busy(sc)==0); mt7921_tok_enable=1; }
    reset_bar(0x7902); CHECK(mt7921_pci_init(sc)<0&&sc->state==MT_ST_HWFAIL);
    reset_bar(chipid); fw_dir="/nonexistent"; CHECK(mt7921_pci_init(sc)==0&&mt7921_dma_init(sc)==0); CHECK(mt7921_fw_load(sc)<0&&sc->state==MT_ST_NOFW); fw_dir=getenv("MT_FW_DIR");
}

int main(int argc,char** argv){
    (void)argc;(void)argv;
    fw_dir=getenv("MT_FW_DIR"); if(!fw_dir){ printf("set MT_FW_DIR\n"); return 2; }
    bar=mmap(0,MT_BAR0_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
    if(bar==MAP_FAILED||(unsigned long)bar<0x100000)return 2;
    run_chip(0x7961,"x/WIFI_MT7961_patch_mcu_1_2_hdr.bin","x/WIFI_RAM_CODE_MT7961_1.bin");
    run_chip(0x7922,"x/WIFI_MT7922_patch_mcu_1_1_hdr.bin","x/WIFI_RAM_CODE_MT7922_1.bin");
    R(0x40020)=0x80;
    { reset_bar(0x7961); R(0x40020)=0x80; CHECK(mt7921_pci_init(&g_mt7921)==0&&g_mt7921.chip==0x7920); }
    printf(fail_n?"mt7921 pci/fw: %d FAILED\n":"mt7921 pci/fw: all passed\n",fail_n);
    return fail_n?1:0;
}
