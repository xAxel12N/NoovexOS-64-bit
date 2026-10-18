#ifndef NVX_FS_VOLUMES_H
#define NVX_FS_VOLUMES_H

#include "include/nvx/base.h"

#define VOL_MAX 24
enum { VOL_SYS=0, VOL_FIXED=1, VOL_USB=2 };
typedef struct { char letter; int kind, disk, part; u32 start; u64 total, freeb; int has_free; char label[36]; char fs[12]; char dev[28]; } nvol;

extern nvol g_vol[VOL_MAX]; extern int g_vol_n; extern u32 g_vol_scans; extern u32 g_vol_ms;
void vol_scan(int force);
void vol_name(const nvol* v,char* out,int cap);

#endif
