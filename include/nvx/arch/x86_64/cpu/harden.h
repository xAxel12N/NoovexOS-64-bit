#ifndef NVX_ARCH_X86_64_CPU_HARDEN_H
#define NVX_ARCH_X86_64_CPU_HARDEN_H
#include "include/nvx/base.h"
enum { HD_NOCPU=-1, HD_OFF=0, HD_ON=1 };
extern int g_hd_smep, g_hd_smap, g_hd_umip, g_hd_wp, g_hd_nx, g_hd_smap_want, g_hd_guard_src;
extern u64 g_hd_cpus;
void harden_cpu(void);
void harden_guard_init(void);
u64 hd_guard_mix(u64 seed,u64 rd,u64 tsc);
int hd_cr4_bits(u32 ebx7,u32 ecx7,int smap_want,u64* set,int* smep,int* smap,int* umip);
int uacc_range(u64 p,u64 n);
int uacc_in(void* dst,u64 usrc,u64 n);
int uacc_out(u64 udst,const void* src,u64 n);
long uacc_str(char* dst,u64 usrc,long cap);
int harden_report(char* o,int cap);
const char* harden_line(void);
#endif
