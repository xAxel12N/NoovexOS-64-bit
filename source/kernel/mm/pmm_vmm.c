#include "include/nvx/base.h"
#include "include/nvx/mm/pmm_vmm.h"
#include "include/nvx/mm/dmawin.h"
#include "include/nvx/mm/heap.h"
#include "include/nvx/arch/x86_64/smp_sched.h"
#include "include/nvx/apps/files_classic.h"
/* ================= STAGE 1: physical page-frame allocator =================
   Manages 4 KB frames in the free RAM gap between the image-scratch region and
   the heap (160 MB .. 256 MB). Identity-mapped, so the kernel can build page
   tables in these frames directly. Needs >=~272 MB RAM to enable. */
   /* fwd (defined after detect_ram_mb) */
     /* fwd (defined later) */
   /* fwd (framebuffer dims, defined later) */
    /* fwd (for the presentation cmd) */
   /* Lua 5.5 interpreter */

mmreg g_mmr[MMR_MAX]; int g_mmr_n=0, g_mmr_over=0;
void mm_load(void){
    g_mmr_n=0; g_mmr_over=0;
    if(*(volatile u32*)0x502CULL!=0x49464555u){ const volatile u8* q=(const volatile u8*)0x7000ULL; if(*(const volatile u32*)q!=0x4558564Eu)return;
        u32 n=*(const volatile u32*)(q+4); if(n>40)n=40;
        for(u32 i=0;i<n;i++){ mmreg r; u64 b=*(const volatile u64*)(q+8+i*24), l=*(const volatile u64*)(q+16+i*24); u32 t=*(const volatile u32*)(q+24+i*24);
            if(!l||b+l<b)continue; r.s=b; r.e=b+l; r.t=t==1?7u:t==3?9u:t==4?10u:t==5?8u:0u;
            if(g_mmr_n>=MMR_MAX)break; int j=g_mmr_n; while(j>0&&g_mmr[j-1].s>r.s){ g_mmr[j]=g_mmr[j-1]; j--; } g_mmr[j]=r; g_mmr_n++; }
        return; }
    const volatile u8* p=(const volatile u8*)0x7000ULL; if(*(const volatile u32*)p!=0x4D58564Eu)return;
    u32 n=*(const volatile u32*)(p+4); g_mmr_over=(int)(n>>31); n&=0x7FFFFFFFu; if(n>MMR_MAX)n=MMR_MAX;
    for(u32 i=0;i<n;i++){ mmreg r; r.s=*(const volatile u64*)(p+8+i*24); r.e=*(const volatile u64*)(p+16+i*24); r.t=*(const volatile u32*)(p+24+i*24); if(r.e<=r.s)continue;
        int j=g_mmr_n; while(j>0&&g_mmr[j-1].s>r.s){ g_mmr[j]=g_mmr[j-1]; j--; } g_mmr[j]=r; g_mmr_n++; }
}
int mm_known(void){ return g_mmr_n>0; }
static int mm_ok_type(u32 t){ return t==7||t==4; }
static u64 mm_run_end(u64 c,int clean){ u64 st=c; for(int k=0;k<=g_mmr_n;k++){ int mv=0; for(int i=0;i<g_mmr_n;i++) if((clean?g_mmr[i].t==7:mm_ok_type(g_mmr[i].t))&&g_mmr[i].s<=c&&g_mmr[i].e>c){ c=g_mmr[i].e; mv=1; } if(!mv)break; } return c>st?c:0; }
u64 mm_usable_end(u64 c){ return mm_run_end(c,0); }
int mm_clean_region(u64 lo,u64 hi,u64* bs,u64* be){ u64 best=0,bb=0; for(int i=0;i<g_mmr_n;i++){ if(g_mmr[i].t!=7)continue; u64 s=g_mmr[i].s; if(s<lo)s=lo; if(s>=hi)continue; if(mm_run_end(s,1)==0)continue; u64 e=mm_run_end(s,1); if(e>hi)e=hi; s=(s+0xFFFFFULL)&~0xFFFFFULL; if(e>s&&e-s>best){ best=e-s; bb=s; } } if(!best)return 0; *bs=bb; *be=bb+best; return 1; }
int mm_first_bad_ex(u64 s,u64 e,int clean,u64* bs,u64* be,u32* bt){
    if(!mm_known())return 0; u64 c=s;
    while(c<e){ u64 u=mm_run_end(c,clean); if(u){ c=u; continue; }
        u32 t=0xFFFFFFFFu; u64 en=e; for(int i=0;i<g_mmr_n;i++) if(g_mmr[i].s<=c&&g_mmr[i].e>c){ t=g_mmr[i].t; en=g_mmr[i].e; break; }
        if(t==0xFFFFFFFFu) for(int i=0;i<g_mmr_n;i++) if(g_mmr[i].s>c&&g_mmr[i].s<en)en=g_mmr[i].s;
        *bs=c; *be=en<e?en:e; *bt=t; return 1; }
    return 0; }
