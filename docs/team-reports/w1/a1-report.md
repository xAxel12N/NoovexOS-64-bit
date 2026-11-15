# a1 — Architecture analysis: why NoovexOS 64 keeps freezing "with no explanation"

Repo: /home/claude/src/NoovexOS-64-bit-main (v11.06). Read-only analysis. All line numbers are v11.06.

## 1. TL;DR

- The whole desktop is **one cooperative loop on CPU 0** (core/init/main.c:296-374). Every click, key, terminal command,
  Wi-Fi scan, browser call and disk wait runs **inline** in that loop or in a callee of it.
- Long operations stay responsive with **nested pumps** (busy_pump, desk_nested_step, ui_pump, kidle, us_pump,
  desk_nv_progress). Each one runs a small copy of the main loop *inside* the handler, and **each one increments
  `g_ml_beat`**, the only thing the freeze watchdog looks at. So any handler that sits in a pump loop is **by
  construction invisible** to the freeze box. The NMI box only fires if CPU 0 has interrupts *off* for 3 s, which pumps
  never do. The owner's taskbar/Start freeze (no box at all) fits this exactly.
- Separate from the watchdog, the **slowdown finder is mislabeled**: stage 6, which `ml_lag` ignores as "idle wait",
  actually holds uvc_poll, bench_tick, recorder_tick, **app_io_tick (new in v11.03)** and **xhci_mouse_poll**. The real
  idle wait is billed to stage 14, "updates (boot partition)". A slow USB poll or app_io_tick is never reported, and a
  freeze box raised during them would say "last step: idle wait".
- Fix direction (staged below): make the watchdog honest first (S0/S1, which would have diagnosed the current bug), then
  remove re-entrant input handling, add a deferred-work queue, move long commands onto task stacks that *yield* in place
  of pumping, and put all input behind one lock and one consumer.

## 2. The real architecture

### 2.1 Main loop (core/init/main.c:296-374), CPU 0 only, holds the BKL except while idle

| ml stage | what actually runs (line) | notes |
|---|---|---|
| (top) | pit_poll_tick, alive log, jc_next_wdl, sx/swap_poll, cpupower (297-303) | not timed |
| 1-3 | `g_ml_beat++`, then tp_poll, etp_poll, syn_poll, vm_mouse_poll (304) | the only beat that *should* exist |
| 4-5 | acpi_poll, snd_poll, ctemp_poll every 2 ticks (305) | |
| **6** | uvc_poll, bench_tick, recorder_tick, **app_io_tick**, quake/writer blink (306) **and xhci_mouse_poll (307)** | `ml_lag` skips stage 6 (main.c:184), so none of this is ever reported as slow |
| **14** | idle: bkl_drop, cpu_idle until IRQ, bkl_take (308) | labeled "updates (boot partition)" in `lag_stage_name`. Stage 14 is reused at 343 for upd_tick |
| 15 | BKL re-take (309), sleep request, pack_poll, xt gen, g_open_req→nvx_open_url_tab, fs write-back nvx3_sync_all (310-316) | |
| **7** | **input dispatch**: process_input / live_input / bootmenu / … (317-325) | all clicks and keys run here, inline |
| 10 | jc_step, Linux programs (328) | |
| 11 | inst_app, live jc_step, Ctrl+Alt hotkeys, xfull, fb0, media_tick (329-342) | |
| 14/19 | upd_tick, crash_tick (343-344) | |
| 8,16,17,18 | usb_hotplug_poll, nic_watch, svc_tick, sysfiles/focus_tick, wifi_mgr_tick (346) | |
| 9 | firefox_tick, nvd_poll, acct, fid, battery, USB-arrived→open_win (347-352) | |
| 12-13 | video/browser/xdisp fast paths, then compose_present / present_topbar / present_cursor (353-373) | |

`bkl_yield()` calls between stages (315, 326, 329, 345, 353, compose_present_body:65) let APs grab the BKL.

### 2.2 Work done inline in input handlers

