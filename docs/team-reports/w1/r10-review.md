# r10 review of c10 (x-c10, merged in v11.07): storage shutdown + build.sh FFmpeg

Scope: what the lead asked for: (a) can the NVMe/SATA shutdown on power-off/reboot hang the shutdown or lose the final sync, (b) the build.sh FFmpeg flag change. I only skimmed the calculator, licence and 1080p parts.

Line numbers are for main (v11.07).

## Verdict
- **No HIGH findings.** No false fix.
- **Final sync is not lost.** The order in `power_off` / `power_reboot` (core/power.c:85-86, 99-100) is:
  1. `gdisk_flush`
  2. `power_prepare` (nvx3_sync_all, ufat_sync, ntfs/vfat umount)
  3. `gdisk_flush`
  4. `gdisk_shutdown`, which flushes again for every disk, then sends STANDBY / SHN.
- **Nothing writes to disk after `gdisk_shutdown`.** `klog` only goes to RAM rings and serial (core/klog.c:42, sysfiles.c:44, crash.c:143), and nothing else runs after it. Late writes fail cleanly, because the code sets `ahci_ok=0` on power-off and `nvme_ok=0` after SHN.
- **No hang that never ends.** Every wait is bounded:
  - NVMe SHN wait: 5 s by hr_ns, else 10M io_wait (about 10 s).
  - `nvme_flush`: goes through `nvme_wait`, at most 20M io_wait.
  - AHCI commands: 20M polls each, then `ahci_fail` with a COMRESET.
- **There are two real weaknesses (MEDIUM).** A slow shutdown is still watched by the freeze watchdog, and in the worst case the shutdown hook can stall for minutes. See below.
- **build.sh:** correct. One new host dependency (pkg-config), and AV1 was not tested inside NoovexOS (LOW).

## MEDIUM 1: the freeze watchdog stays armed during shutdown; at 30 s it saves a report into a filesystem that is unmounted or shutting down
- **Where:** core/power.c:85-86, 99-100; drivers/storage/nvme.c:267-271; core/crash.c:393-403.
- **What happens:**
  - `power_off` / `power_reboot` never set `g_frz_hold`, and the main-loop heartbeat stops while they run.
  - `freeze_tick` runs from the timer IRQ, and interrupts stay on until the S5 write. So 5 s into a slow shutdown, the "not responding" freeze box is drawn over the shutdown.
  - At 30 s, if the BKL is free, `cx_save` writes `crash-report.txt` and runs `nvx3_sync_all`.
  - `gdisk_shutdown` does not raise `g_disk_busy`, so `g_cx_nofs` stays 0.
- **Why this got more likely now:** c10 added up to 5-10 s of NVMe SHN wait, an HDD spin-down for each SATA disk, and per-disk FLUSH timeouts of up to about 20 s on top of the existing syncs.
- **Worst case:** an IRQ-context `nvme_cmd` write lands while CC.SHN is being processed. `nvme_ok` is only cleared after the wait loop, so the queue tail/doorbells are touched behind the shutdown.
- **Patch:**
```diff
--- a/core/power.c
@@ void power_off(void){ { extern volatile int g_nmiwd_on; g_nmiwd_on=0; }
-void power_off(void){ { extern volatile int g_nmiwd_on; g_nmiwd_on=0; } {
+void power_off(void){ { extern volatile int g_nmiwd_on; g_nmiwd_on=0; extern volatile int g_frz_hold; g_frz_hold=1; } {
@@ void power_reboot(void){ { extern volatile int g_nmiwd_on; g_nmiwd_on=0; }
-void power_reboot(void){ { extern volatile int g_nmiwd_on; g_nmiwd_on=0; } {
+void power_reboot(void){ { extern volatile int g_nmiwd_on; g_nmiwd_on=0; extern volatile int g_frz_hold; g_frz_hold=1; } {
--- a/drivers/storage/nvme.c
@@ int nvme_shutdown(void){
     int r=nvme_flush();
+    nvme_ok=0; nvme_cblk=0xFFFFFFFFu;
     volatile u32* cc=(volatile u32*)((u64)nvme_regs+0x14);
@@
-        if(t0&&hr_ns()-t0>5000000000ULL)break; io_wait(); }
-    nvme_ok=0; nvme_cblk=0xFFFFFFFFu;
+        if(cs&2u)break; if(t0&&hr_ns()-t0>5000000000ULL)break; io_wait(); }
     return (r||!done)?-1:0; }
 void gdisk_shutdown(int poweroff){
     extern void klog(const char*); extern int ahci_shutdown_all(int);
-    int a=ahci_shutdown_all(poweroff), n=nvme_shutdown();
+    __atomic_add_fetch(&g_disk_busy,1,__ATOMIC_SEQ_CST);
+    int a=ahci_shutdown_all(poweroff), n=nvme_shutdown();
+    __atomic_sub_fetch(&g_disk_busy,1,__ATOMIC_SEQ_CST);
```
- **Notes on the patch:**
  - The `cs&2u` check stops waiting as soon as the controller reports CSTS.CFS (fatal), instead of burning the full 5 s.
  - `g_disk_busy` is already visible in nvme.c, because gdisk_flush uses it at nvme.c:187.
  - The crash-path `power_reboot` callers (crash.c:225/226/231/256) do not mind `g_frz_hold=1`.

