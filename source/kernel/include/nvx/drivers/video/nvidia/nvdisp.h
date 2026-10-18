#ifndef NVX_DRIVERS_VIDEO_NVIDIA_NVDISP_H
#define NVX_DRIVERS_VIDEO_NVIDIA_NVDISP_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/video/nvidia/nvgpu.h"

extern u8 g_edid[128];
extern int g_edid_ok;
extern nv_mode g_mode;
extern int g_mode_ok;
extern char g_edid_name[16];

void nvw32(u32 off,u32 v);
void nv_udelay(u32 us);
int nv_display_probe(void);

#endif
