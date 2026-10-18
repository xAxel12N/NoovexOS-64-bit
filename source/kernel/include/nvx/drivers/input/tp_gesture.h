#ifndef NVX_DRIVERS_INPUT_TP_GESTURE_H
#define NVX_DRIVERS_INPUT_TP_GESTURE_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/input/tp_hid.h"

enum { GES_NONE=0, GES_SWIPE_UP, GES_SWIPE_DOWN, GES_SWIPE_LEFT, GES_SWIPE_RIGHT, GES_ZOOM_IN, GES_ZOOM_OUT };
typedef struct { int id, x, y; } tpcontact;
typedef struct {
    int dx, dy, scroll_v, scroll_h, gesture, btn_l, btn_r, btn_m, btn_changed;
} tpout;
typedef struct { int id, sx, sy, zone, edge, seen; } tptrack;
typedef struct {
    int xmin, xmax, ymin, ymax; int upmm_x, upmm_y;
    int natural;
    int n_prev; tpcontact prev[TPF_MAX];
    int active; u32 t0; int maxf; int moved_um; int btn_used;
    int fx, fy;
    int sacc_v, sacc_h;
    int swx, swy, swfired;
    int d0, zacc;
    int phys_btn, phys_which;
    int pulse;
    int tap_l, tap_r, tap_m;
    int palm, palm_sess, dwt_sess, nopulse;
    tptrack tr[TPF_MAX]; int ntr;
    u32 last_ms, tap_ms; int tap_armed, drag, drag_moved; int v_prev;
    u32 zones_used, palms, dwt_skips, drags;
} tpeng;

extern int g_tp_tap, g_tp_speed, g_tp_natural, g_tp_dwt, g_tp_zones;
extern volatile u32 g_kbd_last_tick;
void tpe_init(tpeng* E,int xmin,int xmax,int ymin,int ymax,u32 xum,u32 yum);
void tpe_frame(tpeng* E,const tpcontact* c,int n,int button,u32 ms,tpout* o);

#endif
