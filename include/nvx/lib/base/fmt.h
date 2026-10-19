#ifndef NVX_LIB_FMT_H
#define NVX_LIB_FMT_H

#include "include/nvx/base.h"

char *e4(char*p,u32 r);
char *scopy(char*p,const char*s);
char *udec(char*p,u32 v);
char *u2(char*p,u32 v);
char *uhex(char*p,u64 v,int dg);
char *hx(char*p,u32 v,int dg);
int seq(const char*a,const char*b);

#endif
