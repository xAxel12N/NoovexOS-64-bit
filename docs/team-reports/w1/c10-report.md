# c10 report: quick wins (firmware licences, calculator, FLAC/AV1, storage shutdown, 1080p cap)

Branch x-c10 in /home/claude/wt/c10. There are 4 commits on top of v11.06 (dae38348):
dba0de01, 4e118f94, 33f458a8, b3865bf5.

## 1. Firmware licence notices (sc10 flags 1 and 2): DONE
- New `assets/nvidia/LICENCE.nvidia` and new `assets/firmware/LICENCE.rtlwifi_firmware.txt`. Both are verbatim copies from gitlab.com/kernel-firmware/linux-firmware (main).
- New `assets/firmware/README.txt`. It maps each blob to its upstream path and names the Realtek copyright and licence.
- `assets/nvidia/README.txt`: now says the licence copy ships next to the file and in `/usr/share/licenses/firmware/`.
- `assets/README.md:3`: added a note that the licence texts must ship with every image.
- `build.sh`, the line just before `python3 tools/mksysroot.py $B/sysroot.bin =$SR`: copies both licences and both READMEs to `$SR/usr/share/licenses/firmware/`, so they are in the image's system files.
- `apps/system/files_classic.c` ~line 49: the built-in `LICENSE.txt` in Files now names the firmware and points to `/usr/share/licenses/firmware`.
- Not done: Settings > About has no licences page, and I did not add one.

## 2a. Calculator (sc9): DONE
- `apps/office/calculator.c`:
  - Signed fixed point with 6 decimals (×10^6). There is no FPU use and no libgcc call: `nm -u` shows no `__*ti3`. The 128/64 division is written by hand.
  - New buttons on a 5th row: `%`, `+/-`, `0`, `.`, `=`. The window is 300x420, so the 5th row fits; the old layout drew only 16 buttons and `.` was never on screen.
  - `%` works like Windows: `200+10%` gives 20.
  - Overflow and division by zero show ERROR.
  - The display keeps typed trailing decimals (`3.50`) and trims zeros in results. It falls back to the small font for long numbers.
  - New `calc_key()` handles digits, `+ - * / % .`, `,` (Swedish decimal), Enter/`=`, Backspace, and Esc/`c`.
- `include/nvx/apps/office/calculator.h`: declares `calc_key` and `calc_dec`.
- `graphics/ui/input.c` ~line 334: one added dispatch line, `focus==W_CALC` → `calc_key(c)`. **Possible merge touch point** with whoever edits input.c; it is one line after the W_CALENDAR line.
- Test: a host harness (/tmp/c10t/t.c includes calculator.c with stubs) ran 22 cases and all passed, including 1/3, 2/3, 0.1*0.2, 200+10%=, 5+3 +/- =, 1/0, overflow, chained ops and backspace.

## 2b. FFmpeg FLAC / ALAC / AV1 (sc9): DONE. No downloads needed; build.sh was not run.
- Vendored FFmpeg 8.1.2 already contains flacdec, alac and libdav1d.c. Vendored dav1d is the C-only tree that the web build uses.
- `build.sh`, the ffmpeg block (~line 152):
  - Adds the `flac` demuxer.
  - Adds the decoders `flac, alac, pcm_s24le, pcm_s32le, pcm_f32le, libdav1d`.
  - Adds the parsers `flac, av1`.
  - Adds `--enable-libdav1d`. libdav1d.a is built with `$MCC` from third_party/dav1d (same file list as the web build), into `build/ffmpeg/dav1d`, with a generated `dav1d.pc`.
  - The ffmpeg cache now also rebuilds when the codec list changes (`build/ffmpeg/flags.stamp`). It deletes `ports-dyn/play` so play relinks.
  - The play link line adds `-ldav1d -lpthread -lm`.
  - **The first build.sh after the merge rebuilds ffmpeg once (~5.5 min on 2 cores).**
- `apps/media/videoplayer.c:36`: `.flac` opens in the Video Player. Files already lists `.flac` as audio.
- Verified outside build.sh:
  - Ran the same configure with MCC in /tmp/c10ff and the make succeeded. Linked play the same way as build.sh.
  - Ran play under musl ld.so on the host:
    - AV1 mp4 (libaom-encoded): `av1 320x240, decoded 11 frames`, and the snapshot PPM was correct.
    - FLAC: `audio flac`.
    - ALAC m4a: `audio alac`.
    - The old play said `cannot open t.flac`.
  - play grew from 9.8 MB to 11.2 MB.
- Licences are fine: dav1d is BSD-2 and FFmpeg stays LGPL-2.1+.
- Not done: the web engine's `isTypeSupported` still refuses AV1. That is a web/MSE change and was out of scope.

