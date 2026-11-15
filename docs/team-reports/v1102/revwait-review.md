# RevWait review (v11.02, xHCI waits) - c-wait + c-ring interaction

Note: `git diff main` from c-wait/c-ring is not useful anymore. Both branches are already merged into main
(267f2877 Merge c-ring, cddaa3f7 c-wait). This review covers the MERGED code on main:
drivers/usb/xhci/controller_init.c (main) and drivers/usb/xhci/hid_keyboard_mouse.c (main). Line numbers are from main.

## HIGH-1 Nested wait: hid_xfer -> xhid_recover -> xh_cmd_s inside xh_event_w eats the outer waiter's completion
Where: controller_init.c:192 (c-wait: `if(xh_hid_ep(sl,ep)){ hid_xfer(...); continue; }` inside xh_event_w),
hid_keyboard_mouse.c:103 (c-ring: `if(cc==3||cc==4||cc==6){ xhid_recover(d); return; }`),
controller_init.c:263-268 (xhid_recover issues two xh_cmd_s).
Each branch is fine alone; merged, any HID error event (babble/USB transaction error/stall, common on a wireless
receiver) that arrives while another wait is running starts a NESTED command wait:
- Outer = xh_cmd_w (Enable Slot / Address / Configure EP / Disable Slot / Stop EP of xh_ep0_recover):
  inner xh_cmd_w overwrites `xh_want_cmd` with its own TRB, so the outer command's completion arrives in the inner
  loop, gets `pp!=xh_want_cmd` -> counted as stale and THROWN AWAY (controller_init.c:196). On return the inner sets
  `xh_want_cmd=0`, the outer waits for a completion that has already been consumed -> times out -> enumeration
  step fails ("Enable Slot"/"Configure Endpoint") or ep0 recovery skipped.
- Outer = xh_ctrl (EP0 transfer): the inner wait has want_type 33, so the outer's EP0 transfer event matches
  `want_type!=32` and is pushed into xh_q (or dropped if full). The outer never sees it -> false timeout after 150 ms
  -> Stop EP + Set TR Deq on an idle EP0 -> returns failure -> xh_bo_note(1). Three of these in 3 s put the device
  (possibly the receiver itself, or a hub) into the 5 s back-off.
- Recursion: the inner wait can dispatch another HID error -> another xhid_recover -> deeper nesting (bounded only
  by in-flight TRBs, but each level clobbers xh_want_cmd again).
Fix (defer recovery when inside any wait; do it at top level in xhci_mouse_poll):
```
--- controller_init.c (near xh_q decls, line ~90)
+static int xh_wait_depth=0; static u8 xhid_need_rec[XHID_MAX];
--- controller_init.c xh_event_w (line 184)
 static int xh_event_w(u32 want_type,u32* p0,u8* cc,u8* slot,u8* epid,u32 ticks,int iters){
-    u64 t0=g_ticks;
+    u64 t0=g_ticks; int r=0; xh_wait_depth++;
     for(int t=0;t<iters;t++){ ...
-            if(ty==want_type){ if(p0)*p0=pp; if(cc)*cc=c; if(slot)*slot=sl; if(epid)*epid=ep; return 1; }
+            if(ty==want_type){ if(p0)*p0=pp; if(cc)*cc=c; if(slot)*slot=sl; if(epid)*epid=ep; r=1; break; }
         } else { if(g_ticks-t0>(u64)ticks)break; io_wait(); }
     }
-    return 0;
+    xh_wait_depth--; return r;
 }
--- hid_keyboard_mouse.c:103
-    if(cc==3||cc==4||cc==6){ xhid_recover(d); return; }
+    if(cc==3||cc==4||cc==6){ if(xh_wait_depth){ xhid_need_rec[d]=1; return; } xhid_recover(d); return; }
--- hid_keyboard_mouse.c xhci_mouse_poll, after the xhci_poll_one loop (line ~119)
+    for(int c=0;c<g_xhc_n;c++){ xhc_select(c); if(!xhci_init_ok)continue;
+        for(int i=0;i<xhid_n;i++) if(xhid_need_rec[i]&&xhid_used[i]&&xhid_ctl[i]==g_xcur){ xhid_need_rec[i]=0; xhid_recover(i); } }
```
(xhid_recover calls from xhci_poll_one/watchdog are top level, depth 0, so they stay direct.) Clear
xhid_need_rec[e] wherever an xhid entry is freed/assigned. Add a check.py case: HID cc=4 event during xh_ctrl
wait -> xh_ctrl still returns ok and no command is issued until xhci_mouse_poll.
Belt and braces (optional): in xh_cmd_w save/restore instead of zeroing: `u32 sv=xh_want_cmd; xh_want_cmd=at; ...; xh_want_cmd=sv;`
and same for xh_want_slot/ep/ptr/pn in xh_ctrl/xh_bulk.

