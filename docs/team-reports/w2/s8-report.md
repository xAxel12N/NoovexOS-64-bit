# s8 — Device naming/ID databases and quirk tables

## What we have (grep of tree, third_party excluded)
- `apps/accessories/small_apps.c:45` `pci_class_name()` — ~15 hard-coded PCI class/subclass strings.
- `drivers/net/ethernet/nic_names.c` — hand-written vendor:device -> NIC name for a few Intel/Realtek parts.
- `shell/commands/processes_devices_wifi_bringup.c` — inline switch on 3 Wi-Fi vendor IDs.
- `shell/commands/network_and_apps.c:112` `lsusb`/`usbdevs` — prints the device's own USB string descriptor + VID:PID; no ID database.
- No pci.ids/usb.ids, no USB/HID/PCI quirk table, no HID usage-name table.

## Sources

| # | Source / URL | Licence (verified) | Contents | Size | Use 1-5 |
|---|---|---|---|---|---|
| 1 | pci.ids — https://pci-ids.ucw.cz/v2.2/pci.ids (snapshot 2026-11-14) | header: "distributed under either the GNU General Public License (version 2 or higher) or the 3-clause BSD License" | 2,526 vendors, 21,777 devices, 18,245 subsystems, plus `C` class/subclass/prog-if names | 1.68 MB raw; with comments and subsystems removed 946 KB, gzip-9 222 KB, xz 169 KB | 5 |
| 2 | usb.ids — http://www.linux-usb.org/usb.ids (v2026.06.26) | no licence line in the file itself. hwdata (vcrhonek/hwdata LICENSE), which redistributes it: "GNU GPL version 2 or later" OR "XFree86 1.0". Either option works with GPLv3 | 3,427 vendors, 20,537 products, plus `C` class/subclass/protocol, `AT` audio terminals, `HID` descriptor types, `R` report items, `HUT` (29 HID usage pages, ~1,230 usage names), `L` language IDs | 730 KB raw; vendor/product part 696 KB, gzip-9 242 KB, xz 195 KB | 5 |
| 3 | FreeBSD sys/dev/usb/quirk/usb_quirk.c — https://github.com/freebsd/freebsd-src/blob/main/sys/dev/usb/quirk/usb_quirk.c | `SPDX-License-Identifier: BSD-2-Clause` | `USB_QUIRK(VENDOR, PRODUCT, UQ_...)` table: about 73 distinct UQ_ flags (UQ_MSC_NO_SYNC_CACHE, UQ_KBD_IGNORE, UQ_CFG_INDEX_n, UQ_MSC_FORCE_*...), several hundred entries. Code can be ported | 1,068 lines | 4 |
| 4 | FreeBSD sys/dev/usb/usbdevs | BSD 2-clause text (NetBSD heritage, "Redistribution and use...") | 778 vendors, 3,419 products as symbolic names. This is the name table usb_quirk.c needs (REALTEK=0x0bda) | 5,249 lines | 3 |
| 5 | FreeBSD sys/dev/hid/hid.h (+ sys/dev/usb/usbhid.h) | `SPDX-License-Identifier: BSD-2-Clause` | HUP_* usage pages, HUG_*/HUD_* usage constants, report item tags. Code can be ported | 360 + 97 lines | 3 |
| 6 | Linux drivers/hid/hid-quirks.c | `SPDX-License-Identifier: GPL-2.0-or-later` | about 838 HID_USB/BT_DEVICE quirk entries (HID_QUIRK_NOGET, ALWAYS_POLL, MULTI_INPUT, ignore/have-special-driver lists). The licence allows taking it as code | 1,420 lines | 4 |
| 7 | Linux drivers/hid/hid-debug.c | `SPDX-License-Identifier: GPL-2.0-or-later` | full HID usage-page/usage name table (`hid_usage_table[]`), more complete than usb.ids HUT. The licence allows taking it as code | 3,840 lines | 3 |
| 8 | Linux drivers/usb/core/quirks.c | `SPDX-License-Identifier: GPL-2.0` (only) → **facts only** | about 152 USB_DEVICE entries, 20 USB_QUIRK_* flags (NO_LPM, RESET_RESUME, DELAY_INIT, CONFIG_INTF_STRINGS...). Can be written up again as data (VID, PID, flag) | 820 lines | 3 (facts) |
| 9 | Linux drivers/pci/quirks.c | `SPDX-License-Identifier: GPL-2.0` (only) → **facts only** | about 720 DECLARE_PCI_FIXUP_*: mostly code fixups (ASPM, D3 delay, INTx, ACS, VMD, NVIDIA HDA enable). Only the ID and "what breaks" facts can be used | 6,597 lines | 2 |

