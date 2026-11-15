# v11.01 bug hunt — shared brief (lead: Claude)

BUG on real hardware (v11.00, same as v10.99): Biltema B-1050W 2.4 GHz wireless mouse (Art. 24-593).
The pointer moves ONLY left/right. Up/down does nothing. X decodes, Y is lost (always 0 or filtered).
`xhci` output on the machine:
  port 4  full speed -> keyboard/mouse set up: ep 3 keyboard boot protocol; ep 5 mouse report protocol (IDs)
  port 10 full speed -> keyboard/mouse set up: ep 5 mouse report protocol (IDs); ep 3 keyboard boot protocol
Devices: "2.4G Keyboard Mouse 062A:4101" and "Wireless Receiver 248A:16BB" (one of them is the Biltema dongle).
Also present: Bluetooth Radio 0BDA:C123, USB camera, USB mic, USB stick, hub.

Code: drivers/usb/xhci/controller_init.c (hid_parse ~108, hid_bits ~148, enumeration ~290-330, xhid_rlen),
drivers/usb/xhci/hid_keyboard_mouse.c (hid_report_decode ~53, hid_xfer ~74: got = xhid_rlen - res),
pointer pipeline after mouse_dx/mouse_dy (find it). Shell: `usbinfo` in shell/commands/network_and_apps.c ~212.
Host tests: tools/tests/usb/hidmouse_* and hidkbd_* (see tools/check.py g_usb for exact build commands).

Lead's first lead (verify, don't assume): hid_bits returns 0 when off+sz > len*8, so if the length passed to
hid_report_decode is too short, or Y's offset is wrong/past the end, Y silently becomes 0 while X still fits.

RULES: /home/claude/reports/BUILDERS.md. Never run build.sh or tools/check.py. Compile-check with /home/claude/ccheck.sh.
Never `git add` anything named build or tools/out. GPLv3-compatible only. Stay in your slice; small, clear diffs.
HARD 15-minute limit: commit what you have on your branch and write /home/claude/reports/v1101/<name>-report.md
(root cause with file:line, confidence %, the fix, tests run). Final message: 5-line summary.
