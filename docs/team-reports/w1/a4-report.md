# A4 report: what tools/check.py tests and misses, and new automated tests (v11.06, read-only)

## 1. Verdict
- check.py has no test that touches the UI or the shared input state. Every freeze this week (v11.02 nested USB wait in a click, v11.03 drag-end freeze, v11.05 boot-packet clicks, v11.06 SMP input-queue race, and the open Start/taskbar freeze) is in code that check.py never runs.
- `g_qemu` (check.py:854-969) boots with **no USB devices and no display checks**. Its command line has no `qemu-xhci`, no usb-mouse and no usb-kbd. It only types into the boot shell over PS/2 `sendkey` and runs user-space ABI tests. It never starts the desktop, never moves the mouse and never looks at the screen. `smptest` only tests user-space scheduling. The kernel input path (`in_lock`, the mouse queue, `kpush`) never runs on more than one core in any test.
- `g_qemu.typ()` (check.py:881-889) **retries typing when a keystroke is lost** ("note: a keystroke was lost"). This hides lost keys, which are exactly the input bugs the owner hits.
- The USB host tests (`g_usb`, hidmouse/hidkbd/hidparse/hidsim) are good at decoding. They are single-threaded and stop at `in_rel`/`in_btn_raw`/`kpush`. Nothing checks the consumer side (graphics/ui/input.c:288, present_and_pump.c:127/135) or its locking.
- Nothing tests the freeze detectors. `freeze_tick` (crash.c:363) resets on any `g_ml_beat++`, and the nested pumps (present_and_pump.c:121/132/175, http.c:249 `kidle`) bump `g_ml_beat` too. So "stuck in a modal loop" can never be seen by the box, and no test would notice.
- Honest limit: Q, Sim and the v11.02/v11.03 repro all found **no freeze in QEMU**. A plain QEMU UI test will not reproduce the owner's freeze. It still catches deterministic UI hangs, input or present regressions and locking regressions. To get closer to real-hardware timing, the design below adds fault injection: VM stop/cont jitter, USB hot-unplug, `usbsim biltema`, `-smp 4` with `smp on`, and an AP-side input injector. The host stress test with a mutant (section 3) **would have caught the v11.06 race class before release**.

| This week's bug | What check.py had | What catches it now |
|---|---|---|
| v11.02 blocking/nested wait in a click | nothing | `ui` sweep + `uistatic` modal-path check |
| v11.03 drag-end freeze | nothing | `ui` drag steps (press, move, release, verify) |
| v11.05 3-byte boot packets → clicks | hidmouse decode only | `ui` pass with `usbsim biltema` (exact `lpress` count) |
| v11.06 SMP input-queue race (r>w, about 2^32-iteration loop with IRQs off) | nothing | `instress` host test + mutant, `ui` AP injector |
| Start/taskbar freeze, heartbeat alive, no box | nothing | `ui` `ml_top` stall rule + freeze-detector unit test |

## 2. Test 1: QEMU UI smoke test (group `ui`)

### 2a. Small kernel debug hooks (off by default; one new shell command `uitest`)
Without these, the test has to guess coordinates and can only hash the screen. The hooks make every check exact. They follow the same pattern as `usbsim`: shell/commands/network_and_apps.c:214.
1. `volatile u32 g_ml_top` is incremented **only** in the top-level main loop (core/init/main.c:304, next to `g_ml_beat++`). The nested pumps do not touch it. This is the one signal that tells "main loop stuck in a modal or nested loop but still beating" apart from "alive". That is the owner's exact symptom.
2. `uitest on`: in `freeze_tick` (crash.c:363), **before** the early returns, every 100 ticks (1 s, CPU 0 timer IRQ), write one line to COM1 with polled, non-blocking writes (skip the line if THRE is clear, as `crumb_ser` already does at crash.c:83):
   `UIHB t=<g_ticks> top=<g_ml_top> beat=<g_ml_beat> st=<g_ml_stage> mx=<mx> my=<my> lp=<mouse_lpress> rp=<mouse_rpress> mq=<g_mq_w-g_mq_r> lost=<g_mq_lost> kq=<(kh-kt)&(KBUFN-1)> la=<g_launcher> fo=<focus> nw=<open windows> in=<g_in_what or ->`
   - A missing UIHB line means the ticks or CPU 0 are dead.
   - A frozen `top` with a moving `t` means a modal loop (this freeze).
   - A frozen `mx/my` after injected motion means input is dead.
   - A wrong `lp` delta means lost or doubled clicks.
