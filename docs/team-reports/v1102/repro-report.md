# repro report (v11.02) - FINAL: freeze NOT reproduced in QEMU

## Setup
- Copy of /home/claude/v1101.iso at /tmp/repro/boot.img (repo untouched). Launcher /tmp/repro/run.sh, helpers /tmp/reprotools/.
- qemu-system-x86_64, TCG (no /dev/kvm), -smp 2, -m 2048, flags as tools/check.py g_qemu (IDE boot img, serial file, HMP unix monitor).
- USB: xh1 = qemu-xhci with usb-kbd + usb-mouse; xh2 = qemu-xhci with usb-tablet (port1) + usb-hub (port2) carrying usb-audio (2.1) + usb-storage (2.2).
  Serial: "usb: several xHCI controllers found", hub, "usb audio: QEMU USB Audio", "USB stick behind a hub". vmmouse is also on.
- HMP `mouse_set 4` routes HMP mouse to the USB HID mouse (relative, 1:1 pointer). Boot lands in shell; `desktop` command opens the desktop.
- Liveness checks: serial heartbeat "[t] alive: main loop running" every 500 ticks, screendump hash, pointer visibly moving/hovering after each click.

## Results (every click: pointer kept moving, heartbeat kept ticking, no panic/stall in serial)
| Action | Result |
|---|---|
| Start button | menu opens |
| Start > Drivers & Hardware (hover) | submenu: Gears, Camera, Voice Recorder, Benchmark, Task Manager, Settings |
| > Camera (no camera present) | opens, "No camera found", lists QEMU kbd/mouse/tablet/audio. No freeze |
| > Voice Recorder (usb-audio behind hub) | opens, no freeze |
| > Gears | opens, 5 fps CPU copy, "gpu3d: meshes built", no freeze |
| > Benchmark | opens, no freeze |
| > Task Manager | opens, no freeze |
| > Settings | opens, no freeze |
| device_del usb-mouse + device_add with Start>D&H submenu open | "usb: keyboard/mouse connected"; new mouse drives hover immediately. No freeze |
Only serial anomalies: "lag: drawing the screen ... took 50-650 ms" (TCG + Gears running), no stall/panic lines.

## Interpretation
- The freeze does not reproduce with QEMU's well-behaved xHCI/HID. QEMU HID never NAKs/times out/stalls EP0 the way real combo receivers can, so the v11.01 xh_ctrl timeout path (Stop Endpoint + Set TR Dequeue, Reset Endpoint on cc 3/4/6) and the command-wait event-ring polling are never exercised here. That is consistent with (not proof of) the prime theory: the freeze needs a real control-transfer timeout/stall or a real UVC/UAC device (Camera "code 17" on 0C45:1915) to trigger.
- No camera present = no freeze, so the Camera app UI path itself is not blocking; the real-HW difference is a present UVC device whose stream setup fails.
- Not tested (time): sendkey liveness after each click (keyboard verified only at shell), usbinfo/usbhid, Camera "Start" button with no camera, usb-host passthrough of a real camera.

## Suggested next repro steps
- Fault-inject in the driver (debug build) to force a control timeout / cc=6 stall on EP0 while the mouse/keyboard interrupt endpoints are armed, then click a menu item.
- Ask owner whether pointer still moves / clock ticks when frozen, and whether Gears (non-USB app) also freezes.
