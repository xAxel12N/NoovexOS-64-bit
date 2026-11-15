# s6 - MediaTek mt76 / Atheros ath9k, ath9k_htc, ath10k

## What we have
Nothing driver-level. Only vendor-name strings: 0x168c "Qualcomm Atheros" and 0x14c3 "MediaTek" in
shell/commands/processes_devices_wifi_bringup.c:192-193, and 0x1969 in drivers/net/ethernet/nic_names.c. No ID tables, initvals, EEPROM or firmware parsers.

## Licence check (SPDX lines, torvalds/linux master, fetched 2026-11-14)
- mt76 (mt7921/pci.c, mt76x2/usb.c, mt76_connac2_mac.h): `// SPDX-License-Identifier: BSD-3-Clause-Clear`. mt76 has been relicensed from ISC to Clear BSD. FSF lists Clear BSD as GPL-compatible, so we can use it with attribution. Clear BSD gives no patent grant.
- mt7601u/usb.c: `// SPDX-License-Identifier: GPL-2.0-only`. REJECTED as code; we can take facts (IDs) only.
- ath10k/pci.c: `// SPDX-License-Identifier: ISC`. OK.
- ath9k (ar9003_2p2_initvals.h, hif_usb.c): no SPDX line, but the header carries the ISC permission text ("Permission to use, copy, modify, and/or distribute this software for any purpose..."). OK.

## Sources (base https://github.com/torvalds/linux/tree/master/drivers/net/wireless/)
| File | Licence | Contents | Size | Use (1-5) |
|---|---|---|---|---|
| ath/ath9k/ar9003_2p2_initvals.h (plus ar9003_*/ar9300_*/ar9485/ar955x/ar956x initvals) | ISC | Register {addr,val} init tables per mode (2G/5G HT20/HT40): MAC, BB, radio, TX gain, RX gain, common | 1741 lines / 71 KB each | 4 |
| ath/ath9k/ar9003_eeprom.h + eeprom.h | ISC | AR9300 EEPROM struct (base header, modal 2G/5G, cal pier, target power, CTL), legacy AR5416 layouts | 374 + 733 lines | 4 |
| ath/ath9k/pci.c | ISC | 136 PCI_DEVICE entries for 0x168c (AR5416 to AR9565) | - | 3 |
| ath/ath9k/hif_usb.c (ath9k_htc) | ISC | 27 USB IDs (AR9271/AR7010); HTC firmware upload over USB (htc_9271.fw / htc_7010.fw, open-source firmware in open-ath9k-htc-firmware) | - | 3 |
| ath/ath10k/hw.h, core.h, pci.c | ISC | Firmware-N.bin container: magic "QCA-ATH10K" plus TLV `struct ath10k_fw_ie` (FW_IE_* ids), per-chip hw_params, Copy Engine register maps, board-data handling | 1205 / 1367 lines | 3 |
| mediatek/mt76/mt76_connac_mcu.h (and mt76_connac_mcu.c) | BSD-3-Clause-Clear | Firmware formats: `mt76_connac2_patch_hdr`, `mt76_connac2_fw_trailer`, `mt76_connac2_fw_region` (ROM patch plus RAM image download used by mt7921/mt7922), MCU command/event structs | 2157 lines / 52 KB | 4 |
| mediatek/mt76/mt7921/pci.c, usb.c | BSD-3-Clause-Clear | 7 PCI IDs (14c3:7961/7922/0608/0616...), 6 USB IDs (0e8d:7961...) | ~350 lines | 3 |
| mediatek/mt76/mt76x2/usb.c, eeprom.h (mt7612u) | BSD-3-Clause-Clear | 16 USB IDs; MT76x2 EEPROM field offsets enum | 83 lines eeprom.h | 3 |
| mediatek/mt7601u/* | GPL-2.0-only | USB IDs, initvals (facts only) | - | 1 |

Relevance: none of these chips is in the owner's hardware (RTL8821CE). Their value is as reference designs for an initvals, EEPROM or firmware loader, or as support for USB Wi-Fi dongles later. mt7921/7922 and ath10k also need proprietary firmware blobs from linux-firmware (redistributable, not GPL).

## Top 3
1. mt76_connac_mcu.h/.c (Clear BSD): a clean, modern firmware patch/RAM download format plus MCU protocol. Closest analogue to the rtw88 FW-download work.
2. ath9k ar9003 initvals plus ar9003_eeprom.h (ISC): a complete, permissive example of init tables and an EEPROM layout. Also, AR9271 (ath9k_htc) is the only option here with open firmware.
3. ath10k hw.h/core.h (ISC): a TLV firmware container ("QCA-ATH10K" plus ath10k_fw_ie) and hw_params tables.
