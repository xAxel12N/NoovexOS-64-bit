#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/drivers/video/nvidia/nvgpu.h"
#include "include/nvx/drivers/video/nvidia/nvgsp.h"
#include "include/nvx/drivers/video/fb/damage.h"

#ifndef NV_BOOTINFO
#define NV_BOOTINFO 0x5000ULL
#endif

#include "drivers/video/nvidia/nvgsp_parts/01_xzk_decode_into.c"
#include "drivers/video/nvidia/nvgsp_parts/02_boot_gsp.c"
#include "drivers/video/nvidia/nvgsp_parts/03_nvgsp_fb_present.c"
#include "drivers/video/nvidia/nvgsp_parts/04_nvgsp_tick.c"
