# proto report (v11.06, branch v6-proto)
## Audit of M2 SET_PROTOCOL retry path (v11.05)
- Recursion: none. xh_ctrl inside xhid_proto_poll -> xh_event_w -> hid_xfer -> xhid_diag_rx -> xhid_note_fmt/note_boot only sets flags; xhid_recover defers while xh_wdepth>0; xhci_mouse_poll has a busy flag. Poll path is not run from an IRQ, BUT it is run from cx_panic_wait (core/crash.c:191) and other loops that may have IRQs off.
- Hang/IRQ-off length: with IRQs off g_ticks stops, so xh_event_w is bounded only by XH_IT_CMD=200000 io_wait (~0.2 s); a timeout then runs xh_ep0_recover = up to 3 commands of 200000 iterations each -> ~0.8 s per try, x3 tries. With IRQs on: ~150 ms + up to 150 ms recover per try. Bounded, not infinite, but each try blocks the main loop.
- Back-off clearing: xhid_bo_reset before EVERY poll retry defeated the v11.02 EP0 back-off on a receiver that NAKs forever: every retry paid the full timeout + recover.
- Timing: first 3/4-byte packet (can be the `01 00 00` left press) scheduled SET_PROTOCOL at once (pnext=now), i.e. sent from the next poll WHILE the button was held; a receiver that switches format mid-press can lose/alter the release.
- Shared e0 state saved per region; nothing else uses the device EP0 after enumeration (xhid_recover only uses commands), so the saved enqueue is consistent.
- SET_IDLE(0): infinite idle, harmless on keyboards; STALL -> existing stalled recovery (Reset EP + Set TR Deq), bounded. Each TIMEOUT though costs 150 ms + recover; enumeration kept sending to every interface after timeouts.
## Changes (controller_init.c)
- xh_ctrl sets xh_ctrl_to: 0 answered/stalled, 1 timeout, 2 skipped (EP0 back-off hold).
- Enumeration: after any timeout (SET_PROTOCOL or SET_IDLE) no more class requests to that device; the one re-send happens only if the first was skipped by the back-off (not on timeout/STALL), max 1 per enumeration. Kill switch off => SET_IDLE only to boot keyboards (v11.04 behaviour), no re-send.
- Poll path: xhid_note_boot only schedules when the interface ACCEPTED SET_PROTOCOL (pset 1/3), no retry used, device not dead; earliest 500 ms later. xhid_proto_poll returns unless: switch on, xh_wdepth==0, g_pi_depth==0 (weak ref), IRQs enabled (RFLAGS.IF), no button held, >=500 ms since last button change (tracked in xhid_diag_rx + poll) and since mouse_press_t. Max 1 retry per device per enumeration (xhid_pused), no back-off clearing, any failure -> xhid_pdead (permanent until re-enumeration). One control transfer per poll call.
- Kill switch: `int g_usbproto_off` + `int xhci_usbproto(int on)` (0 off, 1 on, -1 query; clears pending). 
## NOT done (outside my file scope): shell wiring
Add to shell/commands/network_and_apps.c dispatch (next to "usbhid"):
`else if(seq(cmd,"usbproto")){ extern int xhci_usbproto(int); int v=xhci_usbproto(seq(arg,"off")?0:seq(arg,"on")?1:-1); term_puts(v?"usbproto: on\n":"usbproto: off\n"); }`
(check seq/term_puts names there). shell/commands/usbhid.c "retries" line still works (ptry now 0/1).
## Tests
ccheck xhci.c: 46 warnings before/after. tools/tests/usb/hidsim_gen.py: stub `static int g_usbproto_off, xh_ctrl_to;` (sim extracts the enumeration block). `python3 tools/check.py --only usb`: 12/12.
