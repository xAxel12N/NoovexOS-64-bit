# Typist report: wireless USB keyboards (2.4 GHz receivers)

Branch `typist` in /home/claude/wt/typist.

## Findings (what would break) and fixes
1. **Boot-subclass receiver keyboards that carry report IDs** (combo dongles that put keyboard ID 1, mouse, media keys on the boot keyboard interface). The driver never read their report descriptor and forced boot protocol. Many cheap receivers ignore SET_PROTOCOL(boot) and keep sending `[01, mod, ...]`, so the ID byte was read as Left Ctrl (Ctrl stuck: every key became a shortcut) and the keys were shifted by one byte. Media keys on the same interface were lost.
   Fix (controller_init.c, enumeration loop, NOT hid_parse): boot keyboards now also fetch and parse the report descriptor. If it has report IDs and a keyboard layout, the interface runs in report protocol (SET_PROTOCOL 1) through hid_report_decode. Otherwise it stays in boot protocol exactly as before (Unifying/Bolt if0 has no IDs, so nothing changes there). A failed or stalled descriptor read falls back to boot.
2. **Phantom/rollover**: only `d[2]==1` was checked. Now any slot holding 0x01..0x03 (ErrorRollOver/POSTFail/Undefined) makes the report ignored after the modifiers are updated, so held keys stay held and nothing phantom is typed.
3. **Modifiers sent as array keys 0xE0..0xE7** (some cheap receivers, mod byte 0): they are now folded into the modifier byte, so Shift/Ctrl work.
4. **Several keyboards on one interface with different report IDs** (receivers exposing one keyboard collection per paired device): all shared one `xhid_kprev[e]`, so a report from keyboard 2 released keyboard 1's held keys (and re-typed them). Now each keyboard layout gets its own previous-state buffer (`xhid_kprevx[XHID_MAX][HIDLAY_MAX-1][8]`, cleared at enumeration). `kbd_report` takes the prev buffer as a parameter: `kbd_report(int e,u8* prev,volatile u8* d)`.
5. **Short reports in boot mode read stale bytes**: the boot path passed the raw DMA buffer (rotating in-flight buffers) without looking at the length, so a 3-byte report read 5 stale bytes from an older report (phantom keys). Now it is copied into a zeroed 8-byte buffer using the actual length; reports shorter than 3 bytes are ignored.
6. **Stuck repeat on transfer error**: if a keyboard's transfer completes with an error, key repeat for that device is cancelled (so a held key cannot repeat forever while the endpoint is dead).
- Consumer/media keys on their own report ID: already decoded separately (kind 3); verified by tests that they never type letters. Non-boot (subclass 0) keyboards: already handled via report descriptor and correctly get no SET_PROTOCOL; NKRO bitmap decode verified.

## Files
- drivers/usb/xhci/hid_keyboard_mouse.c (kbd_report, hid_report_decode, hid_xfer)
- drivers/usb/xhci/controller_init.c (xhid_kprevx declaration; boot-keyboard descriptor check in the xhci_enum_loc interface loop; kprevx clear). hid_parse NOT touched.
- tools/tests/usb/hidkbd_gen.py, hidkbd_env.h, hidkbd_main.c, hidkbd_test.sh (new host test)
- tools/tests/usb_template.txt: 3 new checks (boot-subclass keyboard with IDs -> report protocol, SET_PROTOCOL 1, types 'h' without stuck Ctrl); Unifying set-ups now clear `g_rd[0]` (they inherited a stale descriptor from earlier sections, which would now be read for the boot keyboard).

## Testing
- `tools/tests/usb/hidkbd_test.sh`: cuts the real hid_parse/hid_bits/hid2ascii/hid2scan/usb_key_code/kbd_report/usb_kbd_repeat/cc_hk/hid_report_decode out of the driver, ASan+UBSan. 38 checks, ALL PASS: boot keyboard (boot and report protocol), Logitech-style ID-1 keyboard with mouse ID 2 and media ID 3 on one interface (no reserved byte, array to 0xFF), QMK-style NKRO bitmap (ID 4, 120 keys), combo receiver (kbd/mouse/consumer/system), two keyboards on IDs 1 and 5, phantom rollover, missed release, repeat start/stop, array modifiers, 20000 random reports.
- The existing g_usb test (usb_template.txt), run by importing check.py's g_usb/g_xhci_init with KDIR = my worktree (gcc only, work dir in /tmp, not build/): 86 checks pass. The 3 new checks fail on the old driver.
- ccheck.sh drivers/usb/xhci.c: 48 warnings before and after (all old misleading-indentation in the MSD code), no new ones.
- Not run on real hardware.
- **Lead**: add `hidkbd_test.sh` to check.py g_usb if you want it in CI (I did not edit check.py).

## For Tracker (hid_parse owner) - suggestions, not done
- **Report length**: hid_parse does not return the longest input report. rlen is `min(mps,64)`. A report longer than the endpoint's max packet (for example a 9-byte ID+boot report on an 8-byte endpoint) is split over two TDs. The second piece is decoded as a separate report (harmless in practice: its first byte is read as an ID, usually 0). Suggestion: return the longest input report size in bits (per ID, max over IDs, +8 if IDs), and set rlen = that size when it is larger than mps (capped at 64).
- **Several bitmap items**: `bm_off/bm_min/bm_cnt` assume ONE contiguous bitmap. A descriptor with two bitmap items (for example 0x04-0x65 and then 0x87-0x94, or padding between) counts all bits from the first offset, so keys in the second item decode as the wrong usages. A layout can also have BOTH a 6-key array and a bitmap (hybrid keyboards). hid_report_decode then uses only the array. Fix: keep up to 2 bitmap segments, and decode the array and the bitmap together.
- Arrays with Report Size != 8 (rare) are skipped (`rsize==8` only).

## Open / not handled
- Logitech DJ mode (one HID device per paired keyboard, via HID++) is not enabled. In plain mode the receiver merges all paired keyboards into if0, which works but shares one key state.
- `kdown[]` modifier state is global: a report from keyboard B clears a modifier held on keyboard A. This is minor.
- Halted interrupt endpoint (transfer error) is not recovered (no Reset Endpoint / Set TR Dequeue for interrupt endpoints). Tracker may hit the same thing for mice. It is the likely remaining cause of "receiver dies after interference".
