# Vega: permissively licensed code from other open OSes and libraries for NoovexOS 64

Scope: FreeBSD, OpenBSD, Haiku, Zephyr, Apache NimBLE, lwext4, FatFs (plus license checks on BTstack and two Linux driver families). Every license and LOC figure below was checked in shallow or sparse clones on 2026-11-14 (upstream HEAD). LOC means `wc -l` of the files named, comments included.

What NoovexOS has today, checked in `/home/claude/src/NoovexOS-64-bit-main`:
- `fs/vfs/volumes.c` and `parts.c` only recognise exFAT and ext2/3/4 and print a label. Neither filesystem can be mounted.
- USB networking (`drivers/usb/xhci_parts/05_usbnet.c`) covers only the class protocols CDC-ECM, CDC-NCM and RNDIS. No vendor chips.
- There is no evdev, hidraw, tun or usbfs-ioctl interface. `/dev/input` lists only `mice` (`sys/linux/syscalls/sysfs.c:359`).
- There is no Bluetooth code. `rtw_coex_wifi_only` treats the RTL8821CE's Bluetooth half as absent.

Effort scale: S = up to 1 week, M = 1–3 weeks, L = 1–2 months, XL = more than 2 months, for one developer porting with Claude.

## Ranked table

| # | Item | Project | Path / URL | License (verified) | LOC | Glue NoovexOS needs | Effort | Value |
|---|---|---|---|---|---|---|---|---|
| 1 | Bluetooth: USB HCI transport, Realtek and Intel firmware loaders, host stack | FreeBSD (`rtlbtfw`, `iwmbtfw`, `ng_ubt` as reference); Zephyr BT host (classic + LE); or Apache NimBLE (LE only) | freebsd-src `usr.sbin/bluetooth/rtlbtfw/`, `usr.sbin/bluetooth/iwmbtfw/`, `sys/netgraph/bluetooth/drivers/ubt/ng_ubt.c`; zephyr `subsys/bluetooth/host/` (+`classic/`); mynewt-nimble `nimble/host/src/` | rtlbtfw, iwmbtfw, ng_ubt: BSD-2-Clause. Zephyr host: Apache-2.0 (all 52 SPDX lines). NimBLE: Apache-2.0 | rtlbtfw 1,388; iwmbtfw 1,862; ng_ubt 1,728; Zephyr host core 50,835 + classic 58,471; NimBLE host 40,267 | Write a USB HCI driver in xHCI (control endpoint for commands, interrupt endpoint for events, bulk endpoints for ACL; about 600 lines, with ng_ubt as the reference). Port rtlbtfw from libusb to direct xHCI control transfers. Write a shim for the Zephyr kernel APIs it uses (k_work, net_buf, atomic, logging, settings, PSA crypto), or for NimBLE's NPL layer (callout, eventq, mutex, sem). Pairing UI. Bridge HID-over-BT to the input layer. | XL (Zephyr) / L (NimBLE, LE only) | Very high. Nothing exists yet. Unlocks BT mice, keyboards, headsets and the RTL8821CE's own Bluetooth part |
| 2 | Intel Wi-Fi 6/6E: AX200, AX201, AX210, AX211 (`iwx`); older 7260–9560 (`iwm`) | OpenBSD | openbsd/src `sys/dev/pci/if_iwx.c`, `if_iwxreg.h`, `if_iwxvar.h`; `sys/dev/pci/if_iwm.c` | `if_iwx.c` and `if_iwxvar.h`: ISC. `if_iwxreg.h`: dual BSD/GPLv2 (Intel), use under BSD. `if_iwm.c`: ISC | iwx 12,903 + 8,961 + 858; iwm 12,243; net80211 19,963 | Either port OpenBSD net80211 (scan, node and AMPDU handling; 276 `ieee80211_` references in `if_iwx.c`) or map those calls onto `net/wifi/wifi_mgr.c` and the existing WPA2/WPA3/EAP code. Also needed: PCI MSI-X, DMA rings, firmware loading of Intel `.ucode` TLV images and AX210 PNVM, and 802.11 header to Ethernet conversion. | XL | Very high. Intel is the most common laptop Wi-Fi |
| 3 | Realtek USB Ethernet RTL8152, RTL8153, RTL8156 (`ure`) | OpenBSD | openbsd/src `sys/dev/usb/if_ure.c`, `if_urereg.h` | BSD-2-Clause (no advertising clause) | 4,477 | xHCI bulk in/out plus vendor control requests (`ure_ctl` register access); register a `struct nvx_nic` as `05_usbnet.c` does; PHY/link polling | M | High. Most USB-C and Lenovo dock Ethernet uses these chips (60 USB IDs in the driver) |
| 4 | Mount ext2/3/4, read and write, with journal recovery | lwext4 | github.com/gkostka/lwext4 `src/`, `include/` | BSD-3-Clause, except `ext4_extent.c` and `ext4_xattr.c`, which are GPL-2.0-or-later. Whole library is GPLv3-compatible | 17,628 (`src/*.c`) | `ext4_blockdev` callbacks to AHCI/NVMe/USB-MSC sector I/O; VFS adapter in `fs/vfs`; locking | M | High. Dual-boot Linux partitions are already detected but cannot be opened |
| 5 | SD/MMC card readers: SDHCI (`sdhc`), Realtek RTS52xx PCIe (`rtsx`) | OpenBSD | openbsd/src `sys/dev/sdmmc/sdhc.c`, `sdmmc.c`, `sdmmc_mem.c`, `sdmmc_io.c`, `sdmmc_cis.c`; `sys/dev/pci/sdhc_pci.c`; `sys/dev/ic/rtsx.c`, `rtsxreg.h`; `sys/dev/pci/rtsx_pci.c` | ISC (all files listed) | sdmmc + sdhc + sdhc_pci 5,201; rtsx 2,138 | PCI attach, interrupt and DMA buffers; replace sdmmc's task thread with a NoovexOS worker; expose as a block device next to AHCI/NVMe (skip `sdmmc_scsi.c`) | M | Medium-high. Laptops with built-in card slots |
| 6 | Gamepads: Xbox 360 wired, DualShock 4, DualSense, generic HID joystick | FreeBSD | freebsd-src `sys/dev/hid/hgame.c`, `xb360gp.c`, `ps4dshock.c`, `ps5dsense.c`, `hidmap.c`, `hid.c` | BSD-2-Clause (SPDX on all) | 230 + 180 + 1,406 + 472 + 834 + 1,105 | Small hidbus shim over the existing xHCI HID path. Xbox 360 is not HID class: FreeBSD matches it in `usbhid.c:763` and supplies a custom report descriptor. Add Linux evdev `/dev/input/eventN` with EVIOCG* ioctls so SDL2 and glibc games see the pads. | M | Medium-high. Doom and Quake are already ported, and apt/X11 games need evdev |
| 7 | exFAT read/write, plus FAT12/16/32 | FatFs R0.16 | elm-chan.org/fsw/ff (`source/ff.c`, `ffconf.h`, `diskio.c`) | FatFs license (1-clause BSD style; text explicitly says GPL-compatible) | 7,249 (`ff.c`) | `disk_read`/`disk_write`/`disk_ioctl` to the block layer; `FF_FS_EXFAT=1`, `FF_LBA64=1`; VFS adapter | S–M | Medium. SDXC cards and large USB sticks are exFAT. Pairs with #5 |
| 8 | WireGuard VPN (kernel) | OpenBSD | openbsd/src `sys/net/if_wg.c`, `wg_noise.c`, `wg_cookie.c`; `sys/crypto/blake2s.c`, `curve25519.c`, `chachapoly.c`, `chacha_private.h`, `poly1305.c` | ISC for wg, blake2s, curve25519 and chachapoly; public domain for chacha_private.h and poly1305.c | 2,865 + 1,338 + 698 + ~1,950 crypto | Kernel UDP socket, a virtual interface in `net/stack`, routing by allowed-IPs (OpenBSD uses ART), and a configuration path (ioctl or a Linux netlink subset for `wg(8)`) | L | Medium |
| 9 | CPU temperature sensors | OpenBSD `ksmn` (AMD Zen, families 17h/19h, via SMN) and `km` (AMD 10h–16h); FreeBSD `coretemp` (Intel) | openbsd/src `sys/dev/pci/ksmn.c`, `km.c`; freebsd-src `sys/dev/coretemp/coretemp.c` | ksmn and km: ISC. coretemp: BSD-2-Clause | 262; 124; 440 | PCI config-space access (already present), MSR reads, then expose through sysfs hwmon or the Settings app | S | Medium-low. Easy, visible win (fan/thermal panel) |
| 10 | USB serial: CDC-ACM (`umodem`), FTDI, PL2303, CP210x, CH340 | OpenBSD | openbsd/src `sys/dev/usb/umodem.c`, `uftdi.c`, `uplcom.c`, `uslcom.c`, `uchcom.c`, `ucom.c` | uslcom: ISC. The others: BSD-2-Clause (no advertising clause) | 5,636 together | Bulk endpoints; a tty layer with termios for `/dev/ttyUSB*` and `/dev/ttyACM*` in the Linux layer | M | Medium. Arduino, Pico, serial consoles |
| 11 | Qualcomm Wi-Fi 6: QCA6390, WCN6855 (`QCNFA765`), QCN9074 (`qwx`); WCN7850 (`qwz`) | OpenBSD | openbsd/src `sys/dev/ic/qwx.c`, `sys/dev/pci/if_qwx_pci.c`; `sys/dev/ic/qwz.c`, `if_qwz_pci.c` | ISC | qwx 27,639 + 4,288; qwz 25,208 | Same net80211 question as #2, plus MHI/QMI firmware boot (inside `qwx.c`) | XL | Medium |
| 12 | Packet filter / firewall (`pf`) | OpenBSD | openbsd/src `sys/net/pf*.c`, `pfvar.h`, `pfvar_priv.h` (+ `sbin/pfctl`) | BSD-2/ISC. `pfkeyv2*.c` has the 4-clause advertising clause but is not part of pf | 24,591 kernel (pfctl not measured) | Deeply tied to mbuf, ifnet, routing and pfsync; hooks into the `net/stack` input/output paths; pfctl and its yacc grammar | XL | Medium. A small native filter may cost less |
| 13 | Broadcom FullMAC Wi-Fi: BCM4350, 4356, 43602, 4371, 4378, 4387 (`bwfm`) | OpenBSD | openbsd/src `sys/dev/ic/bwfm.c`, `sys/dev/pci/if_bwfm_pci.c` | ISC | 3,324 + 2,596 | msgbuf/PCIe rings and firmware plus NVRAM loading. FullMAC means the firmware does the 802.11 MAC; still 98 `ieee80211_` calls in `bwfm.c` | M–L | Low on x86 PCs (mainly Macs) |
| 14 | Older Realtek Wi-Fi: RTL8188CE/8192CE PCI (`rtwn`), RTL8188/8192 USB (`urtwn`) | OpenBSD | openbsd/src `sys/dev/ic/rtwn.c`, `sys/dev/pci/if_rtwn.c`, `sys/dev/usb/if_urtwn.c` | ISC | 3,433 + 2,407 + 2,736 | net80211, or reuse the RTL8821CE 802.11 code | L | Low-medium (cheap USB dongles) |
| 15 | exFAT (read-only) and ext2/3/4 drivers in C++ | Haiku | haiku `src/add-ons/kernel/file_systems/exfat`, `.../ext2` | MIT | 2,685; 13,757 | C++ runtime in the kernel and the Haiku fs_volume API | L | Low. #4 and #7 (C libraries) fit better |

