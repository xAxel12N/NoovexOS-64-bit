/*
 * SPDX-License-Identifier: MIT
 * Port of nv50_sor_dp_watermark_sst() and nv50_dp_bpc_to_depth() from
 * Linux nouveau drivers/gpu/drm/nouveau/dispnv50/disp.c.
 *
 * Copyright 2011 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
#define NVDP_WM_ADJUST 2u
#define NVDP_WM_LIMIT 20u
#define NVDP_WM_ADJUST_INC 8u
#define NVDP_WM_LIMIT_INC 22u
typedef struct { u32 wm, hbs, vbs, tu; } nvdp_sst_t;
static int nvdp_sst_calc(u32 khz,u32 hact,u32 htotal,u32 bpc,u32 bw,u32 lanes,int enh,int incwm,nvdp_sst_t* o){
    if(!khz||!bw||!(lanes==1||lanes==2||lanes==4)||!bpc||htotal<=hact)return 0;
    u64 rate=(u64)bw*27000000ULL, pclk=(u64)khz*1000ULL, PF=100000; u32 tu=64, depth=bpc*3, adj=incwm?NVDP_WM_ADJUST_INC:NVDP_WM_ADJUST, wmin=incwm?NVDP_WM_LIMIT_INC:NVDP_WM_LIMIT;
    if(pclk*depth>=8ULL*rate*lanes)return 0;
    u64 ratio=pclk*depth*PF/(8ULL*rate*lanes); if(PF<ratio)return 0;
    u64 wmf=ratio*tu*(PF-ratio)/PF; u32 wm=(u32)(adj+(2*((u64)depth*PF/(8u*lanes))+wmf)/PF);
    u32 nsym=(u32)((u64)hact*depth/(8u*lanes)); if(wm>39||wm>nsym)return 0; if(wm<wmin)wm=wmin;
    u32 bb=3*8*lanes+(enh?3*8*lanes:0); bb+=3*8*4; u32 rem=hact%lanes; bb+=rem?(lanes-rem)*depth:0;
    u64 nblk=(u64)bb*PF/(8u*lanes); u32 minhb=(u32)(nblk*pclk/rate/PF)+12;
    if(minhb>htotal-hact||hact<=60)return 0;
    long long hb=(long long)((u64)(htotal-hact-minhb)*rate/pclk); hb-=1; hb-=3; hb-=lanes==1?9:lanes==2?6:3;
    long long vb=0; if(hact>=40){ vb=(long long)((u64)(hact-40)*rate/pclk)-1; vb-=lanes==1?39:lanes==2?21:12; }
    o->wm=wm; o->hbs=hb<0?0:(u32)hb; o->vbs=vb<0?0:(u32)vb; o->tu=tu; return 1; }
