#ifndef NVX_FS_PKG_H
#define NVX_FS_PKG_H

#include "include/nvx/base.h"

extern u8 g_pkg_raw[8u*1024u*1024u];
extern char g_pkg_name[64], g_pkg_ver[48], g_pkg_arch[24], g_pkg_desc[128];
extern int g_pkg_nfiles;
extern long g_pkg_total;

int tar_walk(const u8* t,long n,void(*cb)(const char*,unsigned,const u8*,long,int,void*),void* ctx);
void pkg_print_cb(const char* name,unsigned mode,const u8* data,long len,int isdir,void* ctx);
int pkg_open(const u8* file,long n,const u8** tar,long* tarlen);
int pkg_install(const u8* file,long n,int extract);

#endif
