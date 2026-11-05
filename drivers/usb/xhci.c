#include "include/nvx/base.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/mem/dmawin.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/lib/js/js.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/drivers/input/ps2/ps2.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/drivers/input/input.h"
/* ---- xHCI USB controller: stage 1 (init + port detection), ported from Noovex9 32-bit ---- */

static u32 g_xmem=DMW_FIXED+0x10000u; int g_xcur=0, g_xhc_n=0, msd_ctl=0, xhci_pci_fn=0, xhci_hi4g=0;
static u32 g_xhc_mem[XHC_MAX-1];

#include "drivers/usb/xhci_parts/01_xhc_mem_for.c"
#include "drivers/usb/xhci_parts/02_hid2ascii.c"
#include "drivers/usb/xhci_parts/03_ufat_poke.c"
#include "drivers/usb/xhci_parts/04_uvc_er_init.c"