int mm_first_bad(u64 s,u64 e,u64* bs,u64* be,u32* bt){ return mm_first_bad_ex(s,e,0,bs,be,bt); }
static int mm_is_clean(u64 s,u64 e){ u64 a,b; u32 t; return !mm_first_bad_ex(s,e,1,&a,&b,&t); }
int mm_is_usable(u64 s,u64 e){ u64 a,b; u32 t; return !mm_first_bad(s,e,&a,&b,&t); }
static u64 mm_skip_bad(u64 c,u64 n,u64 top){ for(int k=0;k<256&&c+n<=top;k++){ u64 bs,be; u32 bt; if(!mm_first_bad_ex(c,top,1,&bs,&be,&bt)||bs>=c+n)return c; c=(be+4095)&~4095ULL; } return top; }
const char* mm_tname(u32 t){ switch(t){ case 7: return "usable"; case 4: return "usable (used by firmware before boot)"; case 0: return "reserved"; case 5: return "runtime code"; case 6: return "runtime data"; case 8: return "unusable"; case 9: return "ACPI tables"; case 10: return "ACPI NVS"; case 11: return "MMIO"; case 12: return "MMIO port"; case 13: return "PAL code"; case 14: return "persistent"; case 0xFFFFFFFFu: return "not in map"; default: return "other"; } }
unsigned long long dma_cur=DMA_ARENA_BASE;
void* dma_alloc(unsigned long n){ n=(n+63)&~63UL; u64 c=mm_skip_bad(dma_cur,n,DMA_ARENA_TOP); if(c+n>DMA_ARENA_TOP)return 0; dma_cur=c; void* p=(void*)(unsigned long)dma_cur; dma_cur+=n; unsigned char* z=(unsigned char*)p; for(unsigned long i=0;i<n;i++)z[i]=0; return p; }
void* dma_alloc_pages(unsigned long pages){ unsigned long n=pages*4096UL; u64 c=(dma_cur+4095)&~4095ULL; c=mm_skip_bad(c,n,DMA_ARENA_TOP); if(c+n>DMA_ARENA_TOP)return 0; dma_cur=c+n; unsigned char* z=(unsigned char*)(unsigned long)c; for(unsigned long i=0;i<n;i++)z[i]=0; return z; }
u32 g_devbuf __attribute__((section(".data")))=DMW_FIXED; static int g_dmw_moved=0;
void dmw_init(void){ u64 a=(dma_cur+0x1FFFFFULL)&~0x1FFFFFULL; u64 c=mm_skip_bad(a,DMW_SIZE,DMA_ARENA_TOP);
    if(c+DMW_SIZE>DMA_ARENA_TOP||c+DMW_SIZE>0x100000000ULL){ g_dmw_moved=-1; return; }
    unsigned char* z=(unsigned char*)(unsigned long)c; for(unsigned long i=0;i<DMW_SIZE;i++)z[i]=0;
    dma_cur=c+DMW_SIZE; g_devbuf=(u32)c; g_dmw_moved=1; }
const char* dmw_where(void){ return g_dmw_moved==1?"device buffers in the checked DMA arena":g_dmw_moved<0?"device buffers at the fixed address (DMA arena full)":"device buffers at the fixed address"; }
u8  pf_bitmap[(PF_MAXN+7)/8];
int pf_ready=0; static u32 pf_used=0; static u16 pf_ref[PF_MAXN]; static u32 pf_hint=0;
u32 pf_n=PF_N; u64 pf_x_base=0, pf_x_top=0;
#define HP_MAXR 32
static struct { u64 b,t; u32 first; } hp_r[HP_MAXR]; static int hp_nr=0; u32 hp_n=0; static u64* hp_bits=0; static u16* hp_ref=0; static u32 hp_used=0, hp_hint=0; u64 g_hi_mapped=0;
int pf_idx(u64 pa){ if(pa>=PF_BASE&&pa<PF_TOP)return (int)((pa-PF_BASE)>>12); if(pa>=pf_x_base&&pa<pf_x_top)return (int)(PF_N+((pa-pf_x_base)>>12));
    if(pa>=0x100000000ULL) for(int r=0;r<hp_nr;r++) if(pa>=hp_r[r].b&&pa<hp_r[r].t)return (int)(pf_n+hp_r[r].first+(u32)((pa-hp_r[r].b)>>12));
    return -1; }
