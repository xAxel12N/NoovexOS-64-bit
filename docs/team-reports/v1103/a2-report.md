# A2 - desktop render pipeline: drag vs after drag end (read-only)

## Pipeline
main loop core/init/main.c:356-370: ch -> compose_present(); heartbeat 50 ticks -> compose_present (GPU) / present_topbar; cursor-only -> present_cursor (full frame if dock/menus/fdrag/ddrag; NOTE window drag is NOT in that list, but mouse_step returns changed=1 on every move so drag frames go via ch).
compose_present (present_and_pump.c ~95) -> compose_build (desktop_clicks.c:263) -> per window gfx_window(i) (gfx.c:240) or draw_app(i) (logs.c:90) on CPU path -> gfx_scene_end -> gq_copy nc -> draw_cursor -> blit_all (igpu_present_hint) -> gfx_frame_end (gfx_sync) -> nc_restore -> gfx_scene_settle.
Nested: desk_nested_step / busy_pump / ui_pump (present_and_pump.c ~140-180).

## State flipped at drag end: graphics/ui/input.c:197
`if(drag>=0&&g_snap) apply_snap(drag,g_snap); drag=-1; g_resize=-1; g_snap=0; g_marq=0; changed=1;`
apply_snap (wm.c:62) changes x/y/w/h, sets max=1, z=++ztop.

## Code that runs ONLY after drag flips to -1
1. gfx.c:243 `moving=(drag>=0)&&!live_app(i)&&...` -> during ANY drag, EVERY non-live window (Files, Term, Editor, Browser, Settings, Writer, Paint, Logs, ...) skips draw_app entirely and just composites its cached layer. First frame after release: draw_app() runs again for ALL open non-live windows, op capture (gq_cap_begin OPMAX 2048) and, for the moved/snapped one, full replay (x!=L->dx or w!=L->dw -> full=1, region = whole screen if !valid). This is the biggest "after release" body of new work. Live apps (Camera, Recorder, Video, Clock, Sysmon, X, games) draw every frame anyway -> no change for them.
2. Snap: apply_snap changes w/h -> any app draw that reallocates/relayouts on size change runs now (e.g. xdisplay.c:159 kfree/kmalloc of shadow on size change). On CPU path (g_gfx off) draw_app runs every frame during drag too, so only the size change is new.
3. desk_win_exposed (present_and_pump.c:1) becomes 1 -> direct-present fast paths resume from main loop (main.c:356): browser_media_fast -> web_call(NWE_OP_MEDIA) synchronous + present_rect (mouse_and_bookmarks.c:119); video_fast -> media_blit + present_rect (videoplayer.c:255); xdisp_fast -> xfb_collect/xd_blit + present_rect (xdisplay.c:79).
4. desk_popups_clear (desktop_clicks.c:299) becomes 1 (callers gate on it).
5. snap_preview_draw stops; drag alpha (gfx.c:278) restored.
6. hover: mouse_step win_edge_hit runs again when drag<0 && !btn.

## Waits on those paths (all checked bounded)
- gq_sync/bsubmit (gq.c:67,179) -> g_gqhw.submit; failure -> gfx_stop. Bounded by backend.
- igpu eng_run (igpu.c:170) hr_ns timeout e_tmo, then 10 ms csb_idle; blitter fail -> g_pres_eng=1 shader fallback (igpu.c:303).
- igpu_wait_flip (igpu.c:79) 40 ms timeout, 3 strikes -> flip off. Called from every blit_region2.
- NVIDIA nv_poll (nvflcn.c:14) ms-bounded; nvflcn.c:28 2.1 s bound.
- ALL depend on hr_ns() advancing. If hr_ns stalls (HPET/TSC not ticking, or read with IRQs off on a broken source) every one of these becomes unbounded. Not verified here.
- present_rect/quiet_on call gfx_sync with g_scene_quiet held; frame_check runs with IRQs off (not on drag path).
No while-loop without timeout found on the drag-end render path in the files read.

## Ranked suspects (for "freeze when drag ends")
1. (40%) First draw_app() of all non-live windows after drag (gfx.c:243-251). Any app draw that blocks (web engine call from browser_draw, Files/USB enumeration, Settings pages that query hardware, Writer) or an op-capture overflow/replay path re-runs here only. Fix/probe: klog before/after draw_app in gfx_window with window id; or make `moving` drop only the dragged window (i==drag) so other windows keep drawing during drag (then a hang would show while dragging, separating cause), and on release mark L->valid=0 lazily one window per frame.
2. (20%) browser_media_fast/video_fast/xdisp_fast resuming (desk_win_exposed=1) -> synchronous web_call or media_blit from main loop; only if Browser/Video/X open. Fix: gate on web_busy and add timeout; klog entry.
3. (15%) Snap resize (apply_snap) -> app relayout/kmalloc in draw on new size; full-screen replay of large op list. Ask owner if window hit a screen edge.
4. (10%) hr_ns-bounded GPU waits becoming unbounded if clock source stalls (igpu.c:79,170; nvflcn.c:14) - would also hit during drag though, so less likely.
Confidence overall that freeze is in render path (vs input/xHCI): ~50%.
