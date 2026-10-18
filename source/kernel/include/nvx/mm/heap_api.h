#ifndef NVX_MM_HEAP_API_H
#define NVX_MM_HEAP_API_H

#include "include/nvx/base.h"

void* malloc(unsigned long n);
void free(void* p);
void* realloc(void* p,unsigned long n);
void* calloc(unsigned long a,unsigned long b);
unsigned long heap_used(void);

#endif
