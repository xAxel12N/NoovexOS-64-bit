# Scout sc6: storage and file systems (v11.06)

## 1. What NoovexOS has today (checked in the code, not only the docs)

| Area | State | Where |
|---|---|---|
| Disk drivers | ATA PIO, AHCI (all ports, RAID-class), NVMe (polled, 1 I/O queue, multi-block PRP, VMD, BAR >4 GB), VirtIO-blk | `drivers/storage/gdisk.c` (real AHCI), `nvme.c` (NVMe + generic `gdisk_*` layer) |
| NVMe extras | Flush (opc 0) yes. No shutdown notification (CC.SHN), no TRIM/DSM, no APST | `nvme.c:73,185` |
| USB mass storage | Bulk-Only Transport only. ONE device at a time (globals `msd_ok/msd_slot`), LUN 0 only (no GET MAX LUN), READ(10)/WRITE(10) only (32-bit LBA, max 2 TB), only 512-byte sectors (`msd_bsize!=512` refused), no REQUEST SENSE / media-change handling, no UAS | `drivers/usb/xhci/hid_keyboard_mouse.c:159-270`, `storage_fat_and_hubs.c` |
| USB stick file system (`/mnt/usb`) | Own mini-FAT `ufat_*`: first MBR slot only (no GPT), FAT16/32, ROOT DIRECTORY ONLY, 8.3 NAMES ONLY (LFN entries skipped), max 32 entries (`ufs[32]`), files read whole into RAM, write = whole file, no subfolders | `hid_keyboard_mouse.c:278-374` |
| FAT (system disk) | `fs/fat/vfat.c`: FAT16/32 read/write with LFN, subdirs, runs of 128 clusters; used for root FS, boot partition, updater. Not used for USB sticks | `fs/fat/vfat.c` |
| NTFS | ntfs-3g (GPLv2+) read/WRITE, can be root FS, installer formats it; `ntfs mount/ls/cat/write/mv/mkdir/rm` shell command but only on the current gdisk disk | `third_party/ntfs`, `fs/ntfs/ntfsk.c` |
| NVX4 native FS | Full (extents, two checksummed metadata copies) | `fs/nvx/nvx4.c` |
| Detection only | `parts.c`/`volumes.c` recognise GPT+MBR and NTFS, exFAT, FAT, ext2/3/4, XFS, btrfs, HFS+, APFS, ISO9660, LUKS, BitLocker, swap; This PC shows tiles with label + free space for FAT/NTFS/exFAT/ext | `fs/vfs/parts.c`, `fs/vfs/volumes.c` |
| Opening a non-system volume | NOT POSSIBLE. `tpc_open` only opens the system drive or a FAT USB stick; every other tile (Windows C:, ext4 data, exFAT stick, second disk) only shows Properties | `apps/system/file_explorer/open_and_draw.c:95-98` |
| exFAT / ext4 / ISO9660 / SD readers / UAS | none (detection only for the FS) | - |

Already in PORTING-WORKLIST.md: #7 SD + rtsx, #8 lwext4, #9 FatFs exFAT. NTFS write is ALREADY there (ntfs-3g) - do not port it again.

Main finding: the daily-user problem is not a missing FS library but the plumbing. The USB stick path is a 32-file, root-only, 8.3-only FAT reader, and no partition other than the system one can be opened in Files. Any new FS (exFAT, ext4) is useless until there is a generic "block device -> partition -> mounted volume" layer.

## 2. Candidates (ranked)

### Rank 1: USB mass storage rework + generic volume mounts (own code, BSD/MIT references)
- References: FreeBSD `sys/dev/usb/storage/umass.c` (SPDX BSD-2-Clause, 3053 lines, quirk table), Haiku `usb_disk.cpp` ("Distributed under the terms of the MIT License"). Use them as reference / quirk lists, write the code ourselves on our xHCI.
- Work: (a) several sticks at once (per-device struct instead of `msd_*` globals, own DMW slices); (b) GET MAX LUN + per-LUN units (USB card readers - Genesys GL3xxx, Realtek RTS5129/5179 USB - show each slot as a LUN); (c) READ CAPACITY(16)/READ(16)/WRITE(16) for >2 TB disks, 4096-byte sector disks; (d) TEST UNIT READY / REQUEST SENSE / unit-attention for card insert/remove; (e) register each stick as a `gdisk`-style block device so `parts_scan` (MBR+GPT) works; (f) a volume-mount layer: `/mnt/<label>` per partition, a vfs back end that is read/write (today registered back ends are read-only), one table of ops (FAT, NTFS, later exFAT/ext4), Files `tpc_open` opens any mounted volume, safe-eject (flush + SYNCHRONIZE CACHE). Retire `ufat_*` (32-entry root-only reader) for `vfat.c` or FatFs.
- Firmware: none. Size: ~1.5-2.5k own lines. Effort: 12-16 agent-hours (can split: USB block layer 6-8 h, volume layer + Files 6-8 h).
- Value: HIGHEST. Owner's USB stick + USB 2.0 hub; every user with a stick with folders or long names; prerequisite for ranks 2-4.

