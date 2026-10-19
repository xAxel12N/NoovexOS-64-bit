#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/mem/pmm_vmm.h"
/* ---- heap allocator (foundation for large ports: Lua/NetSurf/Quake): first-fit free-list + bump ---- */

hblk* heap_head=0; unsigned long long heap_cur=0, heap_top=0, heap_base=0;
  
int g_heap_mapped=0;
void heap_init(int ram_mb){
    heap_base=0x18000000ULL;
    unsigned long long ram=(unsigned long long)(ram_mb>0?ram_mb:64)*1048576ULL;
    heap_top=(ram>heap_base+0x8000000ULL)?(ram-0x100000ULL):(heap_base+0x8000000ULL);
    if(heap_top>0xFC000000ULL)heap_top=0xFC000000ULL;
    if(mm_known()){ u64 cs=0,ce=0; if(mm_clean_region(0x18000000ULL,0xFC000000ULL,&cs,&ce)&&ce-cs>=0x8000000ULL){ heap_base=cs; heap_top=ce-0x100000ULL; g_heap_mapped=2; heap_cur=heap_base; heap_head=0; return; }
        unsigned long long ue=mm_usable_end(heap_base); if(ue>0xFC000000ULL)ue=0xFC000000ULL;
        if(ue>=heap_base+0x4000000ULL){ heap_top=ue-0x100000ULL; g_heap_mapped=1; }
        else { for(unsigned long long c=0x18000000ULL;c<0xF0000000ULL;c+=0x100000ULL){ unsigned long long e=mm_usable_end(c); if(e>0xFC000000ULL)e=0xFC000000ULL; if(e>=c+0x8000000ULL){ heap_base=c; heap_top=e-0x100000ULL; g_heap_mapped=1; break; } if(e>c)c=(e&~0xFFFFFULL); } } }
    heap_cur=heap_base; heap_head=0;
}
/* ---- extra heap arenas carved from identity-mapped RAM above 4 GB ----
   kmalloc normally serves the low arena (heap_base..heap_top). When that is
   exhausted (big apt installs: .deb buffers, decompressed data.tar, package
   index), it pulls contiguous high pages via hp_alloc_run and keeps going, so
   all installed RAM is usable instead of only the sub-4 GB region. */
#define HEAP_EX_MAX 1024
#define HEAP_EX_CHUNK 0x4000000ULL
static struct { unsigned long long base, cur, top; } g_ex[HEAP_EX_MAX];
static int g_nex=0;
static int in_arena(unsigned long long a){
    if(a>=heap_base && a<heap_top)return 1;
    for(int i=0;i<g_nex;i++) if(a>=g_ex[i].base && a<g_ex[i].top)return 1;
    return 0; }
static int hblk_ok(hblk* b){ unsigned long long a=(unsigned long long)b; return b && (a&7)==0 && in_arena(a) && b->magic==HBLK_MAGIC; }
static void* heap_place(unsigned long long at,unsigned long n){
    hblk* b=(hblk*)at; b->size=n; b->used=1; b->next=0; b->magic=HBLK_MAGIC;
    if(!heap_head)heap_head=b; else { hblk* t=heap_head; int g2=0; while(hblk_ok(t->next)){t=t->next; if(++g2>1000000)break;} t->next=b; }
    return (u8*)b+sizeof(hblk); }
void* kmalloc(unsigned long n){
    if(n==0)return 0; n=(n+15)&~15UL; int g=0;
    for(hblk* b=heap_head; hblk_ok(b); b=b->next){ if(++g>1000000)break; if(!b->used && b->size>=n){ b->used=1;
        if(b->size >= n+sizeof(hblk)+32){ hblk* nb=(hblk*)((u8*)b+sizeof(hblk)+n); nb->size=b->size-n-sizeof(hblk); nb->used=0; nb->magic=HBLK_MAGIC; nb->next=b->next; b->next=nb; b->size=n; }
        return (u8*)b+sizeof(hblk); } }
    unsigned long long need=sizeof(hblk)+n;
    if(heap_cur+need<=heap_top){ unsigned long long at=heap_cur; heap_cur+=need; return heap_place(at,n); }
    for(int i=0;i<g_nex;i++) if(g_ex[i].cur+need<=g_ex[i].top){ unsigned long long at=g_ex[i].cur; g_ex[i].cur+=need; return heap_place(at,n); }
    if(g_nex<HEAP_EX_MAX){ unsigned long long want=(need>HEAP_EX_CHUNK?need:HEAP_EX_CHUNK); want=(want+0x1FFFFFULL)&~0x1FFFFFULL;
        unsigned long long pa=hp_alloc_run((u32)(want>>12));
        if(pa){ g_ex[g_nex].base=pa; g_ex[g_nex].cur=pa+need; g_ex[g_nex].top=pa+want; g_nex++; return heap_place(pa,n); } }
    return 0;
}
void kfree(void* p){ if(!p)return; hblk* b=(hblk*)((u8*)p-sizeof(hblk)); if(!hblk_ok(b))return; b->used=0;
    int g=0; for(hblk* c=heap_head; hblk_ok(c); c=c->next){ if(++g>1000000)break; int gg=0;
        while(hblk_ok(c->next) && !c->used && !c->next->used && (u8*)c->next==(u8*)c+sizeof(hblk)+c->size){ c->size+=sizeof(hblk)+c->next->size; c->next=c->next->next; if(++gg>1000000)break; } } }
void* krealloc(void* p,unsigned long n){ if(!p)return kmalloc(n); if(n==0){kfree(p);return 0;} hblk* b=(hblk*)((u8*)p-sizeof(hblk)); unsigned long old=b->size; if(old>=n)return p; void* np=kmalloc(n); if(!np)return 0; unsigned long c=old<n?old:n; for(unsigned long i=0;i<c;i++)((u8*)np)[i]=((u8*)p)[i]; kfree(p); return np; }
void* kcalloc(unsigned long a,unsigned long b){ unsigned long n=a*b; void* p=kmalloc(n); if(p){u8* q=p; for(unsigned long i=0;i<n;i++)q[i]=0;} return p; }
