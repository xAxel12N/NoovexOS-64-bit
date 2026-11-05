#include "include/nvx/core/klog.h"
#include "include/nvx/base.h"
#include "include/nvx/lib/crypto/nvxtls.h"
#include "include/nvx/lib/base/mem.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/lib/js/js.h"
#include "include/nvx/drivers/net/ethernet/nic.h"
#include "include/nvx/drivers/storage/ahci.h"
#include "include/nvx/net/stack/tcpip.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/sys/native/syscall.h"
#include "include/nvx/apps/internet/browser.h"
#include "include/nvx/graphics/ui/desktop.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/fs/vfs/ramfs.h"
/* =========================================================================
   Browser (online: http + https, text rendering)
   ========================================================================= */
/* =========================================================================
   Image decode (PNG all common types, baseline JPEG) + multi-image pool
   ========================================================================= */
#define IMGPOOL_SZ (8*1024*1024)
static u8* imgpool; static int imgpool_off=0;

#include "apps/internet/browser_parts/01_imgpool_ok.c"
#include "apps/internet/browser_parts/02_show_errors.c"
#include "apps/internet/browser_parts/03_perm_bar_click.c"
#include "apps/internet/browser_parts/04_g_bmbar_on.c"
