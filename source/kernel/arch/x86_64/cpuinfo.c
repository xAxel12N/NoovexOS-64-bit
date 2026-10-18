#include "include/nvx/base.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/arch/x86_64/cpuinfo.h"

static inline u64 rdmsr(u32 m){ u32 lo,hi; __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(m)); return ((u64)hi<<32)|lo; }
char g_brand[52]; int g_lma;
void get_brand(void){
    u32 a,b,c,d; cpuid(0x80000000,&a,&b,&c,&d); g_brand[0]=0;
    if(a>=0x80000004){ char*q=g_brand;
        cpuid(0x80000002,&a,&b,&c,&d);q=e4(q,a);q=e4(q,b);q=e4(q,c);q=e4(q,d);
        cpuid(0x80000003,&a,&b,&c,&d);q=e4(q,a);q=e4(q,b);q=e4(q,c);q=e4(q,d);
        cpuid(0x80000004,&a,&b,&c,&d);q=e4(q,a);q=e4(q,b);q=e4(q,c);q=e4(q,d);*q=0;
        char*s=g_brand; while(*s==' ')s++; if(s!=g_brand){char*w=g_brand;while(*s)*w++=*s++;*w=0;}
    } else scopy(g_brand,"x86-64 CPU");
    g_lma=(int)((rdmsr(0xC0000080)>>10)&1);
}