static u64 hp_pa(u32 j){ int lo=0,hi=hp_nr-1; while(lo<hi){ int m=(lo+hi+1)/2; if(hp_r[m].first<=j)lo=m; else hi=m-1; } return hp_r[lo].b+(u64)(j-hp_r[lo].first)*4096ULL; }
u64 pf_pa(u32 i){ if(i>=pf_n)return hp_pa(i-pf_n); return i<PF_N?PF_BASE+(u64)i*4096ULL:pf_x_base+(u64)(i-PF_N)*4096ULL; }
static int mm_ram_type(u32 t){ return t==7||t==4; }
static int hi_pd_mapped(u64* pdpt,u64 pa){ u64 e=pdpt[(pa>>30)&511]; if(!(e&1))return 0; if(e&0x80ULL)return 1; u64* pd=(u64*)(e&PTE_ADDR); u64 d=pd[(pa>>21)&511]; return (d&1)&&(d&0x80ULL)&&((d&0x000FFFFFFFE00000ULL)==(pa&~0x1FFFFFULL)); }
static void mm_map_high(void){
    if(!mm_known())return; u64 cr3=cpu_get_pt(); u64* pml4=(u64*)(cr3&~0xFFFULL); if(!(pml4[0]&1))return; u64* pdpt=(u64*)(pml4[0]&PTE_ADDR);
    static u8 need[512]; int cnt=0;
    for(int i3=4;i3<512;i3++){ need[i3]=0; if(pdpt[i3]&1)continue; u64 s=(u64)i3<<30, e=s+0x40000000ULL;
        for(int i=0;i<g_mmr_n;i++) if(mm_ram_type(g_mmr[i].t)&&g_mmr[i].s<e&&g_mmr[i].e>s){ need[i3]=1; cnt++; break; } }
    if(!cnt)return; u8* raw=(u8*)kmalloc((unsigned long)cnt*4096UL+4096UL); if(!raw)return; u64 pdb=((u64)raw+4095ULL)&~4095ULL;
    for(int i3=4;i3<512;i3++){ if(!need[i3])continue; u64* pd=(u64*)pdb; pdb+=4096; u64 s=(u64)i3<<30;
        for(int k=0;k<512;k++){ u64 cs=s+((u64)k<<21), ce=cs+0x200000ULL; int ram=0; for(int i=0;i<g_mmr_n;i++) if(mm_ram_type(g_mmr[i].t)&&g_mmr[i].s<ce&&g_mmr[i].e>cs){ ram=1; break; }
            pd[k]=ram?(cs|0x83ULL):0; if(ram&&ce>g_hi_mapped)g_hi_mapped=ce; }
        pdpt[i3]=((u64)pd)|3ULL; }
    cpu_set_pt(cr3); }
