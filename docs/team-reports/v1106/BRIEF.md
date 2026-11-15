# v11.06 — shared brief (lead: Claude). 10 agents. Target release ~19:15 (check `date`).

OWNER REPORT (real hardware, v11.05): the FIRST LEFT CLICK on EMPTY DESKTOP froze the whole OS: no keyboard, nothing,
and NO red box appeared (the v11.04/v11.05 red box needs the timer IRQ / another CPU spinning on the BKL — so this
freeze likely has interrupts disabled on CPU 0, or all CPUs stuck). Right click is a different path (context menu).
Mouse: Biltema receiver 248A:16BB. It ignores SET_PROTOCOL(report) and sends 3-byte BOOT packets [btn,dx,dy]:
left press without motion = `01 00 00`, release = `00 00 00`. Its descriptor declares input report IDs 1 (mouse,
5 bytes), 3 (consumer, 5 bytes), 4 (system, 2 bytes); full descriptor in /home/claude/reports/v1105/BRIEF.md.
v11.05 changes (suspects; see `git log v11.04..v11.05`, docs/team-reports/v1105/):
- M1 decode (hid_keyboard_mouse.c hid_report_decode ~57-75): boot packets now decoded -> a still left click now
  REACHES on_click for the first time (before it was lost). Left press on empty desktop -> graphics/ui/input.c ~161
  `g_marq=1` (selection rectangle) -> desk_win_exposed()/desk_popups_clear() return 0 while g_marq.
- M2 (controller_init.c ~106-140 xhid_note_boot/xhid_note_fmt/xhid_proto_poll): the first boot packet schedules
  SET_PROTOCOL(report) re-sends from the poll path (xh_ctrl, up to 3 tries) + SET_IDLE(0) to all interfaces.
- M3 (graphics/ui/input.c INM markers, core/crash.c in_mark, crash_tick box save/erase), M4 (deferred Settings work).
v11.02-v11.04 relevant: xh_event_w waits (tick + iteration bounded; may run with IRQs off), heap kmalloc/kfree
IRQ-off sections (mem/heap.c), BKL (arch/x86_64/cpu/percpu.c), freeze watchdog (core/crash.c, interrupts.c).
QEMU tools from earlier rounds: /tmp/repro/run.sh (2x qemu-xhci, usb-kbd, usb-mouse, usb-tablet, hub, usb-audio,
usb-storage; monitor socket /tmp/repro/mon), /tmp/reprotools/{hmp.py,goto.py,mv.py,shot.py}. ONLY agent "Sim"
runs QEMU and builds an ISO.
RULES: /home/claude/reports/BUILDERS.md. Never run build.sh / full check.py (except Sim, see its prompt).
`python3 tools/check.py --only usb` is allowed. Compile-check with /home/claude/ccheck.sh. Never `git add` build
or tools/out. Report: /home/claude/reports/v1106/<name>-report.md. Final message max 4 lines.
