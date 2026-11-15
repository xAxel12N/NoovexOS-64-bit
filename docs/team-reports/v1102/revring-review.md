# RevRing review (v11.02): c-ring halt recovery + interaction with c-wait

Scope: commit 671aea4e (c-ring) and cddaa3f7 (c-wait). Both are ALREADY MERGED into main
(267f2877 / cddaa3f7 sit in `git log main`), so `git diff main` on the worktrees shows main's newer
work reversed. I reviewed the commits themselves and the merged code on main.
Line numbers are for main: controller_init.c (CI), hid_keyboard_mouse.c (HK).

## What is correct
- Opcodes and fields: Reset Endpoint = type 14, Set TR Dequeue = type 16. `type|(dci<<6)` is shifted by
  `<<10` in xh_cmd_w, so the EP ID lands in dword3 bits 20:16. TSP=0. Slot ID goes in bits 31:24. Correct.
- DCI and endpoint index: xhid_dci = epnum*2+1 for IN, and the same value goes into the doorbell, the
  command EP ID and the context index. Correct.
- Output context offset: `dc + ctx_sz()*dci` (slot ctx at index 0, EP ctx for DCI n at index n; ctx_sz is
  32 or 64 from HCCPARAMS1.CSZ). Dword0 bits 2:0 = EP state (2 Halted, 4 Error). Correct. (The input
  context uses dci+1 elsewhere, which is also correct.)
- Set TR Dequeue pointer = `epin + enq*16 | cyc`. It is 16-byte aligned, SCT=0, and DCS = the producer cycle
  of the next free TRB. The TRB at enq still has the old-lap cycle bit, so the HC waits there. Correct.
- Capacity in the normal case: the 15 TDs still queued after the halted TD are skipped by Set TR Dequeue,
  produce no events and are never reclaimed (they are overwritten one lap later). After the 16 new arms,
  16 TDs are live out of 255 ring slots, and the 16 buffers rotate without overlap. Correct, but ONLY if
  both commands succeed (see H2).

## HIGH

### H1. A nested command from hid_xfer inside c-wait's xh_event_w loses the outer completion (CI:192, HK:103, CI:263-267)
c-wait made xh_event_w dispatch HID transfer events inline (`if(xh_hid_ep(sl,ep)){ hid_xfer(...) }`).
c-ring made hid_xfer call xhid_recover, which calls xh_cmd_s twice, which recurses into xh_event_w(33).
- Outer is xh_cmd_w(A) (Address Device, Configure EP for the camera or mic, Stop EP...): the inner call
  sets xh_want_cmd=B. When A's completion arrives, `pp!=xh_want_cmd` counts it as stale and DROPS it
  (CI:196). Then the inner call clears xh_want_cmd=0, the outer waits for an event that is already gone,
  times out after XH_T_CMD and reports failure. The command actually succeeded on the HC, so software and
  HC state disagree (for example the camera "refused the stream", or the slot is half-configured).
- Outer is xh_ctrl (want_type 32 on EP0): during the inner wait(33) the EP0 completion matches
  `want_type!=32`, so it is not HID and lands in xh_q. The outer never sees it, times out, and runs
  xh_ep0_recover plus xh_bo_note(1). Enough of these and the per-slot back-off kicks in, so a single
  babbling mouse can make the camera or mic open fail.
- Re-entrancy: depth is bounded by the number of HID endpoints, but each level can drop one completion.
  core/crash.c:185 also calls xhci_mouse_poll from the crash screen, which can be inside a wait.
Fix: never issue commands from inside a wait. Defer the recovery (patch below, together with H2/H3).

### H2. Recovery ignores both command results and always arms 16 more TDs, so TDs pile up and the ring overruns (CI:263-267)
If Reset EP fails, or Set TR Dequeue fails (cc 19 Context State Error because the EP is not Halted or
Stopped, or a timeout), the old 15 TDs are still live and 16 more are added. Every later completion
re-arms one, so the in-flight count stays at 31, and the next spurious recovery makes it 47, and so on. Past 16, two
live TDs share one 64-byte buffer (`xhid_bs&15`), which corrupts reports. Past 254, the enqueue pointer
overwrites TRBs the HC has not consumed yet, so the ring is corrupt and the endpoint is dead.
A concrete trigger is the watchdog racing the event path. The endpoint halts after poll_one drained the
ring, the watchdog sees state 2 and recovers, and on the next poll the queued error event calls hid_xfer
-> xhid_recover again on a Running EP. Reset gets 19, Set TR Dequeue gets 19, and 16 extra TDs are armed.
Fix: read the EP state first, recover only from 2/4, and arm only when Set TR Dequeue returns success.

