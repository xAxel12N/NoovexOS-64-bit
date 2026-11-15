# h9 — fs / storage / power / installer / crash-save hunt (v11.06)

Scope read: fs/nvx/nvx4.c (all), fs/nvx/nvxfs.c, fs/vfs/ramfs.c (evict/ensure), fs/vfs/vfs.c write path,
drivers/storage/{gdisk.c (AHCI), nvme.c (NVMe + gdisk layer + buffer cache), ata.c, ahci.c}, fs/ntfs/ntfsk.c (block layer,
nk_install), core/power.c (all), core/init/installer/install_files.c (ip_run2), usb_clone_service.c (prep), core/crash.c
(cx_save, freeze_tick save path), ufat_* in drivers/usb/xhci/hid_keyboard_mouse.c, fs/pkg/apt/deb_archive.c batch path.
Skipped: what v1104/s2-report already fixed (ufat cluster bounds, BPB checks, vfat cycles).

---------------------------------------------------------------------------------------------------------------------

## 1. HIGH (data loss) — shutdown/restart/sleep skip the disk sync unless a timed write-back is pending
core/power.c:70 (power_prepare) and core/power.c:138 (power_s2idle)

`if(g_fs_wb_pending)nvx3_sync_all();`  — g_fs_wb_pending is only set by fs_wb_mark(), which runs only when a file
descriptor is CLOSED (core/proc/jobs/resume_and_fds.c:62) or by the mmap write-back (poll_epoll.c:119).
vfs_write() (fs/vfs/vfs.c:164) only sets g_fs_dirty[k]=1 and makes FS[k].clean=0 — it does NOT set wb_pending.
Scenario: a Linux app (editor, game save, browser profile, sqlite, log writer) has a file open and has written to it;
the user clicks Shut down / Restart (or the battery-empty path at power.c:155). g_fs_wb_pending==0, so nvx3_sync_all
is skipped, power_prepare even unmounts NTFS/FAT (rootfs installs) without syncing -> everything written since the
last close/sync is lost. Same for a write-back that was already consumed: main.c:316 clears the flag when it runs, so
any write after that and before the next close is never flushed at power off.
nvx3_sync_all -> n4_commit is cheap when nothing is dirty (serialise + diff against the shadow, no disk writes).
Patch (both lines):
  power.c:70   `{ extern volatile int g_fs_wb_pending; extern void nvx3_sync_all(void); if(g_fs_wb_pending)nvx3_sync_all(); }`
           ->  `{ extern void nvx3_sync_all(void); nvx3_sync_all(); }`
  power.c:138  `{ extern volatile int g_fs_wb_pending; extern void nvx3_sync_all(void); if(g_fs_wb_pending)nvx3_sync_all(); }`
           ->  `{ extern void nvx3_sync_all(void); nvx3_sync_all(); }`
Confidence 85%.

## 2. HIGH (install fails, disk already wiped) — reinstalling onto a machine with a mounted NoovexOS disk always fails
core/init/installer/install_files.c:298-300 + fs/nvx/nvx4.c:293 + fs/nvx/nvx4.c:312

