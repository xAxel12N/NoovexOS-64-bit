# s5 - Intel Wi-Fi (AX200/AX201/AX210/AX211/BE200) scout

Research only, nothing ported. Checked 2026-11-14. Owner's machine has RTL8821CE, not Intel, so all of this is low priority.

## What we have
- No Intel Wi-Fi code in the tree. A grep for iwx/iwlwifi/0x2723 finds only docs and unrelated third_party noise.
- `docs/PORTING-WORKLIST.md` row 16 already lists "Intel Wi-Fi AX200-AX211, OpenBSD iwx(4), ISC, XL".
- `docs/team-reports/w1/sc10-report.md` already covers iwx in depth: licence, the AX200-AX211 ID list, firmware names, INTx/ICT polling. This report adds only what is new: BE200 and the FreeBSD iwlwifi licence answer.

## Sources

| # | URL / file | Licence (SPDX/header) | Tables / layouts | Size | Use 1-5 |
|---|---|---|---|---|---|
| 1 | github.com/openbsd/src `sys/dev/pci/if_iwx.c` (r1.230), `if_iwxreg.h`, `if_iwxvar.h` | if_iwx.c: ISC ("Permission to use, copy, modify, and distribute...", genua/stsp). if_iwxreg.h: Intel dual BSD/GPLv2, use under BSD | `iwx_dev_info_table[]` at if_iwx.c:11791. 18 `PCI_PRODUCT_INTEL_WL_22500_*` IDs. TLV ucode parser. In if_iwxreg.h: about 151 `IWX_UCODE_TLV_*` defines, about 60 `IWX_*_CMD` IDs, `IWX_NVM_ACCESS_CMD 0x88`, `IWX_NVM_GET_INFO 0x02`, NVM channel flags, CSR/PRPH registers, gen2 context-info/TFH layouts | 12.9k + 9.0k + 0.9k lines | 5 (a complete small driver for AX200-AX211; no BE200) |
| 2 | github.com/openbsd/src `sys/dev/pci/if_iwm.c` | ISC | 7260/8265/9xxx (pre-AX). Older NVM-section layout (`NVM_SW/CALIB/HW sections`) and TLV parsing | 12.2k lines | 2 (wrong generation for AX) |
| 3 | github.com/freebsd/freebsd-src `sys/contrib/dev/iwlwifi/` (the Linux iwlwifi import). Checked: `pcie/drv.c`, `iwl-drv.c`, `fw/file.h`, `fw/img.h`, `fw/api/commands.h`, `fw/api/nvm-reg.h`, `cfg/bz.c`, `pcie/gen1_2/trans.c` | Every file checked carries `SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause`. **So it is dual-licensed: take it under BSD-3-Clause, which is GPLv3-compatible.** (It runs through FreeBSD's LinuxKPI shim, so it is not standalone) | **BE200: `0x272b` -> `iwl_gl_mac_cfg`, BE201/BZ: `0xA840` + subsystem IDs -> `iwl_bz_mac_cfg`** (drv.c:513+), plus the full AX table. `fw/file.h`: `IWL_TLV_UCODE_MAGIC 0x0a4c5749`, `struct iwl_tlv_ucode_header`, 182 `IWL_UCODE_TLV_*` enum values. `fw/api/commands.h`: command groups (LONG 0x1, SYSTEM 0x2, MAC_CONF 0x3, PHY_OPS 0x4, DATA_PATH 0x5, REGULATORY_AND_NVM 0xc...) and 48 `*_CMD = 0x..`. `nvm-reg.h`: NVM_GET_INFO response and regulatory layouts. `cfg/bz.c`: ucode API min/max for BZ/GL | drv.c 1.4k, iwl-drv.c 2.2k, file.h 1.1k, commands.h 0.7k, nvm-reg.h 0.7k | 4 (the only permissive source for BE200 IDs and newer API structs; heavy Linux style) |
| 4 | Linux upstream `drivers/net/wireless/intel/iwlwifi` | Same per-file `GPL-2.0 OR BSD-3-Clause` (sc10 counted SPDX per file; a few non-Intel files are GPL-2.0-only) | Same as #3, newest. BE200 uses the newer `mld/` op-mode | large | 3 (prefer #3's snapshot, or check SPDX file by file) |
| 5 | FreeBSD `sys/dev/iwx/` | ISC (port of OpenBSD iwx) | Same as #1 | about 11k | 3 (duplicate of #1) |

Firmware: linux-firmware `iwlwifi-*.ucode` under LICENCE.iwlwifi_firmware. It may be redistributed but is not open source. iwx uses the same images (cc-a0, Qu/QuZ-hr, so-a0-gf + pnvm, ma-b0, bz-b0). BE200 needs `iwlwifi-gl-*.ucode` + pnvm.

## Top 3
1. **OpenBSD iwx(4)** (#1): the ISC driver to port for AX200/201/210/211, and the source for TLV, command ID, NVM_GET_INFO and context-info layouts.
2. **FreeBSD sys/contrib/dev/iwlwifi** (#3): dual GPL-2.0 OR BSD-3-Clause, so usable under BSD. Take BE200/BZ/GL PCI IDs, `iwl_tlv_ucode_header` + TLV enum, and command-group/NVM structs from it to extend iwx past AX211.
3. **OpenBSD if_iwm.c** (#2): only if pre-AX cards (7260-9560) matter. Same ISC licence and driver shape.

Downloads are in /tmp/s5dl (untrusted, reference only).
