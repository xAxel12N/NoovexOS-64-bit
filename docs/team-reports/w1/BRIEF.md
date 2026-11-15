# NoovexOS 64 — 15-hour run (20:00 Sat → ~11:00 Sun), lead: Claude. Wave 1 brief.

Repo: /home/claude/src/NoovexOS-64-bit-main (v11.06, tag v11.06). From-scratch x86-64 OS "for easy daily use".
History of this week: docs/handoff-v11.0*.md and docs/team-reports/ (v1102…v1106).

## OWNER REPORT (real hardware, v11.06)
- Clicking EMPTY DESKTOP now works (v11.06 fixed the input-queue race).
- Clicking the TASKBAR or the START MENU (Start button / menu items) FREEZES the OS with NO red box at all.
  v11.06 has two red boxes: the normal freeze box (core/crash.c freeze_tick, fires when the main-loop heartbeat
  g_ml_beat stops) and a new NMI box (fires when CPU 0's timer ticks stop for 3 s, needs SMP). Neither appeared,
  so most likely: ticks keep running AND the heartbeat keeps beating → a NESTED/MODAL loop inside the click handler
  (busy_pump / desk_nested_step / ui_pump or a `while(button held)` / wait loop) that keeps feeding g_ml_beat but
  never returns; or input dead while the loop runs (nested steps don't poll USB keyboard/mouse); or the screen is no
  longer presented. Start menu and taskbar worked in v11.02; changes since: v11.03 (gfx.c drag cache, app_io_tick,
  USB recovery), v11.04, v11.05 (boot-packet decode, INM input markers, SET_PROTOCOL retry), v11.06 (input-queue
  spinlock: drivers/input/input.c in_lock/in_unlock now a real non-recursive spinlock + in_lock_x/in_unlock_x used in
  graphics/ui/input.c ~288 and graphics/ui/desktop/present_and_pump.c ~127/135 — a re-entry while held would DEADLOCK
  with the heartbeat possibly still alive; NMI watchdog; breadcrumbs `crumb()`; desk g_marq changes).
- Owner's PC: i5-11400F 12 threads (SMP on at boot), GTX 1650 (NVIDIA auto-start is opt-in), RTL8821CE Wi-Fi (works),
  BT 0BDA:C123, 2 xHCI, wireless combo receivers 062A:4101 + Biltema 248A:16BB (sends 3-byte boot packets), USB camera
  0C45:1915 (code 17), USB mic 0BDA:0005, USB stick, USB 2.0 hub.

## LICENSE RULE (hard)
Only code we can ship in a GPLv3 OS: GPLv3, GPLv2-or-later, LGPL-v2.1+/v3, BSD-2/3, MIT, ISC, Apache-2.0, zlib, public
domain, or DUAL licensed with one of those (e.g. Linux "Dual BSD/GPL" files may be used under BSD). NOT allowed:
GPLv2-only (most of Linux), CDDL, proprietary, "non-commercial". Firmware blobs: only with a redistributable license
(linux-firmware WHENCE entries) — note the license per blob.
Before proposing anything, check what WE ALREADY HAVE: docs/ARCHITECTURE.md, docs/PORTING-WORKLIST.md, drivers/, apps/.
The owner gets annoyed by proposals for features that already exist.

## RULES
/home/claude/reports/BUILDERS.md. Never run build.sh or the full tools/check.py (single fast groups like
`python3 tools/check.py --only usb` are OK for coders). Compile-check: /home/claude/ccheck.sh <file>.
Never `git add` build or tools/out. Reports: /home/claude/reports/w1/<name>-report.md.
FINAL MESSAGE: max 3 lines (the lead reads ~50 of them). QEMU (only agents told to): /tmp/repro/run.sh + /tmp/reprotools,
v11.06 ISO /home/claude/v1106.iso (copy it), boot-shell `usbsim biltema` then `desktop`.
