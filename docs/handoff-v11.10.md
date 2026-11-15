# NoovexOS 64 — handoff v11.10

**Status:** 93 of 93 checks passed (build, static, analyze, usb, nvidia, drivers, wifi, fs, qemu incl. SMP, linux, bt). Branch main. Release notes are at the top of docs/architecture-src/00-whatsnew.md. Reports are in docs/team-reports/q1/.

## New
- **Intel Wi-Fi (AX200/AX201/AX210/AX211)** is in the build. It covers scan, join and CCMP keys. It runs only when there is no Realtek card, so it is inert on your PC.
- **Bundled firmware** in /lib/firmware: 34 files (Intel Wi-Fi/BT, MediaTek BT, Realtek BT), with licences.
- **Realtek Bluetooth:** 15 chips (was 2).
- **Reviewer fixes:** Bluetooth no longer resets unrelated USB devices; Intel RX ring and command-group fixes; Intel 8260 BT fix; your Wi-Fi association byte is back to the old value.

## Please test on the real PC
1. Wi-Fi joins as before. After a download, `wifi diag` should show BA sessions > 0.
2. `bt up` / `bt scan` should work as before.
3. The taskbar/Start click test from v11.08.

## Open (reviews in docs/team-reports/q1/)
- A-MPDU: 2 MEDIUM security items (a forged or replayed frame can move the reorder window; A-MSDU duplicate check). They need the early PN check from ramp-review.md.
- Intel Wi-Fi:
  - WPA2 through the generic path;
  - rate control and HT;
  - AX201/AX211 on PCI function 3;
  - iteration caps on waits (riwx M1).
- wifi_hw: one-shot detection with firmware loading at boot (rhw M1); re-entrant TX from RX (rhw M2).
