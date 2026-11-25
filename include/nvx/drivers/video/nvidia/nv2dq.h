#ifndef NVX_DRIVERS_VIDEO_NV2DQ_H
#define NVX_DRIVERS_VIDEO_NV2DQ_H

#include "include/nvx/base.h"

#define NV2Q_FMT 0xCFu
#define NV2Q_MAXW 16384u
#define NV2Q_OPDW 64

typedef struct { u64 va; u32 pitchb, w, h; } nv2qs;
typedef struct { int init; u64 dva, sva; u32 dpb, dw, dh, spb, sw, sh; int rop; u32 fills, copies; } nv2q;

#define NV2Q_GW 1024
#define NV2Q_GH 512
#define NV2Q_GN 768
typedef struct { u32 key; u16 x, y; } nv2qg;
typedef struct { nv2qg e[NV2Q_GN]; int n, sx, sy, sh; u32 hits, miss, full; } nv2qa;

void nv2q_reset(nv2q* q);
int nv2q_surf_ok(const nv2qs* s);
int nv2q_fill(nv2q* q,u32* out,const nv2qs* d,int x,int y,int w,int h,u32 c,int wfi);
int nv2q_copy(nv2q* q,u32* out,const nv2qs* d,int dx,int dy,int w,int h,const nv2qs* s,int sx,int sy,int same,int wfi);
void nv2qa_reset(nv2qa* a);
int nv2qa_get(nv2qa* a,u32* px,u32 pitch,const unsigned char* g,int ch,int mode,int s,int* ox,int* oy);
void nv2q_glyph_raster(u32* px,u32 pitch,const unsigned char* g,int mode,int s);

#endif
