#include "include/kapi.h"
#include "include/drivers.h"
#define R_IDR 0x00
#define R_TNPDS 0x20
#define R_CMD 0x37
#define R_TPOLL 0x38
#define R_IMR 0x3C
#define R_ISR 0x3E
#define R_TCR 0x40
#define R_RCR 0x44
#define R_9346 0x50
#define R_PHYS 0x6C
#define R_RMS 0xDA
#define R_CPCR 0xE0
#define R_RDSAR 0xE4
#define R_MTPS 0xEC
#define NRX 32
#define NTX 16
#define BUFSZ 2048
#define OWN 0x80000000u
#define EOR 0x40000000u
#define FS 0x20000000u
#define LS 0x10000000u
struct desc { volatile u32 opts1, opts2; volatile u64 addr; };
static u16 io;
static struct desc* rx; static struct desc* tx;
static u8* rxb; static u8* txb;
static int rxi, txi;
static void o32(int r,u32 v){ kapi_outl(io+r,v); }
static int r69_send(const u8* d,int len){
    if(len>BUFSZ)return -1;
    struct desc* t=&tx[txi];
    for(int w=0;w<100000&&(t->opts1&OWN);w++)kapi_delay_us(1);
    if(t->opts1&OWN)return -1;
    u8* b=txb+txi*BUFSZ; for(int i=0;i<len;i++)b[i]=d[i]; for(int i=len;i<60;i++)b[i]=0; if(len<60)len=60;
    t->addr=kapi_phys(b); t->opts2=0;
    kapi_mb();
    t->opts1=OWN|FS|LS|(txi==NTX-1?EOR:0)|(u32)len;
    kapi_mb();
    kapi_outb(io+R_TPOLL,0x40);
    txi=(txi+1)%NTX;
    return len;
}
static int r69_recv(u8* out,int max){
    struct desc* r=&rx[rxi];
    u32 o=r->opts1; if(o&OWN)return 0;
    int n=0;
    if((o&(FS|LS))==(FS|LS)&&!(o&(1u<<21))){ n=(int)(o&0x3FFF); if(n>=4)n-=4; else n=0; if(n>max)n=max; const u8* b=rxb+rxi*BUFSZ; for(int i=0;i<n;i++)out[i]=b[i]; }
    r->opts2=0; r->addr=kapi_phys(rxb+rxi*BUFSZ);
    kapi_mb();
    r->opts1=OWN|(rxi==NRX-1?EOR:0)|BUFSZ;
    rxi=(rxi+1)%NRX;
    kapi_outw(io+R_ISR,0xFFFF);
    return n;
}
static int r69_link(void){ return (kapi_inb(io+R_PHYS)&0x02)!=0; }
int rtl8169_probe(struct nvx_nic* out){
    static const u16 ids[]={0x8161,0x8168,0x8169,0x8136,0x8167,0x2600,0x2502,0};
    int b,d,f; if(!kapi_pci_find(0x10EC,ids,0,&b,&d,&f))return 0;
    u32 bar=kapi_pci_rd32(b,d,f,0x10); if(!(bar&1))return 0;
    io=(u16)(bar&0xFFFC); if(!io)return 0;
    kapi_pci_enable(b,d,f);
    kapi_outb(io+R_CMD,0x10);
    int ok=0; for(int t=0;t<100000;t++){ if(!(kapi_inb(io+R_CMD)&0x10)){ ok=1; break; } kapi_delay_us(10); }
    if(!ok){ kapi_log("rtl8169: reset timeout"); return 0; }
    rx=(struct desc*)kapi_dma_pages(1); tx=(struct desc*)kapi_dma_pages(1);
    rxb=(u8*)kapi_dma_pages(NRX*BUFSZ/4096); txb=(u8*)kapi_dma_pages(NTX*BUFSZ/4096);
    if(!rx||!tx||!rxb||!txb)return 0;
    for(int i=0;i<NRX;i++){ rx[i].opts2=0; rx[i].addr=kapi_phys(rxb+i*BUFSZ); rx[i].opts1=OWN|(i==NRX-1?EOR:0)|BUFSZ; }
    for(int i=0;i<NTX;i++){ tx[i].opts1=(i==NTX-1?EOR:0); tx[i].opts2=0; tx[i].addr=0; }
    kapi_mb();
    kapi_outb(io+R_9346,0xC0);
    kapi_outw(io+R_IMR,0);
    kapi_outw(io+R_ISR,0xFFFF);
    o32(R_RCR,0x0000E70Fu);
    kapi_outb(io+R_CMD,0x04);
    o32(R_TCR,0x03000700u);
    kapi_outw(io+R_RMS,BUFSZ-1);
    kapi_outb(io+R_MTPS,0x3B);
    o32(R_TNPDS,(u32)kapi_phys(tx)); o32(R_TNPDS+4,(u32)(kapi_phys(tx)>>32));
    o32(R_RDSAR,(u32)kapi_phys(rx)); o32(R_RDSAR+4,(u32)(kapi_phys(rx)>>32));
    kapi_outb(io+R_CMD,0x0C);
    kapi_outb(io+R_9346,0x00);
    rxi=0; txi=0;
    for(int i=0;i<6;i++)out->mac[i]=kapi_inb(io+R_IDR+i);
    out->name="Realtek RTL8111/8168/8169"; out->send=r69_send; out->recv=r69_recv; out->link=r69_link;
    kapi_log("rtl8169: up");
    return 1;
}
