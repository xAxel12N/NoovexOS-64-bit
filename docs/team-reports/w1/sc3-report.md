# Scout sc3: USB/PCIe Wi-Fi beyond rtw88 (MediaTek mt76, Atheros ath9k/ath9k_htc, ath10k/11k, Broadcom brcmfmac)

Sources checked: local Linux 7.4-rc trees (/tmp/ref/linux, /tmp/ref/linux-atlas), OpenBSD src (/tmp/ref/openbsd),
GitHub torvalds/linux tags v6.12, v6.18, v6.19, v7.0, v7.1 (raw SPDX lines), linux-firmware WHENCE + LICENSES/ (gitlab main, today).

## 0. What NoovexOS already has (do not re-propose)
- Wi-Fi driver: rtw88 hand port, PCIe only (RTL8821CE works on owner PC; 8822BE/8822CE chips added). `drivers/net/wifi/rtw88/`.
  Bus layer `g_rtw_bus` (`include/nvx/drivers/net/wifi/rtw_chip.h`) already has `RTW_BUS_USB`, but only r8..w32 hooks (no bulk TX/RX).
- Wi-Fi stack `net/wifi/`: manager, scan, join, WPA2/WPA3/EAP, netdev, **software CCMP** (`wifi_netdev.c`). No mac80211/net80211.
- USB: xHCI with HID, storage/hubs, UVC, CDC-ECM/NCM/RNDIS (`drivers/usb/xhci/usb_network.c`: one static device, own bulk rings).
- Worklist already has: #1 rtw88 USB (RTL8821CU/8822BU/8822CU), #18 MT7921/7922 PCIe (mt76), #20 ath10k / bwfm / qwx.
  Worklist "Not usable" already lists Linux mt7601u.

