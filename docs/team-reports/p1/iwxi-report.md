# iwxi report (p1): Intel iwx integration

Branch p-iwxi, commit 11d7f41c, worktree /home/claude/wt/iwxi. I committed only my own files. I did not touch iwxj's files (iwx_cmd.c, iwx_data.c, iwx_scan.c, iwx_pnvm.c).

## What I did
1. **Name reconciliation.**
   - iwx1's iwx.h already includes iwx_cmd.h and implements every extern that iwx2 declared: get_sc, tx_hcmd, tx_data, rx_poll, is_ax210, fw_api, fw_capa, hw_mac, now_ms.
   - The two headers share no macro or struct names, and the .c files share no static names.
   - iwx_glue.c now includes iwx.h, so the whole driver uses one API.
2. **Build wiring.**
   - The six iwx units are in units.list right after net/wifi/wifi_hw.c, and in kernel64.c after wifi_netdev.c, following the rtw pattern.
   - The kernel64.c unity build still compiles.
3. **Probe hook.**
   - In net/wifi/wifi_hw.c the probe now reads: `whw_probe(){ rtw_wifi_hw_probe(); if(!whw_n) iwx_wifi_hw_probe(); }`.
   - When the RTL8821CE has registered, no iwx code runs at all, so the owner's path is unchanged.
   - `iwx_wifi_hw_probe` (in iwx_glue.c) runs: PCI ID match, then `iwx_start()` (firmware plus ALIVE, all waits bounded), then `iwx_glue_register()` only once ALIVE.
4. **Shell commands.**
   - `iwx status` and `iwx up` sit next to `wifi`. The dispatch is in processes_devices_wifi_bringup.c and `iwx_cmd()` is in system_disks_packages_gpu.c.
   - `iwx up` = probe, start, register, then print the status. This lets the owner retry after copying firmware to /lib/firmware.
   - Both commands are listed in the Terminal Finder (apps/system/logs.c).
5. **NOTICE.** drivers/net/wifi/intel/iwx/NOTICE now covers every iwx file (iwx_pnvm.c too): the OpenBSD ISC licence, the Intel code under its BSD-3-Clause branch, and the fact that no firmware blob is committed.
6. **Tests.**
   - tools/check.py `wifi` group now builds and runs tools/tests/iwx/iwx_host.c with ASan and UBSan.
   - It also syntax-checks iwx_pcie.c, which enforces new `_Static_assert`s in iwx.h:
     - ctxt_info = 1792
     - prph_scratch = 1660
     - ctxt_info_gen3 = 104
     - TFD = 256, RX transfer desc = 16, wide cmd header = 8, rx_packet = 8, mpdu_desc = 64
7. **Static-check fix.** iwx_pcie.c used inline `mfence`, which failed the static check "no inline assembly outside arch/". It now uses `cpu_mb()`.

## Verification
- ccheck.sh passes with no warnings on all iwx .c files, logs.c and the iwx_glue.c change.
- wifi_hw.c and shell/commands.c have the same warning counts as before (4 and 40, all pre-existing misleading-indentation warnings).
- `python3 tools/check.py --only wifi,static`: 29/29 passed.
- build.sh was not run.

## Works in theory (no Intel hardware here)
- PCI match for AX200/AX201/AX210/AX211/BZ.
- TLV firmware load from /lib/firmware, with the missing file named in the log.
- Context info gen2/gen3 and the ALIVE wait.
- Registration as a generic `wifi_hw` driver.
- wifi_mgr then calls `init` (NVM plus init commands), UMAC scan v14, and RX of beacons into the scan list.

## Still stubbed / missing
- **Join:** MAC/PHY context, binding, ADD_STA, session protection.
- **Keys:** ADD_STA_KEY.
- **AX210/AX211 PNVM:** not loaded yet. The init commands on those cards will time out cleanly.

iwxj is working on all three. Two other limits:
- Scan v15-v17 (newer AX210 firmware) is refused.
- There is no stop/reset path.

## Follow-ups for the lead
- When iwxj adds iwx_pnvm.c, add it to units.list and kernel64.c after iwx_data.c, or the link fails.
- An Intel-only box blocks for up to about 4 s on the first Wi-Fi use while the firmware boots.
