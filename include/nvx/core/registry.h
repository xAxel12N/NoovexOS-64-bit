#ifndef NVX_CORE_REGISTRY_H
#define NVX_CORE_REGISTRY_H
#include "include/nvx/base.h"
enum { REG_EBADKEY=-1, REG_EBADNAME=-2, REG_ERO=-3, REG_EPERM=-4, REG_ERANGE=-5, REG_EFULL=-6, REG_ENOENT=-7 };
void reg_boot(void);
void reg_load(int apply);
void reg_user_changed(void);
int reg_save(void);
int reg_get(const char* key,const char* name,char* out,int cap);
int reg_set(const char* key,const char* name,const char* val);
int reg_delete(const char* key,const char* name);
int reg_add_key(const char* key);
int reg_keys(char (*out)[64],int cap);
int reg_values(const char* key,char (*names)[32],int cap);
int reg_export(const char* path,const char* prefix);
int reg_import(const char* path,int* nset,int* nfail);
int reg_can_write(const char* key);
const char* reg_help(const char* key,const char* name);
const char* reg_err(int e);
#endif
