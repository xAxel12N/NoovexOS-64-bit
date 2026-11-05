#include "include/nvx/base.h"
#include "include/nvx/fs/vfs/sysroot.h"
#include "include/nvx/core/update.h"
#include "include/nvx/lib/base/mem.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/lib/crypto/nvxcrypto.h"
#include "include/nvx/arch/x86_64/cpu/cpuinfo.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/net/ethernet/nic_names.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/drivers/input/ps2/ps2.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/drivers/storage/nvme.h"
#include "include/nvx/fs/nvx/nvxfs.h"
#include "include/nvx/core/buildsig.h"
#include "include/nvx/core/power.h"
#include "include/nvx/fs/nvx/nvx4.h"
#include "include/nvx/fs/ntfs/ntfsk.h"
#include "include/nvx/apps/games/doom.h"
#include "include/nvx/graphics/ui/desktop.h"
#include "include/nvx/core/init/splash.h"
#include "include/nvx/core/init/bootmenu.h"
#include "include/nvx/core/init/installer.h"
#include "include/nvx/core/init/setup.h"
#include "include/nvx/core/init/main.h"
/* ================= Disk Manager (graphical partition editor + installer) ================= */
static int g_dm_open=0, g_dm_sel=-1, g_dm_editname=0, g_dm_ret=ST_BOOTMENU; static void ip_open_preset(int ret,int mode,int part); static char g_dm_name[16]="NoovexOS";
static u8 g_dm_mbr[512]; static int g_dm_msg=0; static const char* g_dm_msgtext=""; static int g_dm_confirm=0; static char g_dm_ctext[120];
#define DM_ROWS 8
#define DM_NB 9
static int dm_rowy[DM_ROWS], dm_bx[DM_NB], dm_by[DM_NB], dm_bw[DM_NB];
typedef struct { int used; u8 type; u32 start, size; int gi; char name[40]; char fs[24]; } dmpart;
static dmpart g_dm_p[DM_ROWS]; static int g_dm_np=0, g_dm_gpt=0;
static u8 g_dm_ghdr[512]; static u8* g_dm_gent=0; static u32 g_dm_gn=0, g_dm_gsz=0, g_dm_gsec=0; static u64 g_dm_gelba=0, g_dm_galt=0;

#include "core/init/installer_parts/01_dm_crc32.c"
#include "core/init/installer_parts/02_ip_mem_verify.c"
#include "core/init/installer_parts/03_ip_pick_disk.c"
