# cs1: sweep of hardware- and state-dependent wait loops (whole OS, third_party excluded)

Scope: every while/for/do loop whose exit depends on hardware, on another context, or on g_ticks. That is 227 one-line polling loops (grep on pause/io_wait/delay/pump/idle/halt), plus 110 `for(;;)`/`while(1)` blocks checked by a script that extracts each body (/tmp/cs1/scan.py, output in /tmp/cs1/inf.txt), plus a manual read of the whole taskbar and Start-menu click path.

## Headline for the freeze (taskbar / Start menu)
- I found **no unbounded hardware wait loop on the taskbar or Start-menu click path**. Path checked: on_click_body (input.c:108-131), xtray_bar_click/xtray_pop_click, launcher toggle, launcher_search, launcher_draw/classic_menu_draw, lgrp_refresh, xapps_refresh (bounded at 256 + 256 entries), app_icon_draw/find_icon/load_png_scaled (link hops capped at 8), open_win/focus_win/ws_switch, wifi_gui_scan (non-blocking), and the qs/clock/notif popups. Every loop there has an iteration bound.
- The xHCI driver's wait loops are all bounded (iteration counts, `guard<128`, `t<200000`, xh_event iters). ps2 pw_read/pw_write are bounded. igpu_wait_flip is bounded by hr_ns (40 ms). NVIDIA waits are bounded by hr_ns.
- The only unbounded spin with interrupts off is `drivers/input/input.c:8` in_lock (test_and_set with IF=0, no bound). It only deadlocks on same-CPU re-entry. I found no re-entry: no IRQ handler can preempt it because IF=0, and no caller calls back into in_* while holding it. If it ever deadlocks, the NMI box should fire (SMP).
- **Why a nested hang hides from the freeze box (fact):** g_ml_beat is fed unconditionally by `kidle()` (net/http/http.c:249), `busy_pump()` (present_and_pump.c:132, before its re-entry/lastp check), `desk_nested_step()` (:121) and `desk_nv_progress()` (:175). Any unbounded loop that calls one of these keeps the heartbeat alive, so the freeze box never shows. The loops below are the ones that do that, or that hang for another reason.

## Findings (most important first)

### 1. MEDIUM, confidence 70%: Start menu runs Terminal=true apps with run_cmd in a hidden terminal
`apps/x11/xapps.c:230` app_launch. If an installed .desktop entry has `Terminal=true`, run_cmd runs it straight from the Start-menu click handler, but the Terminal window is never opened. Compare pyide_run (editor.c:39) and the Files menu (input.c:445), which call `open_win(W_TERM)` first.
- Scenario: Start, then category, then a package app with Terminal=true (nano, htop, a python REPL entry, and so on). The program's output and prompts go to a terminal that isn't on screen. The user sees nothing happen, or a desktop that looks dead while the command is in the foreground (g_tty_fg) and keys go to the tty. With the handoff's "terminal commands run synchronously while holding the BKL", a blocking command would freeze the desktop from a Start-menu click with no box.
- Patch:
  - old: `if(a->term){ static char b[200]; int i=0; for(;a->exec[i]&&i<199;i++)b[i]=a->exec[i]; b[i]=0; run_cmd(b); } else xrun_launch(a->exec); } }`
  - new: `if(a->term){ static char b[200]; int i=0; for(;a->exec[i]&&i<199;i++)b[i]=a->exec[i]; b[i]=0; open_win(1); run_cmd(b); { extern void term_prompt(void); if(!g_tty_fg)term_prompt(); } } else xrun_launch(a->exec); } }`
  - g_tty_fg comes from jobs.h, which is already included. W_TERM==1, and open_win is already declared at line 90.

### 2. MEDIUM, confidence 85%: nvxlc_waitkey never polls USB, so a Lua `waitkey()` hangs forever with a USB-only keyboard
`graphics/ui/input.c:63`: `while((k=kpop())<0)cpu_idle();`. USB HID is **polled only**: xhci_mouse_poll is reached from the main loop, kidle, busy_pump, ui_pump and desk_nested_step, never from an IRQ. The owner's keyboard and mouse are USB receivers, so kpop never gets a key and the loop never exits. The v11.03 s1 report dismissed this as "only if IRQs off". That is wrong: with USB input it hangs even with IRQs on.
- Scenario: Terminal, then a Lua script that calls `waitkey()` (third_party/lua/nvxlua.c:31). The whole desktop is dead and the keyboard does nothing. The heartbeat stops, so the freeze box does fire, but the user can't recover.
- Patch:
  - old: `int  nvxlc_waitkey(void){ int k; while((k=kpop())<0)cpu_idle(); return k; }`
  - new: `int  nvxlc_waitkey(void){ extern void kidle(void); int k; while((k=kpop())<0)kidle(); return k; }`

### 3. LOW-MEDIUM, confidence 75%: password and yes/no prompts are modal loops that freeze the mouse and screen while the heartbeat stays alive
`shell/commands/prompt_and_dispatch.c:4` (sh_readpw) and `:13` (sh_yesno), and `shell/commands/terminal_keys_and_render.c:229` (Full name prompt). They run `for(;;){ c=kpop(); if(c<0){ kidle(); continue; } ...}`. kidle feeds g_ml_beat and polls xHCI, but nothing presents the cursor or the screen until a key arrives.
- Scenario: Settings (pages.c:288, apt_cmd "remove -y") is fine. But `sudo`, `su`, `passwd` or `apt install` without -y from the terminal: the cursor stops and clicks do nothing until Enter or Ctrl+C. That looks exactly like "frozen, no red box". If the prompt comes up in a terminal the user isn't looking at, it looks like a permanent freeze.
- Patch (same in all three loops): replace `if(c<0){ kidle(); continue; }` with `if(c<0){ extern void busy_pump(void); busy_pump(); kidle(); continue; }`. busy_pump moves and presents the cursor and does not consume keys. It is a no-op outside ST_DESKTOP and is re-entry safe.

