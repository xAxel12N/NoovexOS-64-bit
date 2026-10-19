#ifndef NVX_FS_NTFSK_H
#define NVX_FS_NTFSK_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/rtc/cmos.h"

#define vf_clock read_clock
typedef struct { const char* name; int (*mount)(nvxblk_rd,nvxblk_wr,unsigned long long,unsigned long long,int); void (*umount)(void);
    int (*exists)(const char*); int (*mkdir)(const char*); long (*write)(const char*,const char*,long); long (*read)(const char*,char*,long);
    int (*del)(const char*); int (*ls)(const char*,nvxntfs_lscb,void*); int (*format)(nvxblk_rd,nvxblk_wr,unsigned long long,unsigned long long,const char*); } rfs_ops;

extern const rfs_ops g_rfs_ntfs;
extern const rfs_ops g_rfs_fat32;
extern const rfs_ops g_rfs_fat16;
extern const rfs_ops* g_rfs;

int nk_mount(int ro);
void nk_lscb(const char* name,int isdir,unsigned long long size,void* ctx);
int nk_root_sync_file(int k);
void nk_root_flush(void);
void nk_root_dt_save(void);
int nk_root_read(int k,char* buf,long len);
int nk_root_try(void);
int nk_install(const rfs_ops* o,unsigned long long base,unsigned long long nsect,int copy,void (*prog)(int done,int total));

#endif