Not usable or not recommended:
- **BTstack is not GPLv3-compatible.** Its LICENSE clause 4 reads "Any redistribution, use, or modification is done solely for personal benefit and not for any commercial purpose". That is an extra restriction the GPL forbids.
- **OpenBSD `sys/dev/usb/if_cdce.c` and `sys/net/pfkeyv2*.c`** carry the 4-clause advertising clause, which is GPL-incompatible. CDC-ECM is already implemented in NoovexOS anyway.
- **Zephyr has no USB HCI transport.** `drivers/bluetooth/hci/` covers only UART (H4/H5), SPI, IPC and vendor SoCs, so the USB transport must be written for NoovexOS.
- **NimBLE is BLE only.** No BR/EDR, so no classic BT keyboards or A2DP headphones.

Related Linux findings outside my area, checked in `/tmp/ref/linux`:
- `drivers/net/wireless/intel/iwlwifi/mvm/mac80211.c` is `GPL-2.0 OR BSD-3-Clause`.
- `drivers/net/wireless/mediatek/mt76/mac80211.c` and `mt76/mt7921/main.c` are `BSD-3-Clause-Clear`.
- So Linux iwlwifi and mt76 are also candidates, which matters for MediaTek Wi-Fi.
- FreeBSD's own iwx port (`sys/dev/iwx/if_iwx.c`, 11,214 lines) is `(GPL-2.0-only OR BSD-3-Clause) AND ISC`, usable under the BSD option.

