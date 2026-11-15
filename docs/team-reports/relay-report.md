# Relay report: RTL8822CE (rtw88 8822C) chip port

Branch `relay` in /home/claude/wt/relay. Commits: 4ba5415e (tables, converter, firmware), 7e77a516 (chip file, build wiring), 91067291 (host test).
No hardware test was possible. The port follows Linux rtw88 (GPL-2.0 OR BSD-3-Clause); the SPDX and copyright lines are kept in every ported file.

## Files
- `drivers/net/wifi/rtw88/chips/rtw8822c.c`: defines `const rtw_chip rtw8822c_chip` (RTL8822CE, 10ec:c822) and `rtw8822c_chip_c82f` (10ec:c82f, same ops and data). Linux maps both IDs to one hw_spec. Add both to the core chip list.
- `drivers/net/wifi/rtw88/chips/rtw8822c.h`: exports, `RTW8822C_TXFF_SIZE`, and the RQPN mapping values.
- `drivers/net/wifi/rtw88/chips/rtw8822c_tables.c/.h`: generated tables (agc, bb, rf_a, rf_b, array_mp_cal_init).
- `tools/rtw_table_conv.py`: the converter. It also handles 3-word DPK tables with BIT()/MASK tokens. Regenerate with:
  `python3 tools/rtw_table_conv.py <linux>/rtw8822c_table.c rtw8822c drivers/net/wifi/rtw88/chips/rtw8822c_tables.c drivers/net/wifi/rtw88/chips/rtw8822c_tables.h agc bb rf_a rf_b array_mp_cal_init`
- `assets/firmware/rtw8822c_fw.bin`: from linux-firmware. 202600 bytes, signature 0x8822, v9.9.15, sha256 3deecb31…ab03ba. Because the version is 9.9.15 (at least 9.9.13), the OLD_PAGE_NUM quirk is not needed.
- `build.sh`: an extra `ld -r -b binary` makes `rtw8822c_fw.o`, which is added to BLOBS. The symbols are `_binary_rtw8822c_fw_bin_start/_end`.
- `units.list`: adds the chip file and the tables file.
- Tests: `tools/tests/wifi/rtw8822c_gen.py`, `rtw8822c_main.c` and `shim/include/nvx/base.h`. The build command is in the header of the gen script.

## Size added to the kernel
- Tables: 369,856 bytes of .rodata.
- Chip code: about 22.5 KB (text 21,383, data 368, bss 816).
- Firmware: 202,600 bytes.
- Total: about 595 KB.

I left out the tables nothing uses yet: bb_pg, txpwr_lmt type0/5 and the dpk tables. The converter can add them. If the size matters, the RF tables (80% of the table data) could be packed to one u32 per entry (addr<<20 | data). That would mean leaving the shared conditional format.

## Source checks (scout notes)
- Confirmed: RF base 0x3c00/0x4c00, SIPI 0x1808/0x4108, and the mix write path (direct masked write for RF addr != 0, SIPI only for RF 0x00).
- Confirmed: DACK runs inside rf_init. It first tries to restore from backup, then runs the full ADC/DAC search.
- Confirmed: the BT handshake polls 0xa8[22:21] == 0, sends H2C 0x6d, then polls 0x49c bit0.
- Confirmed: IQK uses an H2C packet with sub-id 0x0E and clear=1, polls 0x2d9c == 0xaa, then writes 0x1b10 = 0.
- **Correction:** the note "rf_tbl[0] is rf_b" is misleading. Linux lists `{rf_b, rf_a}`, but each table carries its own path (`RTW_DECL_TABLE_RF_RADIO(rtw8822c_rf_a, A)`), so rf_b is written to **path B** and rf_a to **path A**. The order only means path B is loaded first.
  - My contract `rf_tbl[]` is indexed by path: `[0]=rf_a`, `[1]=rf_b`.
  - phy_init loads B, then A, as Linux does.
- Upstream quirk, kept as is: `dac_backup_reg` stores RF 0x8f of both paths into slot 0, so path B's value is restored on both paths. This is the `path*i+i` indexing in Linux.