process_input (graphics/ui/input.c:245) → mouse_step (167) → on_click (107/108) → for example:
- Taskbar: `xtray_bar_click` runs first for every click at `py>=g_tby` (input.c:109) → `xt_ev(...)` (apps/x11/xtray.c:53-58),
  i.e. X11 tray traffic from inside the click handler.
- Start button: `g_launcher^=1; launcher_search()` (input.c:126/127). launcher_search walks LAUNCH, xapps_count/app_name,
  FS[] (graphics/ui/desktop/desktop_clicks.c:48-57). Start items: `lapp_open`, `open_win`, `launcher_power`,
  `launcher_open_result`→`py_open_slot`/`editor_open` (desktop_clicks.c:193-205).
- open_win → focus_app_started, win_anim (stubbed), **gfx_win_event** (graphics/ui/wm.c:51-54). close_win → video_stop,
  camera_closed, quake_closed, browser_media_pause…
- Wi-Fi icon → `wifi_gui_scan` (input.c:130,135).
- Context menu "Extract all" unzips every entry inline (input.c:448-452), "Run" calls `run_cmd` inline (input.c:445).
- Keys: Enter in Terminal → term_key → **run_cmd** (shell/commands/terminal_keys_and_render.c:17,
  prompt_and_dispatch.c:204), which runs every builtin synchronously: `wifi scan` → `wifi_scan_blocking` (spins
  `while(sc_on){scan_step();ui_pump();}`, net/wifi/wifi_mgr.c:193), the rtw bring-up channel loops with
  `rtw_delay_us(60000); ui_pump()` (processes_devices_wifi_bringup.c:332/353/415), apt, update, and so on.
- Modal prompts inside commands: `sh_readpw`, `sh_yesno` (prompt_and_dispatch.c:2-20),
  terminal_keys_and_render.c:229, processes_devices_wifi_bringup.c:56 all do `for(;;){c=kpop(); if(c<0){kidle();continue;}}`.
  That is a private event loop that **steals keys** from the desktop, while the desktop is frozen behind it.
- `browser_tick()` is called from inside process_input with `g_pi_depth--` around it (input.c:337), specifically so the
  nested pump below may **re-enter process_input**.

### 2.3 BKL and the APs

- arch/x86_64/cpu/percpu.c:23-53: a ticket lock, recursive per owner. **`bkl_lock` disables IRQs before spinning**
  (percpu.c:26-27), so a CPU 0 that waits on an AP-held BKL loses its timer ticks. (This case *is* visible: the
  frz_bkl_wait box after 5 s, or the NMI box.)
- APs (arch/x86_64/smp/smp_sched.c:79-89, ap_loop): take the BKL → jc_pick/jc_enter (Linux user jobs). Also
  wk_ap_turn (web workers) and smp_work_step (pure compute). AP preemption re-takes the BKL (smp_ipi/ap_timer, 63-71).
- Net effect: **all kernel/UI state is BKL-serialized, and the UI is pinned to CPU 0**. SMP gives user jobs
  parallelism, never the desktop. A long handler on CPU 0 also holds the BKL (except during bkl_yield), so APs stall
  behind it.

### 2.4 Nested pumps (the core problem)

| pump | file:line | beats `g_ml_beat` | polls | handles input? | guard |
|---|---|---|---|---|---|
| desk_nested_step | graphics/ui/desktop/present_and_pump.c:118-130 | yes (121) | tp/etp/syn/vm/xhci, acpi, snd, media | **calls process_input() when g_pi_depth==0** (128), otherwise only moves the cursor | `static in`, `g_comp_depth` |
| busy_pump | present_and_pump.c:131-146 | **yes, before its guard** (132), so it beats even when it does nothing | xhci only | no; only cursor + Esc flag | `static in`, once per tick, ST_DESKTOP only |
| ui_pump | present_and_pump.c:147-152 | **no** | tp/etp/syn, xhci | no; cursor only | none |
| kidle | net/http/http.c:249 | yes | xhci only (no touchpad/PS2 parser poll) | caller kpop()s | none |
| us_pump / up_rd / up_wr | core/update.c:93,105,108,201,210 | yes | — | — | |
| desk_nv_progress | present_and_pump.c:174-175 | yes, unconditionally | — | — | |
| g_wait_pump (=busy_pump) | set main.c:295; called in drivers/storage/ahci.c:130, net/stack/tcpip.c:26, apps/internet/browser/websock.c:39, core/update.c:400 | via busy_pump | | | |
| g_wpa_pump (=ui_pump) | lib/crypto/nvxwpa.c:74, processes_devices_wifi_bringup.c:186 | no | | | |
| nwk_busy (browser engine) | apps/internet/browser/glue/startup.c:35-48 | via desk_nested_step / busy_pump | | re-enters process_input | g_web_nest |

