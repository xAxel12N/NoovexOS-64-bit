# NoovexOS 64 — handoff v11.06

Released 2026-11-14. ISO sha256 `e9826ec3d3b1bc2d3485c39e8c899680785f948b9c50a2b2fd4ea9d517969c32`.
Checks: final build 43/43 (build, static, usb, qemu incl. SMP). The previous full run of the same code before the style fixes also passed analyze, nvidia, drivers and linux. Not tested on real hardware. The freeze was NOT reproduced in QEMU.

## Problem (v11.05)
The first left click on empty desktop froze the whole OS: no keyboard and no red box.

## Most likely cause (lead, from review R3)
- `drivers/input/input.c`: the mouse-button queue (written by USB `in_btn_raw` without any lock, read by the desktop under a bare cli) had no SMP lock. Only button changes use it, not motion.
- On the owner's 12-core machine with SMP on, concurrent updates can push the read index past the write index. The rebase loop then runs about 2^32 times with interrupts off: a freeze with no red box.
- Fix: a real spinlock plus IRQ-save in in_lock/in_unlock, `in_btn_raw` self-locking (`in_btn_locked`), `mq_sane` (r=w if out of order), and `in_mq_take_locked`/`in_lock_x` used by graphics/ui/input.c and present_and_pump.c instead of bare cli/sti.
- Confidence: medium. It fits every symptom (click only, SMP, interrupts off, no box), but it was found by review, not reproduced.

## Team (10 agents)
- **Sim:** `usbsim biltema` debug switch, off by default. In QEMU every click case stayed alive, which rules out the decode, the selection box and the SET_PROTOCOL retries as hangs on their own.
- **Trace:** breadcrumb ring with a COM1 mirror; "Last steps" shown in boxes and reports. The extra crumb sites patch was skipped because of merge conflicts.
- **Nmi:** NMI watchdog over an IPI from an AP after 3 s without ticks; red box with RIP, function and crumbs. `crash test hardlock`, `crash nmiwd on|off`.
- **Proto:** SET_PROTOCOL retry restricted; `usbproto off`.
- **Desk:** selection box cleanup; redraw only on motion.
- **IrqU:** xh_event_w time check; USB network under the poll flag; error recovery.
- **IrqC:** heap growth zeroing with interrupts on; rebase cap.
- **R1/R2/R3:** reviews. The lead applied:
  - the watchdog quiet cases (polled timer, holds, sleep, IF=1, panic/power off);
  - Stop EP before recovering a running endpoint;
  - removal of the 500-tick poll takeover;
  - the input-queue lock.

## Owner test
1. Click empty desktop (left, without moving), then normal use.
2. If it freezes: wait about 10 s. The NMI box should appear even with interrupts off (needs SMP, which is on by default). Photograph it.
3. Run `usbhid` and photograph the format line (boot/report, retries) for the wheel question.

## Not fixed / next
- If the NMI box shows a different function, follow it.
- Terminal commands run synchronously while holding the BKL.
- The extra breadcrumbs (USB/heap) still need to be rebased.
- Everything listed under v11.05 and v11.04 "Not fixed".