## What each op does
- **power_on**:
  - Runs `rtw_mac_pre_system_cfg` (PCIe parts) and the power switch. If the card is already on, it switches off, then on again, as `rtw_mac_power_on` does.
  - Runs the card enable flow from the trans_* tables, PCIe and all-interface entries only. A failed poll is retried after toggling BIT_PFM_WOWL.
  - Runs the system config (sys_func_en 0xD8).
  - Applies the pci.c 8822C quirks: DBI write 0x725 = 0, HCI_MIX_CFG bit26 when cut >= D, and ANAPARSW_MAC_0 BIT_CF_L_V2 for RFE 5.
- **power_off**: the card disable flow.
- **efuse_parse**: rtw8822c_efuse layout (MAC at 0x120, rfe at 0xca, xtal at 0xb9, thermal at 0xd0/0xd1, power index table at 0x10, 42 bytes per path). It sets `g_rtw_rfe` and `g_rtw_pkg = 15`; phy_init sets `g_rtw_cut` from SYS_CFG1, so `rtw_apply_cond` gets the 8822C condition values.
- **mac_init**: `rtw8822c_mac_init`, line by line.
- **phy_init**: `phy_set_param`:
  - BB/RF power, DIS_DPD, header_file_init(pre), then tables: bb, agc, rfk-init (after the 0x1e24/0x1cd0 setup), rf_b to path B, rf_a to path A.
  - Crystal cap, header_file_init(post), trx mode 2T2R (from BIT_RF_TYPE_ID), adaptivity_init (EDCCA).
  - rf_init: DACK, x2 check, thermal trim, power trim and PA bias, reading the physical efuse through 0x30.
- **set_channel** (`m, ch, bw`):
  - `ch` is taken as the **primary** channel. Center channel and sub-channel index are worked out with the rtw88 rules (LOWEST/LOWER/UPPER/UPMOST).
  - 2.4 GHz 40 MHz uses ch <= 7 → secondary above. 80 MHz on 2.4 GHz falls back to 40.
  - Then set_channel_bb, rtw_set_channel_mac, set_channel_rf (LUT rxbb write and RF18 on both paths inside a 3-wire reset), toggle_igi.
  - `rtw8822c_set_channel_ex(m, center, bw, pri_idx)` is exported for callers that already know the center channel.
- **set_tx_power**: rtw88 power index from the efuse base and diffs (txgi_factor 2, clamped to 0x7f), then `set_tx_power_index`: CCK/OFDM reference registers and per-rate diffs at 0x3a00. Like the existing 8821C port, no power-by-rate or limit tables are used.
- **calibrate**:
  - rfk_power_save(off), then handshake start (H2C 0x6d), TXGAPK **stub**, handshake end.
  - IQK in firmware.
  - DPK **stub**: DPD_EN is cleared on S0/S1, so coefficients that were never calibrated are not used.
  - rfk_power_save(on).
  - Both stubs write a klog line. TXGAPK (~640 lines) and DPK (~1000 lines) were not ported.
- **coex_wifi_only**: `__rtw_coex_init_hw_config(wifi_only)` for 8822C:
  - Slot-extend H2C 0x69, LTE coex off, coex_cfg_init.
  - Priority masks 0x6cc, GNT_BT SW low and GNT_WL SW high through the LTE indirect registers, control owner to WL.
  - Scoreboard 0xaa = 0x8003, table case 1 (0x55555555), gnt_debug off.
  - TDMA and the BT-info query are skipped, as in the 8821C port.
- **rfe_setup**: 8822C has no driver antenna switch (`coex_set_ant_switch` is NULL), so it only reapplies the RFE-5 PCIe bit.

