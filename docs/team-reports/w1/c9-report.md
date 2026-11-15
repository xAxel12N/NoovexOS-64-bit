# c9: Bluetooth milestone 1 (scan and list nearby devices). Branch x-c9

Worktree /home/claude/wt/c9. 7 commits on top of v11.06 (dae38348): 5c8acd22 .. HEAD.
This has not been tested on real hardware. QEMU has no BT dongle, so the first real run has to be on the owner's PC.

## What was built

### HCI-over-USB transport: `drivers/bluetooth/btusb.c` (new, own code)
- This file is a fragment included from `drivers/usb/xhci.c:24`, the same way `usb_network.c` is, because the xHCI helpers are static. It follows the usn pattern: its own DMA pages (8, `dma_alloc_pages`), its own EP0 context swap (`btu_ctrl`), a pump that takes `xh_poll_busy` (`btu_pump`), and bounded waits.
- It claims the first interface with class E0/01/01, alt 0. Endpoints: interrupt IN for events, bulk IN/OUT for ACL. The bulk endpoints are configured in the endpoint context but not armed yet; that comes later with ACL.
- HCI commands go out on EP0 as a class request: bmRequestType 0x20, bRequest 0, wIndex 0 (same as the spec, Linux and FreeBSD).
- Events: 8 interrupt TRBs of max-packet size. Reassembly uses the event header length (as Linux btusb does) into a 32-entry queue.
- Errors: halt recovery (Reset EP, Set TR Dequeue, CLEAR_FEATURE), unplug detection, and drop/error counters.
- Hooks into the xHCI code (each one does nothing unless `btu.ok`):
  - `controller_init.c:143-146`: forward declarations.
  - `controller_init.c:157`: `xhid_reset` calls `btu_reset_all`.
  - `controller_init.c:256`: event routing in `xh_event_w_in`.
  - `hid_keyboard_mouse.c:96`: `hid_xfer`.
  - `hid_keyboard_mouse.c:141`: `xhci_mouse_poll` also runs when only BT is active.
  - `hid_keyboard_mouse.c:262`: `msd_autoinit` skips the BT port.
  - `storage_fat_and_hubs.c:33`: hub removal.
  - `storage_fat_and_hubs.c:108`: `msd_autoinit_safe` skips the BT port.
  - `usb_network.c:235`: `usn_claimable` skips the BT port.
  - Without the last three, a USB-stick or usbnet rescan would port-reset the adapter and wipe the firmware patch.
- **Nothing happens at boot.** The adapter is only touched when the user runs `bt up` or `bt scan`.
  - `btusb_probe` only resets a root port whose `g_usbdev` entry has interface class 0xE0. Boot enumeration already records that entry as "Bluetooth adapter".
  - It skips ports with a hub, HID device, camera, audio, USB stick or usbnet on them.
  - So machines without BT see no change, apart from about 92 KB of firmware in the kernel image.
- Limitation: an adapter behind a USB hub is not probed. The owner's 0BDA:C123 is internal, so it is normally on a root port.

### Realtek firmware loader: `drivers/bluetooth/rtlbt_fw.c` + `.h` (port of FreeBSD rtlbtfw, BSD-2)
- The full FreeBSD BSD-2 header and copyrights are kept. `drivers/bluetooth/NOTICE` lists the origin of every file and every blob.
- Ported parts: V1 epatch (`Realtech`), V2 epatch (`RTBTCore`: snippets, dummy, and security sections when the key id is non-zero), the project-id check against the chip, appending the config file, and the 0xFC20 download (252-byte fragments, index 0..0x7F then wrapping to 1, 0x80 on the last fragment).
- Steps added from btrtl: read the ROM LMP subversion via 0xFC61 first. If Read_Local_Version then no longer matches the ROM table, the patch is already running and the download is skipped (result 2). This covers warm reboots.
- One deliberate difference from FreeBSD: a V2 security subsection whose key id matches the chip is included, as Linux btrtl does. FreeBSD `break`s on it. This only matters when the chip has key_id != 0.
- Chip detection at run time from Read_Local_Version:
  - 8821C: 0x8821 / rev 0xc / ver 8, V1 file.
  - 8822C: 0x8822 / rev 0xc / ver 0xa, V2 file.
