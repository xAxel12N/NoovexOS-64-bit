# R1 review: v11.05..HEAD, NMI watchdog + crumbs (read-only)

## Verdict
The new code is mostly sound. No issue found that can hang the system. The handler runs on a per-CPU IST1 stack (PCPU->ist_nmi, set in ring3.c:44 with idt_use_ist(2,1)). It goes through isr_common, which saves all GPRs and returns with iretq. The ICR value 0x4400 (NMI delivery, level assert) is correct for both xAPIC and x2APIC. The handler checks g_nw_hit first, so foreign NMIs are still ignored exactly as before.
Boot without SMP and boot with `smp off` are unaffected: g_smp_on=0 makes nmiwd_check return at once, and nmiwd_nmi bails on g_nw_hit==0.
FALSE-NMI RISK: in normal desktop use, LOW. The cases below are the exceptions, and the first two are real.

## HIGH
H1. Polled-timer machines (g_ticks_live=0, main.c:288). g_ticks then only moves when the main loop runs pit_poll_tick, so any main-loop operation of 3 s or more fires a false NMI and red box, even with IRQs on. Examples: GSP boot, disk sync, install, apt.
Patch, at the top of nmiwd_check in core/crash.c:
    extern int g_ticks_live; if(!g_nmiwd_on||!g_smp_on||!g_ticks_live||cpu_id()==0)return;

## MEDIUM
M1. Terminal screens get a false box. Two cases:
- crash_panic: cpu_irq_off, then smp_halt_others. An AP spinning on the BKL with IRQs off never takes IPI_HALT, and frz_bkl_wait calls nmiwd_check BEFORE the g_frz_hold/g_frz_sleep check.
- power_off fallback ("safe to turn off" screen) and power_reboot: cpu_irq_off plus a long io_wait/cpu_stop. APs keep running ap_timer.
In both cases, after 3 s the "froze" box is drawn over the panic or shutdown screen. That gives misleading reports.
Patch: set `g_nmiwd_on=0;` as the first statement of crash_panic (before smp_halt_others), power_off and power_reboot (core/power.c).
M2. VM or host pauses: QEMU `stop`, a host suspend, or a long vCPU deschedule. The TSC jumps while g_ticks has not caught up, so the AP sends an NMI. CPU0's handler can run before the pending PIT IRQ and draw a false box. The cost is only the box, a crash-report.txt write and a toast.
Patch, in nmiwd_nmi after the g_ticks check: skip if CPU0 had interrupts enabled, because then it is not a hard lockup.
    if(r->rflags&0x200)return;
This keeps the box's text ("interrupts off") honest. If you want to keep catching the IF=1 / dead-PIT case, change the headline instead of returning.

## LOW
L1. The xAPIC ipi_send (smp_sched.c:28) is not IRQ-safe: the ICR_HI and ICR_LO writes are separate. ap_timer can now call smp_nmi_cpu between them, and the interrupted IPI then goes to CPU0. This is very rare (it needs a stall at that exact moment).
Patch: wrap the xAPIC branch of ipi_send in `u64 f=cpu_irq_save(); ... cpu_irq_restore(f);`.
L2. nmiwd_check runs on every BKL spin iteration and does an xchg on g_nw_lock, so the cache line bounces between the spinning APs. Use test-and-test-and-set:
    if(__atomic_load_n(&g_nw_lock,__ATOMIC_RELAXED))return;
Put this before the exchange.
L3. cpu_id() in nmiwd_nmi reads GS. In the 2-instruction window after the exit swapgs and before iretq, GS is the user base. The window is harmless in practice, because a CPU0 stalled for 3 s can't sit there, and g_nw_hit is checked first.
L4. Nested NMI: an exception inside the handler would re-enable NMIs on iretq and reuse IST1. crash_bt validates via cx_va_ok/cx_ks_text, and only one NMI is sent per stall, so this is acceptable.
L5. COM1 mirror: non-blocking (THRE check). It writes 16 bytes into the FIFO; on a UART without FIFO the extra bytes are dropped, which is harmless. The last-4 dedupe will not stop per-frame output if the patch adds more crumb sites. That is still non-blocking, just noisy.

## OK as is
- Crumb ring: one lock xadd per entry. The reader reads the fields, then checks seq; a torn slot shows "(being written)". All pointers are static strings, so torn writes are harmless.
- Box drawn without locks: cosmetic only. Recovery calls fb_shadow_invalidate.
- The g_frz_hold/g_frz_sleep sections run with IRQs on, so the PIT keeps ticking. Not respecting them is fine for an IRQ-off detector, apart from M1.
