#ifndef NVX_FS_VFAT_H
#define NVX_FS_VFAT_H

#include "include/nvx/base.h"

struct nvx_g_vfat_t { nvxblk_rd rd; nvxblk_wr wr; unsigned long long base, nsect; int ro, mounted, type;
    u32 spc, rsvd, nfats, fatsz, rootent, rootsecs, root_lba, data_lba, clusters, rootclus, fsinfo, cbytes, next_free, free_cnt;
    u8* fat; u8* fdirty; u8* cbuf; char label[12]; };

extern struct nvx_g_vfat_t g_vfat;

int vf_mounted(void);
void vf_umount(void);
int vf_mount(nvxblk_rd rd,nvxblk_wr wr,unsigned long long base,unsigned long long nsect,int ro);
int vf_exists(const char* path);
long vf_read(const char* path,char* buf,long cap);
long vf_write(const char* path,const char* buf,long len);
int vf_mkdir(const char* path);
int vf_delete(const char* path);
int vf_ls(const char* path,nvxntfs_lscb cb,void* ctx);
int vf_format(nvxblk_rd rd,nvxblk_wr wr,unsigned long long base,unsigned long long nsect,const char* label,int type);

#endif
