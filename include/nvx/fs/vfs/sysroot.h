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
u32 sysroot_crc(const u8* p,u32 n);
u32 sysroot_want_crc(void);
u32 sysroot_want_size(void);
void sysroot_file_name(u32 crc,char* out);
int sysroot_base_ok(void);
const u8* sysroot_base_blob(u32* size);
int sysroot_set_base(const u8* blob,u32 size,int owned);
extern int g_sr_src;
#define SR_SLOT 0x20000000ULL
#define SR_SLOTMAX 0x10000000u
u64 sysroot_slot_end(void);
u64 sysroot_slot_base(void);
u32 sysroot_ino(const char* path,int follow);
const u8* sysroot_layer_blob(const char* tag,u32* size);
#endif
