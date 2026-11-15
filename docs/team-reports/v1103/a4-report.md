# a4 — QEMU drag-release repro (v11.02 ISO copy, /tmp/a4)
Setup: /tmp/a4/t/run.sh (copy of /tmp/repro/run.sh: -smp 2 TCG, 2x qemu-xhci, usb-kbd+usb-mouse on xh1, tablet/hub/usb-audio/usb-storage on xh2), mouse_set 4 (HID Mouse). Drag = mouse_button 1, 8px steps, mouse_button 0. After each release: 3 s, screendump, mouse_move, screendump, 6 s heartbeat check.
NO FREEZE REPRODUCED. Every case: pointer move changes screen, "alive" heartbeat + lag lines continue.
Cases (all PASS): Welcome drag middle; Welcome drag to left edge; Welcome drag to right edge, partly off-screen (x≈975..1280+);
Terminal (taskbar) drag to top edge -> snapped/maximized; Browser drag to left edge; Files drag middle; sendkey after.
Not done (time): Task Manager, Settings, Gears, Camera, Voice Recorder via Start menu (no USB camera in QEMU anyway, so camera/recorder start paths are untestable here).
Serial tail: /tmp/a4/a4-serial.log (last: "[29510] alive: main loop running", lag ~50-70 ms).
Conclusion: freeze on drag-end is NOT reproducible with qemu HID/xHCI; likely depends on real-HW factors (camera/mic USB devices with live apps, 2nd xHCI receivers, wireless combo HID report format, or real SMP timing). Confidence the bug is in generic wm/input drag-release logic: low (~20%).
