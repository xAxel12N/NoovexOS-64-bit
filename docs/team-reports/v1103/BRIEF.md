# v11.03 — shared brief (lead: Claude). Deadline: release by ~15:38. v11.01 release commit = 6d88d5df.

OWNER REPORT (real hardware, v11.02): "works to click the start menu and open app and drag it, but when I
didn't drag the window anymore it just froze the entire OS". So: Start menu OK, app opens OK, dragging a window
OK, the freeze happens WHEN THE DRAG ENDS (mouse button released). Unknown: which app, whether the window was
snapped to a screen edge, whether pointer/clock still move. Earlier: v11.01 froze on clicking Camera/Voice Recorder;
v11.02 fixed that path (see docs/team-reports/v1102/ and docs/handoff-v11.02.md).
Machine: 2 xHCI, wireless combo receivers 062A:4101 + 248A:16BB (keyboard+mouse), USB camera 0C45:1915 (code 17),
USB mic 0BDA:0005, BT 0BDA:C123, USB stick, USB 2.0 hub, RTL8821CE Wi-Fi, SMP CPU.

Key code: graphics/ui/input.c (~150-205: mouse step, drag start/move/release at ~197 `drag=-1`, apply_snap),
graphics/ui/wm.c (apply_snap ~62, open_win), graphics/ui/desktop/present_and_pump.c (desk_win_exposed: window
content cache is OFF while drag>=0 and turns ON at release; desk_nested_step, ui_pump, busy_pump),
graphics/ui/desktop/desktop_clicks.c, apps/media/camera.c + recorder.c (start once per open, first start happens
on a draw), apps/system/logs.c ~96 (per-frame app draw dispatcher), drivers/usb/xhci/* (v11.02: HID events handled
during waits, xh_wdepth, xhid_recover + watchdog every 256 polls, per-slot back-off), mem/heap.c (v11.02: kmalloc/
kfree IRQ-off), sys/native/syscall.c sleep (v11.02 bkl_drop), core/init/main.c main loop.
v11.02 tag = `git tag v11.02`; compare with `git diff v11.01-ish..v11.02` (find v11.01 release commit via git log).

RULES: /home/claude/reports/BUILDERS.md. Never run build.sh or tools/check.py. Never `git add` build or tools/out.
Read-only unless your role says you have a worktree. Report to /home/claude/reports/v1103/<name>-report.md:
ranked findings with file:line, why it matches "freeze when the drag ends", confidence %, suggested exact fix.
FINAL MESSAGE: at most 3 lines (the lead reads 28 of them). QEMU ISO (v11.02): /home/claude/v1102.iso.
