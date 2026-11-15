# sc5 — Wired network scout (v11.06)

## 1. What we already have (do NOT re-propose)
| Area | File | Covers |
|---|---|---|
| Intel e1000 | drivers/net/ethernet/e1000.c (66 l) | 8254x (QEMU) |
| Intel e1000e | e1000e.c (162 l) | 82571-82574/82583, I217/I218/I219 (pch/spt id tables) |
| Intel igb + **igc** | igb.c (135 l) | 82576/82580/I350/I210/I211 + **I225/I226 incl. I226-V** (ids 15F2,15F3,15F7,15F8,15FD,0D9F,125B,125C,125D) |
| Realtek | rtl8139.c, rtl8169.c (90 l) | RTL8139; RTL8111/8168/8169/8136/8167; **Killer E2600 (10EC:2600)** and 2502; **RTL8125/8126 + Killer E3000/E3100 (10EC:3000)** |
| PCnet, vmxnet3, VirtIO-net | nic.c, vmxnet3.c, drivers/virtio/virtio_net.c | VMs |
| USB | drivers/usb/xhci/usb_network.c (+usbnet_proto.h) | **CDC-ECM, CDC-NCM, RNDIS**, hot-plug, behind hubs, `usbnet` cmd. Scans **all configurations (up to 4)** and picks the CDC one |
| Tests | tools/check.py group `nics`, tools/tests/drivers/drvsim.c (igb/igc/I219/RTL8125), tools/tests/usbnet/proto_main.c | |

So I226-V (igc), Killer E2600 and E3000/E3100 are NOT missing. Note ARCHITECTURE.md line 17 (summary table) is stale: still lists only "PCnet, e1000, RTL8139/8169, VirtIO".

Important consequence of the all-config scan: most RTL8152/8153/8156 dongles (config 2 = CDC-ECM/NCM) and the newer ASIX AX88179A/AX88772D (NCM mode) probably already work generically, but slower/without vendor link handling. The original ASIX AX88179 (0B95:1790, vendor-only, no CDC config) does NOT work today.

Quality gap: our RTL8125/8126 path is ~15 lines of special-casing in rtl8169.c (I/O BAR, no MAC-OCP init, no PHY MCU patch, RTL8126 treated as 8125). igc is a minimal init. Neither seen on real hardware (ARCHITECTURE.md line 643).

## 2. Candidates (licenses verified from file headers, fetched 2026-11-14)

### #1 OpenBSD ure(4) — Realtek USB RTL8152/8153/8153B/8156/8156B
- URL: https://github.com/openbsd/src/blob/master/sys/dev/usb/if_ure.c (+ if_urereg.h)
- License: **BSD-2-Clause** (header: Kevin Lo 2015-2019, J. Fletcher 2020; clauses 1-2 only, no advertising clause). v1.39 2026/11/06.
- Size: 3724 + 753 lines.
- Firmware: none for 8152/8153/8156 (PHY/MAC init is in code). Only RTL8157 (5G) / RTL8159 (10G) need `ure-rtl8157/8159` blobs — skip those in v1.
- Maps onto: new `kind 4` in usb_network.c; reuse existing bulk ring setup, usn_ctrl, nvx_nic registration, hot-plug and `usbnet`. Needs vendor control reads/writes (bRequest 0x05, OCP register windows) and ure's RX/TX descriptor framing (8-byte TX hdr, 24-byte RX hdr). Match VID/PID (0BDA:8152/8153/8156/8050 + Lenovo/Microsoft/TP-Link/Dell dock IDs in ure_devs[]) BEFORE the CDC parse and select config 1 (vendor).
- Effort: 6-8 agent-hours (+1 h test stub in tools/tests/usbnet).
- Value: HIGH — the chip in almost every USB-C dongle/dock (Lenovo, Microsoft Surface, Dell, Ugreen 2.5G = RTL8156B). Gives proper 1G/2.5G, link state, and covers RTL8152 and OEM docks without CDC config.
- Cheap pre-step (0.5 h): log which config the CDC path picked for 0BDA:* so owner can test a dongle on v11.06.

### #2 OpenBSD rge(4) — upgrade our RTL8125/8125B/8125D/8126 path (+ Killer E3000/E5000)
- URL: https://github.com/openbsd/src/blob/master/sys/dev/pci/if_rge.c (+ if_rgereg.h)
- License: **ISC** (header "Copyright (c) 2019, 2020, 2023-2026 ... Permission to use, copy, modify, and distribute ...").
- Size: 4698 lines + reg header (~650).
- Firmware: rge loads MAC/PHY MCU patches: linux-firmware rtl_nic/rtl8125a-3.fw, rtl8125b-2.fw, rtl8125d-1/2.fw, rtl8126a-2/3.fw — WHENCE "Driver: r8169", **Licence: Redistributable. See LICENSE.r8169** (WHENCE line 3866). OK to ship.
- Maps onto: replace the is8125 branch in drivers/net/ethernet/rtl8169.c with a separate rtl8125.c (MMIO BAR, MAC-OCP/PHY-OCP init, ephy, PHY MCU, 16-byte descriptors already similar). drvsim.c RTL8125 model exists for tests.
- Effort: 8-10 agent-hours.
- Value: HIGH — RTL8125B is the on-board LAN of most current AMD B550/B650/X670 and many Intel B660-Z790 boards and Killer E3000/E3100 laptops. Current minimal path likely fails link or is unreliable on real chips (untested).

