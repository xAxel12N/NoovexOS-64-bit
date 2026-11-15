# s2: Intel / MediaTek / Broadcom USB Bluetooth firmware loading

## What we already have
drivers/bluetooth/{btusb.c,hci.c,rtlbt_fw.c}: generic btusb plus Realtek rtlbt firmware loading only (owner BT is 0BDA:C123). A grep for btintel/btmtk/bcm20702/patchram/iwmbt/0x8087/0x0e8d found nothing. So we have no Intel, MediaTek or Broadcom vendor init.

## Sources (licence checked from the file header)
| # | URL | Licence (SPDX) | Contents | Size | Use |
|---|-----|----------------|----------|------|-----|
| 1 | github.com/freebsd/freebsd-src usr.sbin/bluetooth/iwmbtfw/{iwmbt_fw.h,iwmbt_fw.c,iwmbt_hw.c,iwmbt_hw.h,main.c} | `SPDX-License-Identifier: BSD-2-Clause` | Intel BT, legacy and TLV. Has `struct iwmbt_version` (FW_VARIANT 0x06 bootloader, 0x23 operational), `iwmbt_boot_params`, `iwmbt_version_tlv`, TLV_IMG_TYPE 0x01/0x03, RSA/ECDSA CSS header sizes (644/320, offset 8), .sfi/.ddc file naming, HCI 0xFC05 read-version / 0xFC09 secure-send / 0xFC01 reset / 0xFC8E DDC flow, USB endpoints | hw.c 721 lines, fw.h 158, hw.h 122 | 5 |
| 2 | github.com/torvalds/linux drivers/bluetooth/btintel.{h,c} | `SPDX-License-Identifier: GPL-2.0-or-later` (usable) | Full INTEL_TLV_* enum (0x10..0x50: CNVI/CNVR_TOP, IMAGE_TYPE, SBE_TYPE, OTP_BDADDR, ...), `struct intel_tlv`, `intel_version_tlv`, `intel_boot_params`, firmware name formation (ibt-<cnvi>-<cnvr>.sfi), boot/bootloader states | btintel.h 471 lines, btintel.c ~3.7k | 4 |
| 3 | github.com/torvalds/linux drivers/bluetooth/btmtk.{h,c} | `/* SPDX-License-Identifier: ISC */` (usable) | MediaTek MT7921/7922/7925 USB BT. Firmware file names (BT_RAM_CODE_MT7922_1_1_hdr.bin, MT7961_1_2_hdr.bin, ...), `btmtk_global_desc`/`btmtk_section_map` patch-header layout, WMT opcodes (PATCH_DWNLD, FUNC_CTRL, RST, SEMAPHORE), regs MT7921_BTSYS_RST 0x70002610, DLSTATUS 0x7c053c10, MTK_BT_WDT_STATUS 0x740003A0, MTK_EP_RST_OPT 0x74011890, USB vendor control reg read/write | btmtk.h 322 lines, btmtk.c ~1.5k | 5 |
| 4 | github.com/torvalds/linux drivers/bluetooth/btbcm.c | `SPDX-License-Identifier: GPL-2.0-or-later` (usable) | Broadcom BCM20702 patchram: HCI 0xFC2E download-minidriver, then the .hcd stream of raw HCI commands (0xFC4C write-RAM, 0xFC4E launch-RAM), 250 ms wait, HCI reset. Subver/product to BCM name table (BCM20702A0/A1/B0...), USB IDs via btusb | ~800 lines | 4 |
| 5 | github.com/freebsd/freebsd-src usr.sbin/bluetooth/bcmfw/bcmfw.c | `SPDX-License-Identifier: BSD-2-Clause` | Only for the old BCM2033 (0a5c:2033) with a 2-stage USB bulk/interrupt loader. Not BCM20702 patchram | 309 lines | 1 |
| 6 | freebsd sys/netgraph/bluetooth/drivers/ubt/ng_ubt.c | `SPDX-License-Identifier: BSD-2-Clause` | Generic USB BT transport and device-ID table, including quirks for the Intel 8087:07dc/0a2a/0aa7 and 0025/0026/0029 bootloader-mode ignore list | 2069 lines | 3 |

Not fetched: Zephyr (it has no USB Intel/MTK loaders, only HCI-UART vendor code). OpenBSD ubt has no Intel or MTK firmware loaders.

## Top 3
1. FreeBSD iwmbtfw (BSD-2): portable, complete Intel legacy and TLV loader; the easiest to port.
2. Linux btmtk.c/h (ISC): the only clean source for the MT7921/7922 patch header and the WMT download protocol.
3. Linux btbcm.c (GPL-2.0-or-later): BCM20702 .hcd patchram is simple (minidriver cmd, then replay HCI records).
