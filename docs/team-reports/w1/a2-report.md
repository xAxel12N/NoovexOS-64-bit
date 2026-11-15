# a2: v11.00 (4296f5df) to v11.06, reviewed as one body of work (read-only)

Scope: `git diff 4296f5df..v11.06` minus tools/, tests/ and docs. That is 90 commits and about 3.3k lines of code diff. I read the whole core diff (arch, core, drivers/input, drivers/usb, graphics, mem) and the apps diff, and I skimmed net/wifi and web. I did not repeat R1, R2 or R3 (docs/team-reports/v1106/r*-review-report.md). Where they already cover a point, I give their reference.

## Bottom line
1. The red-box design cannot see the failure the owner most likely has. Here CPU 0 is alive with IF=1, ticks keep running, and some code keeps feeding `g_ml_beat` from somewhere other than the main loop. Three weak spots stack up (ranked 1 below): the heartbeat is fed by nested pumps and kidle, the NMI box only fires when IF=0, and `g_frz_hold` is a shared plain flag that turns BOTH watchdogs off. Fix this before more freeze hunting. Otherwise the next real-hardware run will again tell us nothing.
2. The best v11.03+ candidate that is click-correlated is the **late SET_PROTOCOL** (v11.05/v11.06, `xhid_proto_poll`). It fires once, at least 500 ms after the first button release, as soon as boot packets have been seen. From then on the receiver changes report format mid-session. If the receiver then misbehaves (stops sending, or sends something our heuristics misread), the pointer is dead and the OS "freezes" with no box. On the owner's flow this hits the click after the first one. Cheap test for the owner: boot-shell `usbproto off`, then `desktop`, then click Start. Check whether `usbproto` is persisted first; I only saw the runtime switch, shell/commands/network_and_apps.c:213.
3. I found no in_lock re-entry path. All `in_lock_x` regions only touch `mouse_*` and `in_mq_*_locked`. Also, an in_lock deadlock spins with IF=0, which stops ticks, so it WOULD give the NMI box, unless `g_frz_hold` happens to be set (see 1c). So it is unlikely to be the cause.

## Ranked risks

### 1. Watchdog blind spots (safety, HIGH). This is why there is "no red box"
a. **The heartbeat is fed by things that are not the main loop.** These sites feed it: `busy_pump` (graphics/ui/desktop/present_and_pump.c:132, beats BEFORE its `in`/same-tick guard, so even a call that does nothing feeds it), `desk_nested_step` :121, `desk_nv_progress` :175, `kidle` (net/http/http.c:249, from any CPU), and core/update.c:93/105/108/210. `busy_pump` is also wired globally as `g_wait_pump` (core/init/main.c:295). Every network wait (`nd_poll`, tcpip.c:26; ahci.c:130; websock.c:39) therefore keeps the freeze watchdog quiet indefinitely. `ui_pump` does not beat, so the waits inside Wi-Fi code still trip the box.
   Cleanup: `g_ml_beat++` only at the top of the kmain loop. Nested pumps bump a separate `g_nest_beat` and set `g_nest_t0` on entry. freeze_tick fires a "stuck in a nested wait: <g_in_what>, <last crumb>" box when the main loop has not completed an iteration for 10 s while nested beats continue. That case is exactly the owner's hypothesis.
b. **The NMI box returns when IF=1** (core/crash.c:335, added on R1-M2). With IOAPIC masked and PIC-only timing this is mostly fine. It does mean, though, that a stalled tick with IF=1 (PIT IRQ lost or masked, a stuck PIC ISR) gives no box from either watchdog, since freeze_tick itself runs on the tick. Cleanup: keep the IF=1 case and change the headline ("timer stopped, interrupts on"), as R1 already suggested as the alternative.
c. **`g_frz_hold` is a plain shared flag with save/restore.** The users are syscall 51 waitkey (sys/native/syscall.c:125, any CPU, loops in kidle forever and HOLDS the BKL), fs/vfs/sysroot.c:133/137 (CPU 0 only), apt, update and install. Interleaved save/restore across CPUs can leave it stuck at 1. While it is 1, freeze_tick, frz_bkl_wait (crash.c:301) AND nmiwd_check all return early. So one stuck flag silently turns off every box for the rest of the session. The worst combination: a native program in waitkey on an AP holds the BKL, CPU 0 spins in bkl_lock with IF=0, and no box can fire (hold=1). Cleanup: make it an atomic counter (like `g_frz_sleep`), owned by CPU 0 only. waitkey should drop the BKL (as syscall 71 does since v11.02) and should not set hold. Show "watchdog held for N s by <site>" in `crash nmiwd`.
d. Four box mechanisms (panic, freeze, BKL stuck, NMI) each have their own save, recovery toast and report code (crash.c:397-406 duplicates :386-394). Fold them into one `cx_box_event(kind, lines[], crumbs)` and one "recovered" path.

