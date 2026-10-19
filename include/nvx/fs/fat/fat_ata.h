#ifndef NVX_FS_FAT_ATA_H
#define NVX_FS_FAT_ATA_H

#include "include/nvx/base.h"

extern int fat_ok , fat_is32;
extern u32 fat_part_lba, fat_fatstart, fat_datastart, fat_rootlba, fat_rootclus;
extern int fat_spc, fat_rootents, fat_bps;

u32 fr32(const u8*p);
int fat_mount(void);
void fat_listdir(void (*cb)(const u8* ent));
int fat_read_file(const char* name,u8* out,int cap);

#endif
