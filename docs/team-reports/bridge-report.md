# Bridge report: RTL8822BE (10ec:b822) chip port

Branch `bridge` (worktree /home/claude/wt/bridge), commits 8e919747 and 6c0459c7, on top of 0aef5934.
Source: Linux rtw88 rtw8822b.c, rtw8822be.c and rtw8822b_table.c, plus the parts of mac.c, phy.c, coex.c, bf.c and fw.c the chip depends on. All of these are GPL-2.0 OR BSD-3-Clause, and the SPDX line is kept in each ported file. I never had hardware for this chip, so the port follows rtw88 line by line.

## Files
- `drivers/net/wifi/realtek/rtw88/chips/rtw8822b.c`: defines `const rtw_chip rtw8822b_chip`. Settings: name "RTL8822BE", 10ec:b822, PCIe, 2 paths, WCPU 3081, txff 262144 (2048 pages), rxff 24576, 8 reserved driver pages, descriptors 48/24/16. Page table {64,64,64,64,1} and rqpn {vo 2, vi 2, be 1, bk 1, mg 0, hi 3} are the rtw88 PCIe entries (index 1). Tables mac/bb/agc and rf_a/rf_b; no agc_btg. Firmware comes from `_binary_rtw8822b_fw_bin_*`. rf_base {0x2800,0x2C00}, rf_sipi {0xC90,0xE90}.
  - `power_on` / `power_off`: card_enable_flow / card_disable_flow, using the full rtw88 command tables with interface and cut masks. Its own parser (`pwr_run`) follows `rtw_sub_pwr_seq_parser`: it polls for up to 20000 x 50 us, and on PCIe it toggles BIT_PFM_WOWL and retries. It also does the 3081 RPWM toggle, and if the chip is already on it cycles it off and on, as `rtw_mac_power_on` does.
  - `efuse_parse`: rtw8822b_efuse layout. MAC at 0xD0 (PCIe), rfe 0xCA (full byte), xtal 0xB9 (0xFF becomes 0), thermal 0xBA, country 0xCB/0xCC, tx power at 0x10 + 42*path. It returns -2 if rfe is not 2, 3 or 5 (the same check as rtw_check_supported_rfe).
  - `mac_init`: rtw8822b_mac_init.
  - `phy_init`: the start of rtw8822b_phy_set_param, run before the tables (BB/RF enable, RXPSEL clear). `phy_post`: everything after the tables (crystal cap, RXPSEL set, config_trx_mode with the RF mode table, adaptivity_init/EDCCA, phy_rfe_init, bf_phy_init, 0x1C94).
  - `set_channel(ch,bw)`: set_channel_bb, rtw_set_channel_mac, set_channel_rf (RF18 on paths A and B, RF 0xBE band, 0xDF[18] for channel 144, XTALX2 toggle), rxdfir, toggle_igi, CCA (the cut/rfe tables), RFE switch, then set_tx_power. **`ch` is taken as the primary channel.** For 40/80 MHz the chip works out the center channel and RTW_SC index as rtw_update_channel does: 2.4 GHz HT40+ for ch <= 7, otherwise HT40-; 80 MHz on 2.4 GHz is lowered to 40.
  - `set_tx_power`: the rtw88 base index per rate (rtw_phy_get_2g/5g_tx_power_index, including the bw80 average of two bw40 groups). Rate sections CCK, OFDM, HT1S, HT2S, VHT1S and VHT2S go to 0x1D00/0x1D80, capped at 0x3F; CCK is written only on 2.4 GHz. The by-rate (bb_pg) and limit tables are not applied, the same as the existing 8821C port.
  - `calibrate`: rtw8822b_do_iqk. Sends H2C_PKT_IQK (0x0E, clear 0, segment 0, total length 9) through `rtw_h2c_pkt_send`, polls RF-A 0x08 for 0xABCDE (300 x 20 ms), clears it, and reads 0x1BF0. Results go to `rtw8822b_iqk_rf08` / `_fail` / `_reload` (globals, for debugging).
  - `coex_wifi_only`: __rtw_coex_init_hw_config(wifi_only) with the 8822B ops (rfe_type and scoreboard, cfg_init, wl_pri_mask, ant_path WONLY: GNT via LTE indirect, control owner, BBSW ant switch to WLG, coex table 1 only when share_ant). The LTE indirect access is done locally in the chip file using rtw_r32/rtw_w32 (0x1700/0x1704/0x1708).
  - `rfe_setup(band5)`: rtw8822b_set_channel_rfe_efem (rfe 2) or _ifem (rfe 3, 5).
