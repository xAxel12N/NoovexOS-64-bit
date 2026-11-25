/*
 * Turing/Ampere/Ada hardware cursor: core channel cursor methods (HEAD_SET_*_CURSOR, as in
 * nouveau dispnv50/headc37d.c used by headc57d.c) and the cursor immediate
 * channel (classes C37A/C57A/C67A, dispnv50/cursc37a.c, nvkm gv100_disp_curs,
 * which nvkm ga102.c also uses for GA102_DISP_CURSOR).
 * Method numbers and channel registers come from nouveau, which is:
 * Copyright 2018 Red Hat Inc. and Copyright (c) 1993-2017 NVIDIA CORPORATION.
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include "include/nvx/base.h"
#include "include/nvx/drivers/video/nvidia/nvgpu.h"
#include "include/nvx/drivers/video/nvidia/nvdisp.h"
#include "include/nvx/drivers/video/nvidia/nvcursor.h"
#include "include/nvx/drivers/video/nvidia/nvflip.h"

int g_nvcur_enable=0, g_nvcur_on[4];
int nvcur_layout(int w){ return w==32?0:w==64?1:w==128?2:w==256?3:-1; }
u32 nvcur_ctrl_word(int on,int layout,int hx,int hy){
    return ((u32)(on?1:0)<<31)|NVC37D_CURSOR_FORMAT_A8R8G8B8|(on?((u32)(layout&3)<<8)|((u32)(hx&0xFF)<<12)|((u32)(hy&0xFF)<<20):0); }
u32 nvcur_comp_word(void){ return 0xFFu|(2u<<8)|(7u<<12); }
int nvcur_set_mthds(nvcur_m* m,int max,int head,u32 handle,u64 offset,int w,int hx,int hy){
    int l=nvcur_layout(w); if(l<0||head<0||head>3||max<4||(offset&0xFF)||hx<0||hy<0||hx>=w||hy>=w)return -1;
    m[0].mthd=NVC37D_HEAD_SET_CONTROL_CURSOR(head); m[0].data=nvcur_ctrl_word(1,l,hx,hy);
    m[1].mthd=NVC37D_HEAD_SET_CONTROL_CURSOR_COMPOSITION(head); m[1].data=nvcur_comp_word();
    m[2].mthd=NVC37D_HEAD_SET_CONTEXT_DMA_CURSOR(head,0); m[2].data=handle;
    m[3].mthd=NVC37D_HEAD_SET_OFFSET_CURSOR(head,0); m[3].data=(u32)(offset>>8);
    return 4; }
int nvcur_clr_mthds(nvcur_m* m,int max,int head){
    if(head<0||head>3||max<2)return -1;
    m[0].mthd=NVC37D_HEAD_SET_CONTROL_CURSOR(head); m[0].data=nvcur_ctrl_word(0,0,0,0);
    m[1].mthd=NVC37D_HEAD_SET_CONTEXT_DMA_CURSOR(head,0); m[1].data=0;
    return 2; }
u32 nvcur_user(int head){ return NVC57A_USER_BASE+(u32)(NVC57A_CURS_CHID+head-1)*0x1000u; }
u32 nvcur_point_word(int x,int y){ return ((u32)x&0xFFFFu)|(((u32)y&0xFFFFu)<<16); }
extern int g_nv_ampere_on __attribute__((weak)); extern int g_nv_ada_on __attribute__((weak));
int nvcur_gen_ok(u32 boot0){ u32 c=(boot0>>20)&0x1FFu; if((c&0x1F0u)==0x160u)return 1;
    if(c>=0x172u&&c<=0x177u)return &g_nv_ampere_on&&g_nv_ampere_on; if(c>=0x192u&&c<=0x197u&&c!=0x195u)return &g_nv_ada_on&&g_nv_ada_on; return 0; }
int nvcur_supported(void){ return g_nvcur_enable&&g_nv_bar0&&nvcur_gen_ok(g_nv_boot0); }
static int nvcur_idle(int head){
    u32 soff=(u32)(NVC57A_CURS_CHID+head-1)*4u;
    for(int i=0;i<2000;i++){ if((nvr32(NVC57A_STAT_BASE+soff)&0x00070000u)==0x00040000u)return 0; nv_udelay(1000); }
    return -1; }
int nvcur_chan_init(int head){
    if(!nvcur_supported()||head<0||head>3)return -1;
    nvw32(NVC57A_CTRL_BASE+(u32)(NVC57A_CURS_CHID+head)*4u,1);
    if(nvcur_idle(head)){ nvcur_chan_fini(head); return -1; }
    g_nvcur_on[head]=1; return 0; }
void nvcur_chan_fini(int head){
    if(head<0||head>3||!g_nv_bar0)return;
    u32 c=NVC57A_CTRL_BASE+(u32)(NVC57A_CURS_CHID+head)*4u;
    nvw32(c,nvr32(c)|0x10u); nvcur_idle(head); nvw32(c,nvr32(c)&~1u); g_nvcur_on[head]=0; }
int nvcur_move(int head,int x,int y){
    if(head<0||head>3||!g_nvcur_on[head])return -1;
    u32 u=nvcur_user(head); nvw32(u+NVC37A_SET_CURSOR_HOT_SPOT_POINT_OUT(0),nvcur_point_word(x,y)); nvw32(u+NVC37A_UPDATE,1); return 0; }
static char* nvcur_s(char* p,char* e,const char* s){ while(*s&&p<e)*p++=*s++; return p; }
int nvcur_cmd(const char* a,char* out,int n){
    char* p=out; char* e=out+n-1; int r=0;
    if(a[0]=='o'&&a[1]=='n'){ g_nvcur_enable=1; if(nvcur_chan_init(0)){ g_nvcur_enable=0; p=nvcur_s(p,e,"hardware cursor: not available (needs an NVIDIA Turing, RTX 30 or RTX 40 card with its display running), the software cursor stays\n"); r=-1; } else p=nvcur_s(p,e,"hardware cursor: on\n"); }
    else if(a[0]=='o'&&a[1]=='f'){ nvcur_hw_off(); for(int h=0;h<4;h++)if(g_nvcur_on[h])nvcur_chan_fini(h); g_nvcur_enable=0; p=nvcur_s(p,e,"hardware cursor: off, software cursor\n"); }
    else p=nvcur_s(p,e,g_nvcur_on[0]?"hardware cursor: on\n":"hardware cursor: off (software cursor)\n");
    *p=0; return r; }
u32 nvcur_emit(u32* pb,u32 cap,const nvcur_m* m,int n){
    u32 o=0; if(n<0)return 0;
    for(int i=0;i<n;i++){ if(o+2u>cap)return 0; pb[o++]=(1u<<18)|(m[i].mthd&0xFFFCu); pb[o++]=m[i].data; }
    return o; }
int nvcur_img_build(const u32* bk,const u32* wh,int sw,int sh,int hx,int hy,u32* out,int* ow,int* ohx,int* ohy){
    int x0=sw, y0=sh, x1=-1, y1=-1;
    for(int y=0;y<sh;y++)for(int x=0;x<sw;x++){ u32 b=bk[y*sw+x]&0xFFFFFFu, w=wh[y*sw+x]&0xFFFFFFu; if(!b&&w==0xFFFFFFu)continue;
        if(x<x0)x0=x; if(x>x1)x1=x; if(y<y0)y0=y; if(y>y1)y1=y; }
    if(x1<0)return 0;
    if(hx<x0)x0=hx; if(hx>x1)x1=hx; if(hy<y0)y0=hy; if(hy>y1)y1=hy;
    int ext=(x1-x0>y1-y0?x1-x0:y1-y0)+1, w=ext<=32?32:ext<=64?64:0; if(!w)return -1;
    for(int y=0;y<w;y++)for(int x=0;x<w;x++){ int sx=x0+x, sy=y0+y; u32 px=0;
        if(sx<sw&&sy<sh){ u32 b=bk[sy*sw+sx], v=wh[sy*sw+sx]; int d=0;
            for(int s=0;s<24;s+=8){ int k=(int)((v>>s)&255u)-(int)((b>>s)&255u); if(k>d)d=k; }
            u32 a=255u-(u32)d; px=a<<24;
            for(int s=0;s<24;s+=8){ u32 c=(b>>s)&255u; if(c>a)c=a; px|=c<<s; } }
        out[y*w+x]=px; }
    *ow=w; *ohx=hx-x0; *ohy=hy-y0; return 1; }
void nvcur_clip(int x,int y,int w,int scw,int sch,int* ox,int* oy){
    if(x<1-w)x=1-w; if(x>scw-1)x=scw-1; if(y<1-w)y=1-w; if(y>sch-1)y=sch-1; *ox=x; *oy=y; }
static struct{ int on, head, w, hx, hy, slot, shown; u64 vram; volatile u32* cpu; } NCUR;
int nvcur_hw_active(void){ return NCUR.on&&nvflip_head()==NCUR.head&&g_nvcur_on[NCUR.head]; }
int nvcur_hw_head(void){ return NCUR.on?NCUR.head:-1; }
int nvcur_hw_shape(const u32* img,int w,int hx,int hy){
    int head=nvflip_head(); nvcur_m m[4]; u32 mv[8]; int n;
    if(head<0||!g_nvcur_enable)return -1;
    if(!g_nvcur_on[head]&&nvcur_chan_init(head))return -1;
    if(!img){ n=nvcur_clr_mthds(m,4,head); }
    else {
        if(nvcur_layout(w)<0||w>64||hx<0||hy<0||hx>=w||hy>=w)return -1;
        u64 v; volatile u32* c; if(!nvflip_cursor_buf(0x8000u,&v,&c))return -1;
        int s=NCUR.on&&NCUR.head==head?NCUR.slot^1:0; volatile u32* d=c+(u32)s*0x1000u;
        for(int i=0;i<w*w;i++)d[i]=img[i];
        n=nvcur_set_mthds(m,4,head,NVFLIP_HVRAM,v+(u64)s*0x4000u,w,0,0); if(n<0)return -1;
        NCUR.slot=s; NCUR.vram=v; NCUR.cpu=c; }
    for(int i=0;i<n;i++){ mv[i*2]=m[i].mthd; mv[i*2+1]=m[i].data; }
    if(!nvflip_core_methods(mv,n)||!nvflip_core_update(0))return -1;
    NCUR.on=1; NCUR.head=head; NCUR.shown=img!=0; if(img){ NCUR.w=w; NCUR.hx=hx; NCUR.hy=hy; } return 0; }
int nvcur_hw_move(int x,int y,int scw,int sch){
    if(!nvcur_hw_active())return -1; if(!NCUR.shown)return 0;
    int px, py; nvcur_clip(x-NCUR.hx,y-NCUR.hy,NCUR.w,scw,sch,&px,&py); return nvcur_move(NCUR.head,px,py); }
void nvcur_hw_off(void){
    if(!NCUR.on)return; nvcur_m m[2]; u32 mv[4]; int n=nvcur_clr_mthds(m,2,NCUR.head);
    if(n>0&&nvflip_head()==NCUR.head){ for(int i=0;i<n;i++){ mv[i*2]=m[i].mthd; mv[i*2+1]=m[i].data; } if(nvflip_core_methods(mv,n))nvflip_core_update(0); }
    NCUR.on=0; NCUR.shown=0; }
