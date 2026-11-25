#ifndef NVX_DRIVERS_VIDEO_FB_GTEXT_H
#define NVX_DRIVERS_VIDEO_FB_GTEXT_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/video/nvidia/nv2dq.h"

#define GT_MAXN 256
#define GT_LAYOPS 1024

typedef struct { short x, y; char ch; u8 mode, s; u32 c; } gly;
typedef struct { int sid; u32* px; u32 pitch; u32 ep; int ops, reset; nv2qa a; } gtatl;

extern int g_gfx_atlas;
extern u32 g_gt_runs, g_gt_glyphs, g_gt_fall, g_gt_resets;

u32 gt_stage(u32* stm,u32 mp,u32* stc,u32 cp,int sx,int sy,int w,int h,const gly* g,int n,int x0,int y0,int mono);
void gt_init(gtatl* A,int sid,u32* px,u32 pitch);
void gt_frame(gtatl* A);
int gt_run(gtatl* A,int t,const gly* g,int n);

#endif
