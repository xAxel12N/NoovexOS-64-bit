# c8 report: SMP BKL contention after Start/taskbar clicks (branch x-c8, commit e9706f50)

## Cause
The Start/taskbar click does not hand the APs any extra work. The trouble is `ap_loop` (arch/x86_64/smp/smp_sched.c, old line 84).
Every AP called `bkl_lock()` on every wake, even with nothing to run: 100 Hz timer ticks per AP, plus every
`smp_kick_idle()` IPI from `jc_kick_waiters` and `jc_next_wdl`. It took the lock just to run `jc_pick()`. With 7 APs (11 on the owner's PC) that makes
a FIFO ticket convoy. The main loop drops the BKL in its idle section (main.c:308, ml_mark 14), and on `bkl_take` it then had to queue
behind every AP that had already taken a ticket. Each AP held the lock for a full `jc_pick` scan over JMAX, which also runs timers.
In QEMU -smp 8 this showed as "lag: updates (boot partition)" (= ml_mark 14), and all APs sat in bkl_lock+0x90.

## Changes
- core/proc/jobs/faults.c:78 `jc_ap_peek()`: a new lock-free, conservative "might any job be runnable" check. It checks the same
  things as jc_runnable: S_RUN, wake<=g_ticks, a deliverable signal, wdl due, itimer it_next due, and STOP+SIGKILL. It has no side effects.
- arch/x86_64/smp/smp_sched.c:80-94 `ap_loop`: an AP takes the BKL with `bkl_lock` only when `jc_ap_peek()` says there may be work.
  Otherwise, at most ONE AP per tick (a CAS on `g_ap_sweep`) does a full `jc_pick` sweep, using `bkl_try` (it never queues). The sweep
  keeps lx_ptimer_tick, jc_itimer and the lxn_cpu_hold paths running as before. Idle APs never queue on the BKL. Stats: g_ap_full/g_ap_sweeps/g_ap_skip.
- arch/x86_64/cpu/percpu.c:23-47 `bkl_lock`:
  - AP admission cap: when SMP is on, at most `BKL_APQ_CAP`=2 APs can hold a ticket and wait at the same time (`g_bkl_apq`).
    Extra APs spin before taking a ticket, holding nothing. IRQs are left at the caller's state while they spin, and PCPU->bkl_wait=1 is set so
    smp_tlb_sync does not wait on them. So CPU 0 waits for at most the owner plus 2 AP critical sections.
  - CPU 0 priority flag `g_bkl_bsp_want`: set while CPU 0 waits for its ticket. While it is set, APs take no new tickets.
    This lives in bkl_lock itself, so main.c was NOT touched.
  - bkl_try, bkl_unlock, bkl_drop and bkl_take are unchanged. Nested and owner paths are unchanged.

## Tests
- /home/claude/ccheck.sh on percpu.c, smp_sched.c and core/proc/jobs.c (the parent of faults.c): clean.
- `python3 tools/check.py --build --only build`: 4/4 ok (841 s). Note: the worktree's `build/` is a SYMLINK to the main repo's build/,
  so the main repo's build/kernel64.elf is now the x-c8 kernel. The ISO is at wt/c8/tools/out/check.iso.
- QEMU run C config (/tmp/q/runC.sh: -smp 8 on a 2-core host, -cpu max, no PS/2, mouse behind a hub on the 2nd xHCI, `usbsim biltema`,
  `desktop`), then a Start click, Start->About, a taskbar button, and Start open/close:
  - BEFORE (v11.06 ISO, same session and same steps): 36 idle-section lags in about 155 s, max 1066 ms (q-report earlier: 4.4 s and 15 s).
    `info registers -a` x3: 7-8 of 8 CPUs at bkl_lock+0x90.
  - AFTER (x-c8 ISO): 0 idle-section lags in about 240 s. The only lags were "drawing the screen" at about 50-60 ms (one at 579 ms during
    desktop start). The heartbeat came every 5 s. Samples x5: all APs at cpu_idle+0x27 (hlt), CPU 0 in cpu_idle/crumb. The Start menu, the About
    window and the taskbar all worked.
  - Serial log of the after run: /tmp/c8-after-serial.log. /tmp/repro/boot.img has been restored to the v11.06 ISO.

## Skipped / not done
- I did not touch xapps.c or main.c. The click handlers schedule no extra AP work. The convoy is the AP idle path.
- Long BKL holds inside AP Linux syscalls (lsys_c holds the BKL for the whole syscall) are not addressed. lx_slow_note already
  reports any over 150 ms. If the owner's freeze is one AP holding the BKL a long time, the next step is to add bkl_yield inside long syscalls.
- No USB or input files were touched, so the usb check was not needed.

## Fix round (branch y-c8, commit 7c594b16), applying r8-review.md
- MEDIUM 1 (faults.c jc_ap_peek): the runcpu skip now comes before the it_next check. A running job's itimer no longer makes every idle AP call bkl_lock.
- MEDIUM 2 (smp_sched.c ap_loop): the g_ap_sweep CAS now comes first, so only one AP per tick tries. If 5 or more ticks have passed since the last good sweep
  (g_ap_sweep_ok), that AP uses a blocking bkl_lock instead of bkl_try. This bounds POSIX-timer, wdl and lxn_cpu_hold latency to about 50 ms, with at most one queued AP per 50 ms.
- LOW 3: peek treats wdl as a deadline only for S_SLEEP, pollwake or fx_wait, the same rule as jc_wdl_due.
- LOW 4 (percpu.c bkl_ap_admit): the admit spin records w0 on its first failure and calls frz_bkl_wait every 0x10000 spin rounds
  (about the same as the 0xFFFFF pauses in the ticket spin), so a stuck AP shows the "kernel lock" box.
- LOW 5: bkl_try sets g_bklt0, so the longest-hold stats are no longer wrong.
- Tests: ccheck is clean on percpu.c, smp_sched.c and core/proc/jobs.c. QEMU was not run (as told).
