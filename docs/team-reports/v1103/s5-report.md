# S5 report - core/ arch/ mem/ lib/ hang search (read-only)

Verdict: nothing in core/arch/mem/lib by itself turns "mouse button released" into a hang. All the heap,
page-allocator, swap and timer loops have limits (they stop after a fixed count). The strongest finding in my area
is a different one: **the freeze watchdog is set up so it can never fire in the likely freeze cases**. So a real
freeze looks like a silent hard freeze with no "not responding" screen. That fits the owner's report, but it is
not the trigger. The trigger most likely lives in graphics/ui (release -> apply_snap / desk_win_exposed cache ON
-> app draw) or in drivers/usb, which is outside my area.

## 1. Freeze watchdog is blind when the BKL is stuck or IRQs are off (70% that this is why the freeze looks "total and silent"; 0% that it is the trigger)
- arch/x86_64/interrupts/interrupts.c:94 - every BSP IRQ except PS/2 (PIT vector 32 included) calls `bkl_lock()` before
  `isr_body()`. `freeze_tick()` (core/crash.c:253) is only called from inside isr_body (interrupts.c:81).
- arch/x86_64/cpu/percpu.c:23-31 - `bkl_lock` spins with IRQs off.
- Result: (a) if the BSP main loop is stuck in any loop with IRQs off (an IRQ-off kmalloc/kfree, a bkl_lock wait,
  an xHCI wait under irq_save), the PIT never arrives. (b) If an AP holds the BKL forever, the PIT handler itself spins in bkl_lock.
  In both cases freeze_tick never runs: no "NoovexOS is not responding" screen and no saved CRASH.TXT. This is
  exactly the kind of "entire OS froze" report we have.
- Fix: in isr_dispatch, for v==32 run the watchdog without the lock and before bkl_lock:
  `if(v==32){ g_ticks++; ...; extern void freeze_tick(regs_t*); freeze_tick(r); }` and remove the call at
  interrupts.c:81. freeze_tick only reads g_ml_beat and draws with cx_* straight to FBB. At 30 s it calls cx_save,
  which touches the FS and USB, so set g_cx_nofs/g_cx_nousb when `g_bkl.owner!=0 && g_bkl.owner!=-1` or when
  g_heap_busy is set. Also: each main-loop stage already calls ml_mark, so the report will show
  g_ml_stage = where the loop got stuck (stage 7 = process_input, 13 = compose_present, 8 = usb_hotplug and so on). That
  is the most useful diagnostic for the next real-hardware run. Add g_bkl.owner / g_bkl.next / g_bkl.serving and
  g_heap_busy to the freeze report.

## 2. v11.02 IRQ-off kmalloc: the heap can grow with IRQs off for tens of ms (25% that it matters, as a stall, not a hang)
- mem/heap.c:65 + 80-82: when the low arena and the extra arenas are full, `hp_alloc_run(0x4000000>>12)` scans the bitmap
  and zeroes 64 MB (pmm_vmm.c:100-106) **while IRQs are off**. That is roughly 10-40 ms on real hardware with no
  interrupts, and the xHCI event ring can fill. heap_place's tail walk (heap.c:59) can also run up to 1M steps with IRQs off.
  These are not endless loops. Under memory pressure they show up as a hitch, not a permanent freeze. A drag ending
  that allocates a full-window content cache (desk_win_exposed turns the cache ON at release) is a plausible
  trigger for a first-time heap growth.
- No retry-forever loop on OOM: kmalloc returns 0 and logs it (heap.c:67). Callers that loop on a NULL result were not checked (outside my area).
- Fix: in kmalloc_raw, drop to `cpu_irq_restore` around hp_alloc_run + zeroing (take the run first, then publish
  g_ex[] with IRQs off). Or zero the chunk outside the IRQ-off region. Also add a real spinlock (not just
  local cli). kmalloc is only SMP-safe while every caller holds the BKL. The callers I checked do: AP IRQ paths
  don't allocate, and wk_ap_turn uses its own arena. heap.c:38 g_heap_busy/g_heap_reent count re-entry but don't prevent it.

## 3. Nested panic = a silent halt that looks like a freeze (15%)
- core/crash.c:236 `if(in_panic){ for(;;)cpu_stop(); }`. If cx_build/stack walk or cx_save (FS/USB) faults inside
  the panic, the second panic halts silently, maybe before anything is drawn. The panic draws to FBB with g_gfx=0
  (crash.c:211). If scanout is on a GPU surface (NVIDIA path), nothing drawn to FBB is visible, so the red screen looks like a freeze.
- Fix: on the nested path, draw a minimal red bar straight to FBB, then `power_reboot` after ~15 s instead of halting forever.
  In cx_panic_main, draw before cx_save.

## 4. Native sleep syscall disables the watchdog for up to 1 h (5%)
- sys/native/syscall.c case 71: sets g_frz_hold=1 while it sleeps, so freeze_tick returns early (crash.c:255). If
  this runs on the BSP from an app, a freeze during that time is never reported. v11.02 added a cap of 3600000 ms. Better:
  hold the watchdog only while this CPU is actually inside the loop, and cap it at about 10 s.

## Checked, no hang found
- Main loop (core/init/main.c:296-374): the idle path (line 308) arms a LAPIC one-shot at about 1.5 ms when xhid_n and hr_precise.
  Otherwise the PIT wakes it every 10 ms. It re-checks mouse_evt/in_mq/kpending with IRQs off before sti;mwait (no lost wakeup).
  Line 309 `if(bkl_held())g_bkl.depth=1` is harmless.
- BKL ticket lock / bkl_try / bkl_yield (percpu.c): correct. The PS/2 path uses bkl_try. The tlb_sync wait has a limit and skips bkl_wait CPUs.
- hrtimer delays have limits (hrtimer.c hr_delay_us). ap_loop is fine. wk_fault_land spins forever only on CPU1 without holding the BKL.
- swap/zs_reclaim (budget 200000) and hp_alloc_run (linear) have limits. Slab has no loops.
- 6d88d5df..v11.02 in core/arch/mem/lib: only version strings, heap IRQ-off, sleep bkl_drop.
