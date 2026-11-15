# Echo: Unix programs and Linux-compatibility gaps (NoovexOS 64 v10.98)

Scope: read-only review of `sys/linux/` and checks against upstream sources. Nothing was run inside NoovexOS, so "no gap found" means the code review found nothing missing. It does not mean the program was tested.

Note on paths: while I worked, `sys/linux/linux_parts/NN_*.c` was renamed to `sys/linux/syscalls/*.c`. The contents and line numbers are the same. This report uses the new names:
`fds_and_system_info.c` (was 07, holds `lx_dispatch`), `memory_and_signals.c` (05), `fifos_and_names.c` (01), `poll_epoll.c` (04), `more_syscalls.c` (09, `lx_more`), `namespaces_containers.c` (10), `sysfs.c` (08), `proc_extra.c` (11), `inotify.c` (03), `timers_and_handles.c` (02), `files_dirs.c` (06).

## 1. System-call coverage (checked against the code, not the docs)

How a call is dispatched: `linux_syscall()` → seccomp → `lxn_hook_pre()` (namespaces) → `lx_dispatch()` (fds_and_system_info.c:163) → `lx_more()` (more_syscalls.c:671) → `lxm_numa()` → `-ENOSYS`.
In total 305 syscall numbers are handled. I compared them with Linux master `arch/x86/entry/syscalls/syscall_64.tbl`.

### Not handled at all (return -ENOSYS)
- **Matter for this program list:**
  - 101 `ptrace`
  - 206-210 and 333 `io_setup`, `io_submit`, `io_getevents`, `io_pgetevents` (old kernel AIO)
  - 425-427 `io_uring_*`
  - 300/301 `fanotify_*`
  - 323 `userfaultfd`
  - 321 `bpf`
  - 440 `process_madvise`
  - 448 `process_mrelease`
  - 454-456 `futex_wake/wait/requeue` (futex2)
  - 444-446 `landlock_*`
  - 462 `mseal`
  - 451 `cachestat`
  - 457/458 `statmount`/`listmount`
  - 428-433 and 442 (new mount API)
  - 463-469 (`*xattrat`, `file_getattr`/`file_setattr`)
- **Legacy or irrelevant:**
  - 134 `uselib`, 136 `ustat`, 139 `sysfs`, 154 `modify_ldt`, 156 `_sysctl`
  - 172/173 `iopl`/`ioperm`
  - 174-178 and 313 (kernel modules)
  - 179 and 443 (quota)
  - 205/211 `set/get_thread_area`
  - 212, 214-216, 236, 246, 320 (kexec)
  - 331 `pkey_free`, 335/336 (uprobes), 447 `memfd_secret`, 453 `map_shadow_stack`
  - 459-461 (LSM), 470-472
- **Handled but stubbed or partial** (verified in code):

| Nr | Call | Behaviour | Where |
|---|---|---|---|
| 25 | mremap | always `-ENOMEM` (glibc realloc falls back) | `lx_dispatch` |
| 56/58/435 | clone/vfork/clone3 | `CLONE_VM` without `CLONE_THREAD` becomes a COW **fork**, and `CLONE_VFORK` does not suspend the parent. Because of this, glibc `posix_spawn` cannot report exec errors: the child writes `args.err`, but in a copied address space the parent never sees it. clone3 returns ENOSYS with `CLONE_PIDFD` (callers fall back). | `memory_and_signals.c:lx_clone` |
| 93 | fchown | validates the fd, **changes nothing** | `lx_dispatch` |
| 280 | utimensat(fd, NULL) = futimens | validates the fd, **changes nothing**. With a path, it stores **whole seconds only** and ignores the `AT_SYMLINK_NOFOLLOW` flag. | `fds_and_system_info.c:lx_utimens` |
| 132/235/261 | utime/utimes/futimesat | seconds only | `lx_dispatch` |
| stat family | - | `st_atim/st_mtim/st_ctim` nanoseconds are always 0. `st_dev` is the constant 0x4e5658 for every file. | `fifos_and_names.c:lx_stat_out` |
| 86/265 | link/linkat | new name's last part must be ≤51 characters, or ENAMETOOLONG | `lx_dispatch` |
| 160 | setrlimit | returns 0 and changes nothing | `lx_dispatch` |
| 302 | prlimit64 | own pid only; the new limit is ignored | `lx_dispatch` |
| 97 | getrlimit | NOFILE = 256 (FDMAX); everything else unlimited except the stack | `memory_and_signals.c:lx_rlimit` |
| 116 | setgroups | root only, no effect | `lx_dispatch` |
| 98 | getrusage | only user time from ticks | `lx_dispatch` |
| 270/271 | pselect6/ppoll | signal-mask argument ignored (race for SIGCHLD-style loops) | `lx_dispatch` |
| 232/233/281/441 | epoll | **level-triggered only**: `EPOLLET` (bit 31) is dropped and only event bits `&0x3F` are kept, so `EPOLLRDHUP` is lost. `EPOLLONESHOT` is honoured. 128 fds per set, 256 sets. | `poll_epoll.c:lx_epoll_wait/lx_epoll_ctl` |
| 248-250 | add_key/request_key/keyctl | -EOPNOTSUPP (pam_keyinit is optional) | `lx_more` |
| 188-199 | xattrs | -ENOTSUP | `lx_dispatch`/`lx_more` |
| 36/38 | itimers | `ITIMER_VIRTUAL`/`ITIMER_PROF` never fire | - |
| 334 | rseq | ENOSYS (glibc falls back) | - |
| 303/304 | name_to_handle_at/open_by_handle_at | EOPNOTSUPP | - |

