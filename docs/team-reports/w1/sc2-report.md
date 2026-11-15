# sc2 - Intel Wi-Fi scout (w1)

Sources checked on 2026-11-14: OpenBSD src master (if_iwx.c r1.230 2026/09/19, if_iwxreg.h r1.75, if_iwxvar.h r1.50, if_iwm.c r1.419),
FreeBSD main sys/dev/iwx/if_iwx.c, Linux master 23c6230 (2026-11-14) drivers/net/wireless/intel/iwlwifi (sparse clone, SPDX of every file counted),
linux-firmware WHENCE (gitlab main) + LICENCE.iwlwifi_firmware.

## 1. What NoovexOS already has (do not re-propose)
- No Intel Wi-Fi code at all. Only Wi-Fi driver: `drivers/net/wifi/rtw88/` (8821CE/8822BE/8822CE, ~20k lines incl. tables).
- Intel Wi-Fi is already on the list: `docs/PORTING-WORKLIST.md` row 16 "Intel Wi-Fi AX200-AX211, OpenBSD iwx(4), ISC, XL".
  Earlier scout notes: `docs/team-reports/atlas.md` rows 8-9, `docs/team-reports/vega.md` row 2. This report refines them; it does not add a new idea.
- `net/wifi/` (902 lines): `wifi_mgr.c` (saved networks, auto-connect, DHCP, portal), `wifi_netdev.c` (software CCMP data path, PN replay, A-MSDU check),
  `wpa2.c`, `wpa3.c` (SAE), `eap.c`. Crypto in `lib/crypto/nvxwpa.c` (SHA1/HMAC/PBKDF2, WPA PRF, AES-CCM, key unwrap) - driver-independent and reusable as is.
- IMPORTANT CORRECTION: there is NO real mac80211-like driver interface yet. The stack is hard-wired to rtw88:
  `wifi_mgr.c` has 53 `rtw_*` calls (`rtw_find_mmio`, `rtw_r32/w32`, RCR bit 7 beacon filter at 0x608, ...), `wifi_netdev.c` 14 (reads ring pointers 0x3A8/0x3B4 directly),
  `wpa2.c` 12 (builds the rtw TX descriptor itself). Auth/assoc (`rtw_mgmt_xchg`, `rtw_wpa2_join`, `rtw_assoc`) and even the EAPOL 4-way state machine
  (`wpa_rx_eapol_p`) live in `drivers/net/wifi/rtw88/rtw_sec.c`. Firmware is embedded with `ld -r -b binary` (build.sh:35-37).
- No PCI MSI/MSI-X in the kernel (xHCI is poll-based). iwx supports legacy INTx + ICT table and MSI-X, so a polled CSR_INT/ICT loop is fine.

## 2. Candidates (license proof, size, firmware, mapping)

| # | Candidate | License (proof) | Size | Chips | Firmware |
|---|---|---|---|---|---|
| 1 | OpenBSD iwx(4) `sys/dev/pci/if_iwx.c`, `if_iwxvar.h`, `if_iwxreg.h` | if_iwx.c + if_iwxvar.h: ISC ("Permission to use, copy, modify, and distribute this software for any purpose..."). if_iwxreg.h: "This file is provided under a dual BSD/GPLv2 license" (Intel) - use under BSD | 12,903 + 858 + 8,961 = 22.7k; 276 `ieee80211_*` refs | AX200 (0x2723), AX201 CNVi (0x02f0 0x06f0 0xa0f0 0x34f0 0x3df0 0x43f0 0x4df0), AX210 (0x2725, 0x7e40), AX211 CNVi (0x2726 0x51f0 0x51f1 0x54f0 0x7a70 0x7af0 0x7f70, MTL 0x7740 BZ family). NOT BE200 | iwx-cc-a0-77, iwx-Qu*/QuZ*-hr-b0-77, iwx-so-a0-gf-a0-77 (+pnvm), iwx-ma-b0-gf-a0-89 (+pnvm), iwx-bz-b0-gf-a0-100. Same images as linux-firmware `iwlwifi-<same>.ucode` (all present in WHENCE, e.g. lines 960, 980, 1052, 1158, 1291-1304) |
| 2 | OpenBSD iwm(4) `if_iwm.c`, `if_iwmvar.h`, `if_iwmreg.h` | if_iwm.c/var.h: ISC; if_iwmreg.h: dual BSD/GPLv2 (Intel) | 12,243 + 693 + 6,842 = 19.8k; 287 `ieee80211_*` refs | 3160, 7260, 7265/7265D, 3168, 8260, 8265 (0x24fd), 9260 (0x2526), 9560 CNVi (0x9df0 0xa370 0x31dc) | iwm-7260-17, 7265-17, 7265D-29, 3168-29, 8000C-36, 8265-36, 9260-46, 9000-46 = linux-firmware iwlwifi-7260-17 (WHENCE 658), 8265-36 (690), 9260-th-b0-jf-b0-46 (714), ... |
| 3 | Linux iwlwifi `pcie/` + `fw/` + `mvm/` (+ `cfg/7000..ax210,bz`) | SPDX "GPL-2.0 OR BSD-3-Clause" on 239 of 247 non-dvm/mei files - use under BSD-3. NOTE: MODULE_LICENSE is "GPL" (iwl-drv.c:35, mvm/ops.c:35, mld/mld.c:33), not "Dual BSD/GPL"; per Linux license-rules.rst "GPL" does not pick a version, the per-file SPDX governs | pcie 15.5k, fw 32.1k, mvm 56.4k | everything iwm+iwx cover, 7260 through AX211 | same iwlwifi-*.ucode, newer API numbers (up to -89/-100+) |
| 4 | Linux iwlwifi `mld/` op mode (+ pcie, fw) | SPDX "GPL-2.0 OR BSD-3-Clause" on all 75 files | mld 35.5k + pcie + fw | Selected only when `device_family >= BZ && RF >= FM` (iwl-drv.c:181-185): BE200/BE201 (Wi-Fi 7) and newer. AX2xx stay on mvm | iwlwifi-gl-c0-fm-c0-c10x.ucode + .pnvm (WHENCE 1222-1234) |
| 5 | FreeBSD iwx `sys/dev/iwx/if_iwx.c` | "SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) AND ISC" - usable under BSD-3 AND ISC | 11,214 | port of OpenBSD iwx r1.184 to FreeBSD net80211 (2025, FreeBSD Foundation) | same as iwx |
| - | FreeBSD sys/dev/iwlwifi / sys/contrib/dev/iwlwifi | It is the Linux iwlwifi source run through LinuxKPI: same licenses as row 3/4, nothing new to port | - | - | - |