## 3. NVMe/AHCI clean shutdown (sc6 quick win): DONE
- `drivers/storage/nvme.c`, appended at the end:
  - `nvme_shutdown()`: Flush, set CC.SHN=01b, then wait for CSTS.SHST=10b (at most 5 s by hr_ns, with an io_wait loop fallback). Then sets `nvme_ok=0`, so a late I/O fails instead of hitting a shut-down controller.
  - `gdisk_shutdown(int poweroff)`: calls both shutdown functions and writes the result to klog.
- `drivers/storage/gdisk.c`, appended at the end: `ahci_shutdown_all(int standby)`. For every AHCI unit it sends FLUSH CACHE EXT (0xEA), plus STANDBY IMMEDIATE (0xE0) when standby is set.
- Prototypes are in `include/nvx/drivers/storage/nvme.h` and `gdisk.h`.
- `core/power.c`:
  - power_off: `if(g_gdisk_probed)gdisk_shutdown(1);` after the second gdisk_flush.
  - power_reboot: `gdisk_shutdown(0)`. NVMe gets SHN on reboot too; SATA disks are not spun down on reboot.
- Only new functions plus these two calls, so it should merge cleanly with c4's storage edits.
- Not done:
  - Suspend (core/power.c:139) is untouched; it would need re-init on wake.
  - Deleting the NVMe I/O queues before SHN is a "should" in the spec. I skipped it, as most simple drivers do.
- No hardware test.

## 4. Lifting the 1920x1080 cap (sc7): REPORT ONLY. It is not a contained change.
Raising MAXW/MAXH alone breaks the build:
- kernel .bss is fixed at 0x10000000–0x18000000 and ends at 0x17A3A8A8 in the current build, so about 5.8 MB is free.
- `arch/x86_64/kernel64.ld` asserts that limit.
- Four full-screen static buffers of MAXW*MAXH*4 bytes (8.3 MB each) live in .bss: `SCENE_STATIC`, `BG_STATIC` (drivers/video/fb/framebuffer.c:18-19), `SCENE_NC_STATIC` (graphics/ui/desktop/desktop_clicks.c:283) and `g_blurtmp` (shell/commands/terminal_keys_and_render.c:32).
- At 2560x1600 that is +33 MB; at 4K, +100 MB.

What a proper lift needs (estimate 6-10 h, needs QEMU and real hardware):
1. Allocate those 4 buffers at runtime with size SW*H*4, after the mode is known. That is in core/init/main.c:221, which runs before the heap exists, so it needs an early pmm/contiguous allocation or a temporary small boot buffer.
2. Rework the gfx stop fallback, which copies back into the static arrays at drivers/video/fb/gfx.c:338-340. Also update the `W>MAXW` refusal at gfx.c:320 and the externs at gfx.c:310 and framebuffer.h:13-15.
3. `drivers/video/intel/igpu.c:368`: size b_gs from SW*H, not MAXW*MAXH.
4. Row-sized arrays only need a larger row constant (cheap): `ws_x0/ws_fx/ws_cw[MAXW]` (app_tiles_and_icons.c:125) and `g_glasstmp[MAXW]` (graphics/theme/effects.c:4).
5. `wallsrc[1920*1200*4]` (app_tiles_and_icons.c:111, 9.2 MB .bss): check wallpaper scaling at larger sizes.
6. Clamps:
   - main.c:221 and core/init/bootmenu.c:145: change the clamp.
   - boot/uefi/uefiboot.c:81-82 `MAX_W/MAX_H`, used at lines 292 and 338: raise.
   - The boot tests assert that 1920x1080 is picked over 2560x1440 and must be updated: boot/uefi/test/firmware_sim.c:101 and pe_run.c:102, :174.
   - The BIOS path has its own cap: boot/src/stage2.c:370-375.
7. Performance: the compositor is software, and 2560x1440 has 1.8x the pixels of 1080p. Suggest capping at 2560x1600 first, or making the larger size opt-in.

Moving the buffers to the heap would also free about 33 MB of .bss.

## Tests run
- `/home/claude/ccheck.sh` on calculator.c, input.c, files_classic.c, videoplayer.c, nvme.c, gdisk.c, power.c and kernel64.c (the unity build includes the storage files): 0 errors. Only pre-existing warnings.
- `python3 tools/check.py --only usb`: 12/12 passed.
- Calculator host harness: 22/22.
- FFmpeg + dav1d configure, build and play link in /tmp: OK. Host decode of AV1, FLAC and ALAC worked.
- `bash -n build.sh`: OK.

## Notes for memory
None.
