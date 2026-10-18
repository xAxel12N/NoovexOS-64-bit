#ifndef NVX_FS_VFS_H
#define NVX_FS_VFS_H

#include "include/nvx/base.h"
#include "include/nvx/fs/ramfs.h"

struct vfs_fs_ops {
    const char* name;
    int  (*match)(const char* path);
    int  (*read)(const char* path,char* buf,int off,int len);
    int  (*size)(const char* path);
};

void vfs_register(const vfs_fs_ops* fs);
const vfs_fs_ops* vfs_lookup(const char* path);
void vfs_init(void);
int vfs_open(const char* nm,int flags);
int vfs_read(int fd,char* buf,int len);
int vfs_write(int fd,const char* buf,int len);
int vfs_lseek(int fd,int off,int whence);
int vfs_close(int fd);
int vfs_dup(int fd);
int vfs_fstat(int fd);

#endif
