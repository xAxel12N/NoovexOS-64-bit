#ifndef NVX_DRIVERS_VIDEO_FB_DAMAGE_H
#define NVX_DRIVERS_VIDEO_FB_DAMAGE_H

#include "include/nvx/base.h"

#define DMG_TW 64
#define DMG_TH 16
#define DMG_MAX 256

typedef struct { int x, y, w, h; } dmg_rect;

int dmg_rects(const u32* now,const u32* prev,u32 stride,int w,int h,dmg_rect* out,int max);
u64 dmg_area(const dmg_rect* r,int n);

#endif
