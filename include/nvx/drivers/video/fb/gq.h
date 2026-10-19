#ifndef NVX_DRIVERS_VIDEO_GQ_H
#define NVX_DRIVERS_VIDEO_GQ_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/video/intel/gpukern.h"

#define GQ_MAXS 48
#define GQ_MAXOPS 1600
#define GQ_EMPTY 0xFF000000u

typedef struct { u32* px; u64 gfx; u32 w, h, pitch; int used; } gsurf;

typedef struct {
    u32* ring; u32 ring_cap;
    u32* st; u64 st_g; u32 st_size;
    u64 kern_g; u32 koff[GK_N];
    u64 scratch_g;
    int (*submit)(u32 ndw);
    void (*flush)(const void* p,u64 n);
    u64 (*now)(void);
} gqhw;

#define GQM_OFF 0
#define GQM_CPU 1
#define GQM_GPU 2

extern int g_gq_mode, g_gq_wflush, g_gq_tlb, g_gq_serial;
extern char g_gq_diag[160];
extern volatile int g_gq_inb;
extern gsurf g_gs[GQ_MAXS];
extern gqhw g_gqhw;
extern u32 g_gq_ops, g_gq_stalls, g_gq_syncs, g_gq_batches, g_gq_fail, g_gq_last_us, g_gq_frame_ops, g_gq_frame_stalls, g_gq_frame_syncs;

int gq_surf(u32* px,u64 gfx,u32 w,u32 h,u32 pitch);
void gq_surf_update(int s,u32 w,u32 h);
void gq_surf_free(int s);
void gq_fill(int d,int x,int y,int w,int h,u32 c);
void gq_blend(int d,int x,int y,int w,int h,u32 c,int a);
void gq_mask(int d,int x,int y,int w,int h,u32 c,int a,int m,int mx,int my);
void gq_copy(int d,int x,int y,int w,int h,int s,int sx,int sy,int a,int key);
void gq_copym(int d,int x,int y,int w,int h,int s,int sx,int sy,int m,int mx,int my,int a,int key);
void gq_grad(int d,int x,int y,int w,int h,u32 c0,u32 c1,int vert);
void gq_overc(int d,int x,int y,int w,int h,int s,int sx,int sy,int a);
void gq_overm(int d,int x,int y,int w,int h,int s,int sx,int sy,int m,int mx,int my,int a);
void gq_tint(int d,int x,int y,int w,int h);
void gq_blur1(int d,int x,int y,int w,int h,int s,int sx,int sy,int vert);
int gq_sync(void);
int gq_pending(void);
void gq_frame_mark(void);
u32 gq_bl(u32 d,u32 s,u32 a);
typedef void (*gq_prefn)(int d,int x,int y,int w,int h,int s,int m);
void gq_set_pre(gq_prefn f);
extern u32 g_sgen[GQ_MAXS]; extern u64 g_ophash, g_gq_wait_ns; extern int g_hash_d; extern u32 g_gq_submits;
void gq_set_key(u32 k);
extern int g_cap_why, g_cap_wk;
void gq_bump(int s);
void gq_cap_begin(int s,void* buf,int max,int cx,int cy,int cw,int ch);
int gq_cap_end(void);
int gq_op_size(void);
int gq_op_same(const void* a,const void* b);
void gq_op_rect(const void* p,int* x,int* y,int* w,int* h);
void gq_replay(const void* buf,int n,int cx,int cy,int cw,int ch);
void gq_touch_reset(int s);
int gq_touch_get(int s,int* x0,int* y0,int* x1,int* y1);
void gq_abort(void);
void gq_srec_begin(int scene,void* a,void* b,int max);
int gq_srec_end(int* x,int* y,int* w,int* h);
void gq_srec_invalidate(void);
void gq_ext_mark(int s,int x,int y,int w,int h);
extern int g_scene_quiet; extern u32 g_scene_wr;
extern int g_sr_why, g_sr_full, g_sr_part, g_sr_none, g_sr_ops, g_sr_run;
void gq_log_begin(int d);
void gq_log_end(void);
int gq_log_find(int x,int y,char* out,int max);
int gq_selftest(const int* sid,u32** cpu,int nd,int msid,u32 seed,int n,char* why);

#endif