- `hci.c` runs the Realtek path when the maker is 0x005D or the VID is 0x0BDA.

### Firmware: `assets/firmware/rtl_bt/`
- Files: `rtl8821c_fw.bin`, `rtl8821c_config.bin`, `rtl8822cu_fw.bin`, `rtl8761bu_config.bin` (linux-firmware links `rtl8822cu_config.bin` to the 8761bu file).
- They are unmodified, from git.kernel.org linux-firmware. The fw blobs are byte-identical to sc4's downloads.
- Licence: the WHENCE btusb entry says "Redistributable, LICENCE.rtlwifi_firmware.txt" — the same licence as our rtw88 blob. The licence text is now in `LICENSES/` upstream and sits next to the files here, with a `WHENCE.txt` that has sha256 sums.
- `build.sh:38` links them into `rtlbt_fw.o`, which is added to BLOBS (`build.sh:297`). `rtlbt_fw.c` references the symbols weakly, so a missing blob gives "firmware file is not built in" instead of a link error.

### Minimal HCI layer: `drivers/bluetooth/hci.c` + `hci_parse.h` (own code)
- Commands are synchronous, with matching on Command Complete / Command Status opcodes. Every wait is bounded, and it calls `ui_pump()` and bumps `g_ml_beat` every 20 ms so the freeze box does not fire.
- `bt_up` sequence:
  1. Read_Local_Version.
  2. Realtek setup.
  3. HCI_Reset (5 s timeout).
  4. Read_BD_ADDR and features (LE and BR/EDR bits).
  5. Set_Event_Mask (adds inquiry-with-RSSI, EIR and LE meta).
  6. Write_Inquiry_Mode 2 (falls back to 1), Page_Timeout 2.56 s.
  7. LE host supported and the LE event mask.
- `bt_scan(secs)`:
  - Runs Inquiry (GIAC) and active LE scan at the same time.
  - Parses Inquiry_Result, Inquiry_Result_with_RSSI (14- and 15-byte variants), Extended_Inquiry_Result (EIR name, CoD), and LE Advertising Report (complete/short name, appearance, RSSI).
  - Then sends Remote_Name_Request to up to 4 BR/EDR devices that still have no name.
  - The list holds up to 40 devices and keeps the best RSSI. A complete name beats a short one, and a remote-name result beats both.
- Shell (`shell/commands/network_and_apps.c:15`):
  - `bt` or `bt status`: adapter, address, HCI/LMP versions, firmware message, counters, antenna state.
  - `bt up`
  - `bt scan [seconds]` (default 8): prints address, LE or BR/EDR, RSSI in dBm, name (or "(no name)") and device class, sorted by RSSI.
  - `bt list`
  - `bluetooth` is an alias.

### Coex (the Wi-Fi antenna problem): `rtw_coex_bt_share` in `drivers/net/wifi/rtw88/rtw8821ce.c:262-281`
- How it was done: during `bt scan` only, BT gets a share of the antenna without turning Wi-Fi off.
  - The LTE coex register 0x38 GNT_BT/GNT_WL fields (mask 0xFF00) are set to HW PTA. This is rtw88 `COEX_SET_ANT_2G`.
  - Coex table: rtw88 8821c table 9 (0x66555555 / 0x5A5A5A5A) if Wi-Fi is connected, otherwise table 1 (0x55555555).
  - On 8821C boards with a non-BTG RFE type, the antenna switch is set to PTA control (0xCB4 = 0x66, rv). For BTG RFE types the current switch position already matches rtw88's WLG_BT.
  - Afterwards, 0x38, 0x6C0, 0x6C4 and 0xCB4 are restored exactly to their saved values, so the result is Wi-Fi-only again. `wifi_only` is not re-run, which would break the 5 GHz switch position.
  - It only runs if `g_rtw_coex_done` is set, i.e. the Wi-Fi driver has taken the antenna. If Wi-Fi was never started, nothing is touched.
- Not done: no TDMA (that needs firmware H2C commands and the full coex.c port), so Wi-Fi throughput may dip during a scan. Bluetooth that stays on (milestone 2, connected devices) needs the real rtw88 coex.c port (GPL-2.0 OR BSD-3).
- Risk: if a Wi-Fi channel or band change happens during a BT scan, the restored 0xCB4 is the old value until the next channel set.
- Extra prototypes in `include/nvx/drivers/net/wifi/rtw8821ce.h`. `rtw_chip.h` is unchanged.

