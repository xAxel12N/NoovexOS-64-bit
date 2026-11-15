# sc8 report: input + platform (laptop daily use)

Owner laptops from memory/BRIEF: ASUS TUF Gaming F17 (11th-gen Intel = Tiger Lake-H, NoovexOS installed on it), Lenovo laptop and HP laptop with AMD GPU (owner wants ELAN touchpad on both). Desktop i5-11400F is not a laptop, so this area matters only for the laptops.

## 1. What we already have (do NOT propose again)
| Area | Have | Where |
|---|---|---|
| PS/2 kbd/mouse | yes, plus Print Screen and media keys | drivers/input/ps2/ps2.c, ps2_mouse.c |
| PS/2 touchpads | Synaptics PS/2, ELAN PS/2 (coasting, gestures) | drivers/input/ps2/syn_ps2.c, elan_ps2.c |
| I2C-HID precision touchpads | yes: HID descriptor, reset, own HID parser, gestures, pinch, coasting | drivers/input/touchpad/i2c_hid.c, tp_hid.c, tp_gesture.c |
| ELAN I2C (non-HID) | yes, ~60 ELAN0xxx IDs | drivers/input/touchpad/elan_i2c.c |
| Touchpad ACPI discovery | _STA/_INI/_CRS/_DSM, ASUS F17 fixes, scan fallback, `tpinfo` | touchpad.c, tp_acpi.c |
| I2C controller | DesignWare (Intel LPSS + AMD), polled only | drivers/i2c/dw_i2c.c |
| GPIO | AMD GPIO (AMDI0030) pin read; Intel GPIO pin read ONLY for INT344B (SKL-LP), INT3452 (BXT), INT3453 (GLK) | drivers/gpio/amd_gpio.c, drivers/input/touchpad/tp_igpio.c |
| USB HID | keyboard, mouse, media keys, boot-protocol | drivers/usb/xhci/hid_keyboard_mouse.c |
| ACPI AML | own interpreter (60 KB, checked against ACPICA acpiexec in check.py), pre-scanner | drivers/acpi/aml.c, aml_scan.c |
| EC, battery, AC, lid, buttons | PNP0C09 EC with back-off, _BST/_BIF/_BIX, ACPI0003, _LID, PNP0C0C/0E, low-battery shutdown | drivers/acpi/acpi_dev.c, apps/system/powerui.c |
| Backlight | ACPI video _BCL/_BCM/_BQC only; /sys/class/backlight | acpi_dev.c, sys/linux/syscalls/sysfs.c |
| Laptop hotkeys | ThinkPad (IBM/LEN0068), IdeaPad (VPC2004), ASUS ATK/WMNB, ATK0100; kbd backlight; battery charge limit (BCSS/SBMC/ASUS 0x00120057) | drivers/acpi/laptop.c |
| Sleep | S1 if present, else s2idle (PNP0D80 _DSM), lid/power/key wake | core/power.c |
| CPU power/temp | HWP/CPPC/P-states, mwait C-states, MSR temp | arch/x86_64/cpu/cpupower.c, cputemp.c |

