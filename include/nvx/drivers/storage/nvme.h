#ifndef NVX_DRIVERS_STORAGE_NVME_H
#define NVX_DRIVERS_STORAGE_NVME_H

#include "include/nvx/base.h"

enum { DK_NONE, DK_IDE, DK_AHCI, DK_NVME, DK_VIRTIO };

extern int nvme_ok;
extern int nvme_bad , nvme_lasterr;
extern u32 g_nvme_bsz;
extern int nvme_found , nvme_failstage;
extern int g_dktype;
extern u32 g_dksectors;
extern unsigned long g_bc_hits , g_bc_miss;
extern int g_gdisk_probed;
extern const unsigned char _binary_inst_boot_bin_start[], _binary_inst_boot_bin_end[];
extern const unsigned char _binary_inst_stage2_bin_start[], _binary_inst_stage2_bin_end[];
extern const unsigned char _binary_inst_boot_cfg_start[], _binary_inst_boot_cfg_end[];

const char* gdisk_name(void);
int gdisk_read(u32 lba,int cnt,u8* buf);
void bc_invalidate(u32 lba,int cnt);
int gdisk_read_cached(u32 lba,int cnt,u8* buf);
int gdisk_flush(void);
int gdisk_write(u32 lba,const u8* buf);
int gdisk_writen(u32 lba,int cnt,const u8* buf);
int gdisk_readn(u32 lba,int cnt,u8* buf);
u32 dk_ratio(void);
void gdisk_ensure(void);
void ins83(const char* nm,u8 out[11]);

#endif
