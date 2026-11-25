#ifndef NVX_MMC_BLK_H
#define NVX_MMC_BLK_H
#include "include/nvx/base.h"
#include "drivers/mmc/mmc.h"
#define MMCBLK_MAX 4
#define MMCBLK_CHUNK 64
typedef int (*mmcblk_rd_fn)(void* ctx,u64 lba,int n,u8* buf);
typedef int (*mmcblk_wr_fn)(void* ctx,u64 lba,int n,const u8* buf);
typedef struct { mmcblk_rd_fn rd; mmcblk_wr_fn wr; void* ctx; u64 sectors; int used, ro, ejected, kind; char name[24]; char model[32]; u32 nrd, nwr, nerr, gen; } mmcblk;
enum { MMCBLK_SD=0, MMCBLK_SDHC=1, MMCBLK_MMC=2 };
extern mmcblk g_mmcblk[MMCBLK_MAX]; extern u32 g_mmcblk_gen;
int mmcblk_add(int kind,const char* model,u64 sectors,int ro,void* ctx,mmcblk_rd_fn rd,mmcblk_wr_fn wr);
void mmcblk_remove(int i);
int mmcblk_present(int i);
int mmcblk_count(void);
int mmcblk_read(int i,u64 lba,int n,u8* buf);
int mmcblk_write(int i,u64 lba,int n,const u8* buf);
int mmcblk_eject(int i);
int mmcblk_status(char* out,int cap);
int mmcblk_attach_host(struct mmc_host* h);
void mmcblk_detach_host(struct mmc_host* h);
void mmcblk_sync_hosts(void);
#endif
