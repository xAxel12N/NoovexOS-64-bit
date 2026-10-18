#ifndef NVX_DRIVERS_VIDEO_GPUKERN_H
#define NVX_DRIVERS_VIDEO_GPUKERN_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/video/gen9asm.h"

#define GK_FILL  0
#define GK_BLEND 1
#define GK_MASK  2
#define GK_COPY  3
#define GK_COPYM 4
#define GK_GRADH 5
#define GK_GRADV 6
#define GK_TINT  7
#define GK_BLURH 8
#define GK_BLURV 9
#define GK_OVER  10
#define GK_OVERM 11
#define GK_N     12

#define GC_BX 0
#define GC_BY 1
#define GC_NX 2
#define GC_NY 3
#define GC_NW 4
#define GC_NH 5
#define GC_C0 6
#define GC_A  7
#define GC_SDX 8
#define GC_SDY 9
#define GC_C1 10
#define GC_INV 11
#define GC_KEY 12
#define GC_MDX 13
#define GC_MDY 14
#define GC_GOFF 15

void gk_build(int id,g9asm* a);
const char* gk_name(int id);

#endif
