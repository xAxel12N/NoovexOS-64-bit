# wait report (v11.02, xHCI waits) - branch c-wait

## Findings
1. HIGH / high confidence - input loss during waits (drivers/usb/xhci/controller_init.c, xh_event_t, old ~line 160).
   While xh_cmd_s / xh_ctrl / xh_bulk wait, transfer events for HID endpoints were only *queued* in xh_q[32]; when full
   they were silently DROPPED. Each HID endpoint keeps 16 TRBs in flight and is only re-armed by hid_xfer, so every
   dropped event loses one TRB for good. With 2 combo receivers (~6 HID endpoints x 16 TRBs) and waits of up to
   ~1.2 s (see 2), one slow control request (camera open from the Start menu: the camera already refuses its stream)
   can overflow the queue; repeated a few times, mouse+keyboard endpoints run dry -> input "frozen" permanently.
   Fixed: HID events are dispatched to hid_xfer immediately inside the wait (re-armed at once); only non-HID events
   are queued; overflow is counted (xh_q_drop). Port-status / other event types are still ignored as before.
2. HIGH / high - waits were iteration-only (400000 x io_wait, ~0.4-0.8 s on real hardware). xh_ctrl timeout path =
   transfer wait + Stop Endpoint wait + Set TR Dequeue wait = up to ~1.2-2.4 s per call, repeated for every request
   (camera probe/commit, hub polling every 0.8 s, usbinfo). Fixed: xh_event_w(ticks, iters) - control transfers
   150 ms, commands 100 ms, EP0 recovery commands 50 ms each (worst case one xh_ctrl ~250 ms), with an iteration
   cap (200000) as fallback when g_ticks is frozen (IRQs off). Bulk (USB stick) keeps the old 400000 iterations
   plus a 400 ms tick bound.
3. MEDIUM / high - stale completions. A command or control TD that timed out completes later; the next xh_cmd_s took
   the *previous* command's completion (wrong slot id / cc, off-by-one forever), and the next xh_ctrl on the same
   slot could take the old TD's event (wrong data). Fixed: command completions must match the command TRB address;
   transfer events for the awaited slot/EP must point at one of this TD's TRBs (also for msd bulk). Pointer 0 is
   accepted (synthetic tests). Counter xh_stale_ev.
4. MEDIUM - v11.01 EP0 recovery: Set TR Dequeue pointer (enqueue|e0_cyc) and EP-ID encoding (1<<6 -> bits 16..20)
   are correct. But it ran even when Stop Endpoint failed (slot gone -> cc 11, controller dead -> another full
   timeout). Now: Set TR Dequeue only if Stop/Reset succeeded or Stop returned 19 (halted -> Reset Endpoint first).
5. MEDIUM - no back-off: a dead device (camera, hub) was hammered on every call. Added per (controller, slot)
   back-off: 3 timeouts within 3 s -> control requests to that slot return failure immediately for 5 s (no TRBs, no
   commands). Cleared on success and when Enable Slot hands out that slot again. Stalls do not count.
6. Not found: no cli/irq-off inside drivers/usb/xhci; no nested waits from hid_xfer (it only re-arms + decodes).
   hub port reset loop (100 x status) exits on the first failed status, so with back-off it is bounded.
   Not checked (outside slice/time): whether xhci_mouse_poll or a UI path calls xh_ctrl from an IRQ context.

## Changed
- drivers/usb/xhci/controller_init.c: xh_event_w/xh_cmd_w (time bounds, HID dispatch, stale filtering),
  xh_ep0_recover, back-off (xh_bo_*), xh_ctrl records its TD TRB addresses. Happy path unchanged.
- drivers/usb/xhci/hid_keyboard_mouse.c: xh_bulk passes its TRB address.
- tools/check.py g_usb: extracts the new helpers; tools/tests/usb_template.txt: xh_cmd_w stub, test 13 updated to the
  new (immediate) dispatch, new section 13b (stale completion skipped, timeout -> Stop + Set TR Deq, back-off after
  3 timeouts, other device unaffected, retry after hold).

## Tests
g_usb / g_xhci_init / g_uvc run individually (imported from check.py into /tmp; full check.py not run): all ok
(usb 91 checks incl. 5 new). ccheck drivers/usb/xhci.c: 0 errors.
