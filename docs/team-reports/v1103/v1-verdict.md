# V1 verdict: "freeze when a window drag ends" (v11.02, real HW)

## Disputed claims, checked against the code
- gfx.c:190 live_app = GPU3D, QUAKE, CAMERA, BENCH, RECORDER, VIDEO, DOOM, SNAKE, FLAPPY, CLOCK, SYSMON, XDISP.
  gfx.c:243 `moving=(drag>=0)&&!live_app(i)&&L->valid&&L->dw==w&&L->dh==h`. So H1 is RIGHT: live apps (Camera,
  Recorder and the rest) call draw_app on EVERY drag frame. Non-live windows skip draw_app while dragging, and only
  after they have been drawn once (L->valid). On release they get a full redraw at the new geometry (full=1).
  Nothing is drawn for the FIRST time at release.
  -> S2#1/#2 (camera uvc_start / recorder snd_rec_start "2nd draw lands at release") are REFUTED. Camera is live, so
  its 2nd draw happens on the frame after open, before or during the drag. If it hung, it would hang there.
  -> A1 says the "first-ever kmalloc of opA/opB" happens at release. That is wrong for the same reason: opA/opB are
  allocated on the window's first gfx_window pass.
- desk_win_exposed (present_and_pump.c:2) returns 0 while drag>=0 and 1 after release. Confirmed. The fast paths
  (main.c:354) run only when !ch, so they start on frame 2 after release, not on the release frame (A1 and H3 agree).
- UI release code (input.c:197, apply_snap wm.c:62, desk_win_exposed) did not change from v11.00 to v11.02 (A3).
  QEMU repro (A4) of drags, snaps and off-screen windows on Welcome, Terminal, Browser and Files: no freeze.
- NVIDIA idle re-check (S4): tick_on_off.c:7 runs nvgsp_all_off()+nvgsp_on() synchronously in the main loop once
  `g_ticks-g_last_input_t>=200`. g_last_input_t is updated on every mouse event (input.c:265) and every key (:273).
  So the first 2 s with no input after a drag runs it. Confirmed. It is armed by nv_auto_need (boot.c:121) after
  a GPU fence timeout or a read-back mismatch, needs G.state>=7 (GSP running), and runs once per boot.
