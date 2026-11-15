# Tracker report: wireless USB mouse moves only left/right

## What I checked
I ran the real hid_parse and hid_report_decode (taken from the worktree source) against these report descriptors: a boot mouse, the Logitech Unifying receiver's interface 1 (report ID 2, 12-bit X/Y, wheel, AC Pan, plus consumer ID 3, system ID 4 and vendor ID 8), Lightspeed 16-bit (report ID 2 with a 5-byte vendor tail, taken from the mse_high_res_ls_1_3 layout in Linux hid-logitech-dj), a 16-bit gaming mouse with no report ID, and a combo receiver (keyboard ID 1 + mouse ID 2 + consumer ID 3 + system ID 4).
- Parsing is correct for all of them. Y (0x31) gets its own field, offsets are correct when there is a report-ID byte, 12-bit and 16-bit fields decode with the right sign, and the mouse in the 2nd collection of a multi-collection interface works. So the bit offsets, 12/16-bit decoding, report IDs and multiple collections are NOT the cause.

## Cause (deduced from the code, not reproduced on hardware)
1. **Interrupt transfer length = wMaxPacketSize** (controller_init.c, report mode: `rlen=mps`). If a receiver's input report is longer than its max packet size, the xHC ends the transfer after the first packet. hid_bits then gets a report cut short. Y sits after X, so it falls past the end and reads as 0 while X still decodes: that is exactly "left/right only". Linux usbhid uses the longest input report as the transfer length.
2. **Device stays in boot protocol after SET_PROTOCOL(report)**. Some cheap dongles do this. Boot bytes [btn][dx][dy][wheel] decoded with a 16-bit X/Y layout give X = dx + 256*dy and Y = wheel/0, so the pointer only moves along a horizontal line.

## Fix (drivers/usb/xhci/controller_init.c only; keyboard code untouched)
- hid_parse now also records `hid_inlen`, the longest input report in bytes (report-ID byte included). Report-mode transfer length = max(wMaxPacketSize, hid_inlen), capped at the 64-byte buffer slot. Because it is never smaller than the max packet size, it cannot cause babble errors.
- After SET_PROTOCOL(1) on a boot-subclass mouse, the driver sends GET_PROTOCOL. If the device answers 0 (boot), that interface is decoded with the boot layout.
- Compile check: `ccheck.sh drivers/usb/xhci.c` gives 48 warnings before and after (all pre-existing, none new).

## Tests
- New: tools/tests/usb/hidmouse_gen.py + hidmouse_main.c. The generator cuts hid_parse, hid_bits, cc_hk and hid_report_decode out of the real sources and stubs kbd_report. It asserts positive and negative X/Y deltas (including ±2047 for 12-bit and ±32767 for 16-bit), that consumer report ID 3 does not move the pointer, the longest-report length for each descriptor, and the cut-report case (Y lost when cut to a 4-byte packet, kept with the new length). Result: 26/26 pass, built with ASan/UBSan.
  Run it by hand: `python3 tools/tests/usb/hidmouse_gen.py drivers/usb/xhci/controller_init.c drivers/usb/xhci/hid_keyboard_mouse.c include/nvx/drivers/usb/xhci.h /tmp/hm.c tools/tests/usb/hidmouse_main.c && gcc -fsanitize=address,undefined -o /tmp/hm /tmp/hm.c && /tmp/hm`
- Hooked into tools/check.py g_usb (2 new records). I did not run check.py (builder rules). In the existing usb_template.txt test, the xh_ctrl mock does not fill the GET_PROTOCOL buffer, so pr stays 0xFF and those tests keep report mode. `static int hid_inlen;` sits inside the `ty` slice that g_usb already extracts, so its build should still work. The lead should confirm both by running check.py.

## Untested / open
- Not tested on real hardware, and the exact descriptor of the owner's dongle is unknown. If the bug remains, get `usbinfo` raw report bytes (xhid_last) plus the report descriptor from the owner.
- The Unifying descriptor bytes match the published interface-1 layout (the mouse part is identical to hid-logitech-dj's mse_descriptor). The vendor tail is from memory of public dumps.
- Unchanged edge case: if a report-ID interface also sends a shorter report whose length is an exact multiple of the max packet size, with no zero-length packet after it, it would merge with the next report. Linux behaves the same way.
