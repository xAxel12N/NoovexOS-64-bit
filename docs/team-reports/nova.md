# Nova: storage, input, sound, platform and sensor drivers to port

Scout: Nova (read only). Date: 2026-11-14.
NoovexOS tree read at /home/claude/src/NoovexOS-64-bit-main. Note: `drivers/usb/xhci_parts/` was renamed to `drivers/usb/xhci/` while I was working (controller_init.c, hid_keyboard_mouse.c, storage_fat_and_hubs.c, uvc_webcam.c, usb_network.c). The paths below use the new names.

Sources checked. The SPDX line or license header of every file listed was read with `git show HEAD:<file>`:
- Linux 7.4-rc2 at /tmp/ref/linux (commit 7e1392a4). It is a blob:none partial clone, so blobs were fetched on demand. One call started a background `git gc` auto-pack there; after that I ran with `-c gc.auto=0`.
- OpenBSD /tmp/ref/openbsd (shared clone).
- FreeBSD /tmp/ref/freebsd (shared clone). Another agent deleted it partway through; the FreeBSD data below was read before that.
- Haiku: new metadata-only clone at /tmp/ref/haiku.
- libfprint: /tmp/ref/libfprint.
- sof-bin: /tmp/ref/sof-bin.

License key: "OK" means GPLv3-compatible under CONTEXT.md. Linux `GPL-2.0` (the old SPDX form) means GPL-2.0-only, so it is NOT OK.

LOC means `wc -l` of the main files named; headers are left out unless stated.

## Ranked table

