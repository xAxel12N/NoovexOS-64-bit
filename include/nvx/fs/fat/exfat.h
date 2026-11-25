#ifndef NVX_FS_EXFAT_H
#define NVX_FS_EXFAT_H
#include "include/nvx/base.h"
int xf_mount(nvxblk_rd rd,nvxblk_wr wr,unsigned long long base,unsigned long long nsect,int ro);
int xf_umount(void);
int xf_mounted(void);
int xf_ro(void);
int xf_unclean(void);
int xf_repaired(void);
int xf_mark(int dirty);
int xf_ls(const char* path,nvxntfs_lscb cb,void* ctx);
long xf_size(const char* path);
long xf_read(const char* path,char* buf,long cap);
long xf_write(const char* path,const char* buf,long len);
int xf_mkdir(const char* path);
int xf_delete(const char* path);
int xf_rename(const char* from,const char* to);
unsigned long long xf_free_bytes(void);
#endif
