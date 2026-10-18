#ifndef NVX_FS_SYSROOT_H
#define NVX_FS_SYSROOT_H
#include "include/nvx/base.h"
void sysroot_init(void);
const u8* sysroot_get(const char* path,u32* len,int* exec);
int sysroot_isdir(const char* path);
int sysroot_exists(const char* path);
int sysroot_islink(const char* path);
int sysroot_readlink(const char* path,char* out,int cap);
int sysroot_list(const char* dir,int idx,char* name,int cap,int* isdir);
void sysroot_mkdirs(void);
int sysroot_add_layer(const char* tag,const u8* blob,u32 size,int owned);
int sysroot_del_layer(const char* tag);
int sysroot_has_layer(const char* tag);
int sysroot_layer_files(const char* tag,int idx,char* name,int cap);
int sysroot_stats(int* files,int* layers);
#endif
