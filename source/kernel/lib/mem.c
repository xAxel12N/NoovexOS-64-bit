#include "include/nvx/base.h"
#include "include/nvx/lib/mem.h"
void *memset(void *d,int c,unsigned long n){ u8 *p=d; while(n--) *p++=(u8)c; return d; }
void *memcpy(void *d,const void *s,unsigned long n){ u8 *p=d; const u8 *q=s; while(n--) *p++=*q++; return d; }
