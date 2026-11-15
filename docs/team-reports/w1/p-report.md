# P: report (main loop keeps running but the screen or input looks dead after a Start or taskbar click)

Read-only review of v11.06. Ranked from most to least likely. Paths are relative to the repo root.

## Verdict on the hypothesis
- **"Screen is never presented again": unlikely.** I found no flag, lock or counter that can stop presenting for good.
  - `compose_present_body` (graphics/ui/desktop/present_and_pump.c:62-92) skips a frame only when nothing is damaged and the cursor has not moved (`skip=part&&g_dmw<=0&&cxlast==mx&&cylast==my`).
  - `pm_screen_off_draw` returns early only when the screen is off.
  - srec damage tracking (drivers/video/fb/gq.c:218): any direct write to SCENE goes through `scene_ptr` → `gfx_sync` → `sr_abort`, which falls back to a full frame. The next frame also invalidates, because `g_scene_wr` changed.
  - The igpu flip wait does not apply: the i5-11400F has no iGPU. NVIDIA drawing is opt-in.
  - The main loop forces `compose_present` whenever `g_launcher`, `g_ctx`, `g_submenu` or `g_qs` is open and the cursor moves (core/init/main.c:376). With a menu open it presents *more*, not less.
- **"A frame never finishes": not found.** `launcher_draw`, `classic_menu_draw`, `lgrp_refresh`, `draw_dock`, `xtray_*` and `wifi_panel_draw` have no loops that can run forever. Every `LG_BASE` row ends in -1, and the `pw_str` trim loop ends.
- **"Input dropped while a popup is open": no permanent drop.**
  - `in_mq_clear` (main.c:327) runs only when `g_state!=ST_DESKTOP`. The only Start-menu path that changes the state is Log Out (`launcher_power(0)` → `acct_ui_lock`).
  - Keys are swallowed while `g_qs`, `g_clockpop` or `g_notifpanel` is open (graphics/ui/input.c, key loop: `if(g_qs||g_clockpop||g_notifpanel){...continue;}`), and go to `launcher_key` while `g_launcher` is open. Both are by design, and Esc closes them.
  - `launcher_click` closes the menu on any click outside it.

## 1. (TOP) Nested pump loops keep the heartbeat alive but cannot handle clicks or keys and never recompose. This matches "frozen, no box".
- busy_pump (present_and_pump.c:132) does `g_ml_beat++` *before* its `in` / `g_state` / same-tick guard, so every call feeds the freeze watchdog, even a re-entered one. `desk_nested_step` (:120) and `kidle` (net/http/http.c:249) also feed it.
- `freeze_tick` (core/crash.c:366) resets on any beat. A modal wait that pumps therefore **never** shows the red box.
- A click runs inside `process_input`, so `g_pi_depth>0`. In that state `desk_nested_step` (:127) only moves the pointer. It never calls `process_input` or `kpop`, so clicks and keys pile up and are never handled. `busy_pump` and `ui_pump` only call `present_cursor`, never `compose_present`. Result: the cursor may still move, the picture stays frozen, nothing reacts, and no box appears.
- Click paths from the Start menu or taskbar that can block like this:
  - `lapp_open` / `launcher_open_result` → `open_win` / `app_launch` / `pyide_open` (desktop_clicks.c:72-78, :102)
  - the taskbar Files button `open_win(W_FILES)` (input.c:127, `px>=g_lbx_end-96`)
  - the classic quick-launch `focus_win(QW[q])` (input.c:126)
  - `classic_taskbtn_click`
  - opening the browser, whose `web_call` → `nwk_busy` → `desk_nested_step` / `busy_pump` (apps/internet/browser/glue/startup.c:35-46)
- **Fix A (diagnosis, do first).** Add a separate top-level beat `g_ml_top`, incremented only at main.c:304. In `freeze_tick`, if `g_ml_top` has not moved for 5 s while `g_ml_beat` keeps moving, draw a smaller box: "busy for N s inside <g_in_what>", plus the breadcrumbs (`crumb_dump`). `g_in_what` is already set by `INM("Start menu click")`, `"dock click"` and so on, so the box will name the handler.
- **Fix B.** In `busy_pump`, move `g_ml_beat++` below the `if(in||g_state!=ST_DESKTOP||g_ticks==lastp)return;` guard. With Fix A, nested pumps should feed only `g_ml_beat`, not `g_ml_top`.
- **Fix C (usability).** In `desk_nested_step` with `g_pi_depth>0`, at least handle Esc (`kdown[0x01]`): set `g_busy_esc=1` for any focus, not only `W_BROWSER` (:126). Then a stuck modal wait can be cancelled.

## 2. USB side, outside my area but it fits "beat alive, looks frozen, no box": late SET_PROTOCOL switch after the first click
- drivers/usb/xhci/controller_init.c:125-140, `xhid_proto_poll`, is called from `xhci_mouse_poll_in` on the main loop.
- About 0.5 s after the buttons go quiet (`XHID_BTN_QUIET=50`, using `mouse_press_t`), it sends SET_PROTOCOL(report) to a receiver that sends boot packets. That is exactly the Biltema 248A:16BB (`xhid_note_boot`, :115).
- After the switch the report layout changes. If the decode (`hid_report_decode`, hid_keyboard_mouse.c:57-75) then misreads the real reports, pointer and clicks die while the loop keeps running.
- This is tied to the first click, not to the taskbar as such. It is a cheap test on the owner's PC: run `usbproto off` (that is `xhci_usbproto(0)`), or write NMIWD-style flag files, then click Start.
- Also seen: `hid_report_decode` updates `mouse_dx`, `mouse_dy` and `mouse_scroll` without `in_lock` (hid_keyboard_mouse.c:72, 84). That is a race, not a deadlock. Fix: wrap those updates in `in_lock_x` / `in_unlock_x`, or route them through `in_rel` / `in_scroll`.

## 3. Low: the v11.06 spinlock
- `in_lock` (drivers/input/input.c:8) does cli, then spins, so a holder always has IF=0.
- The holders that take `in_lock_x` (input.c:288, present_and_pump.c:127/135) do no work while holding it that can re-enter: no `xhci_mouse_poll` and no `in_btn_raw`. PS/2 and xHCI callers run with IF=0 on the holder's CPU.
- I found no re-entry. A real deadlock would stop CPU 0's ticks and should trigger the NMI box, not "no box".

## 4. Low: `xover_mouse` eats taskbar clicks
- input.c:183: when no launcher is open and an X overlay window grabs the mouse, `btn` is reset, so `on_click` never runs for taskbar clicks. The result is "nothing happens", not a freeze.
- Fix: skip `xover_mouse` when `my>=g_tby`.

## Checked and not a cause
- v11.03 gfx.c `moving=(drag==i)` and `draw_app_marked`: only timing and logging.
- crash_tick red-box erase: forces one full frame through `need=1` and `fb_shadow_invalidate`, which is harmless.
- `app_io_tick`: only sets `ch`.
- `crumb()` serial output: no busy-wait; it drops the write if THRE is not set.
- `desk_popups_clear` and `desk_win_exposed` returning 0: only push xdisplay, video and browser into full-frame mode.
