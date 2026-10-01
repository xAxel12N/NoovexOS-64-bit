/* ================= STAGE 1: physical page-frame allocator =================
   Manages 4 KB frames in the free RAM gap between the image-scratch region and
   the heap (160 MB .. 256 MB). Identity-mapped, so the kernel can build page
   tables in these frames directly. Needs >=~272 MB RAM to enable. */
static u32 g_ram_mb;   /* fwd (defined after detect_ram_mb) */
static volatile u64 g_ticks; static int disk_ok; static void idt_set128(void);   /* fwd (defined later) */
static u32 W, H;   /* fwd (framebuffer dims, defined later) */
static int fext_eq(const char*,const char*); static void pptx_load(int);   /* fwd (for the presentation cmd) */
extern const char* nvx_lua_run(const char*);   /* Lua 5.5 interpreter */
#define PF_BASE 0x0A000000ULL          /* 160 MB */
#define PF_TOP  0x10000000ULL          /* 256 MB (heap base) */
#define PF_N    ((u32)((PF_TOP-PF_BASE)/4096ULL))   /* 24576 frames */
#define DMA_ARENA_BASE 0x09000000ULL
#define DMA_ARENA_TOP  0x0A000000ULL
static unsigned long long dma_cur=DMA_ARENA_BASE;
static void* dma_alloc(unsigned long n){ n=(n+63)&~63UL; if(dma_cur+n>DMA_ARENA_TOP)return 0; void* p=(void*)(unsigned long)dma_cur; dma_cur+=n; unsigned char* z=(unsigned char*)p; for(unsigned long i=0;i<n;i++)z[i]=0; return p; }
static u8  pf_bitmap[(PF_N+7)/8];
static int pf_ready=0; static u32 pf_used=0;
static void pframe_init(void){ for(u32 i=0;i<sizeof(pf_bitmap);i++)pf_bitmap[i]=0; pf_used=0; pf_ready=(g_ram_mb>=272); }
static u64 pframe_alloc(void){ if(!pf_ready)return 0;
    for(u32 i=0;i<PF_N;i++) if(!(pf_bitmap[i>>3]&(1u<<(i&7)))){ pf_bitmap[i>>3]|=(u8)(1u<<(i&7)); pf_used++;
        u64 pa=PF_BASE+(u64)i*4096ULL; u64* z=(u64*)pa; for(int j=0;j<512;j++)z[j]=0; return pa; }
    return 0; }
static void pframe_free(u64 pa){ if(pa<PF_BASE||pa>=PF_TOP)return; u32 i=(u32)((pa-PF_BASE)/4096ULL);
    if(pf_bitmap[i>>3]&(1u<<(i&7))){ pf_bitmap[i>>3]&=(u8)~(1u<<(i&7)); if(pf_used)pf_used--; } }
static u32 pframe_free_count(void){ return pf_ready?(PF_N-pf_used):0; }
/* ================= STAGE 2: virtual memory manager (paging) =================
   Per-address-space page tables (PML4->PDPT->PD->PT). New spaces copy the
   kernel's PML4 entries so kernel code/data/heap stay mapped after a CR3 switch;
   new per-space mappings use high PML4 slots that the kernel map doesn't touch. */
static u64 vm_new_pml4(void){ u64 p=pframe_alloc(); if(!p)return 0; u64* t=(u64*)p;
    u64 cr3; __asm__ volatile("mov %%cr3,%0":"=r"(cr3)); u64* k=(u64*)(cr3&~0xFFFULL);
    for(int i=0;i<512;i++)t[i]=k[i];   /* share the kernel mappings */
    t[8]=0;
    return p; }
static u64* vm_walk(u64 pml4pa,u64 v,int create){
    u64* pml4=(u64*)pml4pa; int i4=(int)((v>>39)&0x1FF),i3=(int)((v>>30)&0x1FF),i2=(int)((v>>21)&0x1FF),i1=(int)((v>>12)&0x1FF);
    if(!(pml4[i4]&1)){ if(!create)return 0; u64 f=pframe_alloc(); if(!f)return 0; pml4[i4]=f|0x7ULL; }
    u64* pdpt=(u64*)(pml4[i4]&~0xFFFULL);
    if(!(pdpt[i3]&1)){ if(!create)return 0; u64 f=pframe_alloc(); if(!f)return 0; pdpt[i3]=f|0x7ULL; }
    u64* pd=(u64*)(pdpt[i3]&~0xFFFULL);
    if(!(pd[i2]&1)){ if(!create)return 0; u64 f=pframe_alloc(); if(!f)return 0; pd[i2]=f|0x7ULL; }
    u64* pt=(u64*)(pd[i2]&~0xFFFULL);
    return &pt[i1]; }
static int vm_map(u64 pml4,u64 v,u64 pa,u64 flags){ u64* e=vm_walk(pml4,v,1); if(!e)return -1;
    *e=(pa&~0xFFFULL)|flags|1ULL; __asm__ volatile("invlpg (%0)"::"r"(v):"memory"); return 0; }
static void vm_unmap(u64 pml4,u64 v){ u64* e=vm_walk(pml4,v,0); if(e){ *e=0; __asm__ volatile("invlpg (%0)"::"r"(v):"memory"); } }
/* proves isolation: same virtual addr -> different physical frames in two spaces.
   returns 0=ok(isolated), 1=no frame pool, 2=alloc fail, 3=isolation FAILED */
static int vm_selftest(u32* r1,u32* r2){
    if(!pf_ready) return 1;
    u64 cr3o; __asm__ volatile("mov %%cr3,%0":"=r"(cr3o));
    u64 sp1=vm_new_pml4(), sp2=vm_new_pml4(); if(!sp1||!sp2) return 2;
    u64 fa=pframe_alloc(), fb=pframe_alloc(); if(!fa||!fb) return 2;
    u64 V=0x8000000000ULL;   /* 512 GB: PML4 slot 1, untouched by the kernel map -> per-space */
    if(vm_map(sp1,V,fa,0x3ULL)||vm_map(sp2,V,fb,0x3ULL)) return 2;
    __asm__ volatile("mov %0,%%cr3"::"r"(sp1):"memory"); *(volatile u32*)V=0xAAAA1111u;
    __asm__ volatile("mov %0,%%cr3"::"r"(sp2):"memory"); *(volatile u32*)V=0xBBBB2222u;
    __asm__ volatile("mov %0,%%cr3"::"r"(sp1):"memory"); u32 a=*(volatile u32*)V;
    __asm__ volatile("mov %0,%%cr3"::"r"(sp2):"memory"); u32 b=*(volatile u32*)V;
    __asm__ volatile("mov %0,%%cr3"::"r"(cr3o):"memory");   /* back to the kernel space */
    if(r1)*r1=a; if(r2)*r2=b;
    /* free the frames + tables we used (leak the intermediate tables for simplicity of the test) */
    pframe_free(fa); pframe_free(fb);
    return (a==0xAAAA1111u && b==0xBBBB2222u)?0:3;
}