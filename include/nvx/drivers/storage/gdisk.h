#ifndef NVX_DRIVERS_STORAGE_GDISK_H
#define NVX_DRIVERS_STORAGE_GDISK_H

#include "include/nvx/base.h"
#include "include/nvx/mem/dmawin.h"

#define AHCI_BUF DMW(0x183000u)
#define AHCI_MAXSEC 128u

extern int ahci_ok;
extern u64 ahci_sectors;

int ahci_cmd(u64 lba,u32 count,u32 buf,int write,int atacmd);
int ahci_flush(void);
int ahci_flush_ok(void);
int ahci_shutdown_all(int standby);
int ahci_read(u64 lba,int cnt,u8* out);
int ahci_write1(u64 lba,const u8* in);
int ahci_init(void);
int ahci_resume(void);

#endif
