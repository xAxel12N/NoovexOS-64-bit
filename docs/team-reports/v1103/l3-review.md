# L3 logic review: commit 23b259c0 (xhci HID recovery / boot fallback / poll guard)

Verdict: safe to ship. No HIGH findings. The question "does the watchdog recover a healthy Stopped endpoint and double the TDs?" comes out NO (see 1). Two MEDIUM points and three LOW points follow, with patches.

## Key fact the review depends on
`xhid_recover_now` sends TRB type **14 = Reset Endpoint**, not Stop Endpoint (15). The commit message and brief say "Stop", but no HID path ever sends type 15 (`git grep "15u|"` finds nothing). A Reset Endpoint sent to a Running or Stopped endpoint fails with cc 19 (Context State Error) and changes nothing.

## 1. Can the watchdog see a transient 'Stopped' and recover a healthy EP (doubling TDs)? NO (no defect)
- Nothing in our code stops a HID interrupt EP. EP0 recovery (controller_init.c:220) resets DCI 1. usb_network.c:57 resets its own slot. Configure Endpoint calls (storage_fat_and_hubs.c:250, uvc_webcam.c:170) target the camera and mic slots. So st==3 on a HID EP only happens after our own Reset Endpoint, when the Set TR Dequeue that should follow timed out or failed. Recovering from that state is exactly what the watchdog is for.
- Worst case: some controllers leave the output-context EP State at 3 after the doorbell restarts the ring (the xHC is not strictly required to write Running back). The watchdog would then fire on a healthy, running EP every 256 polls. The sequence is: Reset gets cc19 → we read the stale 3 → Set TR Dequeue gets cc19 (the EP is really Running) → we return **before** the `xhid_arm` loop. No dequeue jump, no extra TDs, no lost reports. The only cost is 2 fast commands every 256 polls (and `xhid_recn` caps it at 3 per 100 ticks). That is LOW (see 5).
- When the dequeue jump does happen, Set TR Dequeue goes to `enq`, which drops every outstanding TD. Re-arming XHID_INFLIGHT TDs then restores the normal count and does not double it. Reset Endpoint does not produce a cc 26/27 "Stopped" transfer event the way a Stop would, so there is no stale event to arm an extra TD.

## 2. Does the busy flag ever stay set? NO
`xhci_mouse_poll` has a single exit after `__sync_lock_test_and_set`. All early returns (`wd&255`) live inside `xhci_mouse_poll_in`, so the wrapper always restores `xhid_rbudget` and releases the flag. Every wait inside is bounded by XH_T_CMD. Reentry from an IRQ or another CPU just returns, so it cannot deadlock.

## MEDIUM-1: boot fallback loses right-click (and some button combinations) on receivers that send boot-format reports on a report-protocol EP
Old rule: `!declared || (id==M->id && !fits && len<=4)`. New rule: `!declared && (len<=4 || !id)`.
- Real boot-protocol mice (`xhid_rep==0`) go through the separate path at hid_keyboard_mouse.c:97 and are **unaffected**. Report-mode devices without IDs (`hid==0`) are also unchanged.
- Biltema 062A:4101 (ID 3, 7 bytes, ID 3 declared as a mouse in real_descriptors.h) has `declared=1`, so boot=0 and the report-layout decoder handles it. **Unaffected.** (On COMBO-style receivers ID 3 is consumer and is still not treated as pointer input, which is correct.)
- What regresses: a combo receiver that ignores SET_PROTOCOL (see the test section "receiver ignores SET_PROTOCOL") and sends `[btn,dx,dy,wz]`. Any btn value that equals a declared ID (1 kbd, 2 mouse, 3 consumer, 4 system in RD_COMBO) is now dropped. In particular **right button (btn=2) is lost** for that class. Left (btn=1) was already lost before this commit. Also, an 8-byte padded boot report with any button that is undeclared and non-zero (for example btn=5) used to decode and is now dropped (`len>4 && id!=0`).
- Upside: undeclared long reports (HID++ 0x10/0x11, vendor reports) no longer become phantom motion. The intent (no phantom right-click from `[2,0,0,0]`) is valid. But a genuine report-format ID-2 report in RD_COMBO is 8 bytes, so a ≤4-byte packet that starts with 2 can only be boot format or a truncated packet.
- Suggested patch: keep the phantom-click guard but accept short boot reports that clearly carry motion. In hid_keyboard_mouse.c:61:
```c
int boot=hid?(!declared&&(len<=4||!id))||(id==M->id&&!fits&&len<=4&&(b[1]||b[2])):(!fits&&len<=4);
```
  The existing test `[2,0,0,0]` → g_btn==0 still passes. `[2,5,-7,1]` decodes again as right button plus motion. Optional: add the test `{2,5,(u8)-7,1}` back with this expectation. This does not block the release: the owner's confirmed mouse is the Biltema, and it is unaffected.

## MEDIUM-2: Reset/Set-TR timeout sets a hold but does not set the pending flag
controller_init.c `xhid_recover` clears `xhid_recpend[e]=0` before it calls `recover_now`. On a timeout (`!c2`) the code sets `xhid_hold[e]` and returns, but `recpend` stays 0. A retry then only happens if the watchdog later reads st∈{2,3,4} or a new error event arrives. With a stale output context, or a halted EP that the controller still reports as Running, the mouse stays dead. Patch:
```c
if(!c2){ xhid_hold[e]=g_ticks+1000; xhid_recpend[e]=1; return; }
...
if(!xh_cmd_s(...16u|(dci<<6)...)){ if(!c2){ xhid_hold[e]=g_ticks+1000; xhid_recpend[e]=1; } return; }
```
This is safe: the hold check in `xhid_recover` keeps it pending until 10 s have passed, and the rbudget allows only 1 retry per poll.

## LOW-3: usb_network.c:74 calls `xhci_poll_one()` outside the busy guard, with `xhid_rbudget==-1` (unlimited)
HID completion events picked up there can run `xhid_recover` → command waits from inside `usn_send`, and this can happen concurrently with `xhci_mouse_poll` on another CPU. It only matters when a USB NIC is active (`usn.ok`). The owner's machine has none, so it is not relevant to this report. Patch: in `usn_events` replace `xhci_poll_one();` with
```c
{ int ob=xhid_rbudget; xhid_rbudget=0; xhci_poll_one(); xhid_rbudget=ob; }
```
(With budget 0, recovery becomes `recpend=1` and the next mouse poll handles it.)

## LOW-4: `(st==3&&!xh_wdepth)` is redundant
`xhid_recover` already defers when `xh_wdepth>0`. Harmless.

## LOW-5: stale-Stopped controllers send 2 commands per 256 polls forever
See 1. Optional cheap guard: only treat st==3 as a reason to recover when a recovery for that EP is outstanding:
```c
if(st==2||st==4||(st==3&&(xhid_hold[i]||xhid_recpend[i])))xhid_recover(i);
```
Fallback if MEDIUM-2 is applied: a genuinely Stopped EP whose Set TR timed out keeps `hold`/`recpend` set, so it is still recovered.

## Not a problem (checked)
- cc19 branch: `c2` starts at 0 and is set by the command; the DCBAA/ctx offset matches the watchdog's; st 3/4 are exactly the states where Set TR Dequeue is legal; Halted (2) goes through a successful Reset Endpoint.
- `xhid_hold`, `xhid_recn`, `xhid_rect` and `xhid_recpend` are reset at enumeration (controller_init.c:392).
- One recovery per poll: the pending loop and the watchdog loop both stop when `xhid_rbudget` reaches 0. hid_xfer recovery inside poll_one spends the same budget.
