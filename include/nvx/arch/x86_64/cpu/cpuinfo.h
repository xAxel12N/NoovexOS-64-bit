#ifndef NVX_ARCH_X86_64_CPUINFO_H
#define NVX_ARCH_X86_64_CPUINFO_H

#include "include/nvx/base.h"

extern char g_brand[52];
extern int g_lma;

void get_brand(void);

static inline void cpuid(u32 lf,u32*a,u32*b,u32*c,u32*d){ __asm__ volatile("cpuid":"=a"(*a),"=b"(*b),"=c"(*c),"=d"(*d):"a"(lf),"c"(0)); }

#endif