### REJECTED (GPL-2.0-only)
- Linux `dvm/` (1000-6000 series; 20 of 25 files GPL-2.0-only) and `cfg/1000.c 2000.c 5000.c 6000.c` - old chips anyway.
- `mvm/rs.c`, `mvm/rs.h` (host rate scaling), `mvm/vendor-cmd.c` - GPL-2.0-only. Not needed: use firmware rate scaling (TLC offload, `rs-fw.c` is dual) and rewrite the few `rs.h` types.
- `iwl-debug.h`, `iwl-devtrace*.{c,h}` - GPL-2.0-only. `iwl-debug.h` is included everywhere: if Linux code is used, replace with our own IWL_ERR/IWL_DEBUG macros (~50 lines). (OpenBSD iwx/iwm do not have this problem.)
- `mei/` (CSME/AMT Wi-Fi sharing, all 8 files GPL-2.0-only) - skip, not needed for normal use.
- `tests/`, `mvm/tests/module.c` - GPL-2.0-only, not needed.

### Firmware license (all candidates)
linux-firmware WHENCE "Driver: iwlwifi - Intel Wireless Wifi ... Licence: Redistributable. See LICENCE.iwlwifi_firmware".
LICENCE.iwlwifi_firmware: "Redistribution and use in binary form, without modification, are permitted" with conditions: reproduce copyright + disclaimer
in the documentation, no endorsement, no reverse engineering/decompilation/disassembly; patent grant only when combined with an OSI-licensed OS (GPLv3 qualifies).
=> Shippable. Must ship the licence text with the distro. ~1.0-1.7 MB per .ucode (+~30-60 KB .pnvm for AX210/AX211). Embedding every blob with
`ld -r -b binary` like rtw88 would add ~10 MB to the kernel; load from `/lib/firmware`-style path at `wifi_up` time instead (one blob per machine).

