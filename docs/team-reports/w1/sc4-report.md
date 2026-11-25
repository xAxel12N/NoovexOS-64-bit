# Scout sc4: Bluetooth for NoovexOS 64

Date 2026-11-14. Read-only. Repo v11.06. Sources: NoovexOS docs/drivers, Linux in `/tmp/ref/linux-atlas`, linux-firmware WHENCE and blobs (`/tmp/sc4dl`), NetBSD-release-11 netbt and dev/bluetooth (`/tmp/sc4dl/nb`), Zephyr main @7cd4b56 2026-11-13 (sparse clone `/tmp/sc4dl/zephyr`), FreeBSD rtlbtfw (web), Zephyr Classic status deck (NXP, Nov 2025).

## 0. What we already have (so nothing below duplicates it)

- No Bluetooth code at all. Only: `drivers/usb/xhci/storage_fat_and_hubs.c:92` names class 0xE0 "Bluetooth adapter"; the Settings category "Bluetooth & devices" (`apps/system/settings/storage_and_layout.c:92`) has no BT page behind it; ARCHITECTURE says "No Bluetooth HID code exists" and "Bluetooth headsets ... not supported".
- Useful existing parts:
  - xHCI control EP0, bulk IN/OUT, interrupt IN (HID polling) and isochronous (UVC, UAC1). `drivers/usb/xhci/usb_network.c` (bulk + interrupt on one device) is the closest template for an HCI-USB driver. Every transfer type the BT transport needs is already there.
  - The HID report parser and input queue (`drivers/usb/xhci/hid_keyboard_mouse.c`, `drivers/input/input.c`). Bluetooth HID uses the same report format, so BT HID only needs an L2CAP feed into them.
  - Audio: UAC1, HDA, AC97 with `drivers/sound/ring.h`. An A2DP sink is one more output on the same path.
  - SBC codec: FFmpeg in `third_party/ffmpeg` already contains `libavcodec/sbcenc.c`, `sbcdec.c` and `sbcdsp*` (LGPL-2.1+). A2DP needs no new codec.
  - Firmware licensing: `rtw88/rtw8821c_fw.bin` is already shipped under `LICENCE.rtlwifi_firmware.txt`. The BT blobs use exactly the same licence.
- Prior scouting exists: PORTING-WORKLIST rows 2 and 3, and `docs/team-reports/atlas.md` section 4 (btrtl init sequence). This report checks those against the sources and goes further: Zephyr Classic, NetBSD, the milestone plan and the coex blocker.

## 1. Corrections to the task brief and to earlier notes

1. **btusb is NOT GPL-2.0-only.** `drivers/bluetooth/btusb_main.c` line 1 is `// SPDX-License-Identifier: GPL-2.0-or-later` (MODULE_LICENSE("GPL") at line 5130). `btrtl.c` and `btrtl.h` are also `GPL-2.0-or-later` (copyright Endless Mobile 2015). Under our GPLv3 rule both are ALLOWED. Only `net/bluetooth` (the Linux host stack: hci_core etc., `GPL-2.0`) is GPL-2.0-only and stays rejected. We should still write the transport from the spec, because it is small and btusb is tied to Linux internals. btrtl can be used as the reference for the loader.
2. **0BDA:C123 sits in btusb's "Additional Realtek 8822CE Bluetooth devices" group** (`BTUSB_REALTEK | BTUSB_WIDEBAND_SPEECH`). The owner's Wi-Fi half is RTL8821CE, which works. The chip has to be identified at run time from HCI_Read_Local_Version (8821C: lmp_subver 0x8821, hci_rev 0x000c, hci_ver 8; 8822C: lmp_subver 0x8822). Ship both blobs:
   - `rtl_bt/rtl8821c_fw.bin`: 54636 B, header "Realtech" (V1 epatch), ends in 51 04 fd 77, project ID 10.
   - `rtl_bt/rtl8822cu_fw.bin`: 37327 B, header "RTBTCore" (V2 epatch), project ID 13. Its config is a link to `rtl8761bu_config.bin`.
   - `rtl8821c_config.bin` is 10 B and optional for 8821C over USB (`config_needed=false`).
   The loader must parse both V1 and V2.
