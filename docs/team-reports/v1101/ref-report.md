# ref report (v11.01)

## Real data found
- 062A:4101 MOSART "2.4G Keyboard Mouse" lsusb -v (https://raw.githubusercontent.com/h3rl/jetsonfun/HEAD/hids/mokbhid.txt, also linux-hardware.org probes 475a1011ab, ddf6e5a3f7):
  if0 boot kbd, rdesc 65 B, EP81 mps 8; if1 boot mouse, rdesc 168 B, EP82 **mps 7**, bInterval 4.
- 062A:4101 report-protocol layout MEASURED on real hardware (https://raw.githubusercontent.com/family-mruby/fmruby-core/HEAD/doc/mouse_wheel/report/w3.md):
  7-byte report, ID 0x03; payload bits 0-7 buttons, **X bits 8-19 (12-bit)**, **Y bits 20-31 (12-bit)**, wheel bits 32-39, 1 extra byte. Captures: right1 = 03 00 01 00 00 00 00, down1 = 03 00 00 10 00 00 00.
  Same source: boot protocol is fixed 3 bytes; SET_PROTOCOL(Report) STATUS stage timed out but the device did switch (3 -> 7 bytes).
- Raw descriptor bytes: NOT found publicly for any of 062A:4101, 248A:16BB, 248A:8367/8366, 25A7:FA61, 1D57:FA60, 3938:1031, 062A:5918 (every probe log says UNAVAILABLE). Nothing found for Biltema B-1050W / 24-593 specifically. linux-hardware.org rate-limited (429) part way.

## File
tools/tests/usb/real_descriptors.h (commit on h-ref): measured constants + captures, a RECONSTRUCTED mouse collection reproducing the measured layout (clearly marked), sample reports dx=+5/dy=-3 and dx=-200/dy=+150, a 4-byte truncated report, boot 3-byte report.

## Verified with the driver's own hid_parse/hid_report_decode (hidmouse_gen.py harness, no driver edits)
- recon descriptor -> 1 layout, hid_inlen 7, x_off 8/12, y_off 20/12 (payload bits).
- full 7-byte report: dx=5 dy=-3 OK; dx=-200 dy=150 OK.
- **same report cut to 4 bytes (ID+3): dx=5 dy=0** -> exact symptom.

## Which layouts lose Y but keep X in our driver
hid_parse drops a mouse layout unless both x_off and y_off >= 0, so a missing Y usage kills X too. X-only survives only if Y is located but reads 0:
1. Y extends past `pl*8` in hid_bits (controller_init.c:148 `off+sz>len*8 -> 0`). For 12-bit packed X/Y (MosArt, Unifying style) Y ends at payload bit 32 while X ends at bit 20: any `got` of 3..4 bytes (with ID) keeps X and zeroes Y. For 16-bit X/Y (bits 8-23 / 24-39) got = 4..5 bytes does the same. Strongest candidate: `got = xhid_rlen - res` (hid_keyboard_mouse.c:80) coming out short (wrong residual / rlen), or a receiver with a short mps if inl was computed short.
2. A later Input field in the same report ID and mouse collection re-using usage 0x31 overwrites y_off (controller_init.c ~131) with a constant/padding field.
3. Device actually sending 12-bit packed data while the descriptor-derived layout is 8-bit (or vice versa): reading 8-bit at bit 16 picks up X's high nibble + Y low nibble -> Y garbage, not clean zero; matches fmruby's "X dead, Y jumpy" inverse symptom, so less likely here.
Confidence that MosArt 062A:4101 uses 12-bit packed X/Y at bits 8-19/20-31 with ID 3: 85%. That the bug is a short decode length: 50% (needs `usbinfo` raw last-report bytes + got from the machine to confirm: if xhid_lastn shows 4 bytes, it is #1).
