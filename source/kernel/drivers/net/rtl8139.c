#include "include/kapi.h"
#include "include/drivers.h"
#define R_IDR 0x00
#define R_TSD 0x10
#define R_TSAD 0x20
#define R_RBSTART 0x30
#define R_CR 0x37
#define R_CAPR 0x38
#define R_IMR 0x3C
#define R_ISR 0x3E
#define R_TCR 0x40
#define R_RCR 0x44
#define R_CFG1 0x52
#define R_MSR 0x58
#define RX_LEN 8192
static u16 io;
static u8* rxbuf;
static u8* txbuf[4];
static int txi, rxoff;
static int r39_send(const u8* d,int len){
    if(len>1792)return -1;
    for(int t=0;t<100000;t++){ u32 s=kapi_inl(io+R_TSD+txi*4); if((s&(1u<<13))||!(s&0x1FFF))break; kapi_delay_us(1); }
    u8* b=txbuf[txi]; for(int i=0;i<len;i++)b[i]=d[i]; for(int i=len;i<60;i++)b[i]=0;
    if(len<60)len=60;
    kapi_mb();
    kapi_outl(io+R_TSAD+txi*4,(u32)kapi_phys(b));
    kapi_outl(io+R_TSD+txi*4,(u32)len);
    txi=(txi+1)&3;
    return len;
}
static int r39_recv(u8* out,int max){
    if(kapi_inb(io+R_CR)&1)return 0;
    u8* p=rxbuf+rxoff;
    u16 st=(u16)(p[0]|(p[1]<<8)); u16 len=(u16)(p[2]|(p[3]<<8));
    if(!(st&1)||len<4||len>1792+4){
        kapi_outb(io+R_CR,0x04); kapi_outb(io+R_CR,0x0C); rxoff=0; kapi_outw(io+R_CAPR,(u16)(0-16)); return 0; }
    int n=len-4; if(n>max)n=max;
    for(int i=0;i<n;i++)out[i]=p[4+i];
    rxoff=(rxoff+len+4+3)&~3; rxoff%=RX_LEN;
    kapi_outw(io+R_CAPR,(u16)(rxoff-16));
    kapi_outw(io+R_ISR,0x0015);
    return n;
}
static int r39_link(void){ return !(kapi_inb(io+R_MSR)&0x04); }
int rtl8139_probe(struct nvx_nic* out){
    static const u16 ids[]={0x8139,0};
    int b,d,f; if(!kapi_pci_find(0x10EC,ids,0,&b,&d,&f))return 0;
    u32 bar=kapi_pci_rd32(b,d,f,0x10); if(!(bar&1))return 0;
    io=(u16)(bar&0xFFFC); if(!io)return 0;
    kapi_pci_enable(b,d,f);
    kapi_outb(io+R_CFG1,0);
    kapi_outb(io+R_CR,0x10);
    int ok=0; for(int t=0;t<100000;t++){ if(!(kapi_inb(io+R_CR)&0x10)){ ok=1; break; } kapi_delay_us(10); }
    if(!ok){ kapi_log("rtl8139: reset timeout"); return 0; }
    rxbuf=(u8*)kapi_dma_pages(4); if(!rxbuf)return 0;
    for(int i=0;i<4;i++){ txbuf[i]=(u8*)kapi_dma_pages(1); if(!txbuf[i])return 0; }
    if(kapi_phys(rxbuf)>=0xFFFFF000ULL)return 0;
    kapi_outl(io+R_RBSTART,(u32)kapi_phys(rxbuf));
    kapi_outw(io+R_IMR,0);
    kapi_outw(io+R_ISR,0xFFFF);
    kapi_outl(io+R_RCR,0x0000E78Fu);
    kapi_outl(io+R_TCR,0x03000600u);
    kapi_outb(io+R_CR,0x0C);
    txi=0; rxoff=0;
    for(int i=0;i<6;i++)out->mac[i]=kapi_inb(io+R_IDR+i);
    out->name="Realtek RTL8139"; out->send=r39_send; out->recv=r39_recv; out->link=r39_link;
    kapi_log("rtl8139: up");
    return 1;
}