### Rank 2: FatFs R0.16 for removable media (FAT12/16/32 + exFAT, LFN, GPT)
- URL: http://elm-chan.org/fsw/ff/ (R0.16, latest patch 2026-07-10).
- License: FatFs license, "Redistribution and use ... permitted provided that the following condition is met: 1. Redistributions of source code must retain the above copyright notice" - author calls it equivalent to BSD-1-Clause and GPL-compatible (appnote.html#license). OK.
- Patent note (appnote): exFAT is a Microsoft patent; FatFs says a Microsoft licence "may be needed for commercial products". Microsoft published the spec in 2019 and joined OIN; for a free GPL OS this is the same position as Linux. Mention to owner; `FF_FS_EXFAT` is a build switch.
- Size: ff.c ~7k lines + ffunicode.c (code pages; take only CP437/UTF-16 tables). Deps: `disk_read/disk_write/disk_ioctl` - maps 1:1 onto the rank-1 block device. Reentrancy via `FF_FS_REENTRANT` + our mutex.
- Effort: 6-8 agent-hours on top of rank 1. Value: high - most USB sticks >32 GB and all SDXC cards are exFAT; also gives FAT12 (floppies/small sticks, refused today) and long names on sticks.
- Alternatives (allowed, worse fit): Linux `fs/exfat` (SPDX GPL-2.0-or-later, Samsung; ~9k lines, Linux VFS/buffer-head bound); relan/exfat (GPL-2.0-or-later, FUSE/userspace, would run as a Linux-ABI program). Keep `vfat.c` for the system/boot partition (updater depends on it).

### Rank 3: Dual-boot Windows drive through the existing ntfs-3g (own glue, no port)
- ntfs-3g (GPL-2.0-or-later, already in `third_party/ntfs`) already reads and writes. Missing: mount an NTFS partition on ANY disk/partition (today `nk_find` + gdisk current disk only) as a volume from rank 1, so This PC "Windows (C:)" opens. Must detect Windows Fast Startup / hibernation (`hiberfil.sys` / dirty flag; ntfs-3g `NTFS_MNT_RECOVER` logic) and mount read-only with a clear message ("Windows was not fully shut down - turn off Fast Startup"), and BitLocker volumes (already detected) shown as locked.
- Effort: 4-6 agent-hours. Value: high for dual-boot users. Needs rank 1 volume layer.

### Rank 4: ext2/3/4 read-write: lwext4
- URL: https://github.com/gkostka/lwext4. License: all files BSD-3-Clause except `src/ext4_extent.c` and `src/ext4_xattr.c`, whose headers say "GNU General Public License ... either version 2 of the License, or (at your option) any later version" -> GPL-2.0-or-later, OK in GPLv3. (README badge says "GPLv2" because of those two files; the files themselves are "or later".)
- Size: ~23k lines src+include. Features: extents, journal replay + transactions, metadata_csum, htree dirs, 64bit, flex_bg; block-device abstraction (`ext4_blockdev`) maps onto rank-1 devices; has mkfs.
- Risks: e2fsprogs 1.47 defaults add `orphan_file` (compat) and `orphan_present` (ro_compat) - lwext4 may force read-only; test with a fresh Fedora/Ubuntu ext4. Start read-only by default, write opt-in after QEMU tests with `e2fsck -fn`.
- Alternative: FreeBSD `sys/fs/ext2fs` (15.9k lines; BSD-2/BSD-3/MIT-CMU, but `ext2_hash.c` is "BSD-2-Clause AND RSA-MD" - replace with own half-MD4/TEA hash); tightly bound to FreeBSD vnode/buf cache and does no journal replay (refuses rw on needs_recovery). Harder.
- Effort: 12-16 agent-hours. Value: medium-high (Linux dual-boot, data disks). Already worklist #8.

### Rank 5: SD/MMC + Realtek PCIe card readers
- OpenBSD `sys/dev/ic/rtsx.c` + `sys/dev/sdmmc/{sdmmc,sdmmc_mem,sdhc}.c` - ISC ("Permission to use, copy, modify, and distribute this software ..."). Or FreeBSD `sys/dev/rtsx/rtsx.c` (SPDX BSD-2-Clause, 3915 lines, newer chips RTS522A/5260/5227) as chip reference. `sdhc.c` also covers standard SDHCI readers (Intel/O2/Bayhub on many laptops).
- Firmware: none. Size: ~7.3k (worklist). Effort: 14-20 agent-hours. Value: medium - laptops (owner's ASUS laptop); owner's desktop has none; USB card readers are covered by rank 1 (multi-LUN). Already worklist #7.

### Quick wins (own code, NVMe spec / OpenBSD nvme.c ISC as reference)
- NVMe shutdown notification: set CC.SHN=01b and wait CSTS.SHST=10b before power-off/restart (today only Flush). Prevents "unsafe shutdown" counts and possible data loss on some SSDs. ~1 h.
- AHCI: STANDBY IMMEDIATE (0xE0) before power-off. ~1 h.
- NVMe DSM/TRIM for NVX4 freed extents, later. ~3-4 h.
- ISO9660+Joliet read-only for opening `.iso` files: low value, ~600 own lines, skip for now.

## 3. Rejected
- Linux `drivers/usb/storage/uas.c`: SPDX GPL-2.0 (only). No permissive UAS implementation found in FreeBSD/OpenBSD. UAS adds speed only; UAS enclosures (ASM1153/JMS578/ASM2362) also offer BOT, which rank 1 uses. Defer; own code from the T10 UAS spec if ever.
- Linux ext4 (`fs/ext4`) and ntfs3 (`fs/ntfs3`): GPL-2.0-only. Not needed anyway (lwext4, ntfs-3g).
- FreeBSD `ext2_hash.c` as-is: RSA-MD part (attribution clause) - only usable after replacing the MD4 code.
- Porting "NTFS write": already present via ntfs-3g.

## 4. Suggested order
Rank 1 (block layer + volumes + Files open) -> Rank 2 (FatFs on sticks, retires ufat) -> Rank 3 (Windows drive) -> NVMe/AHCI shutdown quick wins (any time, independent) -> Rank 4 lwext4 -> Rank 5 SD.