Present and fairly complete:
- epoll, eventfd, timerfd, signalfd, inotify, memfd
- POSIX timers, pidfd_open/send_signal/getfd, waitid(P_PIDFD)
- seccomp (BPF interpreter, TSYNC, `RET_ERRNO/TRAP/KILL/LOG`), prctl NNP
- SysV shm/sem/msg, POSIX mqueue
- splice/tee/vmsplice, copy_file_range, statx, openat2, execveat
- process_vm_readv/writev, kcmp, futex_waitv
- unshare/setns/chroot/pivot_root/mount (namespaces)

### Fixed-size kernel tables that block large toolchains (verified)
| Limit | Value | File |
|---|---|---|
| Files in the whole file system (`FS_N`) | **16384** (ramfs and on-disk NVX4 share it) | `include/nvx/fs/vfs/ramfs.h` |
| Directories (`DN_MAX`) | 4096 | `include/nvx/fs/vfs/dirtree.h` |
| Largest file (`FS_CAP`) | 1 GiB; `struct vfile.off` is `int` | `ramfs.h` |
| Path length (`LX_PATH`) | **256 bytes** (Linux allows 4096) | `sys/linux/linux.c` |
| fds per process / open files system-wide | 256 / 1024 | `ramfs.h` (`FDMAX`, `VMAXOPEN`) |
| Process+thread slots (`JMAX`) | 256 | `include/nvx/core/proc/jobs.h` |
| PTYs (`PTY_MAX`) | 32, 16 KiB buffers | `fifos_and_names.c:107` |
| inotify | 16 instances, 64 watches each | `timers_and_handles.c:320` |

## 2. /proc and /sys (verified)

**Top-level `/proc`**
- Listed by `fifos_and_names.c:lx_procent`: `self`, `thread-self`, `cpuinfo`, `meminfo`, `swaps`, `version`, `uptime`, `loadavg`, `mounts`, `filesystems`, `stat`, `partitions`, `vmstat`, `diskstats`.
- Also served but not listed (`proc_extra.c:lx_proc_sys_gen`):
  - `/proc/net/dev`, `/proc/nvxpack`
  - `/proc/sys/kernel/{pid_max,threads-max,ostype,hostname,osrelease,random/boot_id,random/uuid}`
  - `/proc/sys/fs/{file-max,nr_open}`
  - `/proc/sys/vm/{swappiness,drop_caches,...}`

**`/proc/<pid>/`**
- Generated by `poll_epoll.c:lx_proc_extra_gen`:
  - `stat` (utime only, stime 0), `status`, `statm`, `cmdline` (works for other pids), `comm`
  - `environ` (always empty), `io` (all zeros), `limits`
  - `oom_score_adj`, `oom_adj`, `oom_score`, `wchan`, `loginuid`, `sessionid`, `mountinfo` (fixed text)
  - `task/<tid>/{comm,stat,status}`
