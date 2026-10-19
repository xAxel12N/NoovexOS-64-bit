#ifndef NVX_DRIVERS_VIDEO_GFX_H
#define NVX_DRIVERS_VIDEO_GFX_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/video/fb/gq.h"

#define GFX_OFF 0
#define GFX_GPU 1
#define GFX_EMU 2

extern int g_gfx, g_gfx_fx, g_gfx_ok, g_rt, gs_scene, gs_bg, gs_nc;
extern int g_gfx_present;
extern u32 g_gfx_frames, g_gfx_frame_us, g_gfx_layers, g_gfx_layer_mb;
extern u64 g_gfx_pres_ns; extern u32 g_gfx_cpu_us, g_gfx_gpu_us, g_gfx_pres_us, g_gfx_subs, g_gfx_text_ops, g_gfx_text_glyphs;
extern int g_lay_abort_win, g_lay_abort_why, g_lay_abort_k;
extern u32 g_lay_full, g_lay_part, g_lay_same, g_lay_abort, g_blur_hit, g_blur_miss, g_shd_fast, g_shd_slow;
extern char g_gfx_why[64];

typedef int (*gfx_alloc_fn)(u32 w,u32 h,u32 pitch,u32** px);
int gfx_start(int mode,gfx_alloc_fn al,u32* scene_px,int scene_sid);
void gfx_stop(const char* why);
void gfx_sync(void);
void gfx_px(int x,int y,u32 c,int a);
void gfx_glyph(int x,int y,char ch,u32 c,int s,int mode);
void gfx_round(int x,int y,int w,int h,int r,int m,u32 c,int a);
void gfx_shadow(int x,int y,int w,int h,int r);
void gfx_circle(int cx,int cy,int r,u32 c,int a);
void gfx_glass(int x,int y,int w,int h,int r,u32 tint,int a,int iters);
void gfx_blur(int x,int y,int w,int h,int r,int iters);
void gfx_frame_begin(void);
void gfx_frame_end(void);
void gfx_window(int i);
void gfx_ghosts(void);
int gfx_anim_active(void);
void gfx_win_event(int i,int kind);
void gfx_scroll_area(int i,int x,int y,int w,int h,int pos);
int gfx_layer_of(int i);
int gfx_scene_end(int* x,int* y,int* w,int* h);
void gfx_scene_settle(void);
void gfx_invalidate_all(void);
extern int g_gfx_srec, g_wall_light;
void gfx_wall_light(void);

#endif
