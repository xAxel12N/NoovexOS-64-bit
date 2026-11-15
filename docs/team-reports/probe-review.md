# Probe review: Bolt's kernel-gap commits (0aef5934..main)

Scope: 00f14e74 (loginuid), f429b868 (sysfs cpuN), 535ffb88 (/proc/<pid>/cwd|root|exe), 80f7d2f5 (EPOLLET), e3e25c52 (utimes/fchown), de13d282 (RLIMIT_NOFILE), 93679405 (HDA). Read-only review; nothing was built or run.

## Ranked findings

### 1. MEDIUM-HIGH: EPOLLET can lose wake-ups for I/O done through syscalls the hook does not count
- **Where:** sys/linux/syscalls/poll_epoll.c:31 (`lx_ep_io`) and :32 (`lx_ep_new`)
- **Issue:** An ET entry is re-armed only when `g_lx_ioc[vi]` changes, when the rx level rises, or when an `epoll_wait` scan sees the bit drop. The counted syscalls are 0, 1, 17-20, 43-47, 288, 295, 296, 299 and 307. Several syscalls that this kernel implements are missing:
  - sendfile 40 (fds_and_system_info.c:226)
  - preadv2/pwritev2 327/328 (fds_and_system_info.c:325)
  - splice 275, tee 276, vmsplice 278 and copy_file_range 326 (more_syscalls.c:699-702)
- **Failure scenario:** nginx uses EPOLLET and usually runs with `sendfile on`.
  1. EPOLLOUT was reported, so `rep` includes OUT.
  2. `sendfile()` hits a full socket buffer and gets EAGAIN. This is not counted.
  3. The peer drains the buffer before nginx calls `epoll_wait` again. That is likely on loopback or a fast LAN.
  4. At the next scan, OUT is set and in `rep`, `ioc` is unchanged and the rx level is unchanged, so no event is reported.
  5. The download stalls until nginx's timeout.

  splice-based proxies (haproxy, socat) can lose wake-ups the same way, on the read side. The same can happen when a FIFO is drained through a *different* vfile (a second `open()` of the FIFO) and refilled between two scans: `lvl` is the same as before, so no event.
- **Fix:** Count both fds for these calls (sendfile: a1 and a2; splice and copy_file_range: a1 and a3; tee: a1 and a2; vmsplice: a1; 327/328: a1). A more robust fix is to bump the counter in the shared vfile read/write/EAGAIN paths instead of a syscall-number list. Add an ET test: sendfile to a full socket, drain, then epoll_wait.

### 2. MEDIUM (security, incomplete fix): utimes/futimesat have no permission check at all
- **Where:** sys/linux/syscalls/fds_and_system_info.c:332 (`case 235: case 261:` call `ux_utime`) and fs/vfs/unixfs.c:159 (`ux_utime` never checks the owner).
- **Issue:** utimensat (280) and utime (132) now follow Linux. `utimes(2)` and `futimesat(2)` still let any uid set arbitrary atime/mtime on any file, including root-owned files. Separately, `at==0` there means "now", so an explicit epoch 0 cannot be set.
- **Failure scenario:** An unprivileged user backdates or forward-dates /etc or root files. That defeats make, backup tools and tripwire-style checks. busybox `touch` and older code still use `utimes()`.
- **Fix:** Route 235/261 through `ux_lookup` and `lx_utimens_hv`. A NULL pointer means h={1,1}; otherwise h={2,2} with tv_sec, and reject tv_usec >= 1000000 with EINVAL.

### 3. MEDIUM (regression): RLIMIT_NOFILE above 256 now fails with EPERM, even for root
- **Where:** sys/linux/syscalls/memory_and_signals.c:349 (`if(v[1]>(u64)FDMAX||...) return -LX_EPERM`)
- **Issue:** This is formally "Linux with nr_open=256". Real systems have nr_open = 1048576, so software never expects this error. Before this change setrlimit always succeeded.
- **Failure scenario:**
  - `ulimit -n 1024` in a `set -e` start script now aborts the script.
  - nginx `worker_rlimit_nofile` logs alerts.
  - Daemons that treat the setrlimit failure as fatal stop working.
  - Root gets EPERM too.
- **Fix:** When the request is above FDMAX, clamp both values to FDMAX and return 0. At least do this for root and for "raise to RLIM_INFINITY" requests. The other option is to raise FDMAX. Keep the EINVAL for cur > max.