## Tests run
- `/home/claude/ccheck.sh` on `drivers/bluetooth/hci.c`, `drivers/bluetooth/rtlbt_fw.c`, `drivers/usb/xhci.c` (the parent of `btusb.c`), `drivers/net/wifi/rtw88/rtw8821ce.c` and `shell/commands.c`: no new warnings (warning counts are identical to v11.06 for the touched old files).
- `python3 tools/check.py --only usb`: 12/12 pass. The generated USB tests needed stubs for the new hooks: `tools/check.py:400`, `tools/tests/usb/hidmouse_gen.py`, `tools/tests/usb/hidsim_gen.py`.
- New host test `sh tools/tests/bluetooth/run.sh` (ASan+UBSan): 43 checks, ALL PASS.
  - HCI event parser: all event types above, the RSSI and name priority rules, malformed and truncated events, a zero-length AD structure, a list overflow, and 20000 random events.
  - Realtek loader on the real blobs:
    - 8821c: V1, project 10, patches for ROM 0 and 1.
    - 8822cu: V2, project 13, patch only for ROM 3.
    - Output is byte-identical to an independent Python reference (`epatch_ref.py`).
    - A synthetic V2 file covers ECO selection, priority sort and the security key id.
  - A mock controller checks the complete download sequence: exact bytes (patch + config), fragment count and index pattern, version re-read, and "already running" detection.

## Not done / open
- Needs a test on the owner's hardware: run `bt status`, `bt up`, `bt scan`, then send the klog. Every step logs `bluetooth: ...`.
  - If the first `bt up` fails, the "Realtek: ..." line gives the exact step.
  - If the scan finds nothing while Wi-Fi is on, compare with Wi-Fi off from a fresh boot (Wi-Fi never started). That tells us whether the coex share is enough.
- Remaining work:
  - No Settings "Bluetooth & devices" page (that is UI work, not in this task).
  - No ACL/L2CAP.
  - No adapters behind hubs.
  - No 8822E / 8852 firmware.
  - No LE extended advertising reports (legacy scan only).
- docs/ARCHITECTURE.md still says there is no Bluetooth code. The lead may want a line there and in PORTING-WORKLIST rows 2/3.

## Fix round (r9 review), branch y-c9, commit on top of v11.07
- H1 fixed: `drivers/bluetooth/hci.c` event mask byte 5 is now 0x40, so Extended Inquiry Result (bit 46) is unmasked. `tools/tests/bluetooth/run.sh` now parses the mask from hci.c and checks bits 0, 1, 6, 33, 46 and 61.
- M1 fixed: `rtw_coex_bt_share` off path in `rtw8821ce.c`:
  - It tries the 0x38 write up to 5 times and verifies each one by reading the GNT bits back.
  - If every try fails, it re-applies `rtw_coex_wifi_only`.
  - The flag is cleared only after one of those two has run.
  - `hci.c` `bt_coex` now logs when the restore failed.
- M2 fixed:
  - `build.sh` (the line after the firmware licence copy) installs into the image:
    - `/usr/share/licenses/bluetooth/NOTICE`
    - `LICENSE.rtlbtfw-BSD-2-Clause` (the BSD-2 header of `rtlbt_fw.c`)
    - `/usr/share/licenses/firmware/WHENCE.rtl_bt.txt`
  - `assets/firmware/README.txt` lists the rtl_bt blobs. `build.sh` was checked with `bash -n` only; it was not run.
- L1 fixed: a root-port unplug calls `btu_remove_below(p,0,0)` (`storage_fat_and_hubs.c`).
- L2 fixed: the config blob is appended even when the key id is non-zero (as FreeBSD and Linux do). The NOTICE says so.
- L3 fixed:
  - Esc (`kdown[0x01]`/`g_busy_esc`) stops the scan loop and the name requests.
  - The scan length is capped at 15 s.
- Test registered: `tools/check.py` has a new `bt` group (`g_bt` runs `tools/tests/bluetooth/run.sh`). `python3 tools/check.py --only bt,usb`: 13/13 pass (bt: 44 checks).
- Compile checks: ccheck shows no new warnings (xhci.c 47 and rtw8821ce.c 5, the same as before).
