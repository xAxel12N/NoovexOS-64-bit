#ifndef NVX_FS_RAMFS_H
#define NVX_FS_RAMFS_H

#include "include/nvx/base.h"

#define FS_N 16384
#define FS_CAP 0x40000000
#define FS_OLDCAP 2097152
#define S_IFMT   0170000
#define S_IFDIR  0040000
#define S_IFREG  0100000
#define S_IFCHR  0020000
typedef struct{ char name[64]; char* data; int len; int cap; int used; int folder; int dir; unsigned int mode; int uid, gid; u32 nlink, mtime, ctime, atime; u8 lazy, clean, orphan, pad; int dlo, dhi; u64 tick; } FsFile;
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0x40
#define O_TRUNC  0x200
#define O_APPEND 0x400
#define VMAXOPEN 1024
#define FDMAX    256
typedef struct vfs_fs_ops vfs_fs_ops;
struct vfile { int used, fs_idx, off, flags, dev, ref; const vfs_fs_ops* vfs; char vpath[128]; };

extern FsFile FS[FS_N];
extern u8 g_fs_dirty[FS_N];
extern volatile int g_fs_wb_pending;
extern u64 g_fs_wb_due;
extern struct vfile g_vf[VMAXOPEN];
extern int g_fd[FDMAX];

void kmemcpy(void* d,const void* s,unsigned long n);
void fs_wb_mark(int k);
void fs_free(int k);
int fs_reserve(int k,long need);
int fs_ensure(int k);
char* fsd(int k);
char* fs_wr(int k,long need);
char* fs_wr_range(int k,long lo,long hi);
int fs_set(int k,const char* src,long n);
int fs_truncate_to(int k,long n);
void fd_init(void);
int fs_find_name(const char* nm);
int dev_id(const char* nm);

#endif
