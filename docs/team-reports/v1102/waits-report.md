# waits report: v11.02 freeze analysis ("click in Start menu, then system and keyboard frozen")

Analyzer: Waits. Read-only; no repo edits. Repo: /home/claude/src/NoovexOS-64-bit-main (HEAD 3828b7f8, v11.01).
Time-boxed at 15 minutes, so the sweep favoured the USB/UI path over a full tree-wide audit.

## Architecture facts that drive everything
- USB HID input is **polled only**: there is no xHCI IRQ. xhci_mouse_poll() runs from the main loop (core/init/main.c:307),
  desk_nested_step / busy_pump / ui_pump (graphics/ui/desktop/present_and_pump.c:123,134,150) and kidle (net/http/http.c:249).
  The wireless combo receivers (062A:4101, 248A:16BB) supply both the keyboard and the mouse, so **any long synchronous call on the
  BSP main thread freezes pointer and keyboard together**. That matches the report: no IRQ deadlock is needed to explain it.
- Every xHCI wait goes through xh_event_t (drivers/usb/xhci/controller_init.c:173). It loops 400000 times with io_wait() = outb 0x80
  (arch/x86_64/cpu/io.h:4), roughly 1-2 us each, so **about 0.4-0.8 s for each timeout**. The bound is a loop count, not ticks or TSC.
- While it waits for any other event, xh_event_t moves HID transfer events into xh_q[32] (controller_init.c:89,178). When that queue
  is full, **the events are silently dropped**. hid_xfer re-arms the endpoint only when it handles an event
  (hid_keyboard_mouse.c:103), so a dropped event means the endpoint loses one of its XHID_INFLIGHT=16 TRBs **for good**.
  The queue is drained only in xhci_poll_one (hid_keyboard_mouse.c:106), and only for the currently selected controller.
- Launching from the Start menu is just open_win() (graphics/ui/wm.c:51, desktop_clicks.c:95). open_win itself does not block.
  The blocking work happens later, **inside the app's draw callback**, called from compose_present in the main loop.

## Ranked top 10 causes of "click in Start menu, then frozen"

