# Forge report: rtw88 multi-chip core (branch forge)

All 5 steps are done and committed. Every touched C file compiles with ccheck and adds no new warnings. The host tests pass: scan, mgmt, crypto (32) and the new chip test (5322 old/new trace compares, 0 failures).

## Commits (on top of 0aef5934)
- 817734aa: step 1. Bus layer. `rtw_r8..rtw_w32` call through `g_rtw_bus`. The default is `rtw_bus_pcie`, the same volatile MMIO accesses as before.
- 47c775dd: steps 2, 3 and 4 in one commit. They touch the same lines, so they could not be split into commits that each compile.
- 9dc14593: step 5. One new `wifi diag` line, plus `wifi iqk on|off` (with no argument it runs IQK now).
- f71f90ae: host test, plus a new `g_wifi` entry in tools/check.py. I did not run check.py; I ran the same commands by hand.
- e455ef64: the shell banner shows the detected chip (or "not supported"). README updated.

## Files
- New: drivers/net/wifi/realtek/rtw88/chip.c. It has:
  - the chip registry (`rtw_chip_find`, `rtw_chip_at`, `rtw_chip_count`, `rtw_chip_select`)
  - the alias table: B821, C82A and C82B map to 8821C; C82F maps to 8822C
  - `rtw_pwr_seq_run`, `rtw_rsvd_boundary`
  - the H2C ring: `rtw_h2c_ring_setup`, `rtw_h2c_pkt_send`
  - the `rtw_chip_calibrate` guard and `rtw_chmark`
- New: drivers/net/wifi/realtek/rtw88/chips/rtw8821c.c, which defines `const rtw_chip rtw8821c_chip`. All 8821C code moved here unchanged: power-on table, power-off (rtw88 act->cardemu), efuse parse (MAC at 0xD0, RFE 0xCA, xtal 0xB9), WMAC init, BB/RF enable, phy_post (xtal cap), coex wifi-only, rfe/antenna switch, set_channel, TX power, IQK, tables, page table {16,16,16,14,1}, rqpn {2,2,1,1,0,3}, firmware `_binary_rtw8821c_fw_bin_*`. The rtw_tables_cond.h include moved here from rtw_efuse.c.
- New in the core, in rtw_efuse.c:
  - `rtw_rf_read` / `rtw_rf_write(path, addr, mask, val)`. They use the chip's rf_base/rf_sipi (0 means {0x2800,0x2C00}/{0xC90,0xE90}). `rf_mix=1` gives 8822C behaviour: direct writes, except RF 0x00 which goes through SIPI.
  - `rtw_bb_write_mask` / `rtw_bb_read_mask`, `rtw_efuse_load`, `rtw_efuse_apply`.
- Changed in rtw8821ce.c (the core):
  - `rtw_mac_trx_init` now computes its values from the chip. For the 8821C they come out the same: boundary 460, H2C queue page 500, RXFF 16127, queue map 0xC5A0.
  - `rtw_set_channel_2g`, `rtw_set_txpwr` and `rtw_coex_wifi_only` are now thin wrappers around the chip ops.
  - `rtw_lte_write` / `rtw_lte_mask` are exported.
  - The MAC address now comes from `g_rtw_efuse.mac`.
  - The beacon-queue boundary now comes from `rtw_rsvd_boundary()`.
  - RF table writes use `g_rtw_rf_path`.
- rtw_sec.c: `rtw_phy_power_on`, `rtw_wmac_init` and `rtw_phy_post` moved to the chip file. The MAC and RFE now come from `g_rtw_efuse`. IQK runs after the channel set in `rtw_wpa2_join`; rtw8821ce.c's `rtw_assoc` does the same.
- shell/commands/processes_devices_wifi_bringup.c:
  - `wifi init` calls `power_on`.
  - `wifi fw` and `wifi load` use the chip's firmware.
  - `wifi allbuilt` calls `phy_init`, then efuse, then `mac_init`, then the chip tables per path, then the MAC address, then `phy_post`. This is the same order as before.
  - `wifi rings` also sets up the H2C ring.
- net/wifi/wifi_mgr.c: `wifi_up` ends with `rtw_chip_calibrate`. The adapter name shows the chip name, but not on the 8821CE, which keeps its old text.
- graphics/ui/desktop/app_tiles_and_icons.c: `rtw_find_mmio` uses `rtw_chip_select`, so `g_rtw_chip` is chosen from the PCI ID. It accepts the same 4 IDs as before, plus any chip that registers.
- units.list: adds chip.c and chips/rtw8821c.c.
- Tests: tools/tests/wifi/chip_gen.py, chip_main.c, chip_ref_v1098.c (the frozen v10.98 code, renamed old_*).

