#ifndef NVX_ARCH_X86_64_SMP_SCHED_H
#define NVX_ARCH_X86_64_SMP_SCHED_H

#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/interrupts/interrupts.h"

extern volatile int g_ncpu;

void smp_kick(int cpu);
void smp_kick_idle(void);
void smp_tlb_shootdown(void);
void smp_tlb_sync(u64 cr3);
void smp_halt_others(void);
void smp_ipi(regs_t* r,int user);
void ap_timer(regs_t* r,int user);
void ap_entry_c(int id);
int smp_enable(void);
int smp_disable(void);
void smp_pref_set(const char* v);
void smp_boot_auto_c(void);

#endif