## MEDIUM 2: a broken SATA disk can stall power-off or reboot for minutes, because STANDBY is still sent after FLUSH has already timed out
- **Where:** drivers/storage/gdisk.c:103-109.
- **What happens:**
  - Each `ahci_cmd` timeout is 20M PxCI MMIO polls, about 10-20 s on real PCIe, plus the COMRESET from `ahci_fail`.
  - For each unit the code sends FLUSH, then STANDBY regardless of the result. So one wedged disk costs about 2 × (20 s + reset), and up to 8 units multiply that.
  - Before c10, only the boot disk's single FLUSH was at risk.
  - The user sees a black or frozen screen and holds the power button, which is exactly the data loss this feature tries to prevent.
- **Patch:** skip STANDBY when FLUSH failed. Standby is pointless on a disk that does not answer. Also restore the port that was selected before, so `power_reboot` (standby=0, `ahci_ok` stays 1) does not leave `ahci_port` on the last unit (see LOW 1).
```diff
--- a/drivers/storage/gdisk.c
 int ahci_shutdown_all(int standby){
-    if(!ahci_abar||!ahci_pn)return 0; int bad=0;
-    for(int k=0;k<ahci_pn;k++){ if(ahci_select(k))continue;
-        if(ahci_cmd(0,0,AHCI_BUF,0,0xEA))bad++;
-        if(standby&&ahci_cmd(0,0,AHCI_BUF,0,0xE0))bad++; }
-    if(standby)ahci_ok=0;
+    if(!ahci_abar||!ahci_pn)return 0; int bad=0, cur=-1, wasok=ahci_ok;
+    for(int k=0;k<ahci_pn;k++) if(ahci_port==(volatile u32*)((u64)ahci_abar+0x100+(u32)ahci_pl[k]*0x80))cur=k;
+    for(int k=0;k<ahci_pn;k++){ if(ahci_select(k))continue;
+        if(ahci_cmd(0,0,AHCI_BUF,0,0xEA)){ bad++; continue; }
+        if(standby&&ahci_cmd(0,0,AHCI_BUF,0,0xE0))bad++; }
+    if(standby)ahci_ok=0; else if(cur>=0){ ahci_select(cur); ahci_ok=wasok; }
     return bad; }
```

## LOW 1: on reboot the AHCI selection is left on the last unit
- **Where:** gdisk.c:104.
- `ahci_select(k)` switches the port and forces `ahci_ok=1`. On reboot (standby=0) nothing restores the boot disk's port.
- Today nothing writes after it, so this is harmless. It becomes silent wrong-disk writes if anyone later adds a write after `gdisk_shutdown(0)`, or calls it from suspend.
- Fixed by the MEDIUM 2 patch.

## LOW 2: build.sh now needs host pkg-config, and nothing checks for it
- **Where:** build.sh:166 (`--pkg-config=pkg-config --pkg-config-flags=--static`).
- If pkg-config is missing, configure fails with "libdav1d not found". build.sh only says "FFMPEG BUILD FAILED (see build/ffmpeg/*.log)", after the old ffmpeg cache was already deleted (`rm -rf $FB` at :160).
- pkg-config is present on this builder (/usr/bin/pkg-config).
- Patch (put it before line 158):
```sh
command -v pkg-config >/dev/null || { echo "build.sh needs pkg-config (apt install pkg-config) for the FFmpeg/dav1d build"; exit 1; }
```

## LOW 3: AV1 was only tested on the host, not under NoovexOS
- dav1d starts its own worker threads. play.c:309 sets `thread_count=0`, so libdav1d.c:215-238 picks threads from `av_cpu_count()`, which is 12 on the owner's PC, and adds frame delay.
- h264/hevc already use FFmpeg threads in the OS, so pthreads basically work.
- dav1d's thread_task pool and `sched_getaffinity` are new syscall paths, though. Please run `play` on an AV1 file in QEMU (`-smp 4`) before claiming AV1 in the release notes.
- No patch.

## build.sh FFmpeg change: checked, OK
- **Same sources as the web build:** the dav1d file list and the `-include config.h` flags are identical to the existing web build (build.sh:190-192), and the sources exist in third_party/dav1d.
- **Licences:** dav1d is BSD-2 and FFmpeg stays LGPL. No `--enable-gpl` is needed.
- **Paths:**
  - `B=build` is relative, so `$(pwd)/$DVB` in the .pc file is a correct absolute prefix.
  - `PKG_CONFIG_PATH=$(pwd)/dav1d/...` is evaluated after `cd $FB` inside the subshell, so it is also correct.
  - `PKG_CONFIG_PATH` is searched before the system dirs, so a host libdav1d-dev cannot take over.
- **Cache invalidation:**
  - The stamp covers the codec lists, and the build is also redone when libdav1d.a is missing.
  - `rm -rf $FB $P/play` forces play to relink, because `port` skips the build when `$P/play` exists.
  - The stamp is written only after a successful install. Existing build dirs rebuild once, as the report says.
- **Link order:** `-lavcodec … -lavutil -L$DVB/lib -ldav1d -lpthread -lm` is correct for static libraries.
- **NVMe register use:** CC.SHN bits 15:14 = 01b and the CSTS.SHST check (bits 3:2 == 10b) match the NVMe spec. A CSTS read of 0xFFFFFFFF (device gone) ends the wait.

## Claimed tests
- The tests that were run (compile checks, the host FFmpeg build and decode, `bash -n`) make sense for build.sh.
- The storage shutdown had **no runtime test at all**, not even QEMU. QEMU's NVMe and AHCI emulation would at least show the SHST handshake, the klog line, and that S5 still happens. Suggest the lead runs `shutdown` from the boot shell with `-drive if=none,id=n,file=x.img -device nvme,drive=n,serial=1` plus an AHCI disk, and checks for "storage: disks flushed, SATA disks in standby, NVMe shut down" on serial before power-off.
