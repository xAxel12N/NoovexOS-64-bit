# NoovexOS 64 — handoff v11.05

Released 2026-11-14. ISO sha256 `983702f9931af3f7828d327797b54793518bb2416fbb3d979c52a7fce58668a7`.
Checks: 71 passed, 0 failed (build, static, analyze, usb, nvidia, drivers, qemu incl. SMP, linux). USB mouse decode: 68 checks, including the exact Biltema descriptor. Not tested on real hardware.

## Problem (v11.04, owner photos)
- Biltema receiver 248A:16BB: the wheel never works and left click only sometimes. `usbhid` showed 3-byte boot packets (`00 FD 01`) on a report-mode endpoint. The descriptor declares input IDs 1 (mouse, 5 bytes), 3 (consumer, 5 bytes) and 4 (system, 2 bytes).
- Red box: "CPU 6 waits for the kernel lock held by CPU 0, last step: input and window actions (stage 7)".

## Agents
- **M1, clicks:** each input report ID's expected length is stored. Shorter packets are boot format. A sticky per-endpoint boot flag is cleared by a full-length report. Deviation: a short no-motion packet that starts with the ID of a mouse whose axes are wider than 8 bits is still ignored (the existing phantom-click test).
- **M2, wheel:**
  - Cause found: the v11.02 EP0 back-off could skip SET_PROTOCOL without sending it.
  - Enumeration now sends SET_PROTOCOL plus SET_IDLE(0) to all interfaces and re-sends a failed SET_PROTOCOL once with the back-off cleared. GET_PROTOCOL is dropped.
  - Boot packets on a report-mode mouse trigger SET_PROTOCOL(report) from the poll path: 3 tries, at least 100 ticks apart, never inside a wait.
  - `usbhid` shows the format, the result and the retry count.
- **M3, red box:** names the handler (`g_in_what`/`g_in_win`/`g_in_arg`), for example "Terminal command 'nvgsp 3d'". Settings shows only "page N". When the system recovers (box not redrawn for over 2 s), it is saved to crash-report.txt and CRASH.TXT, a toast appears, and the box is erased. Nothing is saved if the machine never recovers.
- **M4, handlers:** Wi-Fi connect, print test, NVIDIA Settings actions and app uninstall are deferred one main-loop pass, with "Working...". They still hold the BKL.

## Owner test
1. Left click without moving the mouse, right click, and middle click. Then try the scroll wheel. Run `usbhid` and photograph the "format" line (boot or report, plus retries).
2. If it freezes: photograph the red box. It now names the click or command. After it recovers or after a restart, run `crash`.

## Not fixed / next
- Terminal commands run synchronously from the key handler while holding the BKL. Long nested waits never release the BKL (busy_pump/desk_nested_step). This is the most likely class for the stage-7 freeze.
- If the receiver keeps refusing report protocol, the wheel is impossible (boot packets have no wheel byte).
- Everything listed under v11.04 "Not fixed".
