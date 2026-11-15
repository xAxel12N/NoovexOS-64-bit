# sweep4 report: core / mem / arch / sys (partial), v11.02

Reviewer: Sweep4. Read-only, about 20 minutes. No repo edits. No build was run.

## Bearing on the Start-menu freeze (brief)
Nothing in my quarter explains the freeze by itself. One core behaviour makes a driver-side hang look like a whole-machine freeze, though:
- **bkl_lock spins with interrupts off** (arch/x86_64/cpu/percpu.c:23-31). The BSP timer IRQ (vector 32) and every PIC IRQ except 1/12 call bkl_lock() from IRQ context (arch/x86_64/interrupts/interrupts.c:93). So while one CPU holds the BKL in a long poll loop (for example an xHCI command or control-transfer wait inside a UI-path call), these stop on the BSP: other CPUs, the BSP timer bottom half (freeze_tick, stk_check), and IRQ-driven input.
- g_ticks is still counted, because it is incremented before bkl_lock (interrupts.c:89). A clock that still ticks therefore does **not** prove that the BKL is free.
- If the USB driver waits for completions while holding the BKL, the waits should go through bkl_drop()/bkl_take() (as smp_enable/smp_disable do) or be bounded by hr_ns().
- Suggested check for the driver owners: in xh_cmd_s and the control-transfer wait in drivers/usb/xhci/controller_init.c, confirm that the wait loop has an hr_ns() deadline and does not run with IF=0 while holding the BKL.

## HIGH
None confirmed in the code I read.

## MEDIUM

### M1. Native INT 0x80 syscalls use user pointers without checking them
- **Where:** sys/native/syscall.c:100, 105-106, 115-116, 121, 126, 131-132, 139, 141, 154-158, 161-162. The pre-validation in core/proc/jobs/thread_groups_and_exit.c:279-283 covers only syscalls 1/4/5/157/31/32/145.
- **Problem:** jc_syscall returns 0 for many numbers, and those fall through to syscall_dispatch, which uses the raw pointer:
  - 30 open(name)
  - 35 readdir: writes up to 64 bytes to rcx
  - 44/93 text(str)
  - 52 getmouse: writes three ints to any non-NULL rbx/rcx/rdx
  - 82/83/95 msg send/recv
  - 102/103 sock send/recv
  - 120 getenv: writes up to rdx bytes to rcx
  - 121 setenv
- **Failure scenario:** a buggy native app calls getmouse with a garbage pointer, or getenv with a bad buffer. The kernel either writes into kernel memory (all of it is identity-mapped and supervisor-writable) or takes a ring-0 #PF, which ends in panic/RSOD. The process should have received SIGSEGV instead.
- **Fix:** in jc_syscall's first switch, add jc_ustr() for the string arguments (30, 44, 93, 121, plus rbx of 120) and jc_umapped_w() for the output buffers (35 with 64 bytes; 52 with 4 bytes per non-NULL pointer; 83/95/103 with the cap; 120 with cap), plus jc_umapped() for the 82/102 input buffers. On failure, return -1.

### M2. The preempt demo leaks a BKL depth level on every run
- **Where:** arch/x86_64/interrupts/interrupts.c:71 together with core/proc/sched.c preempt_run, preempt_run_elf and compositor_run.
- **Problem:** isr_dispatch takes bkl_lock() (depth+1) before isr_body. When the last demo process exits, isr_body calls k_longjmp(g_preempt_kern), skipping isr_dispatch's bkl_unlock(). jc_enter avoids this with bkl_take(bd), which rewrites the depth; preempt_run has no equivalent.
- **Failure scenario:** with SMP on, a shell user runs the preempt, IPC or compositor demo. Afterwards the BSP's outermost bkl_unlock only brings the depth down to 1 and never releases the lock. The APs then spin in bkl_lock forever: jobs stop scheduling and smp_disable times out.
- **Fix:** save `int d=bkl_held()?g_bkl.depth:0;` before k_setjmp in the three demo runners, and restore it after the longjmp returns (for example with `g_bkl.depth=d`, or bkl_unlock until the depth equals d). Alternatively, call bkl_unlock() just before k_longjmp at interrupts.c:71.

