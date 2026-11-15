# Bolt report: small kernel gaps (branch `bolt`, worktree /home/claude/wt/bolt)

There are 7 commits on top of 0aef5934, one per item. Every item is done except the nanosecond part of item 5. No Wi-Fi files were touched, and rtw_chip.h is unchanged.

## How I tested
- **Compile check:** `/home/claude/ccheck.sh` on `sys/linux/linux.c`, `fs/vfs/unixfs.c`, `drivers/sound/hda.c` and `core/proc/jobs.c`. I compared warnings before and after with `git stash`. No warning is new. linux.c has one warning fewer, because the old `lx_utimens` is gone.
- **Linux harness:** I followed the `g_linux` steps from tools/check.py by hand, with scripts in /tmp/bolt (`hb.sh` builds, `mp.sh` builds a musl program). The whole kernel builds as a host object, then `lxharness` runs.
  - New test program: `tools/tests/linux/kgap_t.c`, 63 checks. Run it with `HH_ONLY=1 lxharness kgap` (optional argument: `loginuid`, `cpu`, `cwd`, `epoll`, `times` or `nofile`).
  - The epoll, times and nofile groups also pass when the same static-PIE binary runs on the real host Linux kernel. That checks the expected results against real Linux.
  - After each item: the full default `lxharness` run gives ALL PASS (mhello, legacy programs, compression, sqlite, smptest, kfeat, musltest 97/97, abi2test). After items 4 and 6, the fuzzer seeds 1, 2 and 3 (60000 calls each) give ALL PASS.
- **Driver simulator:** for HDA I used `tools/tests/drivers/drvsim.c`, built exactly like `g_drivers` (MODS from build.sh). The result is ALL PASS. The 7 new HDA checks fail on the old hda.c, so they really test the change.
- check.py and build.sh were not run.

## Items
1. **/proc/<pid>/loginuid — done** (00f14e74). The scout was right: an open for writing got EACCES from `lx_proc_extra`.
   - Opening for writing now returns a sysfs-style fd (`LX_SYSW`, vpath `/proc/<pid>/loginuid`). Read, lseek, `ftruncate(0)` and write all work. This is the exact sequence pam_loginuid uses (I checked it against linux-pam master).
   - Root may set its own loginuid. Writing assigns a new sessionid. Writing 4294967295 unsets it. Non-root and other pids get EPERM on write. Both values are inherited on fork.
   - New fields `aluid` and `asess` in `struct jproc` (jobs.h). A process that never set them shows the old values (uid, sid), so nothing changes for existing users.
   - Files: poll_epoll.c, sysfs.c (`sx_wfd_read`/`sx_wfd_write`), fds_and_system_info.c (ftruncate on LX_SYSW), jobs.h.
2. **/sys/devices/system/cpu/cpuN — done** (f429b868).
   - There is one `cpuN` directory per online CPU (the same count as `cpu/online`), each with an `online` file that reads `1`. They appear in the listing, in stat and in open. `cpu00` and cpuN past the last CPU give ENOENT.
   - File: sysfs.c. The harness has only 1 CPU, so the test only saw cpu0.
3. **/proc/<pid>/cwd — done** (535ffb88).
   - readlink of `/proc/<pid>/cwd`, `/root` and `/exe` now works for other pids. You must be root or have the same uid, otherwise EACCES. cwd comes from the leader's `cwd`, which is saved after every system call. root reads `/` outside containers; the namespace hook still answers inside them.
   - `cwd` and `root` are now listed in /proc/<pid>.
   - Files: memory_and_signals.c (`lx_proc_exe`), fifos_and_names.c.
4. **EPOLLET — done** (80f7d2f5).
   - An edge-triggered entry reports a readiness bit only when it is new. That happens:
     - after any I/O call on that fd (read, write, readv, writev, pread, pwrite, recv, send, recvmsg, sendmsg, recvmmsg, sendmmsg, accept or accept4), including one that returned EAGAIN;
     - when the amount of buffered data grew (pipe, TCP rx or accept queue, Unix rx or queue, eventfd count);
     - after EPOLL_CTL_MOD;
     - when the bit dropped and came back.
   - The I/O hook is `lx_ep_io()`, called in `linux_syscall` next to `lx_sc_note`. Level-triggered entries behave as before. EPOLLRDHUP (0x2000) is now passed through, and TCP reports it. Nested epoll respects ET.
   - Known difference from Linux:
     - For fd types without a data level (timerfd, signalfd, inotify, ptys, devices), new data that arrives while the fd is still readable and untouched gives no new event.
     - After a partial read, NoovexOS may report one extra event that Linux would not.
   - File: poll_epoll.c (+1 line in fds_and_system_info.c).
