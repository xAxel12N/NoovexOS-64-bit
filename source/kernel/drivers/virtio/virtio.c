#include "include/kapi.h"
#include "drivers/virtio/virtio.h"
int vio_open(u16 devid,int nth,u16* io,int* bus,int* dev,int* fn){
    u16 ids[2]={devid,0};
    if(!kapi_pci_find(0x1AF4,ids,nth,bus,dev,fn))return 0;
    u32 bar=kapi_pci_rd32(*bus,*dev,*fn,0x10); if(!(bar&1))return 0;
    *io=(u16)(bar&0xFFFC); if(!*io)return 0;
    kapi_pci_enable(*bus,*dev,*fn);
    return 1;
}
u32 vio_begin(u16 io){
    kapi_outb(io+0x12,0);
    for(int t=0;t<1000&&kapi_inb(io+0x12);t++)kapi_delay_us(10);
    kapi_outb(io+0x12,1); kapi_outb(io+0x12,1|2);
    return kapi_inl(io+0x00);
}
void vio_features(u16 io,u32 f){ kapi_outl(io+0x04,f); }
void vio_ready(u16 io){ kapi_outb(io+0x12,1|2|4); }
void vio_fail(u16 io){ kapi_outb(io+0x12,0x80); }
static unsigned long pages_for(unsigned long b){ return (b+4095)/4096; }
int vq_setup(u16 io,int idx,struct vq* q,int maxn){
    kapi_outw(io+0x0E,(u16)idx);
    int n=kapi_inw(io+0x0C); if(n<=0||n>32768||(n&(n-1)))return 0;
    (void)maxn;
    unsigned long a=((unsigned long)16*n+6+2*n+4095)&~4095UL;
    unsigned long total=a+pages_for(6+8*(unsigned long)n)*4096;
    u8* m=(u8*)kapi_dma_pages(pages_for(total)); if(!m)return 0;
    if(kapi_phys(m)>>44)return 0;
    q->io=io; q->idx=(u16)idx; q->n=(u16)n; q->last_used=0;
    q->desc=(struct vdesc*)m; q->avail=(volatile u16*)(m+16*n); q->used=m+a;
    for(int i=0;i<n;i++){ q->desc[i].next=(u16)(i+1); q->desc[i].flags=0; }
    q->free_head=0; q->nfree=(u16)n;
    q->avail[0]=1; q->avail[1]=0;
    kapi_mb();
    kapi_outl(io+0x08,(u32)(kapi_phys(m)>>12));
    return n;
}
int vq_add(struct vq* q,const u64* addr,const u32* len,const u16* flags,int count){
    if(count<=0||q->nfree<count)return -1;
    int head=q->free_head, cur=head, prev=-1;
    for(int i=0;i<count;i++){
        struct vdesc* d=&q->desc[cur];
        d->addr=addr[i]; d->len=len[i]; d->flags=(u16)(flags[i]|(i+1<count?VIRTQ_F_NEXT:0));
        prev=cur; cur=d->next; }
    (void)prev;
    q->free_head=(u16)cur; q->nfree=(u16)(q->nfree-count);
    u16 ai=q->avail[1];
    q->avail[2+(ai%q->n)]=(u16)head;
    kapi_mb();
    q->avail[1]=(u16)(ai+1);
    kapi_mb();
    return head;
}
void vq_kick(struct vq* q){ kapi_mb(); kapi_outw(q->io+0x10,q->idx); }
int vq_used(struct vq* q,u32* id,u32* len){
    volatile u16* uh=(volatile u16*)q->used;
    kapi_mb();
    if(uh[1]==q->last_used)return 0;
    volatile u32* e=(volatile u32*)(q->used+4+8*(q->last_used%q->n));
    u32 i=e[0], l=e[1];
    q->last_used++;
    if(i>=q->n)return -1;
    int c=(int)i, cnt=1;
    while((q->desc[c].flags&VIRTQ_F_NEXT)&&cnt<q->n){ c=q->desc[c].next; cnt++; }
    q->desc[c].next=q->free_head; q->free_head=(u16)i; q->nfree=(u16)(q->nfree+cnt);
    *id=i; *len=l;
    return 1;
}
