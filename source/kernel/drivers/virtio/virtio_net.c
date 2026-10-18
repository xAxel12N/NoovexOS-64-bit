#include "include/kapi.h"
#include "include/drivers.h"
#include "drivers/virtio/virtio.h"
#define HDR 10
#define BUF 2048
#define NRX 64
#define NTX 32
static u16 io; static struct vq rq, tq; static u8* rxb; static u8* txb; static int txn, have_status;
static int rxid[1024];
static void post_rx(int slot){
    u64 a=kapi_phys(rxb+slot*BUF); u32 l=BUF; u16 f=VIRTQ_F_WRITE;
    int h=vq_add(&rq,&a,&l,&f,1); if(h>=0&&h<1024)rxid[h]=slot;
}
static void reclaim_tx(void){ u32 id,len; while(vq_used(&tq,&id,&len)>0){} }
static int vn_send(const u8* d,int len){
    if(len>BUF-HDR)return -1;
    reclaim_tx();
    for(int t=0;t<10000&&(tq.nfree<1||tq.n-tq.nfree>=NTX);t++){ kapi_delay_us(10); reclaim_tx(); }
    if(tq.nfree<1||tq.n-tq.nfree>=NTX)return -1;
    u8* b=txb+(txn%NTX)*BUF; txn++;
    for(int i=0;i<HDR;i++)b[i]=0; for(int i=0;i<len;i++)b[HDR+i]=d[i];
    u64 a=kapi_phys(b); u32 l=(u32)(HDR+len); u16 f=0;
    if(vq_add(&tq,&a,&l,&f,1)<0)return -1;
    vq_kick(&tq);
    return len;
}
static int vn_recv(u8* out,int max){
    u32 id,len; if(vq_used(&rq,&id,&len)<=0)return 0;
    int slot=(id<1024)?rxid[id]:0;
    int n=(int)len-HDR; if(n<0)n=0; if(n>max)n=max;
    const u8* b=rxb+slot*BUF+HDR; for(int i=0;i<n;i++)out[i]=b[i];
    post_rx(slot); vq_kick(&rq);
    return n;
}
static int vn_link(void){ if(!have_status)return 1; return (kapi_inw(io+0x14+6)&1)!=0; }
int virtio_net_probe(struct nvx_nic* out){
    int b,d,f; if(!vio_open(0x1000,0,&io,&b,&d,&f))return 0;
    u32 feat=vio_begin(io);
    u32 want=(feat&(1u<<5))|(feat&(1u<<16));
    vio_features(io,want); have_status=(want>>16)&1;
    if(!vq_setup(io,0,&rq,NRX)||!vq_setup(io,1,&tq,NTX)){ vio_fail(io); kapi_log("virtio-net: queue setup failed"); return 0; }
    rxb=(u8*)kapi_dma_pages(NRX*BUF/4096); txb=(u8*)kapi_dma_pages(NTX*BUF/4096);
    if(!rxb||!txb){ vio_fail(io); return 0; }
    int nrx=rq.n<NRX?rq.n:NRX;
    for(int i=0;i<nrx;i++)post_rx(i);
    vio_ready(io);
    vq_kick(&rq);
    for(int i=0;i<6;i++)out->mac[i]=(want&(1u<<5))?kapi_inb(io+0x14+i):(u8)(i==0?0x52:i==1?0x54:i+0x10);
    txn=0;
    out->name="VirtIO network"; out->send=vn_send; out->recv=vn_recv; out->link=vn_link;
    kapi_log("virtio-net: up");
    return 1;
}
