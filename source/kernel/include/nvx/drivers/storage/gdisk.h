#ifndef NVX_DRIVERS_STORAGE_GDISK_H
#define NVX_DRIVERS_STORAGE_GDISK_H

#include "include/nvx/base.h"
#include "include/nvx/mm/dmawin.h"

#define AHCI_BUF DMW(0x183000u)

extern int ahci_ok;
extern u32 ahci_sectors;

int ahci_cmd(u32 lba,u32 count,u32 buf,int write,int atacmd);
int ahci_flush(void);
int ahci_read(u32 lba,int cnt,u8* out);
int ahci_write1(u32 lba,const u8* in);
int ahci_init(void);

#endif
