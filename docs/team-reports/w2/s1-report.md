# s1: Realtek Bluetooth (rtl_bt) beyond what we have

## What we have
drivers/bluetooth/rtlbt_fw.[ch] (168+41 lines). This is already a BSD-2-Clause port of FreeBSD rtlbtfw. It has:
- The project-ID table for epatch v2 (ids 0..47, mapping to 0x1200/8723/8821/8761/8703/8822/8851/8852/8922).
- The v1 and v2 epatch parsers, the signatures "Realtech" and "RTBTCore", and the extension signature 51 04 FD 77.
- The HCI opcodes FC20 (download), FC61 (read reg/sec proj), FC6D (ROM version) and 1001.
- **Only 2 IC table entries**: 8821C and 8822C. Also embedded blobs (8821c fw/cfg, 8822cu fw, 8761bu cfg).
- No USB VID:PID list for non-0x0bda vendors. 0BDA:C123 is matched by vendor.

## Sources
1. FreeBSD rtlbtfw: https://github.com/freebsd/freebsd-src/tree/main/usr.sbin/bluetooth/rtlbtfw
   - Licence: `SPDX-License-Identifier: BSD-2-Clause` (all 3 files).
   - rtlbt_fw.c (575 lines) has the full `rtlbt_ic_id_table`: 16 entries keyed on (lmp_subver, hci_rev, hci_ver) with flags SIMPLE/CONFIG/MSFT and the fw names. Entries:
     - 8723a: 1200/0xb/6, SIMPLE
     - 8723b: 8723/0xb/6
     - 8723d: 8723/0xd/8, CONFIG
     - 8821a: 8821/0xa/6
     - 8821c: 8821/0xc/8, MSFT
     - 8761a: 8761/0xa/6
     - 8761bu: 8761/0xb/0xa
     - 8822cu: 8822/0xc/0xa, MSFT
     - 8822b: 8822/0xb/7, CONFIG|MSFT
     - 8852au: 8852/0xa/0xb
     - 8852bu: 8852/0xb/0xb
     - 8852cu: 8852/0xc/0xc
     - 8851bu: 8851/0xb/0xc
     - 8922au: 8922/0xa/0xc
     - 8852btu: 8852/0x87/0xc
   - rtlbt_fw.h (140 lines) has the packed structs:
     - fw_header_v1: sig[8], u32 fw_version, u16 num_patches
     - fw_header_v2: sig[8], u8 ver[8], u32 num_sections
     - section: u32 opcode, u32 len
     - sec_hdr: u16 num, u16 rsvd
     - subsec_hdr: eco, prio, cb[2], u32 len
     - subsec_security_hdr: eco, prio, key_id, rsvd, u32 len
     - Patch opcodes: 01 snippets, 02 dummy header, 03 security header.
   - main.c (543 lines) has `rtlbt_list`: 58 non-Realtek VID:PID pairs (Foxconn 0489, IMC 13d3, Azurewave, Asus 0b05, Lite-On 04ca, TP-Link 2357, etc.) for 8821CE/8822CE/8852AE/BE/CE/8851BE. It also has the rtlbt_append_fwfile (config append) logic.
   - Usefulness: **5**. It directly extends our existing port with the same licence and naming.
2. Linux btrtl.c / btusb.c: torvalds/linux drivers/bluetooth. GPL-2.0-only, so it is facts only.
   - It has the most complete ic_id_table. It adds 8723CS/8723DS/8822CS/8852BS (UART) and 8852BT, with has_rom_version/has_msft_ext/config_needed flags.
   - It has btusb quirk IDs, which are a superset of the FreeBSD list.
   - It has the v2 "RTBTCore" section opcodes 0x00 rsvd/patch, 0x01..0x03 and 0x0B..0x0D? (unverified; not fetched, given the time limit).
   - Usefulness: 3 (facts only; FreeBSD already mirrors most of it).
3. Zephyr drivers/bluetooth/hci (Apache-2.0): no Realtek epatch loader found (h4_realtek.c does not exist; 404). Usefulness: 1.
4. OpenBSD/NetBSD ubt: generic HCI USB only, with no Realtek firmware loader that I know of (not verified in the time limit). Usefulness: 1.

## Top 3
1. FreeBSD rtlbt_fw.c `rtlbt_ic_id_table`: add the 14 missing IC rows (8723b/d, 8761a/bu, 8822b, 8852a/b/c/bt, 8851b, 8922a) to our rtlbt_ic_t table.
2. FreeBSD main.c `rtlbt_list`: 58 third-party VID:PID pairs for matching OEM-branded Realtek BT dongles and combo cards.
3. FreeBSD rtlbt_fw.c `rtlbt_append_fwfile` plus the IC_FLAG_CONFIG/MSFT semantics, which select per-IC config-required and MSFT-ext. Our table has no flags field.
