#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/arch/x86_64/cpu/cpuinfo.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/drivers/storage/ata.h"
#include "include/nvx/fs/nvx/nvxfs.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/sys/native/syscall.h"
#include "include/nvx/fs/nvx/nvx4.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/apps/office/editor.h"
#include "include/nvx/apps/internet/browser.h"
#include "include/nvx/apps/system/files_classic.h"
#include "include/nvx/apps/system/file_explorer.h"
#include "include/nvx/apps/media/videoplayer.h"
#include "include/nvx/core/init/installer.h"
#include "include/nvx/fs/vfs/volumes.h"
/* =========================================================================
   File Explorer (v10.60) - browses the real Unix directory tree (DT + FS)
   ========================================================================= */
#define FX_MAXROWS 96
#define FX_MAXB    72

#include "apps/system/file_explorer_parts/01_fx_trash.c"
#include "apps/system/file_explorer_parts/02_tpc_open.c"
#include "apps/system/file_explorer_parts/03_fx_rowat.c"
