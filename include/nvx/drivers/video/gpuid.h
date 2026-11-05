#ifndef NVX_DRIVERS_VIDEO_GPUID_H
#define NVX_DRIVERS_VIDEO_GPUID_H

#include "include/nvx/base.h"

typedef struct { u16 ven, dev; u8 bus, slot; const char* vendor; const char* name; const char* driver; int accel; } gpu_t;

int gpu_list(gpu_t* out,int max);
const char* gpu_name(u16 ven,u16 dev);

#endif
