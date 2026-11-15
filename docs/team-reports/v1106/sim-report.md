# sim report: FREEZE NOT REPRODUCED in QEMU (no RIP/function to report)
Debug switch committed on v6-sim 08f396c9 (default off): boot-shell command `usbsim biltema` (type before `desktop`).
- controller_init.c: xhid_sim_biltema() picks the first non-kbd HID ep with a 52-byte descriptor (QEMU usb-mouse),
  runs hid_parse() on the exact Biltema descriptor (142 bytes), installs layouts/hasid=1/rep=1/mneed/mid/midm, VID/PID 248A:16BB in diag.
- hid_keyboard_mouse.c hid_xfer: `if(d==xhid_sim_e&&got>3)got=3;` before hid_report_decode/xhid_diag_rx (QEMU [btn,dx,dy,wheel] -> 3-byte boot packet).
- usbhid output in sim matches the owner's photo exactly: "248A:16BB report proto (IDs)", "layout id 1 mouse btn 0+5 X 8+8 Y 16+8 wheel 24+8", "id 3 consumer cc 0+32".
Run 1 (QEMU TCG -smp 2, /tmp/repro/run.sh): first left click without motion (landed on Welcome window), still left click on empty desktop,
second click, right click (desktop menu opens), left drag (selection rectangle drawn), 5 rapid clicks, double click: ALL alive
(screen changes on mouse_move, heartbeat "alive: main loop running" continues). The first boot packet triggered
"usb: asked receiver for full reports (try 1..3)" (real SET_PROTOCOL to QEMU from the poll path) without any hang.
Run 2 (fresh boot): reports started before the click (try 1..3 logged during desktop start); still left click, click on empty desktop, middle click: alive.
Not reproduced => the freeze likely depends on something QEMU does not model: real xHCI timing of the SET_PROTOCOL/SET_IDLE control
transfer on the receiver (ignored/STALL/NAK; QEMU answers instantly), real-HW xh_event_w with IRQs off, or the multi-interface
receiver (ids 3/4 on same ep, consumer/system packets) - the sim only feeds id-1 boot packets. M1 decode + g_marq UI path itself does not hang.
Saved kernel ELF for this ISO: /tmp/repro/kernel64-sim.elf (nm-able). Screens: /tmp/repro/{c1..c6,f1..f6}{a,b}.png, rg.png (menu + marquee).
