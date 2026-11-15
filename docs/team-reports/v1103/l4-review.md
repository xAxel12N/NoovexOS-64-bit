# L4 logic review: 3b80216a (F4 freeze watchdog) + 2c6a1d4b (NVIDIA auto opt-in)

Note: the checkout HEAD is 2c6a1d4b, whose parent is 96290600. **3b80216a is NOT an ancestor of HEAD.** It sits on another branch or worktree, so the working tree still has the old crash.c, syscall.c and interrupts.c. The lead must cherry-pick or merge it before the release.

## HIGH

### H1 (2c6a1d4b) `nvgsp auto on` can never work: nd_auto() is gated by g_nvd_auto, which now starts at 0
drivers/video/nvidia/nvdraw.c:116 `if(g_nvd_auto)nd_auto();`. g_nvd_auto is now 0 at boot. The only code that reads nvgsp-auto.cfg is inside nd_auto() (line 102), and nvd_set_auto() is the only other writer. So after a reboot with cfg='1':
- the GPU never starts by itself,
- the Settings toggle (storage_and_layout.c:292, pages.c:305) shows Off,
- `nvgsp auto` in the shell says "off".
The opt-in is dead. Disabling auto start was the goal, so this is functionally "safe", but the feature and its UI are lying. Patch:
```
-    if(g_nvd_auto)nd_auto();
+    nd_auto();
```
and in nd_auto (line 102):
```
-    if(nd_flag_get("nvgsp-auto.cfg")!='1'){ g_nvd_auto=0; klog(...); return; }
+    if(nd_flag_get("nvgsp-auto.cfg")!='1'){ g_nvd_auto=0; klog(...); return; }
+    g_nvd_auto=1;
```
nd_auto runs once (nd_auto_done), so calling it every poll costs nothing. Also load the flag early so Settings shows the right state before 3 s: in nd_auto, at the first-call branch `if(!nd_t_desk){ nd_t_desk=...; g_nvd_auto=nd_flag_get("nvgsp-auto.cfg")=='1'; return; }`.

### H2 (2c6a1d4b) the nvsim host tests now fail (tools/tests/nvidia/nvsim.c)
- :591 sets g_nvd_auto=1 with no nvgsp-auto.cfg and expects g_nvd_auto_skipped==1. nd_auto now returns at the cfg!='1' check, so skipped stays 0, and the test fails (with H1 it fails anyway, because nd_auto is never entered when g_nvd_auto is not set).
- :606 expects `sc_off` to schedule g_auto_at (`sched`), plus the waited/back2 re-check. nv_auto_need is now `if(1||...)return`, so all three checks fail.
Patch: in the test at :591 make nd_flag_get return '1' for nvgsp-auto.cfg (a sim stub). At :606 replace the expectations with `int sched=g_auto_at==0` ("re-check disabled in v11.03") and drop waited/back2, or wrap the line in `#if 0` with a v11.03 note. Otherwise tools/check.py goes red.

## MEDIUM

### M1 (3b80216a) bkl_try() in freeze_tick is recursive, so "klog/cx_save only when the BKL is taken" gives no IRQ safety on CPU 0
percpu.c bkl_try: `if(g_bkl.owner==me){ g_bkl.depth++; return 1; }`. freeze_tick runs on CPU 0 in the timer IRQ. In the usual freeze the main loop on CPU 0 is stuck *holding* the BKL, so bkl_try succeeds. klog (line 258) and cx_save (USB/FS writes, line 261) then run on top of the interrupted code, which may be inside klog, heap, xhci or fs. This is the same as before (the old path also ran under a recursive bkl_lock), so it is not a regression. But the commit's stated guarantee does not hold. Patch (crash.c freeze_tick):
```
-... if(bkl_try()){ klog("freeze: ... report kept"); bkl_unlock(); } ...
+... if(!bkl_held()&&bkl_try()){ klog("freeze: ... report kept"); bkl_unlock(); } ...
```
For cx_save, keep the existing g_cx_nousb/g_heap_busy guards, but also require `!bkl_held()`, or accept the risk explicitly. The report is already on screen.

