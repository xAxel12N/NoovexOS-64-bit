#include "include/nvx/base.h"
#include "include/nvx/mm/heap.h"
#include "include/nvx/mm/slab.h"
/* ---- Slab allocator: fast fixed-size object caches on top of kmalloc ----
   Each cache owns a free-list of same-sized objects carved from larger
   backing slabs. kslab_alloc/kslab_free are O(1) (push/pop a free-list).
   Falls back to kmalloc for sizes above the largest cache.
   Not SMP-locked here; see the SMP section for the guard. */

typedef struct slab_obj { struct slab_obj* next; } slab_obj;
typedef struct slab_page { struct slab_page* next; } slab_page;

typedef struct {
    unsigned long objsize;      /* padded object size */
    slab_obj*    freelist;      /* O(1) free objects */
    slab_page*   pages;         /* backing slabs (for stats) */
    unsigned long per_slab;     /* objects per backing slab */
    unsigned long nalloc;       /* live objects */
    unsigned long npages;       /* backing slabs grabbed */
    const char*  name;
} slab_cache;

#define SLAB_BACKING 8192ULL    /* each backing slab is 8 KB from kmalloc */
#define SLAB_NSIZES  9
static const unsigned long SLAB_SIZES[SLAB_NSIZES]={16,32,64,128,256,512,1024,2048,4096};
static slab_cache g_slabs[SLAB_NSIZES];
static int g_slab_ready=0;

void slab_init(void){
    for(int i=0;i<SLAB_NSIZES;i++){
        unsigned long sz=SLAB_SIZES[i];
        g_slabs[i].objsize=sz; g_slabs[i].freelist=0; g_slabs[i].pages=0;
        g_slabs[i].per_slab=(SLAB_BACKING-sizeof(slab_page))/sz;
        g_slabs[i].nalloc=0; g_slabs[i].npages=0; g_slabs[i].name="slab";
    }
    g_slab_ready=1;
}

/* grab one 8 KB backing slab from kmalloc and carve it into the free-list */
static int slab_grow(slab_cache* c){
    unsigned char* mem=(unsigned char*)kmalloc(SLAB_BACKING);
    if(!mem) return 0;
    slab_page* pg=(slab_page*)mem; pg->next=c->pages; c->pages=pg; c->npages++;
    unsigned char* base=mem+sizeof(slab_page);
    for(unsigned long k=0;k<c->per_slab;k++){
        slab_obj* o=(slab_obj*)(base+k*c->objsize);
        o->next=c->freelist; c->freelist=o;
    }
    return 1;
}

/* pick the smallest cache that fits n */
static int slab_index(unsigned long n){
    for(int i=0;i<SLAB_NSIZES;i++) if(n<=SLAB_SIZES[i]) return i;
    return -1;
}

static void* kslab_alloc(unsigned long n){
    if(!g_slab_ready) slab_init();
    int i=slab_index(n);
    if(i<0) return kmalloc(n);                 /* too big: plain heap */
    slab_cache* c=&g_slabs[i];
    if(!c->freelist){ if(!slab_grow(c)) return kmalloc(n); }
    slab_obj* o=c->freelist; c->freelist=o->next; c->nalloc++;
    return (void*)o;
}

/* free: the caller must pass the size class (or use kslab_alloc_sized pairs).
   Because plain free() can't know the class, we expose a sized free; the
   generic path stores a 1-byte class tag just below large allocations. */
static void kslab_free_sz(void* p,unsigned long n){
    if(!p) return;
    int i=slab_index(n);
    if(i<0){ kfree(p); return; }
    slab_cache* c=&g_slabs[i];
    slab_obj* o=(slab_obj*)p; o->next=c->freelist; c->freelist=o; c->nalloc--;
}

/* convenience: tagged alloc/free that remembers the size class itself.
   Layout: [u16 class][padding to objsize alignment][user data]. We keep it
   simple: allocate class+16 header room inside the next size class up. */
static void* kslab_talloc(unsigned long n){
    void* raw=kslab_alloc(n+8);
    if(!raw) return 0;
    *(unsigned long*)raw = n+8;                 /* remember requested class size */
    return (unsigned char*)raw+8;
}
static void kslab_tfree(void* p){
    if(!p) return;
    unsigned char* raw=(unsigned char*)p-8;
    unsigned long n=*(unsigned long*)raw;
    kslab_free_sz(raw,n);
}

/* stats for a 'slabinfo' command */
void slab_stats(char* out,int cap){
    char* o=out; const char* hdr="size  live  slabs\n";
    for(const char* h=hdr;*h&&o<out+cap-1;)*o++=*h++;
    for(int i=0;i<SLAB_NSIZES;i++){
        slab_cache* c=&g_slabs[i];
        /* size */ unsigned long v=c->objsize; char t[24]; int tn=0; if(!v)t[tn++]='0'; while(v){t[tn++]='0'+v%10;v/=10;} while(tn&&o<out+cap-1)*o++=t[--tn];
        *o++=' ';*o++=' ';
        v=c->nalloc; tn=0; if(!v)t[tn++]='0'; while(v){t[tn++]='0'+v%10;v/=10;} while(tn&&o<out+cap-1)*o++=t[--tn];
        *o++=' ';*o++=' ';
        v=c->npages; tn=0; if(!v)t[tn++]='0'; while(v){t[tn++]='0'+v%10;v/=10;} while(tn&&o<out+cap-1)*o++=t[--tn];
        if(o<out+cap-1)*o++='\n';
    }
    *o=0;
}
