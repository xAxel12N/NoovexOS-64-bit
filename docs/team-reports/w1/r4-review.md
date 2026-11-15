# r4 review of c4 (branch x-c4, merged in v11.07): storage and data-loss fixes

I read every hunk of `git log -p v11.06..x-c4`, the c4 report, the c4 task and the h9 report, and checked each change against the merged files in main (01c10690).

**Result:** 2 HIGH, 2 MEDIUM, 3 LOW. The other changes are correct:
- volumes.c NTFS attribute walk, run list and shift guards
- vfat nsect bounds and metadata check
- ntfsk/parts 4Kn math. I verified that `g_dk_cs`/`g_dksectors` hold 512-byte units (`nvme.c:152 sec512`) and that `part_scan_disk` scales GPT/MBR LBAs by `g_bd[].ratio`.
- GPT entry checks
- the step-drop logic in nvx4 (it fixes a real loss of the unwritten range on a commit in the middle of a step)
- the reservation table growth (it also fixes the conversion backup zone not being reserved once 64 entries were full)

Other coders' changes in the same files: `gdisk_shutdown`/`ahci_shutdown_all` (gdisk.c:103, nvme.c:256) now go through `ahci_fail` when a FLUSH errors. Each port then gets a stop/start, plus a COMRESET if it is busy. That can be slow, but it loses no data. The crash.c changes from c2 merged cleanly next to c4's 2 lines (crash.c:266, :402).

---

## HIGH 1: installer preload reads the WRONG disk when the target disk is not the files' disk, and copies garbage into the user's files without any error
`core/init/installer/install_files.c:253` together with `core/init/installer/disk_picker.c:2` and `fs/nvx/nvx4.c:318-326`.

**How it happens:**
- `ip_pick_disk()` runs before `ip_run2`. It calls `gdisk_pick(i)`, which switches `g_dktype`, the AHCI port and the IDE unit to the target disk. When the disk changes it also sets `disk_ok=0`.
- c4's preload then calls `fs_ensure(k)` on every lazy file, which calls `fs_backend_load`, which calls `n4_read_file`. That reads `g_nvx_base + extent` on the **newly picked disk**.
- The only guard in `fs_backend_load` is `g_clone_src`, and it is -1 outside the clone flow.
- NVX4 file data has no checksum, so the read "succeeds" and returns arbitrary sectors from the target disk.
- `n4_format` (`install_files.c:301`, ip_copy and NTFS copy too) then writes that garbage into the new install as the user's documents.
- The RAM copies are left `clean=1`. If the user cancels and goes back to the old disk, any later partial write to such a file writes garbage blocks to the original disk.

**Before v11.07:** `n4_format` failed ("not enough memory") because the extents were empty, so no garbage was copied. This is a regression from a fail-safe state to silent corruption.

**Patch:** preload while still on the source disk, and refuse in ip_run2 if the files are already unreachable.
```c
/* core/init/installer/disk_picker.c:2 */
static void ip_pick_disk(int i){ extern int gdisk_count(void); extern int gdisk_pick(int); extern int gdisk_current(void); extern int disk_ok; extern int g_nvx_generic; extern int g_fs_pin;
    if(i<0||i>=gdisk_count())return;
    if(i!=gdisk_current()&&disk_ok){ int op=g_fs_pin; g_fs_pin=1; for(int k=0;k<FS_N;k++){ if(!FS[k].used||FS[k].orphan||!FS[k].lazy)continue; if(fs_ensure(k)){ g_fs_pin=op; return; } } }
    int pr=gdisk_pick(i); if(pr>0){ if(disk_ok&&(g_nvx_generic||g_dktype==DK_IDE))disk_ok=0; } dm_load(); ip_load(); ip_mode=0; ip_part=-1; }
```
```c
/* core/init/installer/install_files.c:253 */
    { extern int g_fs_pin; extern int disk_ok; g_fs_pin=1; for(int k=0;k<FS_N;k++){ if(!FS[k].used||FS[k].orphan||!FS[k].lazy)continue; if(!disk_ok||fs_ensure(k)){ ip_fail(disk_ok?"Not enough memory to copy the current files.":"Your current files are on another disk and could not be read.","Restart, pick the disk again, or free memory."); return; } } }
```
**Defence in depth (recommended):** in `fs_backend_load` (nvx4.c:323), generalise the `g_clone_src` guard to a "source disk" index that is recorded at mount time (`n4_mount`/rootfs mount: `g_fs_src_disk=gdisk_current();`). The guard then reads `int src=g_clone_src>=0?g_clone_src:g_fs_src_disk; if(src>=0&&gdisk_current()!=src){ ...return -1; }`.

## HIGH 2: an always-on sync in power_prepare re-commits after a panic inside the filesystem, which writes a corrupt NVX4 metadata generation
`core/power.c:70` (power_prepare), reached from `core/crash.c:225,226,231`.

**How it happens:**
- The panic box calls `power_reboot()` when the user presses R or the countdown runs out.
- Before c4, `power_prepare` synced only `if(g_fs_wb_pending)`. `nvx3_sync_all` clears that flag as its first statement (nvx4.c:376), so a panic inside `nvx3_sync_all` or `n4_commit` did not re-commit on reboot.
- Now it always calls `nvx3_sync_all()`, which runs `n4_commit()` again on top of the half-finished in-memory serialize (`g_n4_img`, bitmap) or a half-issued disk command. Interrupts are off and the heap may be locked (`g_heap_busy`).
- The second commit writes a fresh metadata copy. Its CRC is valid because it is computed over the corrupt image. The superblock then points to that copy, so the filesystem is corrupted and the previous good A/B generation is gone.
- c4 set up exactly the right signal (`g_disk_busy` stays above 0 after a panic inside a command or a commit) but did not use it here.

