# r9 review of c9 (Bluetooth milestone 1), branch x-c9, merged in main as v11.07

Scope: `git log -p v11.06..x-c9` (6 commits), c9-report.md, ctasks/c9.md. Checked against the merged files in main.
I re-ran `sh tools/tests/bluetooth/run.sh` and it passes (43 checks, the V1/V2 patch bytes match the Python reference).
This was a read-only review: nothing was built and nothing was run on hardware.

## Summary
- The transport, the loader and the parser are careful, bounded work. Boot behaviour is unchanged: the adapter is only touched by `bt up` or `bt scan`.
- The licence headers are correct.
- Wi-Fi coex is restored on every code path in `bt_scan`. The one gap is when the restore register write itself fails.
- One HIGH: the event mask leaves Extended Inquiry Result (bit 46) masked, although the report says it is enabled.

## HIGH

### H1. The EIR event is not unmasked (the report's claim is false). Phones and PCs found over BR/EDR lose their names, or are not reported at all.
**Where:** `net/bluetooth/hci.c:76`
```c
static const u8 em[8]={0xFF,0xFF,0xFB,0xFF,0x02,0x00,0x00,0x60};
```
- Extended Inquiry Result is event-mask bit 46, which is byte 5, bit 6 (0x40). Linux sets it the same way: `events[5] |= 0x40`.
- Byte 5 here is 0x00, so the bit stays masked. The default mask covers bits 0-44 only, so it is not set anywhere else either.
- Meanwhile `bt_up` sets `Write_Inquiry_Mode 2` (line 77), so the controller tries to report every device that has EIR data (all modern phones and PCs) as event 0x2F. With that event masked, those results are suppressed (some controllers may fall back to 0x22). Either way the EIR name and class never arrive.
- The only remaining source of names is the 4 Remote_Name_Requests at the end of the scan.
- The parser does handle 0x2F, and the host test exercises it, but the test never checks the mask. So the test passes while the feature is dead.

**Patch:**
```diff
-    { static const u8 em[8]={0xFF,0xFF,0xFB,0xFF,0x02,0x00,0x00,0x60}; if(!bt_ok(0x0C01,em,8))klog("bluetooth: Set Event Mask failed"); }
+    { static const u8 em[8]={0xFF,0xFF,0xFB,0xFF,0x02,0x40,0x00,0x60}; if(!bt_ok(0x0C01,em,8))klog("bluetooth: Set Event Mask failed"); }
```
Optional: add an assert to `tools/tests/bluetooth/hci_main.c` that bit 46 and bit 33 are set in the mask.

## MEDIUM

### M1. Coex restore: if the indirect write to 0x38 fails, the "done" flag is already cleared, so Wi-Fi is left on HW-PTA grant with no retry
**Where:** `drivers/net/wifi/realtek/rtw88/rtw8821ce.c:266`
- On the off path, `g_rtw_coex_bt=0` is set first. Then `rtw_lte_write(m,0x38,saved)` runs. That write can fail when `rtw_lte_wait` times out, for example while the card is in LPS or briefly busy.
- On failure, GNT_BT/GNT_WL stay at HW PTA. Nobody retries, and `bt_coex()` (hci.c:87) ignores the return value.
- This is the only code path where the antenna is not put back exactly. Every other path checks out:
  - `bt_scan` has no early return between `bt_coex(1)` (line 94) and `bt_coex(0)` (lines 98/113). Refused scans, unplug mid-scan and failed name requests all reach `bt_coex(0)`.
  - A partial failure on the on path still leaves `g_rtw_coex_bt=1`, so the next off call restores it.
  - If the 0x38 read returns 0xFFFFFFFF, nothing is touched.
  - Wi-Fi state cannot change during the scan: `ui_pump` only does touchpad, xHCI mouse and cursor work, and never calls `wifi_mgr_tick`. So the report's "channel change during scan" risk cannot happen.

