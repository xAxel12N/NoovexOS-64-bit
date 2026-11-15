# T report: click paths for Start / Start menu / taskbar / clock / tray (v11.06, read-only)

## Verdict
None of these click paths has a wait loop, a nested pump or an in_lock region. The "modal loop inside the click handler" theory is not in the code. I agree with L: in_lock is a leaf lock. The best candidates are on the USB input side and are new since v11.02. The main loop stays alive and only input dies, which explains "no box". Use a real-hardware A/B test (below) to decide between them.

## What runs on a still left click (press, then release)
- The press goes to graphics/ui/input.c:288. The queue is drained under in_lock_x, and the lock is released before any mouse_step call. Then mouse_step:183 calls xover_mouse (only if !g_launcher), and :184 calls INM("mouse click") -> on_click -> on_click_body:109.
  - :110 xtray_pop_click / xtray_bar_click (apps/x11/xtray.c:53,69). These only push a string to g_xtq. No loop.
  - Start button: :127 (classic) / :128 (modern) does `g_launcher^=1; launcher_search()` (desktop_clicks.c:48). launcher_search only touches lc_has, pyide_installed -> sysroot_exists, and xapps_count -> xapps_refresh, which is cached by key (xapps.c:66). No wait.
  - Open menu: :115 INM -> launcher_click (desktop_clicks.c:193) or classic_menu_click (:134). Results are lapp_open/open_win/launcher_power. open_win (wm.c:51) calls win_anim, which is a no-op `return` (present_and_pump.c:155), and gfx_win_event (gfx.c:203), which only sets flags.
  - Dock / task buttons: :146 focus_win (wm.c:57), classic_taskbtn_click (window_titles_and_bars.c:122), xwin_toggle. All are state flips.
  - Clock / tray: :123 `g_clockpop=0`, :127 `g_clockpop^=1`, g_qs (:118-122), g_notifpanel (:122), classic_tray_click (:119). All are state flips, with no I/O.
- The release goes to mouse_step:187-205. It only acts when g_ddrag / g_fdrag / drag / resize is set, and none of these is set by the clicks above.
- A grep of graphics/ui finds no `while(mouse_btn)`, no drag-threshold wait and no animation wait. busy_pump, desk_nested_step and ui_pump are not reachable from these handlers. Their only callers are the browser, net_poll/nd_poll, websock, update and Wi-Fi bring-up.
- The next frame (main.c:360, compose_present) has no loops in launcher_draw, classic_menu_draw, clockpop_draw or the taskbar code.

## Ranked candidates for "freeze, heartbeat alive, no box"

### 1. Late SET_PROTOCOL fires about 0.5 s after the FIRST click (new in v11.05/06). Confidence ~45%
drivers/usb/xhci/controller_init.c:125-139 (xhid_proto_poll), armed by xhid_note_boot :115 when a receiver sends 3/4-byte boot packets (the Biltema).
- It waits for 50 ticks of button quiet after the first press, then sends SET_PROTOCOL(report) from the main loop (stage xhci_poll).
- After that the receiver can change its report format in the middle of a session. If the decoder misreads the new format, all input from that receiver dies or a button sticks. Ticks and heartbeat stay alive, so there is no box.
- This fits "the first click (desktop) works, every later click (taskbar/Start) is dead". It was not present in v11.02, where the taskbar worked.
- Owner test: type `usbproto off` in the terminal before the first click, then click Start. A klog line "usb: asked receiver for full reports ..." after the first click confirms it.
- Fix: default it off.
  `int g_usbproto_off;` → `int g_usbproto_off=1;`  (controller_init.c:110)

### 2. A boot-format still press is classed "ambiguous" and lost (new in v11.05/06). Confidence ~30%
drivers/usb/xhci/hid_keyboard_mouse.c:59-67.
- Case: a receiver with report IDs whose mouse report ID 1 has axes wider than 8 bits (typical for combo receivers).
- A still left press arrives as boot packet `01 00 00`. Then declared=1, wide=1 and b1=b2=0, so amb=1 and rule1, sticky and clear are all 0. The fallback loop (:68) gives boot=0 because b[1]==b[2]==0.
- The packet is then decoded as report ID 1, with buttons read from b[1]=0, so the press is lost.
- A click with motion (a desktop marquee drag) still works. A still click on Start or a dock icon does nothing, which looks like a freeze.
- QEMU usb-mouse has no report IDs, so `usbsim biltema` CANNOT reproduce this.
- Owner test: does a perfectly still click on an empty desktop icon select it? Does a click while moving the mouse open Start?
- Fix: once boot packets have been seen, trust them.
  `amb=declared&&wide&&!b[1]&&!b[2];` → `amb=declared&&wide&&!b[1]&&!b[2]&&!L0->bseen;`

### 3. The USB poll guard can silently kill all USB input (new in v11.06). Confidence ~10%, worth a diagnostic
hid_keyboard_mouse.c:140-149 (xh_poll_busy trylock) plus usb_network.c:72.
- If the guard is ever left set, every xhci_mouse_poll returns at once. USB keyboard and mouse are both dead while the main loop runs, so there is no box.
- xh_poll_t0 is written but never read. I found no path that leaves the guard set, and the xHCI waits do not call pumps.
- Patch (diagnostic only, no forced release):
  `if(__sync_lock_test_and_set(&xh_poll_busy,1)){` → `if(__sync_lock_test_and_set(&xh_poll_busy,1)){ static u64 wl; if(g_ticks-xh_poll_t0>300&&g_ticks-wl>500){ wl=g_ticks; klog("usb: poll guard held over 3 s"); }`

### 4. The heartbeat is masked by busy_pump. LOW, because it is not on these click paths
present_and_pump.c:132 bumps g_ml_beat before its `in` and state guard. Every net_poll/nd_poll/websock/update wait loop therefore keeps the freeze box away, even when re-entered.
- Fix: move `{ extern volatile u32 g_ml_beat; g_ml_beat++; }` to just after the `if(in||g_state!=ST_DESKTOP||g_ticks==lastp)return;` line.

## Not the cause (checked)
- in_lock / in_lock_x: same result as L. These are leaf regions at input.c:288 and present_and_pump.c:127/135. mouse_step runs after unlock. hid_xfer->in_btn_raw never runs inside a locked region. A CPU 0 spin with IF=0 would have raised the NMI box.
- freeze_tick moved to v==32 on CPU 0 (interrupts.c). The I/O APIC is masked, so IRQ0 comes from the PIC on CPU 0, and the box still works.
- g_frz_hold set during a hang: the sysroot blk_get hold (sysroot.c:133) applies only to jobs on the nolock path, not to these UI paths.
- Since v11.02 the only UI changes on these paths are the INM/crumb wrappers (no lock, no loop; COM1 output is non-blocking), the marquee lines and draw_app_marked. None of them can block.

## Ask the owner (this decides between 1, 2 and 3)
1. After the freeze, does the pointer still move? Does the clock still tick? Does Caps Lock toggle?
2. Run `usbproto off`, then click Start.
3. Click Start while moving the mouse, versus a perfectly still click.
4. Run `usbhid` before and after the first click (it shows the format), and check klog for "asked receiver for full reports".
