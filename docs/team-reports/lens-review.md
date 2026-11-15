# Lens review: Wi-Fi multi-chip changes (0aef5934..main)

Scope: drivers/net/wifi, net/wifi, shell, include/nvx/drivers/net/wifi, build.sh, units.list (and the one-line rtw_find_mmio change in graphics/ui/desktop/app_tiles_and_icons.c). Compared with Linux rtw88 at /tmp/ref/linux. Read-only; nothing in the repo was modified. All changed rtw88 files pass `gcc -fsyntax-only -Wall -Wextra` with the build.sh flags and print no new warnings.

## Verdict for the owner's RTL8821CE (priority 1)

I found no change that would alter scan, WPA2 join or DHCP on the 8821CE. I checked each 8821C path against the v10.98 code:

- FIFO layout: `rtw_rsvd_boundary()` = 512 - (8+24+8+8+0+4+0) = 460, which equals the old hard-coded 460. `h2cq_addr_pg` = 512-0-4-8 = 500 (old: 500). rxff 16384 is unchanged. pqmap comes from rqpn {2,2,1,1,0,3} and gives the same bits as before. The page table {16,16,16,14,1} is in the same field order (hq,nq,lq,exq,gapq). The bcnq stage page head is still 460 (rtw8821ce.c:57).
- `sys_func_en`=0 falls back to the old 0xD8 (rtw8821ce.c:86).
- The power-on table in chips/rtw8821c.c:13-22 matches the old inline `seqp[]` entry for entry.
- `rtw8821c_phy_init`, `rtw8821c_mac_init` and `rtw8821c_phy_post` match the removed `rtw_phy_power_on`, `rtw_wmac_init` and `rtw_phy_post` byte for byte. The allbuilt order is still phy-on, efuse, cond, wmac, tables, MAC, post.
- RF access: `rtw_rf_read/rtw_rf_write` (rtw_efuse.c:141-152) use the same 0x2800 read and 0xC90 SIPI write, the same mask/shift read-modify-write and the same 13 us delay. `g_rtw_rf_path` is reset to 0 after the table loop (bringup.c:513).
- Channel set, antenna switch, TX power and coex were moved unchanged into chips/rtw8821c.c. `rtw_rfe()` now reads `g_rtw_efuse.rfe_option`, which is filled from logmap[0xCA] by `rtw_efuse_apply()` on both paths that set `rtw_efuse_valid` (bringup.c:221, rtw_efuse.c:162).
- IQK is off by default (`g_rtw_iqk_en=0`, chip.c:64). `rtw_chip_calibrate` returns early (chip.c:117), so wifi_up, rtw_assoc and rtw_wpa2_join send nothing extra.
- tools/tests/wifi/chip_main.c compares the old and new 8821C functions register by register. That is a good guard.

## Ranked findings

### 1. MEDIUM: the PCIe PHY (MDIO) value for the 8821C is written to every chip
- **Where:** net/wifi/wifi_mgr.c:112, `g_wpci_mdio=m?rtw_mdio_write(m,0x09,0x6380,1):-1;`. This line is not new, but 8822B/8822C cards now reach it because `rtw_find_mmio` accepts them.
- **What is wrong:** 0x09=0x6380 is `pcie_gen1_param_8821c` (Linux rtw8821c.c:1578). Linux writes no PHY parameters for the 8822C (`pcie_gen1/gen2_param_8822c` are empty, rtw8822c.c:4884-4896). For the 8822B it writes a cut-C-only set in which reg 0x09 is 0x321C, plus regs 0x01/0x02/0x08/0x0A/0x20/0x21/0x26/0x29/0x2A (rtw8822b.c:1995+).
- **Failure scenario:** on an 8822BE or 8822CE, the first `wifi on` writes a wrong PCIe PHY parameter. The likely results are an unstable link, PCIe errors, or a card that stops answering mid-session ("card not responding") on some hosts.
- **Fix:** add a per-chip PCIe PHY table to rtw_chip (offset, value, cut mask, gen), or at minimum run this write only when `g_rtw_chip==rtw_chip_at(0)`.

### 2. MEDIUM: IQK on 8822B/8822C always reports "done", never turns itself off, and can freeze the UI for about 6 s per call
- **Where:**
  - chips/rtw8822b.c:512 ignores the result of `rtw_h2c_pkt_send`.
  - chips/rtw8822b.c:514 polls 300 x 20 ms = 6 s with no `ui_pump()`.
  - chips/rtw8822c.c:819 (`rtw8822c_calibrate`) also never sets `g_rtw_iqk_last`.
  - chip.c:121-123 then sets `g_rtw_iqk_last=1` ("done").