## MEDIUM-1 xhid_recover ignores command results (controller_init.c:263-268)
Arms 16 TRBs and rings the doorbell even when Reset EP / Set TR Deq timed out or failed (slot gone, cc 11). The
watchdog (hid_keyboard_mouse.c:121-126) re-runs it every 256 polls for an endpoint stuck in Halted/Error, so a dead
receiver costs up to 2 x 100 ms command timeouts per endpoint every few seconds (UI stutter, looks like a freeze with
2 receivers x 3 EPs) and keeps advancing enq by 16 on a ring whose dequeue never moves (enq laps the HW dequeue
after ~15 rounds).
```
 static void xhid_recover(int e){
     u8 c2=0,s2=0; u32 dci=(u32)xhid_dci[e]&0x1Fu;
-    xh_cmd_s(0,0,0,14u|(dci<<6),(u32)xhid_slot[e],&c2,&s2);
-    xh_cmd_s((xhid_epin[e]+xhid_ein_enq[e]*16)|xhid_ein_cyc[e],0,0,16u|(dci<<6),(u32)xhid_slot[e],&c2,&s2);
+    if(!xh_cmd_w(0,0,0,14u|(dci<<6),(u32)xhid_slot[e],&c2,&s2,XH_T_FIX)&&c2!=19)return;
+    if(!xh_cmd_w((xhid_epin[e]+xhid_ein_enq[e]*16)|xhid_ein_cyc[e],0,0,16u|(dci<<6),(u32)xhid_slot[e],&c2,&s2,XH_T_FIX))return;
     for(int q=0;q<XHID_INFLIGHT;q++)xhid_arm(e);
 }
```
(c2==19: Reset EP on an Error-state EP; Set TR Deq is still valid there.) Optionally add a per-endpoint retry
counter so the watchdog gives up after N failed recoveries until re-enumeration.

## MEDIUM-2 Back-off hold never expires if g_ticks stops (controller_init.c:222)
`(int)(h-(u32)g_ticks)>0` is true forever if g_ticks is frozen (IRQs masked / PIT not firing and pit_poll_tick
not running). The waits fall back to the iteration cap, but the back-off has no such fallback -> a slot can be
locked out permanently (e.g. a hub -> devices behind it never enumerate). Low likelihood (no cli in xhci, IRQs on in
desktop), but cheap to bound:
```
-static int xh_bo_held(void){ ... u32 h=xh_bo_hold[g_xcur][cur_slot]; if(h&&(int)(h-(u32)g_ticks)>0){ xh_bo_skips++; return 1; } ...
+static u16 xh_bo_cnt[XHC_MAX][256];
+static int xh_bo_held(void){ ... u32 h=xh_bo_hold[g_xcur][cur_slot]; if(h&&(int)(h-(u32)g_ticks)>0&&++xh_bo_cnt[g_xcur][cur_slot]<64){ xh_bo_skips++; return 1; } if(h){ xh_bo_hold[g_xcur][cur_slot]=0; xh_bo_cnt[g_xcur][cur_slot]=0; } return 0; }
```

## LOW
- L1 controller_init.c:198: tick bound is only checked when the ring is empty. A continuous stream of other events
  (6 HID EPs at 1 kHz) keeps the loop busy; bound then is XH_IT_CMD events, still finite (~200 k). OK, note only.
- L2 Stop Endpoint on EP0 produces a Transfer Event cc=26/27 for EP0 while waiting for type 33; it is queued into
  xh_q and later discarded by hid_xfer (d<0). Harmless but uses queue slots; could filter `ep==1&&(c==26||c==27)`.
- L3 Pointer compare (xh_ptr_ok, xh_want_cmd) uses only e[0] (low 32 bits); correct as long as rings stay
  identity-mapped below 4 GiB (they are fixed low addresses). TRB addresses are captured after the link-TRB wrap,
  so the link TRB is never expected; cycle bit is not part of the address. Correct. Pointer 0 accepted: fine.
- L4 xh_event_w only drains the CURRENT controller's ring; HID on the other controller still waits for
  xhci_mouse_poll. Same as before, no regression.
- L5 usn_xfer (usb_network.c:37) does not wait; kbd_report/hk_push only queue. No other nested waits found from
  hid_xfer besides xhid_recover.

## Happy path
xh_ctrl/xh_cmd_w/xh_bulk happy path is equivalent to v11.01 (same TRBs, extra bookkeeping only). Command
completion matching against the command TRB address and transfer matching against the TD's TRBs are correct.
EP0 recovery guard logic (skip Set TR Deq unless Stop/Reset ok or cc 19 -> Reset) is correct. Back-off is per
(controller, slot), cleared on success and on Enable Slot, so a fresh mouse/keyboard enumerate is not blocked;
a hub in back-off delays detection behind it by at most 5 s (unless MEDIUM-2).
