# rbt report (in progress)

## Firmware filenames needed (fw coder: bundle under /lib/firmware/rtl_bt/)
Loader reads /lib/firmware/rtl_bt/<name>. 8821C and 8822C stay embedded (not loaded from disk).
REQUIRED fw (one per chip):
- rtl8723a_fw.bin      (simple, raw, no config)
- rtl8723b_fw.bin
- rtl8723d_fw.bin
- rtl8821a_fw.bin
- rtl8761a_fw.bin
- rtl8761bu_fw.bin
- rtl8822b_fw.bin
- rtl8852au_fw.bin
- rtl8852bu_fw.bin
- rtl8852cu_fw_v2.bin  (note _v2 suffix, as FreeBSD/Linux ask)
- rtl8851bu_fw.bin
- rtl8922au_fw.bin
- rtl8852btu_fw.bin
Config (REQUIRED for rtl8723d and rtl8822b; optional for the rest, appended if present):
- rtl8723d_config.bin, rtl8822b_config.bin
- rtl8723b_config.bin, rtl8821a_config.bin, rtl8761a_config.bin, rtl8761bu_config.bin,
  rtl8852au_config.bin, rtl8852bu_config.bin, rtl8852cu_config.bin, rtl8851bu_config.bin,
  rtl8922au_config.bin, rtl8852btu_config.bin
(rtl8723a has no config.)

## Done (branch q-rbt, commit b2907422, worktree /home/claude/wt/rbt)
- Source verified: FreeBSD freebsd-src main usr.sbin/bluetooth/rtlbtfw/{rtlbt_fw.c,rtlbt_fw.h,main.c}, all SPDX BSD-2-Clause
  (same copyright holders already in our header). NOTICE updated.
- drivers/bluetooth/rtlbt_fw.h: rtlbt_ic_t gains `flags`; RTLBT_F_SIMPLE/CONFIG/MSFT/EMB, RTLBT_FWDIR, RTLBT_FWMAX (2 MiB); rtlbt_usb_listed().
- drivers/bluetooth/rtlbt_fw.c: IC table 2 -> 15 entries. NOTE: the FreeBSD table has 15 entries, not 16 (8723A, 8723B, 8723D, 8821A,
  8821C, 8761A, 8761BU, 8822C, 8822B, 8852A, 8852B, 8852C, 8851B, 8922A, 8852BT). 58-entry USB id table (count verified = 58).
  Non-embedded chips load /lib/firmware/rtl_bt/<fw> via vfs_open/vfs_read (bounded 2 MiB, freed on every path); missing fw ->
  "Realtek: firmware file missing: <path>", return -1, nothing sent. Config appended if present; CONFIG flag (8723D, 8822B) makes a
  missing config fatal with the file named. SIMPLE (8723A): raw download, no ROM query, epatch file refused (FreeBSD defines SIMPLE as 0
  so its 8723A path is dead; behaviour follows Linux btrtl). MSFT flag is stored only (no MSFT extension use yet).
- Owner 0BDA:C123 / 8821C, 8822C: flagged EMB, same blob names, same HCI command sequence (FC61, 1001, FC6D, [FC61], FC20..., 1001),
  same messages; never touches /lib/firmware (tested). build.sh:38 untouched.
- drivers/bluetooth/btusb.c: vendor-class (0xFF) claim now also for rtlbt_usb_listed(vid,pid) (was Broadcom vendors only); class 0xE0
  path unchanged; sub/proto 1/1 + alt 0 still required in btu_parse.
- tools/tests/bluetooth/hci_main.c: +21 checks (table/flags, 58 ids, disk loading with a host stub, optional/required config,
  wrong ROM, wrong chip file, 8723A raw, embedded chips never load from disk).
- Checks: ccheck rtlbt_fw.c + xhci.c (btusb) OK; `check.py --only bt,usb` 13/13 pass (bt: 64 checks).

## Untested / follow-ups
- No real hardware for any new chip; disk loading verified only via host stub (fw coder must ship the files listed above).
- Linux has more chips (8723CS/DS UART, 8852BE-VT etc.) and per-chip quirks (e.g. 8852C/8922A need extra setup in Linux btrtl);
  only FreeBSD's table was ported.
- MSFT vendor extension (opcode 0xFCF0) not wired up.