3. **Coex blocker (important).** Our `rtw_coex_wifi_only` (rtw88 `__rtw_coex_init_hw_config(wifi_only=true)`, run by `wifi_up`) sets GNT_WL high, GNT_BT low and gives the shared antenna to Wi-Fi. With that setting the BT radio is likely deaf, so a scan finds nothing even with a perfect stack. Milestone 1 therefore needs a coex change in the Wi-Fi driver:
   - minimal: give the antenna to BT, or run PTA arbitration (GNT_BT high / hardware PTA) whenever BT is enabled;
   - full: port rtw88 `coex.c` (4205 lines, `GPL-2.0 OR BSD-3-Clause`, allowed) later, when Wi-Fi and A2DP have to run at the same time.

## 2. The pieces

### 2a. HCI-over-USB transport (own code, from the spec)
Bluetooth Core spec Vol 4 Part B: the device has class E0/01/01 on interface 0.
- HCI commands: EP0 class request, bmRequestType 0x20, bRequest 0, wIndex = interface.
- Events: interrupt IN (usually 0x81, 16-64 B).
- ACL data: bulk IN/OUT (0x82 / 0x02).
- SCO voice: interface 1 isochronous alt settings. Only needed for headset voice (HFP), not for scan, HID or A2DP.
Size: about 400-600 lines on top of the existing xHCI code. Licence: our own.

### 2b. Realtek firmware download
The protocol, confirmed from btrtl and FreeBSD rtlbtfw:
1. HCI_Reset, then Read_Local_Version, to find the chip.
2. Vendor command 0xFC6D: read the ROM version (1 byte).
3. V2 files also need the key ID: vendor read 0xFC61. Security sections are skipped when the key ID is 0.
4. Parse the epatch:
   - V1: header, `num_patches`, then arrays of chip_id u16, length u16 and offset u32. Pick the entry with chip_id == rom_version+1 and overwrite its last 4 bytes with the fw_version from the header.
   - V2: sections with opcode 1 (snippets), 2 (dummy) and 3 (security). Keep subsections whose eco == rom+1, sort them by priority and join them.
5. Append the config file if there is one.
6. Send with vendor command 0xFC20 in 252-byte fragments. Byte 0 of each fragment is the index. It runs 0..0x7f, then wraps to 1. The last fragment has bit 0x80 set. Each fragment waits for its Command Complete.
7. Read_Local_Version again; the LMP subversion now shows the patched firmware version.
8. Optional: vendor 0xFC66 and re-read when the chip is not recognised.

Usable sources:
- FreeBSD `usr.sbin/bluetooth/rtlbtfw` (main.c ~530 lines + rtlbt_fw.c/hw.c): **BSD-2-Clause** (SPDX line; Chadd 2013, Kondratyev 2019, Future Crew 2023). Parses V1 and V2. This is the best base for a port.
- Linux btrtl.c (1567+198 lines): GPL-2.0-or-later. Also allowed; use it as the reference for the IC table.
- Zephyr and NuttX have no Realtek USB patch loader. Zephyr's `hci_rtk_ameba.c` is a SoC glue file, not a patch loader. Realtek's own rtk_hciattach / rtk_btusb are GPL vendor drops and not needed.

Size: ~600-800 lines. Firmware licence: linux-firmware WHENCE says "Licence: Redistributable. See LICENCE.rtlwifi_firmware.txt", `SPDX-License-Identifier: LicenseRef-LICENCE.rtlwifi_firmware.txt`. This is the same entry and the same terms as our shipped rtw88 blob.

## 3. Host-stack candidates (ranked)

