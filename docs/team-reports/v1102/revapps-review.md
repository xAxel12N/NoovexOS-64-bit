# RevApps review — v11.02 c-launch + c-cam (read-only)

Scope: `git -C wt/c-launch diff main` (apps/media/camera.c, apps/media/recorder.c) and
`git -C wt/c-cam diff main` (drivers/usb/xhci/uvc_webcam.c). Line numbers are on each branch.

Verdict: both diffs are correct in intent and merge cleanly (`git merge-tree` of the two heads: no
conflicts; they touch different files). Together they turn the Camera-open freeze from "~32 x
(4 control xfers + Configure Endpoint) with timeout recovery" into at most ~4 s plus one iteration.
No guard flag is left stuck on close. Camera start is still synchronous on the draw path and on the
input path (MEDIUM below). No HIGH blockers.

## Checks

| Check | Result |
|---|---|
| First open auto-starts | Yes. Frame 1 paints "Starting camera..." (cam_auto 1->2), frame 2 calls cam_try_start (camera.c:48). |
| Start button | Yes. camera.c:98 -> cam_try_start, sets/clears cam_failed. |
| Reopen | Yes. close_win -> camera_closed (camera.c:106) resets cam_auto=1, cam_failed=0. Start menu uses open_win directly (not camera_open), so this reset is the one that counts. Fine. |
| Hotplug: camera plugged in after open | Yes. The auto condition waits for uvc_found(), cam_auto stays 1 until then. |
| Hotplug: unplug/replug while open | No auto restart, cam_failed may be stale (LOW-1). |
| Recorder first open / hotplug of mic | Yes. rc_tried is only set once snd_rec_avail() is true. Reset on close (recorder.c:103). |
| Guard flags reset | cam_busy is set/cleared around a single synchronous call. cam_failed and rc_tried/rc_micerr are reset on close. OK. |
| 4 s budget clock | hr_ns() (arch/x86_64/time/hrtimer.c:71): TSC or HPET, so it advances with interrupts off. The fallback (src==0) is g_ticks*10 ms, which only advances if the PIT IRQ runs; xh_event_t spins in io_wait with interrupts on, so it is fine in practice. The budget is checked only at the top of each iteration (MEDIUM-3). |
| Teardown after failure | Budget/limit exit drops the EP if cam.epon (uvc_webcam.c:37). A Configure Endpoint failure leaves epon=0 (uvc_cfg_ep only sets epon when cc==1). A SET_INTERFACE failure drops the EP. Rings/buffers are static and reused, not leaked. The cc==0 (command timeout) exit leaves an in-flight command (LOW-3). |
| Branches combine | Yes. camera.c only calls uvc_start, and the return contract (<0 on failure, 0 on success) is unchanged. recorder.c's single attempt and ausnd_rec_start's back-off stack without conflict, but the "try again" message is wrong after back-off (MEDIUM-2). |
| Still on the draw path? | Yes (camera.c:48, recorder.c:63). Now bounded, but a 4 s+ synchronous USB sequence still runs inside camera_draw (MEDIUM-1). |
| HD toggle / Switch camera / H key | camera.c:99, 102, 104 call cam_try_start synchronously from click/key handlers (graphics/ui/input.c:302). Bounded to ~4 s now, but it still blocks input (MEDIUM-1). |

## Findings

### MEDIUM-1: camera start still runs synchronously in draw and input handlers
apps/media/camera.c:48 (draw), :98, :99, :102, :104 (click/key). recorder.c:63 (draw).
With c-cam a failing start now takes up to 4 s plus one iteration (worst case several control
timeouts). During that time the UI and all input are frozen, and so is HID servicing if the event
pump is not run. The 1->2 frame trick only ensures the window paints first. Suggested patch: queue
the request and run it from one place, at most once per frame, with the window already painted.
Click/key handlers then only set a flag:

```c
static int cam_req=0;
static void cam_try_start(void){ cam_req=1; }
static void cam_do_start(void){
    if(!cam_req||cam_busy)return; cam_req=0; cam_busy=1; int r=uvc_start(cam_hd); cam_busy=0; cam_failed=r<0; }
void camera_draw(int i){
    win_frame(i,"Camera"); cbn=0;
    if(uvc_found()&&cam_auto&&uvc_state==1&&!cam_failed&&!cam_busy){ if(cam_auto==1)cam_auto=2; else { cam_auto=0; cam_req=1; } }
    else cam_do_start();
```
(The `else` makes sure a request set in this frame runs on the next frame, after the window shows
"Starting camera...". Better still: call cam_do_start() from the desktop idle tick rather than from
draw. That is out of this slice, so the draw-side deferral above is the minimal version.) Also show
"Starting camera..." while cam_req is set: in cam_nocam use `cam_req||cam_auto?"Starting camera..."`.

### MEDIUM-2: recorder says "press the red button to try again", but the back-off silently refuses
apps/media/recorder.c:69 and drivers/usb/xhci/uvc_webcam.c:240 (aud_backoff, c-cam).
After one failure, a red-button press within 5 s returns 0 without trying. After 4 failures
ausnd_rec_start gives up until the device is re-attached, so the button can never work. A user
action should bypass the automatic back-off. Patch (c-cam):