disk_init() runs at every boot (core/init/main.c:252); n4_mount leaves every file `lazy` (nvx4.c:242, data not in RAM).
ip_run2 then (line 281/280) writes the new MBR + boot partition, sets g_nvx_base=dst and calls n4_format(dsz).
n4_format calls n4_alloc_state() (clears all extents, g_n4nx[k]=0) and sets g_n4=1, THEN calls fs_ensure(k) for each
file -> lazy -> fs_backend_load -> n4_read_file(k) with zero extents -> n4_map run=0 -> -1.
Result: "Formatting the data partition failed. not enough memory" every time the ISO/installer is run on a PC that
already has a NoovexOS data partition (exactly the owner's reinstall case), after the partition table and boot
partition are already overwritten. (usb_clone_service.c:8-11 does this right: it pre-loads every lazy file with
g_fs_pin=1 before switching disks; the normal installer does not.)
Patch — install_files.c, insert before line 253 (`{ extern int disk_ok; ... disk_ok=0; g_fs_wb_pending=0; }`):
  { extern int g_fs_pin; g_fs_pin=1; for(int k=0;k<FS_N;k++){ if(!FS[k].used||FS[k].orphan||!FS[k].lazy)continue;
      if(fs_ensure(k)){ g_fs_pin=0; ip_fail("Not enough memory to copy the current files.","Restart without the old disk mounted, or free memory."); return; } } }
and after line 300/302 (end of the NVX branch) and in every ip_fail return after the insert: `g_fs_pin=0;`
(simplest: in ip_run(): `g_frz_hold=1; ip_run2(); g_frz_hold=0;` -> `g_frz_hold=1; ip_run2(); g_frz_hold=0; { extern int g_fs_pin; g_fs_pin=0; }`).
Note the same lazy-file problem exists for g_ip_fs!=0 (nk_install -> fs_ensure at ntfsk.c:184 after g_n4 state is
untouched there, so it reads from the OLD partition which ip_mode==0 may already have overwritten) — the pre-load
fixes both. Confidence 80% (verified by reading; not run).

## 3. HIGH (silent corruption / dead disk) — AHCI never recovers a port after a timeout or task-file error
drivers/storage/gdisk.c:156 (ahci_cmd) and :171 (ahci_cmd48)

On TFES (PxIS bit30, e.g. one unreadable sector) or after the 20M-poll timeout, ahci_cmd just returns -1. Per AHCI
spec the HBA has stopped (PxCMD.CR) and PxCI bit 0 stays set until software clears ST and restarts the port.
Consequences: (a) after one read error every later command does `P[0x38/4]=1` on a slot that is still set, then
polls 20M times -> each disk access takes ~10-20 s -> UI freezes, freeze box, every pending save fails = data loss;
(b) after a timeout where the old command completes late, the NEXT command sees CI clear and returns 0 although its
FIS/PRDT were never executed -> a write is silently dropped / a read returns whatever is in AHCI_BUF (n4 metadata or
file data built from garbage). NVMe has recovery (nvme_bad -> nvme_init); AHCI has none.
Patch — gdisk.c: add a forward declaration + recovery helper above ahci_cmd (after line 140):
  static void ahci_port_stop(volatile u32* P); static void ahci_port_start(volatile u32* P);
  static int ahci_fail(volatile u32* P){ ahci_port_stop(P); P[0x30/4]=0xFFFFFFFFu; P[0x10/4]=0xFFFFFFFFu; ahci_port_start(P); return -1; }
line 156: `{ int ok=0; for(int t=0;t<20000000;t++){ if(!(P[0x38/4]&1)){ ok=1; break; } if(P[0x10/4]&(1u<<30))return -1; } if(!ok)return -1; }`
      ->  `{ int ok=0; for(int t=0;t<20000000;t++){ if(!(P[0x38/4]&1)){ ok=1; break; } if(P[0x10/4]&(1u<<30))return ahci_fail(P); } if(!ok)return ahci_fail(P); }`
line 157: `if(P[0x20/4]&0x01)return -1;` -> `if(P[0x20/4]&0x01)return ahci_fail(P);`
line 171: same replacement of both `return -1;` inside the poll and `if(!ok)return -1;` with `return ahci_fail(P);`
(ahci_port_start already rewrites CLB/FB and sets FRE+ST; PxSERR at 0x30 is RW1C.) Confidence 80%.

## 4. MEDIUM (corruption on 4K-native NVMe) — NTFS/FAT block layer double-converts LBAs when dk_ratio()>1
fs/ntfs/ntfsk.c:26-38 (nk_rd/nk_wr), :45 and :155 (total)

The gdisk layer is 512-byte addressed for every disk type (nvme_read1/nvme_write1 do lba/nvme_ratio themselves,
nvme.c:59-64; nvme_sectors is already in 512 units, nvme.c:152). nk_rd/nk_wr with r>1 do `blk=L/r;
gdisk_read(blk,1,g_nk_blk)` -> reads/writes 512-byte LBA L/8 instead of L, and copies g_nk_blk[off..off+511] for
off up to 3584, i.e. bytes never read. nk_wr writes the sector to LBA L/8.
Scenario: installer with NTFS/FAT32/FAT16 (g_ip_fs!=0) on a 4Kn NVMe: nk_install -> format writes the new FS at
dst/8 (inside the boot partition / gap just written) -> "Verification failed" or a broken install; nk_root_try on
such a disk reads garbage. Also `total=g_dksectors*r` overstates the disk 8x (whole-disk NTFS bounds).
Patch:
  ntfsk.c:26  `u32 r=dk_ratio(); u8* o=(u8*)buf;`        -> `u32 r=1; u8* o=(u8*)buf;`
  ntfsk.c:33  `u32 r=dk_ratio(); const u8* in=(const u8*)buf;` -> `u32 r=1; const u8* in=(const u8*)buf;`
  ntfsk.c:45  `unsigned long long total=(unsigned long long)g_dksectors*r;` -> `unsigned long long total=(unsigned long long)g_dksectors;`
  ntfsk.c:155 same `total` change.
(Keep `st*r` / `elba*r` — MBR/GPT entries are in native-sector units, that part is right.) Same overstatement in
fs/vfs/parts.c:47 and :109 (`(u64)g_dksectors*r`) -> `(u64)g_dksectors` (display/bounds only). Confidence 80%;
only affects 4Kn NVMe (rare; most NVMe ship 512e).

## 5. MEDIUM-LOW (file corruption) — fs_sync_step marks the file clean before its blocks are written
fs/nvx/nvx4.c:351 with fs/vfs/ramfs.c:31-38 (fs_evict) and nvx4.c:317-322 (fs_reloadable)

fs_sync_step (used by the browser web-cache writer, frames.c:221-223, 12 ms per main-loop pass) sets
FS[k].clean=1 at setup, then writes 32 blocks per call over many passes. Between passes, any kmalloc failure runs
fs_evict(): the file is clean, not open, and fs_reloadable() is true when the size did not need a new extent
(same-size rewrite -> g_n4indd[k]==0). Its RAM copy is freed and marked lazy. Next pass: line 339 sees lazy ->
nvx3_sync -> n4_sync_file does nothing -> n4_commit: on-disk file = half new, half old data. Also
nwk_webcache_clear() (frames.c:233) abandons a write and fs_save_bin()s the same file, leaving g_sk==k with stale
g_spos/g_shi, so the next save skips setup and writes a stale range (fails with -1 if the file shrank).
Patch:
  move line 335 `static int g_sk=-1; static u32 g_shi,g_spos;` up to just after line 41 (globals), then
  nvx4.c:318 `if(g_fs_pin||FS[k].orphan||!disk_ok)return 0;` -> `if(g_fs_pin||FS[k].orphan||!disk_ok||k==g_sk)return 0;`
  nvx4.c:330 `if(g_fs_batch)return k;` -> `if(g_sk==k)g_sk=-1; if(g_fs_batch)return k;`
Confidence 70% (needs memory pressure; the web cache has a hash so today the visible effect is a lost cache, but the
mechanism is generic for any future fs_sync_step user).

## 6. MEDIUM-LOW (rare) — crash/freeze report saving from the timer IRQ can run storage I/O on top of a stuck one
core/crash.c:369-371 (freeze_tick -> cx_save -> fs_save_bin + nvx3_sync_all, and ufat_write_file)

freeze_tick runs in the timer interrupt. If the main loop is stuck for 30 s inside a disk wait without the BKL held
(the AHCI poll in ahci_cmd is 20M MMIO reads ~10-20 s; the ufat/USB-MSD path; n4_commit itself), cx_save issues new
commands on the same AHCI slot 0 / same AHCI_BUF / same NVME_BIG buffer / same g_n4blk / g_n4_img while the
interrupted code still owns them; when it resumes ("system recovered") it writes whatever is now in the shared
buffer to its LBA. Only g_heap_busy and "rip in USB code" are checked.
Patch (cheap guard): add a global `volatile int g_disk_busy;` incremented/decremented around gdisk_readn/gdisk_writen/
gdisk_read/gdisk_write/gdisk_flush bodies and n4_commit, and in crash.c:371
  `g_cx_nofs=g_heap_busy;` -> `{ extern volatile int g_disk_busy; g_cx_nofs=g_heap_busy||g_disk_busy; }`
  (same at crash.c:266 for the panic path). Confidence 55% — depends on whether the main loop holds the BKL during
disk I/O (if it always does, bkl_try() already blocks this).

## 7. LOW (conversion only) — old-format -> NVX4 conversion reserves at most 64 old extents
fs/nvx/nvx4.c:256-257 (g_n4_res[64]), callers nvxfs.c:100, unixfs.c:54

n4_reserve_secs silently drops reservations after 64 files, so n4_format may place new data over old files' blocks.
Files are in RAM so a clean conversion is fine, but if power is lost mid-conversion n4_conv_restore only restores
the metadata zone and the old filesystem now points at overwritten data. Patch: make the table FS_N+64 entries
(`static u32 g_n4_res[FS_N+64][2];` and `g_n4_nres>=64` -> `g_n4_nres>=FS_N+64`, also nvx4.c:277 `g_n4_nres<64` ->
`g_n4_nres<FS_N+64`). Confidence 75%, impact rare (one-time upgrade from pre-NVX4 disks).

## 8. LOW — ufat_free_chain caps at 4096 clusters
drivers/usb/xhci/hid_keyboard_mouse.c:344 (`g++<4096`). Overwriting a stick file larger than 4096 clusters
(16 MB at 4 KiB clusters, e.g. a big TRACELOG/report) leaks the rest of the old chain as lost clusters (free space
shrinks, Windows chkdsk complains). Patch: `g++<4096` -> `g<(int)(ufat_nclus+2)&&g++>=0`, or simply
`g++<0x400000` (ufat_cok already bounds every step). Confidence 85%, impact low.

## Checked, no bug found
- n4_commit ordering: data -> barrier -> meta A -> barrier -> meta B; frees deferred until meta written; torn copy
  rejected by CRC; higher gen chosen. Indirect block is copy-on-write. Disk-full rollback in n4_resize is correct.
- n4_mount extent claims/bounds, n4_read_file/n4_write_range partial-block math.
- NVMe PRP list/BIG buffer sizes vs nvme_maxpg (<=16 pages), cid matching, re-init after nvme_bad.
- AHCI_BUF (0x183000) + 360 sectors ends exactly at NVME_ASQ (0x1B0000); gdisk_readn/writen chunk at 128.
- gdisk_read_cached is only used by nvx_rd (MBR/superblock probing); multi-sector writes invalidate it; the single
  gdisk_write callers (installer/disk manager) don't go through nvx_rd afterwards before a remount.
- deb_archive g_fs_batch is always reset (no early return between set and clear).
- ufat_write_file order (data -> FAT -> dirent -> free old chain) is crash-safe enough.