**Patch (retry; keep the flag set so a later call can finish the restore):**
```diff
-    if(!on){ if(!g_rtw_coex_bt)return 1; g_rtw_coex_bt=0; rtw_w32(m,0x06C0,cxs_6c0); rtw_w32(m,0x06C4,cxs_6c4); rtw_w32(m,0x0CB4,cxs_cb4); return cxs_lte38!=0xFFFFFFFFu&&rtw_lte_write(m,0x38,cxs_lte38); }
+    if(!on){ if(!g_rtw_coex_bt)return 1; rtw_w32(m,0x06C0,cxs_6c0); rtw_w32(m,0x06C4,cxs_6c4); rtw_w32(m,0x0CB4,cxs_cb4);
+        for(int t=0;t<5;t++){ if(rtw_lte_write(m,0x38,cxs_lte38)&&(rtw_lte_read(m,0x38)&0xFF00u)==(cxs_lte38&0xFF00u)){ g_rtw_coex_bt=0; return 1; } }
+        klog("wifi: coex 0x38 restore failed - Wi-Fi-only setup will be re-applied"); rtw_coex_wifi_only(m); g_rtw_coex_bt=0; return 0; }
```
- The fallback `rtw_coex_wifi_only` re-applies the known Wi-Fi-only state. That is safer than leaving the PTA grant in place.
- It only runs on the failure path, so the report's concern about the 5 GHz switch position does not apply in the normal case.
- Also, in `hci.c:87`, log the failure when `rtw_coex_bt_share(bt_coex_m,0,0)` returns 0, instead of always logging "antenna back to Wi-Fi only".

### M2. Licence: the BSD-2 notice for the rtlbtfw port and the list of rtl_bt blobs are not shipped in the image
**Where:** `build.sh:304`, which was added by another coder in this wave.
- That line copies only `LICENCE.nvidia`, `LICENCE.rtlwifi_firmware.txt` and the Wi-Fi `README.txt` to `/usr/share/licenses/firmware/`.
- The licence text itself is identical for the BT blobs (I checked with `cmp`), so the blob licence is covered.
- However, `README.realtek.txt` (from `assets/firmware/README.txt`) lists only the three rtw88 blobs.
- BSD-2 clause 2 requires the copyright notice and disclaimer to be reproduced in the documentation of binary redistributions. `rtlbt_fw.c` is compiled into `kernel64.elf`, but its notice is not shipped anywhere in the system files.

**Patch (build.sh, after line 304):**
```diff
+mkdir -p $SR/usr/share/licenses/bluetooth && cp drivers/bluetooth/NOTICE $SR/usr/share/licenses/bluetooth/NOTICE && sed -n '1,28p' drivers/bluetooth/firmware/rtlbt_fw.c > $SR/usr/share/licenses/bluetooth/LICENSE.rtlbtfw-BSD-2-Clause && cp assets/firmware/rtl_bt/WHENCE.txt $SR/usr/share/licenses/firmware/WHENCE.rtl_bt.txt
```
Also add these lines to `assets/firmware/README.txt`:
```
  rtl_bt/rtl8821c_fw.bin, rtl_bt/rtl8821c_config.bin, rtl_bt/rtl8822cu_fw.bin, rtl_bt/rtl8761bu_config.bin
  (Realtek Bluetooth, linux-firmware btusb entry, same LICENCE.rtlwifi_firmware.txt)
```

## LOW

### L1. A hot-unplug does not release the BT claim, so the adapter stays "claimed" after a replug
**Where:** `drivers/usb/xhci/storage_fat_and_hubs.c:121-123`
- The root-port unplug branch releases MSD, HID, UVC and audio, but not `btu`.
- `btu` only notices the unplug inside `btu_recover`, which runs only during BT commands.
- If the adapter is replugged before that, `btu.ok` stays 1 with the old slot. Every `bt` command then times out until reboot, and `bt_up` never re-probes.
- In the same window, the hotplug path runs `xhci_msd_enum(p)`, which port-resets the adapter.

**Patch:**
```diff
-            xhid_remove_port(p); usbdev_gone(p); uvc_unplug(p); aud_unplug(p); }
+            xhid_remove_port(p); btu_remove_below(p,0,0); usbdev_gone(p); uvc_unplug(p); aud_unplug(p); }
```
`btu_remove_below` is already forward-declared in `controller_init.c:146`. `xh_under(route,0,0,0)` is true for a root device.

