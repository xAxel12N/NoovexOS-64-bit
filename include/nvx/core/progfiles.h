#ifndef NVX_CORE_PROGFILES_H
#define NVX_CORE_PROGFILES_H
#include "include/nvx/base.h"
#define PF_MAX 96
enum { PF_BUILTIN, PF_LINUX, PF_PACKAGE };
typedef struct { char name[48]; char folder[48]; char ver[16]; char pub[32]; char cmd[100]; char icon[64]; char desc[96]; char pkg[48]; char desktop[100]; u32 size; int kind, win, xi, hidden; } pf_app;
void progfiles_sync(int force);
int progfiles_count(void);
const pf_app* progfiles_get(int i);
#endif