### 4. LOW-MEDIUM: new per-process fields are inherited from the forking *thread*, not the process
- **Where:** core/proc/jobs/thread_groups_and_exit.c:223 (`jc_fork`: `*c=*par`) and :236 (`jc_clone_thread`: `*c=*par`). The setters write the leader: memory_and_signals.c:350 (`L->nofc/nofm`) and poll_epoll.c:299-300 (`T->aluid/asess`, T = leader).
- **Issue:** A thread holds a snapshot taken when it was cloned. When a non-leader thread forks, the child gets that stale copy of `nofc`, `nofm`, `aluid` and `asess`, not the current values in the leader.
- **Failure scenario:** A multi-threaded program (Go, Java, Electron) changes NOFILE in main and then spawns from a worker thread. The child runs with the old limits. A threaded PAM user sets loginuid, and children forked from other threads show the old loginuid and sessionid.
- **Fix:** In `jc_fork` after the struct copy: `c->aluid=L->aluid; c->asess=L->asess; c->nofc=L->nofc; c->nofm=L->nofm;`

### 5. LOW-MEDIUM (hardware risk): HDA pass 2 also pokes Intel audio DSPs that are not HDA
- **Where:** drivers/sound/hda.c:210-216 (`hda_candidate` k==1 accepts any 8086 class 04/01 with a 64-bit memory BAR) and :224-228 (`kapi_mmio`, `kapi_pci_enable`, then `hda_ctl_init` writes GCTL, CORB and RIRB).
- **Issue:** Haswell/Broadwell "Smart Sound" (for example 8086:9c36 and 8086:9cb6) is class 04/01 with a 64-bit BAR0. That BAR0 is DSP space, not HDA registers. The probe toggles 0x08, writes DMA addresses to 0x40-0x5E and enables the device (probably bus-mastering, depending on `kapi_pci_enable`).
- **Failure scenario:** Broadwell laptops and Chromebooks with I2S codecs have an HDMI-only 04/03 controller, so pass 1 skips it and pass 2 probes the DSP. The usual result is "controller reset failed", but what writes to DSP SRAM or shim registers do is undefined.
- **Fix:** Before any write and before enabling the device, check that the registers look like HDA: VMAJ (0x03)==1, VMIN (0x02)==0, and GCAP is neither 0 nor 0xFFFF. Or allow-list the HDA-compatible SKL+ DSP device IDs.

### 6. LOW: skipped HDA controllers are not fully stopped
- **Where:** drivers/sound/hda.c:230 (`w8(0x4C,0); w8(0x5C,0);`)
- **Issue:**
  - The run bits are cleared, but the code does not poll for them to read back 0, which the HDA spec requires.
  - The CORB/RIRB base registers still point at the shared `corb`/`rirb` pages, which the next controller reuses at once.
  - Bus-mastering stays on.
  - The controller is left out of reset, so codecs can still send unsolicited responses.
- **Failure scenario:** A late RIRB DMA write from controller A lands in the ring that controller B is now using, and B gets a bogus response during `scan()` (wrong widget caps). This is unlikely but hard to debug.
- **Fix:** Poll until the run bits clear, then put the skipped controller back into reset (GCTL.CRST=0) and/or clear the PCI bus-master bit.

### 7. LOW (crash risk, needs a check): BARs above 512 GB are mapped only into the current CR3
- **Where:**
  - drivers/acpi/acpi_dev.c:153-158 (`acpi_hi_map` adds new PML4/PDPT entries to `cpu_get_pt()`)
  - mem/pmm_vmm.c:141-145 (process PML4s copy the kernel entries *by value* when created)
  - drivers/sound/hda.c:225 (now calls `kapi_mmio` for 64-bit BARs, which the old code refused)
- **Issue:** `snd_init()` is lazy. It is reached from Linux ioctls in process context (files_dirs.c:366 and timers_and_handles.c:13/65/92). If the active CR3 there is the process CR3 and the BAR has a PML4 index of 1-7 (at or above 512 GB, common for AMD 64-bit BARs), the mapping exists only in that process's tables. A later HDA access from the main loop or another process then page-faults in the kernel. BARs below 512 GB go into the shared PDPT of PML4[0] and are fine.
- **Fix:** Confirm which CR3 is active during Linux syscalls. Make `acpi_hi_map` write `g_kern_cr3` and pre-create PML4 entries 0-7 at boot, so every PML4 copied later shares them.

