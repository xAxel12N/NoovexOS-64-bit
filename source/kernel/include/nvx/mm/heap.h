#ifndef NVX_MM_HEAP_H
#define NVX_MM_HEAP_H

#include "include/nvx/base.h"

typedef struct hblk { unsigned long size; struct hblk* next; int used; unsigned int magic; } hblk;
#define HBLK_MAGIC 0x4E56484Bu

extern hblk* heap_head;
extern unsigned long long heap_cur , heap_top , heap_base;
extern int g_heap_mapped;

void heap_init(int ram_mb);
void* kmalloc(unsigned long n);
void kfree(void* p);
void* krealloc(void* p,unsigned long n);
void* kcalloc(unsigned long a,unsigned long b);

#endif
