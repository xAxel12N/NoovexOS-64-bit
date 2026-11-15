# NoovexOS 64 — handoff v11.04

Released 2026-11-14. ISO sha256 `9e1d4c599cf8401faa8446f7fe63196005142e0dc399e173932a0155e9a1d3f1`.
Checks: 84 passed, 0 failed (build, static, analyze, usb, nvidia, drivers, wifi, qemu incl. SMP, linux). Not tested on real hardware. v11.03 (drag-end freeze fixes) is still untested by the owner, and v11.04 includes those fixes.

## Team: 22 agents, about 30 minutes
- **Safety searchers S1–S4:**
  - sys/linux: mostly safe; getdents d_reclen re-read is MEDIUM (not fixed).
  - fs and USB-stick FAT: 2 HIGH (fixed).
  - net and Wi-Fi rx: no memory bugs. PEAP sends the MSCHAPv2 answer without checking the certificate (MEDIUM, not fixed).
  - browser decoders: 4 HIGH (fixed).
- **Bug hunters B1–B4, reporting to the coders:**
  - USB stick re-detect and stuck keys (fixed).
  - Files/Writer/Editor data loss (fixed).
  - Wi-Fi auto-join killed by one lost frame, DHCP never renewed, cut-off downloads (fixed).
  - Shutdown without sync and USB-stick write-path corruption (fixed).
- **Brainstormers I1–I5:** each checked what already exists first. Ideas coded: `report`, `log`/`dmesg`, USB unplug toast, Win+D, Win+arrows with restore fix, Ctrl+Alt+U / `usb restart`. Not coded: real Safe Mode, charger and headphone toasts, an image fuzzer, a random stack canary, SMEP/UMIP opt-in.
- **Coders C1–C5:**
  - C1: web safety, 2 rounds.
  - C2: kernel safety plus network.
  - C3: v11.03 MEDIUMs, editor read-only, Files/Writer/Editor fixes.
  - C4: storage safety.
  - C5: the ideas.
- **Reviewers R1–R4:** no HIGH. The lead fixed 2 data MEDIUMs: FAT12 sticks are refused, and the stick is flushed before a USB restart.

## Owner test
1. Same as v11.03: open and drag Files, Settings, Terminal and Task Manager, with and without snapping. If it freezes, wait for a red box and photograph it.
2. New: type `report` in Terminal and photograph the summary. If a USB stick is in, REPORT.TXT is on it.
3. Try Win+D, Win+arrows and Ctrl+Alt+U.

## Not fixed / next
- Reviewer MEDIUMs:
  - Win keys also reach the focused app.
  - files_vol_tick rescans every 3 s.
  - DNS does not fall back to public servers on SERVFAIL.
  - DHCP renew drains the NIC while sockets are open.
- getdents d_reclen (S1); PEAP certificate check (S3); vfat mount BPB checks (S2 #5/#6); SVG `<use>` and composite-glyph hangs, cmap loops (S4 medium).
- From earlier handoffs: camera code 17, XFAIL-halt, NMI watchdog.
