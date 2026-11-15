# ring report (v11.02): HID input re-arm and halt recovery

## Findings
- F1 (high, conf high) drivers/usb/xhci/hid_keyboard_mouse.c hid_xfer: on cc 3/4/6 (babble/transaction error/stall) the endpoint is Halted, but the code only queued one more TRB and rang the doorbell. A halted endpoint ignores doorbells until it gets Reset Endpoint and Set TR Dequeue, so that mouse or keyboard stayed dead for good. This is a strong candidate for "input froze" on real hardware.
- F2 (info) Re-arm accounting: every path through hid_xfer that has a matched HID endpoint calls xhid_arm exactly once: success, short packet, other error, unmatched buffer (ba falls back to dbuf) and bogus residual (got=0). The early returns are usn_xfer (not a HID endpoint) and d<0 (unknown endpoint); neither should re-arm. No path arms twice. Events queued in xh_q during xh_event_t are drained by xhci_poll_one, so none are lost (but see the Wait coder's slice for queue overflow when xh_qn>=32).
- F3 (medium, conf high) XFAIL-halt in hidsim. Every remaining case has a longest report bigger than MPS (inl=9, mps 6 or 8): the 9-byte keyboard report and the mouse report share one interface. The rlen rule the brief asks for ("MPS when every report fits, else the longest report") was already in the code, so it cannot fix these 280 cases. Real reassembly of multi-packet reports would need per-report-ID lengths (hid_parse only keeps the overall maximum, hid_inlen). Not done.

## Changes (commit on c-ring)
- controller_init.c: new xhid_recover(e) sends Reset Endpoint (cmd 14, EP id=dci), then Set TR Dequeue (cmd 16) to the current enqueue pointer with its cycle bit, then queues XHID_INFLIGHT fresh TDs. Each command uses the existing xh_cmd_s, which is bounded and was not changed.
- hid_keyboard_mouse.c hid_xfer: cc 3/4/6 calls xhid_recover instead of xhid_arm.
- hid_keyboard_mouse.c xhci_mouse_poll: watchdog runs every 256 polls. For each used HID endpoint it reads the output device context (DCBAA[slot] -> EP ctx[dci] dword0 bits 2:0); state 2 (Halted) or 4 (Error) calls xhid_recover. Not done: no in-flight counter was added, because it needs new per-endpoint state that the test extractors would also have to copy. The one-re-arm-per-completion rule (F2) keeps the count at 16, but a lost event is not detected.
- Tests: check.py g_usb extracts xhid_recover. In hidsim_gen and hidmouse_gen xhid_recover is a stub (hidmouse: one re-arm, so the ring model stays one-to-one). hidsim_main now models recovery: a babble resets the TD instead of halting it for good (g_nrecov) and is still classed XFAIL-halt.

## Tests
hidkbd ALL PASS (44), hidparse ALL PASS (79), hidmouse ALL PASS (50). hidsim: 6720 cases, ok 6440, still 280 XFAIL-halt, 0 unexpected. The endpoint now recovers instead of dying, but the reports that babbled are still lost. ccheck on drivers/usb/xhci.c: clean apart from one existing unused-function warning. Not run: tools/check.py (not allowed by the rules), QEMU.

## Risk
xhid_recover calls xh_cmd_s from inside the poll path. While it waits, other transfer events go into xh_q, which holds 32. The Wait coder's work on xh_event_t/xh_cmd_s affects how long this can block.
