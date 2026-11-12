#ifndef NVX_CORE_SECBOOT_H
#define NVX_CORE_SECBOOT_H

#include "include/nvx/base.h"

#define SB_KERNEL_OK 1u
#define SB_SYSROOT_OK 2u
#define SB_FW_SECURE 4u
#define SB_OVERRIDE 8u
#define SB_ROLLBACK 16u

extern int g_lockdown;
extern u32 g_sb_flags, g_sb_minsvn, g_sb_loader;

void secboot_init(void);
int secboot_signed(void);
int secboot_check_kernel(const u8* k,u32 n,u32* svn);
int secboot_check_sysroot(const u8* b,u32 n);
int secboot_check_update(const u8* k,u32 n,const u8* sr,u32 srn,u32* svn);
const char* secboot_why(int r);
int secboot_locked(const char* what);
void secboot_report(void (*out)(const char*));
int secboot_check_file(const u8* d,u32 n,u32* body);
const char* secboot_file_why(int r);

#endif
