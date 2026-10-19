#include "include/kapi.h"
#include "include/drivers.h"
#include "drivers/virtio/virtio.h"
#define MAXSEC 128
static u16 io; static struct vq q; static u8* bounce; static u8* hdr; static int ready, has_flush;
static u64 capacity;
static int vb_req(u32 type,u64 lba,int count,int towrite){
    if(!ready)return -1;
    volatile u32* h=(volatile u32*)hdr; h[0]=type; h[1]=0; *(volatile u64*)(hdr+8)=lba;
    volatile u8* st=hdr+16; *st=0xFF;
    u64 a[3]; u32 l[3]; u16 f[3]; int n=0;
    a[n]=kapi_phys(hdr); l[n]=16; f[n]=0; n++;
    if(count>0){ a[n]=kapi_phys(bounce); l[n]=(u32)count*512u; f[n]=towrite?0:VIRTQ_F_WRITE; n++; }
    a[n]=kapi_phys(hdr+16); l[n]=1; f[n]=VIRTQ_F_WRITE; n++;
    if(vq_add(&q,a,l,f,n)<0)return -1;
    vq_kick(&q);
    u64 t0=kapi_ns();
    for(;;){ u32 id,len; int r=vq_used(&q,&id,&len); if(r>0)break; if(r<0)return -1; if(kapi_ns()-t0>5000000000ULL){ kapi_log("virtio-blk: request timeout"); ready=0; return -1; } }
    return *st==0?0:-1;
}
int virtio_blk_read(u64 lba,int count,u8* buf){
    while(count>0){ int c=count>MAXSEC?MAXSEC:count; if(lba+(u64)c>capacity)return -1;
        if(vb_req(0,lba,c,0))return -1; for(int i=0;i<c*512;i++)buf[i]=bounce[i]; buf+=c*512; lba+=(u64)c; count-=c; }
    return 0;
}
int virtio_blk_write(u64 lba,int count,const u8* buf){
    while(count>0){ int c=count>MAXSEC?MAXSEC:count; if(lba+(u64)c>capacity)return -1;
        for(int i=0;i<c*512;i++)bounce[i]=buf[i]; if(vb_req(1,lba,c,1))return -1; buf+=c*512; lba+=(u64)c; count-=c; }
    return 0;
}
int virtio_blk_flush(void){ if(!has_flush)return 0; return vb_req(4,0,0,0); }
int virtio_blk_probe(u64* sectors){
    int b,d,f; if(!vio_open(0x1001,0,&io,&b,&d,&f))return 0;
    u32 feat=vio_begin(io);
    u32 want=feat&(1u<<9); vio_features(io,want); has_flush=want!=0;
    if(!vq_setup(io,0,&q,0)){ vio_fail(io); return 0; }
    bounce=(u8*)kapi_dma_pages(MAXSEC*512/4096); hdr=(u8*)kapi_dma_pages(1);
    if(!bounce||!hdr){ vio_fail(io); return 0; }
    capacity=(u64)kapi_inl(io+0x14)|((u64)kapi_inl(io+0x18)<<32);
    vio_ready(io); ready=1;
    *sectors=capacity;
    kapi_log("virtio-blk: up");
    return capacity>0;
}
