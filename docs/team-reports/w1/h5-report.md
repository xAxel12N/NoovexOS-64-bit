# h5 report: drivers/usb (xHCI core, HID, hubs, storage, camera/audio, usb-net), v11.06

I only read the code. All line numbers are from tag v11.06. Nothing here is listed as fixed in docs/handoff-v11.0*.md.

## 1. HIGH (likely cause of camera code 17): camera and audio Configure Endpoint lay out the input context with the WRONG controller's context size
Files: drivers/usb/xhci/storage_fat_and_hubs.c:238-251 (`uvc_cfg_ep`) and drivers/usb/xhci/uvc_webcam.c:159-170 (`aud_cfg_ep`).

Both functions call `ctx_sz()` (that is, `xhci_csz`, the HCCPARAMS1.CSZ value for each controller) for `ctx_zero`, for the slot context offset `ic0+ctx_sz()` and for the endpoint offset `ic0+ctx_sz()*(dci+1)`. Only after that, just before the command, do they call `xhc_select(cam.ctl)`. When they are called (from the UI through `uvc_start`/`uvc_stop`, or from `ausnd_*`), `g_xcur` is nearly always the LAST controller, because `xhci_mouse_poll_in` and `usb_hotplug_poll` both end their `for(c...) xhc_select(c)` loops on `g_xhc_n-1`. `xhc_save`/`xhc_load` swap `xhci_csz` per controller.

