#ifndef NVX_GUI_OUTPUTS_H
#define NVX_GUI_OUTPUTS_H

#include "include/nvx/base.h"

#define OUT_MAX 2
typedef struct{ int x,y,w,h; } out_rect;
extern int g_disp_extend, g_out_n, g_out_arr, g_snap_o;
enum{ OUT_RIGHT, OUT_LEFT, OUT_ABOVE, OUT_BELOW, OUT_ARR_N };
extern out_rect g_out[OUT_MAX];
void out_layout(u32 w1,u32 h1,int have2,u32 w2,u32 h2,int extend);
int out_count(void);
int out_desk_w(void);
int out_desk_h(void);
int out_at(int x,int y);
int out_of_rect(int x,int y,int w,int h);
void out_clamp_pt(int* x,int* y);
int out_place(int* x,int* y,int w,int h,int o);
int out_move_rect(int* x,int* y,int w,int h,int from,int to);
void out_max_rect(int o,int top,int bot,int* x,int* y,int* w,int* h);
int out_desk_x0(void);
int out_desk_y0(void);
int out_on2(int x,int y);
void out_mouse_clamp(int* x,int* y,int w,int h);
void out_drag_clamp(int* nx,int* ny,int ww,int px,int py,int w,int h,int th);
void out_resize_win(int* x,int* y,int* w,int* h,int max,int rest,u32 ow,u32 oh,u32 nw,u32 nh);
int out_resize_pt(int* x,int* y,u32 ow,u32 oh,u32 nw,u32 nh);
void out_area(int x,int y,int w,int h,int dw,int dh,out_rect* r);
int out_snap_ok(int px,int py,int s);
int out_local_shift(int x,int y,int w,int h,int* dx,int* dy);
void out_sync(void);
int out1_skip_win(int i);
int out_enter(int i,int* px,int* py);
void out_leave(int i,int k,int* px,int* py);
void out2_cursor(void);
int display_arrange(int a);
int display_extend_apply(int on);
int blit_extend2(void);
int display_mode_set(int extend);
int display_move_win(int i,int o);
void display_mode_remember(int on);
void display_cfg_tick(void);
int display_hotplug(int present);
int display_hpd(int present);
int display_hpd_s1(int present);
void display_sizes(u32* w,u32* h,u32* w2,u32* h2);
extern int g_ctx2;
int out_ctx_on2(void);
int out_ctx_click(int px,int py);
void out_ctx_bounds(int* w,int* h);
void out_desk_drop(int* x,int* y,int w,int h);
int out_snap_zone(int px,int py,int w,int h,int* o);
void out_snap_rect(int x,int y,int ww,int wh,int o,int w,int h,int* bx,int* by,int* bw,int* bh);
extern int g_out_mg, g_out_top, g_out_bot;
void out_work_reserve(int mg,int top,int bot);

#endif