### 8. LOW (security, existing code): the new cwd/exe permission check does not apply inside containers
- **Where:** sys/linux/syscalls/namespaces_containers.c:588-589
- **Issue:** When `lxn_on()` is true, the hook answers `/proc/<pid>/cwd|root|exe` itself and checks no uid. The new check at memory_and_signals.c:229 is never reached. Bolt did not change this code.
- **Fix:** Apply the same check (`g_uid==0` or the same uid) in the hook.

### 9. LOW (Linux compatibility): small differences in /proc/<pid> links, chown and utimensat
- **memory_and_signals.c:229:** access is a plain uid comparison. Linux uses `ptrace_may_access`, which also honours non-dumpable processes (PR_SET_DUMPABLE 0, processes that changed credentials). There are no setuid binaries today, so the impact is low.
- **memory_and_signals.c:230:** `/root` is always "/" for a chrooted target seen from outside, where Linux shows the chroot path. A process in a PID namespace without its own root or mounts (`lxn_on()` false) gets global pid numbers here.
- **readlink with bufsiz 0:** returns 0 instead of EINVAL (this was already true for exe).
- **fs/vfs/unixfs.c:153 (`ux_chown_k`), same as the older `ux_chown` at :130:**
  - An owner may set *any* gid; Linux requires membership in that group.
  - chown does not clear S_ISUID/S_ISGID.
  - This is not exploitable while setuid is not honoured on exec.
- **fds_and_system_info.c:471:**
  - AT_EMPTY_PATH is accepted, but an empty path is not resolved to dirfd.
  - A NULL path combined with AT_SYMLINK_NOFOLLOW should give EINVAL.

### 10. NIT
- **poll_epoll.c:31:** `lx_ep_io` runs after dispatch with `fdt()` of the *current* `g_jcur`. The code after it re-fetches `p`, which suggests `g_jcur` can change during dispatch. If it does, the wrong vfile counter is bumped. That causes only a spurious ET event; there is no memory risk, because `vi` is bounds-checked.
- **Non-atomic counters:** `g_lx_ioc[]++` and `++g_lx_asess` are not atomic. That is fine only if the Linux layer is serialized on SMP.
- **Memory:** `struct lx_ep` grows to about 5 KB, so `g_ep[256]` is about 1.3 MB of .bss (512 KB more than before).

## Checked and found correct
- **loginuid permissions:**
  - Open for writing is allowed only for root or the same uid.
  - Write needs `g_uid==0` *and* a target that is the caller's own leader; otherwise EPERM.
  - The target is resolved again from the vpath at write time, so passing the fd to another process gains nothing.
  - The parser bounds the value to 32 bits.
  - `sx_wfd_write` checks the user buffer with `lx_ok` and caps the length at 4096.
- **struct jproc:** `jc_create` zeroes the whole struct (resume_and_fds.c:314), so the new fields start as "unset" or the default limit. fork and clone copy them; exec keeps the leader's values. No exit path needs to reset them.
- **cwd/exe for other pids:** zombie and TDEAD pids give ENOENT. `dt_path_n` is bounded, and `lx_put` is used for the user buffer. A different uid gets EACCES.
- **epoll:**
  - Level-triggered entries are unchanged: `lx_ep_new` returns `re` as-is when ET is not set.
  - `lx_ep_del` moves the new arrays.
  - CTL_ADD and CTL_MOD reset the ET state.
  - Nested epoll does not change the state.
- **RLIMIT_NOFILE:**
  - dup2 above the limit gives EBADF and F_DUPFD gives EINVAL, as in Linux.
  - pipe cleans up correctly when the second install fails (files_dirs.c:431-432).
  - `lx_open` checks for room before `vfs_open`.
  - prlimit64 on another pid checks the uid.
- **utimensat/futimens:** the rules (explicit times need the owner or root, else EPERM; "now" needs the owner or write access, else EACCES; both OMIT returns 0) match Linux.
- **sysfs cpuN:** `sx_read` only runs after `sx_node` has accepted the path (sysfs.c:405-407). Writing needs root.
- **HDA on machines that worked before:** `kapi_mmio` returns the identity mapping below 4 GB (kapi.c:36), so QEMU and older machines get the same `R`. When codec 0 is analog (QEMU hda-duplex, typical Realtek laptops) it is picked first, as before. If no controller has an analog path, the probe still returns 0 and AC97 is tried. DMA buffers are allocated once. For the owner's AMD HP laptop the walk is an improvement: the GPU HDMI function comes first and the analog 1022 controller second.