### 2. Late SET_PROTOCOL changes the mouse format mid-session (HIGH suspicion, cheap to test)
drivers/usb/xhci/controller_init.c:125-140 (`xhid_proto_poll`) and :115-124 (`xhid_note_boot`/`xhid_note_fmt`). This is the only new code that sends a control request to the receiver AFTER the desktop is in use, triggered by click activity (it starts 500 ms after a button change). It also swaps the globals cur_slot, cur_ep0, cur_dbuf, e0_enq and e0_cyc (:133-136) around `xh_ctrl`. While it waits, `xh_event_w` dispatches `hid_xfer` for other endpoints. That code reads the swapped globals only through recovery, which is deferred by `xh_wdepth`, so it is OK today but fragile.
Recommendation: default `g_usbproto_off=1`. Enumeration-time SET_PROTOCOL plus boot-packet decode already gives working clicks; the late retry has no benefit on the Biltema. Re-enable it only per VID:PID if a device ever needs it. Replace the cur_* swapping with a `struct xdev` parameter to `xh_ctrl`.

### 3. USB poll ownership (`xh_poll_busy`) can silently starve input (MEDIUM)
hid_keyboard_mouse.c:142-148 and usb_network.c:72-79. It is a global try-lock. Any nested pump (busy_pump :134, desk_nested_step :123, ui_pump :150) and `kidle` on an AP (http.c:249) call `xhci_mouse_poll`. If the flag is held, the caller just returns, so a nested loop running while another context holds the flag has dead USB input. Since 7f946bc5 ("poll takeover removed") a leaked flag is never recovered. Possible leaks: a fault in hid_report_decode or in_btn_raw, with crash.c:226 then polling from the panic wait (R2-LOW). Then USB keyboard and mouse are dead forever and nothing is shown.
Cleanup: ownership = CPU id plus depth; only CPU 0 polls (kidle on APs must not poll). Count skips (`xh_poll_skips`) and show "USB poll busy for N s" in every box.

### 4. Input queue and locking: patches on patches (MEDIUM)
- drivers/input/input.c: the spinlock is fine. But three callers open-code the same "lock, drain mq, take mouse_dx/dy, rebase" sequence: graphics/ui/input.c (process_input, ~line 288), present_and_pump.c:127 and :135. Replace them with one `in_snapshot(&snap)` in input.c and drop `in_lock_x`/`in_unlock_x`/`in_mq_take_locked`/`in_mq_rebase_locked` from the public surface. That makes "re-entry while held" impossible by construction.
- Unlocked RMW of `mouse_dx`/`mouse_dy`/`mouse_scroll` remains at hid_keyboard_mouse.c:72, 84 and 108 (and ps2.c:110). Route these through `in_rel`/`in_scroll` (R3-MEDIUM, still open).
- graphics/ui/input.c: the INM macro wraps every click site, and `in_unmark("input handling")` appears four times (:281, 286, 295, 335) inside a body that `process_input` already wraps in INM. The Win-key `in_mark` has no unmark. Keep `crumb()` plus one `in_mark` per dispatch; delete the rest.
- graphics/ui/input.c:278: **Ctrl+Alt+U runs `xhci_mouse_autoinit()` inside process_input**, which can itself be nested under desk_nested_step. A full xHCI re-init from inside an input handler, possibly while the outer code is mid-transfer, should be deferred to the main loop (a flag checked after `xhci_mouse_poll`).

