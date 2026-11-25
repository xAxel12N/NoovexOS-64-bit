#ifndef NVX_DRIVERS_VIDEO_AMD_SDMA_GART_H
#define NVX_DRIVERS_VIDEO_AMD_SDMA_GART_H
#include "drivers/video/amd/sdma/sdma_core.h"

#define AGT_PTE_FLAGS (1ULL|(1ULL<<1)|(1ULL<<2)|(1ULL<<4)|(1ULL<<5)|(1ULL<<6)|(3ULL<<48))
#define AGT_MCMAX 0xFFFFFFFFFFFFULL

typedef struct { u32 gc; u32 fbb, fbt, fbo, agpt, agpb, sal, sah, l1, l2, l2c2, ctx, ptlo, stlo, enlo, ireq, iack; } agt_gen_t;
typedef struct { const asd_io_t* io; const agt_gen_t* g; u32 seg0; volatile u64* pt; u64 pt_mc; u32 npte;
    u64 fb_base, fb_top, fb_off, agp_bot, agp_top, sys_lo, sys_hi, start, end; u32 ctx_old, l1_old, flushes; int up; const char* why; } agt_t;

const agt_gen_t* agt_gen(u32 gc_ver);
u64 agt_pte(u64 pa);
int agt_place(agt_t* a);
int agt_flush(agt_t* a,u32 us);
int agt_enable(agt_t* a,u32 us);
void agt_disable(agt_t* a,u32 us);
int agt_map(agt_t* a,u32 first,const u64* pa,u32 n,u32 us);
u64 agt_mc(const agt_t* a,u32 page);
#endif
