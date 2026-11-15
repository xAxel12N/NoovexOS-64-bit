# sim report (v11.01 hunt) — branch h-sim

## What
End-to-end host simulation of the real driver code: tools/tests/usb/hidsim_gen.py extracts hid_parse, hid_bits, hid_pick,
cc_hk, hid_report_decode, hid_xfer and the enumeration block (controller_init.c:291-307 + the rlen lines 321-322) verbatim into
sim_enum(). A simulated combo receiver (if0 boot kbd ep 0x81 MPS 8; if1 mouse ep 0x82) answers GET_DESCRIPTOR(report),
SET/GET_PROTOCOL (answers report / answers boot / stalls both / stalls GET), and a USB packet/TD model (packets of MPS,
short packet ends a TD, full TD ends it, overflow = babble cc=3 + halt) feeds hid_xfer with real residuals.
Run: sh tools/tests/usb/hidsim_test.sh (gcc -fsanitize=address,undefined). Prints ALL PASS only when no case loses an axis.

Matrix (6720): X/Y 8/12/16 bit x 5 or 16 buttons x {Usage list, Usage min/max, separate Input items, usages-before-page,
32-bit logical} x {no ID, ID1, ID2, ID2 + kbd ID1 (9B) + consumer ID3} x MPS {4,5,6,7,8,16,64} x if1 class {1/2, 0/0} x 4 protocol answers.

## Result
- **Zero LOST-Y cases.** Every report-protocol(+IDs) configuration — the state the Biltema shows in xhci output — decodes X AND Y
  correctly, including reports longer than MPS (split 8+1, device short-packets the tail, rlen=inl so got=9) and MPS 8 with a 9-byte report.
  => The lead's hid_bits/length theory does NOT reproduce for any modeled descriptor shape. Either the Biltema descriptor has a shape
  not modeled here (need its raw report descriptor: dump via usbinfo/xhid_last), or Y is lost downstream of mouse_dx/mouse_dy.
- Failing classes (all marked XFAIL in output, 0 unexpected):
  1. XFAIL-boot (620): controller_init.c:305 — GET_PROTOCOL returns 0 after SET_PROTOCOL(1) -> mode=0, but the device keeps sending
     report format; hid_keyboard_mouse.c:84 decodes db[1],db[2]. For a 16-bit no-ID mouse this is EXACTLY "horizontal only":
     dx=Xlo correct, dy=(int8)Xhi = 0 (or -1). usbinfo would then print "boot protocol" though, which contradicts the field output.
     Fix idea: on pr==0 send SET_PROTOCOL(0) before trusting boot, or keep report mode when the descriptor parsed (Linux ignores GET_PROTOCOL).
  2. XFAIL-halt (287): controller_init.c:322 rlen = max(MPS, longest report). If the mouse report is an exact multiple of MPS but shorter
     than rlen (e.g. 8B mouse with 9B keyboard ID1 in the same interface, MPS 8; or 4B/8B with MPS 4), the TD stays open, the next report
     overflows -> babble, endpoint halted, mouse dead. Real bug, wrong symptom (total loss, not Y-only).
  3. XFAIL-parse (745): controller_init.c:118 — hid_parse binds the Usage Page at Usage time; descriptors that put Usage(X/Y) before
     Usage Page(Generic Desktop) lose the mouse layout (Linux binds the page at Main-item time). Loses both axes.

## Confidence
Report-mode decode path is not the Y-loss cause for standard shapes: 85%. Root cause of the field bug: not found in this slice.
No driver changes made.