### H3. No limit or back-off, so recovery storms (HK:103, HK:121-126)
- A babbling device: ring-report F3 and hidsim's 280 XFAIL-halt cases show reports that babble on every
  second report. That means 2 commands + 16 TRBs + 1 doorbell for every 2 reports, at 125-1000 Hz, run from
  kidle and from the UI pump. With H1 that becomes a steady source of dropped completions.
- A stall (cc 6): the device side stays halted until CLEAR_FEATURE(ENDPOINT_HALT) (see M1), so every IN
  stalls again, giving an endless loop.
- A device unplugged or behind a dying hub: transaction errors (cc 4) on every re-arm until the hub or
  port code removes it.
Fix: per-endpoint counter, at most 8 recoveries in 100 ticks, then hold for 1000 ticks. Count it in a
diag variable.

### Combined patch for H1+H2+H3 (controller_init.c and hid_keyboard_mouse.c on main)

controller_init.c, add next to the other statics (after line 90, before xh_event_w):
```c
static int xh_wdepth=0; static u8 xhid_rq[XHID_MAX], xhid_rn[XHID_MAX]; static u32 xhid_rt[XHID_MAX], xhid_rhold[XHID_MAX]; u32 xhid_rec_n=0, xhid_rec_skip=0, xhid_rec_fail=0;
```
controller_init.c:184, rename the existing body to `xh_event_w0` and wrap it so every return path is
counted:
```c
static int xh_event_w0(u32 want_type,u32* p0,u8* cc,u8* slot,u8* epid,u32 ticks,int iters){
    ... existing body unchanged ...
}
static int xh_event_w(u32 want_type,u32* p0,u8* cc,u8* slot,u8* epid,u32 ticks,int iters){ xh_wdepth++; int r=xh_event_w0(want_type,p0,cc,slot,epid,ticks,iters); xh_wdepth--; return r; }
```
controller_init.c:263-268, replace xhid_recover:
```c
static u32 xhid_epst(int e){ u32 dc=*(volatile u32*)(u64)(XHCI_DCBAA+(u32)xhid_slot[e]*8); if(!dc)return 0xFFu; return *(volatile u32*)(u64)(dc+ctx_sz()*(u32)xhid_dci[e])&7u; }
static void xhid_recover(int e){
    if(xh_wdepth){ xhid_rq[e]=1; return; }
    u32 now=(u32)g_ticks;
    if(xhid_rhold[e]&&(int)(xhid_rhold[e]-now)>0){ xhid_rq[e]=1; xhid_rec_skip++; return; }
    xhid_rhold[e]=0; xhid_rq[e]=0;
    u32 st=xhid_epst(e); if(st!=2u&&st!=4u)return;
    if(now-xhid_rt[e]>100u)xhid_rn[e]=0; xhid_rt[e]=now;
    if(++xhid_rn[e]>=8){ xhid_rn[e]=0; xhid_rhold[e]=(now+1000u)|1u; }
    u8 c2=0,s2=0; u32 dci=(u32)xhid_dci[e]&0x1Fu;
    if(st==2u&&!xh_cmd_s(0,0,0,14u|(dci<<6),(u32)xhid_slot[e],&c2,&s2)&&c2!=19){ xhid_rq[e]=1; xhid_rec_fail++; return; }
    if(!xh_cmd_s((xhid_epin[e]+xhid_ein_enq[e]*16)|xhid_ein_cyc[e],0,0,16u|(dci<<6),(u32)xhid_slot[e],&c2,&s2)){ xhid_rq[e]=1; xhid_rec_fail++; return; }
    xhid_rec_n++;
    for(int q=0;q<XHID_INFLIGHT;q++)xhid_arm(e);
}
```
(hid_xfer HK:103 stays `if(cc==3||cc==4||cc==6){ xhid_recover(d); return; }`. Inside a wait it now only
sets the flag, and the halted EP is correctly left un-armed.)

