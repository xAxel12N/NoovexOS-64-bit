# length (hunter) - v11.01 interrupt-path lengths

Branch h-length, commit on top of d7d67a72.

## Verdict
No length bug found that explains "X moves, Y never does". Every length on the path checks out:
- rlen (controller_init.c:321-322): report mode = max(wMaxPacketSize, hid_inlen), capped 64. Right; same as Linux (buffer = longest input report).
- hid_inlen (controller_init.c:146): only kept layouts, per-report-ID bitpos, +1 for the ID byte. Right (overestimates harmlessly if a non-ID report exists).
- MPS (controller_init.c:155): (wMaxPacketSize & 0x7FF), interrupt-IN only. Right.
- Endpoint context (controller_init.c:319): MPS, Avg TRB len = MPS, Max ESIT = MPS (FS/LS, burst 0). Right per spec; TD > MPS just spans two intervals.
- res masked to 24 bits at both event readers (hid_keyboard_mouse.c:96 and xh_event_t controller_init.c:172); got = rlen - res is right for single-TRB TDs.
- Buffer from event TRB pointer: on real hardware ptr is always the completed TRB, buffers rotate 1:1 with arms, never reused while in flight. No stale-slot case.
- hid_report_decode pl=len-1 for ID; hid_bits bound correct.
So for Y to drop via length, got must be < (Y end)/8+1 while X fits; that cannot happen with rlen >= hid_inlen unless the descriptor-derived Y offset itself is wrong (parser/layout slice, not lengths). `usbinfo` "last:" bytes (got, up to 16) will show the real report length on the machine - ask for it.
Confidence no length bug causes it: ~75%.

## Hardening committed
- controller_init.c:215 xhid_arm: Normal TRB now sets ISP (bit 2) and masks length to 17 bits. With ISP clear some xHCs report short packets without a correct residual; Linux sets ISP on all IN TRBs.
- hid_keyboard_mouse.c:78-80 hid_xfer: res masked here too; residual > rlen (bogus) -> got=0 instead of relying on a signed cast; TRB-pointer lookup now excludes the Link TRB slot and needs 16-byte TRB / 64-byte buffer alignment.
- Tried a FIFO fallback buffer for events without a TRB pointer, reverted: tools/tests/usb_template.txt sends ptr=0 events with data in buffer 0, and real hardware never sends ptr=0.

## hid_bits partial fields - decision: keep returning 0
A partly present X/Y would be sign-extended from the wrong bit -> large wrong-sign jumps. 0 is the safe value; the real fix for a short report is the right transfer length (done). Not changed.

## Tests
- tools/tests/usb/hidmouse_gen.py now also extracts xhid_arm + hid_xfer; hidmouse_main.c adds a 32-bit-mmap simulated ring: TRB length/ISP/IOC, 600 transfers through ring wrap with X+Y each time, short packet residual 2/9, stall completions, bogus residual, 16 still armed. 34/34 pass (ASan+UBSan). Against the old v11.00 code only the ISP check fails (1/34); the rest pass, which confirms the old residual/buffer path was already decoding X and Y correctly.
- hidkbd_test.sh 38/38 pass. ccheck xhci.c: rc 0, warnings 48 = 48 before.
- check.py g_usb not run (rules); usb_template behaviour unchanged (ptr=0, res=0 path same as before).