## Contract changes to rtw_chip.h (Forge owns it; Bridge and Relay need to know)
- `txff_size`, `rxff_size` and `txff_pages` are now u32. The old u16/u8 could not hold 65536, 262144 or 2048.
- New `u8 csi_pages`. The 8822C needs 50, and the reserved-page boundary depends on it.
- New op `void (*phy_post)(u32 m)`: crystal cap and BB/RF final setup after the tables.
- New `u32 rf_base[2], rf_sipi[2]; u8 rf_mix;`. 8822B can leave them 0. **Relay (8822C) must set** rf_base {0x3C00,0x4C00}, rf_sipi {0x1808,0x4108} and rf_mix=1.
- New type `rtw_pwr_cmd {off,mask,val,cmd}` (cmd 1=write, 2=poll, 3=1 ms delay), run with `rtw_pwr_seq_run`. Chips can write power_on as a table.
- New helper prototypes: `rtw_chip_select`, `rtw_rsvd_boundary`, `rtw_h2c_ring_setup`, `rtw_chip_calibrate`, `rtw_efuse_load` / `rtw_efuse_apply`, `rtw_rfe_type`, `rtw_chmark`, `g_rtw_coex_ok`, `g_rtw_rf_path`, plus the IQK and H2C counters.
- The header comment now spells out the core bring-up order and which ops are optional.
- Required ops: power_on, efuse_parse, mac_init, phy_init, set_channel, set_tx_power. Optional: power_off, calibrate, phy_post, rfe_setup, coex_wifi_only.
- `coex_wifi_only` stays void. A chip reports failure by setting `g_rtw_coex_ok=0`.
- `set_channel` must call its own rfe_setup and tx power.
- Registration: chip.c holds `extern const rtw_chip rtw8822b_chip __attribute__((weak))` and the same for `rtw8822c_chip`, and skips null entries. Bridge and Relay only define their struct and add their file to units.list. 0xC82F is already aliased to rtw8822c_chip.
- The agc_btg table is applied only when RFE is 2 or 4. That rule comes from the 8821C; other chips leave agc_btg_tbl empty.

## Behaviour on the owner's RTL8821CE
- **Unchanged register writes** (proven by trace compare). These produce exactly the same writes, values and delays as v10.98:
  - set_channel, over 10 RFE types × efuse valid/invalid × coex on/off × previous band × 13 channels (2.4 and 5 GHz)
  - coex
  - MAC TRX init
  - mac_init (WMAC)
  - phy_init and phy_post
  - power-on
  - 4000 random masked RF writes and every RF table write
- **New on purpose (steps 4 and 5):**
  - `wifi rings` programs the H2C ring registers: 0x1320, 0x1328 and the 0x1330 index clear.
  - At the end of `wifi_up` and after the channel set in each join, the driver sends H2C GENERAL_INFO (fw TX boundary 48) and PHYDM_INFO (RFE, 1T1R, cut, antenna A) once per power-on. Upstream sends these after the HCI starts. Then it sends the IQK packet and polls RF 0x08 for 0xABCDE (300 × 20 ms, as upstream) and clears it.
  - The guard: if the ring is not set up, the firmware is not running, the card does not fetch the packet within 5 ms, or IQK times out, it logs (join log plus klog) and carries on. After one failure IQK stays off until Wi-Fi restarts, so the worst case is a single 6 s wait.
  - `wifi iqk off` stops all of these H2C packets. The H2C ring registers are still set up.
- Log text changes only: the `wifi init` poll-timeout line now reports the last failing offset and a count. The banner names the chip.

## How it was tested
- The commands from `g_wifi` in check.py, run by hand: scan_gen and mgmt_gen still pass, because the functions they extract were not moved.
- The new chip test: `python3 tools/tests/wifi/chip_gen.py <kdir> out.c tools/tests/wifi/chip_main.c && gcc -O1 -w -no-pie -fsanitize=address,undefined`, which prints "chip ok". It covers:
  - lookup by ID, including aliases, a strong 8822B overriding the weak declaration, and an absent 8822C
  - completeness of the 8821C ops and tables
  - the old/new trace compares
  - RF path selection: path A SIPI, path B refused on a 1-path chip, path B 0xE90/0x2C00 on 2-path, direct writes with RF00 via SIPI on mix
  - the H2C ring: descriptor layout, QSEL 19, seq, index wrap over 100 packets, full after 31 when stuck, recovery, card gone
  - IQK: success path, segment IQK when connected, timeout (about 6 s simulated, then off), stuck ring, firmware not running