### 5. xHCI waits and recovery: four layers of guards (MEDIUM, simplify)
controller_init.c:96-110, 243-300 and 325-345, plus hid_keyboard_mouse.c:105-140. Over this range the following were added one after another: time-plus-iteration bounded waits, `xh_want_slot/ep/ptr[3]/cmd` globals with a stale filter, `xh_q` with a drop counter, HID dispatch from inside waits, `xh_wdepth`, per-slot back-off (`xh_bo_*`), `xhid_recover` with budget, hold, recn and recpend, an endpoint-state watchdog every 256 polls, `xhid_errn`, and usb-net under the same flag. Each patch is locally correct (R2 checked them), but the combined state space is no longer reviewable, and HIGH-1 from R2 (TD loss on a running endpoint) shows the risk.
Cleanup, one design: a single event dispatcher drains the ring and routes by (slot, ep) to owner callbacks. A command or control request is a struct with a completion flag, and the waiter spins on its own flag, calling the dispatcher. That removes the want_* globals, the stale filter, `xh_q`, `xh_wdepth` and the deferred recovery. Recovery becomes one per-endpoint state machine (RUNNING, HALTED, RESETTING, REARM) run from the poll.

### 6. HID boot-vs-report heuristics (MEDIUM)
hid_report_decode (hid_keyboard_mouse.c:57-74) decides boot format per packet with five overlapping predicates (clear/rule1/sticky/amb plus the second "boot" loop) and the sticky `bseen`. Add `xhid_note_fmt`/`pfmt`/`nboot` and the usbsim truncation (`got=3` at :~95) on top. Recommendation: decide the format per device (BOOT3, BOOT4 or REPORT) from the first N packets plus GET_PROTOCOL, store it in `xhdiag.pfmt`, and use one switch in decode. Keep the per-packet guess only for a "format changed" counter shown in `usbhid`.

### 7. gfx and apps (LOW)
- drivers/video/fb/gfx.c `moving=(drag==i)` (v11.03 F1) and draw_app_marked: OK. The marked draw sets `g_draw_win`, which is useful for boxes. Add it to `cx_where`.
- apps/media/camera.c:115 `app_io_tick` and file_explorer/open_and_draw.c `files_vol_tick`: `vol_scan(0)` now runs from the main loop every 3 s while Files is open (Files is one click on the taskbar). If vol_scan touches the USB stick, that is a periodic xHCI bulk wait in the main loop. These are bounded but worth a crumb (`crumb("vol_scan>")`) so a hang there is identifiable.
- settings/pages.c `st_defer_id` and the Wi-Fi defer (app_tiles_and_icons.c:206-216): good pattern (moves work out of process_input). Unify both and the camera defer into one `defer_run(fn)` queue.
- heap: R3 already covers it (64 MiB zero with IRQs off when kmalloc is called IRQ-off). Still open.

## Things checked and OK
- interrupts.c spurious IRQ7/15 early return: correct. The IOAPIC is masked (cpu_init.c:10-13), so PIC-only.
- `bkl_take(0)` no-op (percpu.c): a fix. All callers pair it with `bkl_drop`, and kmain re-takes at main.c:~308.
- freeze_tick moved before the BKL (vector 32, CPU 0): correct and an improvement.
- crumb ring and COM1 mirror: non-blocking (R1-L5).
- No in_lock re-entry path found. hid_report_decode calls `in_btn_raw` without holding the lock.

## Suggested order
1. Watchdog: main-loop-only beat plus a nested-wait box, `g_frz_hold` as a CPU-0 counter, waitkey drops the BKL, NMI box shown with IF=1. This makes the next hardware run diagnostic.
2. Default `usbproto off` (or have the owner run `usbproto off` now as an A/B test).
3. CPU-0-only USB polling with skip counters, and defer Ctrl+Alt+U to the main loop.
4. `in_snapshot()` API and route `mouse_dx` through `in_rel`.
5. Later (refactor sprint): xHCI dispatcher/completion redesign, per-device HID format state, and one crash-box framework.
