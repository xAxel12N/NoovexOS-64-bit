# L report: in_lock deadlock audit (v11.06), read-only

## Verdict
No self-deadlock or cross-CPU deadlock on in_lock is reachable in v11.06. in_lock is a leaf lock: no code inside any locked region calls anything that takes a lock, waits, or can fault. The taskbar/Start freeze is almost certainly NOT in_lock. A CPU 0 stuck on in_lock would also have shown the NMI box.

## What was checked
drivers/input/input.c:8-27. Every locked region:
- in_rel :10, in_btn_raw :16 -> in_btn_locked :13-15, in_mq_take :19 -> in_mq_take_locked :18 -> mq_sane :17, in_mq_clear :23, in_mq_rebase :25 -> in_mq_rebase_locked :24, in_buttons :26, in_scroll :27. All of them only touch plain globals and g_ticks. No calls out. in_xbtn :6 and in_gesture :28 take no lock.
- graphics/ui/input.c:288 (in_lock_x region): only in_mq_take_locked plus plain loads and stores. Loop bound ne<IN_MQ_N matches the static ex/ey/eb/et[IN_MQ_N], so no overflow or #PF. mouse_step(), which runs the click handlers, nested pumps and modal loops, is called at :289-291, AFTER in_unlock_x. Nested pumps therefore never run with the lock held.
- graphics/ui/desktop/present_and_pump.c:127 (desk_nested_step) and :135 (busy_pump): only in_mq_rebase_locked plus loads and stores. xhci_mouse_poll / tp_poll / process_input / compose_present all run outside the region. No return or goto inside any region, so the unlock is always reached.
- mouse_* are plain volatile ints (net/http/http.c:235), not macros. Nothing hidden runs inside a region.

## IRQ, NMI and exception re-entry
- in_lock does cli (cpu_irq_save, arch/x86_64/cpu/cpu.h:13) BEFORE it spins. The timer IRQ (freeze_tick), the PS/2 path (interrupts.c:92-93 ps2_drain -> mouse_feed -> in_btn_raw, ps2.c:111) and xHCI IRQ work (xh_event_w -> hid -> in_btn_raw) therefore cannot nest on the CPU that holds the lock.
- NMI (interrupts.c:86 -> nmiwd_nmi, crash.c:332) only calls cx_* drawing, kname, crash_bt and crumb_dump. crumb/in_mark (crash.c:59,88) take no lock. No in_lock is taken from NMI.
- Exceptions inside a region: none possible (indices are masked or bounded).

## Lock ordering against the BKL
- The only order in use is BKL -> in_lock: isr_dispatch does bkl_lock or bkl_try, then ps2_drain -> in_btn_raw (interrupts.c:92-93), and the same holds for the main loop under the BKL. No path takes the BKL (or any other lock) while holding in_lock, so ABBA is impossible.

## Can the boxes misattribute an in_lock deadlock?
- The BKL-wait box (frz_bkl_wait, crash.c:300, called only from bkl_lock's spin at percpu.c:28) cannot appear for a pure in_lock deadlock. It can appear only if the in_lock spinner already holds the BKL and another CPU waits on the BKL. Its text then blames "kernel lock held by CPU n", where n is really the CPU that is stuck on in_lock. That is misleading but impossible today, since no in_lock holder ever waits.
- If CPU 0 spun on in_lock, IF would be 0 and its PIT ticks (interrupts.c:91) would stop. ap_timer -> nmiwd_check (smp_sched.c:66, crash.c:324) would send an NMI after 3 s, and nmiwd_nmi would draw the NMI box (it requires rflags.IF=0, which holds). So "no box at all" rules out an in_lock hang on CPU 0, unless: SMP is off, nmiwd.txt=0, g_frz_hold/g_frz_sleep is set, or g_ticks_live=0. A hang on an AP shows no box, but the desktop loop runs on CPU 0.

## Real issues found (data races, not deadlocks), ranked
1. Unlocked read-modify-writes of the same variables that the locked regions read and zero: drivers/usb/xhci/hid_keyboard_mouse.c:72,84,108 (mouse_dx+=, mouse_dy-=, mouse_scroll+=), drivers/input/ps2/ps2.c:110 and :118 (mouse_scroll-=), drivers/input/vm/vmmouse.c:52 (irq_save only, no lock). If these run on a different CPU from the consumer, deltas or scroll are lost or doubled, and the queue's dx/dy snapshot (in_btn_locked :14) can disagree with rebase. Fix: route them through in_rel()/in_scroll(), or wrap in in_lock_x/in_unlock_x. Example: hid_keyboard_mouse.c:72 `if(dx||dy)in_rel(dx,dy);` and `if(wz)in_scroll(wz,0);`. ps2.c:110: `in_rel(dx,-dy)` keeps the current sign, because in_rel does dy-= and this site passes raw +dy, so negate. Check signs with tools/tests/usb.
2. mouse_evt=1 after in_btn_raw outside the lock (hid_keyboard_mouse.c:73,85,109; ps2.c:111) can race with the consumer's mouse_evt=0 at ui/input.c:288. That can leave a pending event unseen until the next input. It is harmless because in_mq_pending() is also checked (:287).
3. Hardening, optional: in_lock has no owner or timeout. To make a future re-entry visible, store an owner (g_in_owner=cpu_id()+1). In in_lock, if g_in_owner==cpu_id()+1, crumb("in_lock re-entry") and return without taking the lock (or panic). If the spin passes about 0x4000000 pauses, call crash_panic(0,"in_lock stuck",...) so a red box is drawn even on an AP.

## Recommendation to lead
Drop in_lock as a suspect for the taskbar/Start freeze. Look for a nested or modal loop that beats g_ml_beat (busy_pump :132, desk_nested_step :120) and never exits. Fix #1 above in the same release, because it can eat clicks and motion on SMP.
