# S1 report — graphics/ hang search (freeze when a drag ends)

Summary: graphics/ contains NO loop whose exit depends on drag/btn/vblank/flag state, no recursion, and no z-order walk that can cycle
(all window walks are bounded `for(k<NWIN)` / bubble sort over `order[]`). The release code itself (input.c:197) is O(1).
The freeze must therefore come from code that RUNS FOR THE FIRST TIME (or again) because drag went -1. Three such gates exist:

## 1. gfx_window layer cache: draw_app() is skipped during drag, re-run at release  — 55%
drivers/video/fb/gfx.c:243  `int moving=(drag>=0)&&!live_app(i)&&L->valid&&L->dw==w&&L->dh==h; if(!moving||g_resize==i){ ... draw_app(i) ... }`
- While a non-live window is dragged its content is NOT redrawn (cached layer composited at new x/y). The first compose after
  release (drag=-1) calls draw_app(i) for EVERY non-live window again, with x!=L->dx -> `full` path (gq_fill + gq_replay of whole op list).
  If apply_snap fired (input.c:197, wm.c:62) w/h also change -> full redraw with new size.
- So any app draw that blocks (net/disk/web_call/USB wait inside *_draw, e.g. browser_draw, files_draw on USB, settings/wifi,
  regedit, logs) is first executed exactly at drag end. Also gq_cap_begin/gq_replay with OPMAX=2048 on a resized window and
  gq_sync -> gfx_stop path in gfx_sync (gfx.c ~292) if GPU stalls.
- live_app list (gfx.c:190) excludes Browser/Files/Settings/Terminal etc. — exactly the apps an owner would open from Start.
Fix/diagnose: log `klog("draw_app %d start/end")` around draw_app in gfx_window; or set a watchdog marker (g_ml_beat/ml_mark) before
draw_app(i) and record i, so the existing main-loop watchdog names the window. Quick mitigation: after release, defer redraw of the
dragged window by one frame is NOT a fix; real fix is in whichever *_draw blocks.

## 2. desk_win_exposed() turns ON at release -> fast present paths start  — 30%
graphics/ui/desktop/present_and_pump.c:1-6 returns 0 while `drag>=0||g_resize>=0||...`, 1 after release (if window is topmost, no toast).
Callers that start doing real work at that moment (core/init/main.c:355 main loop, only when !ch):
- apps/internet/browser/mouse_and_bookmarks.c:120-130 browser_media_fast: when Browser has a page (g_page==3) it calls
  `web_call(NWE_OP_MEDIA,r)` writing straight into SCENE every loop. web_call can block (JS engine) -> whole-OS freeze.
  Note: busy_pump's "page is busy" dialog only shows after 3 s and only if the pump runs during web_call.
- apps/media/videoplayer.c:255-266 video_fast -> media_blit + present_rect.
- apps/x11/xdisplay.c:79-87 xdisp_fast -> xfb_collect + present_rect.
Also note toast_t / g_toast_cur gate: a toast after drag delays this further (could make it look intermittent).
Fix: in browser_media_fast add `if(drag>=0||g_resize>=0)return 0;` is already implied; add a time budget / skip NWE_OP_MEDIA unless
the page actually has playing media (web side flag), and guard web_call with the busy_pump watchdog. Ask owner if Browser was the app.

## 3. present_rect cursor overlay while SCENE is written directly  — 5%
present_and_pump.c:8-16: present_rect draws cursor into SCENE then nc_restore; called from the fast paths of #2. Not a hang
by itself (bounded loops), but quiet_on/quiet_off nesting (g_scene_quiet) — if a fast path returns between quiet_on/off via
web_call reentering desk_nested_step -> compose_present, counters can go off. Low.

## Non-candidates checked (bounded / cannot hang)
- input.c:197 release: apply_snap (wm.c:62) is straight-line; drag=-1, g_resize=-1, g_snap=0.
- input.c:198-213 drag/resize move code: no loops.
- input.c:60,63 nvxlc_waitkey/nvxlc_sleep: wait loops on kpop/g_ticks with cpu_idle — would hang only if IRQs off/ticks stop; not drag-related.
- hk_poll (hotkeys.c:67) ring-buffer drain, bounded.
- win_anim (present_and_pump.c:142) returns immediately (animations off); win_anim_unused never called.
- gfx.c scroll_o / gs_snap sanim: ends by g_ticks after 140 ms; gfx_anim_active ends when anims end.
- theme.c:27 line_bl Bresenham terminates; wallfx.c:24 isq terminates; effects.c:27 bounded.
- desk_nested_step/busy_pump have `static in` reentry guards; ui_pump has none for xhci_mouse_poll/present_cursor (re-entry from
  inside a draw is possible but bounded).
- main.c:367 present choice uses g_fdrag/g_ddrag but not `drag`: window drag frames go through ch from process_input.

## Ask the owner
Which app was dragged (Browser with a page loaded? Files on USB?), was it snapped to an edge, does the pointer still move.
If pointer moves -> main loop alive in a nested pump (web_call). If pointer frozen -> IRQs off / spin in a draw.
