# btm report: MediaTek MT79xx USB Bluetooth firmware download

Branch p-btm, commit b048db3c, worktree /home/claude/wt/btm.

## Licence
Fetched the real Linux drivers/bluetooth/btmtk.c and btmtk.h from raw.githubusercontent.com. Both start with `SPDX-License-Identifier: ISC`, `Copyright (C) 2021 MediaTek Inc.`, so the scout was right. Struct layouts and the WMT flow come from the fetched code, not from memory. The ISC header and the MediaTek copyright sit at the top of mtkbt_fw.c and mtkbt_fw.h, and there is a new entry in drivers/bluetooth/NOTICE. The USB id list was taken as facts from Linux btusb.c (GPL-2.0-or-later); no code was copied from it.

## What it does (drivers/bluetooth/firmware/mtkbt_fw.c)
- `mtkbt_match(vid,pid)`: MediaTek 0E8D:any, or one of the listed OEM MT7921/7920/7902/7922/7922A/7925 ids (13D3, 0489, 04CA, 35F5, 2C7C). Any other id returns 0 straight away, with no USB traffic and no log line. The host test checks this for 0BDA:C123.
- Reads the chip id (0x70010200), firmware version (0x80021004) and flavour (0x70010020) with vendor control IN request 0x63. Chip id 0 on a known MT6639 board is treated as 0x6639. Supported chip ids: 0x7961, 0x7922, 0x7925, 0x7902, 0x6639. Any other chip id is logged and the device is left alone. MT7935 is not supported because it needs CBMCU firmware first.
- Firmware name follows btmtk_fw_get_filename. The file is read from `/lib/firmware/<name>`, for example /lib/firmware/mediatek/BT_RAM_CODE_MT7922_1_1_hdr.bin. If the file is missing, one log line names the full path and nothing is sent. No firmware blob is committed.
- Parse: 32-byte patch header, 64-byte global descriptor (section_num at +12), then 64-byte section maps (secoffset +4, dlsize +16, dlmodecrctype +32, 52 spec bytes from +12). Each section is bounds-checked, as Linux btmtk_fw_validate_* does. For MT6639, only sections with dlmode&0xff==1 are downloaded.
- Download: for each section, a WMT PATCH_DWNLD query with 53 bytes (0 + spec). "In progress" is retried up to 20 times with 100 ms between tries. "Done" skips the section. Otherwise the data goes in chunks of up to 250 bytes with flags 1/2/3. Then a 100 ms wait, the EP reset option (uhw write 0x74011890 = 0x00010001, request type 0x5E, request 2), and WMT FUNC_CTRL on.
- WMT transport: the HCI command 0xFC6F goes out through btusb_cmd. The event is polled with control IN (0xC0, request 1, wValue 48, 64 bytes) every 500 us. The interrupt endpoint is also drained, and a 0xE4 event from it is accepted too. Every wait has a timeout: 2 s per WMT command, 3 s for function on. If the adapter disappears, it stops at once. The UI is pumped every 20 ms, the same way hci.c bt_idle does it.

## Hooks
- btusb.c: one line in `btusb_probe()` after the lock is released: `if(btu.ok)mtkbt_attach(btu.vid,btu.pid);`. bti and btb can add their own line next to it. The setup runs before hci.c `bt_up` sends Read Local Version, which an MT79xx without firmware may not answer.
- btusb.c: a new public `btusb_vctrl(rt,req,val,idx,buf,len)` for generic EP0 vendor transfers, declared in include/nvx/drivers/bluetooth/bt.h. It uses the existing btu_lock and btu_ctrl. bti and btb may want to use it as well.
- units.list: added drivers/bluetooth/mtkbt_fw.c.

## Files
- drivers/bluetooth/firmware/mtkbt_fw.c, drivers/bluetooth/firmware/mtkbt_fw.h (new)
- drivers/bluetooth/transport/btusb.c, include/nvx/drivers/bluetooth/bt.h, units.list, drivers/bluetooth/NOTICE
- tools/tests/bluetooth/mtk_main.c (new), tools/tests/bluetooth/run.sh (now also builds and runs the MTK test)

## Testing
- `/home/claude/ccheck.sh drivers/bluetooth/firmware/mtkbt_fw.c net/bluetooth/hci.c` is clean. drivers/usb/xhci.c (which includes btusb.c) has 47 warnings before and after, so no new ones.
- Host test mtk_main.c (ASan+UBSan): 46/46 checks pass. It covers id matching (Realtek, Intel and Broadcom ids are not matched), file names for all chips, the parser with every rejection case (truncation, overflow, zero or huge section count), the WMT status mapping, and a full simulated-device download. In the simulated download the device receives every section byte for byte with the correct flag sequence, a busy reply is retried, a section the device reports as already present is skipped, and the EP reset and function on are sent. Further cases: missing firmware (clear message, nothing sent), unsupported chip, a silent chip (bounded 2 s timeout), adapter gone, the MT6639 fallback, and a damaged file (rejected before anything is sent).
- `python3 tools/check.py --only bt`: passes.

## Untested / risks
- Not tried on any real MediaTek hardware (the owner has none) or in QEMU.
- xh_ctrl does not report the actual transfer length. An empty WMT poll is detected by zero-filling the buffer first and checking for 0xE4. This assumes a short data stage without ISP still completes with cc 1 or 13; the existing code relies on that too.
- The firmware loader uses vfs_open/vfs_read in a loop (limit 4 MiB). It was not tried in the kernel with a real file under /lib/firmware.
- If the firmware download fails, btu stays claimed and hci.c then reports "controller does not answer HCI commands". hci.c `bt status` does not show the MediaTek firmware message (bt.fwmsg) yet.

## Follow-ups
- Expose the last mtkbt message in `bt status` (bt.fw/bt.fwmsg).
- build.sh line 305: copy the ISC header to usr/share/licenses/bluetooth/LICENSE.btmtk-ISC. I did not touch it, to avoid a merge conflict with bti and btb.
- MT7935 CBMCU path, subsystem reset/retry (btmtk_usb_subsys_reset), ISO interface, MSFT opcode 0xFD30.
- Possibly share the /lib/firmware loader (mtk_load) with bti and btb.
