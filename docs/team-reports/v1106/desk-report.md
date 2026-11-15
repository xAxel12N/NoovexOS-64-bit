# desk report (v11.06) — branch v6-desk

## Trace: left PRESS on empty desktop
- on_click: none of the popup branches fire; dock/window/edge hits miss; classic_icon_hit() is a stub (returns -1), desk_hit() is a stub (returns -1, code after `return` dead), so the double-click block (g_ticks-lt, static lt=0) is unreachable and desk_place_file is never reached from a click. desk_draw() also returns at once. -> g_marq=1, marq_x0/y0=px/py. Nothing else.
- While held: mouse_step set changed=1 on EVERY call (process_input calls mouse_step 2x per queued event + 1x), so a full compose_present per event even with no motion. Not a hang.
- marq_draw: returns if w<2||h<2 (a still click draws nothing). GPU path gq_blend(g_rt=gs_scene) -> norm() clips to surface and drops empty rects; CPU path clips per pixel. No division, no loops beyond w*h.
- gfx.c: nothing reads g_marq; 'moving' needs drag==i, so the marquee leaves window caching alone. gq srec (partial redraw): sr_align/gq_srec_end loops are bounded (n<=4096, 12 iterations max). No wait loops other than gfx_sync->gq_sync->bsubmit (driver submit, pre-existing).
- desk_win_exposed/desk_popups_clear returning 0 only make xdisplay/video/browser fall back to full redraw (xdisplay: xd_clear, return 1; browser: return 0). No loop.
- Release: `!btn&&mouse_btn_prev` clears g_marq; g_ddrag path (DGW/DGH constant divisors) not taken.
- Conclusion: no infinite loop, div-by-zero, null pointer, recursion or wait found in the graphics side of the first left click. The freeze is most likely in USB/HID (M1/M2: SET_PROTOCOL/SET_IDLE re-sends from the poll path, xh_event_w with IRQs off) rather than graphics.

## Fixes (graphics/ui/input.c)
1. Stuck g_marq: the early-return paths in mouse_step (acct dialog, screen off, pm, screensaver) set mouse_btn_prev=btn without clearing g_marq, so if the release arrived there g_marq stayed 1 forever (desk_win_exposed/desk_popups_clear return 0 for good, marquee frozen on screen). Now cleared at the next event when both prev and current button are up.
2. `if(g_marq&&btn) changed=1` -> only when the pointer moved (avoids a full recompose per still event while held).

## Not fixed (listed)
- ux_mouse_push (fs/vfs/unixfs.c) has `while(dx||dy||1)` — terminates via break, but odd.
- desk_hit/classic_icon_hit are stubs; desktop icons are disabled (dead code after return).
- compose cost on CPU path: full-screen per-pixel mix in marq_draw for a large rectangle (slow, not a hang).
