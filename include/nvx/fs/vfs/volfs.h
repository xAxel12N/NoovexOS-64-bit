#ifndef NVX_FS_VOLFS_H
#define NVX_FS_VOLFS_H
#include "include/nvx/base.h"
enum { VOLFS_NONE=0, VOLFS_EXT4=1, VOLFS_FAT=2, VOLFS_EXFAT=3 };
enum { VOLFS_OK=0, VOLFS_E_ARG=-1, VOLFS_E_MOUNT=-2, VOLFS_E_FS=-3, VOLFS_E_RO=-4, VOLFS_E_OFF=-5, VOLFS_E_IO=-6, VOLFS_E_BUSY=-7 };
typedef void (*volfs_lscb)(const char* name,int isdir,unsigned long long size,void* ctx);
int volfs_open(int vi);
void volfs_close(void);
int volfs_kind(void);
int volfs_vol(void);
int volfs_ro(void);
int volfs_unclean(void);
int volfs_esp(void);
int volfs_usb_yield(void);
int volfs_usb_claim(int on);
int volfs_usb_owned(void);
int volfs_ls(const char* path,volfs_lscb cb,void* ctx);
long volfs_size(const char* path);
long volfs_read(const char* path,char* buf,long cap);
long volfs_write(const char* path,const char* buf,long len);
int volfs_mkdir(const char* path);
int volfs_delete(const char* path);
int volfs_rename(const char* from,const char* to);
int volfs_eject(int vi);
int volfs_join(char* out,int cap,const char* dir,const char* name);
const char* volfs_why(int err);
#endif