- `drivers/net/wifi/realtek/rtw88/chips/rtw8822b_tables.c/.h`: the raw conditional {addr,data} pair arrays (same format as rtw_cmac/rtw_cbb...): `rtw8822b_cmac` 125 pairs, `cbb` 1492, `cagc` 10151, `crf_a` 5089, `crf_b` 4452, each with an `_n` pair count.
- `tools/wifi-tables/rtw_condgen.py`: the converter (`rtw_condgen.py <chip>_table.c <prefix> out.c out.h`). It checks that IF/ENDIF are balanced and works for other rtw88 chips too.
- `assets/firmware/rtw8822b_fw.bin`: from linux-firmware (git.kernel.org). 161240 bytes, sha256 a72da690597bfa99d8eb6fc2ab090d18d8ad92ac2befd35db1c9e3662d8d8418, license LICENCE.rtlwifi_firmware.txt (the same terms as rtw8821c_fw.bin).
- `build.sh`: `ld -r -b binary` makes `$B/rtw8822b_fw.o`, and it is added to BLOBS right after rtw_fw.o. I checked that ld exports `_binary_rtw8822b_fw_bin_start/_end`.
- Tests: `tools/tests/wifi/rtw8822b_main.c`, `rtw8822b_gen.py`, `rtw8822b_test.sh`, plus stubs in `tools/tests/wifi/rtw8822b_stub/`.

## Contract header changed (please read)
`include/nvx/drivers/net/wifi/rtw_chip.h` in my branch is a **copy of Forge's uncommitted version** from wt/forge (as of about 01:40 UTC). The committed header could not hold 8822B: `txff_size` was u16 and `txff_pages` u8, while 8822B needs 262144 and 2048. The copy also has phy_post, rf_base/rf_sipi/rf_mix and the documented bring-up order. The chip uses designated initializers, so changes to field order are harmless. If Forge commits a different header, take Forge's version; my file only needs the fields named above. I could not reach Forge by message.