## Top-5 notes

### 1. Bluetooth
Order of work:
1. **USB transport.** The Bluetooth part of the RTL8821CE combo card shows up as a USB device. Write `drivers/usb/xhci_parts/0x_ubt.c`. Use `sys/netgraph/bluetooth/drivers/ubt/ng_ubt.c` (BSD-2) as the reference for endpoint layout:
   - control transfer with bmRequestType 0x20 (`UBT_HCI_REQUEST`, `ng_ubt_var.h:55`) for HCI commands
   - interrupt IN for events
   - bulk IN/OUT for ACL
   - isochronous endpoints for SCO (can wait)
2. **Firmware patch.** Port `usr.sbin/bluetooth/rtlbtfw/{main.c,rtlbt_fw.c,rtlbt_hw.c}`. `rtlbt_fw.c:70-74` has an explicit `rtl8821c` entry (needs `rtl8821c_fw.bin` and `rtl8821c_config.bin` from linux-firmware). It currently uses libusb (`main.c:43`). Swap those calls for NoovexOS xHCI control transfers. For Intel AX2xx/9xxx Bluetooth, use `usr.sbin/bluetooth/iwmbtfw/` the same way.
3. **Host stack.** Two options:
   - **Zephyr** (`subsys/bluetooth/host/hci_core.c`, `conn.c`, `l2cap.c`, `smp.c`, `att.c`, `gatt.c`, `classic/{br.c,conn_br.c,l2cap_br.c,ssp.c,sdp.c,hid_host.c,a2dp.c,avdtp.c}`) covers BR/EDR and LE. `classic/hid_host.c` (1,680 lines) is the path to BT keyboards and mice. a2dp/avdtp is the path to headphones. Most-used Zephyr headers are `kernel.h`, `net_buf.h`, `sys/atomic.h`, `logging/log.h` and `settings/settings.h`, so a compatibility shim is the main glue.
   - **NimBLE** (`nimble/host/src/`, porting layer at `porting/npl/linux/src/`) is a smaller, explicitly portable LE-only alternative. `nimble/transport/socket/src/ble_hci_socket.c` shows a raw-HCI transport (AF_BLUETOOTH/HCI_CHANNEL_USER) that could sit on a NoovexOS HCI device.
