# v11.05 — shared brief (lead: Claude). 4 agents, ~15 min each.

OWNER REPORT (real hardware, v11.04), Biltema B-1050W wireless mouse, receiver 248A:16BB (port 5):
- scroll wheel NEVER works; left click works only SOMETIMES; pointer moves fine.
- `usbhid` shows the mouse interface (ep 0x82, dci 5, "report protocol (IDs)") with layout
  "id 1 mouse btn 0+5 X 8+8 Y 16+8 wheel 24+8" and "id 3 consumer cc 0+32", but the received packets are
  3-byte BOOT-format reports: `rx [3] 00 FD 01 -> dx -3 dy +1`, `rx [3] 00 00 00`. So the receiver ignores
  SET_PROTOCOL(report) (controller_init.c ~372) and keeps sending boot reports [buttons, dx, dy] with NO wheel byte.
- Left click without motion = `01 00 00`; first byte 0x01 == declared mouse report ID 1, so hid_report_decode
  (hid_keyboard_mouse.c ~61, `boot=hid?(...)`) treats it as a report-ID-1 packet that is too short -> click lost.
  Clicks while moving work because of the (b[1]||b[2]) clause. Right click `02 00 00` can collide similarly.
Report descriptor of that interface (from the photo; row 000 not visible, assume the standard
`05 01 09 02 A1 01 85 01 09 01 A1 00 05 09 19 01`):
 010: 29 05 15 00 25 01 95 05 75 01 81 02 95 01 75 03
 020: 81 01 05 01 09 30 09 31 15 81 25 7F 75 08 95 02
 030: 81 06 09 38 15 81 25 7F 75 08 95 01 81 06 C0 C0
 040: 05 0C 09 01 A1 01 85 03 75 10 95 02 15 01 26 8C
 050: 02 19 01 2A 8C 02 81 00 C0 05 01 09 80 A1 01 85
 060: 04 75 02 95 01 15 01 25 03 09 82 09 81 09 83 81
 070: 60 75 06 81 03 C0 05 01 09 00 A1 01 85 05 06 00
 080: FF 09 01 15 81 25 7F 75 08 95 07 B1 02 C0
 (input IDs: 1 mouse = 5 bytes incl. ID, 3 consumer = 5 bytes, 4 system = 2 bytes; 5 = feature only)
Also: the v11.04 red box worked: "CPU 6 waits for the kernel lock held by CPU 0, last step: input and window
actions (stage 7)" — the system froze inside process_input (core/init/main.c ~317-318), i.e. a click/key handler.

Host tests: tools/tests/usb/hidmouse_* (see tools/check.py g_usb); `python3 tools/check.py --only usb` is ALLOWED.
RULES: /home/claude/reports/BUILDERS.md. Never run build.sh or the full check.py. Compile-check with
/home/claude/ccheck.sh. Never `git add` build or tools/out. Report: /home/claude/reports/v1105/<name>-report.md.
Final message: max 4 lines.
