# R4 review: w-c4 (8c4c8464) USB-stick FAT / power-off sync / unplug

Verdict: OK to merge. No HIGH. 1 MEDIUM (pre-existing, but the brief asks "FAT12 still mounts"), 2 LOW.

Verified good
- ufat_cok: c>=2 && c<nclus+2 -> last valid cluster = nclus+1. Correct. Bad/reserved values (0xFFF7, 0x0FFFFFF7) fall outside and read as EOC.
- BPB: bps==512, spc power of two 1..128 (u8, 0 rejected), resv/fatsz nonzero, data start < total, nclus clamped to FAT capacity (fatsz*256 / *128), FAT32 rootclus in [2, nclus+1]. Real FAT16/FAT32 sticks (resv 1/32, 2 FATs, spc 1..64, root clus 2) all pass.
- Chain caps: root walk guard*spc<=512 sectors, dirent scan <=4096 sectors (65536 entries at spc 128) - fine for any real root dir.
- ufat_write_file COW: old chain kept until the new dirent write succeeds; new chain freed + flushed on every failure path (alloc, data write, dirent read, dirent write); oc!=first guard ok.
- vfat idx cap 65536 = FAT spec dir maximum; vf_slot stops growing at it.
- power_prepare: ufat_sync only writes if the FAT cache is dirty (normally never after write_file returns), and umsd_* returns -1 immediately when msd_ok=0 and is round-bounded otherwise -> can't hang shutdown on pulled stick. nvx3_sync_all bounded. Second gdisk_flush harmless.
- nvx3_flush_meta: rootfs path now leaves g_fs_wb_pending set, so main.c:316 nvx3_sync_all still writes the file data. Correct.
- msd_port set before ufat_mount -> unplug of an unmountable stick now releases msd. Correct.
- kdown release on hub/root unplug (xhid_remove_port -> xhub_remove_below): kprevx uses same layout (byte 0 mods, 2..7 keys); hid2scan is defined before use (hid_keyboard_mouse.c included before storage_fat_and_hubs.c). check.py order change matches.

MEDIUM (pre-existing) - FAT12 sticks/images are treated as FAT16
ufat_type=16 whenever BPB_FATSz16!=0; a FAT12 volume gets 16-bit FAT reads (garbage chains) and ufat_alloc/ufat_set write 16-bit entries -> corrupts the FAT on write. Spec: FAT type is decided by cluster count. Patch (hid_keyboard_mouse.c ufat_mount, after the ents clamp, before the FAT32 rootclus check):
-      if(ufat_type==32&&(ufat_rootclus<2||...
+      if(ufat_type==16&&ufat_nclus<4085){ klog("usb: FAT12 stick not supported"); ufat_nclus=0; return; }
+      if(ufat_type==32&&(ufat_rootclus<2||...
(Refuse is safe; real FAT12 USB sticks are <16 MB and rare.)

LOW - ufat_free_chain cap 4096 leaks clusters for big files
With spc=1..8 a >2..16 MB file has >4096 clusters; both the failure cleanup and the old-chain free on overwrite stop early -> lost clusters (no corruption). Patch:
-static void ufat_free_chain(u32 clus){ int g=0; while(clus>=2&&clus<0x0FFFFFF8u&&g++<4096){
+static void ufat_free_chain(u32 clus){ u32 g=0; while(ufat_cok(clus)&&g++<=ufat_nclus){

LOW - COW needs old+new free space
Overwriting a file on a nearly full stick now returns -1 where v11.03 succeeded. Intended trade-off; just make the UI message say "stick full" rather than a generic failure.
