#ifndef NVX_DRIVERS_VIDEO_NVIDIA_NVQMD_H
#define NVX_DRIVERS_VIDEO_NVIDIA_NVQMD_H

#include "include/nvx/base.h"

enum { NVQMD_NO_SHADER=-1, NVQMD_UNVERIFIED=-2, NVQMD_BAD_QMD=-3, NVQMD_GUARD_OFF=-4 };

static inline void nvqmd_set(u32* q,u32 lo,u32 hi,u32 v){ u32 w=lo>>5, b=lo&31, n=hi-lo+1; u32 m=n>=32?0xFFFFFFFFu:((1u<<n)-1u); q[w]=(q[w]&~(m<<b))|((v&m)<<b); }

static inline int nvqmd_pick(u32 sm,u32 qmd,int verified,int try_unverified){
    if(sm==75)return qmd==2?75:NVQMD_BAD_QMD;
    if(sm!=86&&sm!=89)return NVQMD_NO_SHADER;
    if(qmd!=3)return NVQMD_BAD_QMD;
    if(!verified&&!try_unverified)return NVQMD_UNVERIFIED;
    return (int)sm; }

static inline const char* nvqmd_why(int r){
    return r==NVQMD_UNVERIFIED?"the sm_86/sm_89 compositor shader is ported but not checked on hardware - off until enabled; this GPU draws through the copy and 2D engines":
           r==NVQMD_GUARD_OFF?"the RTX 30/40 compute compositor is off in Settings or after failures - drawing through the copy and 2D engines":
           r==NVQMD_BAD_QMD?"no launch descriptor layout for this GPU - drawing through the copy and 2D engines":
           "no compositor shader for this GPU generation - drawing through the copy and 2D engines"; }

static inline int nvqmd_fill(u32* q,u32 major,u32 gx,u32 gy,u32 regs,u32 bars,const u64* cba,const u32* cbl,u64 prog){
    if(major!=2&&major!=3)return 0;
    for(int i=0;i<64;i++)q[i]=0;
    nvqmd_set(q,134,134,1); nvqmd_set(q,189,189,1); nvqmd_set(q,191,191,1); nvqmd_set(q,368,369,1); nvqmd_set(q,378,378,1);
    nvqmd_set(q,384,415,gx); nvqmd_set(q,416,431,gy); nvqmd_set(q,448,463,1); nvqmd_set(q,544,561,0);
    nvqmd_set(q,592,607,32); nvqmd_set(q,608,623,4); nvqmd_set(q,624,639,1); nvqmd_set(q,648,656,regs);
    if(major==2){ nvqmd_set(q,366,366,1); nvqmd_set(q,562,568,3); nvqmd_set(q,569,575,17); nvqmd_set(q,657,663,3); nvqmd_set(q,576,579,2); nvqmd_set(q,580,583,2); nvqmd_set(q,955,959,bars); }
    else { nvqmd_set(q,562,567,3); nvqmd_set(q,569,574,17); nvqmd_set(q,657,662,3); nvqmd_set(q,576,579,0); nvqmd_set(q,580,583,3); nvqmd_set(q,763,767,bars); }
    for(u32 b=0;b<8;b++){ u64 a=cba[b]; u32 l=cbl[b]; if(!a||!l)continue; u32 base=1024u+b*64u;
        nvqmd_set(q,640u+b,640u+b,1); nvqmd_set(q,base,base+31,(u32)a); nvqmd_set(q,base+32,base+48,(u32)(a>>32)); nvqmd_set(q,base+51,base+63,(l+15u)>>4); }
    nvqmd_set(q,1536,1567,(u32)prog); nvqmd_set(q,1568,1584,(u32)(prog>>32));
    return 1; }

#endif