- Served elsewhere:
  - `exe` link and `fd/` (**own process only**): `memory_and_signals.c:lx_proc_exe` and `fifos_and_names.c:lx_procdir`
  - `maps` (**own process only**; every non-stack mapping is shown as `r-xp`): `poll_epoll.c:lx_proc_maps`
  - `uid_map`, `gid_map`, `setgroups`, `cgroup`, `mounts`, `ns/*`: only after a namespace has been created (`g_lxn_any`): `namespaces_containers.c`
- All generated `/proc` files refuse an open for writing with **EACCES** (`lx_proc_extra`).

**Missing in `/proc`**
- `/proc/<pid>/cwd` and `root` (outside containers), `mem`, `auxv`, `smaps`, `smaps_rollup`, `pagemap`, `attr/`, `autogroup`, `syscall`, `stack`
- `exe`, `fd/` and `maps` of other processes
- `/proc/net/{tcp,tcp6,udp,unix,route}`
- `/proc/sys/kernel/yama/ptrace_scope`

**`/sys/devices/system/cpu`**
- Has `online`, `possible`, `present` and `kernel_max`.
- **No `cpu0`..`cpuN` directories** (`sysfs.c:295`), and no `cpufreq`.

## 3. Ranked table

| Rank | Program | Needs | Missing in NoovexOS (exact) | Fix effort | Value |
|---|---|---|---|---|---|
| 1 | openssh-server (sshd) | pty (`/dev/ptmx`, TIOCGPTN/TIOCSPTLCK, TIOCSCTTY) ✓; AF_UNIX+SCM_RIGHTS ✓; chroot ✓ (`lxn_chroot`); seccomp sandbox with arch check ✓; setresuid ✓; PAM | **Login fails in PAM:** Debian `/etc/pam.d/sshd` has `session required pam_loginuid.so`. pam_loginuid opens `/proc/self/loginuid` with `O_RDWR`, which gets EACCES from `lx_proc_extra` (not ENOENT), so the module returns `PAM_SESSION_ERR` (checked against linux-pam source). `/proc/self/uid_map` is also absent unless namespaces were used. No service manager, so the postinst cannot start sshd (`systemctl start` fails by design). setgroups has no effect. | S | High (remote shell with real OpenSSH; dropbear exists as fallback) |
| 2 | cron | fork/setsid ✓, PAM, `/dev/log` | Debian `/etc/pam.d/cron` also has `session required pam_loginuid.so`, so jobs hit the same `PAM_SESSION_ERR`. No service manager to start the daemon. pam_limits' setrlimit is a silent no-op (harmless). | S | Medium |
| 3 | htop | `/proc/stat`, `/proc/meminfo`, `/proc/<pid>/{stat,statm,status,cmdline,comm,io,task}` ✓, `/sys/devices/system/cpu/cpuN` | **CPU count is 1**: htop counts `cpuN` directories in `/sys/devices/system/cpu` (`LinuxMachine_updateCPUcount`), and there are none. Missing `/proc/<pid>/cwd`, `smaps_rollup`, `autogroup`, `ns/pid`, `attr/current`, and `exe` for other pids (optional columns). `io` is all zeros and `stime` is always 0. | S | High (top-like tool everyone uses) |
| 4 | tmux | forkpty ✓, tcgetpgrp on master (0x540F) ✓, TIOCGSID ✓, AF_UNIX+fd passing ✓, libevent on epoll (LT) ✓, `/proc/<pid>/cmdline` ✓ | `/proc/<pid>/cwd` missing, so `osdep_get_cwd()` fails and `#{pane_current_path}` / `new-window -c` from the current dir do not work. 32 PTYs system-wide (one per pane). | S | High |
| 5 | gcc / make / binutils | posix_spawn/vfork, FIFOs (make 4.4 jobserver) ✓, mmap ✓, flock ✓ | No missing syscall found. Weak points: posix_spawn exec errors are not reported (clone `CLONE_VM` = fork, see table above); mtimes have 1-second granularity (make may miss rebuilds within one second); futimens and fchown are no-ops; `LX_PATH` 256. Headers (`linux-libc-dev` 2559 files + `libc6-dev` 518) count against `FS_N`. | S-M | High |
| 6 | nodejs | V8 (mmap/mprotect/MADV_DONTNEED ✓), libuv epoll/eventfd/inotify ✓, io_uring (falls back on ENOSYS) | Electron's Node already runs on NoovexOS (per ARCHITECTURE.md), so no blocker found. NOFILE is a hard 256 and setrlimit raises are ignored. `node_modules` trees can exceed `LX_PATH`=256. | S | High |
| 7 | rsync | utimensat, lchown, mknod, xattrs (-X), hardlinks (-H) | mtime set and read in seconds only (stat nsec = 0); `AT_SYMLINK_NOFOLLOW` ignored (symlink times go to the target); fchown is a no-op; `-X` gets ENOTSUP; link name ≤51 chars; `st_dev` is the same for all files (`-x` cannot tell file systems apart). | M | Medium |
| 8 | rustc / cargo | threads, flock ✓, statx ✓, copy_file_range ✓, posix_spawn, jobserver pipes, `/proc/self/exe` ✓ | No missing syscall found. Limits: `LX_PATH` 256 (deep `target/` and registry paths), NOFILE 256, 1 GiB file cap. ARCHITECTURE.md v10.11 records an earlier block from storage (358 MB toolchain). That predates the disk install, and I did not recheck it. | M | Medium-High |
| 9 | golang | clone threads ✓, sigaltstack ✓, futex ✓, epoll_pwait ✓, tgkill preemption signals, pidfd/waitid ✓ | **`golang-1.24-src` alone has 14,111 files** (packages.debian.org trixie filelist), against `FS_N`=16384 for the whole file system, so the toolchain cannot be installed next to a normal system. Go's netpoller registers fds with `EPOLLET`. NoovexOS treats that as level-triggered, so expect busy wake-ups on writable sockets (correct results, wasted CPU). | M (raise FS_N/DN_MAX) + S (EPOLLET) | Medium |
| 10 | nano | ncurses, TIOCGWINSZ, SIGWINCH | None found | - | Medium |
| 11 | zsh / fish | job control (setpgid, TIOCSPGRP/TIOCGPGRP ✓), posix_spawn, pselect | pselect6/ppoll ignore the signal mask (possible lost-wakeup race); otherwise none found | S | Medium |
| 12 | mc | subshell on a pty ✓, ncurses | None found besides the 32-PTY limit | - | Low-Medium |
| 13 | man-db | fork/pipes, libseccomp sandbox (seccomp ops 1/2/3 ✓) | None found (fallback: `MAN_DISABLE_SECCOMP=1`) | - | Low |
| 14 | strace | `ptrace` (PTRACE_SEIZE/SYSCALL/GETREGS/PEEKDATA, `PTRACE_O_TRACESYSGOOD`), waitpid stop states, `/proc/<pid>/status` TracerPid | **syscall 101 ptrace is not handled at all (ENOSYS)** | L | High for developers (the kernel already has `lx_sc_note` tracing as an in-kernel substitute) |
| 15 | gdb | ptrace (attach, regs, single-step, breakpoints via POKETEXT), `/proc/<pid>/maps` and `/proc/<pid>/mem` of another process, personality(ADDR_NO_RANDOMIZE) (returns 0 ✓) | ptrace missing; `/proc/<pid>/mem` missing; `maps` works for the own process only | L | High for developers |