5. **Timestamps / futimens / fchown — partly done** (e3e25c52).
   - Done: futimens (utimensat with a NULL path) and fchown now change the file or directory behind the fd. Before, they only checked the fd.
   - Done: utimensat now handles UTIME_OMIT per field (before, OMIT meant "now"), explicit time 0, `AT_SYMLINK_NOFOLLOW`, bad tv_nsec (EINVAL), unknown flags (EINVAL) and directories.
   - Done: permission checks follow Linux. Explicit times need the owner or root, else EPERM. Setting "now" needs the owner or write access, else EACCES.
   - Done: `utime()` now honours its time buffer (it used to always set "now").
   - New helpers `ux_utimes_k`, `ux_chown_k` and `ux_owner_k` are in fs/vfs/unixfs.c and unixfs.h.
   - **Skipped: nanosecond timestamps.** `FsFile`/`DNode` keep times as u32 seconds and NVX4 stores them that way, so it needs an on-disk format change, which forces a reinstall. stat still reports nsec = 0. utimes and futimesat (235/261) still take whole seconds through `ux_utime`.
6. **RLIMIT_NOFILE — done** (de13d282).
   - Each process has its own soft and hard limit (new fields `nofc` and `nofm` in jproc). They are inherited on fork, kept on exec, and capped at FDMAX = 256 (`/proc/sys/fs/nr_open`).
   - setrlimit now checks its input. cur > max gives EINVAL. Raising above nr_open, or above the hard limit without root, gives EPERM. Other resources still succeed without effect.
   - The soft limit is enforced by `lx_fd_install` (all fd creation in the Linux layer), by open (before `vfs_open`), by dup2/dup3 (EBADF) and by F_DUPFD (EINVAL).
   - prlimit64 now applies the new limit and works on other pids (same uid or root).
   - `/proc/<pid>/limits` now shows the real values. Before, it showed a fixed 1024/1048576.
   - Behaviour change: a program that raises NOFILE above 256 now gets EPERM instead of a silent success. That matches Linux with nr_open = 256.
7. **HD Audio probe — done** (93679405). The scout's facts are confirmed.
   - The probe now tries every class 04/03 controller. After those, it tries Intel class 04/01 controllers that have a 64-bit memory BAR (DSP-enabled parts). AC97 is never touched, because its BARs are I/O. The 0x040380 case is class 04/03, so it is covered by the first pass.
   - On each controller it tries every codec in STATESTS and takes the first with an analog line-out, speaker or headphone path. HDMI pins were already ignored by `scan()`, so an HDMI-only codec or controller is skipped.
   - A skipped controller has its CORB and RIRB stopped, so it no longer writes into the shared buffers by DMA.
   - BARs above 4 GB are now mapped with `kapi_mmio`, the same way as e1000e and igb.
   - If no candidate has an analog path, the result is the same as before: the driver logs and returns 0, and AC97 is tried.
   - DMA buffers are allocated once (the old code allocated them on every probe) and the PCM buffer is zeroed.
   - The logic is written from the HDA spec; no code from Linux intel-dsp-config.c (GPL-2.0-only) was used.
   - Simulator: there is now a second PCI device, an HDMI codec model, a codec-address dispatch and a mapping for high BARs. That adds 7 new checks.

## Not tested
- Nothing ran on real hardware or in QEMU: no real sshd or pam_loginuid, htop, tmux, Go programs, or a multi-CPU sysfs listing.
- HDA on real AMD or Intel DSP machines. Whether the legacy HDA registers respond on a given 0x0401 part depends on the firmware.
- The fs and accounts harnesses were not run (only the linux harness was allowed). unixfs.c changed only by adding new functions.

## Open issues / notes for the lead
- `struct jproc` gained 4 ints. Everything that includes jobs.h must be rebuilt; build.sh does that.
- For full Linux ET semantics, add data levels in `lx_ep_lvl` for ptys, timerfd and signalfd.
- Nanosecond times need an FsFile and NVX4 format bump; that is a decision for the lead.
- Memory facts: none were used or are new.

