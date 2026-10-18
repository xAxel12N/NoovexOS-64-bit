#ifndef NVX_APPS_SMALL_APPS_H
#define NVX_APPS_SMALL_APPS_H

#include "include/nvx/base.h"

extern int g_calyr , g_calmo , cal_lx,cal_ly,cal_rx,cal_ry,cal_aw2;
extern int snlen, sdx, sdy, applex, appley, snake_score, snake_state;
extern u64 snake_last;
extern int fb_y, fb_vy, fb_state, fb_score, fb_best;
extern u64 flappy_last;
extern int paint_col , paint_size , paint_inited , pcx,pcy;
extern int pt_colx[12], pt_coly[12], pt_bx[3], pt_by, pt_clrx, pt_clry, pt_clrw, pt_clrh;

int isin(int deg);
int icos(int deg);
void clock_draw(int i);
void sysmon_draw(int i);
void sysmon_click(int px,int py);
int cal_dow(int y,int m,int d);
int cal_dim(int y,int m);
void cal_open(void);
void cal_key(int c);
void calendar_draw(int i);
void snake_reset(void);
void snake_step(void);
void snake_key(int c);
void snake_draw(int i);
void flappy_reset(void);
void flappy_step(void);
void flappy_flap(void);
void flappy_key(int c);
void flappy_draw(int i);
void neofetch_draw(int i);
void paint_clear(void);
void paint_at(int px,int py);
void paint_draw(int i);

#endif
