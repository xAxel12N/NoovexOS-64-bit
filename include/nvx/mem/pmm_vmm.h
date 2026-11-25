#ifndef NVX_MM_PMM_VMM_H
#define NVX_MM_PMM_VMM_H

#include "include/nvx/base.h"

#define PF_BASE 0x0C000000ULL
#define PF_TOP  0x0E000000ULL
#define PF_N    ((u32)((PF_TOP-PF_BASE)/4096ULL))
#define PF_MAXN (PF_N+524288u)
#define DMA_ARENA_BASE 0x0B000000ULL
#define DMA_ARENA_TOP  0x0C000000ULL
typedef struct { u64 s,e; u32 t; } mmreg;
#define MMR_MAX 170
#define PTE_COW  0x200ULL
#define PTE_LAZY 0x400ULL
#define PTE_SHR  0x800ULL
#define PTE_FILE 0x100ULL
#define PTE_FH_SHIFT 32
#define PTE_FP_MASK 0x00000000FFFFF000ULL
static inline u64 vm_file_marker(u32 h,u32 pg,u64 bits){ return 0x400ULL|PTE_FILE|((u64)(pg&0xFFFFFu)<<12)|((u64)(h&0xFFFFFu)<<PTE_FH_SHIFT)|(bits&(0x0010000000000000ULL|0x0020000000000000ULL|0x8000000000000000ULL)); }
#define PTE_ADDR 0x000FFFFFFFFFF000ULL
#define PTE_PRO  0x0010000000000000ULL
#define PTE_PNO  0x0020000000000000ULL
#define PTE_NX   0x8000000000000000ULL
#define PTE_PBITS (PTE_PRO|PTE_PNO|PTE_NX)
#define PTE_SWP  0x80ULL
#define PTE_ANON 0x0040000000000000ULL
int swap_in(u64 e,u64 dst) __attribute__((weak)); int swap_read(u64 e,u64 dst) __attribute__((weak)); int swap_dup(u64 e) __attribute__((weak)); void swap_put(u64 e) __attribute__((weak));
u32 mem_reclaim(u32 want) __attribute__((weak)); u32 swap_pageout(u64 cr3,u64 a,u64 e) __attribute__((weak));
u64 swap_cycles(void) __attribute__((weak)); int swap_reserve_level(void) __attribute__((weak)); void swap_stats(u64* total_kb,u64* used_kb,u64* store_kb,u64* comp_kb) __attribute__((weak));
extern int g_vm_oom_why; extern u32 g_vm_frame_rounds; extern u64 g_vm_oom_fails;
static inline int pte_is_swap(u64 x){ return (x&(1ULL|PTE_SWP|PTE_LAZY|PTE_FILE))==(PTE_SWP|PTE_LAZY); }
extern int g_nx_on;
u64 vm_prot_bits(int prot);
u64 vm_live(u64 pa,u64 old);
u64 vm_pflags(u64 base,u64 bits);
int vm_map_lazy_p(u64 pml4,u64 v,u64 bits);
void cpu_nx_init(void);
u64 kva_to_pa(u64 va);

extern mmreg g_mmr[MMR_MAX];
extern int g_mmr_n , g_mmr_over;
extern unsigned long long dma_cur;
extern u8 pf_bitmap[(PF_MAXN+7)/8];
extern u32 pf_n;
extern u64 pf_x_base, pf_x_top;
extern int pf_ready;

extern const char* nvx_lua_run(const char*);
void mm_load(void);
int mm_known(void);
u64 mm_usable_end(u64 c);
int mm_clean_region(u64 lo,u64 hi,u64* bs,u64* be);
int mm_first_bad_ex(u64 s,u64 e,int clean,u64* bs,u64* be,u32* bt);
int mm_first_bad(u64 s,u64 e,u64* bs,u64* be,u32* bt);
int mm_is_usable(u64 s,u64 e);
const char* mm_tname(u32 t);
void* dma_alloc(unsigned long n);
void* dma_alloc_pages(unsigned long pages);
int pf_idx(u64 pa);
u64 pf_pa(u32 i);
int pf_owned(u64 pa);
void pframe_init(void);
u32 pframe_hi_count(void);
u64 mm_hi_mapped_top(void);
u64 mm_ram_total(void);
u64 pframe_alloc(void);
u64 hp_alloc_run(u32 pages);
void pframe_free(u64 pa);
u64 vm_skip(u64 pml4pa,u64 v);
void pframe_ref(u64 pa);
u32 pframe_free_count(void);
u64 vm_new_pml4(void);
u64* vm_walk(u64 pml4pa,u64 v,int create);
int vm_map(u64 pml4,u64 v,u64 pa,u64 flags);
int vm_map_lazy(u64 pml4,u64 v);
int vm_pte_used(u64 e);
int vm_fault_user(u64 cr3,u64 va,int write);
int vm_selftest(u32* r1,u32* r2);

#endif