How it fails: the owner has 2 xHCI controllers. If the camera is on controller 0 and the two controllers use different context sizes (Intel PCH uses 32-byte contexts; AMD, Renesas and some others use 64-byte), the input context is built at 32-byte offsets but read at 64-byte offsets, or the other way round. The controller then sees a zero or garbage slot context (Context Entries=0, speed=0) and garbage endpoint fields. That gives Parameter Error **17** on EVERY alternate setting, which is exactly the symptom in the v11.01/v11.02 handoffs ("all attempts fail with code 17"). The USB mic 0BDA:0005 (`aud_cfg_ep`) would fail the same way ("USB controller refused the audio stream"). If both controllers happen to use the same context size, the bug is latent.
Severity HIGH. Confidence: 95% that the bug is real, about 45% that it is THE cause of code 17 (that depends on the second controller's CSZ; `usbhid`/`camera` info could print `xhci_csz` per controller to confirm).

Patch (storage_fat_and_hubs.c):
```
old: static int uvc_cfg_ep(int add,const uvc_alt_t* a){
new: static int uvc_cfg_ep_in(int add,const uvc_alt_t* a);
     static int uvc_cfg_ep(int add,const uvc_alt_t* a){ int sv=g_xcur; xhc_select(cam.ctl); int r=uvc_cfg_ep_in(add,a); xhc_select(sv); return r; }
     static int uvc_cfg_ep_in(int add,const uvc_alt_t* a){
old (line 251): u8 cc=0,sl=0; int sv=g_xcur; xhc_select(cam.ctl); xh_cmd_s(ic0,0,0,12,(u32)cam.slot,&cc,&sl); xhc_select(sv);
new:            u8 cc=0,sl=0; xh_cmd_s(ic0,0,0,12,(u32)cam.slot,&cc,&sl);
```
Patch (uvc_webcam.c):
```
old: static int aud_cfg_ep(aud_ep_t* e,const uac_alt_t* a,int add){
new: static int aud_cfg_ep_in(aud_ep_t* e,const uac_alt_t* a,int add);
     static int aud_cfg_ep(aud_ep_t* e,const uac_alt_t* a,int add){ int sv=g_xcur; xhc_select(aud.ctl); int r=aud_cfg_ep_in(e,a,add); xhc_select(sv); return r; }
     static int aud_cfg_ep_in(aud_ep_t* e,const uac_alt_t* a,int add){
old (line 170): u8 cc=0,sl=0; int sv=g_xcur; xhc_select(aud.ctl); xh_cmd_s(ic0,0,0,12,(u32)aud.slot,&cc,&sl); xhc_select(sv);
new:            u8 cc=0,sl=0; xh_cmd_s(ic0,0,0,12,(u32)aud.slot,&cc,&sl);
```
(I checked every other `ctx_sz()` user: msd, usb-net, HID enumeration, the HID watchdog and recovery all run with the right controller selected.)

### Contexts for a high-speed device behind a USB 2.0 hub (what the lead asked to check): no spec error found
- Child location, storage_fat_and_hubs.c:42-44: route = hub route | port<<(4*depth), so a hub on a root port gives route=port. Root port = hub root + 1. speed=3 (hub port status bit 10, :41). TT slot/port are inherited from the hub, which is 0 for a high-speed hub on a root port. That is correct: TT fields are only for FS/LS devices behind a HS hub. MTT=0 is consistent, because the hub is never switched to its multi-TT alt setting.
- Hub slot, :69: Hub=1, NumPorts, TTT for HS. Correct.
- Camera isochronous endpoint, :244-249: type 5, CErr 0, HS Max Burst = (wMaxPacketSize>>11)&3, Mult=0 for non-SS, interval bInterval-1 (HS) or bInterval+2 (FS), Max ESIT = maxp*(burst+1), average TRB length = ESIT, slot context copied from the output context with dword3 cleared. This is the same as Linux (xhci_endpoint_init). So the contexts are correct by themselves. Their offsets are wrong (finding 1).

## 2. MEDIUM: camera and mic behind a hub are never removed on unplug, and their slots are never disabled (slot leak and device-context aliasing)
Files: storage_fat_and_hubs.c:26-33 (`xhub_remove_below`) and :120, uvc_webcam.c:100, :266.
- When a device is unplugged from a hub port (:88), `xhub_remove_below` cleans up HID, hubs, msd and usb-net, but not `cam`/`aud`. `uvc_unplug`/`aud_unplug` are only called for ROOT ports and compare `root==port`. Result: after the camera or mic is pulled from the hub, `cam.ok`/`aud.ok` stay 1. Sound stays routed to a dead headset (`snd_usb_detach` is never called, so there is silence), the Camera app tries to drive a gone device, and `uvc_reg_hold`/`aud_reg_hold` keep a region forever.
- Even at a root port, `uvc_unplug`/`aud_unplug` never call `xh_disable_slot`. Each camera or mic replug leaks one xHC slot (Enable Slot eventually fails with No Slots). The region is freed (`uvc_reg_hold=-1`) while the old slot's DCBAA entry still points at `XHID_DEVCTX(d)`. The next device to take region d (the lowest free region, so very likely) shares its output device context with a live slot. The HID watchdog (hid_keyboard_mouse.c:136-138) reads EP state from that shared memory and can start recovery loops.
Confidence 90%. Patch:
```
storage_fat_and_hubs.c:160  old: static struct { int ok, ctl, reg, slot, speed, root, vc_if,
                            new: static struct { int ok, ctl, reg, slot, speed, root, depth, vc_if,
                     same line old: u32 e0_enq, e0_cyc; uvc_alt_t alt[24];
                               new: u32 e0_enq, e0_cyc, route; uvc_alt_t alt[24];
storage_fat_and_hubs.c:219  append after "cam.dci=0;":  cam.route=L->route; cam.depth=L->depth;
uvc_webcam.c:102            old: static struct { int ok, ctl, reg, slot, speed, root, ac_if,
                            new: static struct { int ok, ctl, reg, slot, speed, root, depth, ac_if,
                     same line old: u32 e0_enq, e0_cyc; uac_alt_t alt[16];
                               new: u32 e0_enq, e0_cyc, route; uac_alt_t alt[16];
uvc_webcam.c:248            append: aud.route=L->route; aud.depth=L->depth;
uvc_webcam.c after line 267 add:
static void uvc_aud_remove_below(int root,u32 R,int dR){ int cs=-1, as=-1;
    if(cam.ok&&cam.ctl==g_xcur&&cam.root==root&&xh_under(cam.route,cam.depth,R,dR)){ cs=cam.slot; uvc_gone(); uvc_reg_hold=-1; }
    if(aud.ok&&aud.ctl==g_xcur&&aud.root==root&&xh_under(aud.route,aud.depth,R,dR)){ as=aud.slot; aud_gone(); }
    if(cs>0)xh_disable_slot(cs); if(as>0&&as!=cs)xh_disable_slot(as); }
controller_init.c:149       append: static void uvc_aud_remove_below(int root,u32 R,int dR);
storage_fat_and_hubs.c:32   old: usn_remove_below(root,R,dR);
                            new: usn_remove_below(root,R,dR); uvc_aud_remove_below(root,R,dR);
```
The root-port path calls `xhid_remove_port(p)`, which is `xhub_remove_below(p,0,0)`, so it is covered too. The later `uvc_unplug`/`aud_unplug` calls then do nothing.
Related LOW: `usbdev_gone` is also only per root port, so hub children stay in the device list. There is no "removed" toast, and on replug the "connected" toast is suppressed (`fresh=0`). No small patch, because `usbdev_t` has no route.

## 3. MEDIUM: a second audio device silently replaces the first (the owner has camera-with-mic + mic 0BDA:0005)
controller_init.c:410 and uvc_webcam.c:269-271: there is only one `aud` instance. If the mic enumerates first and then the camera, `uvc_add_mic`→`aud_attach` zeroes `aud` and sets `aud_reg_hold` to the camera's region. The 0BDA:0005 mic is orphaned: its slot stays enabled and its region becomes "free", which causes the same output-context aliasing as finding 2 when the next receiver or hub child is enumerated. The order is the port order at boot, so whether it happens depends on which ports are used. Confidence 85%. Patch (first device keeps audio):
```
controller_init.c:148 append: static int aud_inuse(void);
uvc_webcam.c after line 267 add: static int aud_inuse(void){ return aud.ok; }
controller_init.c:410  old: if(aud_has_audio(full,wtot)){
                       new: if(!aud_inuse()&&aud_has_audio(full,wtot)){
uvc_webcam.c:270       old: e0_enq=cam.e0_enq; e0_cyc=cam.e0_cyc; aud_incam_next=1;
                       new: if(aud.ok)return; e0_enq=cam.e0_enq; e0_cyc=cam.e0_cyc; aud_incam_next=1;
```
(An audio-only second device then fails with "no keyboard or mouse interface", and XFAIL disables its slot, which is clean.)

## 4. MEDIUM: camera/audio event rings are set up once globally, not per controller
uvc_webcam.c:3-7 (`uvc_er_done`) and :155 (`aud_er_done`) are single flags that only `xhid_reset` clears (a full USB restart). Scenario: the camera is used on controller 0, then moved to a port on controller 1 (the owner has 2 xHCI). `uvc_er_init` sees `uvc_er_done=1` and only writes ERDP, so interrupter 1 of controller 1 never gets ERSTBA/ERSTSZ. The isochronous TRBs target interrupter 1 (`1u<<22`, :262), and an xHC with ERSTSZ=0 on the target interrupter can raise Host Controller Error, which kills keyboard and mouse on that controller. At best the camera gets no events. The audio side is the same (interrupter 2, `2u<<22`). Confidence 60% on the HCE outcome, 90% on "camera dead after a controller change". Patch:
```
uvc_webcam.c:3  old: if(uvc_er_done){     new: if(uvc_er_done==g_xcur+1){
uvc_webcam.c:5  old: uvc_er_done=1;       new: uvc_er_done=g_xcur+1;
uvc_webcam.c:155 old: if(!aud_er_done){ aud_er_done=1;   new: if(aud_er_done!=g_xcur+1){ aud_er_done=g_xcur+1;
```
(Both are called with `xhc_select(cam.ctl/aud.ctl)` already done.)

## 5. LOW: EP0 data buffer of a HID+audio device overlaps its HID interrupt buffers
`cur_dbuf=XHID_DATABUF(d)` (EP0 data stage) is the same memory as entry d's 16 in-flight interrupt TDs (`xhid_dbuf[d]=XHID_DATABUF(d)`, 16x64 B, controller_init.c:455). After arming, `aud_req` on a headset/mic that also has a HID interface (volume/mute buttons, `aud.reg==d`) writes SET_CUR data into, and reads GET_CUR replies from, slot 0 of the live HID buffer. A button report can be corrupted, or the sample-rate readback can return report bytes (range-checked at :219, so the damage is small). Fix idea: use `XHID_DATABUF(d)+0x800` as EP0 buffer everywhere (`cur_dbuf=` in enum_loc :375, `xhid_proto_poll` :134, `aud_req` :115, `uvc_req` :173). It is a 4-site change, so I did not write it as a one-liner.

## Checked, no bug found
- SET_PROTOCOL/SET_IDLE (controller_init.c:432-437, :115-140): boot keyboards stay in boot protocol unless the report descriptor has IDs and a keyboard layout. SET_IDLE(0) gives on-change reports, and identical repeated reports are harmless in `kbd_report`. The late retry only targets interfaces with a mouse layout in report mode (`mneed`), is sent once per device, and is skipped while a button is held, inside waits, with IRQs off, or inside process_input. Save/restore of cur_slot/ep0/e0_enq is correct, so it is safe even when the poll runs between enumeration steps. Two receivers have separate regions and per-device `pused/pdead`.
- Poll versus waits: `xh_event_w_in` hands HID/usb-net events to `hid_xfer`/`usn_xfer`. Recovery is deferred while `xh_wdepth>0`, usb-net polling checks `xh_wdepth` and `xh_poll_busy`, stale transfer and command completions are filtered by TRB pointer, waits are tick-bounded even under event storms, and `hid_xfer` does not re-arm on Stopped (26/27). Recovery handles Running→Stop→Set TR Deq. The one leftover theoretical race is a user process on an AP calling syscall 51 (`kidle`→`xhci_mouse_poll`) while CPU0 is inside an enumeration wait, which does not hold `xh_poll_busy`. That only matters if the BKL does not serialize them; not verified.
- Hubs: port reset, C_PORT_RESET/C_CONNECTION clears, speed decode, depth limit 5, recursive removal (`xh_under`), and msd behind a hub (route/depth tracked) are all fine. Hub interrupt EP configured but never armed (OK).
- DMA layout: XHID regions 0x80000..0xD0000 end exactly at MSD 0xD0000; per-controller rings and scratchpad fit in 0x26 pages.
