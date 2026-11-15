# sweep3 report - graphics/, shell/, apps/, usr/ - READ-ONLY

## HIGH
H1. apps/media/recorder.c:62 - recorder_draw() calls snd_rec_start() on EVERY frame while the mic is not running.
    snd_rec_start (core/sound.c:44) first calls rec_gain (USB SET_CUR control transfer) then ausnd_rec_start ->
    aud_setup_ep (drivers/usb/xhci/uvc_webcam.c:206/231: Configure Endpoint + SET_INTERFACE). If the UAC mic
    (0BDA:0005) refuses/times out, g_cap_on stays 0 and the next frame does it again. With v11.01's EP0 timeout
    recovery (Stop EP + Set TR Dequeue, each a blocking xh_cmd_s wait) every frame becomes N x timeout long ->
    desktop looks frozen, HID endpoints are not serviced from the UI loop. Start > Drivers & Hardware > Voice Recorder.
    Fix: start capture once on open (latched flag like cam_auto, reset on close); on failure show "Microphone did
    not start - Retry" button, never retry from the draw path.
H2. apps/media/camera.c:61 - camera_draw() calls uvc_start() synchronously from the compositor draw path on the
    first frame. uvc_start (drivers/usb/xhci/uvc_webcam.c:34-61) tries up to 2 passes x 16 sizes = 32 attempts, each
    3 EP0 control transfers (probe SET/GET, commit) + 2 Configure Endpoint commands. On the owner's machine every
    attempt fails with cc 17 ("USB controller refused the stream (code 17)" = Parameter Error), so all 32 run:
    ~96 control transfers + 64 commands inside one frame. Any EP0 timeout now adds Stop EP + Set TR Deq waits.
    Result: multi-second-to-minute freeze right after clicking Start > Camera; input not pumped meanwhile.
    camera_closed() resets cam_auto=1, so every reopen repeats it. Also camera_click/camera_key "HD" toggle call
    uvc_start from input path (same cost).
    Fix: never call uvc_start from draw; start from camera_open()/a button, cap attempts (stop after the first
    cc 17 / cc 8 config failure since a Parameter Error will repeat for every size), and pump input (or yield)
    between attempts. Driver side: decode why Configure Endpoint gets cc 17 (input context fields).

Context for H1/H2 (verified): compose_build (graphics/ui/desktop/desktop_clicks.c:263-281) draws every open,
non-minimized window each frame via draw_app -> apps/system/logs.c:96 -> camera_draw/recorder_draw. The Start
menu click itself is cheap: launcher_click/classic_menu_click -> lapp_open (desktop_clicks.c:95) -> open_win
(graphics/ui/wm.c:51), which only sets flags (win_anim is a no-op, present_and_pump.c:154). So "click freezes"
= the FIRST FRAME after the click runs the new app's draw, and for Camera/Voice Recorder that draw does
blocking USB work. Nothing in the USB wait paths calls ui_pump/busy_pump (only Wi-Fi/net do), so while
uvc_start / aud_setup_ep wait, the mouse and keyboard endpoints are not polled from the UI loop and the
pointer + keyboard look dead. The main-loop heartbeat (g_ml_beat) also stalls.
Items in Start > Drivers & Hardware (desktop_clicks.c:82 LG_BASE[7]): Gears(W_GPU3D), Camera, Voice Recorder,
Benchmark, Task Manager, Settings. Task Manager draw (apps/system/taskmgr.c:301) and Settings draw: no blocking
device I/O found (sample() is rate-limited to 1/s, reads counters only). Settings > Sound page only reads
snd_rec_avail/name (snd_rec_avail may call snd_init once -> hda/ac97 probe, one-shot).
Test to discriminate (QEMU or owner): open Task Manager from the same submenu - if it does not freeze but
Camera/Voice Recorder do, H1/H2 are the UI half of the bug; time how long the Camera freeze lasts
(32 attempts x per-attempt cost).

