#ifndef NVX_FS_UNIXFS_H
#define NVX_FS_UNIXFS_H

#include "include/nvx/base.h"

#define NLINKS 256
struct ulink { int used, dir, ino; char name[52]; };

extern struct ulink g_lk[NLINKS];
extern int g_n3_conv;
extern int g_fb0_open , g_fb0_dirty;
extern int g_mq_h ,g_mq_t , g_mouse_open;
extern char g_hostname[32];

u32 ux_now(void);
void ux_touch(int k,int data);
int nvx3_try_load(void);
int ux_is_admin(int uid);
int ux_lookup(const char* p,int* isdir,int follow);
int ux_link(const char* from,const char* to);
int ux_symlink(const char* target,const char* path);
int ux_readlink(const char* p,char* out,int cap);
int ux_unlink(const char* p);
int ux_chown(const char* p,int uid,int gid,int follow);
int ux_chmod(const char* p,unsigned mode);
int ux_utime(const char* p,u32 at,u32 mt);
int ux_mount(const char* src,const char* target);
int ux_umount(const char* target);
void ux_mouse_push(int dx,int dy,int btn);
int ux_dev_read(struct vfile* v,char* buf,int len);
int ux_dev_write(struct vfile* v,const char* buf,int len);
int ux_perm_ok(int k,int want);
int perm_file(int k,int want);
int perm_dir(int d);
int ux_links_in(int d);
int fs_is_open(int k);
void fs_newslot(int k);
int pd_touch(int k);
int ux_can_unlink(int d,int k);
void fs_slot_freed(int k);
void fs_orphan_close(int k);
void fs_drop_name(int k);
int ux_rename_link(const char* from,const char* to);
int proc_gen(const char* p,char* out,int cap);
int ux_statfs(const char* p,unsigned long* o);
int ux_read_file(const char* p,char* out,int cap);
int ux_uid_name(int uid,char* out,int cap);
void ux_save_localpw(void);
void ux_load_localpw(void);
int ux_stat2(const char* p,u32* o,int follow);
int k_readdir_extra(int d,int n,char* name,int* type);
void ux_core_start(const char* user);
void ux_desktop_start(const char* user);

#endif