### #3 OpenBSD axen(4) — ASIX AX88179 / AX88178A / AX88179A USB3 gigabit
- URL: https://github.com/openbsd/src/blob/master/sys/dev/usb/if_axen.c (+ if_axenreg.h)
- License: **ISC** (header: Yojiro UO 2013). Alternative/reference: Linux drivers/net/usb/ax88179_178a.c is **GPL-2.0-or-later** (SPDX line) — also allowed, more complete for AX88179A/AX88772D.
- Size: 1512 + 300 lines. No firmware.
- Maps onto: `kind 5` in usb_network.c; vendor reg access (req 0x01/0x02), 4-byte-per-packet RX trailer header, 8-byte TX header. IDs 0B95:1790, 0B95:178A, D-Link DUB-1312, Lenovo, Samsung, Sitecom.
- Effort: 4-5 agent-hours.
- Value: MEDIUM — second most common cheap USB3 gigabit dongle (older Ugreen/Anker/Amazon basics), not covered at all today.

### #4 OpenBSD alc(4) (or Linux alx) — Qualcomm Atheros AR8161/8162/8171/8172 + Killer E2200/E2400/E2500
- URL: https://github.com/openbsd/src/blob/master/sys/dev/pci/if_alc.c (+ if_alcreg.h). Table includes E2200 (1969:E091), E2400 (E0A1), E2500 (E0B1).
- License: **BSD-2-Clause** (Pyun YongHyeon 2009, clauses 1-2). Alternative Linux drivers/net/ethernet/atheros/alx/main.c: **GPL-2.0-or-later + ISC (Qualcomm Atheros notice)** — usable (MODULE_LICENSE("GPL") but header grants "version 2 ... or any later").
- Size: 3376 + 1501 lines (alc); alx ~2054 + hw.c ~1100. No firmware.
- Maps onto: new drivers/net/ethernet/alx.c, nvx_nic probe/send/recv/link like igb.c. nic_names.c already names vendor 1969.
- Effort: 6-8 agent-hours.
- Value: MEDIUM-LOW — gaming laptops/boards ~2013-2018 (MSI, Alienware, Gigabyte), older Lenovo/ASUS AR8161/8171.

### #5 OpenBSD aq(4) — Aquantia/Marvell AQC100/107/108/109/111/112/113/113C/114/115/116
- URL: https://github.com/openbsd/src/blob/master/sys/dev/pci/if_aq_pci.c
- License: **ISC** (J. Matthew, M. Larkin 2021) **+ BSD-2 (aQuantia Corporation 2014-2017)** portion — both allowed.
- Size: 4124 lines, single file. Firmware runs from on-card flash; nothing to load.
- Maps onto: new drivers/net/ethernet/aq.c (MMIO, FW-mailbox for link, descriptor rings).
- Effort: 8-10 agent-hours.
- Value: LOW-MEDIUM — 5G/10G add-in cards (ASUS XG-C100C, TP-Link TX401), high-end ASUS/Gigabyte boards, Mac mini/Pro 10G. Not on owner's known hardware.

### Follow-up (not a new driver): igc hardening reference
OpenBSD igc(4) (dev/pci/if_igc.c 2823 l + igc_*.c, **BSD-2-Clause**, Graziano 2016 / Netgate 2021) — use as reference only if the owner's or a tester's I225/I226-V fails on real hardware (known EEE/link-drop issues on I225-V B1/B2).

## 3. Ranking
1. ure(4) USB Realtek — BSD-2 — 6-8 h — High
2. rge(4) RTL8125/8126 upgrade — ISC + redistributable fw — 8-10 h — High
3. axen(4) AX88179 — ISC (or Linux GPL-2.0+) — 4-5 h — Medium
4. alc(4)/alx Killer E2x00, AR816x/817x — BSD-2 / GPL-2.0+&ISC — 6-8 h — Medium-low
5. aq(4) Aquantia AQC1xx — ISC + BSD-2 — 8-10 h — Low-medium

## REJECTED
| Item | Reason (proof) |
|---|---|
| Broadcom BCM57xx via OpenBSD bge(4) | 4-clause BSD with advertising clause (Bill Paul/Wind River header, clause 3 "All advertising materials...") — GPL-incompatible. FreeBSD if_bge.c has the same clause 3. |
| Broadcom BCM57xx via Linux tg3 | GPL-2.0 only (MODULE_LICENSE("GPL"), no "or later" in header); also needs tg3 firmware. Only route: clean-room from Broadcom's public 57XX programmer's guide. Low value (old Dell/HP business desktops). |
| Linux r8152 (RTL815x USB) | `SPDX-License-Identifier: GPL-2.0-only` — use ure(4). |
| Linux atlantic (Aquantia) | `SPDX-License-Identifier: GPL-2.0-only`, MODULE_LICENSE("GPL v2") — use aq(4). |
| Linux igc | `SPDX-License-Identifier: GPL-2.0` (only) — use OpenBSD igc as reference. |
| OpenBSD if_axe (AX88172/88772 USB2) | contains Bill Paul 4-clause advertising portion. |
| Killer E2600 / E3000 / E3100 as "missing" | Already in rtl8169.c id tables (10EC:2600, 10EC:3000). |
| I226-V as "missing" | Already in igb.c igc_ids (125B/125C/125D). |

## Notes
- Owner's wired NIC is unknown (BRIEF lists only Wi-Fi). If his i5-11400F board is B560/H510 it is likely RTL8111H (covered) or Intel I219-V/I225-V (covered, minimal).
- Sources downloaded for inspection only to /tmp/sc5dl (untrusted, not in repo).
