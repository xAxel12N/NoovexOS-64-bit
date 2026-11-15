# c4 report: storage and data-loss fixes (branch x-c4, worktree /home/claude/wt/c4)

There are 11 commits on top of v11.06 (dae38348). 10 files changed, +64/-42.

## Applied (each item was checked against the real code first)
1. **h9#1 power sync**: `core/power.c:70` (power_prepare) and `:138` (power_s2idle) now always call `nvx3_sync_all()`. Before, they only did it when `g_fs_wb_pending` was set.
2. **h9#2 installer "not enough memory" after the wipe**: `core/init/installer/install_files.c:253`, before `disk_ok=0`.
   - Sets `g_fs_pin=1` and calls `fs_ensure()` on every lazy file. If that fails, the installer stops with ip_fail before anything is written to disk.
   - `ip_run` (`:243`) saves the old `g_fs_pin` value and restores it afterwards.
3. **h9#3 AHCI recovery**: new `ahci_fail(P)` in `drivers/storage/gdisk.c:10-13`. Every error and timeout return in `ahci_cmd` and `ahci_cmd48` now calls it. It:
   - stops the port (clears ST and FRE),
   - clears PxSERR and PxIS,
   - does a COMRESET via PxSCTL if TFD still shows BSY/DRQ (this goes beyond the h9 patch),
   - restarts the port.
4. **h9#4 4Kn NVMe**: I checked that the gdisk layer is 512-byte addressed (nvme_read1/write1 divide by nvme_ratio themselves).
   - `fs/ntfs/ntfsk.c` nk_rd/nk_wr now use `r=1`.
   - `total` at `:45` and `:155` is now `g_dksectors`, not `*r`.
   - `fs/vfs/parts.c:47,:109` sectors are now `g_dksectors`. This also fixes the backup GPT lookup at `total-R`.
5. **cs4#10 GPT entries**: `fs/ntfs/ntfsk.c:55` rejects an entry size that is not a power of two. `:61` skips entries with `lz<f`.
6. **h9#5 fs_sync_step**: in `fs/nvx/nvx4.c`, `g_sk/g_shi/g_spos` moved up to the globals and a new `n4_step_drop()` was added.
   - `fs_reloadable` refuses to evict file `g_sk`.
   - When a stepwise save is abandoned, the unwritten range `[g_spos,g_shi)` is marked dirty again. This happens in nvx3_sync, n4_commit, a step on a different file, and the early returns in fs_sync_step.
   - The report's plain `g_sk=-1` would have lost that range. I extended the fix for that reason.
7. **h9#6 crash save on a stuck disk**:
   - `drivers/storage/nvme.c`: the public `gdisk_read/write/writen/flush` are now wrappers that raise and lower an atomic `volatile int g_disk_busy` around the `_raw` bodies.
   - `n4_commit` is wrapped the same way.
   - `core/crash.c:266` and `:371` (2 lines only): `g_cx_nofs=g_heap_busy||g_disk_busy`.
8. **h9#7 conversion reservations**: the table in `fs/nvx/nvx4.c` is now heap-allocated, starts at 256 entries and doubles. It was a fixed 64.
   - Adjacent ranges are merged.
   - If memory runs out, `g_n4_nres` becomes -1. Then `n4_conv_backup` and `n4_format` fail before writing anything, instead of silently dropping reservations.
9. **cs4#7 NTFS endless loop**: `fs/vfs/volumes.c:58` uses `l>rsz-a`. Also in that file:
   - the run list is bounded (attribute length >= 0x40, run offset < length, `rl+ln+of<=end`),
   - shift counts read from disk are guarded: NTFS spc, NTFS clusters-per-record, and exFAT `s[108]` in 9..12 with `s[108]+s[109]<=25`.
10. **cs4#8 vfat spilling into the next partition**:
    - `fs/fat/vfat.c:184` rejects a volume whose metadata is larger than ts. The check uses 64-bit math.
    - `vf_rs` and `vf_ws` refuse I/O outside `nsect`.
    - `vf_format` now sets `g_vfat.nsect`. Without this the new guard would compare against a stale value.

## Skipped
- **h9#8 ufat_free_chain**: it is in drivers/usb/xhci/hid_keyboard_mouse.c, which is not in my file list.
- **crash.c USB save while the main disk is a busy USB stick**: I kept the crash.c change minimal, as asked, so this is not guarded.

## Tests
- `/home/claude/ccheck.sh` passes on every touched unit, with no new errors: power.c, installer.c (parent of install_files.c), gdisk.c, nvme.c, ntfsk.c, parts.c, volumes.c, vfat.c, nvx4.c, crash.c.
- `python3 tools/check.py --only fs` passed all 3 checks (47 NVX4 checks). These include power cuts, old-format conversion and the stepwise-save power cut.
- I did not touch USB or input, so the usb group was not run.
- None of the changes ran on real hardware or in QEMU.

---
# Fix round (branch y-c4, on top of v11.07 01c10690). Covers r4-review and r10 MEDIUM 2.

## Changes by review item
- **r4 HIGH 1 (preload read the wrong disk):**
  - `disk_picker.c` ip_pick_disk now pins (`g_fs_pin=1`) and loads every lazy file **before** `gdisk_pick` switches disks.
  - `install_files.c:253`: if any file is still lazy while `!disk_ok`, or a load fails, the installer stops before anything is written ("Nothing was written").
  - Added the reviewer's defence in depth. A new `g_fs_src_disk` records which disk the filesystem was mounted from. It is set in n4_mount, n4_format and the ntfsk rootfs mount.
  - `fs_backend_load` refuses to read when `gdisk_current()` is not that disk. `g_clone_src` still takes priority.
- **r4 HIGH 2 (re-commit after a panic):** `power.c` power_prepare and power_s2idle skip the filesystem sync when `g_cx_panicking && (g_heap_busy||g_disk_busy)`. This is different from the review's patch.
  - I limited the skip to panics so that a leaked counter (r4 LOW 2) can never block the sync on a normal shutdown.
  - To make this possible, `g_cx_panicking` in crash.c:274 is no longer `static`. That is the only crash.c change.
- **r4 MEDIUM 1 (installer service):** `usb_clone_service.c` is_step0 now runs the same preload-or-fail-safe for the non-clone installer, before `disk_ok=0`.
- **r4 MEDIUM 2 (g_disk_busy gaps):** `nvx4.c` now also raises the counter around:
  - `nvx3_sync`, `fs_sync_step`, `nvx3_sync_all`, `nvx3_flush_meta` (renamed `*_raw` with wrappers),
  - `n4_rd` and `n4_wr` (this covers the IDE path).
  - Still not covered: vfat writes made directly by the Files app, outside the rootfs sync.
- **r4 LOW 1:**
  - ahci_fail gives up (sets `ahci_ok=0` and `ahci_port=0`) if the HBA does not stop, so it can't be hit by a late DMA.
  - After a COMRESET it waits for BSY/DRQ to clear before restarting the port.
- **r10 MEDIUM 2:** gdisk.c `ahci_shutdown_all`:
  - It no longer sends STANDBY to a unit whose FLUSH failed.
  - On reboot it restores the port that was selected before.

## Not done
- **r4 LOW 2** (reset the counter at the longjmp landings): no longer needed, because of how HIGH 2 is gated now.
- **r4 LOW 3** (new two-disk and panic tests in fs_glue): not written.

## Tests
- ccheck passes on all touched units.
- `python3 tools/check.py --only fs` passes 3/3 (47 NVX4 checks).
- These fixes were not run in QEMU or on hardware.
