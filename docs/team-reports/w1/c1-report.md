# c1 report: USB part of the Start/taskbar freeze (branch x-c1, commit 9d74f503)

## Changes
1. Late SET_PROTOCOL retry is now OFF by default. drivers/usb/xhci/controller_init.c:110
   - The t-report/a2 patch (`g_usbproto_off=1`) was WRONG as written. g_usbproto_off is also read at
     enumeration time (:435 SET_IDLE for report-protocol interfaces, :437 enumeration SET_PROTOCOL retry).
     Setting it to 1 would have changed enumeration.
   - Instead there is a new flag `g_usbproto_late_off=1`, used only by xhid_note_boot (:118) and xhid_proto_poll (:128).
     g_usbproto_off stays 0, so enumeration is unchanged.
   - `usbproto on|off` (xhci_usbproto :112) sets both flags, as before. Its return value, and so the shell's
     "usbproto: on/off", now reports the late-retry state. On a fresh boot it prints "off".
2. Still boot press is no longer classed "amb". drivers/usb/xhci/hid_keyboard_mouse.c:63
   - The t-report fix `&&!L0->bseen` BREAKS the existing phantom-click test (combo ID2: `02 00 00 00` after a boot
     packet became a right click). I checked this.
   - Used instead: `amb=declared&&wide&&!b[1]&&!b[2]&&!(L0->bseen&&id==1);`. Once boot packets have been seen, a still
     left press `01 00 00` on a wide mouse-ID-1 receiver is a left press. A still `02 ..` (mouse ID 2 = right) stays
     ambiguous, so the phantom right-click guard still holds.
   - Note: the real Biltema ID1 is not wide (its `01 00 00` test already passed), so this fix matters only for
     wide-ID1 combo receivers (possibly 062A:4101).
   - New test, tools/tests/usb/hidmouse_main.c (end): "wide1" descriptor (ID 1, 16-bit axes), 5 checks. It fails on the
     old decode and passes now.
3. Unlocked mouse delta updates now go through in_rel()/in_scroll():
   - hid_keyboard_mouse.c:72,73,84,85 (report decode, boot and report paths), :108,:110 (non-report path in hid_xfer)
   - drivers/input/ps2/ps2.c:110 `in_rel(dx,-dy)` (keeps the raw-dy convention); :114,:117,:118 scroll; :116 hscroll -> in_scroll(0,h)
   - drivers/input/vm/vmmouse.c:52: the cli/sti RMW is replaced by in_rel(tx-cx,ty-cy)
   - Small behaviour change: PS/2 wheel now also sets mouse_evt (in_scroll does this). Harmless.
   - Test stubs for in_rel/in_scroll added to tools/tests/usb/hidmouse_gen.py, hidsim_gen.py, hidkbd_env.h and
     tools/tests/usb_template.txt. The usb_template was needed: the main USB test failed to link without it.

## Tests
- /home/claude/ccheck.sh on drivers/usb/xhci.c, drivers/input/ps2/ps2.c and drivers/input/vm/vmmouse.c: rc=0 (warnings were already there).
- `python3 tools/check.py --only usb`: 12 of 12 passed. The hidmouse suite now has 73 checks (was 68).

## Skipped
- t-report items 3/4 (poll-guard diagnostic, busy_pump beat): not in my task.
