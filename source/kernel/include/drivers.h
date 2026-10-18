#ifndef NVX_DRIVERS_H
#define NVX_DRIVERS_H
struct nvx_nic {
    const char* name;
    u8 mac[6];
    int (*send)(const u8* data,int len);
    int (*recv)(u8* out,int max);
    int (*link)(void);
};
int e1000_probe(struct nvx_nic* out);
int rtl8139_probe(struct nvx_nic* out);
int rtl8169_probe(struct nvx_nic* out);
int virtio_net_probe(struct nvx_nic* out);
int virtio_blk_probe(u64* sectors);
int virtio_blk_read(u64 lba,int count,u8* buf);
int virtio_blk_write(u64 lba,int count,const u8* buf);
int virtio_blk_flush(void);
struct nvx_snd {
    const char* name;
    int rate;
    int (*start)(void);
    int (*free_bytes)(void);
    int (*write)(const u8* pcm,int len);
    void (*poll)(void);
    void (*stop)(void);
    void (*volume)(int percent);
    int (*rec_start)(void);
    int (*rec_read)(u8* pcm,int len);
    void (*rec_stop)(void);
    void (*rec_gain)(int percent);
    const char* rec_name;
};
int ac97_probe(struct nvx_snd* out);
int hda_probe(struct nvx_snd* out);
const char* hda_info(void);
#endif
