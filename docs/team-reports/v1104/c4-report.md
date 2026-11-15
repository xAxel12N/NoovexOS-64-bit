# C4 report (branch w-c4)
Fixed (patches from s2/b4/b1 reports):
- ufat (drivers/usb/xhci/hid_keyboard_mouse.c): ufat_cok() bound check in ufat_next/ufat_set; ufat_alloc limited to ufat_nclus+2;
  ufat_mount rejects spc 0/non-power-of-2, resv=0, fatsz=0, meta>=ts, plba+ts wrap, rootclus out of range; nclus clamped to FAT capacity;
  root scan capped at 512 sectors, dirent_scan at 4096 sectors (cycle protection).
- ufat_write_file copy-on-write: new chain written + FAT flushed, then dirent, then old chain freed; new chain freed on every error.
- vfat: vf_loc idx<0||>=65536 -> -1; vf_slot stops growing at 65536 entries.
- power.c: power_prepare syncs nvx3 write-back (if pending) + ufat_sync; gdisk_flush also after power_prepare in off/reboot.
- nvx4.c: nvx3_flush_meta no longer clears g_fs_wb_pending on rootfs.
- storage_fat_and_hubs.c: msd_port set on enum even if FAT mount fails (F1); kdown[] cleared for held keys of unplugged receiver (F2).
- tools/check.py: hid2scan moved before xhub_remove_below in the usb harness extraction order (needed by F2).
Checks: `python3 tools/check.py --only usb` 12/12 pass; ccheck drivers/usb/xhci.c, fs/fat/vfat.c, core/power.c, fs/nvx/nvx4.c: no errors.
Not done: s2 #5 (vfat mount BPB checks), #6 (vf_mark_dir).