### M2 (3b80216a) frz_bkl_wait ignores g_frz_hold, which gives false red boxes during legit long BKL holds
Many legit long operations set g_frz_hold and run under the BKL on CPU 0: update screen (core/update.c:99), apt (deb_archive.c:77/169/179/259), installer ip_run, usb clone, sysroot layer add, browser fs_sync, power sleep. Any AP or IRQ path that spins in bkl_lock for more than 5 s during them paints "NoovexOS is stuck" over the update or installer screen. GSP boot (`nvgsp all`, run from the main loop under the BKL) has the same problem if it takes more than 5 s. Camera start (~4 s) is under the threshold. Patch:
```
 void frz_bkl_wait(int me,int owner,u64 w0){
-    if(!FBB||!W||!H)return;
+    if(!FBB||!W||!H||g_frz_hold)return;
```
(move `volatile int g_frz_hold;` above frz_bkl_wait, or add an extern).

### M3 (3b80216a) the red box is never erased once the wait ends
frz_bkl_wait paints straight into FBB, and nothing invalidates the shadow or damage state. With fbdamage on (only changed areas sent) the box can stay on screen after a legit long wait is over. Patch: add `static volatile int g_frz_bkl_drawn;`, set it to 1 after drawing, and in freeze_tick's "beat moved" branch add `if(g_frz_bkl_drawn){ g_frz_bkl_drawn=0; g_cx_frz_redraw=1; }`. That works if g_cx_frz_redraw triggers a full repaint. Otherwise call `fb_shadow_invalidate()` from the main loop when the flag is set (not from IRQ context).

## LOW

- L1: frz_bkl_wait runs on every waiting CPU, including APs and IRQ paths, with IRQs off (bkl_lock did cpu_irq_save). It takes no locks, does no allocation and makes no klog or GPU calls: OK. The reentry guard (atomic exchange on g_frz_bkl_busy) is fine. g_frz_bkl_last is a plain u64 written only inside the guard: fine. `if(s!=g_frz_bkl_last)` inside the guard is redundant.
- L2: Framebuffer validity. With Intel page flipping, FBB points at the back buffer between igpu_pre_blit and igpu_post_blit (igpu.c:80-82). If the stall happens inside a blit, the box is drawn into a buffer that is not scanned out, so it is invisible but harmless. On NVIDIA, FBB is never reassigned (no `FBB=` in drivers/video/nvidia), so the pointer is valid. Whether the GSP scanout shows FBB was not verified.
- L3: The spin check runs every 2^20 pauses. A PAUSE is ~140 cycles on Skylake and later, so that is ~50 ms per check: fine. TSC to seconds uses g_hr.tsc_hz with a 1 GHz fallback. Before calibration this can claim "5 s" too early on a fast CPU, but only during early boot.
- L4: bkl_take(0) is now a no-op. Callers: smp_sched.c:102/117, unpack.c:31/35 (already guarded by `h`), power.c:158, main.c:308, faults.c:104, wifi_bringup.c:92, syscall.c:205. All pass the result of bkl_drop(), which returns 0 only when the caller did not hold the BKL. The old code then *took* the BKL and leaked it (depth 1, never released), so the no-op is the correct fix. No caller relies on bkl_take(0) acquiring the lock.
- L5: The sleep cap went from 3600 s to 10 s, so sleep(60000) returns after 10 s. Callers that do not loop on the clock will wake early. It also changes Linux-ABI-visible timing if case 71 is reachable from those programs. Suggest that user processes loop in chunks of 10 s or less instead: `while(ms){ u64 c=ms>10000?10000:ms; ...; ms-=c; }`. Not urgent.
- L6: g_frz_sleep is only counted on CPU 0, which is correct because freeze_tick watches the CPU 0 main-loop beat. Syscall 51 (wait for key) still uses g_frz_hold save/restore, so it has the same nesting hazard the counter was meant to fix. Suggest the same counter there.
- L7 (2c6a1d4b): the new `auto on/off` branch in nvgsp_cmd (framebuffer_present.c:297) is unreachable from the shell, because system_disks_packages_gpu.c:276 handles `nvgsp auto` before it calls nvgsp_cmd. It is harmless. Its message says "(next boot)" while the shell says nothing about timing. Pick one. The leftover `1||` in nv_auto_need will trigger a -Wunused warning for g_auto_why only, which is fine.
- Gears 'G' key, Settings "Start NVIDIA now" and `nvgsp all` go through nvd_run_request / nvgsp_cmd("all"). They depend on neither g_nvd_auto nor the auto re-check, so they still work. Ctrl+Alt+N goes through nvgsp_on directly and is unaffected. The status line at framebuffer_present.c:233 simply stops printing, because g_auto_tries and g_auto_at stay 0.
