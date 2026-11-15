# sweep1 report — drivers/ quarter (NoovexOS 64, v11.02 review)

Read-only review. No repo edits. Time-boxed 20 min, so coverage is partial (see bottom).

## HIGH

### H1. HID transfer events are dropped while a command/control wait runs -> TRBs never re-armed -> mouse+keyboard dead for good
- drivers/usb/xhci/controller_init.c:178 (xh_event_t), with hid_keyboard_mouse.c:103 (xhid_arm only from hid_xfer) and controller_init.c:336 (16 TRBs armed once).
- While xh_cmd_s / xh_ctrl / xh_bulk wait, every HID transfer event on the same controller goes into `xh_q[32]` (shared by all devices). When the queue is full the event is silently discarded (`if(xh_qn<32){...} continue;`). Each HID endpoint has exactly XHID_INFLIGHT=16 TRBs and a TRB is only re-armed when its event is processed by hid_xfer. 16 dropped events for one endpoint = endpoint has no TRBs left = no more reports, forever (until replug/reinit).
- Failure scenario: click Start > Camera -> uvc_start (uvc_webcam.c:34-60) runs up to 2x16 tries, each 3 control transfers + 1-2 Configure Endpoint commands (and on a timeout each xh_ctrl now costs up to 3 x 0.4 s with the v11.01 Stop Endpoint + Set TR Dequeue). Mouse moving / keys pressed during that on the same xHC produce >32 events -> overflow -> both combo receivers' endpoints starve -> pointer and keyboard look frozen exactly as reported. Same during hub polling, USB stick I/O (msd_wait_rounds up to 150), mic setup.
- Fix (small): in xh_event_t, do not queue HID events at all — call `hid_xfer(sl,ep,c,res,ptr)` inline (it only decodes and rings a doorbell, never waits, so no recursion into xh_event_t). If you prefer to keep the queue: on overflow at least re-arm, i.e. find the HID entry for (g_xcur,sl,ep) and call xhid_arm(e). Also add a watchdog in xhci_mouse_poll: if an endpoint got no event for N seconds, re-arm/reset it.
- Confidence: high that the mechanism exists; medium that it is THE freeze (also check H2).

### H2. HID interrupt endpoints are never un-halted after an error (stall / transaction error / babble)
- drivers/usb/xhci/hid_keyboard_mouse.c:102-103.
- On cc != 1/13 (e.g. cc=4 USB Transaction Error, 6 Stall, 3 Babble — common with 2.4 GHz receivers behind a hub/EMI) the xHC puts the endpoint in Halted state. hid_xfer just queues one more TRB and rings the doorbell; a halted endpoint ignores doorbells until Reset Endpoint (cmd 14) + Set TR Dequeue (cmd 16). Result: that device is dead until replug. usb_network.c:55-68 already has the right pattern (usn_unhalt).
- Failure scenario: one transaction error on the mouse receiver -> mouse stops; keyboard on the same receiver may follow.
- Fix: in hid_xfer, if cc is 3/4/6, set a per-entry `halt` flag; in xhci_mouse_poll (not inside xh_event_t) issue Reset Endpoint (dci) + Set TR Dequeue to `xhid_epin[e]+xhid_ein_enq[e]*16 | cyc`, then re-arm XHID_INFLIGHT TRBs (copy usn_unhalt). Rate-limit to avoid loops.

### H3. Command completions are not matched to the command that was issued (stale completion after a timeout poisons every later command)
- drivers/usb/xhci/controller_init.c:189 (xh_cmd_s takes the first type-33 event, ignores its Command TRB Pointer `pp`).
- v11.01 made xh_ctrl issue Stop Endpoint + Set TR Dequeue on timeout. If any command completes later than the 400000-io_wait window (e.g. Stop Endpoint on a busy EP0, Configure Endpoint with bandwidth calc on a 2-controller laptop), its completion is consumed by the NEXT xh_cmd_s, which then returns the wrong cc/slot id; from then on every command is off by one (Enable Slot returns a stale slot id, Configure Endpoint "fails" with code 17 = Parameter Error from a different command, etc.). This may also be what the Camera app shows as "USB controller refused the stream (code 17)" (uvc_cfg_ep reports cc of whatever completion it got).
- Fix: in xh_cmd_s remember `trb=XHCI_CMDRING+old_enq*16`; loop xh_event_t(33,&pp,...) until `pp==trb` (bounded), discarding older completions.

## MEDIUM

