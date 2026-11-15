# H6 report: lock / timer / SMP deadlock hypothesis (read-only, 12 min)

## Verdict
I found no path where a CPU holds the BKL with IRQs off while waiting for something that only another CPU or an IRQ can supply. Confidence that the drag-release freeze is a BKL/SMP deadlock: ~15%. Two side findings matter for diagnosis. (1) The freeze watchdog cannot see a BKL deadlock. (2) The watchdog can be switched off for good by g_frz_hold.

## Checked and cleared
- mem/heap.c:65,90-97 (v11.02 IRQ-off kmalloc/kfree): the IRQ-off section only walks lists, plus hp_alloc_run (mem/pmm_vmm.c:100), which is pure bookkeeping. It never waits for a tick and never takes the BKL. Reporting (hb_report, OOM log) runs after the IRQ restore. Cleared.
- Timer IRQ (arch/x86_64/interrupts/interrupts.c:90-92): g_ticks++ and lx_load_sample run BEFORE bkl_lock. If an AP holds the BKL, BSP ticks still advance by 1 and then the BSP spins with IRQs off. lx_load_sample only reads g_jp and takes no lock. Cleared.
- AP-side BKL holders (smp_sched.c:53,59,76; isr128_c/lsys_c): the user-job run path drops the BKL (core/proc/jobs/faults.c:101). Native sleep (sys/native/syscall.c:205) drops it. Linux syscalls do not idle-wait on g_ticks (grep in sys/linux finds no cpu_idle, hr_delay_us or while(g_ticks)). xhci waits (controller_init.c:187-203) are bounded by `iters` even if g_ticks stalls. Cleared.
- TLB shootdown spin (smp_sched.c ~40-46): skips CPUs with bkl_wait set and is bounded at 200000 spins. Cleared.
- bkl_drop/bkl_take depth (percpu.c:51-52): restoring the depth is correct when depth>0. The preempt path (ap_timer bkl_lock, then longjmp, then bkl_take(bd)) also ends up correct: depth++, then it is overwritten to bd=1, then ap_loop unlocks.
- Webworker on CPU1 (apps/internet/browser/webwork.c:127): runs without the BKL, but its kmallocs are in init only (webwork.c:76-78). Browser only, so not relevant to a plain drag.
- The main loop resets depth to 1 at main.c:309. That hides any depth leak on the BSP.

## Findings (ranked)
1. Diagnostic blind spot, 90% real (not a root cause). freeze_tick and stk_check are called from isr_body (interrupts.c:81), which runs AFTER bkl_lock (interrupts.c:92). If the BSP's timer IRQ is spinning on a BKL held by another CPU, or the BSP is stuck with IRQs off, there is no "NoovexOS is not responding" screen and no report.
   - Use for triage: ask the owner whether the red freeze box appeared after 5 s.
     - Box appeared: the main loop is stuck with IRQs on (UI or USB loop). Not a BKL deadlock.
     - No box: IRQs are off on the BSP, or there is a BKL deadlock or a triple fault.
   - Suggested fix: in isr_dispatch, call freeze_tick/stk_check for v==32 before bkl_lock. freeze_tick only draws into FBB. Its cx_save at 3000 does file I/O, so gate it with `if(bkl_try()){...;bkl_unlock();}`.
2. g_frz_hold can latch at 1, 60% real (masks the watchdog). The native sleep at syscall.c:205 saves and restores a global. Overlapping sleeps on two CPUs (A saves 0, B saves 1, A restores 0, B restores 1) leave it at 1 forever, and freeze_tick then returns at crash.c:255 on every tick. power.c also sets it.
   - Fix: make it a counter. Use `__sync_fetch_and_add(&g_frz_hold,1)` / `__sync_fetch_and_sub(&g_frz_hold,1)` instead of save/restore.
3. bkl_take(0) acquires the BKL, low risk (20%). When the caller did not hold the BKL, bkl_drop returns 0 and bkl_take(0) still takes the lock with depth 1, so it leaks. This affects power.c:158 (inside power_sleep; ok, since took=1 there), syscall.c:205 (always entered with the BKL held via isr128_c), and faults.c:104. On the BSP, main.c:309 masks it. On an AP a leak would block every other CPU's timer IRQ (with IRQs off), which would be a silent total freeze. I found no concrete path that enters without the BKL.
   - Fix (percpu.c:52): `void bkl_take(int depth){ if(depth<=0)return; bkl_lock(); g_bkl.depth=depth; }`. Make sure faults.c callers always hold the BKL at setjmp, or the fix changes semantics there.

## Bottom line for the lead
Nothing in lock/timer/SMP explains a freeze that triggers at drag release. Look at the release path instead: apply_snap, the window content cache turning back on in desk_win_exposed, and the first full app draw after the drag. Finding #1 decides between the two: ask the owner whether the freeze box appeared.