`g_pi_depth` (input.c:243-245) is the only re-entrancy marker, and it is deliberately broken at input.c:337. xhci also
reads it (controller_init.c:128) to skip SET_PROTOCOL while input is being handled.

The heartbeat can be fed from inside a handler at **8+ sites**: present_and_pump.c:121,132,175; http.c:249; update.c:93,105,108,201,210.

### 2.5 Polled drivers

- xHCI is poll-only, no MSI (drivers/usb/xhci/controller_init.c:72). `xhci_mouse_poll` (hid_keyboard_mouse.c:140-148)
  **silently returns if `xh_poll_busy` is set**. If anything inside it (xhid_recover, xh_ctrl → xh_event_w spin loops,
  controller_init.c:244-250) waits long, every nested pump's xhci poll is a no-op, so USB input is dead while the beat
  continues. If `xh_poll_busy` ever leaks (fault redirect/longjmp), USB input is dead forever with no message.
- PS/2 keyboard and touchpad are IRQ-fed (drivers/input/ps2/ps2.c:89-99). The touchpad parsers are polled.
- Keyboard ring `kpush`/`kpop` (http.c:246-250) is **unlocked**, written from the PS/2 IRQ and the USB poll, and read by
  **28 kpop() call sites** (dispatcher, modal prompts, syscall waitkey sys/native/syscall.c:125, nvxlc_waitkey
  input.c:63, skull screen…).