### L2. The config blob is skipped when key_id != 0; neither FreeBSD nor Linux does that
**Where:** `drivers/bluetooth/firmware/rtlbt_fw.c:153` (`if(cl>0&&!key&&...)`)
- FreeBSD `main.c` calls `rtlbt_append_fwfile(&fw,&cfg)` whenever a config file is present. Linux btrtl does the same.
- This only affects 8822C units with a non-zero security key, not the owner's 8821C (V1, key=0). It is still an undocumented deviation (the NOTICE lists only the security-subsection one).

**Patch:**
```diff
-    if(cl>0&&!key&&pl+cl<=cap2){
+    if(cl>0&&pl+cl<=cap2){
```

### L3. A scan blocks the main loop for up to about 60 s; the amber busy box appears and Esc does nothing
- `bt scan` runs for up to 30 s, plus 4 s of grace, plus 4 remote-name requests at 2 s + 4 s each. That is about 58 s of nested loop.
- Because `ui_pump` bumps `g_nest_beat` while `g_ml_top` stays still, c2's busy box appears after 5 s even for the default 8 s scan.
- The box says "Press Esc", but `bt_idle` never checks `g_busy_esc`.
- Wi-Fi RX, DHCP and TCP are also stalled for that time, because `wifi_mgr_tick` is not called.

**Patch (hci.c):**
- Line 18: in `bt_idle`, add `extern volatile int g_busy_esc;`.
- Line 102: break the scan loop when `g_busy_esc` is set.
- Line 109: in the name loop, `if(g_busy_esc)break;`.
- Line 91: lower the cap from `secs>30` to `secs>15`.

## Licence verification (as asked)
- **rtlbtfw header:** `rtlbt_fw.c` and `rtlbt_fw.h` carry the full BSD-2-Clause text and the three upstream copyright lines, verbatim: Adrian Chadd 2013, Vladimir Kondratyev 2019, Future Crew LLC 2023. I compared them with FreeBSD main `usr.sbin/bluetooth/rtlbtfw/{rtlbt_fw.c,main.c}`. A NoovexOS port line was added, which is allowed.
- **NOTICE:** `drivers/bluetooth/NOTICE` correctly separates own code from the BSD-2 port and from the blobs. Linux btrtl (GPL-2.0-or-later) is named as a reference only, which is allowed under our rule.
- **Firmware licence:**
  - `assets/firmware/rtl_bt/LICENCE.rtlwifi_firmware.txt` is byte-identical to the linux-firmware text (the same file as `assets/firmware/LICENCE.rtlwifi_firmware.txt`). It is redistributable.
  - Its "no reverse engineering" clause applies to the blob only. The blobs are shipped unmodified.
  - The sha256 sums in `WHENCE.txt` match the files.
  - The rtl8822cu_config -> rtl8761bu_config link is documented.
- **Gap:** see M2. The BSD notice and the BT blob list are not installed into the image.

## Other checks that came out OK
- **Unrelated USB devices:** `btusb_probe` only touches root ports that are listed as class 0xE0 and are not claimed by HID, camera, printer, MSD, usbnet, audio or a hub.
- **Rescans:** the MSD and usbnet rescans skip a port that BT owns.
- **xHCI reset:** `xhid_reset` releases BT.
- **Event routing** is slot+DCI exact. Bulk endpoint events are swallowed, which is fine because bulk is not armed.
- **Locking:** `btu_ctrl` saves and restores the EP0 globals. `btusb_cmd`, `btusb_probe` and `btu_pump` take `xh_poll_busy` with a bounded spin and refuse when `xh_wdepth>0`.
- **Merge:** the hooks are present in the merged main, with no conflict with c1's SET_PROTOCOL changes. The tools/check.py stubs are in place.
- **Firmware download** matches btrtl/rtlbtfw: 252-byte fragments, index wrap at 0x7F to 1, 0x80 on the last fragment, the fw_version tail on V1. The ROM-LMP check covers a warm reboot with the patch already running.
- **Parser** bounds look right, and the 20k-random-event ASan run passes.
- **Hardware test still required.** In the owner's test, after a `bt scan`, run `wifi diag` and check that 0x38 has its original GNT bits back.
