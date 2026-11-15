# fw report (round q1)

Branch q-fw, commit 6d56ef16 (worktree /home/claude/wt/fw). build.sh was not run; `bash -n build.sh` passes; `check.py --only bt` passes (44 checks).

## What was done
- Unmodified blobs from linux-firmware commit 2b2c1f39d7ae (gitlab.com/kernel-firmware/linux-firmware, sparse clone), placed in
  `assets/firmware/linux-firmware/<upstream path>`. All have WHENCE `Licence: Redistributable`.
- `assets/firmware/linux-firmware/WHENCE.txt`: the WHENCE lines for each file (Driver, File, Link, Version, Licence, SPDX) plus size and sha256.
- `assets/firmware/linux-firmware/links.txt`: the 33 WHENCE `Link:` entries we need. Unused ones (rtl8822cu_config, rtl8723bs OBDA) are left out, so nothing changes for the embedded 8821C/8822C path.
- `LICENSES/`: LICENCE.iwlwifi_firmware, LICENCE.ibt_firmware, LICENCE.mediatek, LICENCE.rtlwifi_firmware.txt.
- build.sh line 306 (straight after the licence lines at 304/305): copies intel/, mediatek/ and rtl_bt/ into `$SR/lib/firmware/`, creates the links as relative symlinks, copies the licences and `WHENCE.linux-firmware.txt` into `/usr/share/licenses/firmware/`, and exits with "FIRMWARE STAGING FAILED" on error.
  - I ran this line alone against a temp $SR, then ran `tools/mksysroot.py`: 34 files, 12,518,014 bytes, which packs to 4.1 MB of xz in sysroot.bin.

## Files (bytes), total 12.5 MB
| File | Bytes |
|---|---|
| iwlwifi-cc-a0-77.ucode (AX200) | 1368100 |
| iwlwifi-QuZ-a0-hr-b0-77.ucode (AX201) | 1406720 |
| iwlwifi-Qu-b0-hr-b0-77.ucode | 1406576 |
| iwlwifi-Qu-c0-hr-b0-77.ucode | 1406592 |
| iwlwifi-ty-a0-gf-a0-89.ucode (AX210) | 1679180 |
| iwlwifi-ty-a0-gf-a0.pnvm | 55020 |
| intel/ibt-20-1-3.sfi / .ddc | 801016 / 9 |
| intel/ibt-19-0-4.sfi / .ddc, plus links for 19-0-0/1/3, 19-16-4, 19-32-0/1/4, 19-240-1/4 | 801412 / 9 |
| intel/ibt-0041-0041.sfi / .ddc | 713524 / 9 |
| mediatek/BT_RAM_CODE_MT7961_1_2_hdr.bin | 533870 |
| mediatek/BT_RAM_CODE_MT7961_1a_2_hdr.bin (flavor 1a, which mtkbt_fw_name asks for) | 494705 |
| mediatek/BT_RAM_CODE_MT7922_1_1_hdr.bin | 517590 |
| mediatek/mt7925/BT_RAM_CODE_MT7925_1_1_hdr.bin | 448367 |

rtl_bt: all 13 fw names in rbt's table (commit b2907422) are bundled: 8723a, 8723b, 8723d, 8821a, 8761a, 8761bu, 8822b, 8852au, 8852bu, 8852cu_fw_v2, 8851bu, 8922au, 8852btu (24-130 KB each).
- Configs bundled as files: rtl8761bu, rtl8822b, rtl8922au, rtl8723bs, rtl8821c.
- Configs provided as upstream Link: symlinks: 8723b, 8723d, 8821a, 8761a, 8852au/bu/cu, 8851bu, 8852btu.
- Upstream has no real rtl8723d_config file; WHENCE links it to rtl8821c_config.bin, and we do the same.

No Broadcom files.

## Loader path verification
- The sysroot (`tools/mksysroot.py $B/sysroot.bin =$SR`) is registered as a VFS backend in vfs_init (fs/vfs/vfs.c:59), with paths rooted at `/`.
- The Intel BT and MediaTek loaders read through `vfs_open`/`vfs_read`. They check the user overlay first, then fall back to the sysroot.
- iwx (p-iwx iwx_fw.c:110) uses `vfs_lookup` + `fs->size`/`fs->read`, which also go to the sysroot.
- Symlinks: mksysroot stores them with absolute targets, and `sr_walk` follows them (fs/vfs/sysroot.c:79). The same mechanism already serves /usr/bin/env.
- Paths match the loaders:
  - `/lib/firmware/intel/ibt-*` (intelbt_fw.c:187)
  - `/lib/firmware/mediatek/...` (mtkbt_fw.c:188)
  - `/lib/firmware/iwlwifi-<base>-<api>.ucode`, served by the flat symlinks
- Size limits: the largest file is 1.68 MB. Limits are iwx 8 MB, ib_load 4 MB, MTKBT_FWMAX at least 1 MB growth.

## Notes / follow-ups
- Licence:
  - LICENCE.mediatek forbids shipping the files "as part of the Linux kernel or in any other form which would require these files themselves to be covered by the GPL". That is why the blobs are separate files in sysroot.bin and must never be `ld -b binary`'d into kernel64.elf.
  - The Intel licence forbids reverse engineering. That is fine for bundling.
- iwx: for 0x2725 (AX210) the loader tries `so-a0-gf-a0` before `ty-a0-gf-a0`. so-a0 is not bundled, so it falls through to ty-89 with pnvmbase=ty, which is correct. If someone later bundles so-a0-gf-a0 for AX211, an AX210 would pick it up first. The loader should choose based on the MAC type in hw_rev.
- I did not bundle AX211 (so-a0-gf-a0), the QuZ-jf files, or MediaTek Wi-Fi blobs because they were not requested. They can be added the same way.
- Untested on hardware: the owner has none of these chips. The owner's RTL8821CE and 0BDA:C123 are unaffected: embedded blobs and build.sh:38 are unchanged, and no file that the 8821C/8822C path uses was added under /lib/firmware.