hid_keyboard_mouse.c:117-127, replace xhci_mouse_poll's tail:
```c
    for(int c=0;c<g_xhc_n;c++){ xhc_select(c); if(xhci_init_ok)xhci_poll_one(); }
    if(xh_wdepth)return;
    static u32 wd=0; int full=((++wd&255)==0);
    for(int c=0;c<g_xhc_n;c++){ xhc_select(c); if(!xhci_init_ok)continue;
        for(int i=0;i<xhid_n;i++){ if(!xhid_used[i]||xhid_ctl[i]!=g_xcur||xhid_slot[i]<=0)continue;
            if(xhid_rq[i]||full)xhid_recover(i); } }
```
(xhid_recover now reads the state itself, so the watchdog can just call it. A Running EP returns at once
with no commands.)
Also add to xh_cmd_w (CI:205) a canary so any other nested command path shows up in diag:
`static u32 xh_nest_cmd; if(xh_wdepth)xh_nest_cmd++;` (export it next to xh_stale_ev in the usbhid
capture).
Test extractors: check.py g_usb must also pull xhid_epst and the new statics. hidsim/hidmouse stubs stay
as they are.

## MEDIUM

### M1. A stall needs CLEAR_FEATURE(ENDPOINT_HALT) to the device (CI:263)
Reset Endpoint only clears the HC side and resets the HC data toggle to DATA0. For cc 6 the device stays
halted, so it stalls again (feeding H3). For cc 3/4 the device toggle is not reset, so the first report
after recovery can be discarded as a toggle mismatch (one lost report, harmless). Suggested follow-up:
in the deferred path only (xh_wdepth==0), when the last cc was 6, select the device's EP0
(cur_slot/cur_ep0/cur_dbuf/e0_enq for region e, the same switch the enumeration code uses) and send
`xh_ctrl(0x02,1,0,(u16)(0x80|(xhid_dci[e]>>1)),0,0,0)` before Set TR Dequeue. This needs an
xhid_lastcc[e] byte that hid_xfer sets. It is not a one-liner because of the cur_* switch, so do it as
its own small change.

### M2. The watchdog trusts a stale state for a slot being torn down (HK:123-126)
DCBAA[slot] is cleared by xh_disable_slot, which is good. During unplug, though, the slot stays valid
until the hub or port code runs, so a Halted EP on a vanished device gets two commands every 256 polls
(transaction errors). With the H3 back-off this costs at most 8 tries per 100 ticks and then holds, which
is acceptable. Without it the cost is unbounded.

## LOW
- L1 (HK:124): only the low 32 bits of the DCBAA entry are read. That matches the rest of the driver
  (everything is below 4 GiB), so it is fine as long as the device contexts stay in low memory.
- L2 (HK:126): state 4 (Error). Reset EP gets cc 19 in that state, which the old code ignored and the
  patch above skips (it goes straight to Set TR Dequeue, which is legal from Error). OK with the patch.
- L3: the hidsim/hidmouse stub of xhid_recover (one re-arm) cannot catch H1 or H2. Suggest one hidmouse
  case that calls xhid_recover twice on a Running EP (stubbed epst=1) and asserts that g_armed is
  unchanged.
- L4 (outside the slice, seen in c-wait): `xh_event_t(32)` with xh_want_slot==-1 takes ANY transfer event
  that is not HID or usn as its answer, including another device's. Today the callers set the slot, but a
  guard `if(want_type==32&&xh_want_slot<0)` that logs would be cheap.

## Verdict
The recovery sequence itself (opcodes, DCI, context offsets, DCS) is correct. As merged, though, it is
unsafe. The H1 nesting can turn one babbling or stalling HID device into failed commands and failed
control transfers for other devices, which fits the "freeze when opening Camera" class of bugs. H2 can
corrupt the HID ring, and H3 has no storm limit. Apply the combined patch before the v11.02 ISO.
