#include "include/kapi.h"
#include "include/drivers.h"
#define NRX 32
#define NTX 16
#define BUF 2048
struct rxd { volatile u64 addr; volatile u16 len, csum; volatile u8 status, errors; volatile u16 special; };
struct txd { volatile u64 addr; volatile u16 len; volatile u8 cso, cmd, status, css; volatile u16 special; };
static volatile u32* M; static struct rxd* rx; static struct txd* tx; static u8* rxb; static u8* txb; static int rxi, txi;
static void w(u32 r,u32 v){ M[r>>2]=v; } static u32 r(u32 reg){ return M[reg>>2]; }
static int e1k_send(const u8* d,int len){
    if(len>BUF)return -1;
    struct txd* t=&tx[txi];
    for(int k=0;k<100000&&!(t->status&1);k++)kapi_delay_us(1);
    if(!(t->status&1))return -1;
    u8* b=txb+txi*BUF; for(int i=0;i<len;i++)b[i]=d[i]; for(int i=len;i<60;i++)b[i]=0; if(len<60)len=60;
    t->addr=kapi_phys(b); t->len=(u16)len; t->cso=0; t->css=0; t->special=0; t->status=0; t->cmd=0x0B;
    txi=(txi+1)%NTX;
    kapi_mb(); w(0x3818,(u32)txi); kapi_mb();
    return len;
}
static int e1k_recv(u8* out,int max){
    struct rxd* x=&rx[rxi];
    if(!(x->status&1))return 0;
    int n=0; if((x->status&2)&&!x->errors){ n=x->len; if(n>max)n=max; const u8* b=rxb+rxi*BUF; for(int i=0;i<n;i++)out[i]=b[i]; }
    x->status=0; x->addr=kapi_phys(rxb+rxi*BUF);
    kapi_mb(); w(0x2818,(u32)rxi);
    rxi=(rxi+1)%NRX;
    return n;
}
static int e1k_link(void){ return (r(0x0008)&2)!=0; }
int e1000_probe(struct nvx_nic* out){
    static const u16 ids[]={0x100E,0x100F,0x1004,0x1010,0x1015,0x1017,0x1019,0x101A,0x1026,0x1027,0x1028,0x1013,0x1014,0x1076,0x1077,0x1078,0x1079,0x107A,0x107B,0x10D3,0};
    int b,d,f; if(!kapi_pci_find(0x8086,ids,0,&b,&d,&f))return 0;
    u32 lo=kapi_pci_rd32(b,d,f,0x10); if(lo&1)return 0;
    u64 bar=lo&0xFFFFFFF0u; if(((lo>>1)&3)==2)bar|=(u64)kapi_pci_rd32(b,d,f,0x14)<<32;
    if(!bar||bar>=0x100000000ULL){ kapi_log("e1000: BAR not usable"); return 0; }
    kapi_pci_enable(b,d,f);
    M=(volatile u32*)(u64)bar;
    w(0x00D8,0xFFFFFFFFu); (void)r(0x00C0);
    w(0x0000,r(0x0000)|0x04000000u);
    kapi_delay_us(10000);
    for(int t=0;t<1000&&(r(0x0000)&0x04000000u);t++)kapi_delay_us(10);
    w(0x00D8,0xFFFFFFFFu); (void)r(0x00C0);
    w(0x0000,(r(0x0000)|0x60u)&~0x88080000u);
    u32 ral=r(0x5400), rah=r(0x5404);
    for(int i=0;i<4;i++)out->mac[i]=(u8)(ral>>(8*i)); out->mac[4]=(u8)rah; out->mac[5]=(u8)(rah>>8);
    if(!(rah&0x80000000u)||((ral|rah)&0xFFFFFF)==0){
        for(int k=0;k<3;k++){ w(0x0014,((u32)k<<8)|1u); u32 v=0; for(int t=0;t<10000;t++){ v=r(0x0014); if(v&0x10)break; kapi_delay_us(1); } out->mac[k*2]=(u8)(v>>16); out->mac[k*2+1]=(u8)(v>>24); } }
    w(0x5400,(u32)out->mac[0]|((u32)out->mac[1]<<8)|((u32)out->mac[2]<<16)|((u32)out->mac[3]<<24));
    w(0x5404,(u32)out->mac[4]|((u32)out->mac[5]<<8)|0x80000000u);
    for(int i=0;i<128;i++)w(0x5200+(u32)i*4,0);
    rx=(struct rxd*)kapi_dma_pages(1); tx=(struct txd*)kapi_dma_pages(1); rxb=(u8*)kapi_dma_pages(NRX*BUF/4096); txb=(u8*)kapi_dma_pages(NTX*BUF/4096);
    if(!rx||!tx||!rxb||!txb)return 0;
    for(int i=0;i<NRX;i++){ rx[i].addr=kapi_phys(rxb+i*BUF); rx[i].status=0; }
    for(int i=0;i<NTX;i++){ tx[i].addr=0; tx[i].cmd=0; tx[i].status=1; }
    kapi_mb();
    w(0x2800,(u32)kapi_phys(rx)); w(0x2804,(u32)(kapi_phys(rx)>>32)); w(0x2808,NRX*16); w(0x2810,0); w(0x2818,NRX-1);
    w(0x0100,0x02u|0x08u|0x10u|0x8000u|0x04000000u);
    w(0x3800,(u32)kapi_phys(tx)); w(0x3804,(u32)(kapi_phys(tx)>>32)); w(0x3808,NTX*16); w(0x3810,0); w(0x3818,0);
    w(0x0410,0x0060200Au);
    w(0x0400,0x02u|0x08u|(0x0Fu<<4)|(0x40u<<12));
    rxi=0; txi=0;
    out->name="Intel e1000 (8254x)"; out->send=e1k_send; out->recv=e1k_recv; out->link=e1k_link;
    kapi_log("e1000: up");
    return 1;
}