## 3. Which Intel chips matter (estimate - linux-hardware.org did not answer, no hard counts)
1. AX201 (CNVi, 10th/11th-gen Intel laptops 2019-2021) and AX211 (CNVi, 12th-14th gen / Meteor Lake 2022-2024): the bulk of current Intel laptops. -> iwx.
2. AX200 / AX210 (M.2 discrete): very common in AMD laptops and in desktop PCIe add-in cards (the realistic option for a desktop like the owner's). -> iwx.
3. 9560 (CNVi 8th/9th gen), 8265, 7265, 9260: 2015-2019 laptops, still many in use. -> iwm.
4. BE200 (M.2) / BE201 (CNVi, Arrow/Lunar Lake 2024+): newest, still a minority; needs Linux `mld/` -> later phase.
The owner's own PC has an RTL8821CE (no Intel Wi-Fi), so this is value for "common laptops", not for the owner's machine. QEMU cannot emulate any Intel Wi-Fi: every step needs a real Intel card for testing.

## 4. Minimum for scan + WPA2-PSK join (iwx, AX200/AX201/AX210)
Step 0 (prerequisite, own code, ~8-12 agent-h): split `net/wifi` from rtw88 - a `wifi_hw_ops` table
(`up/down, scan_start/scan_poll -> scan_ap[], join_prepare(ap), tx_mgmt(frame), tx_data(frame), rx_poll(cb), set_key(ptk/gtk) or swcrypto flag, set_assoc, bcn_filter`).
Move `rtw_mgmt_xchg`/auth/assoc IE building and `wpa_rx_eapol_p` (4-way + group key) from `rtw_sec.c` into `net/wifi/`. rtw88 becomes the first ops implementation;
host tests in `tools/tests/wifi/` must still pass. Without this, an Intel driver would have to copy the whole join logic.

Then from iwx (keep ~7-9k of the 22.7k lines; drop AMPDU/BA, HT/VHT/HE tuning, background scan, power save, WoWLAN, LED, debug):
1. PCI: BAR0, bus master, `pci_enable_upstream` (exists), ASPM/D0 (exists for rtw). Poll CSR_INT / ICT instead of MSI-X.
2. Transport gen2/gen3: NIC reset/prepare, context-info (22000) or context-info gen3 + PRPH scratch (AX210 SO/MA, BZ), firmware sections + paging into DMA,
   one RX queue (RFH, 512 RBs, completion ring), command queue + TX queues with gen2 TFDs (`iwx_tx`), `IWX_SCD_QUEUE_CONFIG_CMD`.
3. Firmware: .ucode TLV parser (`iwx_read_firmware`), PNVM for AX210/AX211, wait for ALIVE.
4. Init commands: INIT_EXTENDED_CFG, NVM_GET_INFO (MAC address, bands), TX_ANT_CONFIG, PHY_CFG, SOC_CONFIGURATION, MCC_UPDATE (LAR regulatory, needed before 5 GHz scan),
   PHY_CONTEXT add, BINDING, MAC_CONTEXT add, power table (CAM), BT coex basic.
5. Scan: UMAC scan (`iwx_umac_scan_v14`/`v17`, chosen by fw TLV API) with our SSID list; beacons/probe responses arrive as RX_MPDU frames -> feed the existing scan_ap parser.
   That alone gives a working "Wi-Fi networks" list (milestone 1, ~20-25 agent-h incl. transport).
6. Join: PHY context to AP channel, MAC context (BSSID), ADD_STA, SESSION_PROTECTION / time event (stay on channel during auth), TX auth + assoc via TX_CMD on the mgmt queue
   (frames come from the shared `net/wifi` code), TLC_MNG_CONFIG (firmware rate scaling - mandatory, small), set MAC context assoc=1.
7. Keys: either ADD_STA_KEY (hardware CCMP; iwx `iwx_set_key` + `iwx_ccmp_decap` PN check) or software crypto (send already-encrypted frames, firmware passes undecrypted RX up,
   reuse `wifi_netdev.c` CCMP). Hardware keys are the OpenBSD-tested path; recommend hardware keys, software CCMP as fallback.
8. Data: 802.11<->Ethernet conversion already in `wifi_netdev.c`; only the TX submit/RX poll become ops calls.
Milestone 2 (WPA2-PSK join + DHCP): another ~20-30 agent-h. WPA3/SAE: reuse `wpa3.c`, needs MFP (BIP key) ~4-6 agent-h more.

## 5. Ranking
| Rank | Item | Effort (agent-h) | Value | Note |
|---|---|---|---|---|
| 1 | net/wifi driver-ops split (own) | 8-12 | prerequisite for any second Wi-Fi driver (Intel, MediaTek, rtw89, USB) | do first |
| 2 | OpenBSD iwx -> AX200/AX201/AX210/AX211 | 45-60 to scan + WPA2 join | Very high (most current laptops) | ISC + BSD; small, no mac80211 emulation; Linux mvm/pcie (dual) as reference for missing details |
| 3 | OpenBSD iwm -> 7260...9560 | 25-35 after iwx (shared design, gen1 transport + firmware load differ) | High for 2015-2019 laptops | ISC + BSD |
| 4 | Linux iwlwifi mld (+pcie, fw) -> BE200/BE201 | 60+ (needs a mac80211/cfg80211-shaped shim) | Medium now, rising | dual GPL-2.0 OR BSD-3 under BSD; avoid iwl-debug.h |
| 5 | FreeBSD iwx | reference only | - | useful because it is iwx already mapped onto net80211 APIs closer to ours |
| - | Linux mvm as a direct port | not recommended | - | legal (except rs.c/rs.h/debug/devtrace) but 100k+ lines of mac80211 semantics; iwx covers the same chips |

Risks: no Intel card on the owner's PC and no QEMU model - development is blind without a tester with an Intel laptop (or a cheap AX200/AX210 M.2 card in a PCIe adapter
in the owner's desktop, ~150-250 SEK, which would also be the cheapest test rig). CNVi parts (AX201/AX211/9560) only work in Intel laptops whose chipset has the CNVi bridge.
