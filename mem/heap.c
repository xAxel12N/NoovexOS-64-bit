#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/fs/vfs/sysroot.h"
/* ---- heap allocator (foundation for large ports: Lua/NetSurf/Quake): first-fit free-list + bump ---- */

hblk* heap_head=0; unsigned long long heap_cur=0, heap_top=0, heap_base=0;
  
int g_heap_mapped=0;
void heap_init(int ram_mb){
    unsigned long long ram=(unsigned long long)(ram_mb>0?ram_mb:64)*1048576ULL;
    unsigned long long lo=0x18000000ULL, cap=0xFC000000ULL, sb=sysroot_slot_base(), se=sysroot_slot_end();
    if(se){ if(sb>=lo+0x10000000ULL)cap=sb; else if(se+0x8000000ULL<=ram&&(!mm_known()||mm_is_usable(sb,se)))lo=se; }
    heap_base=lo;
    heap_top=(ram>heap_base+0x8000000ULL)?(ram-0x100000ULL):(heap_base+0x8000000ULL);
    if(heap_top>cap)heap_top=cap;
    if(mm_known()){ u64 cs=0,ce=0; if(mm_clean_region(lo,cap,&cs,&ce)&&ce-cs>=0x8000000ULL){ heap_base=cs; heap_top=ce-0x100000ULL; g_heap_mapped=2; heap_cur=heap_base; heap_head=0; return; }
        unsigned long long ue=mm_usable_end(heap_base); if(ue>cap)ue=cap;
        if(ue>=heap_base+0x4000000ULL){ heap_top=ue-0x100000ULL; g_heap_mapped=1; }
        else { for(unsigned long long c=lo;c+0x8000000ULL<=cap;c+=0x100000ULL){ unsigned long long e=mm_usable_end(c); if(e>cap)e=cap; if(e>=c+0x8000000ULL){ heap_base=c; heap_top=e-0x100000ULL; g_heap_mapped=1; break; } if(e>c)c=(e&~0xFFFFFULL); } } }
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
int (*g_heap_namefn)(char*,int,u64); void (*g_heap_log)(const char*);
u32 g_heap_bad, g_heap_dfree, g_heap_badfree, g_heap_reent, g_heap_oom; int g_heap_poison; volatile int g_heap_busy;
#define HGUARD 0xEAu
static void hb_mark(hblk* b,unsigned long req,u64 caller){ b->caller=(u32)caller; b->req=req<0xFFFFFFF0UL?(u32)req:0xFFFFFFFFu; if(b->req==0xFFFFFFFFu)return; u8* q=(u8*)b+sizeof(hblk); for(unsigned long i=req;i<b->size&&i<req+8;i++)q[i]=HGUARD; }
static int hb_guard_ok(hblk* b){ if(b->req==0xFFFFFFFFu||b->req>=b->size)return 1; u8* q=(u8*)b+sizeof(hblk); for(unsigned long i=b->req;i<b->size&&i<(unsigned long)b->req+8;i++) if(q[i]!=HGUARD)return 0; return 1; }
static void hb_report(const char* what,hblk* b,u64 who){
    char t[200],*p=t; const char* w=what; while(*w)*p++=*w++;
    if(b){ const char* s=" (block of "; while(*s)*p++=*s++; char d[24]; int n=0; unsigned long v=b->req; do{ d[n++]=(char)('0'+v%10); v/=10; }while(v&&n<20); while(n)*p++=d[--n]; s=" bytes from "; while(*s)*p++=*s++; if(g_heap_namefn)p+=g_heap_namefn(p,64,b->caller); *p++=')'; }
    if(who){ const char* s=" called by "; while(*s)*p++=*s++; if(g_heap_namefn)p+=g_heap_namefn(p,64,who); }
    *p=0; if(g_heap_log)g_heap_log(t); }
static void* heap_place(unsigned long long at,unsigned long n){
    hblk* b=(hblk*)at; b->size=n; b->used=1; b->next=0; b->magic=HBLK_MAGIC; b->caller=0; b->req=0xFFFFFFFFu;
    if(!heap_head)heap_head=b; else { hblk* t=heap_head; int g2=0; while(hblk_ok(t->next)){t=t->next; if(++g2>1000000)break;} t->next=b; }
    return (u8*)b+sizeof(hblk); }
static void* kmalloc_raw(unsigned long n);
void* kmalloc(unsigned long n){
    if(n==0||n>0xFFFFFFFF00000000UL)return 0;
    u64 who=(u64)__builtin_return_address(0); unsigned long req=n;
    if(g_heap_busy){ g_heap_reent++; }
    g_heap_busy=1; void* p=kmalloc_raw((n+8+15)&~15UL); g_heap_busy=0;
    if(p)hb_mark((hblk*)((u8*)p-sizeof(hblk)),req,who);
    else if(g_heap_oom++<32){ char t[160],*q=t; const char* s="heap: out of memory for "; while(*s)*q++=*s++; char d[24]; int k=0; unsigned long v=req; do{ d[k++]=(char)('0'+v%10); v/=10; }while(v&&k<20); while(k)*q++=d[--k]; s=" bytes requested by "; while(*s)*q++=*s++; if(g_heap_namefn)q+=g_heap_namefn(q,64,who); *q=0; if(g_heap_log)g_heap_log(t); }
    return p; }
static void* kmalloc_raw(unsigned long n){
    int g=0;
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
void kfree(void* p){ if(!p)return; hblk* b=(hblk*)((u8*)p-sizeof(hblk)); u64 who=(u64)__builtin_return_address(0);
    if(!hblk_ok(b)){ if(g_heap_badfree++<16)hb_report("heap: free of a pointer that is not a heap block",0,who); return; }
    if(!b->used){ if(g_heap_dfree++<16)hb_report("heap: the same block was freed twice - second free ignored",b,who); return; }
    if(!hb_guard_ok(b)){ if(g_heap_bad++<16)hb_report("heap: a block was written past its end",b,who); }
    if(g_heap_poison){ u8* q=(u8*)p; for(unsigned long i=0;i<b->size;i++)q[i]=0xDB; }
    if(g_heap_busy)g_heap_reent++;
    g_heap_busy=1; b->used=0;
    int g=0; for(hblk* c=heap_head; hblk_ok(c); c=c->next){ if(++g>1000000)break; int gg=0;
        while(hblk_ok(c->next) && !c->used && !c->next->used && (u8*)c->next==(u8*)c+sizeof(hblk)+c->size){ c->size+=sizeof(hblk)+c->next->size; c->next=c->next->next; if(++gg>1000000)break; } }
    g_heap_busy=0; }
void* krealloc(void* p,unsigned long n){ if(!p)return kmalloc(n); if(n==0){kfree(p);return 0;} hblk* b=(hblk*)((u8*)p-sizeof(hblk));
    if(!hblk_ok(b)||!b->used){ if(g_heap_badfree++<16)hb_report("heap: realloc of a pointer that is not a live heap block",0,(u64)__builtin_return_address(0)); return 0; }
    unsigned long old=b->req==0xFFFFFFFFu?b->size:b->req;
    if(b->size>=n+8){ hb_mark(b,n,b->caller); return p; }
    void* np=kmalloc(n); if(!np)return 0; unsigned long c=old<n?old:n; for(unsigned long i=0;i<c;i++)((u8*)np)[i]=((u8*)p)[i]; kfree(p); return np; }
void* kcalloc(unsigned long a,unsigned long b){ if(b&&a>0xFFFFFFFFFFFFUL/b)return 0; unsigned long n=a*b; void* p=kmalloc(n); if(p){u8* q=p; for(unsigned long i=0;i<n;i++)q[i]=0;} return p; }
int heap_check(int* used,int* bad,char* first,int cap){
    int nb=0,nu=0,nbad=0,g=0; if(first&&cap)first[0]=0;
    for(hblk* b=heap_head; b; b=b->next){ if(++g>2000000)break; if(!hblk_ok(b)){ nbad++; break; } nb++; if(b->used){ nu++; if(!hb_guard_ok(b)){ nbad++; if(first&&cap>80&&!first[0]){ char* p=first; const char* s="first overrun block was allocated by "; while(*s)*p++=*s++; *p=0; if(g_heap_namefn)g_heap_namefn(p,64,b->caller); } } } }
    *used=nu; *bad=nbad; return nb; }
