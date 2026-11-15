# Scout sc1: Realtek Wi-Fi beyond what we have (rtw88 USB/SDIO, rtw89, firmware)

Sources checked locally: Linux 7.4-rc3 merge `7e1392a` (/tmp/ref/linux, rtw88 + rtw89 trees) and
linux-firmware `1399ab8` (2026-11-13, /tmp/ref/linux-firmware, WHENCE + LICENSES/).

## 1. What NoovexOS already has (do not re-propose)
- drivers/net/wifi/rtw88/: hand port, PCIe only. Chips: RTL8821CE (works on owner's PC), RTL8822BE, RTL8822CE
  (chips/rtw8821c.c, rtw8822b.c, rtw8822c.c + generated tables). Firmware embedded: assets/firmware/rtw8821c_fw.bin.
- Bus layer: `rtw_bus` in include/nvx/drivers/net/wifi/rtw_chip.h has only r8/r16/r32/w8/w16/w32 + `kind`
  (`RTW_BUS_USB` enum exists). `g_rtw_bus=&rtw_bus_pcie` (rtw_efuse.c:19). No USB bus ops implemented;
  `rtw_h2c_ring_setup` returns -1 on non-PCIe (chip.c:70). No TX/RX or firmware-download hook in the bus struct.
- USB host: xHCI with per-class static helpers (`xh_ctrl` controller_init.c:105, `xh_bulk` hid_keyboard_mouse.c:174,
  usb_network.c). No generic USB class-driver API (vendor ctrl + multi-URB bulk IN) yet.
- Already on docs/PORTING-WORKLIST.md: #1 rtw88 USB (8821CU/8822BU/8822CU), rtw89 listed by atlas as XL.
  docs/team-reports/atlas.md section 3 already contains the full usb.c builder facts (vendor request 0xC0/0x40
  bRequest 0x05, 4x32 KB bulk-IN, 0x4e0 quirk, ID lists). This report extends that, it does not replace it.

## 2. License proof (all verified by file header + MODULE_LICENSE)
| Files | SPDX line 1 | MODULE_LICENSE | Verdict |
|---|---|---|---|
| rtw88/usb.c (1406), usb.h (103) | GPL-2.0 OR BSD-3-Clause | "Dual BSD/GPL" (usb.c) | OK under BSD-3 |
| rtw88/sdio.c (1896) | GPL-2.0 OR BSD-3-Clause | "Dual BSD/GPL" | OK |
| rtw88/rtw8821cu.c 62, rtw8822bu.c 104, rtw8822cu.c 46, rtw8723du.c 36 | GPL-2.0 OR BSD-3-Clause | "Dual BSD/GPL" | OK |
| rtw88/rtw8723d.c 2212, rtw8723x.c 789 | GPL-2.0 OR BSD-3-Clause | "Dual BSD/GPL" | OK |
| rtw88/rtw8821au.c 78, rtw8812au.c 94, rtw88xxa.c 1952, rtw8821a.c 1231, rtw8812a.c 1125 | GPL-2.0 OR BSD-3-Clause | "Dual BSD/GPL" | OK |
| rtw89/* (all 105 .c/.h) | every file GPL-2.0 OR BSD-3-Clause | 23x "Dual BSD/GPL" | OK |

Answer to the specific question: yes, rtw88 usb.c is `GPL-2.0 OR BSD-3-Clause` + `MODULE_LICENSE("Dual BSD/GPL")`.
rtw89 is the same, every file, no exceptions found (`head -1 | sort | uniq -c` gave 105/105).

## 3. Firmware (linux-firmware WHENCE lines 3601 rtw88, 3631 rtw89)
Both entries: `Licence: Redistributable. See LICENCE.rtlwifi_firmware.txt` /
`SPDX-License-Identifier: LicenseRef-LICENCE.rtlwifi_firmware.txt`. Terms (LICENSES/LICENCE.rtlwifi_firmware.txt,
Realtek 2010): binary redistribution allowed without modification; must reproduce the copyright notice + disclaimer
in docs; no endorsement; no reverse engineering of the blob. Patent grant covers use with an OSI-approved-licensed OS
(GPLv3 qualifies). Action: ship the licence text next to the blobs (e.g. assets/firmware/LICENCE.rtlwifi_firmware.txt)
and mention it in the about/credits doc. Never patch the blob.

| Blob | Bytes | Used by |
|---|---|---|
| rtw88/rtw8821c_fw.bin | 139472 | 8821CE (have) + 8821CU/8811CU (same file) |
| rtw88/rtw8822b_fw.bin | 161240 | 8822BE (have) + 8822BU |
| rtw88/rtw8822c_fw.bin (+ _wow 145224) | 202600 | 8822CE (have) + 8822CU |
| rtw88/rtw8723d_fw.bin | 28884 | 8723DE / 8723DU / 8723DS |
| rtw88/rtw8821a_fw.bin / rtw8812a_fw.bin | 31898 / 27030 | 8811AU/8821AU / 8812AU |
| rtw89/rtw8852b_fw-2.bin (newest) | 1594394 | 8852BE / 8852BU |
| rtw89/rtw8851b_fw-1.bin | 1408706 | 8851BE / 8851BU |
| rtw89/rtw8852c_fw-2.bin | 2719574 | 8852CE |
| rtw89/rtw8922a_fw-4.bin | 1849226 | 8922AE |
| rtw89/rtw8852a_fw-1.bin | 1958648 | 8852AE |
Note: rtw88 blobs for USB chips are the same files as PCIe; rtw89 blobs are 1-5 MB each (ISO size matters).

## 4. Candidates, ranked

### #1 rtw88 USB HCI + RTL8821CU / RTL8811CU (and the bus refactor)  - ACCEPT
- Source: rtw88/usb.c + usb.h + rtw8821cu.c (1571 lines). Chip code = existing chips/rtw8821c.c.
- License: GPL-2.0 OR BSD-3-Clause, Dual BSD/GPL. Firmware: rtw8821c_fw.bin, already embedded.
- 8811CU is the 1T1R 8821C variant; covered by the 8821CU ID table (0bda:c811, 0bda:8811, 7392:c811 etc., 15 IDs).
- Needed work: (a) generic USB vendor-device API in xHCI: claim interface ff/ff/ff, EP0 vendor ctrl 1/2/4 + burst
  254 B, 1 bulk IN with >=1 (ideally 4) outstanding 32 KB transfers, 1-4 bulk OUT; (b) extend `rtw_bus` with
  tx(qsel, buf, len), rx poll, fw/rsvd-page write and h2c write (USB has no IDDMA and no H2C ring; both go as bulk-OUT
  packets with TX descriptor + checksum, our `rtw_desc_csum` already exists); (c) USB power-on deltas: re-add the
  USB-only power steps that v10.x removed for PCIe (00-whatsnew #5) behind `kind==RTW_BUS_USB`, clear
  REG_SYS_STATUS1+1 b0 after power seq (mac.c:315), 0x4e0 write after always-on reg reads; (d) RX aggregation
  parsing (8-byte aligned multi-frame buffers), rqpn by out-pipe count; (e) efuse MAC offset for USB in 8821C.
- Effort: 10-14 agent-hours (USB API ~4, bus refactor ~3, usb.c port ~4, bring-up/debug ~3). Needs real dongle to
  verify; QEMU can't emulate it (usb-host passthrough only).
- Value: HIGH. Gives Wi-Fi to any desktop without a card; owner's 8821CE work is reused 1:1. Cheapest AC600 sticks.

### #2 RTL8822BU + RTL8822CU (on top of #1) - ACCEPT
- rtw8822bu.c 104 (36 IDs incl. TP-Link Archer T3U/T4U 2357:*, ASUS 0b05:*, Edimax 7392:*), rtw8822cu.c 46 (7 IDs).
- Chip code exists (chips/rtw8822b.c, rtw8822c.c). Extra: USB2->USB3 mode switch (REG_PAD_CTRL2 + device
  re-enumerate via usb_reset_device) - optional, skip first (run at USB2 speed).
- Firmware: rtw8822b_fw.bin / rtw8822c_fw.bin (already used by our 8822BE/CE path).
- Effort: 3-5 h after #1. Value: HIGH (AC1200/AC1300 2x2 sticks are the most sold Realtek USB adapters).

### #3 RTL8811AU / RTL8821AU / RTL8812AU (rtw88 88xxa, now upstream) - ACCEPT, medium
- rtw8821au.c 78 (26 IDs), rtw8812au.c 94 (34 IDs), rtw88xxa.c 1952, rtw8821a.c 1231, rtw8812a.c 1125, tables
  2350 + 2812. All Dual BSD/GPL. Firmware rtw8821a_fw.bin / rtw8812a_fw.bin (8051 WCPU, small, rtlwifi licence).
- USB-only chips (old 8051 firmware download path, different from 3081 IDDMA). New chip file + table generator run.
- Effort: 10-14 h after #1. Value: medium-high: extremely common old AC600/AC1200 dongles (Alfa AWUS036ACH etc.).

### #4 RTL8723DE (PCIe) + RTL8723DU (USB) - ACCEPT, lower priority
- rtw8723d.c 2212 + rtw8723x.c 789 + rtw8723d_table.c 1196 + rtw8723de.c / rtw8723du.c 36 (2 IDs). Dual BSD/GPL.
  PCI ID 10ec:D723. Firmware rtw8723d_fw.bin 28884 B (8051 WCPU, legacy fw download; 802.11n 1x1, 2.4 GHz only).
- 8723DE is in many budget laptops (HP/Lenovo 2018-2021). PCIe path works without #1; DU needs #1.
- Effort: 8-12 h. Value: medium (laptops), needs 8051 firmware path also needed by #3 (share the work).

### #5 rtw89: RTL8852BE first (then 8851BE, 8852CE/AE, 8922AE) - ACCEPT license, DEFER (XL)
- License OK (all files GPL-2.0 OR BSD-3-Clause). Firmware rtlwifi licence, 1.0-2.7 MB per chip.
- Size: whole tree 315283 lines; minimal shared core (core, mac, phy, fw, pci, efuse, cam, chan, coex, ps, regd)
  ~64.5k lines + reg.h 11.1k; 8852B family adds ~31.4k (rtw8852b*.c incl. rfk tables); 8851B ~22.1k.
  coex.c alone is 14.5k (can be stubbed to "WLAN only" initially).
- Completely different architecture from rtw88 (new MAC/HCI-DMA, CMAC/DMAC, firmware-offloaded scan, HE rates). Not
  an extension of our rtw88 port; a second driver. PCI IDs: 8852BE 10ec:b852/b85b, 8851BE b851, 8852AE 8852/a85a,
  8852CE c852, 8922AE 8922/892B. rtw89 also has usb.c (1332) for 8851BU/8852BU/8852AU/8852CU/8922AU.
- Effort: 60-100+ agent-hours for one chip to associate with WPA2. Value: medium-high for newer laptops
  (8852BE is the most common Realtek Wi-Fi 6 card), zero for the owner's PC today.

## 5. REJECTED / not worth it
- rtw88 SDIO (sdio.c, 8723DS/8821CS/8822BS/CS/8723BS/CS): license OK, but x86 PCs essentially never have SDIO Wi-Fi
  (only some Atom tablets) and we have no SDIO host. REJECTED on value, not license.
- Linux rtl8xxxu (RTL8188/8192/8723BU USB): GPL-2.0-only. REJECTED (license). Alternative already listed:
  OpenBSD urtwn (ISC).
- Out-of-tree Realtek vendor drivers (e.g. morrownr/aircrack 88x2bu, 8821cu, 8812au trees): GPL-2.0-only Realtek
  vendor code. REJECTED (license) - use upstream rtw88 instead, which now covers the same chips.
- Firmware: none rejected; all rtw88/rtw89 blobs are redistributable (rtlwifi licence, notice required).

## 6. Recommended order
#1 (USB API + bus refactor + 8821CU) -> #2 (8822BU/CU, cheap) -> #4 PCIe 8723DE (independent) -> #3 88xxa USB
-> #5 rtw89 only as a dedicated multi-night project.