- Mutation check: changing one value in set_channel, MAC TRX init or the power table makes the test fail at the exact write.
- A partial link of all wifi objects shows only the expected undefined symbols (weak 8822B/8822C, firmware blob, existing externs).

## Not tested (needs the lead's build and the owner's hardware)
- The full kernel build/link. Weak undefined symbols in a static initializer resolve to 0 with this linker setup, but that is not confirmed in the real link.
- **Whether the 8821C firmware accepts H2C packets on our ring, and whether IQK answers 0xABCDE.** Ask the owner to run `wifi on`, then `wifi diag`. The "chip" line shows commands sent/failed and the IQK result. If IQK fails, joining still works, and `wifi iqk off` restores the old flow.
- power_off is defined but never called; wifi_radio_off still only pauses TX/DMA, as before.
- USB bus ops: only the struct exists. `rtw_h2c_ring_setup` returns -1 on non-PCIe buses.

## Open issues / notes for the lead
- `rtw_cond_setup` still reads the package bit from logmap[0xCA]&0x20 (8821C layout). The 8822B/C package bit may differ; Bridge/Relay should check.
- rtw_bw20 and the TX power code read 8821C efuse offsets directly from logmap. That is fine because they are in the chip file.
- `rtw_rx_rssi` in the core still uses the 8821C LNA tables (it chooses by rfe==0). Other chips will show approximate RSSI until it becomes a chip op.
- Merge: Bridge and Relay will each add lines to units.list next to mine. Small text conflicts are likely there, nowhere else.

## Integration of bridge + relay (lead request)
Commits: 204967d0 (merge bridge; my rtw_chip.h kept), 3e27adeb (merge relay; build.sh embeds all three firmware blobs, units.list lists chip.c plus rtw8821c / rtw8822b(+_tables) / rtw8822c(+_tables)), 3cb835d3 (core integration).

