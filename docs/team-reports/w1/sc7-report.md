# Scout sc7 — audio + graphics (v11.06)

## 1. What NoovexOS already has (do NOT propose again)

Audio
- `drivers/sound/hda.c` (256 dense lines, 17 KB): HD Audio controller + codec probe (class 0403 any vendor; class 0401 Intel DSP-class only after HDA version check), 64-bit BARs, one codec, up to 4 output paths (speaker / headphones / line-out, chosen by pin default-config device 1/2/0), EAPD on, volume, capture with headset-mic-over-internal-mic choice (`pick_in`), **headphone jack detection already exists** (`hda_jack`, polled every 300 ms, mutes speaker pins; untested on real hardware), dead-codec back-off.
- `drivers/sound/ac97.c` (53 lines).
- USB Audio Class 1 + 2 playback and capture (16/24/32-bit, up to 8 ch mics, UAC2 clock rate), in `drivers/usb/xhci.c` / `uvc_webcam.c` (`aud_*`). Missing: several USB audio devices at once, UAC2 feedback endpoints.
- `core/sound.c` mixer, ALSA kernel interface for Linux programs (`/dev/snd/*`, 9 streams mixed in-kernel).

Graphics
- Firmware framebuffer, EDID parser (`drivers/video/fb/edid.c`), gfx/damage/gq queue.
- Intel iGPU `drivers/video/intel/igpu.c` (435 dense lines, 51 KB): Sandy Bridge..Meteor Lake detection, reads PIPESRC/transcoder/eDP timings/scaler (reports native vs GOP mode), WC mapping via PAT, tear-free page flip gen 8-12, gen9 2D blitter, render engine + own shader assembler (`gen9asm.c`, `gpukern.c`), GPU clock. **No mode setting** (no DPLL/DDI/link training), no HDMI hotplug, no second screen.
- NVIDIA Turing via GSP (`drivers/video/nvidia/gsp/*`, nvgsp, opt-in): boot, 2D/compute, 3D (TURING_A triangles), monitor list (EDID over GSP), `nvgsp mode` second HDMI/DVI monitor mirror with native mode. Missing: page flips, DisplayPort link training, extended desktop.
- AMD: detection/naming only (`drivers/video/gpuid.c`: "no AMD driver yet").
- Mesa llvmpipe for Linux programs via apt (`firefox-webgl`), not in ISO. Own WebGL 1 software rasterizer in the web engine.

## 2. Gaps found in our own code (cheap, no port needed — relevant to the ranking)

| Gap | Where | Effect |
|---|---|---|
| Hard cap 1920x1080 | `include/nvx/drivers/video/fb/framebuffer.h` `MAXW 1920`/`MAXH`, `boot/uefi/uefiboot.c:81-82` `MAX_W/MAX_H` | UEFI loader already picks the largest GOP mode, but never above 1080p. Every 1440p/1600p/4K laptop panel or monitor runs non-native (scaled) **regardless of any GPU driver**. Static arrays `SCENE_STATIC`/`BG_STATIC` are sized MAXW*MAXH. This is the #1 native-resolution fix and is pure own work (switch to runtime-allocated buffers). Effort ~6-10 agent-h. |
| HDMI/DP audio pins never routed | `hda.c` scan: path passes want dev 1,2,0 only; dev 5 (Digital Other Out = HDMI/DP) and 4 (SPDIF) skipped; no ELD, no `0x2D`/ConvChannel/infoframe | HDMI-only codecs log "no usable output path" and are dropped. |
| Only the first controller with a usable codec is used | `hda_probe` stops at first found | GPU HDMI audio function (NVIDIA 01:00.1, AMD 1002:xxxx, Intel display codec 8086:28xx on same controller as a 2nd codec) never becomes a second sound device. |
| No subsystem-ID quirks / GPIO amp enable | `hda.c` has no quirk table, no GPIO verbs (0x715-0x717) | Classic "laptop speakers silent" on HP (GPIO/amp) and wrong pin configs on BIOSes with bad default configs. |

## 3. Candidates (license verified from the actual file headers, Linux master + FreeBSD main, fetched 2026-11-14)

