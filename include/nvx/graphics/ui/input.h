#ifndef NVX_GUI_INPUT_H
#define NVX_GUI_INPUT_H

#include "include/nvx/base.h"

void nvxlc_window(const char* t,int w,int h);
void nvxlc_clear(u32 c);
void nvxlc_rect(int x,int y,int w,int h,u32 c);
void nvxlc_pixel(int x,int y,u32 c);
void nvxlc_text(int x,int y,const char* s,u32 c);
void nvxlc_line(int x0,int y0,int x1,int y1,u32 c);
void nvxlc_present(void);
int nvxlc_w(void);
int nvxlc_h(void);
int nvxlc_key(void);
int nvxlc_waitkey(void);
void nvxlc_mouse(int* x,int* y,int* b);
unsigned long nvxlc_ticks(void);
void nvxlc_sleep(int ms);
void wm_demo(void);
int process_input(void);
void ctx_draw(void);
int ctx_click(int px,int py);

#endif
