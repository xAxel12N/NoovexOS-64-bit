#ifndef NVX_DRIVERS_VIDEO_AMD_SDMA_CORE_H
#define NVX_DRIVERS_VIDEO_AMD_SDMA_CORE_H
#include "include/nvx/base.h"

#define ASD_RING_DW 4096u
#define ASD_RING_ORDER 12u
#define ASD_FENCE_OFF 0x4000u
#define ASD_RPTR_OFF 0x4010u
#define ASD_WPTR_OFF 0x4020u
#define ASD_TEST_OFF 0x5000u
#define ASD_AREA 0x10000u
#define ASD_MAXLIN 0x400000u

typedef struct { u32 (*rd)(void* c,u32 dw); void (*wr)(void* c,u32 dw,u32 v); u64 (*ns)(void* c); void* c; } asd_io_t;
typedef struct { u32 ver; const char* name; const char* fw; int v6; u32 rb_cntl, rb_base, rptr, wptr, poll_cntl, poll_hi, poll_lo, rpa_hi, rpa_lo, ib_cntl, db, db_off, minor, f32, cntl, utcl1, sem, status, uaddr, udata, resp_m, redo_m; } asd_gen_t;
typedef struct { const u8* th0; u32 n0; const u8* th1; u32 n1; u32 ver; int hv; const char* why; u32 b0, b1; } asd_fw_t;
typedef struct { const asd_io_t* io; const asd_gen_t* g; u32 seg0, seg1; volatile u32* mem; u64 mc; u32 wp, seq; u32 ops, timeouts, last_rptr; int up; const void* c_fw; int psp; } asd_t;

const asd_gen_t* asd_gen(u32 sdma_ver);
int asd_fw_parse(asd_fw_t* f,const u8* b,u32 n);
u32 asd_reg(const asd_t* e,u32 off);
int asd_start(asd_t* e,const asd_fw_t* f);
void asd_stop(asd_t* e);
int asd_pk_nop(u32* p,u32 n);
int asd_pk_fence(u32* p,u64 mc,u32 v);
int asd_pk_copy(u32* p,u64 src,u64 dst,u32 bytes);
int asd_pk_fill(u32* p,u64 dst,u32 v,u32 bytes);
int asd_pk_subwin(u32* p,u64 src,u32 spitch_px,u64 dst,u32 dpitch_px,u32 w,u32 h);
int asd_emit(asd_t* e,const u32* p,int n);
int asd_kick(asd_t* e,u32 us);
int asd_copy_rect(asd_t* e,u64 src,u32 spb,u64 dst,u32 dpb,u32 w,u32 h,u32 us);
int asd_fill_rect(asd_t* e,u64 dst,u32 dpb,u32 w,u32 h,u32 v,u32 us);
#endif