3. Layout lines, sent from the main loop after `compose_present` when `g_uitest` is on and the layout key changes (launcher opened, submenu changed, window set changed):
   - `UIR start x y w h`
   - `UIR task <win> x y w h` (classic taskbar buttons / dock icons)
   - `UIR item <i> <win> x y w h` (every visible launcher or classic-menu entry, using `launcher_geom`/`lsub_geom` and the classic menu geometry in desktop_clicks.c, so it follows the real code)
   - `UIR pwr <i> x y w h` (power buttons, so the monkey test can avoid them)
   - `UIR title <win> x y w h` and `UIR close <win> x y w h`
4. `uitest ping N` prints `UIPING N` to COM1 (proves typing works end to end in Terminal).
5. `uitest style classic|modern` sets `g_dstyle` (the owner uses the classic taskbar; both styles are tested).
6. `uitest smpin <secs>` (phase B, see section 3c): APs call `in_rel`/`in_btn_raw` from a new IPI vector while the BSP runs the UI, and print `UISMP sent=<n> presses=<p>` when done.

Phase A works **today on the v11.06 ISO** without these hooks. It uses the fixed 1280x800 coordinates from /tmp/reprotools/item.sh (Start 50,784; group column x=110; submenu x=270), checks screen hashes, and treats serial-log growth as the heartbeat. It is weaker: the `top`, `lp` and `mx/my` rules are not available.

### 2b. Driving QEMU: QMP, not HMP
- Add `-qmp unix:{work}/qmp,server,nowait` next to the existing `-monitor`. Use `input-send-event` with `"device":"mouse"` for relative moves and buttons, and `"device":"kbd"` for keys. This **targets the usb-mouse explicitly**, so events cannot be routed to the tablet or to vmmouse. Use `-machine pc,vmport=off` so `vm_mouse_poll` cannot take over the pointer.
- Use `screendump` with `"format":"png"` (QEMU 8.2.2 here supports it; the files are much smaller than the current 3 MB PPMs).
- Use `stop`/`cont` for jitter and `device_del`/`device_add` for hot-unplug.
- Pointer positioning is **closed-loop**: send steps of at most 6 px (the `g_mouse_accel` scaling at present_and_pump.c:136 only applies above 6), then read `mx/my` from the next UIHB and correct. No acceleration guessing, unlike goto.py/mv.py.
- QEMU command: same disks as run.sh. `-smp 4 -accel tcg,thread=multi`, `qemu-xhci` x2, `usb-kbd,bus=xh1.0,id=kbd`, `usb-mouse,bus=xh1.0,id=mouse`, `usb-hub` + `usb-storage` on xh2 (the owner has a hub and a stick), no tablet, `-display none -vga std -no-reboot`.