static void hp_add(u64 b,u64 t){ if(t<=b)return; if(hp_nr>0&&hp_r[hp_nr-1].t==b){ hp_r[hp_nr-1].t=t; return; } if(hp_nr>=HP_MAXR)return; hp_r[hp_nr].b=b; hp_r[hp_nr].t=t; hp_nr++; }
static void hp_init(void){
    hp_nr=0; hp_n=0; hp_used=0; hp_hint=0; if(!mm_known())return; mm_map_high();
    u64 cr3=cpu_get_pt(); u64* pml4=(u64*)(cr3&~0xFFFULL); if(!(pml4[0]&1))return; u64* pdpt=(u64*)(pml4[0]&PTE_ADDR);
    for(int i=0;i<g_mmr_n;i++){ if(g_mmr[i].t!=7)continue; u64 s=g_mmr[i].s, e=g_mmr[i].e; if(e<=0x100000000ULL)continue; if(s<0x100000000ULL)s=0x100000000ULL; if(e>0x8000000000ULL)e=0x8000000000ULL;
        s=(s+0x1FFFFFULL)&~0x1FFFFFULL; e&=~0x1FFFFFULL;
        for(u64 c=s;c<e;c+=0x200000ULL){ if(!hi_pd_mapped(pdpt,c)||!mm_is_clean(c,c+0x200000ULL))continue; hp_add(c,c+0x200000ULL); } }
    if(!hp_nr)return; u64 tot=0; for(int r=0;r<hp_nr;r++)tot+=(hp_r[r].t-hp_r[r].b)>>12;
    u64 meta=((tot+63)/64)*8+tot*2; meta=(meta+0x1FFFFFULL)&~0x1FFFFFULL;
    int big=0; for(int r=1;r<hp_nr;r++) if(hp_r[r].t-hp_r[r].b>hp_r[big].t-hp_r[big].b)big=r; if(hp_r[big].t-hp_r[big].b<=meta){ hp_nr=0; return; }
    u8* m=(u8*)hp_r[big].b; hp_r[big].b+=meta;
    u32 f=0; for(int r=0;r<hp_nr;r++){ hp_r[r].first=f; f+=(u32)((hp_r[r].t-hp_r[r].b)>>12); } hp_n=f;
    hp_bits=(u64*)m; hp_ref=(u16*)(m+((hp_n+63)/64)*8); u64* z=(u64*)m; u64 zw=(((hp_n+63)/64)*8+(u64)hp_n*2+7)/8; for(u64 i=0;i<zw;i++)z[i]=0;
    if(hp_n&63)hp_bits[hp_n/64]=~0ULL<<(hp_n&63);
    { u32 probe[3]={0,hp_n/2,hp_n-1}; for(int k=0;k<3;k++){ volatile u64* q=(volatile u64*)hp_pa(probe[k]); u64 o=q[0]; q[0]=0x5A17C0DE00000000ULL^probe[k]; if(q[0]!=(0x5A17C0DE00000000ULL^probe[k])){ hp_n=0; hp_nr=0; return; } q[0]=o; } } }
static u64 hp_alloc(void){ if(!hp_n||hp_used>=hp_n)return 0; u32 nw=(hp_n+63)/64;
    for(u32 k=0;k<nw;k++){ u32 w=hp_hint+k; if(w>=nw)w-=nw; u64 v=hp_bits[w]; if(v==~0ULL)continue;
        u32 b=(u32)__builtin_ctzll(~v); u32 j=w*64+b; if(j>=hp_n)continue; hp_bits[w]=v|(1ULL<<b); hp_ref[j]=1; hp_used++; hp_hint=w;
        u64 pa=hp_pa(j); u64* z=(u64*)pa; for(int q=0;q<512;q++)z[q]=0; return pa; }
    return 0; }
u64 hp_alloc_run(u32 pages){ if(!pf_ready||!hp_n||pages==0)return 0;
    for(int r=0;r<hp_nr;r++){ u32 lo=hp_r[r].first, hi=(r+1<hp_nr?hp_r[r+1].first:hp_n); if(hi-lo<pages)continue;
        for(u32 s=lo;s+pages<=hi;){ u32 k=0; while(k<pages&&!((hp_bits[(s+k)>>6]>>((s+k)&63))&1))k++;
            if(k==pages){ for(u32 i=0;i<pages;i++){ u32 j=s+i; hp_bits[j>>6]|=(1ULL<<(j&63)); hp_ref[j]=1; } hp_used+=pages;
                u64 pa=hp_pa(s); u64* z=(u64*)pa; u64 nw=(u64)pages*512ULL; for(u64 w=0;w<nw;w++)z[w]=0; return pa; }
            s+=k+1; } }
    return 0; }
