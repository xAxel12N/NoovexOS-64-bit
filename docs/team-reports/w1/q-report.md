KEY: FREEZE NOT REPRODUCED in QEMU (v11.06 ISO, kernel ELF verified = build/kernel64.elf, copy /tmp/q/kernel64.elf). No looping function to report.
Start button, Start->item (About, Settings), taskbar app buttons, clock, all tray icons, quick-launch, hold/double/drag/right-click: all alive in 4 configs
(A: usbsim biltema -smp2; B: plain mouse -smp2; D: -cpu max, no PS/2, no tablet, mouse behind hub on 2nd xHCI -smp2; E: -smp4). Screen follows mouse_move, sendkey works,
heartbeat continues, crumbs show "@on_click<" (handler returns), only "lag: drawing the screen" 50-200 ms. Biltema boot-packet path makes no difference.
Only anomaly: run C (-smp 8 on a 2-core host): all 8 CPUs sampled in bkl_lock+0x90 (IF=0), g_bkl serving still advancing (convoy, not deadlock), main-loop idle section
(ml_mark 14 bkl_drop/idle/bkl_take) lag 4.4 s then 15 s after the Start click. Most likely host oversubscription artifact; noted because CPU0 re-queues behind every AP's
ap_loop bkl_lock (smp_sched.c:84) after each idle.
Conclusion: the trigger is something QEMU does not model (real receiver multi-interface/consumer packets, real xHCI timing, 12 real threads, Wi-Fi ui_pump nesting).
Helpers: /tmp/q/m.py (clean monitor cmds), /tmp/q/sym.py (addr->func on v11.06 ELF), /tmp/q/chk.sh (liveness check), /tmp/q/runC.sh, runD.sh, runE.sh. QEMU stopped at end.

# Q report (QEMU repro of Start/taskbar freeze, v11.06 ISO)
STATUS: booting v11.06 in QEMU (kernel ELF build/kernel64.elf verified identical to ISO kernel, copied to /tmp/q/kernel64.elf)
20:06 run A (usbsim biltema, -smp 2, usb-mouse): Start button still click -> menu opens, alive; Start->About item -> window opens, alive;
taskbar app button (Welcome) -> focus switch, alive; clock -> calendar popup, alive. NOT reproduced so far. Serial: crumbs "@ml:xhci_poll>" etc. print every loop (~20k lines/5s), lag lines only "drawing the screen" 50-120ms.
20:10 run A cont.: all 7 tray/indicator icons, Start->Settings, quick-launch terminal, sendkey typing, press-hold-2s on Start, double click Start, drag from Start, right-click taskbar + app button: ALL alive (screen changes on mouse_move, heartbeat continues, no new lag lines). NOT reproduced.
Next: run B plain mouse (no usbsim); run C -smp 8 + mouse on 2nd xHCI behind hub, no tablet.
20:13 run B (NO usbsim, plain QEMU usb-mouse, -smp 2): Start click, Start->About, taskbar app button, clock, Wi-Fi tray: ALL alive. Biltema boot-packet path makes no difference in QEMU.
20:15 run C (-smp 8 on a 2-core host, -cpu max, i8042=off, no tablet, usb-mouse behind hub on 2nd xHCI, usbsim biltema): system crawls after desktop; after Start click
  "lag: updates (boot partition) took 4461 ms" then 15119 ms (label = ml_mark 14 = the idle section: bkl_drop/cpu_idle/bkl_take). Sampling `info registers -a` x7: ALL 8 CPUs at bkl_lock+0x90
  (ticket spin, IF=0). g_bkl (0x4b74010) next-serving = 7..8 and serving keeps advancing (0xf9b0 -> 0x10434 in ~10 s), owner mostly -1 => NOT a deadlock: ticket-lock convoy, every handoff
  waits for the next ticket's vCPU thread to get a host core (8 vCPUs/2 host cores). Likely a QEMU oversubscription artifact, but shows CPU0's main loop queues behind every AP at bkl_take
  after idle (ap_loop takes the BKL on every wake, smp_sched.c:84). On real HW (12 threads) this would cost latency, not a hang - unless one AP holds the BKL a long time.
