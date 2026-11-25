#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#define NVX_BASE_H
#define NVX_DRIVERS_VIDEO_FRAMEBUFFER_H
#define NVX_MM_HEAP_H
static void* kmalloc(unsigned long n){ return malloc(n); }
static void kfree(void* p){ free(p); }
u64 hr_ns(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (u64)t.tv_sec*1000000000ULL+(u64)t.tv_nsec; }
#include "graphics/theme/wallfx.c"
static int fails;
#define CK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); fails++; } }while(0)
static void fill(u32* a,int w,int h){ for(int i=0;i<w*h;i++)a[i]=0xFF000000u|(((u32)(i*2654435761u)>>8)^((u32)(i/w)<<3)); }
int main(void){
    int w=640,h=400; u32* a=malloc((size_t)w*h*4); u32* b=malloc((size_t)w*h*4); u32* c=malloc((size_t)w*h*4);
    fill(a,w,h); memcpy(b,a,(size_t)w*h*4); memcpy(c,a,(size_t)w*h*4);
    CK(wall_enhance(a,w,h,w)==1);
    CK(memcmp(a,b,(size_t)w*h*4)!=0);
    CK(wall_enhance_begin(b,w,h,w)==1);
    int steps=0; while(!wall_enhance_step(1))steps++;
    CK(steps>20);
    CK(memcmp(a,b,(size_t)w*h*4)==0);
    CK(wall_enhance_begin(c,w,h,w)==1); CK(wall_enhance_step(3)==0); wall_enhance_abort(); CK(wall_enhance_step(1)==1);
    CK(wall_enhance_begin(c,w,h,w)==1); while(!wall_enhance_step(7)){} CK(memcmp(a,c,(size_t)w*h*4)==0);
    g_wall_fx=0; CK(wall_enhance_begin(c,w,h,w)==0); CK(wall_enhance_step(1)==1); CK(memcmp(a,c,(size_t)w*h*4)==0);
    g_wall_fx=1; CK(wall_enhance(c,32,32,32)==0);
    free(a); free(b); free(c);
    printf(fails?"WALLFX HOST TEST: %d FAIL\n":"WALLFX HOST TEST: ALL PASS\n",fails); return fails?1:0; }
