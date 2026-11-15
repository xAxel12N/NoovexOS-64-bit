# V3: coverage gaps and fix risks (v11.03, read-only)

Fixer scope used: F1 gfx.c (dragged-only cache, draw_app markers, SCENE clip, videoplayer busy loop); F2 apps (no device I/O in draw, media fast-path pause after drag); F3 USB (1 recovery/poll, 10 s hold, Stopped recovery, boot-fallback fix, poll reentrancy guard); F4 watchdog before BKL, last marker, g_frz_hold counter, bkl_take(0).

## A. Top suspects NOT covered by any fixer (ranked)

1. NVIDIA idle re-check / auto GSP start (s4 #1, 25%). drivers/video/nvidia/gsp/tick_on_off.c:7-9 runs nvgsp_all_off+nvgsp_on+selftest+gfx_start(GFX_GPU) inside the main loop on the first 2 s with no input after g_auto_at is armed by a fence timeout. A drag is the heaviest GPU load (most likely to arm it), and the drag ending is the first idle moment, so the timing fits the report closely. nvdraw.c:99-108 nd_auto also runs `nvgsp_cmd("all")` plus USB-stick writes (msd_autoinit_safe, ufat_write_file NVSTART.TXT) 3 s after the desktop appears. Memory says the owner's desktop has a GTX 1650 (Turing, in range 0x1E00-0x1FFF). The brief's RTL8821CE suggests a laptop, so it is unclear which machine was used. If it was the desktop, this is a top-3 suspect and nobody owns it.
   Fix (cheap): tick_on_off.c:7 add `drag<0&&g_resize<0&&g_ticks-g_last_input_t>=1000`; or boot.c:120 `g_auto_tries=1` and nvdraw.c:86 `g_nvd_auto=0` for v11.03.
2. Browser browser_media_fast -> web_call(NWE_OP_MEDIA) (s1 #2, a1 #2, a2 #2, s2 #5; 5-30%). F2 "pauses media fast paths during/just after drags", which only delays the call. A web_call that blocks will still freeze one or two frames later. No timeout or web-side "has playing media" gate is assigned. Ask F2 to also gate on a web-side media flag and web_busy.
3. Non-live app draws other than Files/camera/recorder (a1 #1, a2 #1, s1 #1, h3 #1; 35-55% as a class). F1 markers will *identify* the app, and F2 removes I/O from Files/camera/recorder. Nobody audits browser_draw, Settings (pci_scan/rtw_find_mmio guarded by "once"), terminal or logs for blocking calls. With F1's change these apps now draw *during* the drag (see B1), so a blocking draw would show while dragging instead of after it. That is diagnostic but not a fix.
4. Off-screen window position at release (h2 #2, h3 #1). No fixer clamps WN[drag].x>=0 / y+TITLEH<=g_tby at input.c:197-201. F1 "clip SCENE writes" covers gfx.c only. The unclipped app writers in s2 #4 are not in F1 or F2 scope as listed: file_explorer/open_and_draw.c:110 and :320, browser/site_permissions.c:417, files_classic.c:382. Check that F1/F2 actually patch those 4 sites, or add the release clamp (one line, input.c:197).
5. Synchronous main-loop stalls coincident with idle (low, 4-15% each, uncovered):
   - s6 #1 syscall.c:125 waitkey `while(kpop()<0) kidle();` with g_frz_hold=1. On the BSP it waits forever for a KEY with no redraw. F4's counter fix keeps the watchdog suppressed during it, so it stays invisible.
   - s6 #2 syscall.c:205 sleep clamp 1 h with no pump.
   - s6 #3 wifi_mgr.c:287 synchronous auto-join from the main loop.
   - s4 #2 RTL8821CE LPS-on followed by MMIO polling (PCIe completion stall: a hard CPU hang no watchdog can break).
   - s6 #4 nvx3_sync_all to USB root.
6. s5 #2: heap growth (hp_alloc_run + zeroing 64 MB) with IRQs off, 10-40 ms. Not covered. It is a stall, but the xHCI event ring can overflow while it runs (in-flight 16/EP x N EPs vs 256 entries) and drop the release report. Fix: zero outside the IRQ-off region.
7. s5 #3: nested panic `for(;;)cpu_stop()` draws to FBB, which is invisible when scanout is on a GPU/NVIDIA surface. A crash then looks like a silent freeze. F4 has the same visibility problem (B3).
8. h5 #3: no CLEAR_FEATURE(ENDPOINT_HALT) after a stall, so the endpoint re-stalls at once. F3's 10 s hold limits the cost, but the mouse can stay dead and look like a freeze. h5 #4: g_xcur is not restored after xhci_mouse_poll (not listed in F3).

## B. Risks that a fix makes things worse

1. F1 "only the dragged window uses the cache": during a drag every other non-live window now runs draw_app + op capture + replay every frame. Before, they only composited. Costs:
   - Frame time grows with the number of open windows. If the per-frame budget is exceeded, the main loop polls USB less often. HID in-flight TRBs (16/EP) are fine, but the 128-event guard and xh_q can back up and lag the pointer.
   - Any slow/blocking draw (A3) now runs every drag frame. That gives better diagnosis, but a v11.02 freeze can become an "always laggy drag".
   - The full=1 path: other windows did not move, so they should take the partial diff, not full. Verify that L->dx==x for them, or each frame does a full-screen gq_fill+replay per window.
   - Alternative intent check: the dragged window still skips draw_app, so at release that one window still gets its first draw at a new geometry. The "first draw after drag" trigger is reduced to one window, not removed.
2. F2 "media fast paths paused just after drags": if the pause is time-based but leaves vp_seen_seq/exposed state unchanged, it can recreate h3 #2 (video_fast returns 1, ch=1, full compose every pass, busy redraw). It must return 0 (no work) while paused, not "changed".
3. F4 watchdog in timer IRQ before the BKL:
   - It writes the framebuffer from IRQ context while the main loop (or another CPU holding the BKL) may be mid-blit or mid-flip. Torn output is acceptable. The real dangers:
     - On a GPU scanout (NVIDIA/igpu surface), FBB is not visible: a silent box again.
     - If it calls anything that takes the BKL, kmalloc (IRQ-off cli is not a lock: an AP may be inside kmalloc), klog with locks, gq/GPU submit or cx_save file I/O, it deadlocks or corrupts. It must be draw-to-FBB only, no alloc, no klog-with-lock. The cx_save at 3000 must stay gated by bkl_try (h6 suggestion).
   - Re-entrancy: freeze_tick runs on every tick. Guard with a static flag so a slow draw does not nest under the next timer IRQ.
   - Reading the "last marker" is fine. A marker written as a pointer to a string must not point into freed or stack memory.
   - On SMP only the BSP timer should run it, or each AP tick draws too.
   - False positives: legitimate long waits (uvc_start up to 4 s, nvgsp_cmd("all") many seconds, wifi connect) now trigger the box/report. Confirm the threshold (5 s) is above them, or that they set g_frz_hold.
4. F4 g_frz_hold counter: an unbalanced inc/dec breaks the watchdog. A path that sets hold=1 directly (power.c) or returns early without a decrement makes it sticky (same symptom as before), and a double decrement makes it negative, which, if tested as `if(g_frz_hold)`, disables it permanently. Convert all writers (power.c, syscall.c sleep and waitkey) and test `>0`.
5. F4 bkl_take(0) now a no-op: faults.c:104 and any longjmp/preempt path that relied on bkl_take(0) to (re)acquire the lock will now run without the BKL. That causes races in the scheduler or heap (kmalloc relies on the BKL for SMP safety, s5 #2). h6 flagged this: verify every faults.c caller holds the BKL at setjmp before shipping.
6. F3:
   - 10 s hold after a timeout: if the timeout hits the mouse EP, the pointer is dead for 10 s. That looks like a freeze to the owner, but is better than a real one.
   - Stopped-state (3) recovery in the watchdog: a Stopped EP can be momentarily legitimate during our own Stop/Reset sequence. Must skip if xhid_recpend or recovery is in progress, or it fights itself.
   - Boot-fallback change (`hid && declared` -> never boot): any receiver that declares report IDs but really sends boot-format reports (some 2.4G combos, e.g. 248A:16BB) loses the mouse entirely. Needs the hidsim case (h4).
   - Reentrancy guard: if a nested caller (desk_nested_step/busy_pump from inside a draw that is inside a USB wait) hits the guard and returns, HID events are not drained during that wait. That is fine because xh_event_w_in dispatches HID itself. But the guard must be released on every early return path.
7. F2 moving camera/recorder start out of draw: the start must still not run inside compose or a USB wait (g_comp_depth==0, xh_wdepth==0), and must keep the once-per-open gate. Otherwise the v11.01 Camera freeze comes back in the tick.

## C. Asks to the lead
- Which machine was used (GTX 1650 desktop vs RTL8821CE laptop)? This decides A1.
- After the freeze: did the clock move, did Caps Lock respond, did the red box appear? The answers split CPU hang, dead pointer and IRQ-off.