## MEDIUM
M1. apps/media/camera.c:91-92 (camera_click k==1 / k==4) and :95 (camera_key 'h') - HD toggle / Switch camera
    call uvc_start (up to 32 attempts, see H2) synchronously from the input path. Same freeze, triggered by a
    button or the H key. Fix: same as H2 (async/limited start).
M2. apps/office/editor.c:57-58 + :73-76 - editor_open silently truncates: lines > 199 chars are cut, rows past
    199 are dropped (ED_ROWS=ED_COLS=200, :23-24). editor_save then writes the truncated buffer back over the
    file -> silent data loss for any text file > 199 lines or with long lines (logs, HTML, JSON, CSV).
    Fix: refuse to save (or open read-only with a toast) when the load was truncated; keep an ed_trunc flag.
    Also editor_open keeps '\r' and control bytes (editor_open_buf strips them) - CRLF files show stray chars.

## LOW
L1. graphics/ui/desktop/desktop_clicks.c:136 - classic submenu hit: r=(py-sy-3)/CM_IH; for py in [sy,sy+3)
    the numerator is negative, C truncates to 0, so a click on the top border launches item 0. Fix: test
    py>=sy+3 before dividing.
L2. shell/commands/usbhid.c:46 - usbhid_show ends with xhc_select(0) instead of restoring the previously
    selected controller (save g_xcur first, as uvc_webcam.c does). With 2 xHCI controllers a later call that
    assumes the current selection can hit the wrong controller.
L3. apps/office/editor.c:30/47 - ed_src is ED_ROWS*40 = 8000 bytes; editor_run silently truncates programs
    larger than that (bounded, no overflow).
L4. graphics/ui/desktop/desktop_clicks.c:34 - desk_hit returns -1 unconditionally (dead code after return);
    desktop icon double-click/drag paths in input.c:156,214 are therefore dead. Intentional? Harmless.

## Checked, no bug found
- Start menu (modern + classic) draw/click geometry, LGRP bounds (LG_MAX+1 sentinel), launcher search buffer
  (g_lqn<38 of 40), alt-tab list size (NWIN+32 matches max fill), rename buffer (63 of 64), fx_rename_commit,
  open_win/close_win/focus_win (no loops), win_anim (disabled), present/blit clipping (blit_region2 clips),
  cam_view scaling (guards uvc_w/h>0), recorder draw arithmetic (guards rc_gw>0), process_input event queue
  drain (bounded by IN_MQ_N), mouse_step button edge logic, usbhid_live (deliberate 15 s modal, polls HID and
  kpop each spin, exits on key).

## Coverage (honest)
Fully read: graphics/ui/desktop/desktop_clicks.c, graphics/ui/desktop/present_and_pump.c, graphics/ui/wm.c,
  graphics/ui/desktop.c, apps/media/camera.c, apps/media/recorder.c, shell/commands/usbhid.c;
  outside quarter for context: drivers/usb/xhci/uvc_webcam.c:1-80, core/sound.c:18-50.
Partly read: graphics/ui/input.c (lines 60-293 of 453: click dispatch, mouse_step, process_input),
  apps/system/taskmgr.c (sample, devices, sysmon_draw/click), apps/system/bench.c (bench_draw/click),
  apps/office/editor.c (open/save/run), apps/system/settings/storage_and_layout.c (Sound page),
  apps/system/file_explorer/trash_and_sort.c (rename commit), apps/graphics/gpu3d.cpp (init path via grep).
Skimmed by grep only: settings/pages.c, rest of input.c.
Skipped (time): graphics/ui/desktop/app_tiles_and_icons.c, window_titles_and_bars.c (only icon cases grepped),
  graphics/theme/*, hotkeys.c, all other shell/ files, apps/internet/browser/*, apps/x11/*, file_explorer
  (except rename), writer.c, videoplayer.c, small_apps.c, services.c, regedit.c, logs.c (only dispatch line),
  and all of usr/ (libc, musl/glibc tests, x11 demos).
