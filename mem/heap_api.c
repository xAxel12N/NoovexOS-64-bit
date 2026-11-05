#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/mem/heap_api.h"
/* standard-named aliases so ported C libraries link unchanged */void* malloc(unsigned long n){ return kmalloc(n); }
void  free(void* p){ kfree(p); }
void* realloc(void* p,unsigned long n){ return krealloc(p,n); }
void* calloc(unsigned long a,unsigned long b){ return kcalloc(a,b); }
void heap_stat(unsigned long* used,unsigned long* span){ unsigned long u=0,t=0; int g=0; for(hblk* b=heap_head;b&&b->magic==HBLK_MAGIC&&g<2000000;b=b->next,g++){ t+=b->size; if(b->used) u+=b->size; } *used=u; *span=t; }
unsigned long heap_used(void){ unsigned long u=0; int g=0; for(hblk* b=heap_head;b&&b->magic==HBLK_MAGIC&&g<2000000;b=b->next,g++) if(b->used) u+=b->size; return u; }
   /* defined later */

extern unsigned long g_cx_heap_kb; extern u32 g_ram_mb;
u64 sysroot_slot_base(void); u64 sysroot_slot_end(void);
void mem_summary(u64* total_kb,u64* used_kb,u64* heap_kb){
    u64 t=(u64)g_ram_mb*1024ULL, h=g_cx_heap_kb?g_cx_heap_kb:heap_used()/1024, u=h+(heap_base>>10);
    u64 sb=sysroot_slot_base(), se=sysroot_slot_end(); if(se>sb&&sb>=heap_base)u+=(se-sb)>>10;
    if(u>t)u=t; *total_kb=t; *used_kb=u; if(heap_kb)*heap_kb=h; }
