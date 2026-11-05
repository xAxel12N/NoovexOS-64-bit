#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/fs/vfs/sysroot.h"
#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/arch/x86_64/user/ring3.h"
#include "include/nvx/core/proc/elf.h"
#include "include/nvx/core/proc/sched.h"
#include "include/nvx/arch/x86_64/smp/smp.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/lib/js/js.h"
#include "include/nvx/drivers/net/ethernet/nic.h"
#include "include/nvx/sys/linux/lxsock.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/arch/x86_64/interrupts/interrupts.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/vfs/vfs.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/fs/nvx/nvxfs.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/sys/native/syscall.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/core/sound.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/lib/crypto/nvxcrypto2.h"
#include "include/nvx/shell/vt.h"
#include "include/nvx/arch/x86_64/smp/smp_sched.h"
#include "include/nvx/fs/nvx/nvx4.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/sys/linux/linux.h"
#include "include/nvx/apps/system/files_classic.h"
#define LX_EPERM 1
#define LX_ENOENT 2
#define LX_ESRCH 3
#define LX_EINTR 4
#define LX_EIO 5
#define LX_E2BIG 7
#define LX_ENOEXEC 8
#define LX_EBADF 9
#define LX_ECHILD 10
#define LX_EAGAIN 11
#define LX_ENOMEM 12
#define LX_EACCES 13
#define LX_EFAULT 14
#define LX_EBUSY 16
#define LX_EEXIST 17
#define LX_ENODEV 19
#define LX_ENOTDIR 20
#define LX_EISDIR 21
#define LX_EINVAL 22
#define LX_EMFILE 24
#define LX_ENOTTY 25
#define LX_ENOSPC 28
#define LX_ESPIPE 29
#define LX_EPIPE 32
#define LX_ERANGE 34
#define LX_ENAMETOOLONG 36
#define LX_ENOSYS 38
#define LX_EBADFD 77
#define LX_SNDCTL 116
#define LX_SNDPCM 117
#define LX_ENOTEMPTY 39
#define LX_ENOTSUP 95
#define LX_AT_FDCWD (-100)
#define LX_DIRDEV 11
#define LX_SRDIR 112
#define LX_PATH 256
#define LX_IOMAX (1<<20)

#include "sys/linux/linux_parts/01_lx_ok.c"
#include "sys/linux/linux_parts/02_ivl_muldivk.c"
#include "sys/linux/linux_parts/03_ino_ev_fd.c"
#include "sys/linux/linux_parts/04_lx_wait_deadline.c"
#include "sys/linux/linux_parts/05_lx_mmap.c"
#include "sys/linux/linux_parts/06_lx_mkdir.c"
#include "sys/linux/linux_parts/07_lx_dup3.c"