### C1. HDMI/DP audio — FreeBSD `snd_hda` HDMI path + i915 `intel_audio.c` + nouveau `r535` ELD
- Sources:
  - FreeBSD `sys/dev/sound/pci/hda/hdaa.c` (7281 lines; HDMI/DP pin handling, ELD parse, channel/coding types), `hdac.c` (2218, controller incl. NVIDIA/AMD/Intel device IDs). Header `SPDX-License-Identifier: BSD-2-Clause` (verified). https://github.com/freebsd/freebsd-src/tree/main/sys/dev/sound/pci/hda
  - Intel display side: Linux `drivers/gpu/drm/i915/display/intel_audio.c` (1594 lines) — MIT permission header (verified); `hsw_audio_codec_enable` writes ELD to `AUD_EDID_DATA`, sets `HSW_AUD_PIN_ELD_CP_VLD` ELD-valid/output-enable, `AUD_CONFIG` N/M. Module is "GPL and additional rights" = MIT files usable.
  - NVIDIA side: Linux `drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/disp.c` (1769 lines, MIT header verified): `r535_sor_hda_eld` (NV0073_CTRL_CMD_DFP_SET_ELD_AUDIO_CAPS) and `r535_sor_hda_hpd` — two small GSP controls, fits our existing nvgsp display client in `display_modes.c`.
  - Linux `sound/hda/codecs/hdmi/hdmi.c` (2378) is GPL-2.0-or-later (verified) — usable as reference too. But `sound/hda/core/hdmi_chmap.c` and `core/controller.c` are **GPL-2.0-only → REJECTED**. Linux `drivers/pci/quirks.c` (NVIDIA HDA function enable, cfg 0x488 bit 25) is GPL-2.0 only → do not copy code; the single register fact can be implemented independently.
- Firmware: none.
- Size to port: ~1.0-1.5k lines (HDMI subset of hdaa + ~150 lines i915 audio regs + ~60 lines nvgsp control); our EDID parser already exists (need its CEA extension SAD block for ELD).
- Maps onto: `hda.c` (accept dev 5 pins, set converter channel count/stream format, digital converter enable 0x70D, multi-controller → second `nvx_snd` device chosen in Settings > Sound), `igpu.c` (MMIO already mapped), `nvgsp` display client.
- Effort: 12-18 agent-h (Intel + NVIDIA); codec side alone 6-8 h.
- Value: HIGH. Owner's desktop (GTX 1650, two HDMI monitors) — HDMI audio needs the NVIDIA function exposed + ELD via GSP (so only when `nvgsp all` runs). Every Intel laptop/desktop driving a TV/monitor over HDMI. AMD needs amdgpu DC for ELD → not covered.

