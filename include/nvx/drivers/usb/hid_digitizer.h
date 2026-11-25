#ifndef NVX_DRIVERS_USB_HID_DIGITIZER_H
#define NVX_DRIVERS_USB_HID_DIGITIZER_H

#include "include/nvx/base.h"

#define DG_MAXC 10
#define DG_DEV 8
#define DG_EVQ 256
typedef struct { int off, size, sgn, ok; } dgfld;
typedef struct {
    int pen, pen_rid, touch, touch_rid, uses_ids, nf, in_max;
    dgfld tip, bar1, bar2, eras, inv, rng, pres, tx, ty, x, y;
    int xmin, xmax, ymin, ymax, pmin, pmax, tmin, tmax;
    struct { dgfld tip, conf, cid, x, y, w, h; } f[DG_MAXC];
    int fxmin, fxmax, fymin, fymax;
    dgfld cc; int mode_rid; dgfld mode; int didx_rid; dgfld didx; int ccmax_rid; dgfld ccmax;
    int feat_len[256], in_len[256];
} dglay;
typedef struct { int id, x, y, tip; } dgcon;
typedef struct { int in, x, y, xr, yr, pres, tx, ty, tip, b1, b2, eraser; } dgpen;
typedef struct { u16 type, code; int value; } dgev;
typedef struct {
    int n, act, x0, y0, moved, drag, held, lp, cx, cy, d0, sv, sh, zacc; u32 t0;
} dgges;
typedef struct {
    int used, wacom, proto, pmax, ncur, expect, nprev, slots[DG_MAXC];
    dglay L; dgcon cur[DG_MAXC], prev[DG_MAXC]; dgpen pen; dgges g;
} dgdev;
typedef struct { const char* model; u16 pid; u8 proto; u16 pmax; u16 wmm, hmm; } dgwacom;
enum { DGP_HID=0, DGP_BAMBOO=1 };
enum { DGM_FULL=0, DGM_ASPECT, DGM_LEFT, DGM_RIGHT };

extern int g_dg_on, g_dg_map, g_dg_curve, g_dg_lefty, g_dg_tap, g_dg_touch, g_dg_W, g_dg_H;
extern dgpen g_dg_pen;
extern u32 g_dg_reports, g_dg_frames, g_dg_modeset;

int dg_parse(const u8* d,int n,dglay* L);
int dg_bits(const u8* p,int plen,dgfld f);
void dg_setbits(u8* p,int plen,dgfld f,u32 val);
int dg_mode_report(const dglay* L,u8* buf,int max,int* rid);
const dgwacom* dg_wacom(u16 vid,u16 pid);
int dg_wacom_mode_report(const dgwacom* w,u8* buf,int max,int* rid);
int dg_wacom_bamboo(const u8* b,int n,int pmax,dgpen* o);
int dg_curve(int p,int pmax,int preset);
void dg_map_xy(int x,int xmin,int xmax,int y,int ymin,int ymax,int* ox,int* oy);
int dg_frame(dgdev* D,const u8* b,int n);
void dg_gesture(dgges* G,const dgcon* c,int n,u32 ms,const dglay* L);
int dg_ev_pen(const dgpen* was,const dgpen* now,dgev* o,int max);
int dg_ev_touch(const dgcon* was,int nw,const dgcon* now,int nn,int* slots,dgev* o,int max);
int dg_ev_take(dgev* o,int max);
int dg_probe(int k,u16 vid,u16 pid,const u8* rd,int n,u8* feat,int* flen,int* frid);
void dg_bind(int k,int e);
int dg_xfer(int e,const u8* b,int n);
int dg_report(int e,const u8* b,int n,u32 ms);
int dg_count(int* pens,int* touches);
int dg_enabled(void);
void dg_set_enabled(int on);
const char* dg_why(void);

#endif
