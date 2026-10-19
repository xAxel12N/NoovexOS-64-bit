#ifndef NVX_ARCH_X86_64_HRTIMER_H
#define NVX_ARCH_X86_64_HRTIMER_H

#include "include/nvx/base.h"

struct nvx_g_hr_t {
    int ready, src, ref; u64 tsc_hz, tsc0, tsc_mult; int tsc_ok, tsc_inv;
    int hpet_ok, hpet_64, hpet_ntim; u64 hpet_pa, hpet_va, hpet_fs, hpet_mult, hpet0, hpet_hi; u32 hpet_last;
    int lapic_ok, lapic_x2; u64 lapic_hz; u32 lapic_id;
    volatile u64 lapic_irqs; u64 wakes, wake_early; u64 boot_ns_ticks;
    char why[48];
};

extern struct nvx_g_hr_t g_hr;

u64 hr_rdtsc(void);
u64 hr_mul_shr32(u64 a,u64 m);
u64 hr_muldiv(u64 a,u64 b,u64 c);
u64 hr_hpet_count(void);
u64 hr_ns(void);
int hr_precise(void);
void hr_delay_us(u32 us);
void hr_delay_ms(u32 ms);
void hr_lapic_isr(void);
int hr_wake_at(u64 ns);
void hr_wake_cancel(void);
void hr_init(void);

#endif
