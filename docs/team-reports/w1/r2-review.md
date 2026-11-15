# r2 review of c2 (branch x-c2, merged into main as v11.07)

Scope: a1ef445b and ac84cf95, checked against the merged files in main (01c10690). No other coder touched busy_pump, kidle, freeze_tick or syscall 51 (9aed47a6 g_disk_busy in crash.c doesn't interact with this). Focus as asked: false red and amber boxes during legitimate long work.

Verdict: the S0 mechanism works and the panic hold is correct. One HIGH regression: false red boxes outside the desktop. Three MEDIUM problems, all false or doubled boxes. Two LOW.

---

## HIGH 1: busy_pump no longer feeds g_ml_beat outside ST_DESKTOP, so long network waits in LIVE/SETUP/LOGIN/APP get a false red "froze" box and a saved crash report

- Where: graphics/ui/desktop/present_and_pump.c:132-133 (main).
- What changed: `g_ml_beat++` moved below `if(in||g_state!=ST_DESKTOP||g_ticks==lastp)return;`.
- Why it matters: main.c:295 sets `g_wait_pump=busy_pump`. Every TCP/ARP/DHCP receive wait goes through it: net/stack/tcpip.c:26 nd_poll, drivers/storage/ahci.c:130, browser websock.c:39.
  - In v11.06 those waits fed the heartbeat in every state.
  - Now, in any state other than ST_DESKTOP, nothing beats.
  - freeze_tick (core/crash.c:396-401) has no state filter. So a >5 s network wait without g_frz_hold shows the red "NoovexOS froze" box. At 30 s it runs cx_save, which writes crash-report.txt and CRASH.TXT for a crash that never happened.
- Examples:
  - In the live terminal (ST_LIVE): `wget`/`curl` of a big file, `ping` against a dead host, a DNS timeout.
  - Setup or login screens doing DHCP/HTTP.
- The amber box can't cover this case: busy_pump bumps g_nest_beat only after the same guard, so in LIVE nothing moves.
- c2's report lists this as an intended "behavior change". It is the false red box this review was asked to catch, and it also produces fake crash reports.
- Patch (keeps the S0 intent: on the desktop, nested pumps still feed only after the guard, and the amber box handles them):
```
 void busy_pump(void){
-    static int in=0; static u64 last=0, lastp=~0ULL; if(in||g_state!=ST_DESKTOP||g_ticks==lastp)return; lastp=g_ticks; in=1;
+    static int in=0; static u64 last=0, lastp=~0ULL;
+    if(g_state!=ST_DESKTOP){ extern volatile u32 g_ml_beat, g_nest_beat; g_ml_beat++; if(g_state==ST_LIVE&&g_ticks!=lastp){ lastp=g_ticks; g_nest_beat++; } return; }
+    if(in||g_ticks==lastp)return; lastp=g_ticks; in=1;
```
  - Outside the desktop this restores the v11.06 heartbeat.
  - In LIVE, a long wait gets the honest amber "busy" box instead of the red one.

## MEDIUM 1: waiting for the user's key press is reported as "NoovexOS is busy" and covers the prompt

- Where: net/http/http.c:249 (kidle now bumps g_nest_beat), sys/native/syscall.c:125 (waitkey bumps g_nest_beat and calls busy_pump), graphics/ui/input.c:63 (Lua waitkey calls busy_pump).
- kidle is used only in key-wait loops (checked with grep):
  - shell/commands/prompt_and_dispatch.c:4 sh_readpw (sudo or password prompt)
  - :13 sh_yesno (apt "continue? [Y/n]")
  - terminal_keys_and_render.c:229 ("Full name:")
  - processes_devices_wifi_bringup.c:56 (6 s key wait)
  - Lua waitkey
- The result: 5 s after any of these prompts appears, an amber box at y=30..~290, x=40..1140 is drawn straight to the framebuffer every 0.5 s. It says "the desktop is waiting inside a task that has not finished". It sits over the Terminal or the program window where the user is typing, and the live terminal fills the whole screen. Native or Lua games waiting for "press any key" get it too.
- Why the box isn't needed: the system isn't busy, it's waiting for the user. Since c2's own xapps.c change, the Terminal is open when a Terminal=true app prompts, so the prompt is already on screen.
- Patch (a key wait counts as alive when its prompt is on screen; it still gets the box when the Terminal is hidden, which was the cs1 case):
```
--- shell/terminal.c   (add after term_prompt)
+int term_prompt_visible(void){ return g_state==ST_LIVE||(WN[W_TERM].open&&!WN[W_TERM].min); }
--- net/http/http.c:249
-void kidle(void){ extern int xhid_n; extern void xhci_mouse_poll(void); extern volatile u32 g_ml_beat, g_nest_beat; g_ml_beat++; g_nest_beat++; if(xhid_n)xhci_mouse_poll(); if(kh==kt)cpu_idle(); }
+void kidle(void){ extern int xhid_n; extern void xhci_mouse_poll(void); extern volatile u32 g_ml_beat, g_nest_beat, g_nest_live; extern int term_prompt_visible(void); g_ml_beat++; if(term_prompt_visible())g_nest_live++; else g_nest_beat++; if(xhid_n)xhci_mouse_poll(); if(kh==kt)cpu_idle(); }
--- sys/native/syscall.c:125 (CPU-0 branch)
-            if(c0){ g_ml_beat++; g_nest_beat++; busy_pump(); ...
+            if(c0){ extern volatile u32 g_nest_live; g_ml_beat++; g_nest_live++; busy_pump(); ...
   (also change the extern list: g_nest_beat -> g_nest_live)
--- graphics/ui/input.c:63
-int  nvxlc_waitkey(void){ extern void kidle(void); extern void busy_pump(void); int k; while((k=kpop())<0){ busy_pump(); kidle(); } return k; }
+int  nvxlc_waitkey(void){ extern void kidle(void); extern void busy_pump(void); extern volatile u32 g_nest_live; int k; while((k=kpop())<0){ g_nest_live++; busy_pump(); kidle(); } return k; }
```

## MEDIUM 2: amber and red boxes show at the same time when a loop pumps only ui_pump

- Where: present_and_pump.c:149 (ui_pump bumps g_nest_beat but not g_ml_beat), crash.c:395 (cx_busy_tick runs before freeze_tick).
- Which loops: ui_pump-only loops on the desktop without g_frz_hold:
  - Wi-Fi joins and delays: wpa2.c:77, rtw8821ce.c:511/632, rtw_sec.c:198
  - wifi_scan_blocking (wifi_mgr.c:193)
  - the Terminal `wifi` scan loops (network_and_apps.c:331-333)
  - Wi-Fi auto-join, which runs from wifi_mgr_tick, a main-loop step (wifi_mgr.c:289)
- What happens: g_ml_beat stalls, so the red box shows after 5 s. g_nest_beat keeps moving, so the amber box shows too, drawn on top of the red one.
- The report says "If the pumps stop too, the normal red freeze box takes over". That is not what happens here, because the pumps never stopped.
- Patch (the red box wins; the amber box steps aside while it is shown):
```
--- core/crash.c:382
-    if(g_ml_top!=lt||g_nest_live!=ll||!g_ml_top||g_cx_panicking||g_frz_sleep||g_preempt_on||(g_state!=CX_ST_DESKTOP&&g_state!=CX_ST_LIVE)){
+    if(g_ml_top!=lt||g_nest_live!=ll||!g_ml_top||g_cx_frz_shown||g_cx_panicking||g_frz_sleep||g_preempt_on||(g_state!=CX_ST_DESKTOP&&g_state!=CX_ST_LIVE)){
```
- g_cx_frz_shown is a file static at crash.c:54, so it is in scope.

## MEDIUM 3: the amber box ignores g_frz_hold, and its Esc hint promises what the code doesn't do

- Where: core/crash.c:372 (Esc text), crash.c:389.
- Known holds now show the amber box after 5 s, because their network waits go through busy_pump, which bumps g_nest_beat:
  - apt/Store installs (fs/pkg/apt/deb_archive.c:77/169/180/260 all hold)
  - sysroot.c:133
  - the browser's forced sync (frames.c:221)
- That may be acceptable as honest feedback. But the text says "Press Esc to ask it to stop (works for web pages and network waits)". g_busy_esc is read only in apps/internet/browser/glue/startup.c:53, the browser's JS call budget. No network or websock wait checks it (grep), so Esc does nothing during apt, wget or Wi-Fi.
- busy_pump (present_and_pump.c:138) already sets g_busy_esc on Esc, so the box's own Esc handling is redundant.
- Patch:
```
--- core/crash.c:372
-    ... cx_s(&b,g_cx_busy_esc?"Esc pressed - asked the task to stop (only web pages and network waits can stop early)":"Press Esc to ask it to stop (works for web pages and network waits). If a program waits for a key, type it."); ...
+    ... cx_s(&b,g_cx_busy_esc?"Esc pressed - a busy web page will stop; other tasks must finish":"Please wait - it is still working. Esc stops a busy web page. If a program waits for a key, type it."); ...
```
- Optional: skip the box while g_frz_hold is set and the last crumb or g_in_what belongs to apt (pk_*). Not required.

## LOW 1: NMI box with IF=1 is drawn while CPU 0 is still running

- Where: core/crash.c:336-340.
- If CPU 0 is alive but IRQ0 stops arriving, the main loop keeps presenting frames and overwrites the box within one frame. The box can flicker or be missed.
- A false positive is also possible on boards where the 8254 PIT is clock-gated in deep idle. That is speculative, and nmiwd_check already skips the polled-timer case.
- The `g_frz_hold` check in the IF=1 branch is redundant: nmiwd_check:327 never sends the NMI during a hold. That's harmless.
- No patch needed. If you want the box to stay visible: in crash_tick, while g_nw_drawn is set, skip compose_present.

## LOW 2: Terminal=true apps don't echo the command

- Where: apps/x11/xapps.c:230.
- open_win(1) is correct, since W_TERM==1. The other launchers (editor.c:43, input.c:446) also print `term_puts(cl); term_putc('\n');` before run_cmd, so the user sees what started. Cosmetic.
```
-        ... b[i]=0; open_win(1); run_cmd(b); ...
+        ... b[i]=0; open_win(1); { extern void term_puts(const char*); extern void term_putc(char); term_puts(b); term_putc('\n'); } run_cmd(b); ...
```

## Checked and OK
- g_ml_top has a single writer (main.c:304). The `!g_ml_top` check keeps the box off before the main loop starts. CX_ST_LIVE=7 matches enum ST_LIVE.
- busy_pump's same-tick and `in` guards bump at most once per tick. us_pump, up_rd/up_wr and desk_nv_progress don't bump g_nest_beat, so the update and NVIDIA screens get no amber box (claim confirmed).
- crash_panic: g_frz_hold=1 and g_cx_panicking are set before the in_panic check. crash_panic never returns (cpu_run_on, then cx_panic_wait, then reboot), so the hold being permanent is fine.
- syscall 51 on an AP:
  - It no longer sets the global g_frz_hold. Before, any AP job waiting for a key silenced CPU 0's red box; that was a real bug and it's fixed.
  - It no longer polls xHCI from an AP.
  - BKL drop and retake match case 71.
  - IF is left off between iterations, which is harmless: sysret/iret restores rflags.
- crumb_dump is lock-free and safe to call from the timer IRQ. cx_busy_draw costs about 1100x300 pixels every 0.5 s in IRQ context, which is acceptable.
- The claimed tests (ccheck on 7 units, usb check 12/12) are plausible. None of them exercise the box logic, and there was no QEMU run. Suggested QEMU check after the fixes:
  - in the boot shell (LIVE), run `wget` on a slow URL for more than 30 s: expect no red box and no CRASH.TXT;
  - on the desktop, run `sudo` in the Terminal and wait 10 s: expect no amber box.
