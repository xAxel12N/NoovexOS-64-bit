#ifndef NVX_CORE_PROC_ELF_H
#define NVX_CORE_PROC_ELF_H

#include "include/nvx/base.h"

#define USTK_V   0x40000100000ULL
#define ELF_IMG_TOP  0x40002000000ULL
#define ELF_STK_LO   (USTK_V-15ULL*0x1000ULL)
#define XA_CAP 98304
#define XA_MAX 4096
typedef struct { u8* base; u64 size; u64 entry; int ok; } nvx_module;

extern const unsigned char _binary_user_user_demo_elf_start[], _binary_user_user_demo_elf_end[];
extern const unsigned char _binary_user_user_fork_elf_start[], _binary_user_user_fork_elf_end[];
extern const unsigned char _binary_user_user_app_elf_start[], _binary_user_user_app_elf_end[];
extern const unsigned char _binary_user_user_thread_elf_start[], _binary_user_user_thread_elf_end[];
extern char g_exec_args[2048];
extern u64 g_arg_rsp ,g_arg_argv;
extern int g_arg_argc;
extern char g_xa_buf[XA_CAP];
extern int g_xa_off[XA_MAX], g_xa_n , g_xa_set;
extern char g_xe_buf[XA_CAP];
extern int g_xe_off[XA_MAX], g_xe_n , g_xe_set;
extern int g_elf_pie;

u64 elf_build_space(const u8* elf,u32 len,u64* entry);
int nvx_dlload(const u8* elf,u32 len,nvx_module* mod);
int nvx_dlcall(nvx_module* mod);
int elf_load_run(const u8* elf,u32 len);

#endif
