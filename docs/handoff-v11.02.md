# NoovexOS 64 — handoff v11.02

Released 2026-11-14. ISO sha256 `837556687e08ab5aeb222dd97afb1d2ca3ad0f0b2bdea9bf2792126a68505768`.
Checks: 120 passed, 0 failed (build, static, analyze, usb, drivers, acpi, wifi, qemu incl. SMP, linux). USB host tests: usb 91, hidmouse 50, hidkbd 44, hidparse 79, hidsim, xhci init, uvc 25. Not tested on real hardware. Repro could not make the freeze happen in QEMU, so **the fix is not verified**.

## Problem
In v11.01 the mouse moves in all directions. New problem: clicking a Start-menu item (Drivers & Hardware: Camera, Voice Recorder) froze the system, and the keyboard and mouse stopped working.

## Root cause (most likely)
- During long USB waits, HID events went into a 32-entry queue, and any past 32 were dropped. Each dropped event was a TRB that was never re-armed. After 16 drops a device was dead for good, and its endpoint still looked Running.
- The Camera draw path ran `uvc_start`: up to 32 attempts, all failing with code 17 on the owner's camera. Voice Recorder retried the mic on every frame.
- Halted HID endpoints were never reset.

## 19 agents
- **Wait:** HID events are handled during waits. Waits are bounded by ticks (cmd 100 ms, EP0 150 ms). Completions are matched to the TRB that was issued. EP0 recovery guard. Per-slot back-off (3 EP0 timeouts in 3 s hold the slot's EP0 for 5 s).
- **Ring:** HID halt recovery (Reset EP + Set TR Dequeue) and a watchdog every 256 polls.
- **Launch:** Camera starts automatically once per open. Recorder tries the mic once per open.
- **Cam:** `uvc_start` bounded (6 Configure EP tries, 3 control failures, 4 s). Refused alt settings skipped. Mic back-off.
- **Core:** heap IRQ-safe. Native sleep drops the BKL and caps the duration at 1 h.
- **UI:** found the draw-path start and the blocking click path. Found that `ui_pump` ignores mouse dx/dy (not fixed).
- **Repro:** no freeze in QEMU. The freeze needs real-device timeouts.
- **Waits:** listed every blocking USB wait.
- **Reviewers revwait / revring / revapps / revcore:** two found the nested-wait bug and one found that recovery ignored command results (both fixed). Camera still starts synchronously. Core OK.
- **Manager:** trial merges and tests. Found that counters were not cleared when a slot was reused.
- **Sweep1-4:** whole-OS, 20 minutes each, small coverage. Sweep1 found the root cause on its own.
- **Ship / Scribe:** release.
- **Lead:** recovery deferred while a wait runs (`xh_wdepth`), max 3 per second per endpoint. Re-arm only after both Reset EP and Set TR Dequeue succeed. Recovery counters cleared on reuse.

## What the owner should test
1. Open Camera, Voice Recorder and Task Manager from Start > Drivers & Hardware. Does anything freeze, and for how long? Do the clock and the pointer still move?
2. If input dies: wait 3-4 min, then run `usbhid` and `usbhid live` and photograph both.
3. Unplug and replug the receiver while the system is running.
4. Camera is expected to show code 17, but it should not freeze the system.

## Not fixed / next (ranked)
1. WOFF2 table-length overflow (web/text/woff2.c), causing a kernel heap overflow from any web font. HIGH (Sweep2).
2. AVIF iloc extent-length overflow (web/image/imgx.c `item_data`), causing a heap overflow from `<img>`. HIGH (Sweep2).
3. Native syscalls use user pointers without checks (sys/native/syscall.c). They can write kernel memory or panic (Sweep4 M1).
4. Camera code 17 (Configure Endpoint) still unexplained, so the camera won't stream. `uvc_start` still runs synchronously in draw/click handlers and can stall the UI for up to about 4 s (revapps M1, Sweep3).
5. HID stall recovery does not send CLEAR_FEATURE(ENDPOINT_HALT), so a device-side stall repeats (revring M1).
6. Preempt/IPC/compositor demo leaks a BKL level; with SMP on, the APs spin forever (Sweep4 M2). Also `bkl_take` overwrites the depth (revcore F4).
7. Editor truncates lines over 199 characters and rows past 199, then saves the cut file (Sweep3 M2).
8. XFAIL-halt: 280 hidsim cases (report an exact multiple of MPS but shorter than rlen).
9. Brotli leaks about 830 KB per decode. AVIF/JXL allow 1 GiB images. document.write and DOM recursion have no depth limit (Sweep2 M1-M4).
10. Back-off hold never expires if g_ticks stops (revwait M2). Lost key-release reports leave auto-repeat stuck (Sweep1 M5). `ui_pump` does not consume mouse dx/dy (UI 3).
11. Not reviewed at all: sys/linux (about 5,000 lines of Linux-ABI syscalls), fs/, net/, boot/, most of core/.