- **What is wrong:** the core's failure handling (`g_rtw_iqk_dead`, the -1/-2 states and the fail mask) only works for 8821C. On 8822B/C a failed or rejected IQK looks like success, so it is never disabled.
- **Failure scenario:** a user runs `wifi iqk on` on an 8822BE and the firmware does not process the IQK H2C. Every wifi_up and every join then blocks for 6 s, and the 8822C RFK handshake adds up to 0.8 s more. `wifi diag` shows "calibration (IQK) done, fail mask 0x00".
- **Fix:** do what rtw8821c_calibrate does:
  - set `g_rtw_iqk_last=-1` and return when the send does not return 0;
  - set it to `-2` on poll timeout;
  - on success set `1|(fail_mask<<8)`;
  - call `ui_pump()` every few iterations;
  - in 8822C, make `rtw8822c_do_iqk`'s return value feed `g_rtw_iqk_last`.

### 3. MEDIUM: the core efuse geometry is fixed to 8821C values
- **Where:** include/nvx/drivers/net/wifi/rtw_efuse.h:6-8 (PHY 512, LOG 512, PROT 96), used by rtw_efuse.c:158-163 and bringup.c:219-220.
- **What is wrong:** Linux uses phy 1024 / log 768 / protect 96 for the 8822B (rtw8822b.c:2532-2534) and phy 512 / log 768 / protect 124 for the 8822C (rtw8822c.c:5348-5350).
  - 8822B: only the first half of the physical efuse is read. Efuse packets are appended over time, so a field rewritten in production (power calibration, MAC) after byte 512 is silently lost.
  - 8822C: the decoder walks 28 bytes into the protected area.
  - The fields that are parsed today (0x10-0x125) fit inside 512 logical bytes, so most cards will still work.
- **Failure scenario:** an 8822BE whose TX-power or crystal-cap packets sit after physical byte 512 gets stale or 0xFF values. The result is a wrong crystal cap (frequency offset, so no association) or wrong TX power.
- **Fix:** add phy/log/protect sizes to rtw_chip and size `rtw_phymap`/`rtw_logmap` for the largest chip (1024/768).

### 4. LOW (8821CE behaviour change, register-level only): the H2C ring is now always set up
- **Where:** shell/commands/processes_devices_wifi_bringup.c:617 calls `rtw_h2c_ring_setup()`, which writes 0x1328=32 and 0x1320=ring and sets bits 16 and 8 of 0x1330 (chip.c:67-79). This happens on every 8821CE start, even with IQK off.
- **What is wrong:** nothing, strictly. It matches Linux `rtw_pci_reset_buf_desc` (pci.c:410-416, 471-473). It is, however, the only new register write on the owner's working start-up path, and it runs after the firmware is already running.
- **Failure scenario:** none expected. If the owner sees any change after v10.99, this is the first thing to rule out.
- **Fix (optional):** gate the call on `g_rtw_iqk_en`, or run the setup lazily from `rtw_chip_calibrate`.

### 5. LOW: the PHYDM info H2C assumes 2T2R from the static path count
- **Where:** chip.c:111 sets `p[9]=n_paths>1?2:4` and derives `ant` from n_paths.
- **What is wrong:** Linux uses `hal->rf_type` and `hal->antenna_tx/rx`, read from SYS_CFG1 BIT(27) (fw.c:552-565). The 8822B/C chip files detect this (`hw_info`, rtw8822c power_on), but the core ignores it.
- **Failure scenario:** a 1T1R variant of the 8822B or 8822C tells the firmware FW_RF_2T2R with antenna mask 0x33. The firmware's rate adaptation and IQK then target a missing path.
- **Fix:** let the chip report its detected path count, for example through a field filled at phy_init.

### 6. LOW: 8822C RF tables load path B before path A
- **Where:** chips/rtw8822c.c:598-599. Linux `rtw_phy_load_tables` loads rf_tbl[0] (A) then rf_tbl[1] (B).
- **Failure scenario:** probably none, but it differs from the reference for no reason.
- **Fix:** swap the two lines.