What the core now does for the two new chips (lead's points 1-7):
1. Contract: `txff_pages` is u32. New fields `tables_in_phy_init`, `cond_mode` (RTW_COND_8821C / RTW_COND_FULL_RFE / RTW_COND_CHIP), `sys_func_en` (OR-ed into 0x0003 in rtw_sys_cfg; 0 means 0xD8, 8822B sets 0xDC) and `fw_rxff_size` (informational only, set to 12288; the core uses rxff_size for the RX FIFO boundary, as rtw88 mac.c does). `rtw_h2c_cmd` is now in the contract. phy_post, rf_base/rf_sipi/rf_mix and csi_pages are as described above.
2. Path B tables: the core applies `rf_tbl[1]` to path B, because `rtw_apply_one` sz 5 writes through `rtw_rf_write(g_rtw_rf_path, ...)`. If a chip sets `tables_in_phy_init` (8822C does), `wifi allbuilt` runs efuse load+parse, cond setup and mac_init (+ mac table if any) BEFORE phy_init, and loads no bb/agc/rf tables itself. 8821C/8822B keep the old order.
3. Table conditions: `rtw_cond_setup` uses cond_mode. 8821C keeps its old logic, 8822B gets FULL_RFE (package 15, whole RFE byte; cut unchanged), 8822C gets CHIP (core leaves g_rtw_cut/pkg/rfe alone).
4. `rtw_h2c_pkt_send` always writes the sequence number (word 1 bits 31:16), whatever the chip leaves there. `rtw_h2c_cmd` stays exported from rtw_sec.c.
5. set_channel is documented in the header as taking the primary channel. The core still calls it with RTW_BW_20.
6. Registry: 10ec:b822 maps to rtw8822b_chip and 10ec:c822 to rtw8822c_chip, through weak externs in chip.c. **The c82f alias now points to Relay's `rtw8822c_chip_c82f`, not to rtw8822c_chip.** I filled in both 8822C structs: TX FIFO 262144 / 2048 pages, csi 50, RF bases/SIPI/mix, tables_in_phy_init, cond CHIP. 8822B: sys_func_en 0xDC, cond FULL_RFE.
7. Tests, all passing; I ran check.py's `g_wifi()` on its own, not the whole check.py:
   - scan, mgmt, crypto
   - chip test: 5322 v10.98 trace compares, so the 8821C behaviour is still byte-identical
   - Bridge's rtw8822b_test.sh: all checks passed
   - Relay's rtw8822c test: all passed. It needed `int g_rtw_rf_path;` added to rtw8822c_gen.py, because it extracts rtw_apply_one.
   - check.py g_wifi now also runs the 8822B and 8822C tests when /tmp/ref/linux is present.
   - A partial link (`ld -r`) of all wifi objects resolves every chip symbol; only the firmware blobs and the existing externs stay undefined. No duplicate symbols.

Other core changes:
- 0x060C=15 in allbuilt is now written only on the 8821C, because the 8822B/C mac_init set their own RX packet limit.
- If a chip's calibrate op does not set `g_rtw_iqk_last`, the run counts as done, so the 8822B/C IQK is not switched off by the guard.

Open items for the 8822 chips (not done):
- `wifi_pci_prepare` writes the 8821C MDIO value (0x09 = 0x6380) on every chip. The 8822B/C PCIe PHY tables are not ported (Bridge's note).
- The efuse dump uses the 8821C-style prep (0x34/0x37) and 512/96 sizes. Relay says the 8822C fields it needs are below 0x200.
- `rtw_rx_rssi` still decodes RSSI with the 8821C LNA tables.
- The core never asks for 40/80 MHz, so Relay's HT40 secondary-channel heuristic is never used.
- Nothing has run on 8822B/C hardware.

## Fix round (lens-review / scope-review)
I merged main first: 054cf449, where IQK is off by default. Then one commit per item. ccheck shows no new warnings. Tests: scan, mgmt, crypto, the chip test (5322 compares against the frozen v10.98 code, 0 failures), rtw8822b_test.sh and the rtw8822c test all pass. I ran check.py's `g_wifi()` on its own (11/11 ok), not the whole check.py.

1. **27a0046f: PCIe PHY per chip.** New `pcie_phy` op.
   - 8821C keeps exactly its old MDIO write, 0x09=0x6380 on gen1.
   - 8822B writes the Linux cut-C gen1+gen2 set (0x01/02/08/09/0A/20/21/26/29/2A), and only on cut C.
   - 8822C writes nothing, because the Linux tables are empty.
   - The log and `wifi diag` say "not needed" when a chip has nothing to write.
   - Tested in the chip test and in Bridge's test.
2. **10e5c839: 8822B/8822C IQK.**
   - Both set `g_rtw_iqk_last`: -1 when the command is not taken, -2 on timeout, done otherwise.
   - Both stop if `rtw_h2c_pkt_send` fails.
   - Both call `ui_pump()` in the IQK poll loop. The 8822C also calls it in its RFK handshake loops.
   - Tests cover success, send failure and timeout; in the timeout case the UI is pumped at least 70 times.
3. **45b24c14: eFuse sizes per chip.** I checked them against rtw88: 8821C 512/512/96, 8822B 1024/768/96, 8822C 512/768/124.
   - New fields `efuse_phy/log/prot`; 0 means the 8821C sizes.
   - The maps are now sized for the largest chip (1024/768).
   - The 8821C reads exactly as before; the test checks its sizes.
4. **8636233d: H2C ring only when IQK is on.**
   - With IQK off (the default), `wifi rings` no longer touches 0x1320/0x1328/0x1330, so the owner's card writes exactly the v10.98 start-up registers.
   - `wifi iqk on` sets the ring up straight away, and a calibration sets it up if it is missing.
   - The test checks that IQK off writes no ring registers.
5. **a3459b06: path count and fw_name.**
   - New `rtw_rf_paths()`: BIT_RF_TYPE_ID from SYS_CFG1, capped at n_paths, as Linux rtw_chip_parameter_setup does. It is used for the PHYDM info packet and the per-path RF table loop. On the 8821C it is always 1.
   - 8822B fw_name is now "rtw8822b_fw.bin".
   - **8822C RF order not changed.** Linux rtw8822c.c:5388 has `.rf_tbl = {&rtw8822c_rf_b_tbl, &rtw8822c_rf_a_tbl}`, and phy.c:1925 loads rf_tbl[0] first, so Linux loads path B first. Relay's order already matches Linux, and Lens finding #6 was wrong.
6. **fa467837: captive-portal check no longer blocks.**
   - The blocking `http_fetch_to` after DHCP is gone. The probe is now a background request through the existing web-request service (`nwk_net_start`, the "net" service in apps/system/services.c).
   - It is polled once per Wi-Fi tick with `nwk_net_event`, given up after 3000 ticks (about 30 s), and cancelled on disconnect or connection loss.
   - The decision rule is unchanged: status 200-399 other than 204 means sign-in is needed.
   - A `?n=<ticks>` query string keeps the web cache from answering instead of the network.
   - Not testable on the host. If `nwk_net_start` refuses (offline mode or background requests off), no check runs and `g_wm_portal` stays 0.