### 2c. Scenario (one QEMU boot per pass)
Boot shell: `smp on` (passes 1 and 3), `usbsim biltema` (passes 1 and 3), `uitest on`, `uitest style X`, `desktop`. Then:
1. Desktop up: a UIHB line arrives, a `UIR start` line arrives, and the screen hash changes after a 10 px wiggle (the cursor is software-drawn, `present_cursor`).
2. Click empty desktop (the owner says this works; it is the control case).
3. Click Start: `la` becomes 1, `UIR item` lines arrive, and the hash of the menu rectangle changes. Click Start again: `la` becomes 0.
4. **For every `UIR item`** (about 30 from `LG_BASE`, desktop_clicks.c:82): open Start, hover the group, click the item, wait (3 s; 8 s for W_DOOM/W_QUAKE/W_GPU3D/W_BROWSER under TCG), check `nw` went up or `fo` changed, then click its `UIR close`. Never click `pwr` items or Log Out.
5. Click every `UIR task` button twice (focus, then minimise or restore). Also click the clock, bell, Wi-Fi, quick settings and workspace switcher, then Esc.
6. Drag every open window by its title (press, 15 moves, release). Check the window moved (a new `UIR title`) and the loop is alive 2 s after the release (the v11.03 class).
7. Open Terminal and type `uitest ping <n>` + Enter for 10 different n values. Every `UIPING n` must appear. **No retry**: lost keys are counted and the test fails.
8. Press-and-hold Start for 2 s, double-click Start, right-click the taskbar (Q's cases).
9. Pass 3 only: a seeded monkey of 200 random actions (move, click, right-click, drag, key, Esc), avoiding `pwr`. Every 15 actions do `stop`, sleep 0.2-1.5 s, `cont` (tick bursts like SMIs or slow devices). Once, `device_del mouse`, then after 2 s `device_add usb-mouse,bus=xh1.0,id=mouse` and `usbsim biltema` again (typed in Terminal) while the Start menu is open. Print the seed so a failure can be replayed.

**Fail rules, checked after every action:**
- No UIHB for 5 s.
- `top` unchanged for 5 s while `t` moves (report `st=`, `in=` and the last action).
- `mx/my` does not reach the target within 2 s.
- The `lp` delta differs from the clicks sent.
- `lost` > 0.
- The screen hash is unchanged after a wiggle.
- A panic or freeze-box text in serial.
- Any `UIPING` missing.

On failure, save serial.log, the last 3 PNGs and the action log to `tools/out/ui/<pass>/` (never `git add` these).

### 2d. Runtime budget (this host: 2 CPUs, so the passes run one after another)
| Pass | Config | Content | Budget |
|---|---|---|---|
| 1 | classic, `usbsim biltema`, `smp on` | full sweep, steps 1-8 | 7 min |
| 2 | modern, plain mouse, smp off | steps 1-3, 5 items, 5-7 | 3 min |
| 3 | classic, biltema, smp on | step 9 monkey + jitter + unplug, `uitest smpin 20` | 4 min |

- Boot to the main loop is included in each pass (the same 120 s limit as `g_qemu`).
- A hard group wall clock of 15 min: steps not reached are recorded as "not run (budget)", not as passed.
- `--quick` runs pass 2 only (about 3 min).
- `NVX_UI_SEED` replays a monkey run. `NVX_UI_PASSES=1,3` picks passes.

## 3. Test 2: SMP stress for shared input and keyboard state

### 3a. Host test `instress` (no QEMU; about 15 s; this is the one that catches races)
Files: `tools/tests/input/instress_gen.py`, `instress_main.c`, `instress_shim.h`.
- **The generator splices the real sources**, the same approach as `g_usb`, so the test follows code changes:
  - drivers/input/input.c, whole file.
  - The consumer block from graphics/ui/input.c (the text from `u64 lf=in_lock_x();` to `in_unlock_x(lf);`, line 288).
  - Both rebase blocks from present_and_pump.c (:127 `desk_nested_step`, :135 `busy_pump`).
  - `kpush`/`kpop` from net/http/http.c:246/250 via `defn()`.
  - Each splice is checked with `record()`, so a moved block fails clearly instead of silently testing nothing.
- The shim models IRQs on a CPU with signals:
  - `cpu_irq_save()` blocks SIGUSR1 with `pthread_sigmask` and returns 0x200 if it was unblocked.
  - `cpu_irq_on()` unblocks it.
  - `g_ticks` is advanced by a ticker thread.
  - `mouse_*`, `kdown` and `kshift` are plain globals as in the kernel.
- Threads:
  - 3 producer threads ("USB worker / PS/2 drain on other CPUs") call `in_rel` with known deltas and toggle buttons with `in_btn_raw` (button scripts are per-thread sequence numbered).
  - 1 consumer thread ("CPU 0 main loop") runs the spliced drain block in a loop and, every few iterations, a spliced rebase block.
  - An IRQ injector sends `pthread_kill(consumer, SIGUSR1)` at random 5-50 µs. The handler calls `in_btn_raw`/`in_rel`, which models a PS/2 or xHCI IRQ arriving on CPU 0 in the middle of the main loop. **Any path that takes the lock with IRQs on deadlocks here.**
  - A watchdog thread fails with "DEADLOCK in <block>" if consumer progress stops for 2 s.
- Invariants (after quiescing):
  - Σ produced dx/dy == Σ consumed + rebased + residual `mouse_dx/dy`.
  - Button transitions enqueued == taken + left in the queue.
  - `g_mq_lost == 0` (the queue never fills because the consumer is fast). A `mq_sane` reset counts as lost, so this proves r>w never happened.
  - `0 <= w-r <= IN_MQ_N` whenever sampled under the lock.
  - Event timestamps never go backwards.
- **Mutant self-test**: build a second binary in which the generator deletes the spin (`while(__sync_lock_test_and_set(&g_in_sl,1)) __builtin_ia32_pause();`), which is the v11.05 behaviour. It **must fail** within 5 s. If the pattern is not found, or the mutant passes, record FAIL ("stress has no teeth").
- **Keyboard ring**: 2 producers (the PS/2 drain and the USB `kbd_report` repeat) push sequence-coded chars **while holding a BKL mutex** (the current design: interrupts.c:92-93 and the main loop both hold the BKL). The consumer `kpop`s under the BKL. Check there is no loss, no duplicates, and that `kmod` matches `kbuf`. An extra "no-BKL" variant is informational only. It shows that `kpush` is an unlocked SPSC ring (http.c:246), so anyone who moves USB keyboard handling off the BKL will see it break.
- With `--sanitize`, also build the drain and locking part with `-fsanitize=thread`. Report the result but don't fail on it yet: today `mouse_btn` is read outside the lock in graphics/ui/input.c.

### 3b. Freeze-detector unit test (part of `instress`, cheap)
Splice `freeze_tick` (crash.c:363) with stubs. Cases:
- Top loop stuck while a nested pump keeps bumping `g_ml_beat`: today, no box (this documents the gap).
- After hook 2a.1 adds `g_ml_top` to the rule (for example, `top` frozen 10 s while not in `g_frz_hold`): a box appears.
- Real idle and `g_frz_sleep`: no box.

This locks in whatever freeze-box rule the lead chooses.

### 3c. In-guest SMP stress (inside `ui` pass 3, needs hook 2a.6)
`uitest smpin 20` on `-smp 4`, `smp on`. The APs inject `in_rel` plus press/release pairs via the IPI handler, using the real `cpu_irq_save` and real atomics, while the BSP runs the desktop and the test keeps clicking.
- Pass: `UISMP presses=P`, the UIHB `lp` delta == P + test clicks, `lost=0`, `top` alive, and the pointer still lands within 2 px of its targets afterwards.

## 4. Test 3: other host tests (group `uistatic`, also run with `static`, < 5 s)
1. **No modal or nested loop in click/key handlers.**
   - Build a regex call graph over graphics/ui, apps, shell, drivers/usb and net (function definitions + calls; ignore function pointers).
   - Roots: `on_click_body`, `mouse_step`, `launcher_click`, `classic_menu_click`, `classic_taskbtn_click`, `ctx_click`, `launcher_key`, `focus_win`, `open_win`, `lapp_open`.
   - Fail if any root reaches `busy_pump`, `desk_nested_step`, `ui_pump`, `kidle`, `xh_event_w` or `sleep_ms`, or a `while` whose condition reads `mouse_btn`/`g_ticks`, unless that edge is listed in `tools/tests/ui/modal_allow.txt` with a reason.
   - T found none today; this keeps it that way.
2. **Lock-region discipline.**
   - For every `in_lock_x()`, `in_lock(` and `cpu_irq_save()` region: no call except `*_locked` and allowlisted leaf helpers; no `return`, `goto` or `break` out of the region.
   - Every loop inside an IRQs-off region must be bounded by a constant or a `<=IN_MQ_N`-style check. This is the v11.06 "loop about 2^32 times with IRQs off" class.
   - It automates L's manual audit.
3. **Heartbeat discipline.** `g_ml_beat++` outside main.c must come after the re-entry guard. busy_pump (present_and_pump.c:132) bumps it **before** its `in`/same-tick guard today; that is P's finding, so this check fails until it is fixed or allowlisted. Every nested pump must poll input (`xhci_mouse_poll`/`process_input`).
4. **Owner packet replay.** Add a `tools/tests/usb/replay/*.txt` corpus (raw HID packets from `usbproto`/crash reports, starting with Biltema 248A:16BB 3-byte boot packets mixed with full reports). Feed it through the existing hidsim harness and check the expected button/dx/dy per packet. Every real-hardware capture becomes a regression test.

## 5. Exact check.py layout
New functions, placed after `g_qemu` (check.py:969):
```python
UI_BUDGET = 900

class Qmp:
    def __init__(self, path):
        import socket, json
        self.j = json; self.s = socket.socket(socket.AF_UNIX); self.s.connect(path); self.f = self.s.makefile('rw')
        self.f.readline(); self.cmd('qmp_capabilities')
    def cmd(self, name, **args):
        self.f.write(self.j.dumps({'execute': name, 'arguments': args}) + '\n'); self.f.flush()
        while True:
            r = self.j.loads(self.f.readline())
            if 'return' in r or 'error' in r: return r
    def rel(self, dx, dy):
        return self.cmd('input-send-event', device='mouse', events=[{'type': 'rel', 'data': {'axis': 'x', 'value': dx}}, {'type': 'rel', 'data': {'axis': 'y', 'value': dy}}])
    def btn(self, b, down):
        return self.cmd('input-send-event', device='mouse', events=[{'type': 'btn', 'data': {'button': b, 'down': down}}])
    def key(self, q, down):
        return self.cmd('input-send-event', device='kbd', events=[{'type': 'key', 'data': {'key': {'type': 'qcode', 'data': q}, 'down': down}}])
    def shot(self, path):
        self.cmd('screendump', filename=path, format='png'); return open(path, 'rb').read()

def ui_hb(text):
    return [dict(kv.split('=', 1) for kv in l.split()[1:]) for l in text.splitlines() if l.startswith('UIHB ')]

def ui_rects(text):
    r = {}
    for l in text.splitlines():
        if l.startswith('UIR '): p = l.split(); r.setdefault(p[1], []).append(p[2:])
    return r

def ui_vm(iso, work, smp):
    ...

class UiRun:
    def __init__(self, vm, name, deadline): ...
    def alive(self, why): ...
    def goto(self, x, y): ...
    def click(self, x, y, b='left'): ...
    def drag(self, x0, y0, x1, y1): ...
    def type(self, txt): ...
    def menu_sweep(self, slow): ...
    def taskbar(self): ...
    def drags(self): ...
    def terminal(self, n): ...
    def monkey(self, seed, n): ...

def ui_pass(iso, name, style, biltema, smp, steps, deadline): ...

def g_ui(iso, quick):
    q = os.environ.get('NVX_QEMU') or shutil.which('qemu-system-x86_64')
    if not q or not os.path.exists(iso): return
    head('Desktop UI in QEMU: Start menu, every item, taskbar, drags, Terminal typing, usbsim biltema, SMP input injector')
    t_end = time.time() + UI_BUDGET
    passes = os.environ.get('NVX_UI_PASSES', '2' if quick else '1,2,3').split(',')
    cfg = {'1': ('classic', True, 4, 'full'), '2': ('modern', False, 2, 'short'), '3': ('classic', True, 4, 'monkey')}
    for p in passes: ui_pass(iso, 'pass' + p, *cfg[p], t_end)

def g_instress(work, quick): ...

def g_uistatic(): ...
```
- `ui_pass` records one `record('ui', ...)` per step (for example `'pass1 classic+biltema+smp: Start menu item W_CALC opens and closes, loop alive'`). On the first FAIL in a pass, it records the remaining steps as `not run` (`ok=True` with detail `skipped after failure`, so one hang does not produce 40 FAILs), then saves the artifacts.
- `g_instress` follows the `g_usb` pattern: gen → gcc `-O1 -pthread -w` (+SAN) → run with `timeout=60` → `record('instress', 'mouse queue: 3 producers + IRQ-on-CPU0 + consumer, deltas/buttons conserved, no r>w, no deadlock (N events)')`, then `record('instress', 'mutant without the spinlock is caught')`, `record('instress', 'keyboard ring under BKL: no loss/dup')` and `record('instress', 'freeze_tick: nested-pump beat vs top loop')`. Budget: 4 s per variant, `--quick` 1 s, total ≤ 20 s.

Registration in `main()` (check.py:1404):
```python
ap.add_argument('--only', help='comma list: build,static,uistatic,boot,usb,instress,acpi,gpu,nvme,touchpad,memory,aml,timers,cookies,linux,fs,accounts,nvidia,tls,fetch,qemu,ui,net,nics,drivers,analyze,libc,coreutils')
...
if want('static'): g_static()
if want('static') or want('uistatic'): g_uistatic()
...
if want('usb'): g_usb(work); g_xhci_init(work); g_uvc(work)
if want('usb') or want('instress'): g_instress(work, a.quick)
...
if want('qemu'): g_qemu(iso)
if want('ui'): g_ui(iso, a.quick)
```
- Also update tools/README.md "Groups".
- Fix the hidden retry: in `g_qemu.typ()`, keep the retry but also `record('qemu', 'no keystrokes lost while typing', lost == 0)`, or at least count the retries in the summary.
- Coders may run `--only instress` and `--only uistatic` (fast, host-only). `--only ui` needs QEMU, so only agents told to use QEMU should run it.

Total extra runtime for a full check.py: ui about 14 min (`--quick` 3), instress about 20 s, uistatic < 5 s.

## 6. Order of work (suggested)
1. `instress` + mutant + freeze_tick test: host-only, needs no kernel change, and would have caught v11.06.
2. `uistatic` 1-3.
3. Kernel hooks 2a.1-2a.5 (small, debug, off by default) + `ui` passes 1-2.
4. Phase B: `uitest smpin` (new IPI vector next to IPI_RESCHED/TLB/HALT, smp_sched.c:22-24) + pass 3 jitter/unplug.
5. The replay corpus as the owner sends captures.
