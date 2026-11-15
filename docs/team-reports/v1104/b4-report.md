# B4 report (v11.04) - boot / disk sync / shutdown / crash-report saving

## B4-1 (HIGH) Shutdown/restart drop pending write-back (lost data on power off)
core/power.c:76 power_prepare() (used by power_off():85 and power_reboot():100) flushes NTFS, vfat and
gdisk but never calls nvx3_sync_all(). Writes made by Linux programs are only marked by fs_wb_mark()
(fs/vfs/ramfs.c:28, called from core/proc/jobs/resume_and_fds.c:62 and sys/linux/syscalls/poll_epoll.c:119)
and committed by the main loop up to 100 ticks later (core/init/main.c:316). Sleep already does this
(core/power.c:138); power off/restart do not.
Owner notices: a file saved by a Linux program (editor, apt, etc.) right before Shut down/Restart is
missing or old after boot. Same for USB stick FAT cache if anything left it dirty.
Patch (core/power.c:76, power_prepare, put before the klog):
```
{ extern volatile int g_fs_wb_pending; extern void nvx3_sync_all(void); if(g_fs_wb_pending)nvx3_sync_all(); }
{ extern void ufat_sync(void); ufat_sync(); }
```
(gdisk_flush() runs before power_prepare in power_off/power_reboot; move the `if(g_gdisk_probed)gdisk_flush();`
to after power_prepare() in both, or add another gdisk_flush() at the end of power_prepare, so the sync's
writes reach the disk cache flush.)

## B4-2 (HIGH) ufat_write_file frees the old file before the new one exists -> CRASH.TXT / NVSTART.TXT / logs corrupted on stick-full, USB error or reset
drivers/usb/xhci/hid_keyboard_mouse.c:340 frees the existing chain (ufat_free_chain) first; the FAT cache
is one sector and ufat_load() (line 269) flushes it as soon as alloc touches another FAT sector, so the
"freed" state reaches the stick while the directory entry still points at the old chain. Every error
return at 344/346/349/351 leaves it that way (dirent -> free/reused clusters), and the dirty cache is
written later by the next write. A reset mid-write (crash/freeze path in core/crash.c:163/290/314 writes
CRASH.TXT while the system is dying) does the same.
Owner notices: CRASH.TXT on the stick contains garbage or pieces of another file (NVGSPLOG.TXT, TRACELOG),
Windows/Linux asks to "scan and fix" the stick, or NVSTART.TXT reads wrong so nvgsp auto-start is skipped
or not skipped.
Patch (copy-on-write: write new chain, point dirent at it, then free old):
```
-    if(exists){ if(umsd_read(elba,1,sec)<0)return -1; u8* d=sec+eoff; u32 oc=(d[0x1A]|(d[0x1B]<<8))|((u32)(d[0x14]|(d[0x15]<<8))<<16); if(oc>=2)ufat_free_chain(oc); }
+    u32 oc=0; if(exists){ if(umsd_read(elba,1,sec)<0)return -1; u8* d=sec+eoff; oc=(d[0x1A]|(d[0x1B]<<8))|((u32)(d[0x14]|(d[0x15]<<8))<<16); }
...
-    for(int c=0;c<nclus;c++){ u32 cl=ufat_alloc(); if(cl==0)return -1; ...
+    for(int c=0;c<nclus;c++){ u32 cl=ufat_alloc(); if(cl==0){ if(first)ufat_free_chain(first); ufat_flush(); return -1; } ...
-        ... if(umsd_write(base+s,1,ds)<0)return -1; } }
+        ... if(umsd_write(base+s,1,ds)<0){ ufat_free_chain(first); ufat_flush(); return -1; } } }
...
-    if(umsd_write(elba,1,sec)<0)return -1; ufat_flush(); ufat_mount(); return 0; }
+    if(umsd_write(elba,1,sec)<0){ ufat_free_chain(first); ufat_flush(); return -1; } if(oc>=2&&oc!=first)ufat_free_chain(oc); ufat_flush(); ufat_mount(); return 0; }
```
Also flush the FAT before the dirent write (call ufat_flush() just before `if(umsd_read(elba,1,sec)<0)`)
so the dirent never points at a chain whose FAT entries are only in RAM. Note: the chain from `first` is
always EOC-terminated (ufat_alloc marks each new cluster EOC), so freeing it on error is safe. Cost: a
rewrite needs room for old+new copy (fine for these small files).

## B4-3 (MEDIUM) nvx3_flush_meta() clears g_fs_wb_pending on rootfs without syncing dirty files
fs/nvx/nvx4.c:362: `g_fs_wb_pending=0;` happens before the g_rootfs branch, which only does nk_root_flush()
(no nk_root_sync_file of dirty files, unlike nvx3_sync_all at :363). Any dt_save() (fs/vfs/dirtree.c:41 -
called by wifi_saved_save net/wifi/wifi_mgr.c:62, mkdir, etc.) within the 1 s write-back window cancels the
pending write-back of Linux-program writes, so they stay only in RAM until some unrelated sync_all.
Owner notices (rootfs installs): files written by a Linux program are lost after a reset/power cut even
minutes later, typically right after joining a Wi-Fi network or creating a folder.
Patch:
```
-void nvx3_flush_meta(void){ if(g_fs_batch&&g_n4&&!g_rootfs)return; g_fs_wb_pending=0; if(!disk_ok)return; if(g_rootfs){ nk_root_flush(); return; } if(g_n4)n4_commit(); }
+void nvx3_flush_meta(void){ if(g_fs_batch&&g_n4&&!g_rootfs)return; if(!disk_ok)return; if(g_rootfs){ nk_root_flush(); return; } g_fs_wb_pending=0; if(g_n4)n4_commit(); }
```

## Checked, no bug found (short)
- nvgsp-auto.cfg / nvgsp-start.flag (drivers/video/nvidia/nvdraw.c:89): fs_save_bin + nvx3_sync_all each time; 2-byte file, ok.
- Wi-Fi saved file (net/wifi/wifi_mgr.c:56-62): buffer bound 7600 + max line ~420 < 8192, ux_write_text syncs (fs/vfs/unixfs.c:352); ok apart from B4-3.
- core/crash.c cx_save:161: ok; benefits from B4-2.
- Installer install_files.c:280-317 checks write errors and gdisk_flush result; ok.
- Not covered in time: UEFI boot path, boot menu, ACPI S1/s2idle details.