Gaps found: no S3; no ACPI thermal zones/fan; no native (GPU-register) backlight; Intel GPIO only for 3 old platforms (owner's TGL-H falls back to polling/learning); no GPIO-signaled ACPI events (_AEI / GED ACPI0013, used for lid/EC on hardware-reduced and many AMD laptops); no HP WMI hotkeys; no performance/fan profiles; no touchscreen; no Thunderbolt/USB4 NHI.

## 2. Candidates, ranked for laptop daily use

### #1 Intel PCH GPIO tables + interrupts + ACPI _AEI events: OpenBSD pchgpio.c
- URL: https://github.com/openbsd/src/blob/master/sys/dev/acpi/pchgpio.c
- License: ISC (header "Copyright (c) 2020 Mark Kettenis / 2020 James Hastings. Permission to use, copy, modify, and distribute ..."). OK.
- Covers INT344B, INT3450/INT34BB (Cannon Lake), INT3451/INT345D (SPT-H), INT34C5 (TGL-LP), **INT34C6 (TGL-H = owner's ASUS TUF F17)**, INTC1055, INTC1056/1085 (ADL-S), INTC1057 (ADL-N), INTC1082/1083/105E (Meteor Lake). ~900 lines, mostly per-community pad tables plus intr_establish/enable/disable.
- Firmware: none. Deps: acpi _CRS GpioInt (we parse already), MMIO, an IOAPIC IRQ.
- Maps onto: extend IGP_MAPS in tp_igpio.c with pchgpio's community/pad-group tables (direct pin read works at once for TGL/ADL/MTL), then optional step 2: real GPIO interrupt instead of reading the pin. Step 3 (own code, small): _AEI handler that runs _Lxx/_Exx/_EVT for GpioInt events on both Intel (pchgpio) and AMD (amd_gpio), and GED (ACPI0013). Needed for lid/power button/EC on hardware-reduced ACPI laptops.
- Effort: 3-5 agent-hours for tables, +4-6 h for interrupts and _AEI.
- Value: HIGH. Owner's ASUS F17 touchpad leaves polling; covers every Intel laptop 2018-2024.

### #2 Native backlight (Intel PCH PWM + AMD BL_PWM): Linux i915 intel_backlight.c + amdgpu dce_panel_cntl.c
- URLs: https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/i915/display/intel_backlight.c ; .../drivers/gpu/drm/amd/display/dc/dce/dce_panel_cntl.c (and dcn301_panel_cntl.c)
- License: i915 file "// SPDX-License-Identifier: MIT" (Copyright 2021 Intel). amdgpu file: MIT-style "Copyright 2012-15 Advanced Micro Devices ... Permission is hereby granted, free of charge". OK.
- Why: on most Intel laptops ACPI _BCM only writes the IGD OpRegion ASLE mailbox and expects the graphics driver to act; with no i915-like driver, the brightness keys do nothing. Same on AMD (ATIF via amdgpu). We have igpu.c (no backlight code) and nothing for AMD display.
- What to port: only the CNP+ path (BXT_BLC_PWM_CTL/FREQ/DUTY, 2 controllers, SOUTH_CHICKEN1 check), plus the BXT/SPT path; ~300 lines of 1,100. AMD: BL_PWM_CNTL / BL_PWM_PERIOD_CNTL / GRP1_REG_LOCK, ~150 lines. Optionally the ASLE handler from intel_opregion.c (MIT) so _BCM requests land.
- Firmware: none. Effort: 4-6 agent-hours (Intel), 3-4 (AMD). Plug in as a second backend under bl_step()/bl_cur_pct() and /sys/class/backlight.
- Value: HIGH (brightness keys and the Settings slider on the owner's ASUS F17 and HP AMD laptop; ASUS F17 panel is on the Intel iGPU under Optimus).

### #3 ACPI thermal zones + passive/active cooling: OpenBSD acpitz.c
- URL: https://github.com/openbsd/src/blob/master/sys/dev/acpi/acpitz.c
- License: ISC (Copyright (c) 2006 Can Erkin Acar, 2005 Marco Peereboom). OK.
- ~550 lines: _TMP polling, _CRT clean shutdown, _HOT, _PSV with _TC1/_TC2 trend driving cpu_setperf, _ACx/_ALx fan power resources (_PR0 _ON/_OFF), notify 0x81/0x82.
- Deps map: aml_find/aml_eval (have), cpupower.c (perf limit hook), shutdown path (have for battery), Settings/System info for temps. Drop OpenBSD sensors/kthread glue.
- Effort: 4-6 agent-hours. Value: MEDIUM-HIGH (critical-temperature shutdown and passive throttling are real safety items; EC usually runs fans itself, so _ACx matters on fewer machines).

### #4 Laptop platform extras: Linux hp-wmi.c, asus-wmi.c, ideapad-laptop.c (as references / partial ports)
- URLs: drivers/platform/x86/hp/hp-wmi.c, asus-wmi.c, lenovo/ideapad-laptop.c (torvalds/linux).
- License: all three "// SPDX-License-Identifier: GPL-2.0-or-later". OK (thinkpad_acpi not checked here; dell-laptop is already listed as not usable).
- Gaps vs laptop.c: HP has no driver at all (event GUID 95F24279-4D7B-4334-9387-ACCDC67EF61C, sparse keymap, rfkill, thermal profile, Omen/Victus fan modes); ASUS throttle_thermal_policy / fan boost (Silent/Balanced/Turbo, Fn+F5 on the TUF F17); Lenovo DYTC platform profile and fan_mode. Charge limit and kbd light we already have.
- Effort: HP hotkeys 3-4 h; performance profiles for ASUS/Lenovo/HP 4-6 h (plus a Quick Settings "Power mode" tile).
- Value: MEDIUM-HIGH for the owner's three laptops (HP has zero vendor support now; TUF Fn+F5 fan mode).

### #5 HID touchscreen (2-in-1 laptops): FreeBSD hmt.c
- URL: https://github.com/freebsd/freebsd-src/blob/main/sys/dev/hid/hmt.c
- License: "SPDX-License-Identifier: BSD-2-Clause", Copyright 2014-2020 Vladimir Kondratyev. OK.
- ~800 lines; our tp_hid.c parser already handles digitizer finger collections for touchpads, so only the touchscreen top-level collection (0x0D/0x04), absolute mapping to screen and tap-to-click are new. Deps hidbus/evdev are replaced by our i2c_hid + USB HID.
- Effort: 3-5 agent-hours. Value: MEDIUM-LOW (only touch-screen laptops; no evidence the owner has one).

### Lower / later
- **ACPICA as backup interpreter** (https://github.com/acpica/acpica). License: triple, file header offers Intel license, "Alternatively ... Redistribution and use in source and binary forms ..." (BSD-3 style, with non-endorsement clause) or GPLv2. Use under the BSD option: OK. Size: kernel subset (dispatcher, executer, namespace, parser, tables, events, hardware, resources, utilities) roughly 90-120k lines; needs an OSL of ~40 functions (memory, mutex, semaphores, PCI/IO/memory access, sleep, interrupts). Effort XL (30-50 agent-hours) plus a long tail of integration with acpi_dev.c/power.c. Our aml.c already matches acpiexec in check.py, so recommended only as (a) a host-side oracle on the owner's dumped DSDTs, or (b) a boot-option fallback if aml.c logs errors on real laptops. Value MEDIUM, cost very high. Not now.
- **S3 suspend-to-RAM**: OpenBSD sys/arch/amd64/amd64/acpi_wakecode.S (BSD-2 Watanabe/Iwasaki 2001 + ISC Larkin 2008-09: OK) + FreeBSD acpi_wakeup.c (BSD-2). The trampoline is small (~500 lines asm), but every driver needs suspend/resume (xHCI, NVMe/RST, AHCI, HDA, rtw88, touchpad, SMP APs, HPET/LAPIC), and the firmware does NOT restore the display on S3 resume: our igpu.c has no full modeset, so the screen would stay black on Intel laptops without that. Many 11th-gen+ laptops have no S3 (Modern Standby only); s2idle already exists. Effort XL (40+ h), risk high. Value MEDIUM. Defer; improve s2idle (deeper package C-states via #1 _AEI wake, PEP constraints) first.
- **Thunderbolt/USB4**: FreeBSD sys/dev/thunderbolt (nhi.c "SPDX-License-Identifier: BSD-2-Clause-FreeBSD", Scott Long 2022: OK), ~1k lines NHI plus router/HCM files. On most Windows laptops the firmware connection manager and BIOS already set up PCIe tunnels, and USB on TB/USB4 ports goes through the integrated xHCI we already drive. Hot-plug of TB docks would need PCIe hot-plug first. Effort L, value LOW. OpenBSD has no Thunderbolt driver. Linux drivers/thunderbolt is GPL-2.0-only: REJECTED.

## 3. REJECTED / duplicates
- OpenBSD ihidev/ims, FreeBSD iichid/hms (ISC/BSD-2, license fine): DUPLICATE, we have I2C-HID + precision touchpad + gestures. Useful only as a quirk reference.
- OpenBSD dwiic (ISC): DUPLICATE of dw_i2c.c; only interrupt-driven transfers would be new (not needed while touchpad uses GPIO attention).
- Linux elan_i2c / elantech / synaptics / rmi4 (SMBus "intertouch" for ThinkPads): GPL-2.0-only -> REJECTED (and we already have ELAN I2C/PS2 + Synaptics PS/2 fallback).
- Linux pinctrl-intel / pinctrl-tigerlake / pinctrl-amd: GPL-2.0-only -> REJECTED; use OpenBSD pchgpio (ISC) instead.
- Linux drivers/thunderbolt: GPL-2.0-only -> REJECTED.
- Intel ISH sensor hub (accelerometer, lid angle): GPL-2.0-only (already on the worklist's not-usable list) -> REJECTED.
- Linux acpi/thermal.c, acpi/fan: GPL-2.0-only -> REJECTED; use OpenBSD acpitz.

## 4. Recommended order
1. pchgpio tables into tp_igpio.c (fast win for the owner's ASUS F17), then GPIO interrupts + _AEI/GED events.
2. Native backlight Intel CNP+/BXT, then AMD BL_PWM.
3. acpitz thermal zones (critical shutdown, passive throttling, temps in System info).
4. HP WMI hotkeys + power/fan profiles (ASUS/Lenovo/HP) with a "Power mode" tile.
5. HID touchscreen.
Later: ACPICA as oracle/fallback, S3, Thunderbolt.
