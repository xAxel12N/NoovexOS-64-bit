# v11.02 — shared brief (lead: Claude)

OWNER REPORT (real hardware, v11.01): the Biltema wireless mouse now moves in all directions (fixed).
NEW BUG: clicking an item in the Start menu FREEZES the system; the keyboard froze too ("maybe other drivers").
In the photo the pointer is on Start > Drivers & Hardware > Camera (items: Gears, Camera, Voice Recorder, Benchmark,
Task Manager, Settings). Not known yet: which items freeze, whether the pointer still moves, whether the clock ticks,
whether v11.00 froze. The machine has: xHCI (2 controllers), 2 wireless combo receivers (062A:4101, 248A:16BB),
USB camera 0C45:1915 (Camera app shows "USB controller refused the stream (code 17)"), USB mic UACDemoV1.0 0BDA:0005,
Bluetooth 0BDA:C123, USB stick, a USB 2.0 hub, RTL8821CE Wi-Fi.

v11.01 changed (suspects), see `git log --oneline -12` and docs/team-reports/v1101/:
- xh_ctrl (drivers/usb/xhci/controller_init.c ~193): on timeout now issues Stop Endpoint + Set TR Dequeue on EP0;
  cc 3/4/6 now Reset Endpoint. Each xh_cmd_s waits for a command completion.
- hid_xfer (drivers/usb/xhci/hid_keyboard_mouse.c ~74): stricter TRB/buffer lookup, residual masking, ISP flag.
- GET_PROTOCOL no longer switches mice to boot decoding; boot-format detection in hid_report_decode.
- per-keyboard modifiers (usbk_mods_sync), new `usbhid` diag capture in hid_xfer.
Prime theory to check first: while the driver waits for a command or control completion it polls the event ring and
may DROP or mishandle transfer events of the mouse/keyboard, so their endpoints are never re-armed -> input dead.
Second: a blocking USB call (camera/mic open, hub poll) in the UI path that now waits much longer or forever.

RULES: /home/claude/reports/BUILDERS.md. Never run build.sh or tools/check.py (unless your role says so).
Compile-check with /home/claude/ccheck.sh <file>. Never `git add` anything named build or tools/out.
GPLv3-compatible code only. Small, clear diffs, inside your slice. Write your report to
/home/claude/reports/v1102/<name>-report.md (findings with file:line, severity, confidence, what you changed, tests).
Final message: 5-line summary. A v11.01 ISO for QEMU is at /home/claude/v1101.iso.
