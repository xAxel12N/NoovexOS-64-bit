# c5 report: camera "code 17" and USB camera/mic lifecycle

Branch x-c5, commit c1400bb3. Worktree /home/claude/wt/c5. I checked every h5 patch against the real code before applying it.

## Changes
1. HIGH, code 17 (h5 finding 1)
   - drivers/usb/xhci/storage_fat_and_hubs.c:242. `uvc_cfg_ep` is now a wrapper. It selects `cam.ctl` first, then calls `uvc_cfg_ep_in`, which builds the input context, then restores the previous controller. The old select/restore around `xh_cmd_s` is gone, so `ctx_sz()` and the DCBAA/command ring now all come from the camera's controller.
   - drivers/usb/xhci/uvc_webcam.c:~159. `aud_cfg_ep` got the same change, using `aud.ctl`.
2. MEDIUM, unplug behind a hub, and slot leak (finding 2)
   - `cam` and `aud` now store `route` and `depth`. They are set in `uvc_attach` (storage:222) and `aud_attach` (uvc_webcam:251).
   - I added the new fields at the END of the int lists, so the test generator's struct-prefix match still works.
   - New `uvc_aud_remove_below` at uvc_webcam.c:271. It removes the camera and/or mic under (root, R, dR) on the current controller, then disables their slots. Camera plus its own mic share one slot, which is disabled once.
   - A slot that a HID entry still owns (a headset with buttons) is not disabled here. The HID loop in `xhub_remove_below` disables it, so no slot is disabled twice.
   - The new function is called first in `xhub_remove_below` (storage:26-28). That covers hub-port unplug and root-port unplug (through `xhid_remove_port`). The later `uvc_unplug`/`aud_unplug` calls then do nothing.
3. MEDIUM, a second audio device replaced the first (finding 3)
   - The guard is inside `aud_attach` (uvc_webcam:248) and `uvc_add_mic` (:278): if `aud.ok` is already set, the new device is rejected with a klog line. The first device keeps audio.
   - This way controller_init.c did not need to change. An audio-only device that is rejected falls into XFAIL ("no keyboard or mouse interface"), and XFAIL disables its slot cleanly.
   - Extra: a second camera is rejected the same way (storage:221, `if(cam.ok) return 0`), so XFAIL disables its slot. Before, it orphaned the first camera's slot and region.
4. MEDIUM, camera/audio event rings were set up only once (finding 4)
   - `uvc_er_done` and `aud_er_done` now store controller+1 (uvc_webcam.c:3,5,155). Moving the device to the other controller now programs ERSTBA/ERSTSZ on that controller's interrupter 1 or 2.
5. tools/tests/usb_template.txt:14. I added a stub for `uvc_aud_remove_below`, which the USB test needed after the change to `xhub_remove_below`.

## Skipped
- Finding 5 (the EP0 buffer overlapping the HID buffers). It needs 4 changes, including controller_init.c and hid code, which belong to c1.
- `usbdev_gone` for hub children (LOW). `usbdev_t` has no route field.
- When the camera moves to the other controller, the old controller's interrupter 1 still points at the shared ring memory. I left it alone, because nothing on that controller targets interrupter 1 any more.
- apps/media/camera.c did not need changes.

## Tests
- /home/claude/ccheck.sh drivers/usb/xhci.c: compiles. Only the existing misleading-indentation warnings remain.
- python3 tools/check.py --only usb: 12 of 12 checks pass. That includes the camera/mic parser test and the hot-unplug/hubs simulation.
- Not tested on real hardware or in QEMU. Whether the CSZ fix is the actual cause of code 17 depends on the second controller's CSZ value.

## Fix round (branch y-c5), applying r5-review.md
- MEDIUM 1, uvc_webcam.c `aud_attach`: if the device in use is the camera's built-in mic (`aud.incam`) and the new device is a dedicated mic or headset, the new device now replaces it. The code stops recording, detaches sound and releases `aud_reg_hold`; the camera's slot and region stay, because `uvc_reg_hold` protects them. In every other case the first device still wins. Caveat: if the new device then fails inside `aud_attach` (no 16-bit PCM stream, or too few interrupters), the camera mic is already gone.
- MEDIUM 2, logging:
  - storage_fat_and_hubs.c `xhci_init` now logs one line per controller at init: `usb: xHCI <k> vendor <id> ready, context size 32|64 bytes`. The owner's `log` then shows whether the two controllers differ.
  - uvc_webcam.c, after a failed stream setup: logs `camera: ctl=<c> of <n> ctx=<sizes> cur=<g_xcur> dci=.. mps=.. intv=..`.
- LOW 1: a rejected second camera now also shows a toast ("Only one camera at a time ...").
- LOW 3: when the camera or audio moves to another controller, `uvc_er_init`/`aud_er_init` first select the old controller and write IMAN=0 and ERSTSZ=0 on its interrupter 1 or 2, which disables that event ring. ERSTBA is not touched, because it must not be written while the controller is running.
- LOW 2 (tests for the new removal and CSZ paths): not done in this round.
- Tests: ccheck drivers/usb/xhci.c compiles with no errors. `python3 tools/check.py --only usb`: 12 of 12 pass.