```c
void ausnd_rec_retry(void){ aud_cpfails=0; aud_cpfail=0; }
void ausnd_retry(void){ aud_pbfails=0; aud_pbfail=0; }
```
recorder.c rc_start_rec, before snd_rec_start:
```c
    { extern void ausnd_rec_retry(void); ausnd_rec_retry(); }
```
(If HDA is the active capture backend this is a harmless no-op. Otherwise route it through core/sound.c
as snd_rec_retry().) The automatic callers keep the back-off: Linux ALSA/dsp paths
sys/linux/syscalls/timers_and_handles.c:171/173/198, files_dirs.c:368 and core/sound.c:51
snd_dev_read call snd_rec_start per read/ioctl, which is exactly what the back-off protects.

### MEDIUM-3: the 4 s budget is only checked between iterations
drivers/usb/xhci/uvc_webcam.c:37. One iteration does probe SET_CUR, GET_CUR, commit SET_CUR, Configure
Endpoint and SET_INTERFACE. Each one can hit an xh_event_t spin timeout plus v11.01's Stop Endpoint/Set TR
Dequeue recovery, so the real worst case is about 4 s + 5 timeouts. Also a GET_CUR failure is not counted.
Patch: re-check the deadline after each control request inside the iteration:

```c
#define UVC_OVER() (cfgtries>=6||ctlfail>=3||hr_ns()-t0>4000000000ULL)
...
        if(!uvc_req(0x21,0x01,0x0100,...,p)){ ctlfail++; uvc_m("Camera refused the picture size (probe)"); continue; }
        if(UVC_OVER())continue;
        u8 g2[48]; ... if(uvc_req(0xA1,0x81,...,g2)){ ... } else ctlfail++;
        if(UVC_OVER())continue;
        ... commit ...
        if(UVC_OVER())continue;
```
(`continue` lands on the top-of-loop check, which does the teardown and returns -1.)

### LOW-1: hotplug while the window is open does not re-arm the auto-start, and the error can be stale
camera.c:48. After unplug (uvc_gone, storage_fat_and_hubs.c:235) and replug, cam_auto is 0, so no auto
start. If the earlier start failed, the window keeps saying "The camera did not start" for the new device.
Patch: track the found edge in camera_draw.
```c
static int cam_was_found=0;
...  int f=uvc_found(); if(f&&!cam_was_found){ cam_auto=1; cam_failed=0; } cam_was_found=f;
```

### LOW-2: ctlfail counts fast STALLs the same as timeouts
uvc_webcam.c:40/44. A camera that STALLs probe on 3 unsupported sizes (a fast, harmless failure) now gives
up, where before it tried up to 16 sizes. The time budget already bounds slow failures. Suggest raising the
limit to 6 or counting only timeouts (cc 0 from xh_ctrl) if uvc_req can expose cc.

### LOW-3: the cc==0 exit leaves the timed-out Configure Endpoint pending
uvc_webcam.c:58. When xh_cmd_s times out, the command TRB stays on the ring. Its late completion
(event 33) will be taken by the next xh_cmd_s as its own result. This existed before (xhci slice), but
c-cam now relies on cc==0 meaning "controller not answering". At least latch it so the camera is not
retried until reopen:
```c
            if(!uvc_cfgcc){ uvc_m("USB controller did not answer the stream setup"); uvc_state=1; return -2; }
```
Optional: show a specific message for r==-2 in camera.c. Forward the stale-completion issue to the
xhci owner (controller_init.c:184-190: drain or tag command completions by TRB pointer).

### LOW-4: the cc 17 diagnostic is overwritten in the UI
uvc_webcam.c:49. When every alt is marked bad, "USB controller refused every stream setting" replaces the
"code 17, bad endpoint setting" text the owner needs to report (klog keeps it). Suggest appending:
`uvc_m(uvc_cfgcc==17?"USB controller refused every stream setting (code 17)":"USB controller refused every stream setting");`

### LOW-5: auto-start depends on a second frame being drawn
camera.c:48. If the compositor only redraws on change, the window stays at "Starting camera..." until
the next redraw. In practice camera_draw is redrawn continuously (it polls uvc), so this is only a
note. MEDIUM-1's tick-based variant removes it.

## Verified OK
- badalt bitmask: cam.alt[24] (storage_fat_and_hubs.c:159), so 1u<<k cannot overflow.
- uvc_cfg_ep sets epon only on cc==1 (storage_fat_and_hubs.c:251-252), so the EP is not double-dropped.
- xh_cmd_s returns without writing ccode on timeout, and uvc_cfg_ep initialises cc=0, so cc==0 really means timeout.
- camera_open's `!WN[].open` guard does not reset state for an already-open window. close_win always calls camera_closed/recorder_closed.
- aud_attach resets the fail counters (they are separate statics, so the memset of aud does not matter).