| Rank | Candidate | Licence (proof) | Size | Covers | Maps onto us | Effort (agent-h) | Value |
|---|---|---|---|---|---|---|---|
| 1 | **Own minimal HCI layer for milestone 1** + FreeBSD rtlbtfw loader | own + BSD-2 (rtlbtfw SPDX) | ~1.5-2k lines | Init, firmware, Classic inquiry with names (EIR + Remote_Name_Request), LE scan (adv reports, name AD type), RSSI | xHCI as in usb_network.c; a `bt` shell command; list in Settings "Bluetooth & devices" | 10-16 (+2-4 coex) | High: works on the owner's 0BDA:C123, gives visible progress fast, and is needed under any stack |
| 2 | **Zephyr Bluetooth host** (`subsys/bluetooth/host`) | **Apache-2.0**: all 115 .c/.h files carry `SPDX-License-Identifier: Apache-2.0` | 158k lines total, of which classic/ is 94k. A useful subset (hci_core, conn, l2cap, att, gatt, smp, keys + classic br, conn_br, l2cap_br, ssp, sdp, keys_br, hid_host, a2dp, avdtp, a2dp_codec_sbc) is ~55-65k | LE + Classic: GAP, SMP, SSP, L2CAP, SDP, RFCOMM. Classic **HID host** (`classic/hid_host.c`, 1680 lines, Xiaomi 2026), **A2DP/AVDTP**, AVRCP, HFP, GATT client. The NXP deck says the core is validated and A2DP/AVDTP/HFP are complete | Needs a shim: k_work (delayable), k_sem, k_fifo, atomic, sys_slist, logging, a hand-written autoconf.h (Kconfig), settings for link keys. Copy `lib/net_buf` (1655 lines, Apache-2.0). Plus our HCI driver as a `bt_hci_driver`; `drivers/bluetooth/hci/userchan.c` shows a host-OS-backed HCI driver | Shim 15-25 h; HID host on top 15-25 h; A2DP 20-30 h. BLE mice/keyboards need a HOGP client (~800 lines own) | **Highest**. The only allowed stack that reaches all three goals (scan, Classic + BLE HID, A2DP) |
| 3 | **NetBSD netbt + dev/bluetooth + bta2dpd** | BSD-2 (Iain Hibbert 2005, NetBSD Foundation, Sloss 2015-2020) + BSD-3 (Itronix 2006, non-endorsement clause only). No advertising clause in any of the 40 files (checked) | netbt 15.8k lines (HCI, L2CAP, RFCOMM, SCO sockets); dev/bluetooth 10.1k: bthidev 1026, btkbd 618, btms 492, btsco 1125, plus h5/bcsp/uart; usb/ubt.c 1853 (reference only); bta2dpd ~4k with its own BSD SBC encoder | Classic only: HCI, L2CAP, RFCOMM, SCO, HID kbd/mouse, A2DP source+sink. **No BLE host**, so no BLE mice/keyboards. SSP: the defines exist but pairing is done in userland (bthcid PIN/link keys), so SSP IO capability and user-confirm handling are ~300-500 lines of our own | Kernel socket-style design fits our kernel better than netgraph; bthidev feeds HID reports straight into our parser | Port 20-30 h to HID; A2DP 15-20 h | High for Classic. Simpler and smaller than Zephyr, but a dead end for BLE |
| 4 | **Apache NimBLE** (mynewt-nimble host) | Apache-2.0 (LICENSE checked by Atlas) | host ~1.6 MB of source | **BLE only**: scan, GATT, SMP. No Classic, so no Classic HID, no A2DP | Designed for porting (npl OS shim, HCI transport layer) | Shim 8-12 h, BLE scan + HOGP 15-20 h | Medium. Most cheap BT mice and keyboards, and all headsets, are Classic or dual-mode |
| 5 | **FreeBSD netgraph bluetooth** + bthidd + virtual_oss bt backend | BSD-2 (45/45 files, Atlas) | 32.9k kernel + userland | Classic HCI, L2CAP, RFCOMM; HID via userland bthidd; no BLE | Netgraph framework must be stubbed or replaced, so it fits worse than NetBSD | L-XL | Medium. Use NetBSD instead; take only rtlbtfw from FreeBSD |

### Rejected / not recommended
- **REJECTED, licence: Linux `net/bluetooth`.** GPL-2.0-only (e.g. hci_core.c is `GPL-2.0`).
- **REJECTED, licence: BTstack.** Custom BSD licence with a clause allowing use "solely for personal benefit and not for any commercial purpose". Not GPL-compatible.
- Not recommended, effort (licence is fine):
  - Android Fluoride / Gabeldorsche: Apache-2.0, but C++17 + Rust with libchrome, abseil, protobuf/flatbuffers and the Android HAL. Far too heavy.
  - BlueZ: GPL-2.0-or-later / LGPL. It needs a Linux-compatible AF_BLUETOOTH kernel side (mgmt API), which we would have to write from scratch, plus D-Bus. XL.

## 4. Recommended plan with estimates

