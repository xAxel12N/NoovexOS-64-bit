#ifndef NVX_APPS_CALCULATOR_H
#define NVX_APPS_CALCULATOR_H

#include "include/nvx/base.h"

extern i64 calc_acc , calc_cur;
extern char calc_op;
extern int calc_fresh , calc_err;
extern int cb_x[20],cb_y[20],cb_w,cb_h;
extern const char *CBL[20];

void calc_press(const char*b);

#endif
