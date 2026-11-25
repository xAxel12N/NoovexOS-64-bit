/*
 * Copyright 2018 Red Hat Inc.
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
 */
#define NF_M(m,n) do{ if(o+1u+(u32)(n)>cap)return 0; pb[o++]=((u32)(n)<<18)|(m); }while(0)
#define NF_V(v) (pb[o++]=(v))
u32 nvflip_emit_image(u32* pb,u32 cap,u32 w,u32 h,u32 pitch,u32 handle,u32 off8){
    u32 o=0;
    NF_M(NVC57E_SET_SIZE,4); NF_V(w|(h<<16)); NF_V(nvgsp_cls_cur()->wndw_storage_pitch); NF_V(NVC57E_FMT_A8R8G8B8); NF_V(pitch>>6);
    NF_M(NVC57E_SET_CONTEXT_DMA_ISO0,1); NF_V(handle);
    NF_M(NVC57E_SET_OFFSET0,1); NF_V(off8);
    NF_M(NVC57E_SET_POINT_IN0,1); NF_V(0);
    NF_M(NVC57E_SET_SIZE_IN,1); NF_V(w|(h<<16));
    NF_M(NVC57E_SET_SIZE_OUT,1); NF_V(w|(h<<16));
    return o; }
u32 nvflip_emit_flip(u32* pb,u32 cap,u32 off8,u32 win_mask,int vsync,u32 ntfy_handle,u32 ntfy_off){
    u32 o=0; (void)win_mask;
    NF_M(NVC57E_SET_PRESENT_CONTROL,1); NF_V(vsync?NVC57E_PRESENT_NON_TEARING(1):NVC57E_PRESENT_IMMEDIATE);
    NF_M(NVC57E_SET_CONTEXT_DMA_NOTIFIER,2); NF_V(ntfy_handle); NF_V(ntfy_handle?((ntfy_off&0xFF0u)|1u):0);
    NF_M(NVC57E_SET_OFFSET0,1); NF_V(off8);
    NF_M(NVC57E_SET_INTERLOCK_FLAGS,2); NF_V(0); NF_V(0);
    NF_M(NVC57E_UPDATE,1); NF_V(1);
    return o; }
u32 nvflip_emit_head(u32* pb,u32 cap,int head,int sor,u32 display_id,const nvflip_mode_t* m,u32 vw,u32 vh,u32 lut8,int link){
    u32 o=0, h=(u32)head, ow=m->hact, oh=m->vact;
    if(vw&&vh){ u64 a=(u64)m->hact*vh, b=(u64)m->vact*vw; if(a<=b){ ow=m->hact; oh=(u32)(((u64)m->hact*vh)/vw); } else { oh=m->vact; ow=(u32)(((u64)m->vact*vw)/vh); }
        ow&=~1u; oh&=~1u; if(ow<2)ow=2; if(oh<2)oh=2; if(ow>m->hact)ow=m->hact; if(oh>m->vact)oh=m->vact; }
    u32 ht=(u32)m->hact+m->hfp+m->hsw+m->hbp, vt=(u32)m->vact+m->vfp+m->vsw+m->vbp;
    u32 hse=(u32)m->hsw-1, hbe=(u32)m->hsw+m->hbp-1, hbs=hbe+m->hact, vse=(u32)m->vsw-1, vbe=(u32)m->vsw+m->vbp-1, vbs=vbe+m->vact;
    NF_M(NVC57D_HEAD_SET_VIEWPORT_IN(h),1); NF_V(vw|(vh<<16));
    NF_M(NVC57D_HEAD_SET_VIEWPORT_OUT(h),1); NF_V(ow|(oh<<16));
    NF_M(NVC57D_HEAD_SET_RASTER_SIZE(h),4); NF_V(ht|(vt<<16)); NF_V(hse|(vse<<16)); NF_V(hbe|(vbe<<16)); NF_V(hbs|(vbs<<16));
    NF_M(NVC57D_HEAD_SET_RASTER_LOCK(h),1); NF_V(1);
    NF_M(NVC57D_HEAD_SET_CONTROL(h),1); NF_V(0);
    NF_M(NVC57D_HEAD_SET_PIXEL_CLOCK(h),1); NF_V(m->khz*1000u);
    NF_M(NVC57D_HEAD_SET_PIXEL_CLOCK_MAX(h),1); NF_V(m->khz*1000u);
    NF_M(NVC57D_HEAD_SET_USAGE_BOUNDS(h),1); NF_V(0x00000004u|(1u<<4)|(1u<<8)|(1u<<12));
    NF_M(NVC57D_HEAD_SET_OLUT_CONTROL(h),4); NF_V(0x00000001u|(2u<<2)|(1029u<<8)); NF_V(0xFFFFFFFFu); NF_V(NVFLIP_HVRAM); NF_V(lut8);
    NF_M(NVC57D_HEAD_SET_PROCAMP(h),1); NF_V(0);
    NF_M(NVC57D_HEAD_SET_DITHER(h),1); NF_V(0);
    NF_M(NVC57D_HEAD_SET_OUTPUT_RESOURCE(h),1); NF_V(((u32)m->nhs<<2)|((u32)m->nvs<<3)|(4u<<4)|(0x3Fu<<26));
    NF_M(NVC57D_HEAD_SET_DISPLAY_ID(h),1); NF_V(display_id);
    NF_M(NVC57D_SOR_SET_CONTROL(sor),1); NF_V((1u<<h)|((u32)(link==8||link==9?link:link==2?2:1)<<8));
    return o; }
#undef NF_M
#undef NF_V