### M3. IRQ bottom half runs inside the interrupted code's BKL critical section
- **Where:** arch/x86_64/interrupts/interrupts.c:93 and percpu.c:25.
- **Problem:** bkl_lock is recursive per CPU. If BSP kernel code holds the BKL with IF=1 (for example a hr_delay loop in a driver) and the timer fires, isr_body runs ps2_drain, ps2_kbd_tick, freeze_tick and stk_check *inside* that critical section. The BKL gives no exclusion against same-CPU IRQs.
- **Failure scenario:** any structure that both the interrupted code and the timer path touch (input queues, the freeze watchdog state) can be seen half-updated, which gives sporadic input corruption or lockups.
- **Fix:** document that bottom-half code must be IRQ-safe, or have the IRQ path skip its work when `g_bkl.owner==cpu_id()` and the CPU was interrupted in kernel mode, deferring it to the next tick. That is the bkl_try pattern already used for vectors 33/44.

## LOW
- **L1.** EOI is sent on spurious IRQ7/IRQ15 (interrupts.c:80 and include/nvx/arch/x86_64/interrupts/interrupts.h:32). pic_eoi does not check the ISR register, so a spurious IRQ15 sends a slave EOI that should not be sent. It is harmless today because handlers run with IF=0. Fix: read the ISR (OCW3 0x0B) for irq 7 and 15 before sending EOI.
- **L2.** Native sleep (case 16/71, thread_groups_and_exit.c) computes `g_ticks+(ms+9)/10`. A huge ms makes `ms+9` wrap, so the wake time ends up in the past and the call returns immediately. Clamp ms first.
- **L3.** Native mmap (sys/native/syscall.c:147-150) puts no limit on `len`. Pages are mapped until frames run out, and the frames already mapped stay mapped on failure. sbrk/brk already clamp at 256 MB; clamp mmap the same way.
- **L4.** mm_guard (mem/pmm_vmm.c, near the end) splits a 2 MB page and flushes only the local TLB, with no smp_tlb_shootdown. Safe if it runs only before the APs start; otherwise other CPUs keep the guard page mapped.
- **L5.** The 32-bit HPET wrap in hr_hpet_count (arch/x86_64/time/hrtimer.c:25) is detected only if the counter is read at least once per wrap period (about 5 minutes at 14.3 MHz). This matters only when HPET is the clock source (no invariant TSC). Low risk.
- **L6.** do_fork (core/proc/sched.c, Stage 9) returns -1 if pframe_alloc fails partway through the copy. The child PML4 and the frames already copied are leaked.

## Checked and found OK
- Every EOI path in isr_dispatch/isr_body: PIC IRQs, LAPIC timer on BSP and APs, IPIs, and the 255 catch-all through lapic_eoi_any.
- jc_timer/jc_preempt EOI before jc_leave.
- jc_enter's bkl_drop/bkl_take depth restore.
- Ticket-lock ordering in bkl_lock/bkl_try/bkl_unlock.
- hr_ns TSC math: 128-bit multiply, so no overflow.
- hr_muldiv ranges.
- pframe_alloc/free refcount and hint logic; hp_* bitmap tail masking.
- vm_walk create path; vm_fault_user lazy/file/COW re-walk-and-recheck after allocation, including smp_tlb_sync on COW copy.
- sigreturn (case 143) rflags masking: IOPL cannot be set.

## Coverage (honest)
- **Fully read:** arch/x86_64/interrupts/interrupts.c, core/proc/sched.c, arch/x86_64/cpu/percpu.c, arch/x86_64/cpu/cpu_init.c, arch/x86_64/smp/smp_sched.c, arch/x86_64/time/hrtimer.c, mem/pmm_vmm.c.
- **Partly read:** arch/x86_64/boot/isr64.S (stubs + isr_common), core/proc/jobs/faults.c (lines 1-140), sys/native/syscall.c (dispatch switch), core/proc/jobs/thread_groups_and_exit.c (jc_syscall and jc_leave).
- **Not read:** boot/ (all, including the duplicate myboot trees), the rest of core/ (init, auth, installer, update, crash, media, etc.), mem/heap.c, slab.c, swap.c, arch/x86_64/smp/smp.c, early_fault.c, ap_trampoline.S, boot64*.S, userspace.S, all of sys/linux/ (about 5,000 lines of Linux-ABI syscalls; the highest-value unreviewed area for user-pointer bugs), fs/, net/, lib/, include/, tools/.

Roughly 2,000 of the about 46,000 lines in the quarter were reviewed in depth, under 5%. Recommended next sweep: sys/linux/syscalls/*.c (user-pointer checks), mem/heap.c and slab.c, and fs/vfs.
