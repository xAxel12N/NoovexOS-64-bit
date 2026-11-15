# L2 logic review: a5e1b29f (F2: no device I/O from draw, ticks, fast-path pause)

Verdict: safe to ship. No HIGH findings. Two MEDIUM regressions (Files misses a USB stick that becomes ready late; the "one frame after drag" pause on the fast paths often does nothing). One LOW (mic hotplug no longer auto-starts).

Checked and OK:
- Camera first open: the draw sets cam_auto=0 and cam_req=1. On the next loop iteration camera_tick runs at main.c mark 6, which is outside compose and outside any USB wait, so it is only reachable from kmain. It calls cam_try_start, returns 1, and the window redraws once. It starts only once per open: cam_auto is reset only by camera_open/camera_closed.
- Reopen: camera_open/camera_closed reset cam_auto, cam_failed and cam_req. camera_tick clears cam_req and settle when the window is closed. The recorder clears rc_req/settle in its tick when closed, and recorder_closed resets rc_tried.
- Start, HD and Stream buttons now set cam_req. The tick starts the camera on the next frame and also clears cam_failed through cam_try_start. "Starting camera..." shows in the meantime.
- Camera hotplug: cam_auto stays 1 until uvc_found()&&uvc_state==1 on a draw, which is the same dependency as v11.02.
- Closing mid-request: the request is only a flag, and the closed tick drops it. uvc_start itself is synchronous, same as before.
- Fast paths: all four return 0 (no work) while paused and do not touch vp_seen_seq, so h3 #2 (busy redraw) is not recreated.
- No busy redraw loop: camera_tick returns 1 only when it starts the camera, recorder_tick returns 1 once per request, and files_vol_tick returns 1 only when g_vol_scans changed.
- app_io_tick cost: two WN[] checks per frame when the apps are closed. hr_ns() is called only while a Files request is pending. Negligible.
- Removing uvc_poll() from camera_draw is fine because main.c mark 6 already polls every frame.
- tpc_usage_c, which stays in the draw, only touches memory (FS[] and n4_usage).

## MEDIUM 1: Files never sees a USB stick whose MSD becomes ready without a g_usbdev_gen bump
apps/system/file_explorer/open_and_draw.c files_vol_tick (+136-145). The tick only re-requests a scan when g_usbdev_gen changes. vol_scan(0) also rescans when msd_ok or msd_lastlba changes. msd_ok is set by msd_autoinit_safe from other places, for example core/update.c:134 every 1000 ticks, nvdraw.c, and nvme.c. None of those bump the gen.
Before this commit the draw called vol_scan(0) every frame and caught this. Now the drive stays hidden until the user clicks This PC or reopens Files.
Scenario: insert a stick, the gen bump scans before MSD is up, update.c then sets msd_ok, and Files never shows the drive.
vol_scan(0) returns early and costs almost nothing when nothing changed, so a 3 s poll is safe. Clicking This PC also skips the 3 s wait.
Patch (replace the body after the min/drag line):
```c
    if(WN[W_FILES].min||drag>=0||g_resize>=0)return 0;
    u64 now=hr_ns();
    if(!fx_vs_req&&g_usbdev_gen==fx_vs_gen&&fx_vs_ns&&now-fx_vs_ns<3000000000ULL)return 0;
    fx_vs_req=0; fx_vs_ns=now; fx_vs_gen=g_usbdev_gen; u32 sc=g_vol_scans; vol_scan(0); return g_vol_scans!=sc; }
```
With this patch, an explicit request, a gen change or a 3 s timeout each triggers vol_scan(0), which is a no-op if nothing changed. There is also no longer a 3 s delay after "This PC" when the last scan was recent.

## MEDIUM 2: the fast-path "settle" frame is often skipped
videoplayer.c video_fast, mouse_and_bookmarks.c browser_media_fast and xdisplay.c xdisp_fast each have their own `static settle`. They are called from main.c:354 only when `!ch`, in a chain (browser only if video returned 0, xdisp only if both did).
During a drag ch is normally 1 every frame, so these functions are never called, settle is never set, and the first `!ch` frame after release runs the fast path immediately. That is the same timing as before the commit.
The pause during the drag still works, because ch=1 already blocks the fast paths then. The "one frame after" part does not. The commit message promises it, and v3-gaps B2 depends on it.
Fix: make one shared settle counter that is updated every loop in app_io_tick, which runs at mark 6 before line 354 on every iteration.
camera.c, at the start of app_io_tick:
```c
int g_drag_settle=0;
int app_io_tick(void){
    extern int drag, g_resize; extern int files_vol_tick(void);
    if(drag>=0||g_resize>=0)g_drag_settle=2; else if(g_drag_settle)g_drag_settle--;
    int ch=camera_tick(); if(files_vol_tick())ch=1; return ch; }
```
Then, in each of the three fast paths, replace the inserted block with:
```c
    { extern int g_drag_settle; if(g_drag_settle) return 0; }
```
The value 2 covers the release frame plus one more. It still returns 0, so there is no busy loop. Camera and recorder settle are fine as written, because they are called every loop iteration.

## LOW: mic plugged in after Voice Recorder opens is no longer auto-started
recorder.c:67 now sets rc_tried=1 even when snd_rec_avail()==0, and recorder_tick then skips the start. The old code kept retrying on later draws, so a mic plugged in later started on its own.
Nothing hangs: the user sees "Press the red button to record" and the button still works.
Patch (snd_rec_avail is cheap and already called in this draw at :73):
```c
    if(!rc_tried&&snd_rec_avail()&&!g_cap_on){ rc_tried=1; rc_req=1; }
```

## Notes (no change needed)
- When Files is reopened within 3 s of the last scan, it shows cached g_vol data until the window expires. The MEDIUM 1 patch makes the was_open request skip the wait.
- uvc_start is still a blocking call in the main loop, now outside compose and drags. This matches gap B7.
