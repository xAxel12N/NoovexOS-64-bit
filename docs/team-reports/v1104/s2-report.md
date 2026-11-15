# S2 — fs/ + USB-stick FAT safety sweep (v11.04)
Scope read: fs/fat/vfat.c (boot partition / update path, mounts the USB stick in update.c), fs/fat/fat_ata.c,
drivers/usb/xhci/hid_keyboard_mouse.c:250-351 (ufat_* = THE USB-stick FAT driver), nvx4.c/dirtree.c skim.
Note: block callbacks (up_rd/up_wr, umsd_*) do NOT clamp to the partition, so any bogus LBA math reaches the stick.

## 1 HIGH — ufat_set/ufat_next have no cluster bound -> writes zeros into arbitrary stick sectors
drivers/usb/xhci/hid_keyboard_mouse.c:270-272, 314-316, 321, 340
Scenario: stick with a file whose dirent cluster (or FAT chain value) is e.g. 0x0FFF0000. User saves a file with the
same 8.3 name -> ufat_write_file -> ufat_free_chain(oc) -> ufat_set(clus,0) loads FAT "sector" fatstart+clus*4/512
(far outside the FAT: data area, another partition, or wrapped u32 LBA incl. LBA 0) and ufat_flush writes it back
(nfats copies). Silent corruption of the stick/MBR. ufat_next reads past FAT too.
Patch (add helper, use it in ufat_next and ufat_set):
  static int ufat_cok(u32 c){ return c>=2&&ufat_nclus&&c<ufat_nclus+2; }
  ufat_next: first line  `if(!ufat_cok(clus))return 0x0FFFFFFFu;`
  ufat_set:  first line  `if(!ufat_cok(clus))return;`
  ufat_alloc: replace maxc computation with `u32 maxc=ufat_nclus+2; if(!ufat_nclus)return 0;`

## 2 HIGH — ufat_mount accepts insane BPBs (fatsz=0, resv=0, ds>=ts, u32 wrap of nfats*fatsz)
hid_keyboard_mouse.c:292-300
Scenario: crafted BPB: fatsz32=0x02000000, nfats=255 -> ufat_dataclus wraps to a low LBA; writes go to LBA 0.
ts<=ds -> ufat_nclus=0 -> ufat_alloc scans up to 0x0FFFFFF0 clusters (2M USB reads = hang) and writes past end.
Patch after line 300:
  { unsigned long long ds64=(unsigned long long)ufat_resv+(unsigned long long)ufat_nfats*ufat_fatsz+rootsecs;
    if(!ufat_resv||!ufat_fatsz||ufat_nclus<1||ds64>=0xFFFFFFFFULL-plba)return;
    unsigned long long ents=(unsigned long long)ufat_fatsz*(ufat_type==16?256u:128u);
    if(ents<3)return; if((unsigned long long)ufat_nclus+2>ents)ufat_nclus=(u32)(ents-2);
    if(ufat_type==32&&(ufat_rootclus<2||ufat_rootclus>=ufat_nclus+2))return; }
  Also line 292: `if(ufat_spc==0||(ufat_spc&(ufat_spc-1)))return;` instead of forcing 1.

## 3 MED — root-dir chain cycle = minutes-long hang (scan, save, hotplug)
hid_keyboard_mouse.c:305 (guard 256 clus), 331 (guard 4096 clus)
Scenario: FAT32 root cluster points to itself, sectors filled with 0xE5 entries, spc=128: ufat_dirent_scan reads
4096*128 = 524288 sectors per save; mount reads 32768. Patch: bound by total sectors, not clusters:
  line 331: replace `guard++<4096` with `guard++<4096&&(u32)guard*ufat_spc<=4096u` (FAT spec max dir = 65536
  entries = 4096 sectors); line 305: `guard++<256&&(u32)guard*ufat_spc<=512u`.

## 4 MED — vfat: directory chain cycle hangs vf_next/vf_ls/vf_walk/vf_slot forever
fs/fat/vfat.c:45-50 (vf_loc), 129-139 (vf_slot)
Scenario: update path mounts stick/boot partition; a dir whose cluster chain is A->B->A with no 0x00 terminator
entry -> vf_next idx grows forever (vf_loc never fails), O(n^2) walk, int overflow. Hangs ls/open/update.
Patch: vf_loc first line: `if(idx<0||idx>=65536)return -1;`
       vf_slot inside the vf_loc-failed branch, before allocating: `if(i>=65536)return -1;`

## 5 MED — vfat mount: data-area underflow and nf*fz u32 overflow before the size check
fs/fat/vfat.c:182-186
Scenario: rsvd+nf*fz+rs > ts -> `data` underflows -> cl clamped only to FAT capacity -> clusters map far beyond
the partition end; up_wr does not clamp -> vf_write/vf_mkdir writes into the next partition. fz*512 at :186 wraps
for fz>=0x800000 (caught later at :190 only by luck).
Patch: replace line 182-183 head with
  if(!rsvd||!nf||!fz||ts>nsect||fz>32768)return -1;
  { unsigned long long meta=(unsigned long long)rsvd+(unsigned long long)nf*fz+(re*32u+511u)/512u; if(meta>=ts)return -1; }
  Optional: `if(g_vfat.fsinfo>=rsvd)g_vfat.fsinfo=0;` after :188 (fsinfo write in vf_flush can hit any sector
  whose first dword is 'RRaA').

## 6 LOW — vf_mark_dir exponential walk on subdir loops (vf_reclaim during update)
fs/fat/vfat.c:203. A dir with N entries all pointing at root -> N^16 visits (depth cap 16 only). Patch, first line
of vf_mark_dir: `if(dc&&(g_vf_mark[dc>>3]&(u8)(1u<<(dc&7))))return;` (before vf_mark_chain). Also: if the walk
was cut short, vf_reclaim frees live clusters -> consider aborting reclaim when depth>16 was hit.

## Checked OK
vf_next LFN (ord<=20, lfn[260], name n<250+4 <256), vf_shortname (<=13), ufat_dir_sector nm[16] (<=13),
vf_read/vf_inplace/ufat_read (bounded by size/cap + guards), vf_get/vf_set (clusters clamped to FAT size),
fat_ata.c (internal ATA only; fsize>=2^31 rejected; guards present), nvx4 load (n4_claim bounds, n4_cpy NUL),
dirtree parent cycles (guarded g<32 / self-loop reset). Not reached in time: ntfsk.c, deb_archive.c, unixfs.c.
