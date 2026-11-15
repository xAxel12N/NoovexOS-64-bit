# S2 report: draw/update paths in apps/ and usr/ that can block, loop or write out of bounds (v11.03)

Scope: apps/system/logs.c:96 draw_app dispatch and every *_draw it calls, the *_fast/*_tick hooks that check desk_win_exposed, and usr/ GUI loops.
Key fact from graphics/ui/input.c:184-197: during a drag a window can move to x down to 90-w and y up to H-TITLEH, so most of it can be below the screen.
On release, apply_snap may change w/h. Then desk_win_exposed() (present_and_pump.c:2) turns from 0 to 1, which re-enables the per-app fast paths:
video_fast (videoplayer.c:255), xdisp_fast (xdisplay.c:80) and browser_media_fast (mouse_and_bookmarks.c:119).

## Ranked findings

1. **Camera: uvc_start runs inside a draw (apps/media/camera.c:47-48, cam_try_start at :44).** Confidence 30%.
   The camera is started on the 2nd camera_draw after it opens (cam_auto 1->2->0). That is a blocking USB device bring-up (xHCI control and isoc setup) running in the compositor frame.
   If the redraws are not frame-by-frame while dragging, the "2nd draw" can land on the redraw after the drag ends. That is the same class of bug as v11.01 (freeze on clicking Camera).
   uvc_poll() and cam_decode() also run on every draw. cam_decode does a full JPEG decode up to 1920x1200 on every draw: slow, but it ends.
   Fix: never start a device from a draw. Move the auto-start into a deferred tick that runs only when drag<0 && g_resize<0, run it once, and give it a time limit:
   `if(cam_auto==2 && drag<0 && g_resize<0){ cam_auto=0; cam_try_start(); }` in a camera_tick() called from the main loop, and remove the start from camera_draw.

2. **Voice Recorder: snd_rec_start runs inside a draw (apps/media/recorder.c:63).** Confidence 20%.
   recorder_draw opens the USB mic (0BDA:0005) on its first draw (rc_tried guard): a blocking device open inside the frame.
   Same reasoning as finding 1. rc_pump (recorder.c:24) reads up to 32x8 KB per tick. That is bounded.
   Fix: move it into recorder_tick, gated on drag<0, and run it once.

3. **Files "This PC" view: vol_scan(0) runs on every draw (apps/system/file_explorer/open_and_draw.c:137, fs/vfs/volumes.c:114-124).** Confidence 15%.
   The first call runs msd_autoinit_safe(), which brings up USB mass storage (the owner has a USB stick). Later calls rescan the partition tables of every disk, with sector reads over xHCI/AHCI/NVMe, whenever g_usbdev_gen changes.
   g_usbdev_gen is bumped in storage_fat_and_hubs.c:101/103 on any USB device add or remove. If a xhid_recover or watchdog re-enumeration around the drag counts as an add or remove, the next draw does a full blocking disk scan.
   Fix: do vol_scan in a tick or worker, never in fx_draw_thispc. In the draw, only read g_vol.

4. **Out-of-bounds SCENE writes on an off-screen window (they corrupt memory, which looks like a freeze).** Confidence 10% combined.
   - apps/system/file_explorer/open_and_draw.c:110 (properties dialog dimming) and :320 (delete-confirm dimming) loop over wx..wx+ww and wy..wy+wh writing SCENE[y*SW+x] with no clipping. A window dragged low (y up to H-TITLEH) writes up to about h rows past the end of SCENE.
     Fix: clamp first: `int y0=wy<0?0:wy, y1=wy+wh-1>(int)H?(int)H:wy+wh-1, x0=wx+1<0?0:wx+1, x1=wx+ww-1>(int)W?(int)W:wx+ww-1;`
   - apps/internet/browser/site_permissions.c:417: the find-bar border writes SCENE[(fy+t)*SW+fx] with no clipping (only when find mode is on).
   - apps/system/files_classic.c:382: draw_drive_tile edge writes are not clipped.
   Everything else checked does clip (cam_view, media_blit, xd_blit_rows, writer, pc_save/pc_show).

5. **The fast paths that come back on at drag release.** Confidence 5%.
   - videoplayer.c:263 media_blit clips correctly.
   - xdisplay.c:86 xd_blit_rows clips correctly.
   - mouse_and_bookmarks.c:125 browser_media_fast calls web_call(NWE_OP_MEDIA) right after release. web_call goes into the web engine (nested pump), so a blocked page could stall here. Only relevant if a browser page with media was the dragged window.

## Checked and judged harmless (bounded)
- Loops that depend on window size in camera, recorder, browser tabs and the logs/tfind list. They end on their own and pixel writes are clipped. Negative w/h only skips the loops.
- usr/: the infinite loops are in user processes (nvxwm, nvx-xinput, nvxinstall, init, spin). They are preemptible, so they cannot freeze the kernel compositor unless the scheduler or BKL is held. nvxinstall.c:136/163 loops on syscalls 205/208 and is only relevant if the installer is running.

## Bottom line
Nothing in apps/ or usr/ is a certain infinite loop on drag end. The most likely app-level links are device starts that run from draw code: camera (finding 1), recorder (finding 2) and Files vol_scan (finding 3).
All three run blocking USB I/O inside the compositor frame, and the first redraw after release can trigger them.
Recommended v11.03 change: do no device I/O from *_draw. Gate the deferred starts on drag<0 && g_resize<0. Clip the dimming loops in finding 4.
