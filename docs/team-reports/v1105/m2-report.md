# m2 report (v11.05, branch v5-m2)
## Findings (enumeration order vs Linux usbhid / HID spec)
- Linux usbhid never sends SET_PROTOCOL; it sends SET_IDLE(0) to EVERY interface (before reading the report descriptor) and sets HID_QUIRK_NOGET for boot kbd/mouse (no GET requests). We sent SET_IDLE only to keyboards and issued GET_PROTOCOL to the mouse (result discarded) - a GET that boot mice often never answer.
- v11.02 EP0 back-off: 3 timed-out control transfers within 300 ticks put the slot on a 500-tick hold during which xh_ctrl returns 0 WITHOUT sending. Enumeration does many requests (strings, 2 report descriptors) before SET_PROTOCOL, so a slow receiver can silently lose SET_PROTOCOL(report). Likely contributor to the B-1050W staying in boot format.
## Changes
- controller_init.c enumeration: per interface SET_PROTOCOL (boot subclass) then SET_IDLE(0) for all interfaces; GET_PROTOCOL dropped. Any SET_PROTOCOL that failed/was skipped is re-sent once after clearing the slot's back-off (xhid_bo_reset). Result kept in xhdiag_t.pset (1 ok, 3 ok on 2nd try, 2/4 no answer).
- Per device: EP0 enqueue/cycle saved after enumeration (xhid_e0enq/e0cyc[owner region]) so EP0 can be used later from the poll path (same save/restore pattern as hubs). Devices with audio on the same EP0 are skipped.
- `void xhid_note_boot(int e)` (exported in xhci.h): marks boot format, schedules SET_PROTOCOL(report) retry (max 3, >=100 ticks apart).
- Automatic detection: xhid_note_fmt() is called from xhid_diag_rx() (already called by hid_xfer for every packet), so hid_xfer/hid_report_decode are untouched. Rule: mouse layout needs N bytes (B-1050W: 5 incl. ID); packet with a declared non-mouse ID -> ignored; len>=N -> report format; len 3/4 -> boot. 00 FD 01 and 01 00 00 -> boot; 03.., 04 xx -> ignored.
- xhid_proto_poll() runs from xhci_mouse_poll_in after xhci_poll_one (one-line change in hid_keyboard_mouse.c poll loop, NOT in hid_xfer/decode); returns if xh_wdepth>0. Logs "usb: asked receiver for full reports (try k)" (", no answer)" on failure).
- xhdiag_t += pset, pfmt, ptry, nboot. usbhid prints one extra line for report-mode mouse interfaces: "receiver sends report format (wheel ok)|BOOT format (no wheel)  SET_PROTOCOL ok|...  boot packets n  retries k".
## Why the call is in xhid_diag_rx, not a new line in hid_xfer
The test generators (hidmouse_gen.py, hidsim_gen.py) extract hid_xfer and stub xhid_diag_rx; a new call in hid_xfer broke them and editing tools/tests was outside my scope. Calling from xhid_diag_rx is equivalent (same place in hid_xfer, after decode).
## Tests
ccheck: xhci.c and shell/commands.c - no new warnings (46/39 before and after). `python3 tools/check.py --only usb`: 12/12 pass.
## Untested / open
- No host test for xhid_note_fmt/xhid_proto_poll (would need generator changes). Real hardware will show whether the receiver honours a late SET_PROTOCOL; if it keeps sending boot after 3 tries, M1's boot decode fallback is the remaining fix (and the wheel is unobtainable then).
- SET_IDLE now goes to mice too (Linux does the same); a mouse that STALLs it is handled by the existing stall recovery.
