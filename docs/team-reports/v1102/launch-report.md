# launch report (c-launch)

## Path traced
Start (classic) click -> classic_menu_click (graphics/ui/desktop/desktop_clicks.c:135) -> lapp_open (:95) -> open_win (graphics/ui/wm.c:51).
open_win only sets window state (snake/flappy/calendar reset, focus_app_started); no device work on the click path, no grab, no modal loop.
App init happens lazily in the per-frame draw dispatcher (apps/system/logs.c:96). Modern launcher (launcher_click/launcher_open_result) uses the same open_win.

## Findings
1. HIGH, conf high: apps/media/recorder.c:62 (recorder_draw) called snd_rec_start() EVERY frame while the mic was present but g_cap_on==0.
   If ausnd_rec_start fails (aud_setup_ep: SET_INTERFACE control xfer + Configure Endpoint, drivers/usb/xhci/uvc_webcam.c:206), it is retried each frame,
   so with v11.01's longer timeout recovery (stop EP + set TR deq per timeout) the UI loop spends almost all time in USB control waits = frozen desktop.
   Fixed: one auto attempt per open (rc_tried), error text in the window, manual retry via red button.
2. HIGH, conf medium-high: apps/media/camera.c camera_draw auto-called uvc_start() in the FIRST draw (before the window was ever shown).
   uvc_start (uvc_webcam.c:16) loops up to 2 passes x 16 frame sizes; each try = probe SET_CUR + GET_CUR + commit SET_CUR + Configure Endpoint.
   On the owner's camera every try fails (code 17), so ~32 iterations of control transfers, each possibly hitting the new timeout recovery -> many seconds or longer of a frozen UI. Matches the photo (Drivers & Hardware > Camera).
   Fixed in app: window paints once ("Starting camera...") before the single auto-start; failed start is latched (no auto retry until reopen or Start); re-entry guard cam_busy around all uvc_start calls.
   NOT fixed (xhci owners): uvc_start itself is unbounded in time. Recommend: abort the size loop after the first cfg_ep failure with the same cc (17 will not change per size), and/or cap total time (e.g. 2 s via hr_ns) and per-request timeouts.
3. LOW: Benchmark, Task Manager (sysmon_draw), Gears (gpu3d.cpp), Settings draw: no synchronous device opens found on open; Wi-Fi scan only on button press.
4. No pointer/keyboard grab on menu close found; no double-launch issue (open_win is idempotent).

## Changed
apps/media/camera.c, apps/media/recorder.c (commit on c-launch). Compile-checked with ccheck.sh (only pre-existing style warnings). Not run in QEMU (time).