### Two blockers that apply to EVERY candidate below (and to rtw88 USB)
1. **net/wifi is hard-wired to rtw88**: 79 direct `rtw_*` calls (wifi_mgr.c 53, wifi_netdev.c 14, wpa2.c 12: `rtw_tx_mgmt`,
   `rtw_scan_frame`, `rtw_wpa2_join`, `rtw_tx_data_eapol`, `rtw_rx_wait_eapol`, `rtw_set_channel_2g`, `rtw_h2c_cmd` ...).
   A second driver needs a `wifi_hw_ops` table first (set_channel, tx_mgmt, tx_data, rx_poll, set_bssid/filter, mac addr,
   radio on/off, diag). Est. 5-7 agent-hours incl. host tests. Do it once, ideally together with rtw88 USB (#1).
2. **No generic USB pipe API**: usb_network.c owns its rings privately. A Wi-Fi stick needs vendor control transfers,
   1 bulk-out (often several), 1-2 bulk-in, and for AR9271 interrupt in/out. Factor a small `usbdev_pipe` API out of
   usb_network.c (claim interface, open bulk/int EP, submit, poll events, unhalt). Est. 4-6 agent-hours.

## 1. Candidates (ranked)

| # | Target | Source | License (proof) | Firmware + licence | Size | Effort (agent-h) | Value |
|---|---|---|---|---|---|---|---|
| 1 | **AR9271 USB** (2.4 GHz 11n 1x1) | OpenBSD athn(4): `sys/dev/usb/if_athn_usb.c` + `sys/dev/ic/athn.c, ar5008.c, ar9285.c` (+ reg/var headers) | **ISC** (each file carries the ISC "Permission to use, copy, modify, and distribute" header; copyright Bergamini/Sperling). Reference: Linux ath9k + ath9k_htc **ISC** (all ath9k/*.c,*.h and ath/*.c checked: ISC text; `hw.c`, `init.c`, `htc_drv_init.c` MODULE_LICENSE("Dual BSD/GPL")) | `htc_9271-1.4.0.fw` (OpenBSD name `athn-open-ar9271`), WHENCE: "Licence: Free software. See LICENCE.open-ath9k-htc-firmware" = Clear-BSD style QCA licence + eCos GPLv2+ w/ exception parts; firmware **source is public** (github.com/qca/open-ath9k-htc-firmware). Legacy `ar9271.fw` = LICENCE.atheros_firmware (redistributable) - use 1.4.0 | if_athn_usb 2761 + athn 3171 + ar5008 2954 + ar9285 884 + headers ~4k = **~13.8k** (only AR9285-family paths needed) | 2 blockers ~10 + port ~18 + tests ~4 = **~32** | Medium. Cheapest well-documented chip, fully open firmware, software crypto supported (`athn_usb_set_key` falls back to `ieee80211_set_key`, matches our sw CCMP). 2.4 GHz only |
| 2 | **MT7601U USB** (2.4 GHz 11n 1x1) | OpenBSD mtw(4): `sys/dev/usb/if_mtw.c` + `if_mtwvar.h` + `sys/dev/ic/mtwreg.h` | **ISC** (header in if_mtw.c, copyright Bergamini/Kevin Lo/James Hastings) | `mediatek/mt7601u.bin` (OpenBSD `mtw-mt7601u`), WHENCE: "Redistributable. See LICENCE.ralink_a_mediatek_company_firmware": binary redistribution allowed unmodified, no reverse engineering, patent grant for OSI-licensed OS. **No anti-GPL clause** - OK | ~3.4k + ~1.5k headers = **~5k** | 2 blockers ~10 + port ~14 + tests ~4 = **~28** | Medium. Cheapest sticks on the market (3-6 EUR, often 148f:7601). mtw is young (OpenBSD 7.1+) and has open RX/TX stability reports - budget debug time. Device table only enables MT7601U (MT7610U/MT7612U code paths are half-done) |
| 3 | **MT7612U USB** (dual-band 11ac 2x2) | Linux **v6.18** mt76: `usb.c, mac80211.c, tx.c, agg-rx.c, eeprom.c, mcu.c, util.c`, `mt76x02_*`, `mt76x2/usb*.c` + shared mt76x2 phy/eeprom/mcu/init/mac | **ISC in v6.18** (`// SPDX-License-Identifier: ISC` verified on usb.c, mt76x2/usb.c, mt76x02_usb_core.c at tag v6.18). **v6.19 and later: BSD-3-Clause-Clear** (verified v6.19/v7.0/v7.1). MODULE_LICENSE("Dual BSD/GPL") in mt76x2/usb.c, usb.c. -> Pin the port to v6.18 (also an LTS) to stay ISC | `mediatek/mt7662u.bin` + `mt7662u_rom_patch.bin`, WHENCE: LICENCE.mediatek - **"not ... in any other form which would require these files themselves to be covered by the GPL/LGPL"**. Consequence: must NOT be linked into the kernel image the way rtw88 firmware is (`_binary_rtw8821c_fw_bin_start`); ship as a separate file on the ISO and load at runtime | core 8.7k + x02 6.9k + x2u 2.7k = **~18k**, heavy mac80211 semantics | **~60** (needs a firmware-from-file loader too) | Medium-high: 5 GHz AC sticks (Alfa AWUS036ACM, Netgear A6210, Comfast CF-WU782AC, ~25-40 EUR). Best Linux-supported AC USB chip |
| 4 | **MT7921AU USB** (Wi-Fi 6 2x2) + shares code with worklist #18 (MT7921/7922 PCIe) | Linux **v6.18** mt76: core + `mt76_connac*` + `mt792x_*` + `mt7921/` (`usb.c` IDs 0e8d:7961, 0e8d:7902, 0846:9060/9065 Netgear A8000, 3574:6211, 35bc:0107) | **ISC in v6.18** (mt7921/usb.c verified ISC at v6.12 and v6.18; Clear BSD from v6.19). Dual BSD/GPL in mt7921/usb.c, mt792x_usb.c | `WIFI_MT7961_patch_mcu_1_2_hdr.bin` + `WIFI_RAM_CODE_MT7961_1.bin` (WHENCE "Driver: mt7921"), LICENCE.mediatek (same anti-GPL clause - separate file only) | mt7921 5.3k + mt792x 4.8k + connac 8.5k + core 8.7k = **~27k** | **~100** (MCU command protocol, firmware scan/sta-rec offload) | High long-term (AMD laptops with MT7921/7922 PCIe use the same core), but the USB stick alone is 30-60 EUR and far more work than #1/#2 |
| 5 | **Broadcom FullMAC USB** (BCM43143/43236/43242/43569) | OpenBSD bwfm(4): `sys/dev/ic/bwfm.c` 3324 + `sys/dev/usb/if_bwfm_usb.c` 873; or Linux brcmfmac | **ISC** (bwfm headers; Linux brcmfmac usb.c/core.c/cfg80211.c/fwil.c `SPDX: ISC`, MODULE_LICENSE("Dual BSD/GPL")) | `brcm/brcmfmac43143.bin`, `43236b`, `43242a`, `43569` (WHENCE "Driver: brcmfmac"), LICENCE.broadcom_bcm43xx: redistribute unmodified, only for Broadcom chips, include licence copy, no RE. Acceptable as a separate file | **~4.2k** + headers | ~30 (FullMAC: firmware does join; maps poorly to our softMAC join/WPA2 path, but firmware supplicant offload could replace it) | Low: these USB sticks are old and hard to buy; brcmfmac matters mostly for SDIO (Pi) and Macs |
| - | ath10k (QCA6174/QCA9377, PCIe) | Linux ath10k | ISC (`SPDX: ISC`, Dual BSD/GPL; 62 files) | ath10k/ firmware-*.bin + board-2.bin, LICENSE.QualcommAtheros_ath10k (redistributable) | ~85k | XL | Not a USB option (ath10k USB/SDIO is experimental, no retail sticks). Keep at worklist #20 |
| - | ath11k (PCIe) | Linux ath11k | BSD-3-Clause-Clear since first commit (also at v6.18, verified) - no ISC fallback | ath11k/ firmware + MHI | ~83k | XL | Not USB. Needs owner OK on Clear BSD (FSF lists it GPL-compatible) |

## 2. Rejected / not usable
- **Linux mt7601u**: `// SPDX-License-Identifier: GPL-2.0-only` (verified on master usb.c). REJECTED (use OpenBSD mtw, #2).
- **Linux mt76x0 (MT7610U/MT7610E)**: `mt76x0/usb.c` = `GPL-2.0-only`, MODULE_LICENSE("GPL"). REJECTED.
- **Linux mt76 v6.19+**: BSD-3-Clause-Clear - not rejected, but unnecessary: use v6.18 (ISC).
- carl9170 (AR9170 USB): GPLv2-or-later, so license OK, but hardware is from 2008 and unobtainable. Not pursued.
- TP-Link TL-WN722N **v2/v3** are RTL8188EUS (not AR9271) - warn the owner when buying; only **v1** is AR9271.

## 3. Firmware handling note (important for the lead)
NoovexOS currently links Wi-Fi firmware into the kernel (`assets/firmware/rtw8821c_fw.bin` -> `_binary_*_start`).
That is fine for Realtek and for open-ath9k-htc (free licence, public source) but **not for LICENCE.mediatek blobs**
(MT7612U, MT7921): those must be shipped as separate files on the ISO and loaded at runtime. MT7601U's Ralink licence
has no such clause. Broadcom: separate file + ship licence text.

## 4. Recommendation (least work given our Wi-Fi layer)
0. Baseline: worklist #1 **rtw88 USB (RTL8811CU/8821CU)** is still the least work of any USB stick (same chip family,
   tables, firmware, `RTW_BUS_USB` already in the bus enum). Do it first; build the two shared blockers
   (`wifi_hw_ops`, `usbdev_pipe`) as part of it.
1. Then **AR9271 via OpenBSD athn** (~22 h once blockers exist): ISC code, fully open firmware, software crypto, mature.
2. Then **MT7601U via OpenBSD mtw** (~18 h once blockers exist): cheapest hardware, ISC, firmware OK.
3. MT7612U / MT7921AU from **Linux v6.18 mt76 (ISC)** only after a firmware-from-file loader exists; MT7921 work should
   be planned together with worklist #18 (PCIe) since 2/3 of the code is shared.

Sticks to buy for testing (approx. prices, AliExpress/Amazon/Kjell-type retail): generic AR9271 stick or Alfa AWUS036NHA
(8-30 EUR); any MT7601U "150Mbps" nano stick (3-6 EUR); Alfa AWUS036ACM / Comfast CF-WU782AC for MT7612U (25-40 EUR);
Netgear A8000 / Comfast CF-951AX for MT7921AU (35-60 EUR).