- Mouse state: in_lock (drivers/input/input.c:8-9) is a real non-recursive spinlock with IRQs off (v11.06). It is taken
  in process_input (input.c:288), desk_nested_step (127) and busy_pump (135). Today each critical section is short and
  calls nothing, so a deadlock needs re-entry *from inside* a held section (an IRQ can't: IRQs are off). If it happened,
  CPU 0 would spin with IRQs off and the **NMI box would fire**. The owner saw no NMI box, so the in_lock deadlock
  theory is **unlikely** for the current bug (assuming SMP actually came up, i.e. /etc/smp is not "off").

## 3. Why this design keeps producing "freeze, no explanation"

1. **The liveness signal measures the wrong thing.** `freeze_tick` (core/crash.c:363-366) treats "g_ml_beat changed"
   as healthy. Every pump changes it. A handler stuck in `while(cond){ pump(); }` looks perfectly healthy forever.
   busy_pump even beats when its guard makes it a no-op (wrong state, re-entered, same tick).
2. **The second watchdog only covers IRQs-off.** nmiwd_check (crash.c:324-335) fires when CPU 0's ticks stop. A
   pumping loop keeps ticks running. So the two boxes cover "main loop dead with IRQs on and no pump" and "IRQs off",
   but not "inside a pump loop" (the common case).
3. **Silent off-switches.** `g_frz_hold=1` disables both watchdogs and is set or restored by hand in 9 files (crash.c,
   update.c, power.c, syscall.c waitkey, sysroot.c, deb_archive.c, install_files.c `ip_run` (unconditionally resets to
   0), usb_clone_service.c, browser frames.c). `g_preempt_on` also disables freeze_tick. Any early return or leaked
   flag turns the watchdog off for good. A waitkey syscall that waits for a key no one can deliver is a by-design
   silent freeze.
4. **Re-entrancy is everywhere and guarded by `static int in` flags that silently skip.** desk_nested_step can run
   process_input → on_click → open_win/launcher/xt_ev → … → a pump → desk_nested_step (skipped by `in`). The inner
   handler then runs with **no input polling and no presentation**, while the beat may still come from busy_pump/kidle
   lower down. The screen and pointer freeze, the watchdog stays quiet.
5. **Pumps disagree about what they do.** ui_pump polls the touchpad but doesn't beat. kidle beats but polls only xhci.
   busy_pump moves the cursor but never presents more than the cursor. So the same wait looks different depending on
   which pump the callee happens to use: pointer moves but nothing else redraws, or nothing moves but the beat lives.
6. **Modal private event loops** (sh_readpw/sh_yesno/waitkey/nvxlc_waitkey) own the keyboard while the desktop
   doesn't run. If their window isn't focused or visible, the user sees a frozen desktop that is in fact "waiting for
   Enter".
7. **Instrumentation blind spots:** stage 6 is excluded from lag and holds xhci_mouse_poll plus app_io_tick (v11.03).
   Stage 14 is misnamed. `cx_where` therefore points at the wrong place. Breadcrumbs (`crumb`) are only shown by the
   NMI box, which never fires in this class of hang.
8. **Polled USB with a silent busy-skip** (2.5) makes "input dead, everything else alive" a natural failure mode with no
   signal.

### Relevance to the owner's taskbar/Start freeze (hypothesis, not verified)
From the architecture alone, the taskbar/Start path differs from the empty-desktop path in **xtray_bar_click → xt_ev**
(only for clicks in the taskbar strip), **launcher_search / launcher draw** (Start), and **open_win → gfx_win_event**
(menu items). Any of these that reaches a pump-fed wait (busy_pump through g_wait_pump in network/disk waits, kidle,
desk_nested_step) produces exactly "no red box". S0 + S1 below would name the handler and the pump on screen in one
build. Recommend landing them before more blind fixes.

## 4. Staged plan (each stage 2-4 agent-hours; files listed)

Order: **S0 → S1 (visibility; ship together as v11.07)**, then S2 (no nested input) → S3 (defer queue) → S4a/S4b (tasks)
→ S5 (input lock rule) → S6 (USB poll) → S7 (gate). Each stage leaves the OS working.

### S0 — Honest heartbeat (2-3 h)
- Only main.c:304 may write `g_ml_beat`. Replace every other `g_ml_beat++` with `nest_beat(name)`, which sets
  `g_nest_beat++`, `g_nest_name=name`, `g_nest_t0` (first entry since the last main beat).
- freeze_tick: if the main beat is stalled ≥5 s **while** `g_nest_beat` moves, draw a box: "NoovexOS is busy inside
  <g_nest_name> for N s, called from handler <g_in_what>(<g_in_arg>) — Esc cancels if supported", plus `crumb_dump`
  lines (currently NMI-only).
- busy_pump: move the beat after its guard.
- Fix the stage map: give the idle wait its own stage id (e.g. 6→"idle wait" really at main.c:308). Move
  uvc/bench/recorder/app_io_tick/xhci into a new counted stage (e.g. 20, "camera, recorder, app I/O, USB input poll").
  Make the stage arrays [24].
- Files: core/init/main.c, core/crash.c, graphics/ui/desktop/present_and_pump.c, net/http/http.c, core/update.c,
  include/nvx/core/crash.h.

### S1 — Per-handler time budget, always visible (2-3 h)
- Extend `INM` (graphics/ui/input.c:33) and `in_mark` (crash.c:59) to record `g_in_t0=hr_rdtsc()` and a depth stack (8
  entries: name, win, t0). On exit, if the handler took >50 ms, log `"slow handler: <name> <ms>"` into the lag ring.
  Add the handlers to the `lag` command output.
- freeze_tick (timer IRQ, already draws directly): if the top handler has run >2 s, show a small amber box "still
  handling <name> (N s)" whether or not any beat moves. At >10 s, show the full red report and save CRASH.TXT (reuse the
  cx_save path).
- Replace raw `g_frz_hold=` with `frz_hold_push(name, max_s)` / `frz_hold_pop()`. The hold auto-expires (default 120 s)
  and its name shows in the box. Convert all 9 files.
