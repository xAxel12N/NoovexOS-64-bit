#ifndef NVX_FS_NVX4_H
#define NVX_FS_NVX4_H

#include "include/nvx/base.h"

extern int g_n4;
extern const char* g_n4_err;
extern int g_n4_nres;

int n4_mount(u32 secs);
void n4_reserve_secs(u32 sec,u32 n);
int n4_format(u32 secs);
int n4_conv_backup(u32 secs);
void n4_conv_done(u32 secs);
int n4_conv_restore(u32 secs);
void n4_usage(u64* total,u64* freeb);
int fs_backend_load(int k);
int fs_reloadable(int k);
int nvx3_sync(int k);
void nvx3_flush_meta(void);
void nvx3_sync_all(void);
int fs_sync(int k);

#endif
