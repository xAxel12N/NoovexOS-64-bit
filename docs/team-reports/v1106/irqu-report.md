# irqu report (v11.06, branch v6-irqu)

## Findings: can the xHCI/HID path hang CPU 0 with IRQs off on a `01 00 00` click?
- No cli anywhere in drivers/usb/xhci/*. xHCI interrupts are never enabled (USBCMD.INTE not set, IMAN untouched): pure polling, so no USB IRQ handler exists. All xhci_mouse_poll callers (kidle, desk_nested_step, busy_pump, ui_pump, power sleep loop, crash panic wait, main, installer) call it with IRQs on; the cpu_irq_off() sections in present_and_pump.c do not include the poll. cpu_idle does `sti; hlt`.
- Click path hid_xfer -> hid_report_decode (M1) -> in_btn_raw -> xhid_diag_rx -> xhid_note_fmt/xhid_note_boot (M2): all loops are bounded (nlay, nil<8, ccn<=16, got<=rlen<=64); no waits, no locks; in_btn_raw only queues into g_inq. No hang found here. Report-protocol switch (3 -> 5 bytes, ID 1): decoded by layout via hid_bits (bounds-checked); also bounded.
- busy flag in xhci_mouse_poll: no early return between set and release (the `return` is inside _in). Only stuck if the poll is aborted mid-way (fault/longjmp/killed context) -> then USB keyboard+mouse dead forever while the OS still ticks.
- xh_event_w waits: tick-bounded only in the "no event" branch; with a continuous event stream (e.g. an endpoint completing with an error code that hid_xfer re-arms instantly) each wait ran up to the full 200k/400k iterations.
- hid_xfer re-armed on every non-3/4/6 completion code, including Stopped (26/27, TRB not consumed -> ring over-fill) and persistent errors (TRB error etc. -> event storm).
- usn_events (USB network, called from net_poll paths) ran xhci_poll_one outside the poll guard and even when called inside an xh_event_w wait.
- xhci_poll_one queue loop had no iteration cap.

## Changes (small)
- controller_init.c: xh_event_w_in also checks the tick deadline after handling foreign/HID/net events. Shared `xh_poll_busy`/`xh_poll_t0` declared (line 99, outside Proto's M2 block).
- hid_keyboard_mouse.c: xhci_mouse_poll uses the shared flag; if it has been held >500 ticks it is taken over (resets xh_wdepth/want_*; klog "usb: input poll was stuck, taking it over"). hid_xfer: no re-arm on cc 26/27; 32 consecutive error completions -> xhid_recover instead of re-arm. xhci_poll_one queue loop capped.
- usb_network.c: usn_events skips if xh_wdepth>0 or the poll flag is held; takes/releases it otherwise.
- `python3 tools/check.py --only usb`: 12/12 pass. ccheck clean (no new warnings).

## Not in my scope but worth checking (freeze with no red box)
- The xHCI code cannot disable IRQs itself; an IRQ-off CPU-0 freeze on first left click is more likely in the UI side (graphics/ui/input.c g_marq path, desk_win_exposed/desk_popups_clear loops) or a heap/BKL IRQ-off section, not in the USB path.
