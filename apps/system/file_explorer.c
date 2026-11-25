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
#include "fs/ext4/ext4.h"
#include "include/nvx/fs/vfs/volfs.h"
int ext4k_mounted(void);
/* =========================================================================
   File Explorer (v11.22) - browses the real Unix directory tree (DT + FS)
   ========================================================================= */
#define FX_MAXROWS 96
#define FX_MAXB    72

#include "apps/system/file_explorer/trash_and_sort.c"
#include "apps/system/file_explorer/open_and_draw.c"
#include "apps/system/file_explorer/volume_view.c"
#include "apps/system/file_explorer/clicks_and_drops.c"