## 4. Kernel gaps to close, in priority order

1. **Make `/proc/self/loginuid` acceptable to pam_loginuid** (unblocks sshd, cron and `login`). It is the cheapest fix with the highest value. Two options:
   - accept `O_RDWR` and store the uid in `struct jproc` (new field, read back by the `loginuid` generator), or
   - drop `loginuid` from the generator so the open fails with ENOENT and the module returns `PAM_IGNORE`.

   Where: `sys/linux/syscalls/poll_epoll.c:lx_proc_extra` (the write check `(flags&3)!=0 → EACCES`) and `lx_proc_extra_gen` (the `"loginuid"` branch). S.
2. **`/sys/devices/system/cpu/cpuN/` directories** (with an `online` file each, plus optional `cpufreq/scaling_cur_freq`). Fixes htop's CPU count and helps libuv/node `os.cpus()`. Where: `sys/linux/syscalls/sysfs.c`, the listing at :295 (`SXE` entries), and `sx_stat`/`sx_read` for the `devices/system/cpu/` prefix (:265, :327). S.
3. **`/proc/<pid>/cwd` and `/proc/<pid>/root` links for any pid** (tmux, htop, lsof-style tools). Where:
   - `sys/linux/syscalls/memory_and_signals.c:lx_proc_exe`: add `cwd` from `jc_lead(..)->cwd` via `dt_path`, and allow other pids for `exe`/`cwd`.
   - Add both names to the per-pid list in `fifos_and_names.c:lx_procent`.

   S.