- Files: graphics/ui/input.c, core/crash.c, core/init/main.c (lag_report), core/update.c, core/power.c,
  sys/native/syscall.c, fs/vfs/sysroot.c, fs/pkg/apt/deb_archive.c, core/init/installer/install_files.c,
  core/init/installer/usb_clone_service.c, apps/internet/browser/glue/frames.c.

### S2 — No input handling inside handlers (3-4 h)
- desk_nested_step: never call process_input. Delete the `g_pi_depth--/++` around browser_tick (input.c:337).
  Pumps may only poll drivers, move the cursor, present cursor/toasts, and set the Esc flag. Clicks and keys **stay
  queued** until the main loop's stage 7.
- Give every pump one implementation, `pump_wait_step(name)`: poll tp/etp/syn/vm/xhci, nest_beat(name), cursor,
  present_cursor at most every 2 ticks. Make busy_pump, ui_pump, kidle, desk_nested_step thin wrappers so behavior is
  uniform.
- Add `assert_not_in_handler_reentry`: process_input checks `g_pi_depth==0` on entry. If not, it counts, logs, and
  returns (visible in `lag`), rather than recursing.
- Files: graphics/ui/desktop/present_and_pump.c, graphics/ui/input.c, apps/internet/browser/glue/startup.c,
  net/http/http.c, net/wifi/wifi_mgr.c (ui_pump users), include/nvx/graphics/ui/desktop.h.

### S3 — Deferred work queue + handler budget rule (3-4 h)
- New `core/defer.c`: a ring of `{fn, arg, name, t_enq}`. `defer(fn,arg,name)` can be called from handlers. A
  main-loop stage (new id, after stage 7) drains items while elapsed < 8 ms per loop. Each item is timed with the S1
  machinery, so a slow item is named on screen.
- Rule: **a click/key handler only changes UI state and enqueues work**. Convert first the paths the owner hits:
  xtray_bar_click/xtray_pop_click → defer xt_ev (apps/x11/xtray.c); Start: launcher_search, lapp_open, launcher_power,
  launcher_open_result (graphics/ui/desktop/desktop_clicks.c); open_win's gfx_win_event/focus_app_started and
  close_win's app teardown (graphics/ui/wm.c); wifi_gui_scan (graphics/ui/desktop/app_tiles_and_icons.c:261, input.c:130/135);
  ctx menu Extract/Run (input.c:444-452); the quick-settings Wi-Fi item (window_titles_and_bars.c:108).
- Files: core/defer.c (new), include/nvx/core/defer.h (new), core/init/main.c, units.list (or whatever lists sources),
  plus the files above.

### S4a — Kernel tasks (cooperative, own stack) (3-4 h)
- New `core/proc/ktask.c`: up to 4 tasks, each with a 64-128 KB guarded stack (reuse the pattern of
  `nwe_call_stack` + `mm_guard_range_bottom` in apps/internet/browser/glue/startup.c:50-56; k_setjmp/k_longjmp exist in
  arch/x86_64/boot/userspace.S:105-130; add a small `ktask_switch` there).
- `ktask_yield()` switches back to the main loop. The main loop runs a "tasks" stage with a 4 ms budget per task, so it
  keeps beating and presenting, and input is handled by the one dispatcher.
- **Make `g_wait_pump`/`ui_pump`/`kidle` call `ktask_yield()` when running on a task stack.** Then existing
  synchronous code (ahci nd_poll, tcpip nd_poll, websock w_wait_tick, wifi_scan_blocking, rtw scan loops, update
  download, WPA PBKDF2) becomes non-blocking without rewriting it. A pump outside a task falls back to
  pump_wait_step + nest_beat (still visible from S0).
- Cancel flag per task (`ktask_cancelled()`), checked at yield. Ctrl+C and Esc set it.
- Files: core/proc/ktask.c (new), include/nvx/core/proc/ktask.h (new), arch/x86_64/boot/userspace.S, core/init/main.c,
  graphics/ui/desktop/present_and_pump.c, net/http/http.c, crash.c (box shows "task <name> running N s" in place of a
  freeze).