Owner hardware check:
- pci.ids has `10ec:c821 RTL8821CE 802.11ac PCIe Wireless Network Adapter`, `10de:1f82 TU117 [GeForce GTX 1650]`, `8086:4c8a 11th Gen ... UHD Graphics 750`.
- usb.ids has vendor 0bda but **no c123 entry**, so the BT name has to come from a local override.
- No Linux or FreeBSD USB quirk matches 0bda:c123. Linux lists 0bda:0151/0487/8153 (NO_LPM, CONFIG_INTF_STRINGS).

## Compact-shipping design (lspci/lsusb-style names)
- **Build step:** a host Python packer (`tools/mkids.py`) turns pci.ids + usb.ids into `/usr/share/hwdata/ids.bin`:
  - sorted `u16 vendor -> (str off, dev range)` and `u16 dev -> str off` arrays, plus a string pool.
  - Drop subsystems (`\t\t`) and comments; keep class tables and HUT.
  - Binary search, no parsing at runtime.
  - Estimate: about 1.6 MB uncompressed for both files without subsystems, or about 400 KB gzip if our initrd/fs supports deflate.
- **Tier 2, in-kernel subset:** compile a C array for about 25 common vendors (Intel, NVIDIA, AMD, Realtek, virtio/QEMU/VMware/VBox...). Measured: 500 KB raw, 102 KB deflated, for pci only. Alternatively cover only device classes plus the IDs our drivers bind. This works before the filesystem is up.
- **Fallback chain:** local override table (`0bda:c123 "Realtek Bluetooth (RTL8821CE combo)"`) -> ids.bin -> USB string descriptor (current behaviour) -> class name.
- **Quirks:**
  - Port FreeBSD usb_quirk.c (BSD) as the table plus the UQ_ flag enum.
  - Add Linux USB/PCI quirks as hand-written (VID, PID, flag) facts.
  - Linux hid-quirks.c (GPL-2.0+) can be taken directly.
  - Apply them at xHCI enumeration (NO_LPM, DELAY_INIT, CONFIG_INTF_STRINGS) and in HID attach.
- **Licence note:** in the data file, keep the upstream header and version line. Note "GPL-2.0-or-later OR BSD-3-Clause" (pci.ids) and "GPL-2.0-or-later OR XFree86-1.0" (usb.ids, via hwdata) in third_party notices.

## Top 3
1. **pci.ids + usb.ids, packed into a binary-search blob** (sources 1 and 2): `lspci`/`lsusb` show real names for all owner hardware. The only gap is the BT ID, which needs a local override.
2. **FreeBSD usb_quirk.c + usbdevs** (BSD-2, port as code): a ready quirk mechanism and flag set for xHCI/MSC/HID enumeration.
3. **Linux hid-quirks.c + hid-debug.c usage table** (GPL-2.0-or-later, usable as code): HID device quirks and complete usage names for an `lshid`/HID debug view. Linux usb/core and pci quirks are GPL-2.0-only, so we can take only facts from them.

Downloaded copies for reference: /tmp/s8dl/ (untrusted, not in tree).
