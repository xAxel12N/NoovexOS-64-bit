#ifndef NVX_INIT_INSTALLER_H
#define NVX_INIT_INSTALLER_H

#include "include/nvx/base.h"

extern const unsigned char _binary_inst_efi_bin_start[], _binary_inst_efi_bin_end[];
extern int g_ip_curmoved;

void dm_present(void);
int dm_input(void);
void inst_snapshot_data(void);
void kcheck_run(void);
int mm_area_conflicts(void);
void memmap_run(void);
void memtest_run(void);
void ip_load(void);
void dm_open(int ret);
void ip_open(int ret);
void ip_box(int* ox,int* oy,int* ow,int bw,int bh,const char* title);
void ip_present(void);
int ip_input(void);
void bootmenu_present(void);
int bootmenu_input(void);
long inst_syscall(u64 n,u64 a,u64 b,u64 c);
int inst_launch(int ret);
int inst_app_tick(void);
extern const u8* g_inst_img;
extern u32 g_inst_len;
extern int g_inst_pid;

#endif
