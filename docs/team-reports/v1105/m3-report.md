# m3 report (v11.05) - input sub-step marker, stuck-box persistence, box erase
Branch v5-m3, files: core/crash.c, graphics/ui/input.c. Compile-checked both with ccheck.sh, no new warnings (crash.c 8, input.c 13 = baseline).

(1) Marker: globals in core/crash.c `const char* volatile g_in_what; volatile int g_in_win; char g_in_arg[64];`,
`in_mark(what,win,arg,n)` / `in_unmark(prev,win)`. input.c macro INM(what,win,arg,n,stmt) saves/restores the outer marker.
Marked: whole process_input ("input handling"), hk_poll ("keyboard shortcut"), Windows-key shortcuts, touchpad gesture,
mouse event, mouse click -> on_click, Start menu click/key, context menu click, dock click (target app), close button,
Settings title bar click, "Settings click on 'page N'" (pg_title is static in settings, so page number only),
"click in window" (app_click, with window name), every key press ("key 'x'" / Enter/Esc/#code, focused window),
"Terminal command '<line>'" on Enter in the terminal, Files context-menu run_cmd, browser_tick, video_tick.
Shown by cx_where, so both the BKL-wait box and the freeze box read e.g.
"last step: input and window actions (stage 7): Terminal command 'nvgsp 3d' (Terminal window)".
Panic/freeze reports (cx_build) also get an "Input action:" line.
(2) Persistence: no reserved memory survives warm reboot in this code base (checked), so: frz_bkl_wait copies both box
lines into static g_bkl_box (lock-free, plain stores). crash_tick (main loop, safe point) saves it as crash-report.txt
(+ CRASH.TXT on USB) with "lock became free again after N s" and toasts once the box has not been redrawn for >2 s.
`crash` prints the in-memory "Last 'stuck' box this session" first, then the file; `report` runs `crash`, so it is included.
Not covered: if the machine never recovers, nothing reaches disk (as before - photo needed).
(3) Erase: same crash_tick path calls fb_shadow_invalidate() + returns need=1 (full redraw), and resets g_frz_bkl_last
so a later stuck episode draws again.
Untested on hardware/QEMU (no build allowed). Note: NWIN is hardcoded 28 in cx_in_fmt; input.c has a _Static_assert guarding it.
