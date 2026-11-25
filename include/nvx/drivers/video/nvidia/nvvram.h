#ifndef NVX_DRIVERS_VIDEO_NVIDIA_NVVRAM_H
#define NVX_DRIVERS_VIDEO_NVIDIA_NVVRAM_H

#include "include/nvx/base.h"

extern int g_vl_on;
extern u64 g_vl_budget, g_vl_bytes;
extern u32 g_vl_up_tiles, g_vl_up_rects, g_vl_evicts, g_vl_hits, g_vl_miss, g_vl_fail, g_vl_n;
u64 nvl_src(int s);
void nvl_mark(int s,int x,int y,int w,int h,int gpu);
void nvl_cpu_mark(int s,int x,int y,int w,int h);
void nvl_batch_end(int ok);
void nvl_free(int s);
void nvl_drop_all(int give);
int nvl_dirty_tiles(int s);

#endif
