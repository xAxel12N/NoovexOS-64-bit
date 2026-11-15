# r2-review (v11.06, v11.05..HEAD, drivers/usb + network_and_apps.c) — read-only

Tick rate: pit_init(100) -> 500 ticks = 5 s, XHID_BTN_QUIET 50 = 500 ms.

## HIGH 1: 32-errors recovery loses a TD on a RUNNING endpoint (endpoint drains to 0 TDs, mouse dead)
hid_xfer no longer re-arms when xhid_errn>=32 and calls xhid_recover. xhid_recover_now issues Reset Endpoint
(cmd 14); on a running EP that fails with cc 19, state==1 -> `if(st!=3&&st!=4)return;` -> no Set TR Deq, NO re-arm.
Each 32-error burst (non-halting errors, e.g. cc 2 data buffer) permanently drops one of XHID_INFLIGHT TDs.
Patch (controller_init.c xhid_recover_now): stop the running EP first, then the existing Set TR Deq + re-arm path runs.
```
-        if(st!=3&&st!=4)return; }
+        if(st==1){ c2=0; if(!xh_cmd_s(0,0,0,15u|(dci<<6),(u32)xhid_slot[e],&c2,&s2)){ if(!c2){ xhid_hold[e]=g_ticks+1000; xhid_recpend[e]=1; } if(c2!=19)return; } }
+        else if(st!=3&&st!=4)return; }
```
(The Stop produces a cc 26 event for the in-flight TD; hid_xfer now returns on 26 = correct, Set TR Deq discards it.)

## HIGH 2 (latent, only if two contexts can ever poll): takeover is not safe vs a slow-but-alive holder
xhci_mouse_poll takes over after 500 ticks of xh_poll_busy without checking the holder is dead:
(a) holder is still consuming ev_deq/ERDP -> two pollers -> corrupted event ring;
(b) holder later does __sync_lock_release -> frees the TAKER's lock -> a third poller can enter;
(c) takeover sets xh_wdepth=0 while holder is inside xh_event_w -> holder's xh_wdepth-- leaves -1 forever ->
    `st==3&&!xh_wdepth` (hid_keyboard_mouse.c:137) never true again.
A legit holder can exceed 5 s (hotplug enumeration with ctrl timeouts + ep0_recover + hub delays).
On a single CPU with no preemption of the poll this only fires for a dead (faulted) holder = intended. Make it robust anyway:
```
controller_init.c line 99: add   static volatile u32 xh_poll_gen;
xh_event_w:  { xh_wdepth++; int r=...; if(xh_wdepth>0)xh_wdepth--; return r; }
xh_event_w_in loop top:  if(!(t&1023))xh_poll_t0=g_ticks;      /* heartbeat: alive holder never looks stuck */
xhci_mouse_poll:
    if(__sync_lock_test_and_set(&xh_poll_busy,1)){ if(g_ticks-xh_poll_t0<=500)return; xh_poll_gen++; ...existing reset...; klog(...); }
    u32 my=xh_poll_gen; xh_poll_t0=g_ticks;
    ... ; if(xh_poll_gen==my)__sync_lock_release(&xh_poll_busy);
usn_events: same `u32 my=xh_poll_gen;` after taking the flag and `if(xh_poll_gen==my)__sync_lock_release(...)`.
```

## MEDIUM: xhid_errn counts Stopped (26/27) as errors
Move the 26/27 return BEFORE the counter:
```
-    if(cc==1||cc==13)xhid_errn[d]=0; else if(xhid_errn[d]<255)xhid_errn[d]++;
+    if(cc==26||cc==27)return;
+    if(cc==1||cc==13)xhid_errn[d]=0; else if(xhid_errn[d]<255)xhid_errn[d]++;
     if(cc==3||cc==4||cc==6||xhid_errn[d]>=32){ xhid_errn[d]=0; xhid_recover(d); return; }
-    if(cc==26||cc==27)return;
```
Also xhid_errn is static per index, never cleared on re-enumeration: reset it where xhid_arm(e) is first called in xhci_enum_loc (needs file-scope array).

## LOW
- Stopped 26/27 no-re-arm: correct. Only Stop Endpoint in tree is EP0 (dci 1, controller_init.c:262); HID EPs never see 26/27 today, and after HIGH-1 every Stop is followed by Set TR Deq + XHID_INFLIGHT arms -> no TD loss.
- xh_event_w_in tick check under event streams: OK and bounded; with IRQs off g_ticks is frozen so only the iteration bound applies (unchanged). Side effect: a long foreign stream can turn a late-but-valid ctrl completion into timeout -> enumeration now sets pdead and skips SET_IDLE; acceptable.
- usn_events under the shared flag: correct (prevents concurrent xhci_poll_one). usn_send's 100 ms txout loop just spins and fails if the flag is held elsewhere (no corruption).
- xhci_poll_one cap (i<32, qg<64): bounded; hid_xfer->recover may append to xh_q during the loop, leftovers handled next poll. Fine.
- Panic wait: a fault in hid_report_decode/in_btn_raw is not matched by cx_in_usb ("xh/uf/um/scs/msd"), so cx_panic_wait polls with xh_poll_busy held; with IRQs off g_ticks is frozen -> never taken over -> no USB keyboard on the crash screen (PS/2 + countdown still work). Optional: add `(nm[0]=='h'&&nm[1]=='i'&&nm[2]=='d')||(nm[0]=='i'&&nm[1]=='n'&&nm[2]=='_')` to cx_in_usb.
- Proto: restrictions match the report (switch, xh_wdepth==0, weak g_pi_depth, IF=1, no button held, 500 ms quiet after btn change and mouse_press_t, pset 1/3 only, 1 retry/device, pdead on any failure, one ctrl per poll, no bo_reset in poll). mouse_btn is updated synchronously in in_btn_raw so xhid_btn_track is current. Kill switch clears ppend, prints state, `usbproto` alone queries. Enumeration: STALL -> no retry; only back-off-skipped (pto==2) gets the single re-send. OK.

## Biltema 248A:16BB / normal mice
- Biltema: decode path unchanged (ID 1 declared, xl=5, 8-bit X/Y -> not wide -> amb=0 -> rule1 -> boot; `01 00 00`/`00 00 00` decoded as buttons). If it ACKs SET_PROTOCOL: exactly one late retry >=500 ms after the last button change, IRQs on, never during a press. If it STALLs/times out: no retry, pdead. Click path no longer can trigger a ctrl transfer while held. OK.
- Normal report-protocol mice: note_fmt len>=need -> pfmt=2, no retry; SET_IDLE still sent (unless timeout/pdead); with usbproto off SET_IDLE only to boot keyboards (v11.04). Success completions reset errn. OK.
- None of these changes can disable IRQs; agree with IrqU that the no-red-box freeze is outside drivers/usb.
