# manager report (v11.02) - live, last update 13:49 UTC

Method: per-branch `git log main..HEAD` / `diff --stat main` / worktree status; trial merge in /tmp/mgr
(git clone --shared + fetch of the 6 worktree branches; uncommitted c-wait applied on top as a patch);
USB host tests run from the trial tree by importing tools/check.py's g_usb/g_xhci_init/g_uvc functions
(harness /tmp/mgr-harness/usbrun.py; check.py's main/build never run); ccheck.sh warning diff main vs trial.
Baseline main 3828b7f8: USB host tests 12/12 pass.

## UPDATE 13:48 - lead merged everything into main (433963e1) with a nested-recover guard
- Verified the guard in main: xh_event_w wraps xh_event_w_in with xh_wdepth++/--; xhid_recover() only sets
  xhid_recpend[e] while xh_wdepth>0, and also rate-limits to 3 recoveries per second per endpoint; pending
  recoveries run from xhci_mouse_poll (hid_keyboard_mouse.c:121), outside any wait. Conflict 1 is RESOLVED.
- main 433963e1: USB host tests 12/12 (usb 91, hidmouse 50, hidkbd 44, hidparse 79, hidsim ok, xhci init ok,
  uvc 25); ccheck drivers/usb/xhci.c: 0 errors, only misleading-indentation/unused style warnings.
- NEW LOW-MED finding in the guard: xhid_recpend[]/xhid_recn[]/xhid_rect[] are never cleared (not in xhid_reset,
  not on unplug/xhid_remove_port, not when an entry is reused). If a device is unplugged with a pending recovery
  and its index is reused by a new device, xhid_recover fires once on a Running endpoint: Reset EP and Set TR
  Dequeue fail with Context State Error, then xhid_arm runs 16 more times -> 32 TRBs in flight on a buffer pool
  sized XHID_INFLIGHT*64 -> two TRBs share one buffer -> possible garbled reports. Fix: clear the three arrays for
  the entry in xhid_reset and wherever xhid_used[e] is set/cleared (or only re-arm in xhid_recover_now if the
  Reset EP returned cc 1 / 19-after-stop).
- Diff reviewers (revwait H1, revring H1) found the same nesting bug independently; revapps: no HIGH (camera start
  from click/key handlers still blocks up to ~4 s); revcore: no blockers.
- Repro (QEMU, v11.01): no freeze for any Drivers & Hardware item (Gears, Camera w/o camera, Benchmark, Task
  Manager, Settings) and hot-plug works -> freeze needs real-device timeouts/stalls; fix is NOT verified on HW.

## Per agent status / scope / risk
| agent | state | files | scope | risk |
|---|---|---|---|---|
| c-wait | 1 commit cddaa3f7 (+54/-17 code) | controller_init.c, hid_keyboard_mouse.c (xh_bulk), tools/check.py, tools/tests/usb_template.txt | in scope (xhci waits) | MEDIUM: biggest diff; handles HID transfer events inline during waits (fixes the prime theory), tick-based timeouts (cmd 100 ms, EP0 150 ms, recovery 50 ms), command-TRB/EP0-TRB pointer matching (drops stale completions), per-slot back-off (3 EP0 timeouts in 3 s -> EP0 held 5 s). Alone: 12/12 tests pass. Its report says "no nested waits from hid_xfer" - FALSE once c-ring is merged (see Conflicts 1). |
| c-ring | 1 commit 671aea4e (+13 code) | controller_init.c (xhid_recover), hid_keyboard_mouse.c (hid_xfer cc 3/4/6 -> recover; watchdog every 256 polls re-arms Halted/Error HID endpoints), tests/check.py names | in scope | MEDIUM: semantic clash with c-wait (see Conflicts) |
| c-cam | 1 commit (+18/-9) | uvc_webcam.c only | in scope (not xhci core) | LOW: uvc_start bounded (6 Configure Endpoint tries, 3 control failures, 4 s), bad alt settings skipped, audio start back-off 5 s / 4 fails. Does NOT fix the code 17 root cause (Parameter Error on Configure Endpoint) - camera will still not stream. |
| c-launch | 1 commit (+17/-14) | apps/media/camera.c, recorder.c | in scope | LOW: recorder no longer retries mic start every frame (rc_tried latch); camera start latched + re-entry guard. Camera auto-start STILL runs inside camera_draw (2nd frame) -> UI still blocks for up to c-cam's 4 s bound. |
| b-core | 1 commit (+4/-4) | mem/heap.c, sys/native/syscall.c | in scope (core) | LOW-MED: kmalloc/kfree under cpu_irq_save; native sleep drops BKL, clamps 1 h. Not a freeze fix for this bug. Verified: isr128_c (interrupts.c:98) always holds the BKL, so bkl_drop() returns >=1 and bkl_take restores it; IRQs off on return = gate entry state. OK. |
| b-ui | no commit (report only) | - | - | none; findings 1-3 useful (camera start in draw path, click path blocks input, ui_pump does not consume mouse dx/dy) |
| Repro | booting QEMU, no result yet | | | |
| Waits | no content yet | | | |
| Sweep1/2/4 | in progress, no findings yet; Sweep3 has H1/H2 (recorder per-frame mic start, camera uvc_start in draw) = same as c-launch fixed |

Scope checks: Wait+Ring are the only branches in drivers/usb/xhci core (controller_init.c, hid_keyboard_mouse.c);
Cam only uvc_webcam.c; Launch/UI do not touch xhci. No `build` symlink or tools/out committed on any branch
(trial tree status clean of them). All diffs small.

## Conflicts (trial merge, order wait, ring, cam, launch, core, ui) - re-run 13:43 with all committed branches: clean, 12/12
Textual: NONE. c-ring and c-wait both edit the same `names = [...]` line of tools/check.py g_usb, but git
3-way applied both cleanly (merged line has 'xhid_recover' AND Wait's xh_event_w/... names; Wait's `order` change kept).
Semantic (real):
1. HIGH - c-wait now calls hid_xfer() INSIDE xh_event_w (while a command or EP0 transfer is waited for);
   c-ring's hid_xfer on cc 3/4/6 calls xhid_recover() -> two xh_cmd_s -> NESTED xh_event_w.
   - nested xh_cmd_w overwrites xh_want_cmd and resets it to 0 on return -> outer command loses its filter;
   - the nested wait (want 33) discards the outer command's completion as "stale" (pp != nested TRB) -> outer
     command times out and reports failure (e.g. Configure Endpoint / Address Device fail);
   - if the outer wait was an EP0 control transfer, its completion arrives during the nested wait as a
     non-HID type-32 event and goes to xh_q -> outer xh_ctrl times out -> EP0 recovery -> request fails.
   Merged-tree lines: controller_init.c:192 (`if(xh_hid_ep(sl,ep)){ hid_xfer(...); continue; }` inside xh_event_w),
   hid_keyboard_mouse.c:103 (`if(cc==3||cc==4||cc==6){ xhid_recover(d); return; }`), controller_init.c:209
   (`xh_want_cmd=at; ... xh_want_cmd=0;` not saved/restored), :196 (other command completions dropped as stale).
   Ring's own watchdog (hid_keyboard_mouse.c:126) has the same nesting: xhid_recover -> xh_cmd_w -> inline hid_xfer
   -> xhid_recover for a second halted device. Sweep1 H2 explicitly said "recover in xhci_mouse_poll, NOT inside xh_event_t".
   Trigger needs a HID halt (cc 3/4/6) during another wait - plausible on the owner's 2.4 GHz receivers behind a hub,
   and exactly during the camera's long control sequence.
   Fix (one line, Ring or Wait): never recover from inside a wait - e.g. a `static int xh_in_wait` depth
   counter set in xh_event_w, and in hid_xfer: `if(cc==3||cc==4||cc==6){ if(!xh_in_wait)xhid_recover(d); return; }`
   (Ring's 256-poll watchdog already re-arms a Halted endpoint later).
2. RESOLVED at 13:43 (Wait's committed test resets state; merged tree 12/12). Was: merged tree failed one new c-wait test: "the keyboard's event is handled during the wait (typed,
   re-armed, not queued)". Verified cause: test section 13 in tools/tests/usb_template.txt does not reset
   `kq`; in the merged tree a leftover "h" from an earlier section is in kq, so kq=="hh". Adding
   `kqn=0; kq[0]=0;` before the put_event calls makes ALL PASS. Wait should add that reset (and ideally say why
   the earlier section now leaves an 'h' behind with Ring merged).

## Test results
- 13:43 trial merge of ALL committed branches (wait, ring, cam, launch, core): USB host tests 12/12 pass
  (usb 91 checks, hidmouse 50, hidkbd 44, hidparse 79, hidsim LOST-Y 0 / unexpected 0, xhci init, uvc 25).
- 13:41 earlier trial with uncommitted wait: 11/12 (kq test-state bug, fixed in the commit). hidmouse 50, hidkbd 44,
  hidparse 79, hidsim (LOST-Y 0, unexpected 0), xhci init scratchpad ok, uvc/uac 25 checks ok.
- c-wait alone: 12/12. main: 12/12.
- ccheck (warning diff vs main): xhci.c +1 -Wmisleading-indentation (controller_init.c:210, Wait's
  `if(!ok)return 0; if(ccode)...` line), uvc_webcam.c -1; camera.c +1 (Launch's cam_try_start line 45);
  shell/commands.c, heap.c, syscall.c, recorder.c: no new warnings. No errors. Style-only.

## Cross-check: is the most likely freeze cause fixed?
- Prime theory (HID events dropped/mishandled during command/control waits -> endpoints never re-armed ->
  mouse+keyboard dead): in v11.01 xh_event_t queued them in xh_q[32] and SILENTLY DROPPED beyond 32. Each
  dropped event = one TRB never re-armed; after 16 drops per device (XHID_INFLIGHT) the device is dead for good,
  and its endpoint stays Running, so Ring's Halted/Error watchdog would NOT catch it. FIXED by c-wait only
  (HID events handled inline, drops counted in xh_q_drop). => c-wait is release-critical and still uncommitted.
- Long blocking USB work in the UI path (camera uvc_start 32 tries in draw; recorder mic start every frame):
  fixed jointly by c-launch (latches) + c-cam (bounds) + c-wait (short timeouts, back-off). Remaining: camera
  start still inside the draw path (up to ~4 s UI stall on the owner's camera). Gap, medium.
- Camera code 17 root cause (Configure Endpoint Parameter Error): nobody owns it in v11.02. Gap (feature, not freeze).
- All of input is polled from the UI loop (kidle/busy_pump/ui_pump); any other long blocking call still freezes
  keyboard+mouse. b-ui finding 3 (ui_pump does not consume mouse dx/dy) not fixed by anyone. Gap, low.

## Merge order (proposed)
1. c-wait (after commit + kq test fix)  2. c-ring (after the nested-recover guard)  3. c-cam  4. c-launch
5. b-core  6. b-ui (nothing to merge). Re-run g_usb after 2.

## Top 5 things the lead must do before release
1. DONE (merged). Keep c-wait in - it is the only fix for the most likely freeze cause (dropped HID events -> TRBs never re-armed).
2. DONE by lead in 433963e1 (xh_wdepth guard). Remaining: clear xhid_recpend/xhid_recn/xhid_rect per entry in
   xhid_reset and on unplug/reuse (new LOW-MED finding above), then re-run g_usb.
3. Have the 4 diff reviewers check specifically: Wait's xh_ctrl/xh_bulk TRB-pointer filter (a legit event with a
   pointer not in tp[] is silently dropped -> 150 ms timeout + EP0 recovery), the back-off hold (5 s of instant
   failures for a slot, incl. hub status polls), and Ring's watchdog cost (2 commands per Halted endpoint every
   256 polls, forever, for a permanently failing device - consider a retry cap).
4. Move the camera auto-start out of camera_draw (main loop tick or after open_win), or accept and document the
   up-to-4 s stall; tell the owner the camera still won't stream (code 17 not fixed).
5. Release notes must not claim "freeze fixed" as verified: Repro could not reproduce in QEMU (no real camera).
   Ask the owner which items freeze, whether pointer/clock still move, wait 3-4 min to see if input returns, and run
   `usbhid` (xhid_cnt not rising = TRB starvation) plus memcheck after a freeze. b-ui finding 3 (ui_pump does not
   consume mouse dx/dy) remains open, low.
