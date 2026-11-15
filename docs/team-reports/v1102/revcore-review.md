# RevCore review: b-core (v11.02)

Note on scope: `git -C wt/b-core diff main` does NOT show the core change. main already contains b-core
(merge 6e01a53b), so that diff is only the reverse of the other branches (USB/camera/recorder hunks).
Reviewed instead: commit 3813b65f (`git show 3813b65f`), which is identical to what is in main for
mem/heap.c and sys/native/syscall.c (`git diff 3813b65f main -- mem/heap.c sys/native/syscall.c` is empty).

## 1. mem/heap.c kmalloc/kfree IRQ-off section: SHIP

Checked:
- Return paths. kmalloc (heap.c:65): save and restore are on the same line around kmalloc_raw, which has
  several returns but all of them go back to the restore. kfree (heap.c:90-97): the early returns (bad block,
  double free) happen before the save, and there is one exit after the restore. No leaks.
- Nesting. cpu_irq_save is pushfq+cli and cpu_irq_restore only does sti if IF was set (cpu.h:13-14), so nested
  or IRQ-context callers (IF already 0) never get IRQs turned on by mistake. Calls from IRQ context still work.
- BKL. No BKL take, no waiting, no spinning inside the section. hb_mark, poison memset, the OOM log,
  krealloc's copy and kcalloc's zeroing all stay outside the IRQ-off region.
- Reentry. ISRs take the BKL recursively on the same CPU (interrupts.c:92), so before this change an IRQ
  allocating during kmalloc/kfree really could corrupt the free list. The fix is correct.

Findings (none block shipping):
- F1 (low, conf high) heap.c:80-82: heap growth calls hp_alloc_run with IRQs off. hp_alloc_run
  (pmm_vmm.c:100-104) scans the bitmap and zeroes at least HEAP_EX_CHUNK = 64 MiB (more if the request is
  bigger). Roughly 10-30 ms with IRQs off means 1-3 lost PIC timer ticks (g_ticks drifts) and delayed xHCI
  and PS/2 IRQs. This happens at most HEAP_EX_MAX times per boot, so it is not a freeze. The upside is that
  the growth path, which was racy before, is now atomic. Follow-up for v11.03, not now: zero the chunk outside
  the IRQ-off section. That means hp_alloc_run needs a no-zero variant plus a reserved g_ex slot, which is too
  big for a late patch.
- F2 (info) heap.c:72 heap_place's tail walk (up to 1M steps) can run with IRQs off, but only when g_tail is
  stale. Rare.
- F3 (info) Disabling IRQs gives no protection against other CPUs. That still depends on every AP caller
  holding the BKL. This is unchanged by the commit.

## 2. sys/native/syscall.c:205 sleep (case 71) drops the BKL: SHIP

Checked:
- Depth. bkl_drop (percpu.c:51) saves depth, sets it to 1 and unlocks, so the lock is fully released.
  bkl_take(d) (percpu.c:52) locks and puts back depth = d. isr128_c (interrupts.c:98) always holds the BKL
  here, so bd >= 1. The same pattern is already used in smp_sched.c:102/117, unpack.c:31/35 and faults.c:101.
- Ticks. g_ticks++ runs in isr_dispatch (interrupts.c:89) before bkl_lock, and cpu_idle does sti;hlt/mwait,
  so the wait keeps advancing. The old code held the BKL across the loop. If the sleep ran on an AP, every BSP
  IRQ (timer body, xHCI, freeze_tick) would spin in bkl_lock with IRQs off for the whole sleep, which froze
  the UI. The change removes a real cause of freezes.
- Exit. cpu_irq_off comes before bkl_take, so the syscall returns with IF=0 as the int-gate path expects.
  bkl_lock also handles a pending tlb_pending. Good.
- Shared state while unlocked. The loop reads only g_ticks (volatile) and the locals until/bd. `r` is this
  thread's own kernel-stack frame. g_frz_hold is set before the drop and restored after the take, the same
  as before. The 1 h clamp is fine.
- Preemption. The timer preempt path (interrupts.c:66-75) also switches tasks from ring 0. Before, a native
  task preempted mid-sleep carried the BKL (depth 1) into the next process. Now the ISR takes and releases
  the BKL by itself, which is cleaner.

Finding (pre-existing, does not block):
- F4 (low-medium, conf medium) percpu.c:52: bkl_take overwrites the depth instead of adding to it. If the
  timer preempts task B (inside, say, case 51 waitkey/kidle with the BKL at depth 1) and switches back to
  sleeping task A on the same CPU, A's bkl_take finds owner==me, so depth goes 1 -> 2, and then it is
  overwritten to bd=1. A's isr128_c unlock then frees the BKL while B still thinks it holds it. Ownership is
  tracked per CPU, not per task, so this hole existed before. This commit only adds one more drop/take site.
  A safe, tiny patch that is correct for every existing caller:

```c
void bkl_take(int depth){ bkl_lock(); if(depth>1)g_bkl.depth+=depth-1; }
```

  After a fresh lock the depth is 1, which gives the same result as today. After a recursive lock it adds
  to the depth instead of wiping out the other holder's count. Optional for v11.02. If you take it, run
  ccheck.sh on arch/x86_64/cpu/percpu.c.

## Verdict
- heap.c IRQ-safe kmalloc/kfree: SHIP (follow-up F1 in v11.03).
- syscall.c sleep BKL drop + clamp: SHIP (it also removes a freeze path when the sleep runs on an AP).
- Neither change is related to the Start-menu freeze. The prime suspect is still the USB wait path.
