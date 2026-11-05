#include "include/nvx/base.h"
#include "include/nvx/lib/base/mem.h"
void *memset(void *d,int c,unsigned long n){ cpu_fill(d,c,n); return d; }
void *memcpy(void *d,const void *s,unsigned long n){ cpu_copy(d,s,n); return d; }
