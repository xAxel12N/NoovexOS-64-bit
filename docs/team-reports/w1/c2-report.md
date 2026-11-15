# c2: make every freeze visible (worktree /home/claude/wt/c2, branch x-c2)

Commits: a1ef445b (heartbeat, busy box, panic and NMI fixes), ac84cf95 (waitkey, Lua waitkey, Terminal=true apps).

## Changes
1. **S0 honest heartbeat**
   - core/init/main.c:177: new counters `g_ml_top`, `g_nest_beat`, `g_nest_live`.
   - main.c:304: `g_ml_top++` at the top of the real main loop. This is the only writer.
   - Nested UI pumps bump `g_nest_beat`: desk_nested_step, busy_pump (now only after its guard), ui_pump (graphics/ui/desktop/present_and_pump.c ~121/133/148), and kidle (net/http/http.c:249; one token, needed so kpop-based modal prompts are detected).
   - us_pump, up_rd/up_wr and desk_nv_progress do NOT bump `g_nest_beat`. That keeps the update screen and the NVIDIA progress screen from getting the box.
   - desk_nested_step with `g_pi_depth==0` (a full nested step that runs process_input and compose_present, e.g. a browser page load) bumps `g_nest_live`. The desktop is really alive in that case, so it counts as alive.
   - busy_pump: `g_ml_beat++` moved below the `in/state/same-tick` guard.
   - core/crash.c ~360-392: `cx_busy_draw` and `cx_busy_tick`, called first in freeze_tick, before the g_frz_hold return.
     - Condition: `g_ml_top` and `g_nest_live` stalled for ≥5 s, `g_nest_beat` moved in the last 2 s, state DESKTOP or LIVE, and not frz_sleep, preempt or panic.
     - It then draws an amber box, redrawn every 0.5 s:
       - "NoovexOS is busy (N s) ..."
       - "busy in: <g_in_what> '<g_in_arg>' (App window)"
       - cx_where
       - an Esc hint
       - the last 8 crumbs from crumb_dump
     - Esc while the box is shown sets `g_busy_esc=1`, which stops browser calls and websock/network waits that check it.
     - When the box clears, crash_tick invalidates the shadow (the box is erased) and logs "busy: the desktop was held up for N s - busy in: ...".
     - The box deliberately ignores g_frz_hold (task item 2). If the pumps stop too, the normal red freeze box takes over through `g_ml_beat`.
2. **syscall 51 waitkey** (sys/native/syscall.c:125), based on the cs2 patch but tighter:
   - kpop and the xhci poll still run under the BKL. The BKL is dropped only around `cpu_idle`, as case 71 does.
   - On CPU 0 it keeps g_frz_hold, but also bumps `g_ml_beat`/`g_nest_beat` and calls busy_pump, so the cursor moves and the busy box can show.
   - APs only idle.
3. **h8 #1**: crash_panic (crash.c:275) sets `g_frz_hold=1` and `g_cx_panicking=1`, so no boxes are drawn over the panic screen.
   **h8 #2**: nmiwd_nmi (crash.c:336-340) now also draws when IF=1, with a "timer ticks stopped with interrupts ON" first line. It stays quiet in that case during frz_hold, frz_sleep or panic.
4. **cs1**:
   - apps/x11/xapps.c:230 (not apps/system): Terminal=true apps now do `open_win(1)` (the Terminal window) before run_cmd, then print a prompt if no job owns the tty.
   - graphics/ui/input.c:63: nvxlc_waitkey (Lua) now loops `busy_pump(); kidle();`, so it polls USB and moves the cursor.

## Skipped / notes
- Not done: the stage-map fix (stage 6/14 names), the S1 per-handler budget, frz_hold_push, and the cs1 #3 sh_readpw prompts (shell files not in scope). Those prompts still get the new busy box via kidle.
- Behavior change: busy_pump in non-desktop states (LIVE, installer) no longer feeds `g_ml_beat`. A >5 s blocking network or AHCI wait in ST_LIVE without a hold can now show the red "not responding" box where it was silent before.
- Expected new boxes (these are honest): long terminal builtins such as `wifi scan`, apt downloads, and `sudo`/password prompts now show "busy N s in key 'Enter' (Terminal window)" after 5 s.

## Tests
- /home/claude/ccheck.sh: core/crash.c, core/init/main.c, graphics/ui/desktop.c (parent of present_and_pump.c), net/http/http.c, sys/native/syscall.c, graphics/ui/input.c, apps/x11/xapps.c. No errors. crash.c warning count is unchanged (8, all pre-existing).
- `python3 tools/check.py --only usb`: 12/12 passed.
- No QEMU run.

## Fix round (branch y-c2, commit 9b5b03b1): r2-review + r10 MEDIUM 1
- **HIGH 1** (graphics/ui/desktop/present_and_pump.c, busy_pump):
  - Outside ST_DESKTOP, busy_pump now feeds `g_ml_beat` on every call again, as in v11.06. Long network or disk waits in LIVE, SETUP or LOGIN therefore no longer get a false red box or a fake CRASH.TXT.
  - In ST_LIVE it also bumps `g_nest_beat` once per tick, so a long wait there gets the amber box instead.
- **MEDIUM 1**: key waits count as alive while their prompt is visible.
  - New `term_prompt_visible()` in shell/terminal.c: true in ST_LIVE, or when the Terminal window is open and not minimized.
  - kidle (net/http/http.c) bumps `g_nest_live` when the prompt is visible and `g_nest_beat` otherwise. A prompt hidden behind other windows still gets the amber box.
  - syscall 51 (sys/native/syscall.c) and Lua waitkey (graphics/ui/input.c) always bump `g_nest_live`.
- **MEDIUM 2** (core/crash.c, cx_busy_tick): the amber box steps aside while the red freeze box (`g_cx_frz_shown`) is up. The Wi-Fi loops that pump only ui_pump now show the red box alone.
- **MEDIUM 3**:
  - The hint now reads "Please wait - it is still working." It mentions Esc only while a web page is busy, checked through a weak `web_busy()`.
  - The box sets `g_busy_esc` only in that case.
- **r10 MEDIUM 1** (core/power.c): power_off and power_reboot now set `g_frz_hold=1`, plus a new `g_frz_down=1` (defined in crash.c) that also suppresses the amber box. No box or crash save happens during shutdown.
- **Not done**:
  - LOW 1 (no change needed).
  - LOW 2, echoing the command for Terminal=true apps: xapps.c has `#define term_puts rp` just above app_launch.
  - The nvme.c part of r10's patch is outside my scope.
- **Tests**:
  - ccheck passes with no errors on crash.c, power.c, desktop.c, terminal.c, http.c, syscall.c and ui/input.c.
  - `check.py --only usb`: 12/12.
  - No QEMU run.
