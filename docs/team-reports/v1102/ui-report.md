# ui report (v11.02) - graphics/, shell/, apps/, non-USB input
Time: 15 min hard cap. No code changed (nothing found that was both a confirmed bug and a safe small diff). No commit on b-ui.

## Checked, looks correct
- drivers/input/input.c:11-20 mouse button queue: bounded (g_mq_w-g_mq_r<MQ_N), overflow counted in g_mq_lost, consumer under cpu_irq_off (graphics/ui/input.c:266). No overflow corruption.
- drivers/input/ps2/ps2.c ps2_byte_fast/ps2_drain: 256 ring, bounded, mb[4] index safe even if g_mbytes changes mid-packet.
- net/http/http.c:246 kpush/kpop: bounded ring, drops when full (no hang).
- graphics/ui/desktop/desktop_clicks.c:289 blit_region2, present_and_pump.c:8/17 present_rect/nc_restore: clip negative and off-screen rects -> no OOB blit when a window/cursor is off-screen.
- graphics/ui/input.c:236 window drag clamps (nx/ny) keep windows partially on-screen; resize clamps min 240x160 -> no size-0 windows from the UI.
- apps/media/camera.c:30 cam_view: dw/dh==0 skips loops, no div-by-zero (uvc_w/uvc_h checked >0).

## Findings (not fixed, need owner of the path)
1. MEDIUM, conf high: apps/media/camera.c:46 camera_draw() calls uvc_start() (USB control transfers + stream setup) from inside the draw path (compose_build). Any long/blocked USB wait there stalls the compositor and, with g_comp_depth>0, desk_nested_step (present_and_pump.c:120) refuses to run, so the pointer stops too. On the owner's machine Camera auto-starts on open -> fits the "click Camera in Start freezes" report. Suggest moving the auto-start to the main loop tick (core/init/main.c:306 next to uvc_poll) or doing it once in camera_open after open_win. Same file :47 uvc_poll() also runs inside draw.
2. MEDIUM, conf medium: graphics/ui/input.c:189 on_click runs while g_pi_depth>0; any blocking work started from a click (Start menu launch, camera/mic open) makes desk_nested_step (present_and_pump.c:127) only move the cursor; kpop is never drained, keyboard looks dead until the call returns. If the launched call never returns, the whole desktop is frozen. Launch owner should keep blocking device opens out of the click handler.
3. LOW, conf medium: graphics/ui/desktop/present_and_pump.c:147-152 ui_pump() polls xHCI and repaints the cursor but never consumes mouse_dx/dy (busy_pump :135 does). During Wi-Fi/DHCP/WPA waits USB/PS2 mouse motion accumulates but the pointer does not move -> looks frozen. Fix: same dx/dy consume block as busy_pump.
4. LOW, conf low: drivers/input/ps2/ps2.c:107 mouse_feed -> in_btn_raw and mouse_dx+= run from ps2_drain in IRQ context without in_lock(); safe only because both sides hold the BKL (arch/x86_64/interrupts/interrupts.c:91-92). If any USB HID path updates mouse state without BKL, PS/2 + USB merge can lose button edges.
5. INFO: graphics/ui/desktop/present_and_pump.c:149 ui_pump calls touchpad polls but not vm_mouse_poll; fine on real HW.

## Tests
Code reading only; no build, no QEMU run (time cap).
