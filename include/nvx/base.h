#ifndef NVX_BASE_H
#define NVX_BASE_H
#include <stdint.h>
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t  i64;
#include "include/kapi.h"
#include "include/drivers.h"
#include "arch/x86_64/cpu/cpu.h"
#include "arch/x86_64/cpu/io.h"
#include "lang/rune/rune_gfx.h"
#include "third_party/ntfs/nvxntfs.h"

struct anode;
struct aobj;
struct bkl;
struct hblk;
struct idt_entry;
struct idt_ptr;
struct jpipe;
struct jproc;
struct lxsock;
struct nvx_FFILE_t;
struct nvx_FTREE_t;
struct nvx_JS_BIN_t;
struct nvx_g_acpi_t;
struct nvx_g_acs_t;
struct nvx_g_aec_t;
struct nvx_g_bl_t;
struct nvx_g_buildsig_t;
struct nvx_g_env_t;
struct nvx_g_hr_t;
struct nvx_g_vfat_t;
struct nvx_ufs_t;
struct nvx_zent_t;
struct pcpu;
struct pkg_list_ctx;
struct proc;
struct shmreg;
struct sjob;
struct sysregs;
struct tss64;
struct ulink;
struct vfile;
struct vfs_fs_ops;
struct wmsg;

#endif
