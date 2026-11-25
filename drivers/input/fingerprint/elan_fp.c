/*
 * Elan driver for libfprint
 *
 * Copyright (C) 2017 Igor Filatov <ia.filatov@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * NoovexOS port: used under GPL-3.0-or-later as permitted by LGPL-2.1 section 3.
 */
#include "include/nvx/base.h"
#include "include/nvx/drivers/input/fingerprint/fprint.h"
#define EFP_OUT 0x01
#define EFP_CIN 0x83
#define EFP_IIN 0x82
#define EFP_TMO 10000
#define EFP_FTMO 200
#define EFP_MAXF 30
#define EFP_MINF 7
#define EFP_SKIP 2
#define EFP_MAXH 50
#define EFP_RAWMAX (160*160*2)
static struct { fp_xport_t x; int ok, quirk, fw, w, h, rh, nf; u8 last[8]; } efp;
static u8 efp_raw[EFP_RAWMAX];
static u16 efp_bg[FP_IMG_W*EFP_MAXH], efp_fr[FP_IMG_W*EFP_MAXH], efp_sort[FP_IMG_W*EFP_MAXH];
static u8 efp_frames[EFP_MAXF][FP_IMG_W*EFP_MAXH];
static int efp_cmd(u8 a,u8 b,int rlen,int ep,int ms){
    u8 c[2]={a,b}; if(efp.x.out(efp.x.ctx,EFP_OUT,c,2,EFP_TMO)!=2)return -1;
    if(!rlen)return 0;
    if(efp.quirk==4&&rlen==1)rlen=2;
    u8* d=ep==EFP_IIN?efp_raw:efp.last; int cap=ep==EFP_IIN?EFP_RAWMAX:8;
    int g=efp.x.in(efp.x.ctx,ep,d,rlen<cap?rlen:cap,ms); return g==rlen?0:-1;
}
int elanfp_open(const fp_xport_t* x,u16 pid){
    efp.x=*x; efp.ok=1; efp.fw=efp.w=efp.h=efp.rh=efp.nf=0;
    efp.quirk=pid==0x0907?1:pid==0x0c03?2:pid==0x0c42?4:pid==0x0c58?8:0; return 1;
}
int elanfp_info(int* fw,int* w,int* h){ *fw=efp.fw; *w=efp.w; *h=efp.rh; return efp.ok; }
void elanfp_stop(void){ if(efp.ok)efp_cmd(0x00,0x0b,0,EFP_CIN,EFP_TMO); }
int elanfp_activate(void){
    if(!efp.ok)return 0;
    if(efp_cmd(0x40,0x19,2,EFP_CIN,EFP_TMO))return 0;
    efp.fw=efp.last[0]<<8|efp.last[1];
    if(efp_cmd(0x00,0x0c,4,EFP_CIN,EFP_TMO))return 0;
    if(efp.quirk&2){ efp.w=efp.last[0]; efp.h=efp.rh=efp.last[2]; } else { efp.w=efp.last[2]; efp.h=efp.rh=efp.last[0]; }
    if((efp.w&1)&&(efp.h&1)){ efp.w++; efp.h++; efp.rh=efp.h; }
    if(efp.h>EFP_MAXH)efp.h=EFP_MAXH;
    if(efp.w<8||efp.w>FP_IMG_W||efp.rh<8||efp.rh*efp.w*2>EFP_RAWMAX){ efp.ok=0; return 0; }
    if(efp.quirk&1){ if(efp_cmd(0x40,0x2a,2,EFP_CIN,EFP_TMO))return 0; }
    return 1;
}
static void efp_save(u16* f){
    int m=(efp.rh-efp.h)/2;
    for(int y=0;y<efp.h;y++) for(int x=0;x<efp.w;x++){ int r=(efp.quirk&2)?x+(y+m)*efp.w:m+y+x*efp.rh; f[x+y*efp.w]=(u16)(efp_raw[r*2]|efp_raw[r*2+1]<<8); }
}
static int efp_getimg(void){ return efp_cmd(0x00,0x09,efp.rh*efp.w*2,EFP_IIN,EFP_TMO); }
int elanfp_calibrate(void){
    if(!efp.ok)return 0;
    for(int att=(efp.quirk&8)?30:10;att>0;){
        if(efp_getimg())return 0;
        efp_save(efp_bg);
        if(!(efp.quirk&4)&&efp.fw<0x0138)return 1;
        if(efp_cmd(0x40,0x24,2,EFP_CIN,EFP_TMO))return 0;
        int cm=efp.last[0]*0xff+efp.last[1]; u32 n=(u32)(efp.w*efp.h), bm=0; for(u32 i=0;i<n;i++)bm+=efp_bg[i]; bm/=n;
        int need=((efp.quirk&4)&&(cm>5500||cm<2500))||((int)bm>cm?(int)bm-cm:cm-(int)bm)>500;
        if(!need)return 1;
        int st=0;
        for(;;){ if(--att<=0)return 0; if(efp_cmd(0x40,0x23,1,EFP_CIN,EFP_TMO))return 0;
            if(st==1&&efp.last[0]==3){ break; } if(!st&&efp.last[0]==1){ st=1; } if(efp.x.sleep_ms)efp.x.sleep_ms(50); }
    }
    return 0;
}
static void efp_isort(u16* a,int n){ for(int g=n/2;g>0;g/=2) for(int i=g;i<n;i++){ u16 t=a[i]; int j=i; while(j>=g&&a[j-g]>t){ a[j]=a[j-g]; j-=g; } a[j]=t; } }
static void efp_process(const u16* r,u8* o){
    int n=efp.w*efp.h;
    if(efp.quirk&1){ u16 mn=0xffff,mx=0; for(int i=0;i<n;i++){ if(r[i]<mn)mn=r[i]; if(r[i]>mx)mx=r[i]; }
        for(int i=0;i<n;i++){ o[i]=mx==mn?0:(u8)((u32)(r[i]-mn)*0xff/(u32)(mx-mn)); } return; }
    for(int i=0;i<n;i++){ efp_sort[i]=r[i]; } efp_isort(efp_sort,n);
    u32 l0=efp_sort[0], l1=efp_sort[n*3/10], l2=efp_sort[n*65/100], l3=efp_sort[n-1];
    if(l1<l0+1){ l1=l0+1; } if(l2<l1+1){ l2=l1+1; } if(l3<l2+1){ l3=l2+1; }
    for(int i=0;i<n;i++){ u32 p=r[i], v;
        if(p<l1)v=p<l0?0:(p-l0)*99/(l1-l0); else if(p<l2)v=99+(p-l1)*56/(l2-l1); else v=155+(p-l2)*100/(l3-l2);
        o[i]=(u8)(v>255?255:v); }
}
int elanfp_capture(fp_image_t* img,int finger_ms){
    if(!efp.ok||!efp.w)return 0;
    efp.nf=0; int tmo=finger_ms; int idle=0;
    if(efp_cmd(0x40,0x31,0,EFP_CIN,EFP_TMO))return 0;
    while(efp.nf<EFP_MAXF){
        if(efp_cmd(0x40,0x3f,1,EFP_CIN,tmo))break;
        if(efp.last[0]!=0x55){ if(efp.quirk==8&&(efp.last[0]==0||efp.last[0]==0xaf)){ if(++idle>finger_ms/10)break; if(efp.x.sleep_ms)efp.x.sleep_ms(10); continue; } break; }
        if(efp_getimg())break;
        efp_save(efp_fr); u32 sum=0; int n=efp.w*efp.h;
        for(int i=0;i<n;i++){ efp_fr[i]=efp_bg[i]>efp_fr[i]?0:(u16)(efp_fr[i]-efp_bg[i]); sum+=efp_fr[i]; }
        if(!sum){ elanfp_stop(); return -1; }
        efp_process(efp_fr,efp_frames[efp.nf]); efp.nf++; tmo=EFP_FTMO;
    }
    elanfp_stop();
    if(efp.nf<EFP_MINF)return 0;
    return fp_assemble(&efp_frames[0][0],efp.nf-EFP_SKIP,efp.w,efp.h,img);
}
int fp_assemble(const u8* fr,int nf,int fw,int fh,fp_image_t* img){
    if(nf<1||fw>FP_IMG_W)return 0;
    int stride=FP_IMG_W*EFP_MAXH, y0=0; img->w=fw;
    for(int i=0;i<fh&&i<FP_IMG_HMAX;i++) for(int x=0;x<fw;x++)img->px[i*fw+x]=fr[i*fw+x];
    img->h=fh<FP_IMG_HMAX?fh:FP_IMG_HMAX;
    for(int f=1;f<nf;f++){ const u8* a=fr+(f-1)*stride; const u8* b=fr+f*stride; int best=0; u32 bs=0xffffffffu;
        for(int dy=0;dy<fh-4;dy++){ u32 s=0; int ov=fh-dy; for(int y=0;y<ov;y++) for(int x=0;x<fw;x++){ int d=a[(y+dy)*fw+x]-b[y*fw+x]; s+=(u32)(d<0?-d:d); } s/=(u32)(ov*fw); if(s<bs){ bs=s; best=dy; } }
        y0+=best; int ov=fh-best;
        for(int y=ov;y<fh;y++){ int r=y0+y; if(r>=FP_IMG_HMAX)return img->h; for(int x=0;x<fw;x++)img->px[r*fw+x]=b[y*fw+x]; if(r+1>img->h)img->h=r+1; } }
    return img->h;
}