1. **Camera: uvc_start() runs synchronously inside camera_draw** (apps/media/camera.c:46 -> drivers/usb/xhci/uvc_webcam.c:16-61).
   - Path: Start > Drivers & Hardware > Camera -> lapp_open -> open_win(W_CAMERA) -> next frame compose -> camera_draw -> uvc_start.
   - It makes up to 2 passes x 16 frame sizes = 32 attempts. Each attempt does 3 control transfers (probe SET, GET, commit;
     uvc_req -> xh_ctrl) and 1-2 Configure Endpoint commands (uvc_cfg_ep, storage_fat_and_hubs.c:236-250).
   - On real hardware every attempt fails with cc 17 (Parameter Error), and the code `continue`s, so all 32 attempts run.
     The message "refused the stream (code 17)" proves the loop goes all the way to the end.
   - Worst case: 32 x (3 x ~2.4 s xh_ctrl timeout+recovery, plus 2 x 0.8 s cmd) = **about 2-4 minutes with no input pumped**.
     Even the "fast" case (32 x 5 round trips) floods xh_q (see #2).
   - Changed in v11.01: indirectly. The xh_ctrl timeout path now issues 2 extra commands (see #3).
   - Reachable from the Start click: yes, directly. This is the item in the photo. **Top suspect.**

2. **xh_q overflow drops HID events, leaving keyboard/mouse endpoints permanently starved** (controller_init.c:178, hid_keyboard_mouse.c:103,106).
   - During any long wait (#1, #4, #5, enumeration), each HID endpoint can complete up to 16 TRBs.
   - The 2 receivers have several interrupt endpoints each (kbd/mouse/consumer), so 32 queue slots fill quickly
     when the user moves the mouse or presses keys while the system "hangs".
   - Each dropped event costs that endpoint one TRB. After 16 drops the endpoint has nothing armed. **Input is then dead even after the
     long call returns**, which looks like a permanent freeze (pointer stuck, keyboard dead), while the clock or other parts may still run.
   - Changed in v11.01: no (the queue predates it), but the v11.01 recovery commands add more wait windows. Fits the brief's prime theory.
   - Reachable: yes, through #1 and #5.

3. **v11.01 xh_ctrl timeout/halt recovery doubles or triples each failed control transfer** (controller_init.c:204-208, commit fb74cb46).
   - On timeout: Stop Endpoint + Set TR Dequeue, each going through xh_cmd_s -> xh_event_t(33) with up to 400k iterations.
   - On cc 3/4/6: Reset Endpoint + Set TR Dequeue.
   - Worst case per xh_ctrl rises from ~0.8 s to ~2.4 s.
   - The Stop Endpoint also produces an extra transfer event (cc 26) for EP0, which takes an xh_q slot (feeds #2).
   - Inside uvc_start's 32-attempt loop, and inside aud_req, this multiplies the freeze.
   - Changed in v11.01: **yes**.
   - Reachable: yes (Camera, Voice Recorder).

4. **Voice Recorder: snd_rec_start -> ausnd_rec_start -> aud_setup_ep** (apps/media/recorder.c:55,62; uvc_webcam.c:206-217,231).
   - Runs in the click/tick path. It makes up to 4 aud_req control transfers plus a Configure Endpoint (aud_cfg_ep), each with the same
     xh_ctrl/xh_cmd_s bounded-loop waits.
   - recorder.c:62 retries snd_rec_start on every tick while !g_cap_on, so **a failing mic restarts the wait over and over**
     and gives a near-permanent freeze.
   - Worst case: ~10 s per attempt, repeated.
   - Changed in v11.01: indirectly (#3).
   - Reachable: yes, Start > Drivers & Hardware > Voice Recorder. Verify this item on hardware.

5. **usb_hotplug_poll in the main loop (port reset plus enumeration on the BSP)** (storage_fat_and_hubs.c:152-155 -> xhci_hid_enum / xh_root_reset).
   - xh_root_reset (controller_init.c:232-233) busy-waits up to 200000 io_wait for PRC, then **always** 50000 io_wait (~50-100 ms).
   - Full enumeration then runs many xh_ctrl/xh_cmd_s calls, each up to ~2.4 s if the device does not respond.
   - Runs every 80 ticks. If a device flaps (camera/mic re-enumerating after a failed stream, or BT 0BDA:C123), this repeats.
   - Changed in v11.01: indirectly (#3).
   - Reachable: not from the click itself, but a failed camera/mic open can trigger re-enumeration.

6. **The camera can sit on the other xHCI controller, and its waits never drain HID events there** (uvc_req/xhc_select, storage_fat_and_hubs.c:170-175).
   - While xh_event_t spins on cam.ctl, nothing services the other controller's ring. Those HID events are not lost (the ring holds 256),
     but nothing is pumped there for the whole of #1.
   - If cam.ctl == the HID controller, #2 applies.
   - Changed in v11.01: no.

7. **uvc_er_init drain loop and uvc_start buffer fills run inside the draw** (uvc_webcam.c:3-7,27-29,56).
   - kmalloc of ~4.6 MB+9 MB, a 1920x1200 rgb clear (uvc_rgb loop line 54), and an 8192-iteration ER drain.
   - Bounded and roughly 10-50 ms each time, multiplied by up to 32 attempts (uvc_ring_init/uvc_er_init run on every attempt).
   - Minor, but adds to #1.

8. **xh_event_t's loop-count bound is CPU- and chipset-dependent** (controller_init.c:174).
   - Not an infinite loop, but there is no tick or TSC deadline. Where outb 0x80 is slow (some PCH/eSPI platforms, ~2-4 us),
     every timeout figure above doubles.
   - Recommend a TSC/hr_ns deadline (e.g. 50 ms for commands, 500 ms for control transfers) and calling ui_pump-lite (drain xh_q) inside the wait.

9. **busy_pump / desk_nested_step are skipped while nested or during compose** (present_and_pump.c:118-140).
   - desk_nested_step returns early if g_comp_depth is set. busy_pump only runs once per tick, and only when something calls g_wait_pump.
   - xh_event_t calls neither, so a draw-time USB wait (camera_draw) never pumps input.
   - No recursion hazard found: both pumps have `static in` guards, and xhci_poll_one does not call back into the UI.

10. **PS/2 waits** (drivers/input/ps2/ps2_mouse.c:6-7, pw_write/pw_read: 200000 io_wait).
    - Bounded at ~0.2-0.4 s. They only matter if a PS/2 aux path is probed from tp_poll/syn_poll during the pump; then the main loop stalls
      for a while on each call.
    - Low likelihood: the owner's keyboard is the USB combo receiver.

## Things checked and ruled out (within the time box)
- No xHCI or input IRQ handler does USB work. kidle and the pumps are main-thread only, so there is no spinlock taken in both IRQ and non-IRQ context on the USB path.
- cpu_irq_off sections in the pumps (present_and_pump.c:126,136) only swap mouse_dx/dy. They are short and do not call into drivers.
- open_win, focus_app_started and gfx_win_event do no blocking work.
- No unbounded `while(!reg)` register polls were found in drivers/ outside third_party (a grep for unbounded inb/XMR/volatile waits found only bounded ones).
- v11.01 ISP bit on interrupt TRBs (controller_init.c:222): with a single-TRB TD plus IOC, there is still one event per TD, so no double event. Not a freeze cause.

## Recommended fixes (for the builders)
1. Move uvc_start out of camera_draw. Run it as a state machine stepped from the main loop: one control transfer per tick.
   At minimum, stop after the first Configure Endpoint cc 17 instead of trying all 32 sizes and passes.
2. In xh_event_t: when xh_qn is full, process the oldest HID event immediately (call hid_xfer, which re-arms the endpoint) instead of dropping it.
   Or always re-arm in place. Never discard a type-32 event for an xhid endpoint.
3. Replace the 400000-iteration bound with an hr_ns deadline. Shorten command waits (~100 ms). Skip Stop Endpoint/Set TR Dequeue
   when the controller is not answering commands (the first cmd timed out).
4. Recorder: rate-limit snd_rec_start retries at recorder.c:62 (e.g. once, or every 5 s) after a failure.
5. Hardware test to confirm: open Camera, wait 3-4 minutes. If input comes back, #1/#3 is confirmed. If it never comes back, #2 is confirmed.
   The usbhid diag (xhid_cnt) not increasing after the freeze would confirm the TRB starvation.
