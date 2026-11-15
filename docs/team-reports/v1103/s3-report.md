# S3 - drivers/usb/ unbounded-loop / wait search (v11.03)

Verdict: no loop that can spin forever was found in drivers/usb/ (xhci core, HID, hubs, storage, UVC, UAC, usb-net).
Every wait has a hard iteration cap even if g_ticks stops (IRQs off). Worst-case single stall is about 0.2-0.4 s.
The USB stack is therefore unlikely to be the cause of a *hard* OS freeze when a drag ends. The plausible USB link is a
**dead pointer** (the endpoint stops and never recovers), which the owner could describe as "the entire OS froze".

## Ranked findings

1. Mouse endpoint can stay Stopped forever, so the pointer and clicks die while the OS runs. Confidence that this
   matches the report: 15%.
   - hid_keyboard_mouse.c:273-278 xhid_recover_now: Reset Endpoint (cmd 14) succeeds, which leaves the EP in the
     Stopped state (3). If Set TR Dequeue (cmd 16) then times out or fails, it returns without xhid_arm.
   - hid_keyboard_mouse.c:126-127: the watchdog only recovers state 2 (Halted) or 4 (Error). A Stopped EP with an
     empty ring is never retried. The mouse is then dead until the device is re-plugged.
   - It also fails for state 4: Reset Endpoint is only valid on a Halted EP, so on an Error EP it returns cc=19 and
     xhid_recover_now returns at line 275. The recovery repeats every 256 polls and never succeeds.
   - Fix (hid_keyboard_mouse.c):
     - In xhid_recover_now, accept cc==19 from cmd 14 and continue to Set TR Dequeue:
       `if(!xh_cmd_s(0,0,0,14u|(dci<<6),slot,&c2,&s2)&&c2!=19)return;`
     - In the watchdog, also recover st==3: `if(st==2||st==3||st==4)xhid_recover(i);`
       This is safe: a Running EP is 1 and is never touched.
   - Link to the drag: a long drag sends many interrupt reports. A wireless receiver can have a transaction error
     (cc 4) at the moment of a burst or release, which halts the EP and starts this path. Ask the owner whether the
     clock still ticks and whether the keyboard still works after the "freeze".

2. Release report delivered from inside a nested USB wait (v11.02 change). Confidence: 5%.
   - controller_init.c:195: xh_event_w_in now calls hid_xfer, then in_btn_raw/in_btn queue, during *any* command or
     control wait. Examples are the hub status poll every 80 ticks (storage_fat_and_hubs.c:82-90) and
     uvc_req/aud_req started by an app draw.
   - in_btn_raw (drivers/input/input.c:12) only updates globals and the queue: no locks, no UI calls. So it cannot
     deadlock.
   - Two side effects:
     - mouse_dx/dy are updated without in_lock. busy_pump and desk_nested_step clear them with IRQs off, and USB
       runs on the same CPU, so in practice this is OK.
     - A release that arrives while a draw-time USB wait runs is queued correctly.
   - Not a freeze source. No fix needed.

3. Possible concurrent xhci_mouse_poll on SMP. Confidence: 5%, needs someone who owns BKL/SMP to check.
   - kidle() (net/http/http.c:249) calls xhci_mouse_poll and is reached from syscall 51 waitkey
     (sys/native/syscall.c:125).
   - xhci_mouse_poll has no reentrancy guard or lock, and it shares ev_deq/ev_cyc/g_xcur/xh_q.
   - If a user process on an AP runs waitkey while the BSP main loop polls USB, both consume the event ring. ev_deq
     then gets out of step, the event ring fills, and all USB input stops for good (pointer and keyboard "frozen").
   - Fix: wrap xhci_mouse_poll with a static atomic flag:
     `static volatile int busy; if(__sync_lock_test_and_set(&busy,1))return; ...; __sync_lock_release(&busy);`
     Also use the same flag in usb_hotplug_poll, uvc_poll and aud_service, because they all call xhc_select.

## Checked and bounded (no action)
- controller_init.c:187-204 xh_event_w_in: capped by iters (200k/400k io_wait) even with g_ticks frozen. The ticks
  timeout is only checked on empty slots; a stream of events still hits the iters cap.
- hid_keyboard_mouse.c:106-116 xhci_poll_one: guard 128 per poll. The queue loop is finite because hid_xfer never
  re-queues.
- Poll path vs wait path on the event ring:
  - Both advance ev_deq/ev_cyc and write ERDP|EHB the same way.
  - Both dispatch HID and usb-net events.
  - Non-32 events are dropped in both.
  - The wait path also drops stale command completions and stale TRB pointers.
  - They are consistent.
- In-flight count: 16 TRBs per HID EP. Even 6 EPs (96) fit in the 256-entry event ring, so a long main-loop stall
  at drag release cannot overflow the ring.
- Hub poll (storage_fat_and_hubs.c:82-90): one control transfer per port every 80 ticks. It breaks on the first
  failure, and xh_bo_* backs off for 500 ticks after 3 failures. xhub_port_reset loop: max 100 x 5 ms (enum only).
- Hotplug (storage_fat_and_hubs.c:109-122): enumerates each new port only once (usb_seen is set first).
- UVC: uvc_poll guard 8192, inner loops capped by ntd, refill capped by ntd-1. uvc_er_init drain capped at 8192.
  uvc_start has a 4 s budget, and it runs only when Camera opens, not on drag release.
- UAC: aud_service guard 4096. `while(acc>=rate)` is safe because rate is clamped to 8000..192000 in aud_setup_ep.
  `while(bsz<mps)` is safe because mps<=0x7FF.
- usb-net: usn_send waits at most 100 ms (hr_ns). It is not present on this machine.
- MSD/FAT loops have guards (256 / 4096 / 1M clusters). xh_bulk is bounded by msd_wait_rounds x XH_IT_BULK; a sync
  can be about 150 x 0.4 s at most, but only on an explicit sync.

## Recommendation for lead
The freeze source is most likely outside USB (UI drag-release path: apply_snap, cache-on at release, app draw). Cheap
USB hardening for v11.03: fix 1 (cc==19 + st==3) and fix 3 (reentrancy flag), both small and local.