## Contract notes for Forge / lead
(I could not message Forge: no agent of that name was reachable.)
1. `txff_size` is u16 and `txff_pages` is u8, but the 8822C TX FIFO is 262144 bytes (2048 pages). The 8821C's 65536 does not fit either. I set both to 0 and exported `RTW8822C_TXFF_SIZE`. Please widen them to u32.
2. **RF access:** the contract's `rtw_rf_read/rtw_rf_write` use 8821C addressing. The chip file has its own `rtw8822c_rf_read/rtw8822c_rf_write`, built on rtw_r32/rtw_w32 and `rtw_bb_write_mask`. A per-chip rf op in `rtw_chip` would be cleaner.
3. **H2C box command:** 8822C needs 8-byte box commands (0x6d RFK status, 0x69 coex). I call the existing `rtw_h2c_cmd(m, w0, w1)` from rtw_sec.c through an extern. Keep that symbol or add it to the contract.
   - `rtw_h2c_pkt_send` must set the H2C sequence number (word1 bits 31:16), as `rtw_fw_send_h2c_packet` does. I send the 32-byte IQK packet with the sequence number left at 0.
4. **Tables:** phy_init loads the bb/agc/rfk/rf tables itself. The core must not apply `bb_tbl/agc_tbl/rf_tbl` again for this chip; those fields are only filled in for reference. The MAC table is empty upstream (`mac_tbl.n = 0`).
5. **Efuse:** the 8822C logical map is 768 bytes and the protected area is 124 bytes; the core uses 512/96. Everything this chip reads is below 0x200, so it works.
6. **Link order:** the chip file links only once Forge's core provides `rtw_bb_write_mask`, `rtw_bb_read_mask` and `rtw_h2c_pkt_send` (they are declared in rtw_chip.h but not defined on this branch). Do not build my branch alone; merge it after Forge.
7. **Shared globals:** the chip writes `g_rtw_cut/g_rtw_pkg/g_rtw_rfe/g_rtw_cond_set` from rtw_sec.c so that `rtw_apply_cond` uses the 8822C condition values.

## Testing
- `ccheck.sh` passes on both new .c files with no warnings.
- **Host test:** built with ASan and UBSan and run against a simulated card that records every register write and models the RF windows and SIPI ports. All checks pass:
  - **Chip table:** every field and op is set, the c82f entry is present, page table and RQPN are correct, and the real firmware blob is linked (size and signature checked).
  - **power_on:** runs through the enable flow; the cut-D HCI bit is set.
  - **efuse_parse and tx power index:** checked against hand-computed rtw88 values.
  - **Tables vs an independent oracle:** a Python oracle reads the Linux `rtw8822c_table.c` and applies rtw88's `rtw_parse_tbl_phy_cond` and `check_positive` rules. The test compares every write for bb, agc, rfk-init, rf_a on path A and rf_b on path B, for cut 3/5 and rfe 0/5/0x15. The conditions do change the output: 3 distinct RF write lists across those 6 cases.
  - **RF addressing:** direct writes go to 0x3c00/0x4c00, masked writes are read-modify-write, and RF 0 goes through SIPI 0x4108/0x1808.
  - **set_channel:** RF18 on both paths for ch 1/6/11/36/149 at 20 MHz, ch 36 at 40 MHz and ch 40 at 80 MHz. Also checked: RF18 is written directly (never through SIPI), RXBB LUT, 0xc30 SCO, the CCK-check bit, DATA_SC, and the 40/80 MHz sub-channel.
  - **tx power:** reference and diff registers.
  - **calibrate:** H2C 0x6d start and stop, IQK packet bytes, 0x1b10 cleared, DPD off.
  - **coex:** register values.
  - **Full mac_init + phy_init:** path B tables load before path A; crystal cap and trx mode are right.

## Untested / open
- Nothing has run on hardware. The DACK convergence and the hw_ready polls only ran against a simulator.
- Not done: TXGAPK, DPK, dpk_track, pwr_track/LCK, cfo_track, rx phy-status parsing, beamforming phy init (generic `rtw_bf_phy_init`), and power-by-rate/limit tables.
- The 2.4 GHz 40 MHz secondary-channel choice is a heuristic. The core should pass the real secondary channel through `rtw8822c_set_channel_ex`.
