#ifndef NVX_DRIVERS_INPUT_INPUT_H
#define NVX_DRIVERS_INPUT_INPUT_H

#include "include/nvx/base.h"

extern volatile int mouse_hscroll;
extern volatile int g_gesture;

void in_rel(int dx,int dy);
void in_buttons(int l,int r,int m);
void in_scroll(int v,int h);
void in_gesture(int g);

#endif
