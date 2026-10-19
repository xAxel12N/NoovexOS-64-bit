#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/mem/heap_api.h"
/* standard-named aliases so ported C libraries link unchanged */void* malloc(unsigned long n){ return kmalloc(n); }
void  free(void* p){ kfree(p); }
void* realloc(void* p,unsigned long n){ return krealloc(p,n); }
void* calloc(unsigned long a,unsigned long b){ return kcalloc(a,b); }
unsigned long heap_used(void){ unsigned long u=0; for(hblk* b=heap_head;b;b=b->next) if(b->used) u+=b->size; return u; }
   /* defined later */