## Things the core (Forge) must do for 8822B
1. **RF path B tables**: `rf_tbl[1]` (sz 5) has to be written to path B. Today `rtw_apply_cond` with sz 5 goes to `rtw_rf_write_a` (SIPI 0xC90), which is path A only. Use `rtw_rf_write(m, path, addr, 0xFFFFF, data)`, with RF delays 0xFFE = 50 ms and 0xFE = 100 us.
2. **Condition variables**: `rtw_cond_setup` takes pkg from logmap[0xCA] bit 5 and masks rfe with 0x1F. That is 8821C-only. For 8822B pkg must be 15 (rtw88 never sets pkg_type) and rfe should be `g_rtw_efuse.rfe_option`. Cut handling (raw cut_version, 0 becomes 15) already matches rtw88, and it matters: the RF tables have cut==3 (CUT_D) branches.
3. **sys_func_en**: 8822B uses 0xDC, but the core's `rtw_sys_cfg` writes 0xD8 (8821C). The contract has no field for it. Pre/post system cfg (rtw_mac_pre_system_cfg, __rtw_mac_init_system_cfg) are generic and stay in the core.
4. **fw_rxff_size**: 12288 for 8822B. The contract has no field; it is used for the RX FIFO boundary after firmware download.
5. `rtw_h2c_pkt_send` should fill in the H2C sequence number (FW_OFFLOAD_H2C_SET_SEQ_NUM, word 1 bits 31:16), as rtw_fw_send_h2c_packet does. The chip leaves those bits at 0.
6. I assume `rtw_rf_write(path, addr, mask, val)` and `rtw_rf_read(..., mask)` shift `val` into the mask, like rtw88 rtw_write_rf / rtw_read_rf.
7. The chip keeps private state between calls: cut/paths (filled in by power_on/phy_init), the raw 42-byte per-path tx power block and share_ant (filled in by efuse_parse), and the current channel/bw. Reason: rtw_efuse_info cannot hold the 5G diffs. If you want that state in the contract, add `u8 txpwr_raw[2][42]` and `share_ant` to rtw_efuse_info.
8. Registry: Forge's `rtw_chip_find`/`rtw_chip_select` needs `extern const rtw_chip rtw8822b_chip;`. **I did not add the chip files to units.list**, so my branch still builds without Forge's core. When the core with rtw_rf_read/rtw_rf_write/rtw_h2c_pkt_send/g_rtw_efuse is merged, add:
   `drivers/net/wifi/realtek/rtw88/chips/rtw8822b.c` and `drivers/net/wifi/realtek/rtw88/chips/rtw8822b_tables.c` (about 230 KB of rodata).
   Without the core they fail to link because the contract symbols are undefined.

## Not ported (left out on purpose)
- PCIe PHY parameters (pcie_gen1/gen2_param_8822b, intf_table). The core does the 8821C MDIO setup; 8822B has its own table in rtw8822b.c lines 1995-2088 (cut-masked). This is a likely follow-up if the link behaves badly.
- Runtime DM: pwr_track, false_alarm_statistics, adaptivity (dynamic), query_phy_status, set_antenna, cfg_ldo25, LED, beamformee, and the coex runtime / H2C coex commands (tdma, bt_info, wl_slot_extend; the 8821C port skips these too).
- bb_pg (by-rate) and txpwr_lmt tables.

## Testing
- `/home/claude/ccheck.sh` on rtw8822b.c and rtw8822b_tables.c: no warnings.
- `tools/tests/wifi/rtw8822b_test.sh` (gcc with ASan/UBSan) passes all checks. It covers:
  - Chip table complete: no null ops, all fields and table pointers set.
  - Tables: for all 5 tables x cut {2,3,15} x rfe {2,3,5}, the converted arrays walked through the chip table pointers give the same write count and checksum as an independent Python walk of the Linux source (45 combinations). RF-B writes land on path B.
  - Power sequence: the PCIe and cut-C entries are applied, the USB-only entries are not, cut D skips the cut-C writes, and an already-on chip is cycled off and on.
  - set_channel at bw20 for ch 1/6/11/36/149: RF18 on A and B = 0x8FC01/0x8FC06/0x8FC0B/0x9FD24/0xDFD95, starting from RF18 = 0xFFFFF. Also 36@40, 36@80, 157@80, 1@40, 11@40. The test checks RF 0xBE band, 0x483 subchannel, RFMOD, 0x8AC, CCK check, RFE select and TRSW per band, ch144 0xDF[18], the rfe-2 eFEM CCA values, and that an unsupported rfe programs nothing.
  - efuse_parse on a synthetic map, including the rfe-not-supported, xtal 0xFF and short-map cases.
  - TX power index words for 2.4 GHz, 5 GHz and 5 GHz/80 MHz on both paths.
  - phy_init/mac_init/phy_post register values and the RF mode table; IQK H2C bytes plus the poll and clear; coex scoreboard (0x8003, or 0x8103 for eFEM rfe 2), ant switch, coex table and priority bits; rfe_setup per band.
- Not tested: anything on real hardware. The expected values in the test are hand-derived from rtw88, so a misreading of rtw88 that appears in both the code and the test would not be caught. The table check does not have this weakness because its reference is computed from the Linux source.