## Fix round (after merge into main; `git merge main` was a fast-forward to 054cf449)
There are 8 commits on `bolt`, one per finding. Every touched file passes ccheck.sh with no new warnings (compared against the pre-fix tree).

Tests after the last commit:
- `kgap_t.c` has grown to 71 checks. Under the linux harness: ALL PASS. The full default `lxharness` run: ALL PASS. Fuzz seeds 1-3 (60000 calls each): ALL PASS.
- The new epoll, times and nofile checks also pass when the same binary runs on real host Linux.
- drvsim: ALL PASS (16 HDA checks).

1. **Sentinel #1, the cmsg length wrap** (1923404a). `files_dirs.c` `lx_un_msg` now checks `cl<16||cl>ctllen-off` and never forms the sum. New test: `sendmsg` with a second cmsg of length 2^64-16. On the old code the harness hangs and times out (rc 124); now it passes.
2. **Probe #1, EPOLLET lost wake-ups** (05335878).
   - The re-arm counter is now bumped inside `lx_read`, `lx_write` and `lx_prw`, the shared paths used by readv/writev, splice, tee, vmsplice, copy_file_range, preadv2/pwritev2 and sendfile.
   - The syscall list also counts 40/275/276/278/326/327/328, with both fds where there are two.
   - **Extra bug found and fixed:** `sendfile` to a TCP or Unix socket, a pty or a memfd went through `vfs_write`, which does not handle those types. The bytes were reported as sent but never delivered. sendfile now uses the socket write path (`lxm_kio`), so EAGAIN and blocking behave like `write`.
   - New tests: sendfile until EAGAIN, the peer drains, then EPOLLOUT is reported again; and a test that sendfile actually delivers the data.
   - Still open: a FIFO drained through a different open file (a second `open()` of the FIFO) and refilled between two `epoll_wait` calls can still miss an event.
3. **Probe #2, utimes/futimesat** (42d81c45). Calls 235 and 261 now go through `ux_lookup` and `lx_utimens_hv`, with the same owner/root (EPERM) and write-access (EACCES) checks as utimensat. They reject tv_usec >= 1000000 with EINVAL and can now set time 0. Tested, including the non-root cases.
4. **Probe #3, NOFILE above 256** (b92e98fe). Requests above FDMAX are now clamped to 256 instead of failing; `ulimit -n 1024` and RLIM_INFINITY succeed. EPERM is left only for an unprivileged process raising the hard limit above its current hard limit (compared after clamping). Tested in the harness: clamp to 256, and EPERM for an unprivileged raise after the hard limit was lowered to 16.
5. **Probe #4, fork from a thread** (ae97e4ee). `jc_fork` now copies `aluid`, `asess`, `nofc` and `nofm` from the thread-group leader. Tested: a worker thread forks after main changed NOFILE, and the child sees the new limit.
6. **Probe #5/#6, HDA** (6d27a16d).
   - In pass 2 (Intel class 04/01) the probe first turns on only memory decoding. It then reads VMAJ, VMIN and GCAP and skips the device unless VMAJ=1, VMIN=0 and GCAP is not 0 or 0xFFFF. This happens before any register write and before bus mastering is enabled.
   - A skipped controller now has its CORB/RIRB run bits cleared and polled until they read 0 (bounded to 10 ms), then its PCI bus master bit is cleared.
   - New simulator checks: a Smart Sound-style DSP with no HDA version registers is not written at all; the skipped HDMI controller ends with bus master off.
7. **Probe #8, uid check inside namespaces** (7a562f1d). The `/proc/<pid>/cwd|root|exe` handler used inside mount namespaces and chroots now applies the same rule as outside: root, the caller's own process, or the same uid, else EACCES. Tested with `unshare(CLONE_NEWNS)` plus a tmpfs mount, then a setuid(1000) grandchild. The test fails on the old code and passes now.
8. **Sentinel #4, TCP RST** (c17e3bed). `lxs_tcp_in` now handles RST like this:
   - In established and SYN-RECEIVED, a RST is accepted only when seq == rcv_nxt.
   - Inside the receive window but not exact, an established socket sends a challenge ACK and ignores the RST.
   - Outside the window, the RST is ignored.
   - In SYN-SENT, a RST is accepted only if it ACKs our SYN (RFC 793), so "connection refused" still works.
   - **Not tested by packet injection** (no harness for this exists). Only the regression runs above cover it.

Still open (not in this round): Probe #7, BARs above 512 GB being mapped only into the current CR3, and the nanosecond timestamps.