4. **Pairing and HID.** FreeBSD `usr.sbin/bluetooth/bthidd/` (BSD-2, 2,946 lines) is a small worked example of mapping BT HID reports onto an input layer.

### 2. Intel Wi-Fi (iwx / iwm)
Start from:
- `sys/dev/pci/if_iwx.c`: device table at about line 11669; AX210 PNVM loading at about lines 4347–4373; firmware names `iwx-cc-a0-77`, `iwx-ty-a0-gf-a0-77`, `iwx-bz-*-92`.
- `if_iwxreg.h` and `if_iwxvar.h`.

The main decision is how much of `sys/net80211/` (19,963 lines, ISC/BSD) to bring along. Its scan, association, rate control (`ieee80211_ra*.c`) and AMPDU reordering are what the driver calls into. NoovexOS already has its own WPA2/WPA3/EAP supplicant in `net/wifi/`, so OpenBSD's `ieee80211_pae_*` can be left out. Port iwx first (AX200/201/210 cover most laptops from 2019 on), then iwm, which has a similar structure.

### 3. ure (USB Ethernet)
- Single file pair: `sys/dev/usb/if_ure.c` and `if_urereg.h` (BSD-2).
- Hardware access goes through `ure_ctl()` and the `ure_read_*`/`ure_write_*` helpers (vendor control requests on endpoint 0), bulk RX/TX, and the RTL8152/8153/8156 init sequences (`ure_rtl8152_init`, `ure_rtl8153_init`, `ure_rtl8153b_init` and related).
- Glue mirrors `05_usbnet.c`: claim the interface, set up bulk rings, fill `struct nvx_nic {send, recv, link}`.
- RTL8152/8153/8156 need no firmware file. The newer RTL8157/8159 paths call `ure_loadfirmware()` (`if_ure.c:2533`), so leave those out at first.

### 4. lwext4 (ext2/3/4)
- Start from `src/ext4.c` (mount/open/read/write API), `src/ext4_blockdev.c` (block-device callbacks), `src/ext4_journal.c` (journal recovery), `include/ext4_config.h`.
- Write one `ext4_blockdev_iface` backed by the NoovexOS sector I/O used in `fs/vfs/parts.c` and `volumes.c`, then a VFS adapter.
- Keep the copyright headers. The two GPL-2.0-or-later files are fine under GPLv3.

### 5. SD/MMC (sdhc + rtsx)
- Bus-independent core: `sys/dev/sdmmc/sdmmc.c` (card discovery), `sdmmc_mem.c` (SD/MMC memory cards, block read/write), `sdmmc_io.c` and `sdmmc_cis.c` (SDIO, optional).
- Host controllers:
  - `sys/dev/sdmmc/sdhc.c` plus `sys/dev/pci/sdhc_pci.c` for standard SDHCI, e.g. Intel integrated readers.
  - `sys/dev/ic/rtsx.c` plus `sys/dev/pci/rtsx_pci.c` for Realtek RTS5209/5227/5229/522A/5249/525A and RTL8402/8411/8411B PCIe readers.
- Skip `sdmmc_scsi.c` and register the card directly as a NoovexOS block device.
- Combined with FatFs exFAT (#7), SDXC cards become readable.

## Clones used
All were under `/tmp/ref` and have been deleted except shared directories: `vega-obsd`, `freebsd`, `zephyr`, `nimble`, `lwext4`, `fatfs`, `haiku`. `/tmp/ref/openbsd` is shared with another scout (its sparse set changed while I worked), so I left it.