### 7. INFO / cosmetic
- chips/rtw8822b.c:579: `.fw_name = "rtw88/rtw8822b_fw.bin"` does not match the asset name `rtw8822b_fw.bin` (8821c and 8822c use bare names). Only the banner and diag text are affected.
- chip.c:18: the 8821C aliases 0xC82A/0xC82B are not in Linux rtw8821ce's PCI ID table, which has only B821/C821. They were carried over from the old hard-coded check, so this is not a regression.

## Checked and found OK (priorities 2-4)

- **H2C ring memory safety** (chip.c:67-100):
  - Slot index is the HW host index & 0xFFF % 32, giving a buffer offset of at most 31*128+80 < 4096 (1 page) and a BD offset of at most 31*16+16 = 512 bytes (1 page).
  - `len` is limited to 8..32.
  - The BD layout matches `struct rtw_pci_tx_buffer_desc` (buf_size@0, psb_len@2, dma@4).
  - psb_len = (80-1)/128+1 = 1, as Linux computes for skb->len 48+32.
  - The TX descriptor (size 32, offset 0, QSEL 19) matches `rtw_tx_write_data_h2c_get`/`rtw_pci_tx_write_data`.
  - The full check (nx==rp) is correct.
  - DMA addresses are truncated to 32 bits, the same as every other ring here.
- **H2C packet contents:**
  - GENERAL_INFO: len 12, TX boundary in byte 10 (dword2 bits 23:16).
  - PHYDM_INFO: len 16, bytes 8-11.
  - IQK: len 9, clear/segment bits in byte 8.
  - Sequence number in bytes 6-7.
  All match fw.c:520-581 and fw.h:400-435. The 8821C IQK uses segment=1 when linked, as Linux does. The 8822C uses clear=1, poll 0x2D9C==0xAA for 300 ms and then clears 0x1B10, as Linux does.
- **FIFO math for the new chips:**
  - 8822B: boundary 1996, h2cq page 2036.
  - 8822C: boundary 1938, fw_txbuf 1994 (general-info value 56), h2cq page 1986.
  All equal Linux `rtw_set_trx_fifo_info`. h2cq_addr fits the 18-bit field in 0x244/0x248/0x24C. rxff, sys_func_en (8822B 0xDC, 8822C 0xD8), page_table[1] and rqpn_table[1] match.
- **Weak externs in the registry** (chip.c:11-21): every table walk null-checks entries (`rtw_chip_count`, `rtw_chip_at`, `rtw_chip_find` both loops). All three chip units are in units.list, so the weak references resolve to the real objects. Ops the core calls without a check (`set_channel`, `power_on`, `mac_init`, `phy_init`, `efuse_parse` via apply) are set on every chip. The optional ones (`phy_post`, `calibrate`, `coex_wifi_only`, `mac_tbl` for 8822C) are checked before use.
- **Table bounds:** every `*_n` equals array size / 2, `rtw_apply_cond` reads `t[i*2+1]` for i<n, and the chip structs use sizeof/8 consistently. The 8822B band lookups (low/middle/high) stay inside 15/23/15 entries for the channel ranges that reach them.
- **8822B against rtw88:**
  - The power sequences, cut mask BIT(cut+1) and the PFM toggle on poll failure match.
  - CCA tables, the RFE 2/3/5 mapping and the 0x79a0ea28 override conditions match.
  - The band-edge tables, RF18 bits, BB channel registers, phy_set_param order, rfe_init, config_trx_mode (including the RF 0xEF/0x33 LUT loop) and the TX AGC bases 0x1d00/0x1d80 match.
  - IQK parameters (clear=0, poll RF 0x08 == 0xabcde) match.
- **8822C against rtw88:**
  - rf_base/rf_sipi and the mixed RF write (RF 0x00 via SIPI) match.
  - The efuse offsets (MAC 0x120, RFE 0xCA, xtal 0xB9 & 0x7F, thermal 0xD0/0xD1) match.
  - The RF18 band/BW bits (BW in bits 13:12), the LUT RXBB writes, the BB channel constants (0xC30, 0x1A20-0x1AB0, 0x18AC/0x41AC AGC) and the phy_set_param order match.
- **Build:** the firmware blobs are present and tracked in git (assets/firmware/rtw8822b_fw.bin 161240 B, rtw8822c_fw.bin 202600 B). The `ld -r -b binary` symbol names `_binary_rtw8822[bc]_fw_bin_start/end` match the externs in the chip files, and both objects are in BLOBS. `rtw_h2c_cmd` used by 8822C is defined at rtw_sec.c:285. There are no duplicate global symbols between the chip files, since helpers are static and exports are prefixed.
