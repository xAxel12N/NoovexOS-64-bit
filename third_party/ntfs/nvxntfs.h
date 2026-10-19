#ifndef NVXNTFS_H
#define NVXNTFS_H
typedef int (*nvxblk_rd)(unsigned long long lba,int n,void* buf);
typedef int (*nvxblk_wr)(unsigned long long lba,int n,const void* buf);
typedef void (*nvxntfs_lscb)(const char* name,int isdir,unsigned long long size,void* ctx);
int  nvxntfs_mount(nvxblk_rd rd,nvxblk_wr wr,unsigned long long base_lba,unsigned long long nsect,int readonly);
int  nvxntfs_mounted(void);
void nvxntfs_umount(void);
int  nvxntfs_ls(const char* path,nvxntfs_lscb cb,void* ctx);
long nvxntfs_read(const char* path,char* buf,long cap);
long nvxntfs_write(const char* path,const char* buf,long len);
int  nvxntfs_rename(const char* oldpath,const char* newpath);
int  nvxntfs_mkdir(const char* path);
int  nvxntfs_exists(const char* path);
int  nvxntfs_delete(const char* path);
const char* nvxntfs_label(void);
int  nvxntfs_format(nvxblk_rd rd,nvxblk_wr wr,unsigned long long base_lba,unsigned long long nsect,const char* label);
void nvxntfs_set_logger(void (*fn)(const char*));
#endif