### S4b — Long terminal commands and modal prompts as tasks (3-4 h)
- term_key Enter → `ktask_start("cmd: <line>", run_cmd_task, line)`. While it runs, the terminal shows output, and
  keys for it go to a per-task key queue filled by the dispatcher (not kpop).
- sh_readpw/sh_yesno/terminal_keys_and_render.c:229/processes_devices_wifi_bringup.c:56/syscall waitkey/nvxlc_waitkey
  become `task_getkey()`, which yields until the dispatcher routes a key to that task. Outside a task, they refuse with
  a message rather than spinning.
- Builtins that are quick stay inline (a whitelist).
- Files: shell/commands/terminal_keys_and_render.c, shell/commands/prompt_and_dispatch.c,
  shell/commands/processes_devices_wifi_bringup.c, shell/commands/system_disks_packages_gpu.c, sys/native/syscall.c,
  graphics/ui/input.c (nvxlc_waitkey/sleep), net/wifi/wifi_mgr.c.

### S5 — One input-lock rule (2-3 h)
- **Rule:** all input producers (PS/2 IRQ, xhci poll, vm mouse, touchpad parsers) push events into **one** MPSC ring
  under in_lock. **Only the stage-7 dispatcher pops.** It routes to the desktop, the focused task's key queue, or a
  modal owner. Nobody else calls kpop or reads mouse_dx/mouse_btn. Nothing is called while in_lock is held.
- Move kbuf/kpush/kpop (net/http/http.c:246-250) under in_lock. Make kpop dispatcher-private and give other callers
  task_getkey().
- in_lock gets debug owner tracking: cpu, caller address, t0. Re-entry on the same CPU → red box naming both callers
  (no silent deadlock). Spinning >100 ms → red box.
- Files: drivers/input/input.c, net/http/http.c, graphics/ui/input.c, graphics/ui/desktop/present_and_pump.c,
  drivers/usb/xhci/hid_keyboard_mouse.c, drivers/input/ps2/ps2.c, sys/native/syscall.c, core/crash.c, core/init/main.c
  (skull screen loop 290).

### S6 — USB poll that can't silently stop (2-3 h)
- xhci_mouse_poll: count `xh_poll_busy` skips. If busy since >1 s, show "USB input poll stuck in <step>" via a
  crumb-style marker set in xh_event_w / xhid_recover. Release the busy flag in the fault-redirect paths.
- Cap xh_event_w iterations by time, not count. Defer `xhid_recover`/SET_PROTOCOL to the S3 queue rather than running
  it inside the poll.
- Optional later: an LAPIC-tick flag "poll due", so tasks/pumps poll on a fixed cadence (still polled, no MSI needed).
- Files: drivers/usb/xhci/hid_keyboard_mouse.c, drivers/usb/xhci/controller_init.c,
  drivers/usb/xhci/storage_fat_and_hubs.c (msd waits), drivers/usb/xhci/usb_network.c:72, core/crash.c.

### S7 — Gate so the pattern can't come back (2 h)
- Add a `tools/check.py --only freeze` group that fails on: `g_ml_beat` writes outside core/init/main.c; raw
  `g_frz_hold=`; `kpop()` outside the dispatcher/http.c; `process_input(` outside main.c/input.c;
  `compose_present()`/`blit_all()` inside apps/, shell/, net/, drivers/ (13 sites today; allow-list the existing
  progress screens); `while(` loops containing `cpu_idle()` with no `ktask_yield`/`pump_wait_step` in the same statement.
- Files: tools/check.py (+ its test list).

## 5. What already exists and can be reused (don't re-propose)
- Stack switching for long work: browser engine `nwe_call_stack` + guarded stack + fault redirect
  (apps/internet/browser/glue/startup.c). Use it as the template for ktask.
- Background state machines: apps/system/services.c `svc_tick` (timed per service, shown in `lag`).
- AP work queue for pure compute: arch/x86_64/smp/smp.c:38-50 (not usable for kernel-state work under the BKL).
- Input markers `INM`/`in_mark`, breadcrumbs `crumb()`, `lag` command, frz/NMI/BKL boxes (core/crash.c). S0/S1 extend
  these and don't replace them.