static int hp_isset(u32 j){ return (int)((hp_bits[j>>6]>>(j&63))&1); }
u32 pframe_hi_count(void){ return hp_n; }
u64 mm_hi_mapped_top(void){ return g_hi_mapped; }
u64 mm_ram_total(void){ u64 t=0; for(int i=0;i<g_mmr_n;i++) if(mm_ram_type(g_mmr[i].t))t+=g_mmr[i].e-g_mmr[i].s; return t; }
int pf_owned(u64 pa){ return pf_idx(pa)>=0; }
static void pframe_grow_from_heap(void){
    u64 hs=heap_top-heap_cur; if(hs<0x10000000ULL)return;
    u64 x=(hs/2)&~0x1FFFFFULL; u64 cap=(u64)(PF_MAXN-PF_N)*4096ULL; if(x>cap)x=cap;
    u64 top=heap_top&~0xFFFULL, base=(top-x)&~0x1FFFFFULL; if(base<=heap_cur+0x8000000ULL)return;
    heap_top=base; pf_x_base=base; pf_x_top=top; pf_n=PF_N+(u32)((top-base)>>12);
}
void pframe_init(void){ pf_ready=(g_ram_mb>=500); if(pf_ready)pframe_grow_from_heap();
    for(u32 i=0;i<(pf_n+7)/8;i++)pf_bitmap[i]=0; pf_used=0; pf_hint=0;
    for(u32 i=0;i<pf_n;i++){ u64 pa=pf_pa(i); if(!mm_is_clean(pa,pa+4096ULL)){ pf_bitmap[i>>3]|=(u8)(1u<<(i&7)); pf_used++; } }
    if(pf_ready)hp_init(); }
u64 pframe_alloc(void){ if(!pf_ready)return 0;
    { u64 h=hp_alloc(); if(h)return h; }
    if(pf_used<pf_n) for(u32 k=0;k<pf_n;k++){ u32 i=pf_hint+k; if(i>=pf_n)i-=pf_n;
        if(!(pf_bitmap[i>>3]&(1u<<(i&7)))){ pf_bitmap[i>>3]|=(u8)(1u<<(i&7)); pf_used++; pf_ref[i]=1; pf_hint=i+1>=pf_n?0:i+1;
            u64 pa=pf_pa(i); u64* z=(u64*)pa; for(int j=0;j<512;j++)z[j]=0; return pa; } }
    return 0; }
void pframe_free(u64 pa){ int ii=pf_idx(pa); if(ii<0)return; u32 i=(u32)ii;
    if(i>=pf_n){ u32 j=i-pf_n; if(!hp_isset(j))return; if(hp_ref[j]>1){ hp_ref[j]--; return; } hp_ref[j]=0; hp_bits[j>>6]&=~(1ULL<<(j&63)); if(hp_used)hp_used--; if((j>>6)<hp_hint)hp_hint=j>>6; return; }
    if(!(pf_bitmap[i>>3]&(1u<<(i&7))))return;
    if(pf_ref[i]>1){ pf_ref[i]--; return; }
    pf_ref[i]=0; pf_bitmap[i>>3]&=(u8)~(1u<<(i&7)); if(pf_used)pf_used--; if(i<pf_hint)pf_hint=i; }
void pframe_ref(u64 pa){ int ii=pf_idx(pa); if(ii<0)return; u32 i=(u32)ii; if(i>=pf_n){ u32 j=i-pf_n; if(hp_isset(j)&&hp_ref[j]<0xFFFF)hp_ref[j]++; return; } if((pf_bitmap[i>>3]&(1u<<(i&7)))&&pf_ref[i]<0xFFFF)pf_ref[i]++; }
static u32 pframe_refs(u64 pa){ int ii=pf_idx(pa); if(ii<0)return 1; u32 i=(u32)ii; if(i>=pf_n){ u32 j=i-pf_n; return hp_isset(j)?hp_ref[j]:0; } return (pf_bitmap[i>>3]&(1u<<(i&7)))?pf_ref[i]:0; }
u32 pframe_refcount(u64 pa){ return pframe_refs(pa); }
u32 pframe_free_count(void){ return pf_ready?(pf_n-pf_used)+(hp_n-hp_used):0; }
/* ================= STAGE 2: virtual memory manager (paging) =================
   Per-address-space page tables (PML4->PDPT->PD->PT). New spaces copy the
   kernel's PML4 entries so kernel code/data/heap stay mapped after a CR3 switch;
   new per-space mappings use high PML4 slots that the kernel map doesn't touch. */
u64 vm_new_pml4(void){ u64 p=pframe_alloc(); if(!p)return 0; u64* t=(u64*)p;
    u64 cr3; cr3=cpu_get_pt(); u64* k=(u64*)(cr3&~0xFFFULL);
    for(int i=0;i<512;i++)t[i]=k[i];   /* share the kernel mappings */
    t[8]=0;
    return p; }