### M1. Total stall budget of one UI action is unbounded in practice (every wait is ~0.4 s, retried in loops)
- controller_init.c:174 (400000 x io_wait per wait), :204-208 (timeout = 3 waits), uvc_webcam.c:34-35 (2 x 16 tries x ~5 waits), hid_keyboard_mouse.c:149 (msd_wait_rounds up to 150 => ~60 s), storage_fat_and_hubs.c:84 (hub port status every 80 ticks, per port).
- Failure scenario: camera not answering control requests -> uvc_start blocks the UI thread for tens of seconds to minutes (2*16*3 xh_ctrl * 1.2 s ≈ 115 s), during which nothing is drawn and (H1) input is lost. Looks like a hard freeze.
- Fix: in uvc_start, abort the whole loop after the first control-transfer timeout (distinguish "timeout" from "stall"); cap the number of size tries (e.g. 4); call ui_pump()/xhci_mouse_poll between tries; never run uvc_start/aud_setup_ep from a menu click synchronously without a timeout budget.

### M2. Other controller's event ring is not serviced during long waits
- xh_event_t polls only the current controller (g_xcur). Events of the other xHC stay in its 256-entry ring; at 1 kHz HID that is full after ~0.25 s, the controller then posts Event Ring Full and stops scheduling those endpoints until software catches up. Normally recoverable, but combined with M1 it multiplies the outage. Fix: in xh_event_t's idle branch, occasionally (every ~1000 iterations) poll other controllers' HID events via a non-recursive helper.

### M3. Stale EP0 transfer event can satisfy the next control transfer
- controller_init.c:204: xh_ctrl accepts the first type-32 event for (cur_slot, ep1) without checking the TRB pointer is the Status (or failing Data) TRB just queued. After a timeout where the original TD completes late but during the next xh_ctrl wait, the next request returns with the old cc and the old data buffer. Fix: record the status TRB address and require `pp` in [setup_trb, status_trb] of this request.

### M4. Isoch/hub/HID halted-EP0 recovery uses Reset Endpoint on EP0 for cc 3/4 but a "stopped" EP0 TD may still be pending — partial
- controller_init.c:205-208: after Stop Endpoint the xHC emits a cc=26/27 transfer event for EP0; it is queued into xh_q (ep=1) and later handed to hid_xfer, which ignores it (fine), but each one consumes an xh_q slot, making H1 overflow more likely. Fix: drop EP0 events (ep==1) instead of queuing them.

### M5. A lost key-release report leaves auto-repeat and modifiers stuck
- hid_keyboard_mouse.c:38-50 (kbd_report) + :51-55 (usb_kbd_repeat). Repeat and kdown[] are cleared only when a later report shows the key released. If that report is lost (H1 drop, H2 halt, receiver out of range), usb_kbd_repeat pushes the last key forever and Ctrl/Alt/Shift stay "down" in kdown[], which can make clicks act as shortcuts and the UI look hung/spammed.
- Fix: stop repeat if no report from usbk_rep_dev for >1 s (interrupt keyboards resend or idle-report; track last-report tick per entry), and clear that entry's prev[]/mods when its endpoint is reset or removed.

## LOW

### L1. uvc_start retries even when the controller error is permanent
- uvc_webcam.c:52: on Configure Endpoint failure (code 17) the loop continues with the next size, re-running probe/commit; a Parameter Error is not size-dependent. Fix: break on cc 17 / 5 (TRB error), continue only on cc 8 (bandwidth).

### L2. (checked, OK) xhid_bs indexed `e&63`, XHID_MAX=10; HID regions 0x80000..0xD0000 end exactly at MSD_INPUTCTX, no overlap.

### L3. hid_parse: `bitpos[rid]` rid<=255 OK; `idm[(rid>>5)&7]` OK. No defect found, noted as checked.

## Checked and found OK (no finding)
- Scratchpad/DCBAA/command/event ring sizing in xhci_init_one (controller_init.c:15-61).
- Control data buffer 16 KB vs camera config read capped at 16000 (controller_init.c:281); no 64 KB boundary crossing.
- USB audio rate loops (uvc_webcam.c:183,189) bounded because rate is clamped to 8000..192000 (line 207).
- uvc_poll / aud_service guards bounded; event ring dequeue updates correct (EHB bit set).

## Coverage (honest)
- Fully read: drivers/usb/xhci/controller_init.c (all 340 lines), drivers/usb/xhci/uvc_webcam.c (1-284 except JPEG table), hid_keyboard_mouse.c lines 1-160.
- Partly read: drivers/usb/xhci/storage_fat_and_hubs.c lines 60-258; hid_keyboard_mouse.c 160-335 not read.
- Grep-only sweep for unbounded loops across all of drivers/ (no hang found outside USB; e1000e/igb tx waits time-bounded; ps2 waits bounded; rtw8822c waits bounded).
- Skipped: usb_network.c (except unhalt), input/, net/wifi (beyond grep), storage/, acpi/, sound/, video/, virtio/, iommu/, gpio/, i2c/, the rtw tables.
