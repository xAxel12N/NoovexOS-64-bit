# A3 report: v11.00 -> v11.02 code changes by when they run, and whether each can freeze the OS

Range: 4296f5df..v11.02, *.c/*.h, tools/tests left out.

## Key fact
The UI code is the same as in v11.00. Under graphics/ the only diff is a comment in present_and_pump.c:149, and core/init/main.c only changes a comment.
input.c:197 (drag release), apply_snap (wm.c:62), desk_win_exposed and desk_popups_clear are identical to v11.00.
So the drag-release code itself did not change in v11.01 or v11.02. A new cause has to come through the driver
changes that the release report goes through, or through work that happens per poll or periodically and lines up in time.

## (a) Every frame / every poll
1. drivers/usb/xhci/hid_keyboard_mouse.c:121 (v11.02). On EVERY xhci_mouse_poll, the code walks all HID endpoints and calls xhid_recover(i) for any endpoint with xhid_recpend set.
   xhid_recover_now (controller_init.c:274) issues 2 xHCI commands (Reset EP, then Set TR Dequeue). Each one is bounded by XH_T_CMD=10 ticks (100 ms) or 200k io_wait.
   The rate limit is 3 recoveries per 100 ticks PER ENDPOINT, and there is no global cap. If the controller does not complete commands, or the endpoint halts again at once:
   one endpoint costs up to 3 x 200 ms = 0.6 s per second. 2 combo receivers with about 2-3 HID interfaces each gives up to 5 endpoints, so up to 3 s of command waits per 1 s.
   The main loop gets no time, so the WHOLE OS FREEZES (keyboard too, because the keyboard is decoded in the same poll).
   Trigger: an endpoint halts (cc 3 babble / 4 transaction error / 6 stall). Wireless receivers can do this during bursts of traffic, for example the report at button release.
   **Freeze risk: REAL, confidence 35% that it is the owner's freeze.**
2. hid_keyboard_mouse.c:122-127 (v11.02). Watchdog every 256 polls: reads the endpoint state from the output context and calls xhid_recover on Halted(2) or Error(4). Same cost model as item 1.
   Extra bug: an endpoint in the Error state (4) refuses Reset EP (cc 19). recover_now returns early, so the endpoint stays Error. The watchdog then retries forever, and the HID device stays dead. That is a slow loop, not a freeze, unless commands time out.
3. hid_xfer (hid_keyboard_mouse.c:86) runs for each transfer event. It has new boot-format decode (v11.01) and xhid_diag_rx (cheap). On cc 3/4/6 it now calls recover instead of re-arming. On its own it cannot freeze.
4. controller_init.c:187-205 (v11.02). Every USB wait (xh_event_w) now dispatches HID events inside the wait: hid_xfer, in_btn_raw, xhid_arm (doorbell). Recovery is deferred while xh_wdepth>0, so this does not nest. Waits are bounded by ticks.
   If g_ticks does not advance (IRQ off), the iteration caps still apply: 200k-400k io_wait, about 0.2-0.4 s. **No hard freeze.**
   xh_ctrl timeout (controller_init.c:243) now adds xh_ep0_recover (up to 3 commands x 50 ms). Each failed control transfer costs at most about 300 ms.
5. apps/media/camera.c:48 camera_draw (every frame). The auto start runs on the 2nd draw after open, at most once per open (gated by cam_failed/cam_busy).
   uvc_start is bounded at 4 s, 6 config tries or 3 control failures (uvc_webcam.c:37). **Up to about 4 s full freeze ONCE, right after open, not at drag end.**
   The click and key paths (Start, HD, stream) call cam_try_start, with the same bound.
6. apps/media/recorder.c:63 recorder_draw. snd_rec_start runs once per open (rc_tried). ausnd backoff is 5 s, and attempts stop after 4 failures. Bounded.
7. mem/heap.c:65/90 kmalloc/kfree with IRQ off. There is no wait or lock inside, and heap growth (hp_alloc_run) does no TLB shootdown. No freeze.

## (b) Mouse button events
- in_btn_raw is reached from hid_xfer, and in v11.02 it can also be reached from inside any nested USB wait. The queue is single-producer, so this is fine.
  The v11.01 boot-format detection (hid_report_decode:59) decides how a report is read. A wrong decision gives a stuck or missing button, not a freeze. A missing release would leave drag active, but that is not a freeze either.
- Camera buttons call cam_try_start (bounded 4 s). The recorder record button calls snd_rec_start (backoff). Neither is reached by dragging.

## (c) Window move / drag end
- No changed code. The release path (input.c:181-197), apply_snap and the desk_win_exposed resume of browser/video/xdisplay self-present are identical to v11.00.
  The only new code that runs "at release" is the HID transfer path described in (a)1-4.

## (d) Periodic
- HID watchdog: every 256 polls (see a2). Recovery rate limit: 100 ticks. EP0 per-slot back-off: hold of 500 ticks after 3 failures within 300 ticks (controller_init.c:233).
- ausnd start back-off: 5 s. uvc: none.
- ACPI EC (acpi_dev.c:38): back-off is capped at 30 s. After each back-off expires, aec_fresh allows the full 20000-iteration wait again. A dead EC therefore costs about 20-40 ms every 30 s or less. A stutter, not a freeze.
- wifi_mgr autoc tri-state: no new waits.
- sys/native/syscall.c:205 sleep: drops the BKL, which removes an older AP-sleep freeze. revcore F4 is still open: bkl_take overwrites depth, so on preemption the BKL can be freed under another task. Low risk, and not tied to dragging.

## Earlier reports of drag freezes
- This repo has handoffs only for v11.00-v11.02, plus docs/architecture-src/00-whatsnew.md back to v10.86. None of them mentions a freeze on drag or on release.
  The only related item is in the v10.98 notes: the owner saw input/window-action stalls of 8766/2187/973 ms on v10.97, from Wi-Fi/DHCP, which background DHCP fixed.
- The older handoffs are project docs that cannot be reached from here.
- So there is no evidence that v11.00 had the drag-release freeze. The release UI code is unchanged since v11.00, so if it is a pure UI bug, v11.00 would have had it as well.
  v11.01 could not be tested that far because the Camera click froze first. So "new in v11.02" is likely but not proven. The new per-poll and periodic HID recovery is the main new candidate that is active at the moment the mouse sends its release report.

## Suggested fix (v11.03)
1. A global budget in xhci_mouse_poll: at most ONE xhid_recover per poll across all endpoints. Also put in_btn/kbd decode first.
2. In xhid_recover_now: if xh_cmd_s returns 0 with c2==0 (timeout, no completion), set xhid_rect[e]=g_ticks+1000 and xhid_recn[e]=3, so that endpoint waits 10 s before the next try.
   After 5 timeouts, give up on that endpoint (clear recpend and log).
3. For Error state (st==4), skip Reset EP and go straight to Set TR Dequeue.
4. Add a `lag` stage mark around the recovery loop and the watchdog, so the owner's `lag` output shows whether it is the cause.
