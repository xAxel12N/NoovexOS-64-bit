# NoovexOS 64 — handoff v11.03

Released 2026-11-14. ISO sha256 `bbcf426e7cc43587b2e0b0fc6a5bb3399608ed972284a3b9b32389a88141ed40`.
Checks: 71 passed, 0 failed (build, static, analyze, usb, nvidia, drivers, qemu incl. SMP, linux). Not tested on real hardware. The drag-end freeze was not reproduced in QEMU.

## Problem
v11.02 on real hardware: the Start menu, opening apps and dragging windows all work, but when a drag ends (mouse button released) the whole OS freezes, keyboard included.

## Team: 28 agents in about 50 minutes
- **6 hunters (H1–H6):**
  - App start deferred to drag end: refuted. Live apps keep drawing during a drag.
  - Snap/resize reallocation: refuted.
  - Content cache path: no unbounded loop.
  - Release input path: always returns. Found a report-ID-as-buttons bug.
  - USB: no infinite loop. Found dead-endpoint paths.
  - Locks/SMP: no deadlock found. Found that the watchdog is blind while the BKL is stuck.
- **4 analyzers (A1–A4):** release call trace; render pipeline (gfx.c:243 is the key flip at drag end); every change since v11.00; QEMU repro of 7 drag cases, with no freeze.
- **6 searchers (S1–S6):**
  - graphics: no hang by itself.
  - apps: device I/O in draw and unclipped SCENE writes.
  - USB: all waits bounded.
  - other drivers: NVIDIA idle re-check and auto-start.
  - core: watchdog can't fire under a BKL hang.
  - shell/sys/net/web: waitkey/sleep/Wi-Fi auto-join.
- **4 diagnosers:**
  - V1 ranked the NVIDIA idle re-check first (30%).
  - V2 wrote the owner test plan.
  - V3 found the NVIDIA path unassigned.
  - V4 (the timing is a coincidence) put NVIDIA auto-start at 45%.
- **4 fixers:**
  - F1 render: only the dragged window is cached; draw timing; clipping.
  - F2 apps: no device I/O in draw.
  - F3 USB: recovery budget, hold, Stopped state, boot fallback, reentrancy.
  - F4 watchdog before the BKL, plus a red box.
- **4 logic reviewers:** no HIGH in F1, F2 or F3. L4 found a HIGH in the lead's NVIDIA change (the opt-in setting was never read), fixed. Its MEDIUM about false red boxes is also fixed.
- **Lead:** NVIDIA idle re-check disabled; NVIDIA auto-start opt-in (`nvgsp auto on/off`); the red box respects the hold flags; nvsim test updated.

## Owner test (short version of team-reports/v1103/v2-testplan.md)
1. Open Files and drag it to the middle. Let go and wait 10 s. Then drag it to the top edge so it snaps big. Repeat with Settings, Terminal and Task Manager.
2. If it freezes: wait 60 s for a red box and photograph it. Note whether the pointer moves, the clock ticks and Caps Lock lights.
3. After a restart: run `crash`, `lag` and `usbhid`, and photograph each.
4. To get NVIDIA 2D acceleration back: `nvgsp all` (this boot), or `nvgsp auto on` (every boot). If the freeze comes back only with this on, the NVIDIA path is the cause.

## Not fixed / next
- Reviewer MEDIUMs:
  - Files dimming loops call scene_ptr per pixel (L1 M1).
  - Nested draw markers (L1 M2).
  - USB stick ready later doesn't rescan (L2 M1).
  - The one-frame settle after a drag is not set (L2 M2).
  - Right-click on receivers that ignore SET_PROTOCOL (L3 M1).
  - No retry flag after a 10 s hold (L3 M2).
  - freeze_tick's recursive bkl_try (L4 M1).
- An NMI watchdog: a hang on CPU 0 with interrupts off and no other CPU waiting is still invisible.
- Everything still listed under v11.02's "Not fixed": WOFF2/AVIF overflows, syscall user pointers, camera code 17, the editor truncating long lines, XFAIL-halt.
