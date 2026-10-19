#ifndef NVX_FS_DIRTREE_H
#define NVX_FS_DIRTREE_H

#include "include/nvx/base.h"

#define DN_MAX 4096
typedef struct { char name[64]; int parent; int used; unsigned int mode; int uid,gid; u32 mtime, ctime, atime; } DNode;

extern DNode DT[DN_MAX];
extern int g_dtree_init;

int dt_mkdir_at(int parent,const char* name);
void dt_init(void);
void dt_save(void);
char* mode_str(unsigned int m,char* o);
unsigned int dt_mode(int node);
int dt_child(int parent,const char* name);
int dt_is_tmp(int d);
void dt_load_done(void);
int dt_resolve_dir(const char* p);
int dt_dir_ok(int d);
int k_split(const char* p,int* dir,char* base);
int k_find_file(const char* p);
int k_unlink(const char* p);
int k_is_ancestor(int anc,int node);
int k_rename(const char* from,const char* to);
int k_rmdir(const char* p);
int k_truncate(const char* p,long len);
int k_ftruncate(int fd,long len);
int k_fsync(int fd);
int k_readdir_at(const char* p,int n,char* name,int* type);
u64 k_realtime_ms(void);
void dt_path_n(int node,char* out,int cap);

#endif
