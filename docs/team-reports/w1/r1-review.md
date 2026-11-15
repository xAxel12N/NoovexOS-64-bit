# r1 review of c1 (branch x-c1, commit 9d74f503, merged into main as v11.07)

Scope I checked: `git log -p v11.06..x-c1`, c1-report.md, ctasks/c1.md. I also checked the merged files in main
(drivers/usb/xhci/controller_init.c, hid_keyboard_mouse.c, drivers/input/input.c, ps2.c, vmmouse.c,
graphics/ui/input.c:288, graphics/ui/desktop/present_and_pump.c:127/135) and diffed x-c1 against main for the same files.
The only other change in these files is c-BT's btu_xfer/btu_active hooks. They merged cleanly and do not interact with c1's changes.
Test re-run (read-only, in /tmp): hidmouse generator on main + gcc → ALL PASS (73 checks, 0 failed).

Verdict: no HIGH or MEDIUM findings. All three task items are correct. c1 was right to reject the
`g_usbproto_off=1` patch: that flag also gates enumeration at controller_init.c:439/441. c1 was also right to reject
`&&!L0->bseen`: it breaks the combo-ID2 phantom-right-click test.

## Verified correct
1. Late SET_PROTOCOL off by default. controller_init.c:110 `g_usbproto_late_off=1`. It is checked only in
   xhid_note_boot (:118) and xhid_proto_poll (:128). Enumeration (:439 SET_IDLE, :441 SET_PROTOCOL re-send) still
   reads g_usbproto_off, which is 0, so enumeration is unchanged. `usbproto on` restores the exact v11.06 default
   (both flags 0). `usbproto` with no argument now reports the late state, so a fresh boot prints "off". No other readers of either flag
   exist (grep). In hidsim_gen.py the stub declares only g_usbproto_off, which is enough: the sim pulls in only the enumeration block.
2. amb fix, hid_keyboard_mouse.c:63 `&&!(L0->bseen&&id==1)`. `wide` needs a mouse layout with M->id==id, so the
   exception applies only when the mouse report ID is 1 and the boot latch is set. In that case rule1 fires (len 3 < xl) → boot
   left press. A full-length report packet (len>=xl) still clears the latch (:66), so a device that really does switch to
   report protocol is not misread. ID-2 still `02 00 00 00` stays ambiguous, so the existing phantom test still passes.
3. in_rel/in_scroll routing. The signs match the old code everywhere: HID `mouse_dy-=dy` → in_rel(dx,dy). PS/2 `mouse_dy+=dy` →
   in_rel(dx,-dy). PS/2 `scroll-=z` → in_scroll(-z,0). hscroll → in_scroll(0,h). vmmouse `mouse_dy-=ty-cy` → in_rel(..,ty-cy).
   in_lock does cpu_irq_save, so the PS/2 IRQ path (interrupts.c:78) cannot deadlock against a main-loop holder on the same CPU.
   Every locked section held via in_lock_x (ui/input.c:288, present_and_pump.c:127/135) contains only plain memory ops, with no USB polling.
   in_btn_raw already took the same lock from these same call sites, so the change adds no new deadlock path.
   The PS/2 vertical wheel now sets mouse_evt, which is not a behaviour change: ps2.c:111 already set it for every packet.

## LOW findings

### L1 — the test "wide1: 00 00 00 -> left up" proves nothing (tools/tests/usb/hidmouse_main.c, last wide1 check)
`feed()` (hidmouse_main.c:40) sets `g_btn=0` before every decode, so `g_btn==0` holds even if the release is never decoded.
Patch: check the release explicitly, without the reset:
```
-    { u8 r[3]={0,0,0}; feed(r,3); chk("wide1: 00 00 00 -> left up",g_btn==0); } }
+    { u8 r[3]={0,0,0}; g_btn=7; hid_report_decode(0,r,3); chk("wide1: 00 00 00 -> left up",g_btn==0); }
+    { u8 r[6]={1,0,5,0,0,0}; feed(r,6); chk("wide1: full-length report after latch -> report decode, latch cleared",L0->bseen==0&&mouse_dx==5&&g_btn==0); } }
```
(The second line adds a missing check: the latch exception must not survive a real report-protocol packet.)

### L2 — vmmouse.c:52 reads `mx+mouse_dx` outside the lock (the old code read it under cli)
A PS/2 IRQ can add a delta between the read and in_rel. The cursor then lands at tx plus that stray delta. This is practically
harmless: vm_apply runs only from present_and_pump.c:123 on the main thread, and PS/2 motion is normally silent while vmmouse is
active. The exact fix moves the read-modify-write under the lock:
drivers/input/input.c, after line 10:
```
+void in_abs_to(int tx,int ty){ extern volatile int mx,my; u64 f=in_lock(); int dx=tx-(mx+mouse_dx), dy=ty-(my-mouse_dy); if(dx||dy){ mouse_dx+=dx; mouse_dy-=dy; mouse_evt=1; } in_unlock(f); }
```
(Use the real declaration type of mx/my; check it with `grep -rn "int mx" include graphics`.)
drivers/input/vm/vmmouse.c:52:
```
-    if(tx!=g_vm_lx||ty!=g_vm_ly){ int cx=mx+mouse_dx, cy=my-mouse_dy; in_rel(tx-cx,ty-cy); g_vm_lx=tx; g_vm_ly=ty; g_vmm_events++; }
+    if(tx!=g_vm_lx||ty!=g_vm_ly){ extern void in_abs_to(int,int); in_abs_to(tx,ty); g_vm_lx=tx; g_vm_ly=ty; g_vmm_events++; }
```

### L3 — docs are stale about the default
ARCHITECTURE.md:51 and docs/architecture-src/00-whatsnew.md:22 still say "Kill switch: `usbproto off`". Patch (both files):
```
-... no more class requests after a timeout. Kill switch: `usbproto off`.
+... no more class requests after a timeout. Off by default since v11.07; `usbproto on` enables it (`usbproto off` also stops enumeration-time SET_PROTOCOL/SET_IDLE re-sends).
```

### L4 — the `usbproto off` message understates what it does (pre-existing, shell/klog)
`usbproto off` sets g_usbproto_off=1 as well. That disables the enumeration-time SET_IDLE for report-protocol interfaces and the
SET_PROTOCOL re-send on the next (re)enumeration, but the klog text says only "late ... retries off". This behaviour predates c1, and c1 notes it in the report.
Optional patch, controller_init.c:112: change the off string to `"usb: SET_PROTOCOL/SET_IDLE retries off (late + enumeration)"`.

## Notes for the lead (not defects)
- c1's changes do not fix the Start/taskbar freeze by themselves. They remove one candidate cause: blocking xh_ctrl
  from xhid_proto_poll in the main loop. The nested-loop and heartbeat causes belong to c2's work.
- Known limitation that remains: a wide receiver whose mouse ID is 2 still loses a still right press `02 00 00`
  (documented in v1105 m1-report). The c1 fix covers only mouse-ID-1 receivers. The owner's Biltema has 8-bit axes, so it is unaffected either way.
