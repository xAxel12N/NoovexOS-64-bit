# A1 call trace: mouse-button release during a window drag (v11.02 source, read-only)

## A. USB completion to mouse state (main-loop context, polled, IRQs on, no locks taken)
1. core/init/main.c:307 `xhci_mouse_poll()`, once per main-loop pass. It is also reached from kidle (net/http/http.c:249), desk_nested_step (present_and_pump.c:123) and busy_pump (:134).
2. drivers/usb/xhci/hid_keyboard_mouse.c:117 xhci_mouse_poll: usb_kbd_repeat, then `for c<g_xhc_n` xhc_select+xhci_poll_one. Bounded.
3. :106 xhci_poll_one: drains the deferred xh_q (O(n^2) shift, bounded by xh_qn), then `for guard<128` reads event TRBs. Writes ERDP via MMIO and does not wait. Bounded.
4. :86 hid_xfer: usn_xfer first, then a slot lookup loop over xhid_n. If cc==1 or 13: copy 16 bytes into the last-report buffer and call hid_report_decode. If cc is 3, 4 or 6: xhid_recover. Otherwise xhid_arm. No alloc and no wait in this function. xhid_recover/xhid_arm were not traced (controller_init.c, v11.02 watchdog code). That part is UNVERIFIED and is the only spot on the USB path that could block.
5. :57 hid_report_decode: layout loops are bounded by xhid_nlay and btn_cnt<8. Calls in_btn_raw(0,..) at :65/:77, or the boot path at :99.
6. drivers/input/input.c:12 in_btn_raw: on 1->0 it pushes one entry into g_inq (ring of MQ_N, drops it if full and counts g_mq_lost), then sets mouse_evt=1 and mouse_btn=0. No lock here (callers are polled; in_buttons is the only caller that takes the IRQ-save lock). Always returns.

## B. Main loop (core/init/main.c)
7. :308 idle gate: bkl_drop, cpu_irq_off. If mouse_evt or in_mq_pending is set it skips cpu_idle, then cpu_irq_on and bkl_take(bd). If no event arrives and no wake is armed, cpu_idle runs with IRQs off->hlt. That can only return on an IRQ: hr_wake_at, or the PIT when g_ticks_live. NO-RETURN RISK if neither fires (not specific to release).
8. :309-316 bkl_lock/yield, pack_poll, g_open_req copy (k<511).
9. :318 `ch=process_input()` (graphics/ui/input.c:237, g_pi_depth++).

## C. Window manager (graphics/ui/input.c)
10. :265-269 process_input_body: cpu_irq_off, then `while(ne<IN_MQ_N && in_mq_take)` (bounded), snapshot mouse_dx/dy/btn, cpu_irq_on. Then per queued event: mouse_step(sx,sy,prev) and mouse_step(0,0,eb). Then a final mouse_step(dx-ux,dy-uy,btn=0). The release is handled at the first edge, so the later calls see btn==prev==0.
11. :160 mouse_step(release, btn=0, mouse_btn_prev=1):
    - :161 acct_dlg_active; :165 ux_mouse_push; :170 hover win_edge_hit (NWIN^2 sort, bounded, only runs when drag<0 and !btn, so it is skipped on the release call itself because drag is still >=0 until :197)
    - :175 xover_mouse(...) (apps/x11; not traced)
    - :177 xdisp_mouse; :179 browser_mouse_down/release (only if the browser holds the button)
    - :180-196 g_ddrag/g_fdrag branches are not taken during a window drag
    - **:197 `if(drag>=0&&g_snap) apply_snap(drag,g_snap); drag=-1; g_resize=-1; g_snap=0; g_marq=0; changed=1;`**
    - graphics/ui/wm.c:62 apply_snap: pure arithmetic, sets WN.max=1, focus, z=++ztop. Always returns.
    - :199/:205/:233 skipped (btn=0). Returns changed=1.
12. :270-318 rest of process_input: kpop loop (bounded by key queue), browser_tick (when the browser is open; drops g_pi_depth), video_tick, read_clock every 10 ticks. Returns ch=1.