u64* vm_walk(u64 pml4pa,u64 v,int create){
    if(v<0x8000000000ULL||v>=0x800000000000ULL)return 0;
    u64* pml4=(u64*)pml4pa; int i4=(int)((v>>39)&0x1FF),i3=(int)((v>>30)&0x1FF),i2=(int)((v>>21)&0x1FF),i1=(int)((v>>12)&0x1FF);
    if(!(pml4[i4]&1)){ if(!create)return 0; u64 f=pframe_alloc(); if(!f)return 0; pml4[i4]=f|0x7ULL; }
    u64* pdpt=(u64*)(pml4[i4]&0x000FFFFFFFFFF000ULL);
    if(!(pdpt[i3]&1)){ if(!create)return 0; u64 f=pframe_alloc(); if(!f)return 0; pdpt[i3]=f|0x7ULL; }
    if(pdpt[i3]&0x80ULL)return 0;
    u64* pd=(u64*)(pdpt[i3]&0x000FFFFFFFFFF000ULL);
    if(!(pd[i2]&1)){ if(!create)return 0; u64 f=pframe_alloc(); if(!f)return 0; pd[i2]=f|0x7ULL; }
    if(pd[i2]&0x80ULL)return 0;
    u64* pt=(u64*)(pd[i2]&0x000FFFFFFFFFF000ULL);
    return &pt[i1]; }
int vm_map(u64 pml4,u64 v,u64 pa,u64 flags){ u64* e=vm_walk(pml4,v,1); if(!e)return -1;
    *e=(pa&~0xFFFULL)|flags|1ULL; cpu_flush_page(v); return 0; }

int vm_map_lazy(u64 pml4,u64 v){ u64* e=vm_walk(pml4,v,1); if(!e)return -1; if(*e&1)return 0; *e=PTE_LAZY; return 0; }
int vm_pte_used(u64 e){ return (e&1)||(e&PTE_LAZY); }
int vm_fault_user(u64 cr3,u64 va,int write){
    u64* e=vm_walk(cr3&~0xFFFULL,va&~0xFFFULL,0); if(!e)return -1;
    if(!(*e&1)){
        if(!(*e&PTE_LAZY))return -1;
        u64 f=pframe_alloc(); if(!f)return -2;
        *e=f|0x7ULL; cpu_flush_page(va&~0xFFFULL); return 0; }
    if(!(*e&4))return -1;
    if(write&&!(*e&2)){
        if(!(*e&PTE_COW))return -1;
        u64 pa=*e&PTE_ADDR;
        if(pframe_refs(pa)<=1){ *e=pa|0x7ULL; cpu_flush_page(va&~0xFFFULL); return 0; }
        u64 f=pframe_alloc(); if(!f)return -2;
        u8* d=(u8*)f; const u8* s=(const u8*)pa; for(int b=0;b<4096;b++)d[b]=s[b];
        *e=f|0x7ULL; cpu_flush_page(va&~0xFFFULL); smp_tlb_sync(cr3); pframe_free(pa); return 0; }
    return 0;
}
static void vm_unmap(u64 pml4,u64 v){ u64* e=vm_walk(pml4,v,0); if(e){ *e=0; cpu_flush_page(v); } }
/* proves isolation: same virtual addr -> different physical frames in two spaces.
   returns 0=ok(isolated), 1=no frame pool, 2=alloc fail, 3=isolation FAILED */
int vm_selftest(u32* r1,u32* r2){
    if(!pf_ready) return 1;
    u64 cr3o; cr3o=cpu_get_pt();
    u64 sp1=vm_new_pml4(), sp2=vm_new_pml4(); if(!sp1||!sp2) return 2;
    u64 fa=pframe_alloc(), fb=pframe_alloc(); if(!fa||!fb) return 2;
    u64 V=0x8000000000ULL;   /* 512 GB: PML4 slot 1, untouched by the kernel map -> per-space */
    if(vm_map(sp1,V,fa,0x3ULL)||vm_map(sp2,V,fb,0x3ULL)) return 2;
    cpu_set_pt(sp1); *(volatile u32*)V=0xAAAA1111u;
    cpu_set_pt(sp2); *(volatile u32*)V=0xBBBB2222u;
    cpu_set_pt(sp1); u32 a=*(volatile u32*)V;
    cpu_set_pt(sp2); u32 b=*(volatile u32*)V;
    cpu_set_pt(cr3o);   /* back to the kernel space */
    if(r1)*r1=a; if(r2)*r2=b;
    /* free the frames + tables we used (leak the intermediate tables for simplicity of the test) */
    pframe_free(fa); pframe_free(fb);
    return (a==0xAAAA1111u && b==0xBBBB2222u)?0:3;
}