**Patch (core/power.c:70):**
```c
static void power_prepare(void){ extern volatile int g_heap_busy, g_disk_busy; int fs_ok=!g_heap_busy&&!g_disk_busy;
    { extern int nvgsp_shutdown(void); nvgsp_shutdown(); }
    if(fs_ok){ extern void nvx3_sync_all(void); nvx3_sync_all(); } else klog("power: disk or heap busy - skipping filesystem sync");
    if(fs_ok){ extern void ufat_sync(void); ufat_sync(); } klog("power: flushing filesystems");
    if(fs_ok&&nvxntfs_mounted())nvxntfs_umount(); if(fs_ok&&vf_mounted())vf_umount(); }
```
In power_s2idle at line 138, use the same guard: `{ extern volatile int g_heap_busy, g_disk_busy; extern void nvx3_sync_all(void); if(!g_heap_busy&&!g_disk_busy)nvx3_sync_all(); }`.

## MEDIUM 1: the normal installer program (service path) still has the h9#2 bug
`core/init/installer/usb_clone_service.c:42` (is_step0).

**How it happens:**
- When `g_inst_clone==0`, service 203 calls `ip_pick_disk` and service 204 starts the steps.
- `is_step0` sets `disk_ok=0` with no lazy preload. Then `n4_format(g_is_dsz)` at line 91 fails with "not enough memory" after the MBR, stage2 and boot partition have already been written (lines 69-78).
- h9 said that usb_clone_service "does this right", but that is only true for the clone preparation at lines 5-12.
- If this is the owner's "installer program", c4's fix does not reach the owner's path.

**Patch:** apply HIGH 1's `ip_pick_disk` preload, because service 203 also goes through `ip_pick_disk`. In `is_step0`, before line 42, add:
```c
    if(!g_inst_clone){ extern int g_fs_pin; extern int disk_ok; for(int k=0;k<FS_N;k++){ if(!FS[k].used||FS[k].orphan||!FS[k].lazy)continue; if(!disk_ok||fs_ensure(k))return is_fail("Your current files could not be loaded into memory.","Restart, pick the disk again, or free memory."); } g_fs_pin=1; }
```
`g_fs_pin` is already reset at line 208.

## MEDIUM 2: g_disk_busy only covers single commands and n4_commit, not filesystem operations in progress
`core/crash.c:266,402`, `drivers/storage/nvme.c:164-209`.

**How it happens:**
- The freeze save runs in the timer IRQ and the panic save runs on the panic path. Both can interrupt the main thread **between** two gdisk calls:
  - inside `fs_sync_step` → `n4_write_range` (not wrapped);
  - inside rootfs `nk_root_sync_file`, where nk_wr issues one `gdisk_write` per sector and the counter drops to 0 between sectors;
  - inside vfat writes;
  - inside legacy IDE `ata_writen` (n4_wr when `!g_nvx_generic` bypasses gdisk entirely).
- `cx_save` then calls `fs_save_bin` and `nvx3_sync_all`, and changes NTFS, FAT or NVX metadata halfway through someone else's update. With an NTFS rootfs that is the owner's Windows partition.
- This hole existed before v11.07 and is not a regression, but the fix is narrower than its title says.

**Patch:** count whole entry points, not just commands. In `fs/nvx/nvx4.c`, wrap the bodies of `fs_sync_step`, `nvx3_sync` and `nvx3_sync_all` with the same `__atomic_add_fetch(&g_disk_busy,1,...)`/`sub` pair. Rename the existing bodies to `*_raw` exactly as was done for `n4_commit`. The counter is a counter, so nesting with n4_commit is fine. Do the same around `n4_wr`/`n4_rd` so the IDE path is covered.

## LOW 1: AHCI port can be restarted while the HBA is still running the timed-out command
`drivers/storage/gdisk.c:11-13`.

**How it happens:**
- If `ahci_port_stop` times out with PxCMD.CR still set (a hung HBA), `ahci_port_start` rewrites CLB/FB and sets ST anyway.
- A late DMA from the old READ can then land in AHCI_BUF after the caller has filled it with data for the next WRITE, and those wrong bytes go to disk.
- This is still much better than v11.06, which re-issued on a busy slot.

**Patch:** in `ahci_fail`, after `ahci_port_stop(P);` add `if(P[0x18/4]&((1u<<15)|(1u<<14))){ ahci_ok=0; ahci_port=0; return -1; }`. This fails all further I/O instead of risking a stale DMA. Optionally also wait for `!(P[0x20/4]&0x88)` before `ahci_port_start`, because the spec says not to set ST while BSY/DRQ is set.

## LOW 2: g_disk_busy never comes back down if a disk call is left with a longjmp
`arch/x86_64/interrupts/interrupts.c:71`, `arch/x86_64/user/ring3.c:73`.

- A process killed in the middle of a syscall that is inside gdisk leaves the counter above 0 for good.
- After that, crash reports are never saved to Files, and with the HIGH 2 patch, shutdown would skip the sync.
- **Patch:** reset `g_disk_busy=0` at the `k_longjmp` landing points (ring3.c after `k_setjmp` returns non-zero, interrupts.c:71), only when the target is the kernel main loop.

## LOW 3: the claimed tests do not exercise the risky paths
- `check.py --only fs` covers NVX4 power cuts and the stepwise save.
- It does not cover:
  - installer preload (HIGH 1, MEDIUM 1)
  - panic → reboot (HIGH 2)
  - AHCI fail
  - 4Kn
- **Suggestion:** add a fs_glue test with two fake disks: pick disk 1, run the installer preload, and expect a failure, not loaded data. Add a second test that calls `power_prepare` with `g_disk_busy=1` and checks that no metadata write happens.
