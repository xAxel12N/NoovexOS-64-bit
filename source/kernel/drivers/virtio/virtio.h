#ifndef NVX_VIRTIO_H
#define NVX_VIRTIO_H
#define VIRTQ_F_NEXT 1
#define VIRTQ_F_WRITE 2
struct vdesc { u64 addr; u32 len; u16 flags; u16 next; };
struct vq {
    u16 io, idx, n, last_used, free_head, nfree;
    struct vdesc* desc;
    volatile u16* avail;
    volatile u8* used;
};
int vio_open(u16 vendor_dev,int nth,u16* io,int* bus,int* dev,int* fn);
u32 vio_begin(u16 io);
void vio_features(u16 io,u32 f);
void vio_ready(u16 io);
void vio_fail(u16 io);
int vq_setup(u16 io,int idx,struct vq* q,int maxn);
int vq_add(struct vq* q,const u64* addr,const u32* len,const u16* flags,int count);
void vq_kick(struct vq* q);
int vq_used(struct vq* q,u32* id,u32* len);
#endif
