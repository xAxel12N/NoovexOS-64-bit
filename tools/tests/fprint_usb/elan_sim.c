#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#define FP_HOST
#include "drivers/input/fingerprint/elan_fp.c"
static int last_cmd, phase, scans, finger_n=12, off, bgmean=1000, calib_state;
static double pat(int r,int x){ return 0.5+0.5*sin(r*0.45+x*0.13)*cos(x*0.21-r*0.05); }
static int out_cb(void* c,int ep,const unsigned char* p,int n,int ms){ (void)c;(void)ms; if(ep!=1||n!=2)return -1; last_cmd=p[0]<<8|p[1]; return 2; }
static int in_cb(void* c,int ep,unsigned char* p,int cap,int ms){ (void)c;(void)ms;
    switch(last_cmd){
    case 0x4019: p[0]=0x01; p[1]=0x40; return 2;
    case 0x000c: p[0]=144; p[1]=0; p[2]=64; p[3]=0; return 4;
    case 0x4024: p[0]=bgmean/0xff; p[1]=bgmean%0xff; return 2;
    case 0x4023: p[0]=calib_state++<2?1:3; if(p[0]==3)bgmean=1002; return 1;
    case 0x403f: if(scans>=finger_n)return -1; p[0]=0x55; return 1;
    case 0x0009: { if(ep!=0x82||cap!=144*64*2)return -1;
        for(int x=0;x<64;x++) for(int y=0;y<144;y++){ int v=1000+(x*7+y*3)%5; if(phase){ v+=(int)(3000*pat(off+y,x)); } p[(x*144+y)*2]=v&255; p[(x*144+y)*2+1]=v>>8; }
        if(phase){ scans++; off+=6; } return cap; }
    }
    return -1;
}
static fp_image_t img;
int main(void){
    fp_xport_t x={0,out_cb,in_cb,0};
    elanfp_open(&x,0x0c1a);
    if(!elanfp_activate()){ puts("FAIL activate"); return 1; }
    int fw,w,h; elanfp_info(&fw,&w,&h); printf("fw %04x dims %dx%d\n",fw,w,h);
    if(fw!=0x0140||w!=64||h!=144){ puts("FAIL dims"); return 1; }
    if(!elanfp_calibrate()){ puts("FAIL calibrate"); return 1; }
    phase=1; int r=elanfp_capture(&img,1000);
    printf("frames %d image %dx%d\n",scans,img.w,img.h);
    if(r<=0||img.w!=64){ puts("FAIL capture"); return 1; }
    int exp=50+6*(finger_n-2-1); if(img.h<exp-8||img.h>exp+8){ printf("FAIL assembled height %d expected ~%d\n",img.h,exp); return 1; }
    int bad=0;
    bgmean=3000; calib_state=0; phase=0; scans=0; off=0; if(!elanfp_calibrate()){ puts("FAIL recalibrate path"); return 1; }
    printf("%s\n",bad?"FAIL":"ALL PASS"); return bad;
}
