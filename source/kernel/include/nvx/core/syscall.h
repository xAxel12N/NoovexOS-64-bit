#ifndef NVX_CORE_SYSCALL_H
#define NVX_CORE_SYSCALL_H

#include "include/nvx/base.h"
#include "include/nvx/fs/dirtree.h"

#define NENV 24
struct nvx_g_env_t { int used; char name[24]; char val[128]; };
#define dt_path(n,o) dt_path_n((n),(o),(int)sizeof(o))

extern u8 g_spawnbuf[65536];
extern struct nvx_g_env_t g_env[NENV];

int sys_spawn(const char* path);
int sys_waitpid(int pid);
const char* cur_user(void);
int perm_can_exec(unsigned int mode);
int env_set(const char* nm,const char* val);
int env_pack(char* buf,int cap,int* off,int max);
void env_init(void);
void syscall_dispatch(struct sysregs* r);
int fs_save_bin(const char*name,const u8*data,int len,int folder);
void fs_init(void);

#endif
