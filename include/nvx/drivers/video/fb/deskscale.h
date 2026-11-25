#ifndef NVX_DRIVERS_VIDEO_FB_DESKSCALE_H
#define NVX_DRIVERS_VIDEO_FB_DESKSCALE_H

#include "include/nvx/base.h"

#define DESK_MAXW 3840
#define DESK_MAXH 2160
#define DESK_NPCT 4
#define DESK_RESERVE (64ULL<<20)

static inline u32 desk_pct(int i){ return i<=0?100u:i==1?125u:i==2?150u:200u; }
static inline int desk_pct_ok(u32 p){ return p==0||p==100||p==125||p==150||p==200; }
static inline u32 desk_auto_pct(u32 mw,u32 mh){ return (mh>1600||mw>2880)?150u:100u; }
static inline int desk_dims(u32 mw,u32 mh,u32 pct,u32* w,u32* h){
    if(pct<100||pct>200||!mw||!mh)return 0;
    u32 a=pct==100?mw:(u32)((u64)mw*100u/pct)&~7u, b=pct==100?mh:(u32)((u64)mh*100u/pct)&~1u;
    if(a<640||b<480||a>DESK_MAXW||b>DESK_MAXH)return 0;
    *w=a; *h=b; return 1; }
static inline int desk_ladder(u32 mw,u32 mh,u32 want,u32* pcts,int max){
    int n=0; u32 w,h; if(!want)want=desk_auto_pct(mw,mh);
    for(int i=0;i<DESK_NPCT&&n<max;i++){ u32 p=desk_pct(i); if(p<want)continue; if(desk_dims(mw,mh,p,&w,&h))pcts[n++]=p; }
    return n; }
static inline int desk_big(u32 w,u32 h,u32 sw,u32 sh){ return w>sw||h>sh; }
static inline u64 desk_cpu_bytes(u32 w,u32 h,u32 sw,u32 sh){ return desk_big(w,h,sw,sh)?(u64)w*h*4u*4u+DESK_RESERVE:0; }

#endif
