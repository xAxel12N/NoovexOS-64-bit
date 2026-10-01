#ifndef _SHIM_SYS_STAT_H
#define _SHIM_SYS_STAT_H
struct stat { long st_size; long st_mode; long st_mtime; };
int stat(const char*,struct stat*);
int mkdir(const char*,int);
#define S_ISDIR(m) 0
#define S_IFDIR 0040000
#define S_IRWXU 0700
#endif
