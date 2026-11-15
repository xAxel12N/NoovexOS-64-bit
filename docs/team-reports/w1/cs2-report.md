# cs2 — IRQ / spinlock / BKL sweep (whole OS minus third_party), v11.06

Scope: every cpu_irq_off/on/save/restore, raw cli/sti (cpu.h, hrtimer.c, early_fault, cpu_idle's `sti;hlt`),
bkl_* (lock/unlock/try/drop/take/yield), in_lock/in_lock_x, smp_lock, xh_poll_busy, g_sf_lock, g_apcm_busy,
g_frz_bkl_busy, g_nw_lock, libc pthread spin. ~200 sites read.

## Bottom line for the taskbar/Start-menu freeze
The v11.06 input spinlock is **clean**: in_lock/in_lock_x are only ever held around
in_mq_take_locked / in_mq_rebase_locked / plain field copies (input.c:8-27, ui/input.c:288,
present_and_pump.c:127/135). Nothing inside takes in_lock again, the BKL or any other lock, and IRQs are off
for the whole hold, so the same CPU cannot re-enter from an ISR. ISR producers (ps2_drain -> in_btn_raw) run under
BKL->in_lock; nobody takes the BKL while holding in_lock, so there is no ordering deadlock. Every in_lock has its
unlock on all paths (no early returns inside). **The click freeze is not a lock bug in these files.**
The BKL ticket lock (percpu.c:23-53) is balanced, and the main loop resets depth to 1 each pass (main.c:309).
A CPU spinning for the BKL calls frz_bkl_wait -> it draws a box and runs nmiwd_check, so a BKL deadlock
WOULD show a red box. That matches the brief's view: the hang has IRQs on, ticks running, heartbeat beating.

The one lock-related path that gives exactly "frozen, no red box, ticks + heartbeat OK" is finding 1.

---

## 1. MEDIUM (conf 60% it is reachable from the desktop; 95% that the behaviour is real) — native `waitkey` (syscall 51) holds the BKL forever and turns off every watchdog
sys/native/syscall.c:125
```
case 51: { ... int hold=g_frz_hold; g_frz_hold=1; int k; while((k=kpop())<0){ extern void kidle(void); kidle(); } g_frz_hold=hold; ...
```
Entered through isr128_c (interrupts.c:99), which does `bkl_lock()` first. Any ring-3 program calling `getch()`
(libc.c:693) / `waitkey()` (user.h:40, used by user_app.c:29) spins here **holding the BKL** until a key comes.
- Job on an AP (g_smp_sched): CPU 0's main loop blocks at its next bkl_yield/bkl_lock -> the whole desktop stops.
- Job on CPU 0 (jc_step from the main loop): main loop does not run until a key comes.
In both cases `g_frz_hold=1` silences freeze_tick, frz_bkl_wait (crash.c:304) and nmiwd_check (crash.c:326), and
kidle() bumps g_ml_beat -> **no red box at all**, mouse clicks do nothing, one keypress "unfreezes".
Owner test: when frozen, press a key (Space). If the desktop comes back, this is it.
Patch (wait without the BKL, like case 71 sleep does; keep frz_hold only on CPU 0, where the main loop really is stopped):
```
old: case 51: { extern volatile int g_frz_hold; int hold=g_frz_hold; g_frz_hold=1; int k; while((k=kpop())<0){ extern void kidle(void); kidle(); } g_frz_hold=hold; r->rax=(u64)(long)k; } break;  /* waitkey */
new: case 51: { extern volatile int g_frz_hold; int c0=cpu_id()==0; int hold=g_frz_hold; if(c0)g_frz_hold=1; int bd=bkl_drop(); int k; while((k=kpop())<0){ if(c0){ extern void kidle(void); kidle(); } else cpu_idle(); } cpu_irq_off(); bkl_take(bd); if(c0)g_frz_hold=hold; r->rax=(u64)(long)k; } break;  /* waitkey */
```
(kpop is a single-consumer ring; with this patch the AP job and the desktop may compete for a key — acceptable,
far better than a frozen desktop. The real fix is to block the job in the jc scheduler, later.)

## 2. LOW (conf 90%) — USB/PS2 HID decode updates mouse_dx/dy/scroll outside in_lock
drivers/usb/xhci/hid_keyboard_mouse.c:72, 73, 84, 85, 108 and drivers/input/ps2/ps2.c:110 do plain
`mouse_dx+=dx; mouse_dy-=dy;` / `mouse_scroll+=wz` while every other writer/reader (in_rel, in_btn_locked's queue
snapshot, process_input drain at ui/input.c:288, busy_pump/desk_nested_step) uses in_lock. A PS/2 or vmmouse
IRQ landing in the middle of the USB RMW (or vice versa) loses motion; on SMP with an AP running a poll it
can also desync the queued dx snapshot from mouse_dx. Not a hang, just lost/jumpy deltas.
Patch:
```
hid_keyboard_mouse.c (each of the 3 sites):
old: if(dx||dy){ mouse_dx+=dx; mouse_dy-=dy; mouse_evt=1; }
new: in_rel(dx,dy);
old: if(wz)mouse_scroll+=wz;
new: in_scroll(wz,0);
ps2.c:110:
old: mouse_dx+=dx; mouse_dy+=dy;                   /* loops apply my -= dy, so pass raw dy */
new: in_rel(dx,-dy);                              /* in_rel does mouse_dy-=dy, so this keeps raw dy */
```
Run `python3 tools/check.py --only usb` after (hidmouse/hidparse tests read mouse_dx directly; values are unchanged).

## 3. LOW (conf 85%) — native preempt exit leaks one BKL depth while other processes run
sys/native/syscall.c:93: on exit with `live>0` it does `cpu_irq_on(); for(;;)cpu_halt();` from inside isr128_c,
so isr128_c's `bkl_lock()` is never undone. The timer ISR then switches processes and returns to ring 3 with
CPU 0 still owning the BKL -> every AP blocks on BKL (jobs, syscalls, AP IRQs) until preempt_run ends
(pk_bkl_restore repairs depth). Only the `preempt` demo command; frz_bkl_wait boxes would appear on APs after 5 s.
Patch:
```
old: if(live==0){ g_preempt_on=0; cpu_set_pt(g_kern_cr3); k_longjmp(g_preempt_kern,1); } cpu_irq_on(); for(;;)cpu_halt(); }
new: if(live==0){ g_preempt_on=0; cpu_set_pt(g_kern_cr3); k_longjmp(g_preempt_kern,1); } bkl_unlock(); cpu_irq_on(); for(;;)cpu_halt(); }
```

## 4. LOW (conf 70%) — legacy mouse readers use cli/sti instead of in_lock
core/init/setup.c:287,293,550; core/init/bootmenu.c:159; core/init/installer/disk_picker.c:30,80;
disk_manager.c:142; usb_clone_service.c:143; shell/commands/terminal_keys_and_render.c:104;
network_and_apps.c:247; usbhid.c:44. They take mouse_dx with `cpu_irq_off()...cpu_irq_on()` (unconditional sti)
and do not drain/rebase the button queue. Local cli does not exclude another CPU holding in_lock, and the
unconditional `cpu_irq_on()` would enable IRQs if a caller ever ran them IRQs-off. Today all are main-loop/shell
context on CPU 0, so only theoretical. Fix pattern (where wanted):
`cpu_irq_off(); ... cpu_irq_on();` -> `u64 lf=in_lock_x(); ... in_unlock_x(lf);` (declare `extern u64 in_lock_x(void); extern void in_unlock_x(u64);`).

## Checked and OK (no action)
- bkl_lock/unlock/try/drop/take/yield balance and recursion; ISR nesting on owner CPU (depth++); TLB shootdown
  skips bkl_wait CPUs (smp_sched.c:43) so no IPI-vs-BKL deadlock.
- isr_dispatch: v33/44 bkl_try path, every other vector bkl_lock/unlock balanced; spurious IRQ7/15 EOI logic.
- jc_enter (faults.c:94-112) setjmp/bkl_drop/bkl_take pairing; jc_leave absorbs the syscall's BKL via bkl_take.
- power_sleep (power.c:140-167) took/unlock balanced; drop/take around idle.
- syscall 71 sleep, main-loop idle (main.c:308), smp_disable/enable drop/take pairs.
- heap (heap.c:66-105) save/restore balanced on all paths (early returns are before the save).
- xhci: xh_poll_busy (try-lock, released on the only path), controller_init.c:218-221, xh_irq_on.
- igpu.c:59-62, vmmouse.c:52, hrtimer.c:27/52-62, smp_nmi_cpu, frame_check (if(irq) pair).
- sysfiles g_sf_lock: logline is try-lock (never spins), flush/boot spin only in main context -> no self-deadlock.
- webwork rings are SPSC; apcm/smp_lock/libc pthread spins are not IRQ-context.
- crumb()/in_mark: lock-free, serial output non-blocking.

## Not lock-related but seen (for other hunters)
- graphics/ui/input.c:63 `nvxlc_waitkey` loops `kpop()`+`cpu_idle()` without polling xHCI -> with a USB-only
  keyboard it never returns (heartbeat stops, so the freeze box would show). Add `if(xhid_n)xhci_mouse_poll();`.
- present_and_pump.c:127: inside a click (g_pi_depth>0) desk_nested_step only moves the cursor; button events stay
  queued (by design) — a modal loop waiting for a click inside a click handler will never see it.
