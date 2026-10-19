#ifndef NVX_FS_NVXFS_H
#define NVX_FS_NVXFS_H

#include "include/nvx/base.h"

#define NVX_DIR_LBA  g_nvx_base
#define NVX_DATA0    (NVX_DIR_LBA+8)
#define NVX_SECPF    4096u
#define NVX_MAX      24
#define NVX_DN_MAX   64
#define NVX_PTYPE    0x7F
#define NVX_REGION_SECS (8u+(u32)(NVX_MAX+1)*NVX_SECPF)
typedef struct { char name[64]; u32 start,len,folder,dir,mode,uid,gid; } nvx_ent;
typedef struct { char name[32]; int parent, used; unsigned int mode; } nvx_dnode;
typedef struct { u32 magic,count; nvx_ent e[NVX_MAX]; u32 dcount; nvx_dnode d[NVX_DN_MAX]; } nvxfs_t;

extern int g_rootfs;
extern u32 g_nvx_base , g_nvx_secs;
extern int g_nvx_generic;
extern const char* g_nvx_note;
extern nvxfs_t g_nvx;
extern u8 nvx_sec[512];
extern int disk_ok;
extern int g_nvx_have_tree;

int nvx_rd(u32 lba,u8* b);
int nvx_rdn(u32 lba,int n,u8* b);
int nvx_write(const char*name,const u8*buf,int len,int folder);
void nvx_delete_name(const char*name);
int disk_init(void);

#endif
