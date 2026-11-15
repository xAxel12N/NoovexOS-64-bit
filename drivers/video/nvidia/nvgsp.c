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

#include "drivers/video/nvidia/gsp/firmware_unpack_and_memory.c"
#include "drivers/video/nvidia/gsp/boot.c"
#include "drivers/video/nvidia/gsp/framebuffer_present.c"
#include "drivers/video/nvidia/gsp/tick_on_off.c"
#include "drivers/video/nvidia/gsp/display_modes.c"
#include "drivers/video/nvidia/gsp/accel_3d.c"
