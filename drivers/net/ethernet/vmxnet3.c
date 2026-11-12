#include "include/kapi.h"
#include "include/drivers.h"
#define NTX 64
#define NRX 64
#define NRX1 32
#define NRC (NRX+NRX1)
#define BUF 2048
#define VD_VRRS 0x00
#define VD_UVRS 0x08
#define VD_DSAL 0x10
#define VD_DSAH 0x18
#define VD_CMD 0x20
#define VD_MACL 0x28
#define VD_MACH 0x30
#define PT_TXPROD 0x600
#define PT_RXPROD 0x800
#define CMD_ACTIVATE 0xCAFE0000u
#define CMD_QUIESCE 0xCAFE0001u
#define CMD_RESET 0xCAFE0002u
#define CMD_GET_LINK 0xF00D0002u
struct vdesc { volatile u64 addr; volatile u32 d2, d3; };
static volatile u8* PT; static volatile u8* VD; static u8* shm; static u8* qd;
static struct vdesc *txr, *txc, *rxr, *rxr1, *rxc;
static u8 *txdata, *rxb, *txb;
static u32 tx_fill, tx_gen, tx_done, txc_i, txc_gen, rx_fill, rx_gen, rxc_i, rxc_gen;
static void pw(u32 o,u32 v){ *(volatile u32*)(PT+o)=v; }
static void vw(u32 o,u32 v){ *(volatile u32*)(VD+o)=v; }
static u32 vr(u32 o){ return *(volatile u32*)(VD+o); }
static void put32(u8* p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); }
static void put64(u8* p,u64 v){ put32(p,(u32)v); put32(p+4,(u32)(v>>32)); }
static u32 vx_cmd(u32 c){ vw(VD_CMD,c); return vr(VD_CMD); }
static void vx_reap(void){
    for(int g=0;g<NTX;g++){
        struct vdesc* c=&txc[txc_i]; u32 d3=c->d3; if((d3>>31)!=txc_gen)return;
        u32 idx=c->addr&0xFFF; tx_done=(idx+1)%NTX;
        txc_i++; if(txc_i==NTX){ txc_i=0; txc_gen^=1; }
    }
}
static u32 vx_free(void){ return (tx_done+NTX-tx_fill-1)%NTX; }
static int vx_send(const u8* d,int len){
    if(len<=0||len>BUF)return -1;
    vx_reap();
    for(int k=0;k<100000&&!vx_free();k++){ kapi_delay_us(1); vx_reap(); }
    if(!vx_free())return -1;
    u8* b=txb+tx_fill*BUF; for(int i=0;i<len;i++)b[i]=d[i]; for(int i=len;i<60;i++)b[i]=0; if(len<60)len=60;
    struct vdesc* t=&txr[tx_fill];
    t->addr=kapi_phys(b); t->d3=(1u<<12)|(1u<<13);
    kapi_mb();
    t->d2=(u32)len|(tx_gen<<14);
    tx_fill++; if(tx_fill==NTX){ tx_fill=0; tx_gen^=1; }
    kapi_mb(); pw(PT_TXPROD,tx_fill);
    return len;
}
static void vx_give(void){
    struct vdesc* x=&rxr[rx_fill];
    x->addr=kapi_phys(rxb+rx_fill*BUF); x->d3=0;
    kapi_mb();
    x->d2=(u32)BUF|(rx_gen<<31);
    rx_fill++; if(rx_fill==NRX){ rx_fill=0; rx_gen^=1; }
}
static int vx_recv(u8* out,int max){
    for(int g=0;g<NRC;g++){
        struct vdesc* c=&rxc[rxc_i]; u32 d3=c->d3; if((d3>>31)!=rxc_gen)return 0;
        kapi_mb();
        u32 d0=(u32)c->addr, d2=c->d2; u32 idx=d0&0xFFF, rq=(d0>>16)&0x3FF; int n=0;
        rxc_i++; if(rxc_i==NRC){ rxc_i=0; rxc_gen^=1; }
        if(rq!=0)continue;
        if(idx<NRX&&(d0&(1u<<14))&&(d0&(1u<<15))&&!(d2&(1u<<14))){ n=(int)(d2&0x3FFF); if(n>BUF)n=BUF; if(n>max)n=max; const u8* s=rxb+idx*BUF; for(int i=0;i<n;i++)out[i]=s[i]; }
        vx_give(); kapi_mb(); pw(PT_RXPROD,rx_fill);
        if(n>0)return n;
    }
    return 0;
}
static int vx_link(void){ return (vx_cmd(CMD_GET_LINK)&1)!=0; }
int vmxnet3_probe(struct nvx_nic* out){
    static const u16 ids[]={0x07B0,0};
    int b,d,f; if(!kapi_pci_find(0x15AD,ids,0,&b,&d,&f))return 0;
    u32 b0=kapi_pci_rd32(b,d,f,0x10), b1=kapi_pci_rd32(b,d,f,0x14);
    if((b0&1)||(b1&1)||((b0>>1)&3)==2){ kapi_log("vmxnet3: unexpected BAR layout"); return 0; }
    PT=(volatile u8*)kapi_mmio(b0&0xFFFFFFF0u,0x1000); VD=(volatile u8*)kapi_mmio(b1&0xFFFFFFF0u,0x1000);
    if(!PT||!VD){ kapi_log("vmxnet3: BARs not assigned"); return 0; }
    kapi_pci_enable(b,d,f);
    u32 rev=vr(VD_VRRS); if(!(rev&1)){ kapi_log("vmxnet3: device revision 1 not offered"); return 0; } vw(VD_VRRS,1);
    u32 up=vr(VD_UVRS); if(!(up&1)){ kapi_log("vmxnet3: UPT version 1 not offered"); return 0; } vw(VD_UVRS,1);
    vx_cmd(CMD_QUIESCE); vx_cmd(CMD_RESET);
    if(!shm){ shm=(u8*)kapi_dma_pages(1); qd=(u8*)kapi_dma_pages(1); u8* rings=(u8*)kapi_dma_pages(2); txdata=(u8*)kapi_dma_pages(NTX*128/4096); rxb=(u8*)kapi_dma_pages(NRX*BUF/4096); txb=(u8*)kapi_dma_pages(NTX*BUF/4096);
        if(!shm||!qd||!rings||!txdata||!rxb||!txb){ shm=0; kapi_log("vmxnet3: no memory for the rings"); return 0; }
        txr=(struct vdesc*)rings; txc=txr+NTX; rxr=txc+NTX; rxr1=rxr+NRX; rxc=rxr1+NRX1; }
    for(int i=0;i<4096;i++){ shm[i]=0; qd[i]=0; }
    for(int i=0;i<NTX;i++){ txr[i].addr=0; txr[i].d2=0; txr[i].d3=0; txc[i].addr=0; txc[i].d2=0; txc[i].d3=0; }
    for(int i=0;i<NRX;i++){ rxr[i].addr=0; rxr[i].d2=0; rxr[i].d3=0; }
    for(int i=0;i<NRX1;i++){ rxr1[i].addr=0; rxr1[i].d2=0; rxr1[i].d3=0; }
    for(int i=0;i<NRC;i++){ rxc[i].addr=0; rxc[i].d2=0; rxc[i].d3=0; }
    tx_fill=0; tx_gen=1; tx_done=0; txc_i=0; txc_gen=1; rx_fill=0; rx_gen=1; rxc_i=0; rxc_gen=1;
    put32(shm+0,0xBABEFEE1u); put32(shm+4,720);
    put32(shm+8,1); put32(shm+12,2u|(1u<<2)); put32(shm+16,1); put32(shm+20,1);
    put64(shm+40,kapi_phys(qd)); put32(shm+52,512); put32(shm+56,1500);
    shm[60]=1; shm[62]=1; shm[63]=1;
    shm[80]=1; shm[81]=1; shm[82]=0; put32(shm+108,1);
    put32(shm+120,0x1Fu);
    put32(qd+4,1);
    put64(qd+16,kapi_phys(txr)); put64(qd+24,kapi_phys(txdata)); put64(qd+32,kapi_phys(txc));
    put32(qd+56,NTX); put32(qd+60,NTX); put32(qd+64,NTX);
    u8* rq=qd+256;
    put64(rq+16,kapi_phys(rxr)); put64(rq+24,kapi_phys(rxr1)); put64(rq+32,kapi_phys(rxc));
    put32(rq+56,NRX); put32(rq+60,NRX1); put32(rq+64,NRC);
    for(int i=0;i<NRX-1;i++)vx_give();
    kapi_mb();
    vw(VD_DSAL,(u32)kapi_phys(shm)); vw(VD_DSAH,(u32)(kapi_phys(shm)>>32));
    for(int i=0;i<8;i++)pw((u32)i*8,1);
    u32 res=vx_cmd(CMD_ACTIVATE);
    if(res){ kapi_log("vmxnet3: device refused the configuration"); return 0; }
    vx_cmd(0xCAFE0003u); vx_cmd(0xCAFE0004u);
    pw(PT_RXPROD,rx_fill);
    u32 ml=vr(VD_MACL), mh=vr(VD_MACH);
    for(int i=0;i<4;i++)out->mac[i]=(u8)(ml>>(8*i));
    out->mac[4]=(u8)mh; out->mac[5]=(u8)(mh>>8);
    out->name="VMware vmxnet3"; out->send=vx_send; out->recv=vx_recv; out->link=vx_link;
    kapi_log("vmxnet3: up");
    return 1;
}
