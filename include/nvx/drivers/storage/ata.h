#ifndef NVX_DRIVERS_STORAGE_ATA_H
#define NVX_DRIVERS_STORAGE_ATA_H

#include "include/nvx/base.h"

extern int g_ata_drv;
extern u32 g_disk_sectors;

int ata_read(u32 lba,int count,u8* buf);
int ata_detect(void);
int ata_write(u32 lba,const u8* buf);
int ata_writen(u32 lba,int count,const u8* buf);

#endif
