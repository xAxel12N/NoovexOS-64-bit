# Porting work list (scouts Atlas, Nova, Echo, Vega - v10.99 run)

License rule: NoovexOS is GPLv3. Only GPLv3-compatible code is ported (GPL-2.0-or-later, GPL-3.0, LGPL, MIT, BSD-2/3, ISC, Apache-2.0, zlib, dual-licensed such as rtw88 "GPL-2.0 OR BSD-3-Clause"). Linux "GPL-2.0-only" files are not usable. BSD-3-Clause-Clear (mt76, ath11k/12k) is listed by the FSF as GPL-compatible but needs the owner's confirmation. Full reports: scout files in the v10.99 handoff.

## In progress in v10.99
| Item | Source | License | Builder |
|---|---|---|---|
| rtw88 multi-chip core, H2C packets, IQK for 8821C | own + Linux rtw88 | GPL-2.0 OR BSD-3 | Forge |
| RTL8822BE | Linux rtw88 rtw8822b* | GPL-2.0 OR BSD-3 | Bridge |
| RTL8822CE | Linux rtw88 rtw8822c* | GPL-2.0 OR BSD-3 | Relay |
| Linux-compat gaps (loginuid, /sys cpuN, /proc/pid/cwd, EPOLLET, timestamps, RLIMIT_NOFILE) + HDA probe | own | - | Bolt |

## Next rounds (ranked)
| # | Item | Best source | License | Size | Effort | Value |
|---|---|---|---|---|---|---|
| 1 | rtw88 USB (RTL8821CU/8822BU/8822CU sticks) | Linux rtw88 usb.c + *cu.c | GPL-2.0 OR BSD-3 | 1.5k + chips | M | High |
| 2 | Bluetooth: Realtek USB transport + firmware loader | Linux btrtl.c/btusb (GPL-2.0+), FreeBSD rtlbtfw/ng_ubt (BSD-2) | OK | ~2-7k | M | High |
| 3 | Bluetooth host stack | FreeBSD netgraph bluetooth (BSD-2, 33k) or NimBLE (Apache-2.0, LE only) or Zephyr host (Apache-2.0) | OK | 40-110k | XL | High |
| 4 | Intel 2.5G Ethernet I225/I226 | OpenBSD igc(4) | BSD-2/3 | 9.4k | M | High |
| 5 | Realtek RTL8125/8126 Ethernet | OpenBSD rge(4) (+ firmware) | ISC | 5.3k | M | High |
| 6 | Realtek USB Ethernet RTL8152/8153/8156 | OpenBSD ure(4) | BSD-2 | 4.5k | M | Medium-high |
| 7 | SD/eMMC + Realtek card readers | OpenBSD sdmmc/sdhc + rtsx | ISC | 7.3k | M | Medium-high |
| 8 | ext2/3/4 read-write | lwext4 | BSD-3 + GPL-2.0+ | 17.6k | M | Medium-high |
| 9 | exFAT | FatFs R0.16 | FatFs (GPL-compatible) | 7.2k | S-M | Medium |
| 10 | Gamepads + evdev | FreeBSD hgame/xb360gp/ps4dshock/ps5dsense + evdev; Linux xpad.c (GPL-2.0+) | OK | ~3k | M | Medium |
| 11 | USB serial (CDC-ACM, FTDI, CH340, CP210x, PL2303) | Haiku usb_serial (MIT) / OpenBSD ucom family | OK | 2-3k | M | Medium |
| 12 | usbfs ioctls (libusb programs, fingerprint readers via libfprint) | own, Linux devio.c as reference (GPL-2.0+) | OK | 3k | M-L | Medium |
| 13 | HDA codec quirks (headset mic, mute LEDs) | Linux alc269.c (GPL-2.0+), OpenBSD azalia_codec.c | OK | 1.3k quirk lines | M | Medium |
| 14 | ptrace (strace, gdb) | own | - | - | L | Medium |
| 15 | Bigger FS limits (FS_N 16384, DN_MAX 4096, LX_PATH 256) | own | - | - | M | Medium (Go, node_modules) |
| 16 | Intel Wi-Fi AX200-AX211 | OpenBSD iwx(4) | ISC | 22.7k + 802.11 layer | XL | High |
| 17 | WireGuard | OpenBSD if_wg + crypto | ISC | ~5k | L | Medium |
| 18 | MediaTek MT7921/7922 Wi-Fi | Linux mt76 | BSD-3-Clause-Clear (confirm) | large | XL | Medium-high |
| 19 | pf firewall | OpenBSD pf | BSD-2/ISC | 24.6k | XL | Medium |
| 20 | Qualcomm Wi-Fi 6 / ath10k / Broadcom FullMAC | OpenBSD qwx, Linux ath10k (ISC), OpenBSD bwfm | ISC | 25-85k | XL | Medium |

## Not usable
Linux net/bluetooth (GPL-2.0-only), BTstack (non-commercial clause), Linux mt7601u, rtl8xxxu, igc, r8169, ch341, cp210x, pl2303, dell-laptop, Intel ISH (GPL-2.0-only), OpenBSD if_cdce.c and pfkeyv2 (4-clause advertising).