| # | Item | Best source to port | License | LOC (main files) | Needs in NoovexOS | Effort | Value | Notes |
|---|---|---|---|---|---|---|---|---|
| 1 | HD Audio probe fixes: try every controller and codec, accept Intel DSP-class controllers in legacy mode, allow BARs above 4 GB | Own code. Linux `sound/hda/core/intel-dsp-config.c` as a logic reference only | The intel-dsp-config.c reference is GPL-2.0-only (NOT OK, ideas only). Haiku `hda_controller.cpp` is MIT (OK) | about 50 to 150 changed lines in hda.c | drivers/sound/hda.c, sys/native/kapi.c (`kapi_pci_find_class`), core/sound.c | S | High | Verified gaps, listed under item 1 below. Likely cause of "no sound" on AMD laptops and on Intel laptops whose DSP is enabled |
| 2 | HDA codec quirks: pin fixups, EAPD/GPIO/COEF, headset mic, mute LEDs | Linux `sound/hda/codecs/realtek/alc269.c` + `realtek.c` (1,275 `SND_PCI_QUIRK` lines). Also FreeBSD `hdaa_patches.c`, OpenBSD `azalia_codec.c`, Haiku `hda_codec.cpp` | Linux GPL-2.0-or-later (OK). FreeBSD BSD-2 (OK). OpenBSD 2-clause, no advertising clause (OK). Haiku MIT (OK) | 9,796 + 2,263 (Linux). 822 (FreeBSD). 2,788 (OpenBSD). 1,619 (Haiku) | hda.c: needs a per-codec fixup hook (vendor ID + PCI subsystem ID) applied between `scan()` and `route()` | M | High | Port the table data, not the ALSA framework. Do Realtek ALC2xx first |
| 3 | SD/eMMC host (SDHCI, PCI class 08/05, plus ACPI PNP0D40/INT33BB/AMDI0040) | OpenBSD `sys/dev/sdmmc/{sdhc,sdmmc,sdmmc_mem}.c` + `sys/dev/pci/sdhc_pci.c` + `sys/dev/acpi/sdhc_acpi.c`. Alternatives: Haiku sdhci (MIT), FreeBSD sdhci + mmc (BSD-2) | ISC (OK). Linux `sdhci.c` and `sdhci-pci-core.c` are GPL-2.0-or-later (OK), but Linux mmc core `core.c`/`sd.c`/`mmc.c`/`bus.c`/`host.c`/`block.c` are GPL-2.0-only (NOT OK) | OpenBSD: 1,440 + 872 + 1,364 + 432 + 258 = 4,366. Haiku: 1,011 + 332 + 244 + 729 + 404 = 2,720 | PCI, DMA (`kapi_dma_pages`), an IRQ or polling, block layer (new `BK_SD` in fs/vfs/parts.c `bd_read`), /dev node in sys/linux/syscalls/sysfs.c | M | High | OpenBSD sdmmc_mem handles eMMC (EXT_CSD, DDR52/HS200 timing table) as well as SD. Gives internal eMMC disks and SD slots |
| 4 | Realtek RTSX PCIe card reader (10ec:52xx) | OpenBSD `sys/dev/ic/rtsx.c` + `sys/dev/pci/rtsx_pci.c` on top of item 3's sdmmc. For newer chips: Linux `drivers/misc/cardreader/rtsx_pcr.c` + `rts5xxx.c` + `drivers/mmc/host/rtsx_pci_sdmmc.c` | OpenBSD ISC (OK). FreeBSD `sys/dev/rtsx/rtsx.c` BSD-2 (OK). Linux rtsx PCI files GPL-2.0-or-later (OK). Linux rtsx_usb is GPL-2.0-only (NOT OK) | OpenBSD 1,474 + 140. FreeBSD 3,915. Linux 1,837 + 1,570 + about 500 to 970 per chip file | Item 3's sdmmc layer, PCI, DMA | M (after #3) | Med-High | Chip coverage: OpenBSD 5209/5227/5229/522A/5249/525A/8402/8411/8411B. FreeBSD adds 5260. Linux adds 5228/5261/5264/5286/5287/5289/524A |
| 5 | Generic USB HID gamepad + evdev/joystick ABI (/dev/input/eventN, jsN) | Own hid_parse extension. Mapping from FreeBSD `sys/dev/hid/hgame.c` + `hidmap.c`. Event-device model from FreeBSD `sys/dev/evdev/{evdev,cdev}.c`. ABI from Linux `include/uapi/linux/input.h`, `joystick.h` | FreeBSD BSD-2 (OK, no advertising clause). Linux uapi headers carry "WITH Linux-syscall-note" (OK for ABI). Linux `evdev.c` GPL-2.0-only (NOT OK). Linux `joydev.c` GPL-2.0-or-later (OK) | 230 + 834; 1,176 + 952 | drivers/usb/xhci/controller_init.c `hid_parse` (only knows keyboard, mouse and consumer collections; usage 0x10004/0x10005 maps to kind 0 and is ignored); drivers/input/input.h (mouse-only today); sysfs.c `/dev/input` (lists only `mice`) | M | High | Also lets the web engine's `getGamepads()` stub (web/prelude_gen.c, returns nulls) return real pads. DualShock 4 and DualSense are HID class, so the generic path covers basic input. FreeBSD ps4dshock.c (1,406) and ps5dsense.c (472) add LEDs and rumble |
| 6 | Xbox 360 / One / Series controllers (xpad) | Linux `drivers/input/joystick/xpad.c` | GPL-2.0-or-later (OK) | 2,334 (313 VID/PID entries) | Item 5, xHCI interrupt IN/OUT on a vendor-class (0xFF) interface | S-M (after #5) | High | Only Linux handles Xbox One/Series (GIP init packets). FreeBSD `xb360gp.c` (180, BSD-2) handles 360 only, via a fake HID descriptor in `hidrdesc.h` |
| 7 | USB serial: CDC-ACM, FTDI, CH340/341, CP210x, PL2303 | Haiku `src/add-ons/kernel/drivers/ports/usb_serial/*` (ACM, FTDI, Silicon, WinChipHead, Prolific, SerialDevice, Driver) | Haiku MIT (OK). OpenBSD uftdi/uchcom/umodem/ucom (2-clause, OK) and uslcom (ISC, OK). FreeBSD uftdi/uchcom/umodem/uplcom (BSD-2, OK) and uslcom (ISC, OK). Linux cdc-acm and ftdi_sio GPL-2.0+ (OK); Linux ch341, cp210x, pl2303 and usb-serial core GPL-2.0-only (NOT OK) | Haiku: 160 + 296 + 138 + 251 + 143 + 805 + 384 = 2,177 | xHCI bulk + interrupt endpoints (pattern: usb_network.c `usn_xfer`/`usn_arm`); a new tty vfile modelled on the pty code in sys/linux/syscalls/fifos_and_names.c (TCGETS/TCSETS mapped to SET_LINE_CODING); /dev/ttyUSBn and ttyACMn in sysfs.c | M | Medium | For Arduino, ESP32 and microcontroller work, and for picocom/screen/minicom from apt |
| 8 | usbfs: USBDEVFS ioctls on /dev/bus/usb/BBB/DDD | Linux `drivers/usb/core/devio.c`. ABI: `include/uapi/linux/usbdevice_fs.h` | devio.c GPL-2.0+ (OK). uapi header GPL-2.0+ WITH syscall-note | 2,933 (reference; NoovexOS needs a subset: CONTROL, BULK, SUBMITURB/REAPURB, CLAIMINTERFACE, SETINTERFACE) | sysfs.c already creates the /dev/bus/usb nodes, but `dx_ioctl` handles only block ioctls and returns ENOTTY otherwise; xHCI needs a generic "raw endpoint" API | M-L | Med-High | Lets apt-installed libusb programs run unchanged: libfprint/fprintd, SANE scanners, avrdude, dfu-util, ipp-usb. Fingerprint support (#10) depends on it |
| 9 | HP laptop hotkeys, keyboard backlight, mic-mute (hp-wmi) + WMI core | Linux `drivers/platform/x86/hp/hp-wmi.c` + `drivers/platform/wmi/core.c` | Both GPL-2.0-or-later (OK) | 3,334 + 1,637 | drivers/acpi/laptop.c (already does ThinkPad IBM0068/LEN0068/LEN0268, IdeaPad VPC2004, ASUS ATK0100/0101 via WMNB; no HP), AML evaluator in drivers/acpi/aml.c | S-M | Medium | Linux `intel/hid.c` (836) and `intel/vbtn.c` (397), both GPL-2.0+ (OK), add INT33D5/INT33D6 buttons on modern Intel laptops. No NoovexOS code references INT33D5/INT33D6 today |
| 10 | Fingerprint readers | libfprint (userspace) + fprintd from Debian, running on item 8 | LGPL-2.1 (OK). Depends on glib >= 2.68, gusb, gio | n/a (no kernel driver; the readers are USB devices driven from userspace) | Item 8 (usbfs), D-Bus, PAM integration for login | L (mostly #8 plus integration) | Medium | libfprint drivers: synaptics, goodixmoc, elanmoc, egismoc, fpcmoc, focaltech_moc, realtek, and older aes/vfs/upek |
| 11 | Intel SOF (digital mics and SoundWire on Intel laptops) | Linux `sound/soc/sof/` (core.c, ipc4.c, intel/hda.c, intel/hda-dsp.c, intel/pci-tgl.c) + `drivers/soundwire/{bus,intel}.c`. Firmware: sof-bin | SOF: "(GPL-2.0-only OR BSD-3-Clause)" (OK under BSD-3). SoundWire: "(GPL-2.0 OR BSD-3-Clause)" (OK). Codec and machine drivers such as `rt711-sdca.c`, `soc_sdw_utils.c`, `dmic.c` and the Intel `avs` driver are GPL-2.0-only (NOT OK). sof-bin firmware: BSD-3 sources + Intel limited patent licence (LICENCE.Intel); can be shipped as separate files | 848 + 1,049 + 1,796 + 1,631 + 324 + 2,120 + 1,096, plus topology parsing | hda.c (stream DMA is shared with HDA), firmware loader, NHLT ACPI table parsing, topology parser | XL | Medium | Linux needs SOF only if DMICs (NHLT) or SoundWire are present (`FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE`). Otherwise legacy HDA works, so item 1 covers speakers and headphones on most of these machines |
| 12 | AMD GPU display only (native modeset, second monitor) | Linux `drivers/gpu/drm/amd/display/dc` (DC) + `amdgpu/atom.c` and `atombios_*` | MIT (OK): amdgpu_device.c, atom.c and the DC files carry the MIT permission text; amdgpu_dm.c and dmub_srv.c say `SPDX: MIT` | DC alone: 460,931 lines of .c in 1,148 .c/.h files | PCI BAR/MMIO, EDID (drivers/video/fb/edid.c), compositor back end. Newer DCN parts load DMUB firmware (dmub_srv.c) | XL | Medium | Today drivers/video/gpuid.c says "no AMD driver yet, screen through the firmware framebuffer", so AMD machines already get a picture. Backlight already works through ACPI `_BCM` (drivers/acpi/acpi_dev.c) |
| 13 | ACPI ambient light sensor (ACPI0008) | OpenBSD `sys/dev/acpi/acpials.c` | ISC (OK). Linux `drivers/iio/light/acpi-als.c` GPL-2.0-or-later (OK) | 191 / 261 | drivers/acpi/acpi_dev.c device scan, AML `_ALI` eval | S | Low | Few laptops expose ACPI0008. Most route ALS through ISH or AMD SFH |
| 14 | AMD Sensor Fusion Hub (accelerometer, ALS, lid angle on AMD laptops) | Linux `drivers/hid/amd-sfh-hid/*` | GPL-2.0-or-later, all files (OK) | about 4,040 including headers (pcie 579, client 401, sfh1_1 init 430, descriptor tables 772) | PCI MMIO mailbox, feeding synthetic HID reports into the existing HID parser | M | Low | The HID sensor-usage consumers (`hid-sensor-hub.c`, `hid-sensor-accel-3d.c`, `hid-sensor-als.c`) are GPL-2.0-only, so the sensor-usage layer must be written from scratch |
| 15 | USB printers (class 7) | FreeBSD `sys/dev/usb/serial/ulpt.c` / OpenBSD `sys/dev/usb/ulpt.c` | BSD-2 / 2-clause (OK). Linux `usblp.c` GPL-2.0+ (OK) | 765 / 736 / 1,488 | xHCI bulk; storage_fat_and_hubs.c already marks class-7 devices as "Printer" (`prn` flag) but nothing drives them; there is no print spooler (no CUPS) in the tree | S kernel, L for a full printing stack | Low | IPP-over-USB printers could go through ipp-usb (libusb, so needs item 8) |
| -- | Intel ISH sensors | Linux `drivers/hid/intel-ish-hid/*` | GPL-2.0-only (NOT OK) | 1,034 + 1,274 | -- | -- | -- | No usable BSD equivalent found. Skip, or clean-room |
| -- | Dell laptop extras | Linux `dell/dell-laptop.c` | GPL-2.0-only (NOT OK) | 2,614 | -- | -- | -- | Skip |
| -- | CPU temperatures (coretemp/k10temp) | already present | -- | -- | arch/x86_64/cpu/cputemp.c reads MSR 0x19C/0x1B1 (Intel) and SMN 0x59800 (AMD) | -- | -- | Not needed |
| -- | Lenovo ThinkPad/IdeaPad, ASUS hotkeys and keyboard backlight | already present | -- | -- | drivers/acpi/laptop.c (MHKM/MLCG/SALS/HALS/SBMC/VPCR, ASUS WMNB DSTS/DEVS) | -- | -- | thinkpad_acpi.c (12,469) and ideapad-laptop.c (2,594) are GPL-2.0-or-later if more features are wanted |

## Per-item notes (where a porter starts)

### 1. HD Audio probe fixes (S, do first)
Facts verified in drivers/sound/hda.c (`hda_probe`, around line 178) and sys/native/kapi.c:
- **Only one controller is tried.** It probes `kapi_pci_find_class(0x04,0x03,0,...)`, which is the first class-04/03 function in bus/dev/fn order. On AMD APUs the GPU's HDMI audio function usually sits at a lower function number than the analog HD Audio function. If the first controller has only HDMI pins, `scan()` keeps only pin device types 0, 1 and 2 (line-out, speaker, HP), so it finds no path. `hda_probe` then logs "no usable output path" and returns 0, and core/sound.c falls through to AC97 without trying `nth=1`. Fix: loop `nth` until a controller yields paths.
- **Only one codec is tried**: the first set bit of STATESTS (`r16(0x0E)`). It should loop over all codecs.
- **Intel controllers with the DSP enabled are never found.** Their class is 0x0401 (or 0x0403 with prog-if 0x80). Linux `sound/hda/core/intel-dsp-config.c` lines 713-720 treat 0x040100 and 0x040380 as "DSP present" and fall back to legacy HDA when there are no DMICs or SoundWire. NoovexOS searches only class 04/03, so 0x0401 parts get no sound at all. intel-dsp-config.c is GPL-2.0-only, so copy the logic, not the code.
- **Controllers with a BAR above 4 GB are refused**: `"hda: BAR above 4 GB, not mapped"`. It needs a high-MMIO mapping.
- Reference for multi-codec and controller enumeration: Haiku `src/add-ons/kernel/drivers/audio/hda/hda_controller.cpp` (MIT, 1,433).

### 2. HDA codec quirks
- Start from Linux `sound/hda/codecs/realtek/alc269.c` (fixup tables, `SND_PCI_QUIRK(subvendor, subdevice, ...)`) and `sound/hda/codecs/realtek/realtek.c` (shared helpers). Both are GPL-2.0-or-later.
- The generic parser `sound/hda/codecs/generic.c` (6,128, GPL-2.0-or-later) is a reference for headset-mic and auto-mute logic.
- BSD tables: FreeBSD `sys/dev/sound/pci/hda/hdaa_patches.c` (subsystem-ID pin patches); OpenBSD `sys/dev/pci/azalia_codec.c`.
- HDMI/DP audio (`sound/hda/codecs/hdmi/hdmi.c`, 2,378, GPL-2.0-or-later) would follow item 1. hda.c currently ignores digital pins.

### 3. SD/eMMC (SDHCI)
- Recommended: OpenBSD, which is ISC and self-contained: `sys/dev/sdmmc/sdhc.c` (host, with ADMA2), `sys/dev/sdmmc/sdmmc.c` (bus), `sys/dev/sdmmc/sdmmc_mem.c` (SD + MMC card init, EXT_CSD), `sys/dev/pci/sdhc_pci.c` (matches class SYSTEM/SDHC generically, plus Ricoh/TI/ENE/GL9755 quirks), `sys/dev/acpi/sdhc_acpi.c` (PNP0D40, INT33BB). Skip `sdmmc_scsi.c` and expose a block device directly.
- Smaller alternative: Haiku `src/add-ons/kernel/busses/mmc/sdhci.cpp`, `sdhci_pci.cpp`, `sdhci_acpi.cpp`, `bus_managers/mmc/mmc_bus.cpp`, `drivers/disk/mmc/mmc_disk.cpp` (MIT, C++).
- AMD eMMC (ACPI AMDI0040) appears in FreeBSD `sys/dev/sdhci/sdhci_acpi.c` (BSD-2) but not in the OpenBSD list.
- Intel Gemini Lake eMMC (8086:31cc, `PCI_DEVICE_ID_INTEL_GLK_EMMC` in Linux sdhci-pci.h) has Linux-only tuning quirks in `sdhci-pci-core.c` (GPL-2.0-or-later, usable). FreeBSD falls back to "Generic SD HCI" by class.
- NoovexOS hook: add `BK_SD` to the enum in fs/vfs/parts.c and dispatch in `bd_read`; add /dev/mmcblk0 to sys/linux/syscalls/sysfs.c (the `SXK_DISK` pattern).

### 4. Realtek RTSX
- On top of item 3: OpenBSD `sys/dev/ic/rtsx.c`, `sys/dev/ic/rtsxreg.h`, `sys/dev/pci/rtsx_pci.c` (ISC).
- For RTS5228/5261/5264 and later chips, use the Linux GPL-2.0-or-later files: `drivers/misc/cardreader/rtsx_pcr.c`, `rts5228.c`, `rts5261.c`, `rts5264.c`, `drivers/mmc/host/rtsx_pci_sdmmc.c`, `include/linux/rtsx_pci.h`.
- FreeBSD `sys/dev/rtsx/rtsx.c` (BSD-2) adds RTS5260.
- USB card readers already work as USB mass storage.

### 5. Generic HID gamepad + evdev
- Extend `hid_parse` in drivers/usb/xhci/controller_init.c. The application-collection switch maps usages 0x10001/0x10002 to mouse, 0x10006/0x10007 to keyboard and 0xC0001 to consumer. Add 0x10004 (joystick) and 0x10005 (gamepad), and a field picker for X/Y/Z/Rx/Ry/Rz, hat and buttons.
- Interface claiming is in drivers/usb/xhci/storage_fat_and_hubs.c around line 94 (`kbd`/`mouse`/`cam`/`prn` detection).
- There is no generic input-event layer: input.h exposes only mouse and keyboard helpers. Model a new one on FreeBSD `sys/dev/evdev/evdev.c` + `cdev.c` (BSD-2). Expose /dev/input/eventN (struct input_event, EVIOCG* ioctls) and optionally /dev/input/jsN (uapi joystick.h).
- The button and axis maps follow FreeBSD `sys/dev/hid/hgame.c`.

### 6. Xbox controllers
- Linux `drivers/input/joystick/xpad.c` (GPL-2.0-or-later). The device table has `XTYPE_XBOX360`, `XTYPE_XBOX360W` and `XTYPE_XBOXONE`. Port `xpad360_process_packet`, `xpadone_process_packet` and the Xbox One init sequence.
- The devices are vendor class 0xFF, so they need their own claim path next to HID.

### 7. USB serial
- Haiku `src/add-ons/kernel/drivers/ports/usb_serial/`: `ACM.cpp`, `FTDI.cpp` + `FTDIRegs.h`, `Silicon.cpp` (CP210x), `WinChipHead.cpp` (CH34x), `Prolific.cpp`, `SerialDevice.cpp` (shared line handling). All MIT, compact, and easy to turn into C.
- BSD cross-checks: OpenBSD `sys/dev/usb/{uftdi,uchcom,uslcom,umodem}.c`.
- The tty side should copy `pty_ioctl` and `pty_poll` in sys/linux/syscalls/fifos_and_names.c (TCGETS 0x5401 already formats termios).
- The bulk-transfer pattern is drivers/usb/xhci/usb_network.c (`usn_xfer`, `usn_arm`, `usn_events`, `usn_claimable`).
- There is no USB class-driver registry: each class is wired by hand into enumeration. Adding a small "class driver" table first would make items 5 to 8 cheaper.

### 8. usbfs
- Reference: Linux `drivers/usb/core/devio.c` (GPL-2.0+); ABI in `include/uapi/linux/usbdevice_fs.h`.
- NoovexOS already lists /dev/bus/usb/BBB/DDD and the sysfs uevent attributes (sys/linux/syscalls/sysfs.c, `SXK_USB`), but `dx_ioctl` only knows the block ioctls (0x80081272, 0x1260, 0x1268, ...).
- Implement USBDEVFS_CONTROL, BULK, CLAIMINTERFACE, RELEASEINTERFACE, SETINTERFACE, SUBMITURB, REAPURB(NDELAY) and DISCARDURB, plus reads of the raw descriptors.

### 9. HP WMI and Intel HID
- Linux `drivers/platform/wmi/core.c` (`_WDG` GUID parsing, WMxx/WQxx calls) and `drivers/platform/x86/hp/hp-wmi.c`. Both GPL-2.0-or-later.
- laptop.c already finds PNP0C14 and calls WMI-style methods for ASUS, so a GUID table plus the HP event codes fit the existing `lap_call`/`lap_m*` helpers.
- Intel: `drivers/platform/x86/intel/hid.c` and `vbtn.c` (GPL-2.0+).

### 10. Fingerprint
- After item 8: install Debian's libfprint-2-2 and fprintd.
- libfprint's COPYING is LGPL-2.1. meson.build requires glib/gio/gobject >= 2.68 and gusb >= 0.2.0.
- Driver sources: `libfprint/drivers/{synaptics,goodixmoc,elanmoc,egismoc,fpcmoc,focaltech_moc,realtek}`.

### 11. Intel SOF
- Use the BSD-3 side of the dual licence: `sound/soc/sof/core.c`, `ipc4.c`, `intel/hda.c`, `intel/hda-dsp.c`, `intel/pci-tgl.c`, `drivers/soundwire/bus.c`, `drivers/soundwire/intel.c`.
- Firmware and topology come from sof-bin (/tmp/ref/sof-bin: LICENCE.Intel = BSD-3 sources + limited patent licence).
- Blockers: every SoundWire codec driver checked (`sound/soc/codecs/rt711-sdca.c`) and the machine glue (`sound/soc/sdw_utils/soc_sdw_utils.c`) are GPL-2.0-only. They would have to be clean-room rewrites.
- Do item 1 first: it gives speakers and headphones on most machines where SOF is only needed for the internal mic.

### 12. AMD display
- Everything relevant is MIT: `drivers/gpu/drm/amd/display/dc/*`, `dc/bios/bios_parser2.c`, `dmub/src/dmub_srv.c`, `amdgpu/atom.c`, `amdgpu/atombios_encoders.c`, `include/atombios.h`.
- Size (461k lines of DC .c alone) makes this XL. A minimal path is to parse the VBIOS (atom.c), then program one CRTC/encoder through DC's resource layer for a single ASIC family.
- The user's HP laptop with an AMD GPU (from the user's memory notes; model unknown) would be the test target.

### 13 to 15
- ALS: OpenBSD `sys/dev/acpi/acpials.c`.
- AMD SFH: Linux `drivers/hid/amd-sfh-hid/amd_sfh_pcie.c`, `amd_sfh_client.c`, `hid_descriptor/amd_sfh_hid_report_desc.h`, `sfh1_1/amd_sfh_init.c`.
- Printers: FreeBSD `sys/dev/usb/serial/ulpt.c` or Linux `drivers/usb/class/usblp.c` for /dev/usb/lp0. From the user's memory notes, their own printer is on Wi-Fi/network, so USB printing has low priority for them.