- xHCI recovery (A3#1): hid_keyboard_mouse.c:121 retries pending recoveries on every poll, rate-limited to 3 per
  100 ticks PER endpoint, and each xh_cmd_s can wait up to about 100-200 ms. Confirmed. xhid_recover_now
  (controller_init.c:274) returns early if Reset EP fails or Set TR Dequeue fails, and it does not re-arm (H5/S3
  finding confirmed). The watchdog only acts on states 2 and 4.

## Ranked root causes

1. NVIDIA GSP automatic re-check / auto-start on input idle (S4#1): 30% if the machine is the GTX 1650 desktop, ~0% otherwise.
   For: this is the only code that is triggered by input going idle. The owner said "when I didn't drag the window
   anymore", which describes the mouse going still, not necessarily the button-up. Dragging is the heaviest GPU
   load, so it is the moment a 2D/compute fence timeout is most likely to arm g_auto_at. A GSP off/on with
   twod/selftest plus the gfx_start re-layer can take seconds or hang. nd_auto (nvdraw.c:99) also runs `nvgsp all`
   synchronously 3 s after the desktop appears.
   Against: it needs a Turing GPU with GSP running. The brief lists an RTL8821CE and does not mention the GPU. It
   fires about 2 s AFTER release, not at the instant of release. Not reproducible in QEMU (no NVIDIA there).
   Fix: require drag<0 && 10 s idle, or disable the automatic re-check and nd_auto for v11.03 (boot.c:120
   g_auto_tries=1, nvdraw.c:86 g_nvd_auto=0).

2. xHCI HID recovery storm, or a mouse endpoint left dead (A3#1/#2, H5#1-2, S3#1): 25% combined.
   (a) Storm: endpoints halt (cc 3/4/6) on a wireless receiver. Each recovery costs up to 2 command timeouts and
   there is no global cap across endpoints, so the main loop starves and the WHOLE OS freezes, keyboard included.
   This code is new in v11.02, which fits the regression.
   (b) Dead pointer: Reset EP succeeds, Set TR Dequeue fails, the endpoint is left Stopped with no TRBs, and the
   watchdog ignores state 3. Or the endpoint is in Error state and Reset EP returns cc 19. The pointer dies but the
   clock keeps running, and the owner would call that "frozen".
   For: new code, real-HW only (two receivers, 062A:4101 and 248A:16BB), and it would not show in QEMU. A burst of
   reports during the drag, ending with the release report, is the plausible moment for an endpoint to halt.
   Against: nothing in it depends on the button state, so the drag-end timing would be a coincidence.
   Fix: S3/H5 cc==19 continue plus st==3 in the watchdog; A3 global budget of one recovery per poll plus a 10 s
   back-off after a timeout.

3. Full redraw of a non-live app at release (H3#1, A2#1, S1#1, A1#1): 15%.
   For: this is the only UI work that release actually triggers: draw_app at the new x/y/w/h (snap), with possible
   unclipped SCENE writes for windows dragged off-screen (S2#4: file_explorer open_and_draw.c:110/320, browser
   find bar, files_classic.c:382), or a blocking call inside a draw (Files vol_scan, S2#3).
   Against: those draws already ran at open, and a new position alone is unlikely to block. The UI code is
   unchanged since v11.00 and A4 did not reproduce it. The OOB path needs a dialog open while the window is
   dragged low.
   Fix: clamp at release (x>=0, y+h<=g_tby) and clip the dimming loops in S2#4.

4. Fast paths turning back on via desk_win_exposed (browser_media_fast -> web_call, video_fast, xdisp_fast): 8%.
   Only if Browser (with a loaded page), Video or X was the app. web_call can block in the engine.

5. Silent-freeze modifiers (not triggers, but they explain "entire OS froze, no message"): the freeze watchdog
   runs after bkl_lock in the PIT ISR (H6#1, S5#1), and g_frz_hold can latch (H6#2). Also BSP-side syscall waits:
   waitkey loops forever on keys and runs no UI (S6#1, 15% per S6, but no evidence a native app was running). 7%.

6. Other: heap growth with IRQs off (S5#2, a stall), Wi-Fi LPS MMIO stall (S4#2), SMP concurrent
   xhci_mouse_poll via kidle (S3#3), BKL/SMP deadlock (H6). 15% total, spread out, none tied to the drag.

Refuted or very unlikely: apply_snap or the release path looping (H2, H4, S1), camera/recorder start deferred to
release (H1, verified), HID boot-decode as the freeze (H4#2: at worst a stuck button, which does not freeze).

## Facts from the owner that would decide
1. Which machine: the GTX 1650 desktop (then #1 is live; check the log for "nvgsp: automatic re-check" or
   "start-up did not finish last time", and NVSTART.TXT on the USB stick) or a laptop without NVIDIA (#1 is out).
2. After the freeze, does the clock or the Caps Lock LED still respond, and does the keyboard work?
   Clock running + keyboard working + pointer dead = #2b. Everything dead = #2a, #1 or a CPU hang.
3. Did the red "NoovexOS is not responding" box appear after 5 s? Box = the main loop is stuck with IRQs on (#1,
   #2a, #3, #4). No box = IRQs off, BKL, or triple fault (#5/#6).
4. Did it freeze the instant the button went up, or about 2 s after he stopped moving? About 2 s points to #1.
5. Which app, and was the window snapped to an edge or dragged partly off-screen? Browser/Video points to #4.
   Files with a dialog, or a window dragged low or off-screen, points to #3.
6. Is it repeatable on every drag, or only once per boot? Once per boot fits #1 (runs once per boot); repeatable
   fits #3.

## Recommended for v11.03 (cheap, covers the top 3 without knowing the answers)
Gate the NVIDIA auto re-check and nd_auto (rank 1). xHCI: cc19/st3 handling, a global recovery budget per poll,
and a back-off after timeouts (rank 2). Clamp at release and clip the S2#4 loops (rank 3). Run freeze_tick before
bkl_lock and put g_ml_stage plus the window id from an ml_mark around draw_app into the report, so the next freeze
names its own cause.
