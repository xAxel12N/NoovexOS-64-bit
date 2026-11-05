#ifndef NVX_DRIVERS_INPUT_INPUT_H
#define NVX_DRIVERS_INPUT_INPUT_H

#include "include/nvx/base.h"

extern volatile int mouse_hscroll;
extern volatile int g_gesture;
extern volatile int mouse_xback, mouse_xfwd;
void in_xbtn(int b4,int b5);

void in_rel(int dx,int dy);
void in_buttons(int l,int r,int m);
void in_btn_raw(int l,int r,int m);
int in_mq_take(int* dx,int* dy,int* b,u64* t);
int in_mq_pending(void);
void in_mq_clear(void);
void in_mq_rebase(int dx,int dy);
#define IN_MQ_N 64
extern volatile u32 g_mq_lost;
void in_scroll(int v,int h);
void in_gesture(int g);

#endif