**Milestone 1: "scan and list nearby devices"** (rank 1), 12-20 agent-hours plus owner hardware test.
1. `drivers/bluetooth/transport/btusb.c` (own): claim class E0/01/01 (match 0BDA:C123 and the generic Realtek E0 class). Implement EP0 command send, interrupt-IN event ring and bulk ACL in/out.
2. `drivers/bluetooth/firmware/rtlbt_fw.c`: port FreeBSD rtlbtfw (BSD-2) for V1+V2, cross-checked against btrtl. Add the blobs to the firmware assets: `rtl8821c_fw.bin`, `rtl8821c_config.bin`, `rtl8822cu_fw.bin`, `rtl8761bu_config.bin`.
3. Minimal HCI layer (own):
   - Reset, Read_BD_ADDR, Set_Event_Mask, Write_Inquiry_Mode=2 (EIR).
   - Inquiry: parse Inquiry_Result_with_RSSI and Extended_Inquiry_Result (name from EIR), and run Remote_Name_Request for devices without a name.
   - LE_Set_Scan_Parameters / LE_Set_Scan_Enable: parse LE Advertising Reports (complete/short name, appearance, RSSI).
4. Coex: change `rtw_coex_wifi_only` so BT gets the antenna or PTA arbitration while BT is on. Test Wi-Fi with BT on and off.
5. UI:
   - shell `bt up|scan|list`;
   - a Settings "Bluetooth & devices" page listing devices (name, address, Classic/LE, RSSI, device class icon).

Testing:
- Host test of the epatch parser against the real blobs, compared with a Python version of the btrtl logic. Mutate rom_version 0/1 for V1 and run the V2 section path.
- A mock HCI controller in the host harness for the command/event flow.
- QEMU removed `usb-bt-dongle`, so a real radio needs the owner's PC. Log every HCI command and event (status codes) so a failed first boot can still be diagnosed.

**Milestone 2: HID keyboards/mice**, choose a stack here.
- Recommended: Zephyr host (rank 2). Shim 15-25 h + Classic HID host glue into our HID parser 10-15 h + pairing UI (SSP just-works / passkey display for keyboards) and link-key storage on disk 6-10 h. That is **~35-50 h**. BLE HOGP (most new BT mice) adds ~10-15 h on Zephyr's GATT client.
- Cheaper Classic-only alternative: NetBSD netbt + bthidev, ~25-35 h. No BLE devices.

**Milestone 3: A2DP audio (headphones/speakers)**
- On Zephyr: a2dp + avdtp + SBC through FFmpeg's sbcenc (LGPL) or Zephyr's a2dp_codec_sbc, feeding a new sound sink beside UAC/HDA: **~20-30 h**.
- Plus real coex (port rtw88 coex.c TDMA, 4.2k lines, GPL-2.0 OR BSD-3) so Wi-Fi and audio can run together: **+15-25 h**.
- Optional later: AVRCP volume/buttons 5-10 h; HFP/SCO headset mic, which needs xHCI isoc on interface 1, 15-25 h.

Rough total to "BT mouse + keyboard + headphones on the owner's PC": **~100-150 agent-hours**, of which milestone 1 is ~15.

## 5. Files and paths
- Our existing code: `drivers/usb/xhci/usb_network.c` (template), `drivers/usb/xhci/hid_keyboard_mouse.c`, `drivers/input/input.c`, `drivers/sound/ring.h`, `drivers/net/wifi/realtek/rtw88/rtw8821ce.c:261` + `chips/rtw8821c.c:264` (coex), `third_party/ffmpeg/libavcodec/sbcenc.c`.
- References downloaded:
  - `/tmp/sc4dl/WHENCE`, `/tmp/sc4dl/rtl8821c_fw.bin`, `/tmp/sc4dl/rtl8822cu_fw.bin`;
  - `/tmp/sc4dl/nb/*` (NetBSD netbt, dev/bluetooth, ubt.c);
  - `/tmp/sc4dl/zephyr/subsys/bluetooth/host`, `/tmp/sc4dl/zephyr/drivers/bluetooth/hci`, `/tmp/sc4dl/zephyr/lib/net_buf`;
  - `/tmp/ref/linux-atlas/drivers/bluetooth/btrtl.c`, `btusb_main.c`.
