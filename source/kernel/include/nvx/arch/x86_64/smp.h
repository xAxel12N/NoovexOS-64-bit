#ifndef NVX_ARCH_X86_64_SMP_H
#define NVX_ARCH_X86_64_SMP_H

#include "include/nvx/base.h"

#define NJOBS 16
struct sjob { volatile int state; u64 arg; volatile u64 result; volatile int core; };

extern const unsigned char ap_tramp_start[], ap_tramp_end[];
extern volatile int g_cores_online;
extern volatile unsigned long g_core_load[8];
extern volatile unsigned long g_core_cycles[8];
extern struct sjob g_jobs[NJOBS];
extern volatile int g_smp_work_on;
extern u64 g_lapic_base;
extern int g_lapic_x2;
extern u32 g_lapic_icr_dest;

int acpi_count_cores(void);
void smp_lock(void);
void smp_unlock(void);
u64 smp_compute(u64 n);
int smp_submit(u64 arg);
int madt_apic_ids(u8* out,int max);
int smp_start_aps(void);

static inline u64 lapic_msr_rd(u32 m){ u32 a,d; __asm__ volatile("rdmsr":"=a"(a),"=d"(d):"c"(m)); return ((u64)d<<32)|a; }
static inline void lapic_msr_wr(u32 m,u64 v){ __asm__ volatile("wrmsr"::"a"((u32)v),"d"((u32)(v>>32)),"c"(m)); }
static inline u32 lapic_rd(u32 off){ if(g_lapic_x2){ u32 v=(u32)lapic_msr_rd(0x800+(off>>4)); return off==0x20?(v<<24):v; } return *(volatile u32*)(g_lapic_base+off); }
static inline void lapic_wr(u32 off,u32 v){ if(g_lapic_x2){ if(off==0x310){ g_lapic_icr_dest=v>>24; return; } if(off==0x300){ lapic_msr_wr(0x830,((u64)g_lapic_icr_dest<<32)|v); return; } lapic_msr_wr(0x800+(off>>4),v); return; } *(volatile u32*)(g_lapic_base+off)=v; }

#endif
