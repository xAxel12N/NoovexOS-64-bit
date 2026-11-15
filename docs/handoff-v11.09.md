# NoovexOS 64 — handoff v11.09

**Status:** 90 of 90 checks passed (build, static, analyze, usb, nvidia, drivers, wifi, fs, qemu incl. SMP, linux, bt). Branch main. Release notes are at the top of docs/architecture-src/00-whatsnew.md. Reports are in docs/team-reports/p1/ (and w2/ for the scouts).

## New
- **RTL8821CE frame aggregation (receive):** accepts block-ack sessions, with a reorder buffer. `wifi ampdu on|off`; counters in `wifi diag`.
- **Wi-Fi driver interface** (wifi_hw) for a second chip driver.
- **Bluetooth firmware loaders for Intel, MediaTek and Broadcom USB adapters.** They run only when the ID matches; your Realtek path is unchanged.

## Please test on the real PC
1. Connect to Wi-Fi as usual. Run `wifi diag` after a big download (apt or the browser) and photograph it: BA sessions > 0 means aggregation is active.
2. If Wi-Fi is worse, run `wifi ampdu off` and say whether that fixes it.
3. `bt up` / `bt scan` should behave as in v11.08.
4. The v11.08 tests still apply: taskbar/Start clicks, the amber box.

## Not in this build
- **Intel Wi-Fi (iwx):** code on branches p-iwx/p-iwxi (transport, firmware loading, commands, scan). It still needs wiring into the build, join/keys, and AX210 PNVM. The integrator was stopped by the usage limit.
- A-MPDU TX, BAR handling, protected ADDBA.
