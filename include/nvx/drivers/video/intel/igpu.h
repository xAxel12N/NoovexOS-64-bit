#ifndef NVX_DRIVERS_VIDEO_IGPU_H
#define NVX_DRIVERS_VIDEO_IGPU_H

#include "include/nvx/base.h"

extern int g_fb_wc, g_igpu_flip; extern u8* g_igpu_front; extern u8* g_igpu_back;
void pat_cpu_init(void);
void igpu_early(void);
void igpu_init(void);
void igpu_pre_blit(void);
void igpu_post_blit(void);
void igpu_wait_flip(void);
void igpu_flip_off(const char* why);
int igpu_flip_on(void);
void gpuinfo_run(void);

#endif
