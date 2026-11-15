# L1 logic review: commit 9f084d81 (v11.03 F1)

Verdict: correct as written. No HIGH defects. Two MEDIUM items (one perf, one diagnostic-accuracy), three LOW.

## Questions asked

1. **Does `drag==i` share an index space?** Yes. `drag=i` is set only at input.c:153 from a WN index. In compose_build (desktop_clicks.c:268), `order[]` holds WN indices `<NWIN` (28). `gfx_window(i)` uses the same `i` for `WN[i]` and `LY[i]` (NL=32 >= NWIN). xwins are not in this path. At release, input.c:197 sets `drag=-1`. The dragged layer's `L->dx/dy` were never updated while it was moving, so the next frame takes `full=1` (x!=L->dx) and does one full redraw of that one window. Good.
2. **Frame cost while dragging / USB starvation.** Non-dragged windows now take the `!moving` path every drag frame. Their x/y/w/h equal `L->dx..dh`, so `full=0`. They go through draw_app + op capture + diff, which is usually `f_lay_same` with no replay. That is the same per-frame cost as any ordinary `ch=1` frame (typing, clock heartbeat, video). It now runs at the refresh rate for the whole drag instead of once per change. USB: HID mice keep up to 16 TRBs in flight per EP, and the event ring has 256 entries. A slow frame only means the device NAKs and the pointer lags. That cannot overflow the ring or lose the release report: 16 x a few EPs is far below 256. busy_pump/ui_pump still poll during waits. **Lag, not a freeze.** The one real multiplier is per-pixel `SCENE` in the dimming loops (M1).
3. **klog rate limit.** g_ticks runs at 100 Hz (main.c "50 = ~0.5 s heartbeat"), so `>=100` means at most 1 line per second, global across all windows. `lk=0` initially is fine. klog only writes to memory: kr ring, sysfiles_logline (try-lock, returns if busy), crash_logline, and the hook is 0 after boot. So it is safe inside a layer draw. `ms` cannot overflow, and there is a fallback when tsc_hz=0. OK.
4. **Clipping math.** All four sites from v3-gaps A4 are patched. tpc_dialog/confirm: the original ranges were `y∈[wy,wy+wh-1)` and `x∈[wx+1,wx+ww-1)`. The new code clamps the low end with max(.,0) and the high end with min(.,H/W), both exclusive. No off-by-one, and an empty range is skipped correctly. browser find border: x and y are checked separately for each column. files_classic tile: px and both rows are checked, and `cut(t,...)` still uses unshifted `t`. Layers are allocated W x H with stride SW, so clamping to W/H is also correct when `g_scene=L->px`.
5. **videoplayer.** Correct. Returning 1 when hidden is still needed: `desk_win_exposed` returns 0 when any window has a higher z, even one that does not overlap, so a partly visible video only updates through a full compose. Setting `vp_seen_seq=seq` turns "full compose every loop pass" into "one full compose per new video frame". Do NOT change it to return 0, because that would freeze a visible, non-top video.

## MEDIUM

**M1 (perf, gets worse because of F1): per-pixel `SCENE` in the dimming loops.** `SCENE` is `scene_ptr()`. Each use does `g_scene_acc++`, `g_scene_wr++`, and calls `gfx_sync()` when `g_gq_inb|g_gfx_run|g_gq_capon` is set. During layer capture `g_gq_capon` is set, so each pixel of an 800x600 dim costs a sync check. With F1 this runs on every drag frame while a Files dialog is open in a background window. Hoist the call out of the loop:

open_and_draw.c:110 and :320 (same shape, alpha 70 / 90):
```c
{ u32* S=SCENE; int y0=wy<0?0:wy, y1=wy+wh-1>(int)H?(int)H:wy+wh-1, x0=wx+1<0?0:wx+1, x1=wx+ww-1>(int)W?(int)W:wx+ww-1; for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++){ u32*pp=&S[(u64)y*SW+x]; *pp=mix(*pp,rgb(20,24,34),70); } }
```
(Use 90 at :320.) The same applies to files_classic.c:382 (`u32* S=SCENE;` before the `for`) and site_permissions.c:417. It is semantically the same: the first call still syncs, and g_scene does not change in the middle of the loop.

**M2 (marker accuracy for F4): `draw_app_marked` does not nest safely.** desk_nested_step is guarded by `g_comp_depth`, so nested compose is unlikely. But if any path draws a window from inside another window's draw, the inner call sets `g_draw_win=-1` while the outer one is still running. The watchdog would then report "no draw in progress" during a hung outer draw. Patch in gfx.c:
```c
void draw_app_marked(int i){
    extern void draw_app(int); extern const char *APPNAME[NWIN];
    int pw=g_draw_win; u64 pt=g_draw_t0;
    g_draw_win=i; g_draw_n++; u64 t0=hr_rdtsc(); g_draw_t0=t0;
    draw_app(i);
    u64 dc=hr_rdtsc()-t0; g_draw_last=i; g_draw_win=pw; g_draw_t0=pt;
    ...rest unchanged...
```

## LOW

- **L1:** Only compose-path draws are marked. Other `draw_app` callers (logs.c dispatcher, fast paths) are not. That is fine for "freeze at drag end", which goes through compose. Note the gap in the handoff.
- **L2:** F1 does not remove the "first draw after drag" trigger. It narrows it to the dragged window, which still does its first draw at the new geometry at release (v3-gaps B1). The marker plus the >=50 ms klog will name that window. That is the intended diagnostic, and the owner should be asked for the Logs line "lag: drawing window N".
- **L3:** `extern void draw_app_marked(int);` inside the for-loop body in desktop_clicks.c:271 is legal C but untidy. Move it to file scope or wm.h. Cosmetic.

## Not found

No new unclipped writes in the diff. No new locking, allocation, or I/O added to the draw path. The capture/diff logic is unchanged.