### C2. HDA codec + controller quirks — FreeBSD `hdaa_patches.c` / `hdac.h` (BSD-2) + Linux `alc269.c` as data (GPL-2.0-or-later)
- FreeBSD `hdaa_patches.c` 822 lines, `SPDX BSD-2-Clause` (verified): pin-config overrides per subsystem ID, GPIO/EAPD patches, HP/Lenovo/ASUS/Dell entries.
- Linux `sound/hda/codecs/realtek/alc269.c` (9789 lines) `SPDX GPL-2.0-or-later` (verified, OK under GPLv3) — the richest quirk table (headset mic fixups, mute/micmute LEDs via GPIO/COEF, HP amp GPIO). `sound/hda/common/jack.c` GPL-2.0-or-later.
- OpenBSD `azalia_codec.c` (already listed in PORTING-WORKLIST #13, still not done).
- Firmware: none. Size: ~1-1.5k lines (table + verb applier). Effort: 6-10 agent-h.
- Value: MEDIUM-HIGH for laptops (owner has HP, Lenovo, ASUS laptops): silent speakers, missing headset mic, mute LED. Note jack detection itself already exists — this is the quirk layer that makes it correct on misconfigured BIOS pin tables. Duplicate of worklist #13: merge.

### C3. Intel i915 display subset — mode setting for gen 9-12 (MIT)
- Linux `drivers/gpu/drm/i915/display/*` — files carry the MIT permission header (verified: `intel_dp_link_training.c` 2848, `intel_dpll_mgr.c` 5404, `intel_hdmi.c` 3443, `intel_audio.c` 1594; `i915_module.c` `SPDX MIT`, MODULE_LICENSE "GPL and additional rights"). Helpers `drm_edid.c` (MIT), `drm_dp_helper.c` (HPND-style "Permission to use, copy, modify..." — compatible).
- Firmware: DMC (display microcontroller, i915/*_dmc.bin, linux-firmware LICENSE.i915 redistributable) only needed for DC states/power saving, not for basic modeset.
- Whole i915 display dir is ~130k lines; a useful subset (DDI enable, combo/TC PHY on gen 11/12, DPLL compute, HDMI + DP link training, pipe/plane, hotplug via GMBUS/AUX EDID) ~12-18k lines to adapt.
- Maps onto `igpu.c` (already reads pipe/transcoder/plane/scaler, has GGTT + flip).
- Effort: XL, 50-80 agent-h, needs real Intel hardware testing (QEMU has no Intel GPU).
- Value: MEDIUM. On UEFI the GOP already lights the eDP panel at native (≤1080p, see gap above). Gains: external monitor hotplug, second screen, refresh-rate choice. Do the MAXW/MAXH lift first; it delivers most of the "native resolution on laptops" value for ~1/8 the effort.

### C4. Intel SOF (Sound Open Firmware) host driver + firmware
- Linux `sound/soc/sof/core.c`, `sound/soc/sof/intel/hda.c`: `SPDX (GPL-2.0-only OR BSD-3-Clause)`, `MODULE_LICENSE("Dual BSD/GPL")` (verified) → usable under BSD-3.
- Firmware: sof-bin, signed Intel binaries; `LICENCE.Intel` = BSD-3 + limited patent grant, redistributable (verified). SOF firmware source BSD-3.
- Size: host side ~30-40k lines (IPC3/IPC4, topology parser, HDA DMA code loader, plus SoundWire for SDW-codec laptops, DMIC). Needs topology files (sof-tplg, BSD-3).
- Effort: XL, 80+ agent-h.
- Value: LOW-MEDIUM now. Only needed on Intel laptops whose internal mic is a DMIC array (most Tiger Lake+ ultrabooks) or whose codec is SoundWire-only; speakers/headphones on most such laptops still work through legacy HDA (which we have). Defer.

### C5. nouveau display for pre-Turing NVIDIA (MIT)
- `drivers/gpu/drm/nouveau/dispnv50/disp.c` (3076) MIT header verified; nouveau module "GPL and additional rights". Display on Kepler/Maxwell/Pascal needs no signed firmware.
- Size ~15-25k lines (nvkm disp + devinit + i2c/aux). Effort L-XL 40-60 h.
- Value: LOW for owner (Turing, already handled by our GSP path); medium for the general population of older GTX cards.

### C6. AMD amdgpu DC (MIT) — not now
- `drivers/gpu/drm/amd/display/dc/core/dc.c` (8404) MIT header; `amdgpu_dm/amdgpu_dm.c` `SPDX MIT` (verified).
- Firmware: DMCUB/PSP/SMU blobs, linux-firmware `LICENSE.amdgpu` (redistributable).
- Size: display dir alone several hundred thousand lines plus generated register headers; requires amdgpu base bring-up (GMC, PSP to load DMCUB, SMU). Effort: 200+ agent-h.
- Value: owner's HP laptop has an AMD GPU, but its GOP already gives native eDP (≤1080p); AMD HDMI audio would need DC for ELD. Park; revisit only as a GSP-like long project.

### C7. Mesa softpipe/llvmpipe/lavapipe natively (MIT; LLVM Apache-2.0 w/ exception, compatible)
- Mesa core MIT (docs/license.rst verified). Already available to Linux programs via apt (~190 MB with LLVM); native WebGL already has our own rasterizer. llvmpipe-lite inside the native desktop gives nothing a daily user sees. Value LOW — not recommended.

## 4. Ranking for daily-use value (native res on iGPU laptops, HDMI audio, headphone jack)

| Rank | Item | License | Effort | Value |
|---|---|---|---|---|
| 0 (own, do first) | Lift 1920x1080 cap in loader + framebuffer (runtime buffers) | own | 6-10 h | HIGH — native res on every >1080p screen, no driver needed |
| 1 | C1 HDMI/DP audio (FreeBSD hdaa HDMI + i915 intel_audio.c + nouveau r535 ELD) + multi-controller HDA | BSD-2 / MIT / MIT | 12-18 h | HIGH (owner's HDMI monitors, any HDMI TV) |
| 2 | C2 HDA quirks (FreeBSD hdaa_patches BSD-2; Linux alc269.c GPL-2.0+) | OK | 6-10 h | MED-HIGH (HP/Lenovo/ASUS laptops; jack detect already present) |
| 3 | C3 i915 modeset subset gen 9-12 | MIT (+DMC fw LICENSE.i915 optional) | 50-80 h | MEDIUM |
| 4 | C4 Intel SOF | Dual BSD/GPL → BSD-3; fw BSD-3/Intel redistributable | 80+ h | LOW-MED (DMIC laptops) |
| 5 | C5 nouveau pre-Turing display | MIT | 40-60 h | LOW (owner) |
| 6 | C6 amdgpu DC | MIT; fw LICENSE.amdgpu | 200+ h | LOW-MED, parked |
| 7 | C7 Mesa native | MIT | - | LOW, skip |

REJECTED: Linux `sound/hda/core/controller.c`, `sound/hda/core/hdmi_chmap.c` (GPL-2.0-only); Linux `drivers/pci/quirks.c` (GPL-2.0-only; NVIDIA HDA-enable bit to be reimplemented from the register fact, not copied).

Note: `docs/architecture-src/10-devices.md:466` still says HDA has "no jack detection" — stale since v10.74 (`hda_jack`).
