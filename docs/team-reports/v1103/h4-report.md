# H4 report: button-release handling (input.c release path, mouse queue, HID decode)

Verdict: the release path itself does not loop or deadlock. Confidence 85% that the freeze is NOT in input.c or the mouse queue.

## 1. input.c release branch: clean (graphics/ui/input.c:178-197)
- A window-drag release runs only `if(drag>=0&&g_snap) apply_snap(drag,g_snap); drag=-1; g_resize=-1; g_snap=0; g_marq=0;` (input.c:197). apply_snap (wm.c:62-69) is straight-line code with no calls.
- g_ddrag/desk_open and g_fdrag (editor_open, py_open_slot, fx_drop) only run on an icon or file drag, not on a window drag. The statement-expression at :189 loops over NWIN and stops.
- browser_mouse_release only runs if browser_mouse_down() is true.
- The mouse queue (drivers/input/input.c:11-19) is bounded. in_btn_raw adds an entry only when the button state changes, so a release that arrives twice adds nothing. If the queue is full the event is dropped (g_mq_lost) and nothing waits. process_input takes at most IN_MQ_N events with IRQs off and then calls mouse_step once per event.
- desk_nested_step/busy_pump (present_and_pump.c:118-140) have `in` guards, and when g_pi_depth is set they skip process_input. There is no recursion.
- If the release never arrives, the button stays down: the window keeps following the pointer and nothing freezes. That does not match the report.

## 2. Latent HID decode bug (real, but probably not this freeze). Confidence it caused the freeze: 15%
drivers/usb/xhci/hid_keyboard_mouse.c:57-66, hid_report_decode boot fallback:
`boot = hid ? (!declared || (id==M->id && !fits && len<=4)) : (!fits && len<=4);` then `btn=b[0], dx=b[1], dy=b[2]`.
- (a) Interface with report IDs, short report (len<=4) for the mouse ID where X/Y are 12/16-bit: the ID byte is read as the buttons. With mouse ID 1 (odd) the short release report sets left = 1. The button reads as stuck pressed, so a phantom drag or click follows.
- (b) A report with an undeclared ID and len>=3 (vendor reports from the combo receivers 062A:4101 / 248A:16BB) is decoded as a boot mouse report. b[0]=ID becomes the buttons and b[1..2] become deltas, giving phantom clicks and pointer jumps.
- Suggested fix: when `hid && declared`, never fall back to boot. Use the boot fallback only for `!declared && len<=4`, and only when the boot-protocol request was sent or the device has been seen sending reports without an ID. Otherwise `return` (drop the report). Exact change:
  `int boot = hid ? (!declared && len<=4) : (!fits && len<=4);`
  This needs a hidsim test case for a device that has report IDs and really sends boot-format reports. Check tools/tests/usb before changing it.

## 3. Lead for other hunters (outside my scope, where I would look next)
- The moment the button is released is the moment window content drawing starts again. drivers/video/fb/gfx.c:243 has `moving=(drag>=0)&&!live_app(i)&&...`. During a drag of a non-live app, draw_app(i) is not called because the cached layer is moved instead. The first draw_app since the drag started therefore happens right after release, and if the window was snapped it happens at the new size: apply_snap changes w/h and sets max=1, which makes `full` true and recaptures the op list (OPMAX ops via kmalloc'ed opA/opB).
- desk_win_exposed (present_and_pump.c:2) also turns back on at release, so the direct-present paths in browser, video and xdisplay start running again.
- Any app whose draw_app hangs, or spins at a new size, or on its first draw after a pause, would freeze exactly when the drag ends. Check apps/system/logs.c ~96 (dispatcher), and the draw code for camera.c/recorder.c (start on draw) and any app that recomputes its layout from w/h.
- xHCI recover (hid_xfer cc 3/4/6 -> xhid_recover -> synchronous xh_cmd_s) runs with ev_deq already advanced and is guarded by xh_wdepth and a limit of 3 recoveries per 100 ticks. It looks safe from re-entry.