4. **Honour `EPOLLET` and keep `EPOLLRDHUP`** (Go, nginx-style servers, less wasted CPU). Where: `sys/linux/syscalls/poll_epoll.c:lx_epoll_ctl` (stop masking with `&0x3F`) and `lx_epoll_wait`: remember the last reported readiness per entry and report only on change when bit 31 is set. M.
5. **Real `posix_spawn`/vfork semantics**: for `CLONE_VM` without `CLONE_THREAD`, share the address space, and with `CLONE_VFORK` block the parent until the child calls exec or exits. Fixes the lost exec errors in make, gcc and cargo and avoids COW cost. Where: `sys/linux/syscalls/memory_and_signals.c:lx_clone` and `jc_fork` (`core/proc/jobs/`). M.
6. **Nanosecond timestamps, a working futimens/fchown, and `AT_SYMLINK_NOFOLLOW` in utimensat** (rsync, make, cp -p, tar, dpkg). Where:
   - `fds_and_system_info.c:lx_utimens` and the case 280 NULL-path branch
   - case 93 in `lx_dispatch`
   - `fifos_and_names.c:lx_stat_out` (fill +80/+96/+112)
   - storage: `FsFile` mtime is `u32` seconds (`ramfs.h`), and NVX4 needs an extra field

   M.
7. **Raise `LX_PATH` from 256 towards 4096**, then the 51-character link limit and `vfile.vpath[128]`. These affect node_modules, cargo and go module paths. Where: `sys/linux/linux.c` (`#define LX_PATH`), `lx_path0`, and the many stack buffers `char pa[LX_PATH]` in `lx_dispatch` (watch kernel stack size). M.
8. **Raise `FS_N`/`DN_MAX`, or make them dynamic** (golang, a full gcc + rust + node install). Where: `include/nvx/fs/vfs/ramfs.h`, `include/nvx/fs/vfs/dirtree.h`, and the NVX4 metadata format (`fs/nvx/nvx4.c`). ARCHITECTURE.md notes that a format change forces a reinstall. M-L.
9. **Honour RLIMIT_NOFILE and raise `FDMAX`** (setrlimit/prlimit64 currently succeed but change nothing). Where: `memory_and_signals.c:lx_rlimit`, case 160/302 in `lx_dispatch`, and `FDMAX` in `ramfs.h` (`struct jproc fd[]`). M.
10. **`ptrace`** with the core requests TRACEME, ATTACH, SEIZE, CONT, SYSCALL, GETREGS/SETREGS, PEEK/POKEDATA, SINGLESTEP, GETSIGINFO, SETOPTIONS (TRACESYSGOOD/EXITKILL/TRACECLONE), INTERRUPT. Also needs `/proc/<pid>/mem`, other-pid `/proc/<pid>/maps`, TracerPid in `status`, and ptrace-stop states in wait4/waitid. Where:
    - a new `sys/linux/syscalls/ptrace.c` dispatched from `lx_dispatch` case 101
    - stop and resume hooks in `linux_syscall()` (fds_and_system_info.c, around the `lx_sc_note` call) and in the signal delivery path (`core/proc/jobs`)
    - `poll_epoll.c:lx_proc_maps`, which today rejects `pid != self`

    L, but it unblocks strace, gdb, ltrace and rr-like tools.
11. **Minor:**
    - pselect6/ppoll signal mask (`lx_dispatch` cases 270/271)
    - `/proc/<pid>/{smaps_rollup,environ,io}` with real data (`poll_epoll.c:lx_proc_extra_gen`)
    - `/proc/net/{tcp,unix}` for ss/netstat (`proc_extra.c:lx_proc_sys_gen`)
    - mremap (case 25) for programs that need it

    S each.

Not needed for this list: io_uring and old AIO. libuv and the others fall back on ENOSYS, so they can stay ENOSYS.