### 4. LOW, confidence 60%: DOOM's DG_SleepMs uses bare `hlt` (no sti)
`apps/games/doom.c:67`: `while(g_ticks<tgt) cpu_halt();`. It is called from d_loop.c:784 I_Sleep(1) inside TryRunTics. If it is ever entered with IF=0 (a cli section or the syscall path), `hlt` never wakes and the CPU stops for good: no ticks, no heartbeat. Only the NMI box (SMP) would catch it. On a polled timer (g_ticks_live==0) it also never ends.
- Patch:
  - old: `void DG_SleepMs(unsigned int ms){ u64 tgt=g_ticks+(ms/10)+1; while(g_ticks<tgt) cpu_halt(); }`
  - new: `void DG_SleepMs(unsigned int ms){ u64 tgt=g_ticks+(ms/10)+1, t0=hr_rdtsc(); while(g_ticks<tgt&&hr_rdtsc()-t0<(u64)(ms+20)*3000000ULL) cpu_idle(); }`
  - Add `extern u64 hr_rdtsc(void);` if it isn't visible; the bound assumes a TSC of 3 GHz or more. At minimum, change cpu_halt to cpu_idle (sti;hlt).

### 5. LOW, confidence 90% that the code works this way; it only matters on a machine where the boot check picks the polled timer: g_ticks-only waits never end when g_ticks_live==0
With g_ticks_live==0 (main.c:288 "using a polled timer"), g_ticks advances only in the main loop (main.c:297 pit_poll_tick). These nested waits use only g_ticks and have no iteration or TSC bound, so they hang there:
- `graphics/ui/input.c:66` nvxlc_sleep
- `sys/native/syscall.c:205` sleep (case 71)
- `apps/games/doom.c:67`
- `apps/internet/browser/websock.c:39` w_wait_tick
- `core/update.c:400`
- `net/wifi/wifi_mgr.c:188-193` scan_step plus wifi_scan_blocking (`while(sc_on)`), reached from wifi connect at :253
- `shell/commands/processes_devices_wifi_bringup.c:56`

Not the owner's case: his ticks are live. Cheapest fix, one line: make kidle() and the cpu_idle callers above advance the polled timer.
- Patch (net/http/http.c:249):
  - old: `void kidle(void){ extern int xhid_n; extern void xhci_mouse_poll(void); extern volatile u32 g_ml_beat; g_ml_beat++; if(xhid_n)xhci_mouse_poll(); if(kh==kt)cpu_idle(); }`
  - new: `void kidle(void){ extern int xhid_n; extern void xhci_mouse_poll(void); extern volatile u32 g_ml_beat; extern int g_ticks_live; extern void pit_poll_tick(void); g_ml_beat++; if(!g_ticks_live)pit_poll_tick(); if(xhid_n)xhci_mouse_poll(); if(kh==kt&&g_ticks_live)cpu_idle(); }`
  - NOTE: pit_poll_tick is `static` in core/init/main.c:169. Remove the `static` (or add a non-static wrapper `void pit_poll_tick_x(void){ pit_poll_tick(); }`) before applying this.
  - Then switch nvxlc_sleep, w_wait_tick and update.c:400 from cpu_idle() to kidle().

### 6. INFO (design): nested heartbeat feeders hide nested hangs
busy_pump does `g_ml_beat++` **before** its `if(in||g_state!=ST_DESKTOP||g_ticks==lastp)return;`, so even a re-entered or no-op call counts as a main-loop beat. Suggestion for the watchdog work: have kidle, busy_pump and desk_nested_step bump a separate `g_nest_beat`, and have freeze_tick show a softer box ("waiting inside X for N s", using g_in_what) when g_ml_beat moves only through nested feeders for more than 15 s. That would make the owner's case (heartbeat alive, no box) visible on the next test.

## Checked and bounded (no action)
- **xHCI:** controller_init.c (38/39/57/353), hid_keyboard_mouse.c (120 guard, 179 msd rounds), storage hub waits (37).
- **Input, power, ACPI:** ps2_mouse.c:6-7; acpi_dev.c:28/39; power.c:75/176 (S1 bounded at 200M pauses); smp_sched.c:31/42/51; smp.c:92.
- **Wi-Fi:** rtw8821ce.c (all bounded); rtw8822c.c:796/802; wifi_netdev.c:53.
- **Storage, sound, network:** nvme.c; hda.c; ac97.c; e1000/e1000e/igb/rtl8169/rtl8139/virtio; vtd.c:13; parts.c:21; tcpip.c:26/250; ahci.c:130.
- **Video:** igpu.c:79/106/170/171; nvflcn.c; gsp/*.
- **Other:** core/power.c:149 (sleep: waits for a key or hotkey by design); crash.c:223 (box key loop); usbhid.c:52 and network_and_apps.c:256 (spins plus g_ticks_live fallback); ux_mouse_push (unixfs.c:184, `while(...||1)` exits on break/return); hk_poll (bounded by its queue); sysroot ht_get (load factor at most 0.5).
- **Known, not repeated:** syscall 51 waitkey (v11.03 s6/v3).
