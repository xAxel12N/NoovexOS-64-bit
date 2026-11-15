# nmi report (branch v6-nmi)
What: NMI hard-lockup watchdog, IPI variant (works in QEMU TCG and on real SMP). Perfmon variant NOT done (time box).
- arch/x86_64/smp/smp_sched.c: smp_nmi_cpu(cpu) sends ICR delivery=NMI (0x4400, x2APIC via MSR 0x830 direct, xAPIC with busy-wait). ap_timer (100 Hz on each AP) calls nmiwd_check().
- core/crash.c: frz_bkl_wait (APs spinning on the BKL, IRQs off) also calls nmiwd_check() when me!=0.
  nmiwd_check (any AP, try-lock): heartbeat = g_ticks (PIT IRQ0 on CPU 0). If unchanged for >=3 s (TSC), send ONE NMI to CPU 0 per stall.
  nmiwd_nmi (vector 2, already IST1 per CPU via us_init): only acts on our request and if g_ticks is still stale; records RIP/RSP/RFLAGS, kname_fmt(RIP), crash_bt call chain (kernel mode only), cx_where (stage/input), crumb_dump (weak, from Trace) -> red box at y=110 (under the BKL box at y=30), static buffers only (IST stack is 4 KB), no locks/alloc/klog. Atomic busy guard; drawn once per stall.
  Recovery: when g_ticks moves again, crash_tick invalidates the fb shadow, saves crash-report.txt / CRASH.TXT, klog + toast "NoovexOS froze for N s".
- arch/x86_64/interrupts/interrupts.c: isr_dispatch vector 2 -> weak nmiwd_nmi(r) (was plain return).
- include/nvx/core/crash.h: g_nmiwd_on, nmiwd_check, nmiwd_nmi, smp_nmi_cpu, weak crumb_dump decl.
Commands (via shell `crash`): `crash nmiwd` status, `crash nmiwd off|on` (persisted nmiwd.txt / NMIWD.TXT, read at desktop+3 s), `crash test hardlock` (cli + 8 s TSC spin on CPU 0; box should appear at ~3 s, erase + toast at 8 s).
Default ON. Needs SMP online (g_smp_on); single-core = inactive.
Tested: compile-check only (no new warnings). NOT run in QEMU. Sim: boot with -smp 2, run `crash test hardlock`.
Merge note: Trace defines crumb_dump in core/crash.c; with the weak declaration in crash.h its definition simply becomes weak - fine.
Limits: an AP stuck spinning on a non-BKL lock with IRQs off cannot watch; all-CPUs-stuck only caught if some AP is in the BKL spin or idle.
