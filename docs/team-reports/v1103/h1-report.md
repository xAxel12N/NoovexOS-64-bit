# H1 report — hypothesis "first real draw / one-time device start is deferred to drag end"

VERDICT: KILLED (confidence 85%). No app's first draw or one-time device start is moved to the drag-release frame.

## Why
1. CPU drawing (g_gfx==0): compose_build calls draw_app() for every open window every frame, drag or not
   (graphics/ui/desktop/desktop_clicks.c:271). Nothing is skipped during drag.
2. GPU drawing: gfx_window (drivers/video/fb/gfx.c:239-243) skips draw_app only when
   `moving = drag>=0 && !live_app(i) && L->valid && same w/h`. L->valid is only set after a real draw, so a
   window must already have been drawn once before it can be "cached". live_app (gfx.c:190) = GPU3D, QUAKE,
   CAMERA, BENCH, RECORDER, VIDEO, DOOM, SNAKE, FLAPPY, CLOCK, SYSMON(task manager), XDISP: these draw EVERY
   frame during drag too. So camera_draw (camera.c:48, uvc_start on 2nd draw), recorder_draw (recorder.c:63,
   snd_rec_start once), gpu3d ensure_init (gpu3d.cpp:137, r3d_init on first draw), bench (only starts on click,
   bench.c:180) all run before/while dragging, not at release.
3. desk_win_exposed (present_and_pump.c:1) is used by only 4 fast paths: video_fast, browser_media_fast,
   xdisp_fast (main.c:354) and gpu3d fast (gpu3d.cpp:407). All just return/skip while dragging; none does
   init. At release they resume normal per-frame work. Only notable one: browser_media_fast calls
   web_call(NWE_OP_MEDIA) on the first exposed frame — only when Browser is on a loaded page (g_page==3) with
   no web_busy; same call it does every frame otherwise, not a one-time start.
4. Non-live apps (Settings, Files, Logs, Regedit, etc.) get one full redraw at release (position changed, and
   size too if apply_snap ran, input.c:197). Their draws were already executed at open; settings_draw only does
   pci_scan() if !pci_n, gmetrics() once, wall_thumbs_build(), rtw_find_mmio() — all already done at open.
   No Wi-Fi scan starts from a draw (Scan is a button).
5. No code keys off a drag->no-drag transition other than input.c:197 (apply_snap, drag=-1). grep for
   `drag>=0` in apps/drivers/sys/core finds only gfx.c:243.

## Side findings (not the freeze, low confidence as cause)
- camera.c:44-48: uvc_start() runs INSIDE compose_build / gfx layer capture (g_rt=layer, gq_cap_begin).
  uvc_start is bounded (uvc_webcam.c: 2 passes x <=16 formats, each with timed control transfers) but can
  take seconds on a failing camera (0C45:1915 code 17); during it the frame is held and input is only
  absorbed by xHCI waits. Not tied to drag end. Fix if wanted: move the auto-start out of camera_draw into a
  tick (e.g. media_tick) that runs outside compose (g_comp_depth==0).
- recorder.c:63 snd_rec_start inside draw likewise; snd_rec_start (core/sound.c:44) is a single call, bounded
  by driver.

## Suggestion to lead
Look elsewhere for the release-moment freeze: apply_snap/resize path (wm.c:62) followed by a full-size
re-layout + layer realloc for a live app, the full redraw of a non-live window (gfx layer `full` replay,
gq_fill / gq_replay with new cx0..cx1), or the input release path itself (input.c:185-197, g_fdrag drop
handling, browser_drop_file). The draw-deferral theory does not explain it.
