# irqc: places CPU 0 can hang with IRQs off near the left-click path (outside USB)

## Fixed (branch v6-irqc)
1. mem/heap.c kmalloc_raw (old :80-82) -> hp_alloc_run zeroed a 64 MiB chunk (8M qword stores, roughly 10-50 ms) with IRQs off inside kmalloc.
   Can run on a click if any allocation on the click path (marquee, popups, redraw) exhausts the low heap. Worst case: long IRQ-off stall, not a permanent hang.
   Fix: new heap_grow() called from kmalloc after cpu_irq_restore. It claims pages with IRQs off (mem/pmm_vmm.c new hp_alloc_run_nz, which does not zero), zeroes them with IRQs back on, then installs the arena under IRQ-off and retries.
   hp_alloc_run() keeps its old behaviour (claim, then zero). Note: if the caller of kmalloc already has IRQs off, the zeroing still runs with IRQs off.
2. drivers/input/input.c in_mq_rebase_locked / in_mq_rebase: `for(k=g_mq_r;k!=g_mq_w;k++)` loops about 2^32 times if r ever passes w (in_mq_take is unlocked, so this can happen in a race with in_mq_clear).
   It runs with IRQs off from graphics/ui/desktop/present_and_pump.c:127,135 on every pump. Now bounded to MQ_N. in_mq_rebase calls the _locked version.

## Audited, no change needed
- heap.c kmalloc_raw free-list walk: bounded (64 steps per class, and it breaks on a bad block). kfree coalesce: O(1). heap_place tail walk: bounded to 1M steps, but each step's hblk_ok->in_arena loops over g_nex (up to 1024). Only runs when g_tail is stale, which is rare.
- heap_check: bounded to 2M steps (IRQs on).
- percpu.c:135 bkl_lock spins with IRQs off and no bound. The IRQs-off part is needed (the ticket lock deadlocks if an IRQ on the same CPU takes the BKL). Every 1M spins it calls frz_bkl_wait, which draws the red box from the spinning CPU itself. No red box was seen, so CPU 0 was probably NOT stuck in bkl_lock (unless g_frz_hold or g_frz_sleep were set, or FBB was 0).
- bkl_unlock and bkl_try: no loops. cpu_irq_save/restore nesting in heap, input and BKL: all restore the saved flag. in_unlock only turns IRQs on if IF was set in the saved flags.
- in_btn_raw: lockless ring write, no loops. Callers: ps2.c:111, and USB hid_keyboard_mouse.c:73/85/107 call it without in_lock (USB, not mine).
- ps2.c:126 ps2_byte_fast is bounded to 16 bytes. ps2_drain is bounded by a 256-entry ring.
- core/crash.c:58-59 in_mark/in_unmark: plain stores, the 63-char copy is bounded. Cannot hang.

## Not mine, flag for owners
- graphics/ui/desktop/present_and_pump.c:127,135 use cpu_irq_off()/cpu_irq_on() without saving. If called with IRQs already off (for example from a nested pump or a poll path), they turn IRQs on unexpectedly. Should use cpu_irq_save/restore.
- I did not audit the timer IRQ (arch/x86_64/interrupts/interrupts.c) because it is outside my scope.
