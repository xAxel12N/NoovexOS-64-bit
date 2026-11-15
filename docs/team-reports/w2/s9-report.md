# s9: platform data layouts (ACPI, SMBIOS, EDID, HDA, microcode)

## What we have now (grep of tree)
- ACPI: our own RSDP/XSDT/MADT plus a small AML walker: drivers/acpi/{acpi_dev,aml,aml_scan,laptop}.c (~1000 lines). No full table-struct header for FADT/MCFG/HPET/DMAR etc.
- SMBIOS/DMI: none. No `_SM_`/`_SM3_` scan was found.
- EDID: boot only. boot/src/vbe.c vbe_edid_native() and the UEFI EDID protocol in boot/uefi/uefiboot.c. The kernel has no EDID/DisplayID parser. The NVIDIA side has its own display_modes.c.
- HDA: generic driver in drivers/sound/hda.c (256 lines; it walks verbs and paths). No verb/param constant header and no per-codec quirk table.
- Microcode: no loader. cpuinfo.c prints `microcode: 0`.

## Sources
| # | URL | Licence (header) | File / content | Size | Use 1-5 |
|---|-----|------------------|----------------|------|---------|
| 1 | github.com/uACPI/uACPI | MIT (LICENSE, "Copyright (c) 2022-2026 Daniil Tatianin"). acpi.h has no SPDX line; the repo LICENSE covers it | include/uacpi/acpi.h: packed structs for RSDP, SDT header, FADT, MADT plus all subtable types, MCFG, HPET, GAS, ECDT, SRAT, BGRT, FACS, and the signature #defines | 1837 lines / 52 KB | 5 |
| 2 | github.com/acpica/acpica | `SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only` (take it under BSD-3) | source/include/actbl.h, actbl1.h, actbl2.h, actbl3.h: every ACPI table and subtable (DMAR, IVRS, NFIT, PPTT, TPM2...) | actbl1.h alone 2637 lines / 76 KB | 4 (most complete; heavier naming) |
| 3 | FreeBSD sys/dev/sound/pci/hda/hda_reg.h | `SPDX-License-Identifier: BSD-2-Clause` | full HDA controller register map, verb IDs, parameter IDs, widget caps, pin config default fields, amp/connection bit layouts | 1365 lines / 59 KB | 5 |
| 4 | FreeBSD sys/dev/sound/pci/hda/hdaa_patches.c | `SPDX-License-Identifier: BSD-2-Clause` | per-codec/subvendor pin-config overrides and Realtek ALC coef/EAPD quirk verb tables | 822 lines / 23 KB | 3 |
| 5 | FreeBSD sys/x86/x86/ucode.c + sys/x86/include/ucode.h | `SPDX-License-Identifier: BSD-2-Clause` | Intel microcode header (48-byte header, extended signature table, checksum rule, WRMSR 0x79 / 0x8B revision read) and an AMD container/patch header loader | 499 + 79 lines | 4 |
| 6 | FreeBSD sys/dev/videomode/edid.c, edidreg.h, ediddevs | BSD-2-Clause text (Itronix 2006; no SPDX line, but the full BSD-2 text is in the header) | EDID 1.3/1.4 block layout macros, detailed timing descriptor decode, vendor PNP-ID table | 642 + 253 + 105 lines | 4 |
| 7 | FreeBSD sys/dev/smbios/smbios.h | `SPDX-License-Identifier: BSD-2-Clause` | SMBIOS 2.x/3.x entry-point structs (`_SM_`, `_SM3_`) and the structure header | 113 lines | 3 (DMI type 0/1/2/4/17 field offsets come from the DMTF DSP0134 spec, a free PDF) |
| ref | Linux drivers/firmware/dmi_scan.c, drivers/gpu/drm/drm_edid.c, drm_displayid.c, sound/pci/hda/patch_realtek.c | GPL-2.0-only (drm_edid is MIT) | facts only; patch_realtek is the best fact list of ALC quirks | - | facts only |

Note: drm_edid.c / drm_displayid.c in Linux carry MIT headers. Check the header before taking code from them, since they are the best DisplayID parser available. I did not have time to verify this.

## Top 3
1. uACPI include/uacpi/acpi.h (MIT): a single drop-in header of ACPI table structs that fills out our hand-rolled ACPI code.
2. FreeBSD hda_reg.h (BSD-2): all HDA verb, parameter and pin-config constants for drivers/sound/hda.c. Add hdaa_patches.c later for quirks.
3. FreeBSD ucode.c/ucode.h (BSD-2): Intel and AMD microcode header layout and the load sequence. Follow with FreeBSD edid.c for a kernel EDID parser and smbios.h + DSP0134 for DMI.
