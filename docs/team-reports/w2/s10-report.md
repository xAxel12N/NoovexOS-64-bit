# s10: GPU data tables (GTX 1650 TU117) - wave-2 scout

## What we already have (grep of the tree)
- drivers/video/gpuid.c: NVIDIA exact IDs (incl. 0x1F82 GTX 1650 TU117, 0x1F91/1F95/1F99 mobile), Turing/Ampere/Ada/Pascal family ranges, AMD exact + family table (~27 IDs). Intel: vendor-only, no device-ID table.
- drivers/video/nvidia/nvgpu.c: PMC_BOOT_0/42, PROM read, BIT signature scan, DCB 4.x signature (0x4EDCBDCB) find.
- drivers/video/nvidia/gsp/*: a working GSP-RM path (firmware unpack, WPR meta, cmd/msg queues, RPC header 'VRPC', RPCs 10/47/65/70/76/103, display modes, 2D/3D). ~2.6k lines total.
- drivers/video/intel/igpu.c: gen 9 display regs (pipes, eDP transcoder) - gen from config, not an ID table.
- Note: owner's i5-11400F has NO iGPU ("F"), so Intel IDs matter only for other machines.

## Sources (licences verified from file headers via raw.githubusercontent)
| # | URL / file | Licence (SPDX line) | Content | Size | Use 1-5 |
|---|---|---|---|---|---|
| 1 | github.com/NVIDIA/open-gpu-kernel-modules src/nvidia/generated/g_rpc-structures.h | "SPDX-License-Identifier: MIT" | All GSP RPC payload structs (rpc_alloc_memory, gsp_rm_control, update_bar_pde, os_error_log, run_cpu_sequencer...). We use only 6 RPCs; this is the authoritative layout for the rest, plus g_rpc-message-header / rpc_global_enums (function numbers) | 13,115 lines | 5 |
| 2 | same repo src/nvidia/inc/kernel/gpu/gsp/gsp_static_config.h + gsp_init_args.h | MIT | GspStaticConfigInfo (reply of GET_GSP_STATIC_INFO, fn 65), GSP_ARGUMENTS_CACHED / message-queue init args | 275 + 123 lines | 4 |
| 3 | same repo src/nvidia/arch/nvalloc/common/inc/rmgspseq.h + gsp/gspifpub.h + gsp/gsp_fw_wpr_meta.h | MIT | GSP sequencer opcodes (REG_WRITE/MODIFY/POLL/DELAY/CORE_RESET/RESUME) needed for RUN_CPU_SEQUENCER on suspend/resume and Turing booter; WPR meta layout (we already use magic) | 66 + 222 + 220 lines | 4 |
| 4 | same repo src/common/inc/swref/published/turing/tu102/dev_*.h (dev_bus, dev_fb, dev_gsp, dev_falcon_v4, ...) and disp/v04_02/dev_disp.h | MIT | Turing register field defines (NV_PBUS, NV_PFB, NV_PGSP, falcon v4 regs). TU117 uses tu102 refs. Replace magic numbers in nvflcn.c/nvmem.c | ~30-100 lines each, dozens of files | 4 |
| 5 | git.kernel.org linux drivers/gpu/drm/nouveau/include/nvkm/subdev/bios/{bit,dcb,init,pll,perf,vmap}.h + nvkm/subdev/bios/*.c | headers "SPDX-License-Identifier: MIT"; .c files MIT permission text | VBIOS BIT token layout, DCB entry/connector/I2C/GPIO tables, devinit script opcodes, PLL limits, perf tables - the best open description of VBIOS tables | ~10-80 lines per header, ~40 .c files | 4 |
| 6 | same repo src/nvidia/inc/kernel/platform/nbsi/nbsi_table.h | MIT | NBSI/ACPI-embedded VBIOS & OEM tables (laptops); low value for a desktop 1650 | 382 lines | 2 |
| 7 | linux include/drm/intel/pciids.h | MIT permission text (Copyright 2013 Intel) | Full Intel display device-ID table by platform (RKL 0x4C80/8A/8B/8C/90/9A, TGL, ADL, ...) ~415 IDs | ~800 lines | 3 (fills gpuid.c Intel gap; owner has no iGPU) |
| 8 | linux drivers/gpu/drm/amd/amdgpu/amdgpu_drv.c pciidlist | MIT permission text | Complete AMD PCI-ID -> ASIC family table (hundreds) vs our ~27 | large | 2 |
| 9 | github.com/NVIDIA/open-gpu-doc (BIOS-Information-Table, DCB, Devinit docs) | not verified (repo API blocked this session; path guess 404) | NVIDIA's own BIT/DCB/devinit spec docs | n/a | 3, verify licence first |

All of 1-8 are MIT: GPL-3.0-compatible, usable as code with notice preserved.

## Top 3
1. g_rpc-structures.h + rpc enums (MIT) - authoritative layouts for every GSP RPC we don't yet speak; biggest gap for growing the GSP driver.
2. rmgspseq.h + gsp_static_config.h (MIT) - sequencer opcodes (needed for suspend/resume/reset) and the static-info reply struct we currently parse ad hoc.
3. nouveau nvkm bios headers (MIT) - DCB/I2C/GPIO/connector and devinit tables to go beyond "find DCB signature" (connector enumeration, EDID bus selection without GSP).