## D. After process_input, same loop pass (main.c)
13. :325-341 jc_step, inst_app_tick, xfull_tick, media_tick, upd_tick, crash_tick.
14. :346 usb_hotplug_poll, nic_watch, svc_tick, wifi_mgr_tick, nvd_poll, battery_tick (not traced).
15. :354 runs only if !ch, so it is skipped on the release frame (ch=1). From frame 2 on, video_fast, browser_media_fast and xdisp_fast become live, because desk_win_exposed (present_and_pump.c:2) now returns 1 with drag=-1. That is the "cache ON" transition.
16. :360 `compose_present()` (present_and_pump.c:60).

## E. Frame 1 after drag=-1 (present_and_pump.c:62 compose_present_body)
17. desktop_clicks.c:263 compose_build: gfx_frame_begin, then a full-screen gq_copy BG, desk_draw, sort over NWIN, then snap_preview skipped (drag=-1).
18. :271 `gfx_window(i)` for every open window (drivers/video/fb/gfx.c:239):
    - :243 `moving=(drag>=0)&&...` is now 0, so the dragged window's (and every window's) cached layer is NOT reused: the full re-render path runs.
    - :250 **first-ever kmalloc of L->opA/opB** (OPMAX*gq_op_size each) for any layer not captured before. mem/heap.c kmalloc is IRQ-off in v11.02.
    - :247 kmalloc-backed `g_alloc` for gs_snap (only if wheel+sset).
    - :251 `full=1`, because x!=L->dx after the move (or after apply_snap changed x/y/w/h).
    - :256 **draw_app(i)** (apps/system/logs.c:89 dispatcher) is the first real app draw since the drag began. For live_app windows (CAMERA, RECORDER, VIDEO, ...) moving was already 0 during the drag, so nothing changes for them. For non-live apps (Files, Terminal, Settings, Browser, etc.) this is the first draw_app in N frames, at a new position and possibly a new size after a snap. Any app draw that loops, waits or starts a device on first draw runs HERE.
    - :263 gq_replay over the whole region (bounded by ncap<=OPMAX). :265 diff loop (bounded by max(ncap,nold)).
19. present_and_pump.c:76 gq_copy gs_nc, then :86 blit_all, then desktop_clicks.c:291 **igpu_wait_flip** (drivers/video/intel/igpu.c:79): spin on surflive() with a 40 ms timeout. After 3 timeouts flips are switched off. Bounded.
20. :87 gfx_frame_end; :90 gfx_scene_settle; gfx_sync calls gq_sync: **GPU wait, and stop if no answer** (gfx.c gfx_sync). Bounded only if gq_sync has its own timeout (not verified).

## F. Frames 2-3
21. The input edge has already been consumed, so ch=0 unless something else changes. main.c:354 now runs the exposed fast paths every pass: video_fast (videoplayer.c:255), browser_media_fast (mouse_and_bookmarks.c:119, present_rect), xdisp_fast (xdisplay.c:79), xover_tick. They were all suppressed during the drag and are re-enabled at release. Frame 2 also runs gfx_window with valid layers (partial diff). The heartbeat at :362 runs every 50 ticks.

## Spots that could fail to return (ranked)
1. gfx.c:256 draw_app of a non-live app, run for the first time since the drag (and at a new size if snapped). This is the only new work that release triggers. Whether it hangs depends on the app's own draw function (not traced). Confidence it is the trigger: about 35%.
2. main.c:354 fast paths re-armed through desk_win_exposed on frame 2 (video/browser media/xdisplay). About 20%.
3. gfx_sync/gq_sync GPU wait at the end of the first full frame (full-screen replay). About 15%.
4. xhid_recover/xhid_arm, and kmalloc of opA/opB (IRQ-off) on the first full capture. About 10%.
5. main.c:308 cpu_idle with IRQs off and no armed wake. Generic, not specific to release.
mouse_step, apply_snap, in_btn_raw and hid_report_decode always return (pure and bounded).

## Suggested fix
Not verified, because the app draw functions were not traced in the time limit. Next step: instrument ml_mark around gfx_window/draw_app(i) and the :354 fast paths, so the lead sees which app's draw (or which fast path) stops making progress after drag=-1.
