# H5 report: is the v11.02 xHCI HID change the drag-end freeze?
Verdict: mostly REFUTED. I found no loop in the xHCI HID/event path that can run forever. Every wait is capped
(by g_ticks and by an iteration count) and the poll loop is capped. Chance that xHCI is the root cause: about 10%.
What I did find are ways the mouse endpoint can die for good. A dead pointer looks exactly like "the whole OS froze",
so these are still worth fixing.

## Checked and OK (file:line)
- Watchdog offset: controller_init/hid_keyboard_mouse.c:125-127 reads dc + ctx_sz()*dci, bits 2:0. This is the
  right endpoint context in the output device context (slot ctx at index 0, EP DCI n at index n). States 2 (Halted)
  and 4 (Error) are right. It does NOT fire on a healthy endpoint (Running=1). If it fires on an endpoint that is
  already being recovered, Reset Endpoint fails with cc 19 and the code returns. Nothing is repeated.
- xh_wdepth deferral (controller_init.c:186, 269): recovery never sends a command while another command or control
  transfer is waiting. hid_xfer inside xh_event_w (controller_init.c:195) only re-arms (one TRB and a doorbell). It
  does not change xh_want_*, and in_btn_raw/kbd_report do not call back into the UI or into USB.
- Event ring: there are 2 dequeue sites (hid_keyboard_mouse.c:109-113 and controller_init.c:189-192). Both are
  synchronous polling, there is no xHCI IRQ, and both use the same ev_deq/ev_cyc/ERDP sequence. xhc_save/xhc_load
  swap them per controller. No race and no cycle-bit desync is possible unless polling runs on 2 CPUs at once, and
  I found no caller doing that: the syscall sleep at syscall.c:205 does not poll.
- Ring wrap in xhid_arm (controller_init.c:262-264): the link TRB is set up with TC at init (377-378). On wrap the
  link cycle is set to the producer cycle before the doorbell. 16 in flight out of 255 is fine. The buffer
  round-robin (xhid_bs) matches TRB order.
- xhci_poll_one: the queue loop ends because hid_xfer on a non-HID slot returns at once. The event loop has guard 128.
- Waits: xh_event_w_in exits on ticks, or after XH_IT_CMD=200000 io_wait (~0.2 s) even with IRQs off. ISP+IOC on the
  interrupt TRB has not changed since v11.01.

## Findings (ways the mouse dies for good; none is a CPU hang)
1. (25% that this is what the owner saw, if the pointer stopped while the clock kept running) Endpoint left with no
   TRBs. In xhid_recover_now (controller_init.c:273-277), if Reset Endpoint succeeds and Set TR Dequeue fails or
   times out (XH_T_CMD=10 ticks), it returns. The endpoint is then Stopped (3) with nothing armed. The watchdog
   (hid_keyboard_mouse.c:127) only acts on 2/4, so the mouse never comes back.
   Fix: in hid_keyboard_mouse.c:127 use `if(st==2||st==3||st==4)xhid_recover(i);`. In xhid_recover_now, when Reset
   Endpoint fails with c2==19 (the endpoint is not halted), still try Set TR Dequeue, which is valid in Stopped and
   Error. If that fails, set xhid_recpend[e]=1.
2. Error state (4) can never be recovered. Reset Endpoint on an EP in Error returns Context State Error, so the
   function returns before Set TR Dequeue. The fix above (go on to Set TR Dequeue when c2==19) covers this.
3. Stall (cc 6) on a HID IN endpoint: there is no CLEAR_FEATURE(ENDPOINT_HALT) to the device, so it can stall again
   right away. Recovery is capped at 3 per 100 ticks and then stays pending, so the pointer is dead but the system
   is not hung. Optional fix: send xh_ctrl(0x02,1,0,ep_addr,0,0,0) after Set TR Dequeue. This needs the device's
   cur_* context and must be deferred out of waits.
4. Minor: xhci_mouse_poll (hid_keyboard_mouse.c:117-128) leaves g_xcur set to the last controller and never
   restores it. This is harmless only if every caller that relies on g_xcur selects its controller again after a
   pump. Fix: `int sv=g_xcur; ... xhc_select(sv);` around the body.

## Why none of this matches "freeze exactly when the drag ends"
Nothing in the xHCI path depends on the button state. The release report is a normal 1-13 completion, and a drag
only adds normal reports. The amount of in-flight work does not grow, and no xHCI wait is longer than about 0.2-0.4
s. I would look first at what release starts in the UI: apply_snap, the window content cache switching on at
drag=-1, and an app draw that triggers camera/recorder start or other blocking USB control transfers. xh_ctrl
waits of 150 ms or more, repeated for each frame, could look like a freeze.
Quick test on the owner's machine: after the freeze, does the PS/2 or USB keyboard (e.g. Caps Lock LED) or the clock
still respond? If yes, the pointer is dead (findings 1 and 2). If no, it is a CPU hang somewhere else.
