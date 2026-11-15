# r5 review: c5 (camera code 17, USB camera/mic lifecycle)

Branch x-c5 (c1400bb3), merged into main as v11.07. I read the diff, c5-report.md and ctasks/c5.md, and checked the merged files in main (the merge only touches these files through c1's btu_* lines in xhub_remove_below and controller_init.c; they don't conflict).

Verdict: no HIGH. The code 17 fix is correct as far as it goes. The lifecycle changes are sound. Two MEDIUMs: an audio priority choice that hurts the owner's real setup, and the CSZ fix not being confirmed as the actual root cause.

## What I verified as correct
- Code 17: `XHID_BASE(r)` is `DMW(0x80000)+r*0x8000`, and `g_devbuf` is global, not per controller. So the only thing in `uvc_cfg_ep_in`/`aud_cfg_ep_in` that depends on the selected controller is `ctx_sz()` (`xhci_csz`, which `xhc_load` sets per controller). `usb_hotplug_poll()` (storage:159) leaves `g_xcur` at the LAST controller. So when the camera app starts a stream, a camera on controller 0 had its input context built with controller 1's CSZ. That gives a 32/64-byte layout mismatch: the endpoint context gets read at the wrong offset, EP type 0, Parameter Error 17. The wrapper now selects `cam.ctl`/`aud.ctl` before `ctx_zero`/`ctx_sz`, which fixes it. `xhc_select` is a no-op when the controller is already selected, so nesting is safe.
- Unplug: `uvc_aud_remove_below` runs first in `xhub_remove_below` (storage:29), while the HID entries still exist. So `uvc_slot_hid` correctly leaves a headset-with-buttons slot to the HID loop. Camera plus camera mic share one slot (cs==as), and it is disabled once. Root-port unplug goes through `xhid_remove_port`→(p,0,0), and `xh_under(...,0,0)` matches everything under that root. `cam.route/depth` use the same convention as `xhid_route/depth` (L->route/L->depth).
- Disabled-slot reuse: `uvc_stop` checks `cam.ok` first, `ausnd_rec_stop` checks `cpon` (cleared by `aud_gone`), and `uvc_req`/`aud_req` check `.ok`. A later device that gets the same slot id and region is never sent a stale Configure Endpoint. `uvc_attach` resets `cam.epon`.
- Rejected second device: `uvc_attach` returns before touching `cam.desc`/region, then XFAIL("camera setup") disables the slot (slot_ok=1 at that point). An audio-only device that is rejected goes to XFAIL("no keyboard or mouse interface") and its slot is disabled. Neither hub nor root re-enumerates on failure (conn/usb_seen are already set), so no retry storm.
- Event rings: `uvc_er_init`/`aud_er_init` are only called under `xhc_select(cam.ctl/aud.ctl)` (uvc_webcam.c:53, :223), so storing `g_xcur+1` is consistent.

## MEDIUM 1: the first audio device wins, so a camera mic can lock out a dedicated mic or headset
drivers/usb/xhci/uvc_webcam.c:248 (aud_attach) and :278 (uvc_add_mic).
The owner has both a camera with a mic (0C45:1915) and a USB mic (0BDA:0005). Whichever enumerates first (port order at boot) now owns `aud`. If the camera enumerates first, the USB mic and any headset plugged in later are silently rejected (klog only). Before c5, the later device replaced the camera mic (buggy, but the headset won). A dedicated audio device should beat a camera's built-in mic. The camera mic shares the camera's slot and region, so dropping it does not need a slot disable.
Patch (uvc_webcam.c:248, replace the guard):
```c
    (void)vid; (void)pid;
    if(aud.ok){
        if(aud.incam&&!aud_incam_next){ klog("usb audio: a headset/microphone replaces the camera microphone"); ausnd_rec_stop(); aud.ok=0; aud.pbon=0; aud.cpon=0; aud_state=0; aud_reg_hold=-1; { extern void snd_usb_detach(void); snd_usb_detach(); } }
        else { klog("usb audio: another audio device is already in use, this one is ignored"); return 0; } }
```
(`ausnd_rec_stop` is defined above at :244. `uvc_reg_hold` still protects the camera's region, and `uvc_aud_share` stops sharing because `aud.reg` changes.)

## MEDIUM 2: the CSZ fix may not be what is causing the owner's code 17. Add a log line to confirm it
drivers/usb/xhci/uvc_webcam.c:56. The fix only changes behaviour when the owner's two xHCI controllers report DIFFERENT HCCPARAMS1.CSZ values and the camera is not on the last controller. Intel PCH xHCI usually has CSZ=0. A Renesas or ASMedia add-on controller may have CSZ=1, but nobody has checked this on the owner's machine. c5 says so honestly. It is not a false fix, because the bug is real, but it needs a log line so the next owner report settles it.
Patch (inside the `!uvc_cfg_ep(1,a)` branch, after `klog(b);`):
```c
            { char d[96],*z=d; z=scopy(z,"camera: ctl="); z=udec(z,(u32)cam.ctl); z=scopy(z," csz="); z=udec(z,(u32)g_xhc[cam.ctl].csz); z=scopy(z," cur="); z=udec(z,(u32)g_xcur); z=scopy(z," dci="); z=udec(z,(u32)((a->ep&0x0F)*2+1)); z=scopy(z," mps="); z=udec(z,(u32)a->mps); z=scopy(z," intv="); z=udec(z,(u32)a->intv); *z=0; klog(d); }
```
(`g_xhc` is defined in storage_fat_and_hubs.c, which is included before uvc_webcam.c in the xhci.c unit. If not, use `xhci_csz` after `xhc_select(cam.ctl)`.)

## LOW 1: a rejected second camera still shows up as "Camera connected"
controller_init.c:405 calls `usbdev_note` (with notify) before `uvc_attach`. The second camera is announced and listed under Devices, but the only sign that it was rejected is a klog line. Patch: storage_fat_and_hubs.c:222, replace `klog(...)` with `{ klog("camera: a camera is already in use, the second one is ignored"); extern void toast(const char*); toast("Only one camera at a time - unplug the first camera to use this one"); }`.

## LOW 2: the tests don't cover the new code
tools/tests/usb_template.txt:14 stubs `uvc_aud_remove_below` as an empty function. So "12/12 usb checks pass" only proves that the hub simulation still links. Camera/mic removal, the shared-slot single disable and the HID-owned skip are untested. Same for the CSZ selection. I'd suggest a small generated test: two fake controllers with csz 0/1, `cam.ctl=0`, `g_xcur=1`, then check that the endpoint context offset is 64*(dci+1).

## LOW 3: stale interrupter on the old controller (acknowledged by c5)
When the camera moves from controller A to B, A's interrupter 1/2 still has ERSTBA pointing at `uvc_er`/`aud_er`, which B now uses. Nothing on A targets interrupter 1/2 after the slot is disabled, so the risk is low. A clean fix is to write `XMW(rt+0x10,0)` (ERSTBA) on the old controller in `uvc_er_init`/`aud_er_init` before reprogramming, when `uvc_er_done` is not 0 and not equal to `g_xcur+1`.

## Skipped items (c5's own list)
Finding 5 (the EP0 buffer overlap) and `usbdev_gone` for hub children were reasonable to skip, because both are c1's files or need a field that doesn't exist yet. Hub-child devices still stay listed under Devices after unplug, which predates c5.
