#ifndef NVX_ARCH_X86_64_SIMD_H
#define NVX_ARCH_X86_64_SIMD_H

#include "include/nvx/base.h"

extern int g_accel2d , g_ssse3;
extern const u8 pack24_mask[16] __attribute__((aligned(16)));

static inline void sse_fill32(u32* d,u32 c,int n){ int i=0;
    __asm__ volatile("movd %0,%%xmm7; pshufd $0,%%xmm7,%%xmm7"::"r"(c));
    for(; i+16<=n; i+=16){ __asm__ volatile("movdqu %%xmm7,(%0); movdqu %%xmm7,16(%0); movdqu %%xmm7,32(%0); movdqu %%xmm7,48(%0)"::"r"(d+i):"memory"); }
    for(; i+4<=n; i+=4){ __asm__ volatile("movdqu %%xmm7,(%0)"::"r"(d+i):"memory"); }
    for(; i<n; i++) d[i]=c;
}
static inline void sse_pack24(u8* d,const u32* s,int n){ int i=0;
    if(g_ssse3){ __asm__ volatile("movdqa %0,%%xmm6"::"m"(pack24_mask));
        for(; i+8<=n; i+=4){ __asm__ volatile("movdqu (%1),%%xmm0; pshufb %%xmm6,%%xmm0; movdqu %%xmm0,(%0)"::"r"(d+i*3),"r"(s+i):"memory"); } }
    for(; i<n; i++){ u32 c=s[i]; d[i*3]=(u8)c; d[i*3+1]=(u8)(c>>8); d[i*3+2]=(u8)(c>>16); }
}
static inline void sse_copy32(u32* d,const u32* s,int n){ int i=0;
    for(; i+16<=n; i+=16){ __asm__ volatile("movdqu (%1),%%xmm0; movdqu 16(%1),%%xmm1; movdqu 32(%1),%%xmm2; movdqu 48(%1),%%xmm3; movdqu %%xmm0,(%0); movdqu %%xmm1,16(%0); movdqu %%xmm2,32(%0); movdqu %%xmm3,48(%0)"::"r"(d+i),"r"(s+i):"memory"); }
    for(; i+4<=n; i+=4){ __asm__ volatile("movdqu (%1),%%xmm0; movdqu %%xmm0,(%0)"::"r"(d+i),"r"(s+i):"memory"); }
    for(; i<n; i++) d[i]=s[i];
}

#endif
