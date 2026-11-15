# NoovexOS 64 — handoff v11.08

**Status:** 88 of 88 checks passed (build, static, analyze, usb, nvidia, drivers, wifi, fs, qemu incl. SMP -smp 4, linux, bt). Branch main. This is wave 1 of the 15-hour run (10 scouts, 10 hunters, 5 code searchers, 4 analyzers, 10 coders, 10 reviewers). Release notes are at the top of docs/architecture-src/00-whatsnew.md. All reports are in docs/team-reports/w1/.

## Please test on the real PC
1. Click the taskbar and the Start menu, then open an app from Start.
   - If it freezes, wait 5–10 s. An **amber "busy" box** or a red box should now appear. Photograph it: it names the stage and the last steps.
   - If nothing appears at all, try `crash nmiwd on` before testing again.
2. If clicks feel lost: `usbproto on` turns the old late SET_PROTOCOL retry back on (now off by default). Say whether that makes things better or worse.
3. `bt up` then `bt scan`: should list nearby phones and headsets with names. Check that Wi-Fi still works afterwards.
4. Camera and mic: `log` now shows each xHCI's context size (32/64). Send a photo if the camera still says code 17.
5. Calculator decimals; shutdown and reboot (should be quick, with no box).

## Known open items
- The taskbar/Start freeze has not been reproduced in QEMU. The fixes are the most likely causes: late SET_PROTOCOL, kernel-lock queueing on 12 threads, nested pumps. Amber/red boxes will name it if it remains.
- AltGr is treated as Alt on USB keyboards (fix belongs in hid_keyboard_mouse.c).
- Direct vfat writes are not yet covered by g_disk_busy.
- The #DF-on-web-stack path has never executed.
- Esc does not yet cancel HTTP waits.

## Wave 2 plan
Foundations first: a USB class/pipe API and a Wi-Fi hardware-ops split. Then rtw88 USB (8821CU/8811CU), OpenBSD athn (AR9271, ISC) and mtw (MT7601U, ISC), ure/rge Ethernet, FatFs/exFAT plus multi-stick storage, HDMI audio, GPIO/backlight/thermal. All licences were checked from the file headers (scout reports sc1–sc10).
