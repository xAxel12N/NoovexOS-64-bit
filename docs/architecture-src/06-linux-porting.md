# Part 6: Linux compatibility, libc and porting applications

NoovexOS 64 runs two kinds of user programs side by side:

| Kind | System-call gate | C library | Link type | Loaded at | Source |
|---|---|---|---|---|---|
| Native ("legacy") | `int $0x80`, number in `rax`, args in `rbx rcx rdx rsi rdi` | nvxlibc (`usr/libc`) or the header-only `usr/src/user.h` | static `ET_EXEC`, `usr/src/user.ld` | `0x40000000000` | `usr/src/*.c` |
| Linux ABI, static | `syscall` instruction, Linux x86-64 numbers and registers | musl 1.2.6 (`third_party/musl`), `libc.a` | static-PIE `ET_DYN` | `0x40000400000` (`ELF_PIE_BASE`) | `usr/musl/*.c` test programs |
| Linux ABI, dynamic (v8.97) | same | musl 1.2.6 shared, `/lib/ld-musl-x86_64.so.1` | PIE `ET_DYN` with `PT_INTERP` | program at `0x40000400000`, dynamic linker at `0x7F000000000` | `third_party/*` ports, `dyntest` |

Both run under the same job scheduler (`core/proc/jobs.c`, `struct jproc`), share the same VFS file table and the same signal machinery. This part documents the Linux layer (`sys/linux/linux.c`), the two libcs, the ported programs and how to add your own.

---

## 6.1 How a Linux system call reaches the kernel

`arch/x86_64/user/ring3.c: lsys_init()` programs the `syscall` MSRs:

```c
cpu_wrmsr(0xC0000080,cpu_rdmsr(0xC0000080)|1ULL);
cpu_wrmsr(0xC0000081,(0x08ULL<<32)|(0x10ULL<<48));
cpu_wrmsr(0xC0000082,(u64)lsys_entry);
cpu_wrmsr(0xC0000084,0x47700ULL);
```

EFER.SCE is set, STAR selects the kernel/user segments, LSTAR points at `lsys_entry`, and FMASK `0x47700` clears IF, TF, DF, AC and NT on entry.

`lsys_entry` (`arch/x86_64/boot/userspace.S`) does `swapgs`, saves the user `rsp` in `%gs:8`, loads the per-CPU kernel stack from `%gs:0`, then pushes a fake interrupt frame (`ss=0x23`, user rsp, `r11`=rflags, `cs=0x1B`, `rcx`=rip) followed by all general registers. The result is the same `struct sysregs` layout that `int 0x80` produces, so blocking, fork, signal delivery and preemption reuse one code path. It calls `lsys_c` and returns with `iretq` (not `sysretq`).

```c
void lsys_c(struct sysregs* r){ bkl_lock(); PCPU->syscalls++; linux_syscall(r); bkl_unlock(); }
```

Every Linux system call runs under the big kernel lock (`arch/x86_64/interrupts/interrupts.c`).

`linux_syscall()` (end of `sys/linux/linux.c`):

```c
void linux_syscall(struct sysregs* r){
    if(!g_js_running||g_jcur<0){
        if(r->rax==60||r->rax==231){ r->rbx=r->rdi; r->rax=0; syscall_dispatch(r); return; }
        if(r->rax==1&&(r->rdi==1||r->rdi==2)&&r->rdx<4096&&jc_umapped(r->rsi,r->rdx)){ ... g_r3out ... }
        r->rax=(u64)-LX_ENOSYS; return; }
    struct jproc* p=&g_jp[g_jcur]; p->lx=1;
    long v=lx_dispatch(r);
    r->rax=(u64)v;
    if(jc_deliverable(p))jc_yield_sys(r);
}
```

* Outside the job scheduler (the old single-run `elf_load_run` path) only `exit`/`exit_group` and `write` to fd 1/2 work.
* Inside it, the process is marked `p->lx=1`. From then on `jc_syscall()` (`core/proc/jobs.c`) answers any `int 0x80` from that process with SIGSYS (31) and `-38`: a process uses one ABI or the other, never both. A static-PIE image gets `p->lx=g_elf_pie=1` already at load time (`jc_create`, `jc_exec`).
* After the call, a pending deliverable signal makes the process yield so `jc_enter()` to `jc_deliver()` can run the handler.

### Return values and errno

`lx_dispatch()` returns a `long`. Success is a non-negative value; failure is `-errno` using Linux numbers, which musl turns into `errno`. The constants are at the top of `linux.c` (`LX_EPERM` 1 ... `LX_ENOTSUP` 95). A few call sites use raw numbers: `-23` (ENFILE, global open-file table full in `lx_open`), `-27` (EFBIG, write past `FS_CAP`), `-97` (EAFNOSUPPORT, `socket`), `-110` (ETIMEDOUT, `futex` wait with timeout).

### Blocking and restarting

Blocking calls never sleep inside the kernel. They call `jc_block(r,state,wake)` (`jobs.c`), which saves the registers into `p->ctx` and longjmps back to the scheduler:

* `S_WAIT` (pipe/tty/mouse empty, `wait4`, `poll`): `jc_block` sets `ctx.rip-=2`, so the 2-byte `syscall` instruction is executed again when the process is next picked. The handler simply re-evaluates.
* `S_SLEEP` (`nanosleep`, `pause`, `rt_sigsuspend`): the return value is pre-loaded into `rax`; `jc_enter()` replaces it by `-4` (EINTR) if the process is woken before `p->wake` because a signal arrived.

Ticks are 10 ms (`lx_sleep_ns` rounds up to `10000000` ns units), so sleep and poll timeouts have 10 ms granularity.

### User pointer checking

```c
static int lx_ok(u64 p,u64 n){ return jc_umapped(p,n); }
```

`jc_umapped()` (`jobs.c`) first requires `jc_uok()`: the range must lie in the user window `[0x40000000000, 0x80000000000)` without wrapping. It then walks the current page tables page by page; a non-present lazy page or a read-only copy-on-write page is resolved through `vm_fault_user(cr3,v,1)` (so a later kernel write cannot fault), and the final PTE must be present and user (`(*e&5)==5`). Helpers built on it:

| Helper | Purpose |
|---|---|
| `lx_str(p,out,cap)` | copy a NUL-terminated user string, re-checking at each page boundary; `-EFAULT` / `-ENAMETOOLONG` |
| `lx_put(p,src,n)` / `lx_get(dst,p,n)` | checked copy out / in |
| `lx_vf(fd)` | fd to `struct vfile*` through the per-process table `fdt()`; NULL if closed |
| `lx_fd_install(vi,cloexec,min)` | lowest free fd at least `min`, sets the per-fd close-on-exec byte in `fdce()` |

### Path resolution and dirfd

`lx_path(dirfd,up,out,empty_ok)` turns every path into an absolute one before it reaches the file system:

1. Copy the string with `lx_str` (max `LX_PATH` = 256). An empty string is `-ENOENT` unless `empty_ok`.
2. Any component longer than 63 characters to `-ENAMETOOLONG` (names in `FsFile.name[64]`).
3. Absolute paths are copied as-is. Otherwise the base node is the current directory `g_dcwd`, or, if `dirfd != AT_FDCWD (-100)`, the directory node of that fd, which must be a directory vfile (`dev == LX_DIRDEV == 11`, else `-ENOTDIR`; closed fd to `-EBADF`).
4. `dt_path(node,...)` gives the base path; the relative part is appended.

`lx_noent(p)` refines a failed lookup: it walks the prefixes and returns `-ENOTDIR` if a prefix is a regular file (e.g. `open("file.txt/x")`), else `-ENOENT`. `musltest` checks both.

## 6.2 File-related details

### `open` / `openat` (`lx_open`)

Flags decoded: access mode `flags&3`, `O_CREAT 0x40`, `O_EXCL 0x80`, `O_TRUNC 0x200`, `O_APPEND 0x400`, `O_NONBLOCK 0x800` (kept in `vfile.flags`), `O_DIRECTORY 0x10000`, `O_CLOEXEC 0x80000`. Device paths (`dev_id()` in `fs/vfs/ramfs.c`) and `/proc` (`vfs_lookup`) bypass the file checks. A directory opened read-only becomes a `LX_DIRDEV` vfile whose `fs_idx` is the directory node. Permission checks use `ux_perm_ok(k,2)` for write/truncate and `ux_perm_ok(k,4)` for read. New files get `0100000|(mode&07777&~umask)`. `O_TRUNC` on disk triggers `fs_sync`.

Limits from `fs/vfs/ramfs.c` and `jobs.h`: `FDMAX 256` fds per process, `VMAXOPEN 1024` vfile slots for the whole system (the console shares one slot), and `PIPE_MAX 128` pipes system-wide, each with a 4096-byte buffer (`jc_pipe_new(4096,...)`). Before v8.97 these were 32, 32 and 12.

### Read and write

`lx_read`/`lx_write` cap one transfer at `LX_IOMAX` (1 MiB), check the buffer with `lx_ok`, then call `vfs_read`/`vfs_write`. A `JS_BLOCK` result (`-1000`) means "would block": `-EAGAIN` for `O_NONBLOCK`, otherwise `jc_block(S_WAIT)`. `O_APPEND` moves `v->off` to the file end before each write. Writes that start at or past `FS_CAP-1` (`FS_CAP 0x40000000`, 1 GiB) fail with EFBIG. Successful writes only mark the file dirty (`lx_persist` to `g_fs_dirty`); it is written back when the last descriptor closes (`vf_unref` to `fs_wb_mark`), on `fsync`/`fdatasync`, or on `sync` (162 to `nvx3_sync_all`).

`readv`/`writev` (`lx_rwv`, up to 1024 iovecs) stop early instead of blocking once some data has moved. `pread64`/`pwrite64` (`lx_prw`) only work on regular files (`-ESPIPE` otherwise) and temporarily swap `v->off`. `sendfile` (`lx_sendfile`) needs a regular-file source and copies through a 64 KiB static buffer when source and destination are the same file.

### `struct stat` (`lx_stat_out`)

The kernel fills the 144-byte Linux x86-64 `struct stat` from the 10-word array `ux_stat2()` returns (`o[0]` mode, `o[1]` uid, `o[2]` gid, `o[3]` nlink, `o[4]` size, `o[5]` inode, `o[6]` mtime, `o[7]` atime, `o[8]` ctime, `o[9]` is-directory):

| Offset | Field | Value |
|---|---|---|
| 0 | `st_dev` | `0x4e5658` ("NVX") |
| 8 | `st_ino` | `o[5]` (+ `dev_ino`) |
| 16 | `st_nlink` | `o[3]` |
| 24 / 28 / 32 | `st_mode` / `st_uid` / `st_gid` | `o[0]` / `o[1]` / `o[2]` |
| 40 | `st_rdev` | char devices only: `(1<<8) \| (ino & 0xFF)` |
| 48 | `st_size` | `o[4]` |
| 56 | `st_blksize` | 4096 |
| 64 | `st_blocks` | `(size+511)/512` |
| 72 / 88 / 104 | `st_atime` / `st_mtime` / `st_ctime` | seconds only, nanoseconds are 0 |

`lx_fstat` synthesises values for non-file descriptors: pipes are FIFOs `0010600` with inode `5000+pipe`, `/proc` files `0100444` with their generated size, other devices `0020666` (tty `0020620`) with inode `20000+dev`, regular files use inode `fs_idx+1` and `nlink 0` once unlinked (orphan).

### `getdents64` (`lx_getdents`)

Only `LX_DIRDEV` fds (`-ENOTDIR` otherwise). Each record is a `linux_dirent64`: `d_ino` (u64), `d_off` (= index+1), `d_reclen` = `(19+len+1+7)&~7`, `d_type`, `d_name`. Entries 0 and 1 are synthesised `.` and `..` (`d_type 4`, DT_DIR); the rest come from `k_readdir_at`. The inode is taken from `ux_stat2` so it matches `stat()`. `v->off` is the entry index, which is why `lseek(dirfd, n, SEEK_SET)` is accepted on directories (rewinddir/seekdir). The old `getdents` (78) returns ENOSYS.

### `ioctl` (`lx_ioctl`)

| Request | Name | Devices | Behaviour |
|---|---|---|---|
| `0x5451` / `0x5450` | FIOCLEX / FIONCLEX | any | set/clear close-on-exec |
| `0x5421` | FIONBIO | any | toggle `0x800` in `vfile.flags` |
| `0x541B` | FIONREAD | any | bytes in pipe / tty line buffer / file remainder; on a socket, bytes buffered (TCP) or the size of the next datagram (UDP) |
| `0x5401` | TCGETS | tty (dev 5) | the console's 36-byte termios `g_tios` (`shell/vt.c`). Default: iflag `0x500` (ICRNL, IXON), oflag `5` (OPOST, ONLCR), cflag `0xBF`, lflag `0x8A3B` (ISIG, ICANON, ECHO, ECHOE, ECHOK, ECHOCTL, ECHOKE, IEXTEN), standard `c_cc` |
| `0x5402-0x5404` | TCSETS/TCSETSW/TCSETSF | tty | store the new termios; TCSETSF also discards typed input. Section 11.12 lists what each flag does |
| `0x540B` | TCFLSH | tty | queue 0 or 2 discards typed input |
| `0x5413` / `0x5414` | TIOCGWINSZ / TIOCSWINSZ | tty | rows `trows` (default 25), cols `tcols` (default 80) / no-op |
| `0x540F` / `0x5410` | TIOCGPGRP / TIOCSPGRP | tty | foreground group `g_tty_fg` (set needs session 1) |
| `0x5429` | TIOCGSID | tty | returns 1 |
| `0x540E` | TIOCSCTTY | tty | no-op |
| `0x5000` / `0x5001` | SNDCTL_DSP_RESET / SYNC | `/dev/dsp` (dev 12) | no-op |
| `0xC0045002` | SNDCTL_DSP_SPEED | dsp | returns the real rate (`g_snd.rate`, default 48000); the request is not applied |
| `0xC0045005` / `0x8004500B` | SNDCTL_DSP_SETFMT / GETFMTS | dsp | `0x10` (AFMT_S16_LE) |
| `0xC0045006` / `0xC0045003` | SNDCTL_DSP_CHANNELS / STEREO | dsp | 2 / 1 |
| `0x8010500C` | SNDCTL_DSP_GETOSPACE | dsp | `{free/4096, 32, 4096, free}` from `g_snd.free_bytes()` |
| `0xC004500A` | SNDCTL_DSP_SETFRAGMENT | dsp | no-op |

Any other request on the tty or dsp is `-EINVAL`; on any other device it is `-ENOTTY` (this is what makes `isatty()` false for files).

### `fcntl` (`lx_fcntl`)

`F_DUPFD`(0) and `F_DUPFD_CLOEXEC`(1030), `F_GETFD`/`F_SETFD` (1/2, the `fdce()` byte), `F_GETFL`(3), `F_SETFL`(4, only `O_APPEND|O_NONBLOCK` change), `F_GETLK`(5, always reports `F_UNLCK`), `F_SETLK`/`F_SETLKW` (6/7, succeed without locking), 1025 and 1031 return 0, `F_GETPIPE_SZ`(1032) returns 4096. Anything else is `-EINVAL`.

## 6.3 poll/select, time, memory, processes, signals

poll / ppoll / select / pselect6. `lx_poll_one(fd,ev)` computes readiness per device: pipes (`dev 6` read end: POLLIN when data, POLLHUP when no writers; `dev 7` write end: POLLOUT if space, POLLERR without readers), tty (`dev 5`: always POLLOUT, POLLIN when input is buffered and the caller is in the foreground group), mouse (`dev 10`: POLLIN when packets are queued), everything else is always readable and writable; a closed fd gives POLLNVAL (`0x20`). With nothing ready, `lx_wait_deadline` stores a wake tick in `p->pollwake` and the call blocks with `S_WAIT`; a deliverable signal returns `-EINTR`. `select` reads up to 1024 bits per set (`lx_select` copies each set into a 128-byte array, so descriptors up to `FDMAX-1` work; before v8.97 only the first 64 were looked at) and never reports exceptional conditions. Negative timeouts are `-EINVAL`; timeouts above 10^8 s are clamped.

Time. `lx_now_ns()` anchors `hr_ns()` to `k_realtime_ms()` once. `clock_gettime` (`lx_clock`): clocks 0, 5, 8, 11 (REALTIME family) return wall time; 1, 2, 3, 4, 6, 7, 9 return `hr_ns()`; others `-EINVAL`. `clock_getres` reports 1 ns if `hr_precise()` else 10 ms. `gettimeofday` (96) and `time` (201) use the same clock; the timezone is zero. `clock_nanosleep` supports `TIMER_ABSTIME` (flag 1). `getrusage` (98) reports the tick count as user time; `times` (100) returns `g_ticks`.

Address space. The user window and the regions `linux.c` and `elf.c` assume:

| Range | Use |
|---|---|
| `0x40000000000` ... | `ET_EXEC` images (nvxlibc, link address in `user.ld`) |
| below `0x40000101000` | user stack, up to `JS_STK_PAGES` = 256 pages (1 MiB), demand-mapped (`USTK_V`; the first 16 pages are always reserved, `ELF_STK_LO`) |
| `0x40000400000`, `0x40002000000` | PIE images, static or dynamic (`ELF_PIE_BASE` ... `ELF_IMG_TOP`) |
| `0x40002000000`, `0x40004000000` | `brk` heap (`lx_brk`) |
| `0x40004000000`, `0x80000000000` | `mmap` area (`g_mmap_next`, `lx_find_gap`); shared libraries loaded by the dynamic linker and thread stacks go here |
| `0x7F000000000` | the dynamic linker of a dynamic program (`ELF_INTERP_BASE`) |

`lx_mmap` rejects `len==0`, unaligned offsets and lengths over 1 GiB. Anonymous mappings (`MAP_ANONYMOUS 0x20`) are lazy (`vm_map_lazy`), so an 8 MiB mapping only consumes the pages that are touched. File mappings must be on a regular file (`-ENODEV` otherwise, e.g. `/dev/fb0`) and are private copies filled at map time; writes never reach the file. `prot` is ignored (all pages user-RW). `MAP_FIXED` (0x10) unmaps the target first and refuses the stack region. Without it, `lx_find_gap` reuses freed holes before extending `g_mmap_next`. `mprotect`, `msync`, `madvise`, `mlock*` return 0; `mremap` returns `-ENOMEM` (musl's `realloc` then copies); `mincore` is ENOSYS. All user pages come from one frame pool shared by every process: 32 MiB at `PF_BASE` plus a range taken from the heap that grows with RAM, up to 2 GiB more (section 3.7); `pframe_free_count()` gates `mmap`/`brk`.

Processes.

```c
static long lx_clone(struct sysregs* r,u64 flags,u64 stack,u64 ptid,u64 ctid,u64 tls){
    if((flags&0x80000ULL)&&tls>=0x800000000000ULL)return -LX_EPERM;
    if(stack&&!jc_uok(stack-8,8))return -LX_EINVAL;
    if(flags&0x10000ULL){                                   /* CLONE_THREAD */
        if((flags&0x900ULL)!=0x900ULL)return -LX_EINVAL;    /* needs CLONE_VM and CLONE_SIGHAND */
        int ci=jc_clone_thread(r,stack,tls,(flags&0x80000ULL)!=0); ...
        if(flags&0x200000ULL)c->ctid=ctid;                  /* CLONE_CHILD_CLEARTID */
        ... CLONE_CHILD_SETTID, CLONE_PARENT_SETTID ...
        return tid; }
    int pid=jc_fork(r); ...                                  /* everything else is a fork */
```

With `CLONE_THREAD`, `clone` creates a thread that shares the address space, descriptors and signal handlers (section 5.15). Without it, `clone` forks: `CLONE_VM` and `CLONE_VFORK` are accepted but the child gets a copy-on-write copy from `vm_clone_user`; a child stack, `CLONE_SETTLS`, `CLONE_CHILD_CLEARTID` and `CLONE_PARENT_SETTID` are honoured. `fork` (57) and `vfork` (58) are `clone(0)`. `execve` (`lx_execve`) copies up to `XA_MAX` = 32 argv and 32 envp strings into 2048-byte buffers (`XA_CAP`), rejects names over 63 characters or containing spaces, resolves the program with `jc_find_prog` (6.6) and calls `jc_exec`, which frees the old address space, closes close-on-exec fds, resets caught signal handlers, `fsbase`, `brk` and the mmap cursor. `wait4` supports `WNOHANG|WUNTRACED|WCONTINUED` (`opts&0xB`) and zero-fills `rusage`; with no matching child it is `-ECHILD`. `exit`/`exit_group` store `(code&0xFF)<<8`.

Signals. The kernel keeps signals 1-31 in 32-bit masks where bit *s* is signal *s*; Linux uses bit *s-1*. `lx_k2l` (`k>>1`) and `lx_l2k` (`l<<1`) convert. `rt_sigaction` reads/writes the 32-byte kernel `sigaction` (`handler, flags, restorer, mask`), keeps `SA_RESTORER` (`0x04000000`) restorer addresses, refuses SIGKILL/SIGSTOP, and silently accepts real-time signals 32-64 without effect (`kill` with signal at least 32 also returns 0). `jc_push_handler` builds the frame: a copy of `regs_pt` plus the saved mask, then the restorer address as return address; with `SA_SIGINFO` (flag 4) a 128-byte `siginfo` containing only `si_signo` is passed in `rsi`. `rt_sigreturn` (`lx_sigreturn`) reads that frame from the user `rsp`, forces user segments and sanitises `rflags`, and kills the process with SIGSEGV if the frame or the return `rip` is outside the user window. `rt_sigsuspend` (130) swaps the mask and sleeps; `sigaltstack` reports `SS_DISABLE`. `alarm`, `setitimer` and `getitimer` work for `ITIMER_REAL` (section 5.16). `tgkill` and `tkill` send a signal to one thread; `kill` sends it to the process, and the kernel picks a thread that does not block it (section 5.15). `futex` is described in section 5.15.

Identity and system info. `uname` (`lx_uname`) returns `NoovexOS`, the hostname, release `8.97`, version `NoovexOS 64 v8.97`, machine `x86_64`, domain `(none)`. `sched_getaffinity` (204) returns an 8-byte mask with one bit per online core (`g_cores_online` when `g_smp_on`), which is what musl's `sysconf(_SC_NPROCESSORS_ONLN)` counts; `sched_setaffinity` is a no-op. `getcpu` (309) returns `cpu_id()` and node 0. `getrlimit`/`prlimit64` report `RLIMIT_STACK` = 1 MiB and `RLIMIT_NOFILE` = 256, everything else unlimited. `arch_prctl` supports `ARCH_SET_FS` (0x1002, also loads the MSR through `cpu_set_fsbase` and `lsys_fsbase`) and `ARCH_GET_FS` (0x1003), this is musl's TLS. `getrandom`, `/dev/random` and `/dev/urandom` return output of the kernel's ChaCha20 generator `csprng_bytes` (`lib/crypto/nvxcrypto2.c`), seeded from RDSEED or RDRAND when the CPU has them and from TSC timing jitter. Before v8.97 they were an xorshift or LCG sequence, which is not good enough for TLS keys; the seeding code also used RDSEED without checking CPUID, which faulted on CPUs without it.

## 6.4 Complete Linux system-call table

Every `case` in `lx_dispatch()`; 190 numbers are handled, anything else returns `-ENOSYS`.

| Nr | Name | Notes |
|---|---|---|
| 0 | read | `lx_read`, 1 MiB cap, blocks on pipes/tty/mouse |
| 1 | write | `lx_write`, EPIPE/ENOSPC/EACCES/EIO mapping |
| 2 | open | `lx_open(AT_FDCWD,...)` |
| 3 | close | `lx_close` |
| 4 | stat | follows symlinks |
| 5 | fstat | `lx_fstat` |
| 6 | lstat | no follow |
| 7 | poll | `lx_poll`, ms timeout |
| 8 | lseek | `-ESPIPE` on tty/pipes; dirs accept SEEK_SET only; `/dev/fb0` size is `W*H*4` |
| 9 | mmap | `lx_mmap` |
| 10 | mprotect | returns 0, no effect |
| 11 | munmap | `lx_munmap`, refuses the stack |
| 12 | brk | `lx_brk`, heap window 32 MiB |
| 13 | rt_sigaction | `lx_sigaction` |
| 14 | rt_sigprocmask | `lx_sigprocmask`, sigsetsize must be 8 |
| 15 | rt_sigreturn | `lx_sigreturn` |
| 16 | ioctl | `lx_ioctl` (table in 6.2) |
| 17 / 18 | pread64 / pwrite64 | `lx_prw`, regular files only |
| 19 / 20 | readv / writev | `lx_rwv`, at most 1024 iovecs |
| 21 | access | `lx_access` |
| 22 | pipe | `lx_pipe(...,0)` |
| 23 | select | timeval to ms |
| 24 | sched_yield | `jc_yield_sys` |
| 25 | mremap | `-ENOMEM` |
| 26 | msync | 0 |
| 27 | mincore | ENOSYS |
| 28 | madvise | 0 |
| 32 | dup | lowest free fd |
| 33 | dup2 | `lx_dup3(...,-1)` |
| 34 | pause | sleeps until a signal, returns EINTR |
| 35 | nanosleep | `lx_nanosleep` |
| 36 / 38 | getitimer / setitimer | `ITIMER_REAL` (section 5.16); the other two are accepted and never fire |
| 37 | alarm | one-shot `ITIMER_REAL`, returns the seconds left on the old timer |
| 39 | getpid | |
| 40 | sendfile | `lx_sendfile` |
| 41 | socket | `lx_socket`: `AF_INET` TCP/UDP to fd on device 14; other families `-97` EAFNOSUPPORT |
| 42 | connect | `lx_connect` to `lxs_connect` (blocking, or EINPROGRESS) |
| 43 | accept | `lx_accept` (section 9.7.4) |
| 44 / 45 | sendto / recvfrom | `lx_sendto` / `lx_recvfrom`; `MSG_PEEK`, `MSG_DONTWAIT`, `MSG_NOSIGNAL` |
| 46 / 47 | sendmsg / recvmsg | `lx_msg`, at most 1024 iovecs, 64 KiB per call, no control data |
| 48 | shutdown | `lxs_shutdown` |
| 49 | bind | `lxs_bind` (port only) |
| 50 | listen | `lxs_listen`, backlog capped at 32 |
| 51 / 52 | getsockname / getpeername | `lx_sockname` |
| 53 | socketpair | EOPNOTSUPP |
| 54 / 55 | setsockopt / getsockopt | set ignored; get `SO_ERROR`, `SO_TYPE`, buffer sizes |
| 56 | clone | `lx_clone`: threads with `CLONE_THREAD`, otherwise fork |
| 57 / 58 | fork / vfork | `lx_clone(r,0,...)` |
| 59 | execve | `lx_execve` |
| 60 | exit | ends the calling thread (`jc_thread_exit`); the last thread ends the process with status `(a1&0xFF)<<8` |
| 61 | wait4 | `lx_wait4` |
| 62 | kill | sig 0 probes; uid check via `g_kill_check` |
| 63 | uname | `lx_uname` |
| 72 | fcntl | `lx_fcntl` |
| 73 | flock | 0 for a valid fd, no locking |
| 74 / 75 | fsync / fdatasync | `k_fsync` |
| 76 / 77 | truncate / ftruncate | `k_truncate` / `k_ftruncate` (needs a writable fd) |
| 78 | getdents | ENOSYS (use 217) |
| 79 | getcwd | `-ERANGE` if too small |
| 80 / 81 | chdir / fchdir | sets `g_dcwd` |
| 82 | rename | `lx_rename` (directory-over-empty-directory supported) |
| 83 / 84 | mkdir / rmdir | `lx_mkdir` / `lx_rmdir` |
| 85 | creat | `open(O_CREAT\|O_WRONLY\|O_TRUNC)` |
| 86 | link | `ux_link`, new name at most 51 chars |
| 87 | unlink | `lx_unlink`, EISDIR for directories |
| 88 / 89 | symlink / readlink | `ux_symlink` / `ux_readlink` |
| 90 / 91 | chmod / fchmod | `ux_chmod` / `lx_fchmod` |
| 92 / 94 | chown / lchown | `ux_chown` |
| 93 | fchown | validates fd only |
| 95 | umask | |
| 96 | gettimeofday | tz zero |
| 97 | getrlimit | `lx_rlimit` |
| 98 | getrusage | ticks as user time |
| 99 | sysinfo | uptime, RAM, free frames, process count |
| 100 | times | returns `g_ticks` |
| 102 / 107 | getuid / geteuid | `g_uid` |
| 104 / 108 | getgid / getegid | `g_gid` |
| 105 / 106 | setuid / setgid | only root may change to another id |
| 109 | setpgid | same session, self or child |
| 110 / 111 | getppid / getpgrp | |
| 112 | setsid | EPERM for a group leader |
| 113 / 117 | setreuid / setresuid | uses the effective id argument |
| 114 / 119 | setregid / setresgid | uses the effective id argument |
| 115 | getgroups | 0 groups |
| 116 | setgroups | root only, no effect |
| 118 / 120 | getresuid / getresgid | all three = current id |
| 121 / 124 | getpgid / getsid | |
| 127 | rt_sigpending | pending & blocked |
| 130 | rt_sigsuspend | |
| 131 | sigaltstack | reports SS_DISABLE |
| 132 | utime | times argument ignored |
| 133 | mknod | EPERM |
| 135 | personality | 0 |
| 137 / 138 | statfs / fstatfs | `ux_statfs("/")` |
| 140 / 141 | getpriority / setpriority | 20 / 0 |
| 142-147 | sched_setparam ... sched_get_priority_min | 0 (143 writes priority 0) |
| 148 | sched_rr_get_interval | ENOSYS |
| 149-152 | mlock, munlock, mlockall, munlockall | 0 |
| 157 | prctl | PR_SET_NAME (15) / PR_GET_NAME (16) on `p->cmd`, else EINVAL |
| 158 | arch_prctl | ARCH_SET_FS / ARCH_GET_FS |
| 160 | setrlimit | 0, ignored |
| 161 | chroot | EPERM |
| 162 | sync | `nvx3_sync_all` |
| 165 / 166 | mount / umount2 | `ux_mount` / `ux_umount` (USB FAT at `/mnt/usb`, root) |
| 169 | reboot | EPERM |
| 170 | sethostname | root, at most 31 chars |
| 186 | gettid | the thread ID |
| 191-199 | getxattr ... fremovexattr | ENOTSUP (188-190 setxattr family: ENOSYS) |
| 200 / 234 | tkill / tgkill | signal one thread (`jc_signal_thread`) |
| 201 | time | |
| 202 | futex | `lx_futex`: WAIT, WAKE, REQUEUE, CMP_REQUEUE, WAKE_OP, WAIT_BITSET, WAKE_BITSET (section 5.15) |
| 203 / 204 | sched_setaffinity / sched_getaffinity | no-op / online-core mask |
| 217 | getdents64 | `lx_getdents` |
| 218 | set_tid_address | stores the clear-child-tid address, returns the thread ID |
| 221 | fadvise64 | 0 |
| 228 / 229 / 230 | clock_gettime / clock_getres / clock_nanosleep | |
| 231 | exit_group | `jc_finish`: ends every thread |
| 257 | openat | |
| 258 | mkdirat | |
| 260 | fchownat | `AT_SYMLINK_NOFOLLOW` (0x100) honoured |
| 262 | newfstatat | `AT_EMPTY_PATH` (0x1000) to fstat |
| 263 | unlinkat | `AT_REMOVEDIR` (0x200) |
| 264 / 316 | renameat / renameat2 | renameat2 flags must be 0 |
| 265 / 266 / 267 | linkat / symlinkat / readlinkat | |
| 268 / 269 / 439 | fchmodat / faccessat / faccessat2 | |
| 270 / 271 | pselect6 / ppoll | signal mask argument ignored |
| 273 / 274 | set_robust_list / get_robust_list | stored and returned, not walked |
| 280 | utimensat | UTIME_NOW/UTIME_OMIT handled; NULL path to fd check |
| 285 | fallocate | ENOTSUP |
| 288 | accept4 | `lx_accept` with `SOCK_NONBLOCK` and `SOCK_CLOEXEC` |
| 292 / 293 | dup3 / pipe2 | O_CLOEXEC, O_NONBLOCK |
| 302 | prlimit64 | own pid only, get only |
| 309 | getcpu | |
| 318 | getrandom | kernel ChaCha20 generator, at most 1 MiB |
| 334 | rseq | ENOSYS (musl falls back) |
| 436 | close_range | `CLOSE_RANGE_CLOEXEC` (4) supported |

Not present (ENOSYS): `epoll_*`, `eventfd`, `timerfd`, `signalfd`, `inotify`, `memfd_create`, `statx`, `getdents`, process-shared futexes across different processes, `ITIMER_VIRTUAL`/`ITIMER_PROF` expiry.

## 6.5 The C libraries

### musl 1.2.6 (Linux ABI)

`third_party/musl` is upstream musl (`VERSION` = 1.2.6). `build.sh` builds it once into `build/musl` and caches it (`lib/libc.a` and `lib/rcrt1.o` must both exist to skip):

```sh
( cd $B/musl-obj && CC=gcc ../../third_party/musl/configure --prefix=$M --disable-shared --disable-wrapper CFLAGS="-fPIE -O2" ... && make ... && make install ... )
```

The in-tree programs (`usr/musl/mhello.c`, `musltest.c`, `smptest.c`) are compiled and linked explicitly:

```sh
MCFLAGS="-fPIE -O2 -nostdinc -isystem $M/include -isystem $(gcc -print-file-name=include) -Wall ..."
gcc -static-pie -nostdlib -Wl,--no-dynamic-linker -Wl,-z,noexecstack -o $B/user/$p.elf \
    $M/lib/rcrt1.o $M/lib/crti.o $B/user/musl_$p.o $M/lib/libc.a $(gcc -print-libgcc-file-name) $M/lib/crtn.o
```

`rcrt1.o` is musl's self-relocating start file for static-PIE: the loader only maps the image at `ELF_PIE_BASE` and musl applies its own relative relocations. Nothing in the kernel resolves symbols for user programs (the `nvx_dlload` relocation loader in `elf.c` is for kernel modules).

`tools/mcc` wraps the same recipe as a drop-in `cc`:

```sh
M=${NVX_MUSL:?NVX_MUSL not set}
GI=$(gcc -print-file-name=include)
for a in "$@"; do case "$a" in -c|-S|-E|-M|-MM) exec gcc -fPIE -nostdinc -isystem "$M/include" -isystem "$GI" "$@";; esac; done
exec gcc -fPIE -nostdinc -isystem "$M/include" -isystem "$GI" -static-pie -nostdlib -Wl,--no-dynamic-linker -Wl,-z,noexecstack -L"$M/lib" "$M/lib/rcrt1.o" "$M/lib/crti.o" "$@" "$M/lib/libc.a" "$(gcc -print-libgcc-file-name)" "$M/lib/crtn.o"
```

Compile-only invocations get just the include paths; link invocations get the full static-PIE recipe. `NVX_MUSL` must point at the installed musl (`build.sh` exports it).

Since v8.97 musl is also built a second time as a shared library (step `[musl-shared]`, cached in `build/musl-sh`): `configure --enable-shared --disable-static --syslibdir=/lib`. Its `lib/libc.so` is both the C library and the dynamic linker `/lib/ld-musl-x86_64.so.1`. `tools/mcc-dyn` is the matching compiler wrapper for dynamic programs:

```sh
exec gcc -fPIE -nostdinc -isystem "$M/include" -isystem "$GI" -pie -nostdlib \
    -Wl,-dynamic-linker,/lib/ld-musl-x86_64.so.1 -Wl,-z,noexecstack -L"$MS/lib" \
    "$MS/lib/Scrt1.o" "$MS/lib/crti.o" "$@" -lc "$(gcc -print-libgcc-file-name)" "$MS/lib/crtn.o"
```

It uses the same headers as the static build and needs `NVX_MUSL_SH` (exported by `build.sh`) as well. The ports are linked with it; the test programs `musltest`, `smptest`, `nettest` and `featest` stay static so both paths are exercised. Section 6.10 explains how a dynamic program starts.

Start-up stack. `elf_build_space()` (`core/proc/elf.c`) builds a Linux start stack in an 8 KiB image (`g_stkimg`) copied into the top two stack pages: `argc`, `argv[]`, NULL, `envp[]`, NULL, then the auxiliary vector `AT_PHDR(3) AT_PHENT(4) AT_PHNUM(5) AT_PAGESZ(6)=4096 AT_BASE(7) (0, or the dynamic linker's base) AT_FLAGS(8)=0 AT_ENTRY(9) AT_UID/EUID/GID/EGID(11-14) AT_HWCAP(16)=0 AT_CLKTCK(17)=100 AT_SECURE(23)=0 AT_RANDOM(25) AT_EXECFN(31)`, with 16 random bytes for `AT_RANDOM`. The same registers also carry `argc`/`argv` in `rdi`/`rsi` (`jc_ctx_fresh`) for nvxlibc's `crt0`. When started from the shell the environment comes from `env_pack()` (`sys/native/syscall.c`, 24 entries, defaults `PATH=/bin HOME=/home PWD=/home USER=root SHELL=/bin/nsh TERM=xterm-256color XZ_DEFAULTS=--memlimit-compress=20MiB`; `TERM` was `noovex` before the terminal understood escape sequences); after `execve` it is the caller's `envp`.

### nvxlibc (native `int 0x80`)

`usr/libc` is the native C library. Its single source of truth is `usr/libc/src/legacy_posix.h` (the former header-only `posix.h`). `usr/libc/gen.py` generates from it:

* `src/libc.c`, the implementation with top-level `static` removed and `__sys` turned into an extern;
* `include/nvx/_all.h`, `_types.h`, `_std.h` (351 standard prototypes) and `extras.h` (457 NoovexOS helpers such as `append_file`, `base64_encode`, `arr_*`), plus the standard header names (`stdio.h`, `unistd.h`, `dirent.h`, `pthread.h`, ... each just includes `<nvx/_std.h>`).

Hand-written parts: `printf.c`, `stdlib2.c`, `stdio2.c`; `sys_nvx.c` holds the gate:

```c
long __sys(long n,long a,long b,long c,long d,long e){
    long r; register long r8 __asm__("rsi")=d; register long r9 __asm__("rdi")=e;
    __asm__ volatile("int $0x80":"=a"(r):"a"(n),"b"(a),"c"(b),"d"(c),"r"(r8),"r"(r9):"memory");
    return r;
}
```

`crt0.S` aligns the stack and does `exit(main(argc,argv))` using the `rdi`/`rsi` the kernel set. `sys_host.c` + `crt0_host.S` let the same libc run on Linux for differential testing. Notable semantics in `legacy_posix.h`: `execve` ignores `envp`; `pthread_create` maps to the legacy `thread_create` syscall 61 and `pthread_join` is a no-op; `dlopen` always fails with "dynamic loading not supported (static linking only)"; `isatty` is native syscall 172.

`build.sh` builds it with `LFLAGS` (`-ffreestanding -nostdinc -isystem usr/libc/include -fno-pie -mcmodel=large -Os -ffunction-sections -fdata-sections`), archives `libc.a`, and links each program with `ld -nostdlib -no-pie --gc-sections -T usr/src/user.ld crt0.o prog.o [extra] libc.a`.

### `usr/src` programs

* `user.h` programs (`user_demo`, `user_fork`, `user_app`, `user_thread`, `user_preempt`, `user_ipc`, `user_wmapp`, `user_compdemo`, `user_posix`, `user_mem`, plus `hello.c`, `proc.c`): built with `user_crt0.S` and `UFLAGS`, embedded as `user_*_elf.o`, and started by shell demo commands through `elf_load_run`/`proc_create` (e.g. `shell/commands.c:758`), not through `JS_BIN`.
* `posix.h` programs (now `posix.h` just includes `<nvx/extras.h>` and `<setjmp.h>`): `count spin sigdemo upper greet pipetest nvxbox init libc8` are built against nvxlibc and listed in `JS_BIN`. Other `posix.h` files in the directory (`captest`, `devtest`, `libc2`-`libc6`, `libctest`, `mathtest`, `nettest`, `ptest`, `unixtest`, `files`) are not built by `build.sh`; prebuilt ELFs of several exist in `usr/bin/`.

### nvxbox

`usr/src/nvxbox.c` + `coreutils.c` form one multi-call binary (65 KB). `main` dispatches on `basename(argv[0])`: first `coreutils_main()` (head, tail, wc, sort, grep, cut, tr, uniq, seq, basename, dirname, rev, tac, nl, fold, paste, comm, cmp, md5sum, sha256sum, base64, find, yes, printenv/env; returns `-1000` if not its command), then the built-ins in `nvxbox.c` (ls, cat, echo, mkdir, rmdir, rm, mv, cp, touch, ln, chmod (octal only), chown, stat, pwd, hostname, whoami, id, uname, date, sleep, mount, umount, readlink, tee, true, false). `nvxbox CMD args` re-dispatches on `argv[1]`. Every name is a `NVXB("...")` row in `JS_BIN` pointing at the same blob.

## 6.6 How programs are embedded and found

### Embedding

```sh
emb(){ ( cd $B && ld -r -b binary -o $1 user/$2.elf ); }
...
emb user_${p}_bin.o $p
objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents $B/user_${p}_bin.o
```

`ld -r -b binary` wraps the file in an object whose symbols are derived from the path: `usr/sqlite3.elf` to `_binary_user_sqlite3_elf_start` / `_end`. The rename moves the blob from `.data` to `.rodata`, which `arch/x86_64/kernel64.ld` places before `__immutable_end`, i.e. inside the region whose SHA-256 is stamped into the `NVX64SIG` build signature. Every object must also be listed in `BLOBS` in step `[4/6]`.

The musl programs, the ports and the shared C library are embedded compressed (v8.97). `embz` runs `tools/nvz.py`, which writes `NVZ1`, the 32-bit little-endian original size and a raw DEFLATE stream (zlib level 9), and wraps that under the same symbol names:

```sh
embz(){ mkdir -p $B/zz/user; python3 tools/nvz.py $B/user/$2.elf $B/zz/user/$2.elf; ( cd $B/zz && ld -r -b binary -o ../$1 user/$2.elf ); }
```

`jc_unz` (`core/proc/jobs.c`) inflates such a blob with the kernel's `pz_inflate_raw` the first time it is needed and keeps the result in a 24-entry cache, so later runs cost nothing extra. This took the kernel from 5.46 MB to 3.86 MB before the shared libc, BearSSL client and the other v8.97 programs were added; the current `build/kernel64.bin` is about 4.28 MB. The UEFI loader's limit is `0x5C0000` bytes (section 3.13).

`core/proc/jobs.c` declares each pair `extern const unsigned char _binary_user_X_elf_start[], _binary_user_X_elf_end[];` and lists it in `JS_BIN[]` (name, start, end). Several names may share one blob (`gzip/gunzip/zcat`, `xz/unxz/xzcat/lzma/unlzma/lzcat`, ...); the program sees the name in `argv[0]`. `+init` is the init program. `fs/vfs/unixfs.c:351` lists every non-`+` `JS_BIN` name as a regular file in `/bin`.

### Lookup (`jc_find_prog`)

1. Strip a leading `/bin/`, `/usr/bin/` or `/sbin/` (`/sbin/init` to `+init`), and a trailing `.elf`; compare case-insensitively against `JS_BIN`.
   A compressed blob is inflated through `jc_unz` before it is returned.
2. Otherwise `ux_lookup(name,...)` in the file system: a file of at least 64 bytes starting with `\x7fELF` is executed directly from its in-memory data (`fsd(f)`); non-root users need an execute bit (`FS[f].mode&0111`).
3. Otherwise `fat_read_file(name, g_spawnbuf, 65536)` from the IDE FAT disk (8.3 name, at most 64 KiB).

`jc_build()` then calls `elf_build_space()`: ELF64, `EM_X86_64`, `ET_EXEC` or `ET_DYN`, at most 64 program headers, every `PT_LOAD` inside the file and inside `[0x40000000000, 0x40002000000)`, not overlapping the stack, entry inside an executable segment. Pages are copied (not mapped from the file), and up to 256 stack pages are reserved lazily. A `PT_INTERP` header loads the dynamic linker as well (section 6.10).

### Launch from the shell

`run_cmd()` first tries `jc_launch(line)` (`shell/commands.c:180`). `jc_launch` splits up to 6 `|` stages (quotes respected), finds stages whose first word `jc_is_prog`, handles `<`, `>`, `>>`, `2>`, `2>>`, `2>&1` and a trailing `&`, creates each process stopped with `jc_create`, wires pipes, then starts the group as one job (`jc_job_new`). Spaces inside quoted arguments are encoded as `0x1F` and turned back into spaces by `elf_args_from_string`. Built-ins may stand before the programs (their output is captured and fed in through a pipe) or after them (program output is collected into a 64 KiB buffer, `COLL_CAP`, and handed to the built-in), but a program may not follow a built-in that itself reads from a program.

## 6.7 The ported programs

Since v8.97 the ports are dynamic programs linked with `tools/mcc-dyn` against the shared musl, and the ones that can use threads are built with them. `port()` builds once and caches in `build/ports-dyn`:

```sh
port(){ n=$1; shift; if [ ! -f $P/$n ]; then "$@" || { echo "PORT BUILD FAILED: $n"; exit 1; }; strip $P/$n; fi; }
```

| Command(s) | Source | Build (all via `$MCC -O2 -w`) | Notes |
|---|---|---|---|
| `sqlite3` | SQLite 3.53.4 amalgamation | `-DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0`, `shell.c sqlite3.c` | thread-safe build, no extensions |
| `gzip gunzip zcat` | zlib 1.3.2 + `test/minigzip.c` | all zlib `.c` files | |
| `bzip2 bunzip2 bzcat` | bzip2 1.0.8 | `-D_FILE_OFFSET_BITS=64` | |
| `xz unxz xzcat lzma unlzma lzcat` | XZ Utils 5.8.4 | `-std=gnu11 -include third_party/xz/nvx_config.h`, explicit file list | `nvx_config.h` replaces `configure`: `ASSUME_RAM 128`, `TUKLIB_CPUCORES_SCHED_GETAFFINITY`, `TUKLIB_PHYSMEM_SYSCONF`, `MYTHREAD_POSIX` (with `stream_encoder_mt.c`, `stream_decoder_mt.c`, `outqueue.c`, `hardware_cputhreads.c`, `tuklib_cpucores.c`), so `xz -T` works; `XZ_DEFAULTS` limits compression memory to 20 MiB |
| `zstd unzstd zstdcat` | Zstandard 1.5.7 | `-DZSTD_MULTITHREAD -DZSTD_NOBENCH -DZSTD_NODICT -DZSTD_LEGACY_SUPPORT=0 -DZSTD_DISABLE_ASM -DBACKTRACE_ENABLE=0 -DZSTD_NOTRACE` | `zstd -T` works; built through `sh -c` for the globs |
| `kilo` | kilo (antirez, BSD 2-clause), `third_party/kilo` | `-std=c99 -D_DEFAULT_SOURCE -D_BSD_SOURCE` | a full-screen text editor: raw mode, escape sequences, syntax colours (section 11.12) |
| `fetch` | `usr/musl/fetch.c`, `nvxtls.c` with BearSSL (`third_party/bearssl`) | `-Wall`, linked with `build/bearssl/libbearssl.a`; rebuilt every time | HTTP and HTTPS downloader (section 6.11) |
| `qjs` | QuickJS-ng, `third_party/quickjs` (MIT) | `-D_GNU_SOURCE`, `qjs.c quickjs.c libregexp.c libunicode.c dtoa.c quickjs-libc.c gen/repl.c gen/standalone.c -lm` | JavaScript interpreter with the `std` and `os` modules and a REPL; listed in the program table in `core/proc/jobs.c` (section 13.13) |

After building, each is copied to `build/user/<n>.elf`, embedded compressed (`embz`) and renamed to `.rodata` like the musl programs. Delete `build/ports-dyn/<n>` to force a rebuild. Linking dynamically made the ports about 0.5 MB smaller in total before compression (sqlite3 1.41 MB, zstd 750 KB, xz 256 KB, gzip 96 KB, bzip2 91 KB, kilo 31 KB, fetch 186 KB).

Three other third-party components are kernel code, not user programs:

* Lua 5.5.1 (`third_party/lua`): compiled with `LUAF` (freestanding, SSE enabled) against its own mini libc `nvxlibc.c` (190 lines, output through `lua_emit`), linked as `lua_all.o`; `nvxlua.c` exposes `nvx.window/clear/rect/text/present/waitkey...` and `nvx_lua_run()` backs the shell's `lua <code>` command.
* Doom (`third_party/doom`, doomgeneric): `build_doom.sh` compiles it with SSE at 640x400, `ld -r` combines it and `objcopy --keep-global-symbol` keeps only the interface (`doomgeneric_Create`, `doomgeneric_Tick`, `DG_ScreenBuffer`, `dsh_set_*`, `webp_decode`). `apps/games/doom.c` loads the WAD from a boot module or `DOOM1.WAD`/`FREEDOOM.WAD`/... on the FAT disk (32 MiB buffer).
* NTFS (ntfs-3g, `third_party/ntfs`): built with `-Ulinux -U__linux__ ...` against `compat/` headers and `ntfslibc.c`, mkntfs with `-Dmain=mkntfs_main`; used by `fs/ntfs/ntfsk.c`.

## 6.8 Test programs

`usr/musl/mhello.c`, prints `uname()` and a float; `mhello x` exits 3; `mhello env` prints `MT_VAR` and exits 4/5. `musltest` and the host harness use these exit codes to check `execve`, `posix_spawn` and environment passing.

`usr/musl/musltest.c`, a libc and kernel self-test that also runs on real Linux as the reference. It works in `$MT_DIR/mt<pid>` (default `/tmp`) and prints `ok`/`FAIL` per check:

| Group | Exercises |
|---|---|
| `t_args`, `t_printf`, `t_malloc`, `t_tls`, `t_math`, `t_setjmp` | argv/environ, formatting, 3 MiB malloc via mmap, `__thread` (needs `arch_prctl`), libm |
| `t_files`, `t_dirs`, `t_vfs` | stdio, `pread/pwrite/ftruncate`, `O_EXCL`, file mmap, `getdents64` incl. `.`/`..`, inode match, unlinked-but-open files, `O_APPEND`, directory rename, link counts |
| `t_fork`, `t_exec`, `t_cow` | pipe/fork/waitpid, ECHILD, execve, `posix_spawn` with envp, `atexit`, copy-on-write isolation, lazy 8 MiB mapping, 40 KiB stack growth |
| `t_signals`, `t_time` | SA_SIGINFO, masks and `sigpending`, `kill`+`WTERMSIG`, EINTR from `nanosleep`, `sigsuspend`, clocks, `strftime` |
| `t_sys`, `t_errors`, `t_fixes` | `uname`, `sysconf`, `getrandom`, `/dev/null`, `dup2`, poll, and ~30 exact errno checks (EFAULT on unmapped buffers, ESPIPE, EAGAIN, EISDIR, ENOTDIR, ENOTTY ...), sendfile into a full pipe, `/proc` read far past the end, permission checks after `setuid(1000)` |

`usr/musl/smptest.c`, `smptest [workers] [rounds] [spin]` (defaults 8/24/3,000,000, max 12 workers) forks workers that spin, record `getcpu` in a CPU bitmask, write/read/verify/unlink files in `/tmp`, fork children over pipes every 4th round and test copy-on-write of a 64 KiB anonymous mapping every 5th round. Reports go back through one pipe; output goes to stdout and `/dev/kmsg`. PASS needs all reports, no errors, clean exits and, with at least 2 CPUs (`sysconf(_SC_NPROCESSORS_ONLN)`), at least two CPUs used.

`usr/musl/featest.c` (v8.97), `featest [group]` runs the checks for the features added in v8.97 and prints `featest: ok ...` or `featest: FAIL ...` lines and a `featest: done N passed, M failed` summary to stdout and `/dev/kmsg`. Groups: `fds` (200 open files, `select` and `poll` on descriptor 150, 60 pipes), `mem` (96 MiB `malloc`, two 64 MiB `mmap`s), `timers` (`alarm`, `pause`, `setitimer` with a 50 ms interval, disarming, the default action), `server` (listen/accept over loopback, non-blocking accept, `accept4` flags, refused connection, UDP over loopback), `threads` (section 5.15), `tty` (termios, window size, cursor position reports, scroll region, VMIN/VTIME, alternate screen; only when stdin is the console). `featest listen` is not part of the default run: it waits up to 60 s for a connection to port 9200 from another machine and answers it (the network check uses it).

`usr/musl/dyntest.c` (v8.97) is linked dynamically. It checks that it was started through the dynamic linker (`AT_BASE`), `dl_iterate_phdr`, `malloc` and threads in the shared libc, then writes a small library (`libnvxdemo.so`, embedded in the program) to `/tmp`, loads it with `dlopen`, looks up functions and a variable with `dlsym`, calls into it, checks that its constructor ran and that it can call back into libc, and closes it.

`usr/musl/nettest.c`, `nettest [host]` (default `noovex.test`) exercises the Linux socket layer against the test peer in `tools/tests/net/netpeer.py`: `getaddrinfo`, a 300 KB HTTP download checked byte by byte, a 100 KB TCP echo with `shutdown(SHUT_WR)`, UDP `sendto`/`poll`/`recvfrom`, a non-blocking `connect` + `poll` + `SO_ERROR`, `ECONNREFUSED` on a closed port and `EAFNOSUPPORT` for IPv6. It prints `nettest: PASS` or `nettest: FAIL` to stdout and `/dev/kmsg`. `tools/check.py --only net` runs it in QEMU.

Host harness. `tools/tests/linux/hh.c` compiles the whole kernel as a Linux program (`hk_glue.c` does `#include "kernel64.c"`), traps user `syscall`/`int 0x80` with seccomp and feeds them to `linux_syscall`/`syscall_dispatch`. It runs mhello, legacy programs, every compressor round trip, the SQLite sequence, `smptest 3 3 200000`, `musltest` (requires at least 80 `ok`), optionally `lxfuzz.c` (random system calls with hostile arguments), and then checks that no frames, vfile slots or pipes leaked. Arguments with spaces are passed with `\037` exactly as the shell does.

## 6.9 Step-by-step porting guide

### Step 1, port a portable C command-line program with musl

1. Run `./build.sh` once so `build/musl` exists.
2. Put the sources under `third_party/<name>/` (or a single file under `usr/musl/`).
3. Build by hand to see what breaks:

```sh
cd NoovexOS-64-bit
export NVX_MUSL=$(pwd)/build/musl
tools/mcc -O2 -w -o /tmp/myprog third_party/myprog/*.c
file /tmp/myprog
```

`file` must say `static-pie linked`. Typical fixes: replace `configure` output with a hand-written config header passed via `-include` (as `third_party/xz/nvx_config.h` does), disable threads (`SQLITE_THREADSAFE=0`, no `HAVE_PTHREAD`), assembly (`ZSTD_DISABLE_ASM`), and plugin loading (`SQLITE_OMIT_LOAD_EXTENSION`).

Minimal template, the same shape as `usr/musl/mhello.c`:

```c
#include <stdio.h>
#include <sys/utsname.h>
int main(int argc,char** argv){
    struct utsname u; uname(&u);
    printf("%s on %s %s, %d args\n",argv[0],u.sysname,u.release,argc);
    return 0;
}
```

### Step 2, add it to `build.sh` and `JS_BIN`

1. In the `[ports]` block of `build.sh` add a line after the zstd port:

```sh
port myprog $MCC -O2 -w -I$TP/myprog -o $P/myprog $TP/myprog/main.c $TP/myprog/util.c
```

2. Add `myprog` to the loop `for n in sqlite3 gzip bzip2 xz zstd kilo fetch qjs; do` so it is copied, embedded with `emb user_myprog_bin.o myprog` and moved to `.rodata`.
3. Append `user_myprog_bin.o` to `BLOBS` in step `[4/6]`.
4. In `core/proc/jobs.c`:

```c
extern const unsigned char _binary_user_myprog_elf_start[], _binary_user_myprog_elf_end[];
```

and a row before `{"+init",...}`:

```c
    {"myprog",_binary_user_myprog_elf_start,_binary_user_myprog_elf_end},
```

5. Rebuild. The program appears in `/bin` and runs as `myprog args`, in pipes and in the background. Watch the 8 MiB kernel assertion. For a single test file in `usr/musl/`, add its name to `for p in mhello musltest smptest` instead of using `port`.

### Step 3, run it from disk instead

Embedding is optional: `jc_find_prog` falls back to the file system.

1. Get the stripped ELF onto the machine, e.g. on a FAT USB stick: as root `mount usb /mnt/usb` (`ux_mount` ignores the source and only accepts the target `/mnt/usb`), then `cp /mnt/usb/MYPROG /home/myprog` (copying makes it an NVXFS file).
2. `chmod 755 /home/myprog` (needed for non-root users).
3. Run `/home/myprog args`. A bare name works only if it is in `JS_BIN`; `/bin/` is only an alias for embedded programs.

Limits: the whole image is read from memory (`FsFile.data`), so it must fit the file layer; the IDE-FAT fallback only takes 8.3 names up to 64 KiB; the PIE image must end below `0x40002000000` ( about 28 MiB of `PT_LOAD` memory).

### Step 4, find missing system calls and add one

A missing call shows up as `ENOSYS` (`-38`). To see which one, temporarily add a `klog(...)` of `nr` before the final `return -LX_ENOSYS;` in `lx_dispatch`. Then add a `case` to the switch in `sys/linux/linux.c`. Rules that follow from the existing code:

* Read arguments from `a1...a6` (`rdi rsi rdx r10 r8 r9`); return `-LX_E...` on error.
* Never dereference a user pointer without `lx_ok`/`lx_get`/`lx_put`/`lx_str`; turn paths into absolute ones with `PATH1(dirfd,ptr,empty_ok)`.
* To wait, call `jc_block(r,S_WAIT,g_ticks+1)` and re-check the condition when the call is restarted; for a timed sleep use `lx_sleep_ns`.
* The whole handler runs under the BKL; keep it short.

Example, `syncfs` (Linux 306), modelled on `sync` (162):

```c
    case 306: if(!lx_vf((int)a1))return -LX_EBADF;
        for(int k=0;k<FS_N;k++)g_fs_dirty[k]=0; nvx3_sync_all(); return 0;
```

Then extend `musltest.c` with a `CHECK`, rebuild, and run `musltest` in NoovexOS and on Linux to compare.

### Step 5, port a native nvxlibc program

Use this when you want the native API (`nvx/extras.h`, native window/IPC syscalls) or a tiny binary.

```c
#include "posix.h"
int main(int argc,char** argv){
    char b[256]; long n;
    while((n=read(0,b,sizeof b))>0)write(1,b,(size_t)n);
    return argc>1;
}
```

1. Save as `usr/src/mycat.c` (`upper.c` and `greet.c` are good models).
2. Add `mycat` to `for p in count spin sigdemo upper greet pipetest nvxbox init libc8; do`. The loop compiles with `LFLAGS`, links with `user.ld`, `crt0.o` and `libc.a`, embeds `user_mycat_bin.o`, renames to `.rodata`.
3. Add `user_mycat_bin.o` to `BLOBS`, the `extern` and the `JS_BIN` row as in step 2.

Constraints: the image is linked at `0x40000000000` and must end before the stack at `0x400000F1000` ( about 964 KiB, enforced by `elf_build_space`); it may not issue `syscall` (it would switch the process to Linux mode and its next `int 0x80` gets SIGSYS); `execve` drops `envp`; threads are the legacy cooperative kind.

### Step 6, a GUI-ish program (framebuffer, mouse, sound)

There is no X11/Wayland/SDL; a Linux program can draw on the raw screen:

| Device | Kernel id | Use |
|---|---|---|
| `/proc/fb` | procfs | text `"W H 32 PITCH\n"`, the only way to learn the resolution (no `FBIOGET_*` ioctls) |
| `/dev/fb0` | 9 | `W*H*4` bytes of 0x00RRGGBB pixels (`SCENE`, stride `SW == W`); `lseek` + `write`/`read`; `mmap` is `-ENODEV` |
| `/dev/input/mouse`, `/dev/input/mice` | 10 | 3-byte PS/2 packets: `0x08\|buttons\|sign bits`, dx, dy (positive dy is up: the desktop does `my-=dy` in `graphics/ui/input.c`); blocks when empty unless `O_NONBLOCK`; works with `poll` |
| `/dev/dsp`, `/dev/audio` | 12 | 16-bit little-endian stereo PCM at `g_snd.rate` (48 kHz by default) on HDA or AC'97 |
| `/dev/kmsg` | 13 | lines go to `klog` |

While any process holds `/dev/fb0` open (`g_fb0_open > 0`), `core/init/main.c` stops drawing the desktop and blits `SCENE` whenever the program wrote to it (`g_fb0_dirty`); closing it restores the desktop. Mouse packets are only queued while the mouse device is open (`ux_mouse_push`).

```c
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
int main(void){
    int w=0,h=0; FILE* f=fopen("/proc/fb","r"); if(!f||fscanf(f,"%d %d",&w,&h)!=2)return 1; fclose(f);
    int fb=open("/dev/fb0",O_RDWR), ms=open("/dev/input/mice",O_RDONLY|O_NONBLOCK);
    unsigned* row=malloc((size_t)w*4); int x=w/2,y=h/2;
    for(int j=0;j<h;j++){ for(int i=0;i<w;i++)row[i]=(unsigned)((i*255/w)<<16|(j*255/h)<<8); lseek(fb,(off_t)j*w*4,SEEK_SET); write(fb,row,(size_t)w*4); }
    for(;;){
        struct pollfd p={ms,POLLIN,0}; poll(&p,1,50);
        signed char m[3]; while(read(ms,m,3)==3){ if(m[0]&1){ close(fb); return 0; } x+=m[1]; y-=m[2];
            if(x<0)x=0; if(y<0)y=0; if(x>=w)x=w-1; if(y>=h)y=h-1;
            unsigned c=0xFFFFFF; lseek(fb,((off_t)y*w+x)*4,SEEK_SET); write(fb,&c,4); }
    }
}
```

Keyboard input only arrives through the tty in line mode: `tty_key()` buffers characters until Enter and `TCSETS` is ignored, so there is no raw or non-canonical mode. Programs that need per-key input (games, editors) cannot get it from the Linux layer today.

### Step 7, a network client

Since v8.95 ordinary socket code works unchanged. This program fetches a page
over plain HTTP:

```c
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char** argv) {
    const char* host = argc > 1 ? argv[1] : "example.com";
    struct addrinfo hints = {0}, *ai;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, "80", &hints, &ai)) { puts("dns failed"); return 1; }
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(s, ai->ai_addr, ai->ai_addrlen)) { perror("connect"); return 1; }
    dprintf(s, "GET / HTTP/1.0\r\nHost: %s\r\n\r\n", host);
    char buf[4096]; ssize_t n;
    while ((n = read(s, buf, sizeof buf)) > 0) fwrite(buf, 1, n, stdout);
    close(s); freeaddrinfo(ai); return 0;
}
```

Things to know:
- Run `net` (or join Wi-Fi) once before starting the program. `connect` returns
  `ENETUNREACH` while there is no link.
- DNS works without `/etc/resolv.conf`: queries to musl's default `127.0.0.1:53`
  are forwarded to the DHCP DNS server.
- For HTTPS, link the program with BearSSL and `usr/musl/nvxtls.c` as `fetch`
  does (section 6.11). It then checks certificates against the same roots as
  the kernel's `https` command.
- Server programs can `bind`, `listen` and `accept` (section 9.7.4), on the
  network card and on 127.0.0.1.

### Step 8, common pitfalls

| Pitfall | Cause in the code | What to do |
|---|---|---|
| Out of memory / SIGKILL under load | all user pages come from one frame pool (32 MiB plus up to half the free heap, section 3.7); brk heap at most 32 MiB (musl's malloc then uses mmap) | lower working sets on machines with little RAM (cf. `XZ_DEFAULTS=--memlimit-compress=20MiB`) |
| Stack overflow in deep recursion | 256 stack pages = 1 MiB (`JS_STK_PAGES`, reported as `RLIMIT_STACK`); thread stacks are what the program asks for (musl default 128 KiB) | move large arrays to the heap |
| Futexes between processes do not wake | a futex is keyed by thread group and address | use pipes or sockets between processes |
| `EMFILE`/`ENFILE` | 256 fds per process, 1024 open files system-wide, 128 pipes | close descriptors early |
| Short reads/writes | 1 MiB per call (`LX_IOMAX`) | loop until done (stdio already does) |
| `EFBIG` | writes past `FS_CAP` (1 GiB) | keep files smaller |
| Names rejected | 63 characters per component, 256 per path, `execve` path at most 63 and no spaces | shorten paths |
| Writes to a mapped file are lost | file `mmap` is a private copy, `prot` ignored | use `write`/`pwrite` |
| Box-drawing or other non-ASCII characters show as `-`, `\|`, `+` or `?` | the console font is ASCII; `vt_out` maps common UTF-8 symbols to ASCII | use ASCII where it matters |
| IPv6-only code fails | `socket(AF_INET6)` to EAFNOSUPPORT | use `AF_INET` or `getaddrinfo` with `AF_UNSPEC` (musl falls back) |
| Real-time signals have no effect | signals at least 32 are accepted and ignored | use signals 1-31 |
| ncurses programs cannot find a terminal description | there is no terminfo database in the image | build ncurses with `--with-fallbacks=xterm-256color`, or write escape sequences directly as kilo does |
| A native program dies with SIGSYS | it executed `syscall`; `jc_syscall` rejects `int 0x80` once `p->lx` is set | do not mix ABIs in one process |
| Kernel build fails at `[6/6]` | the flat image exceeds 8 MiB | run the program from disk instead of embedding it |

---

## 6.10 Dynamic linking

Since v8.97 NoovexOS runs dynamically linked musl programs, so ports can be built the way Linux distributions build them.

### How a dynamic program starts

1. `elf_build_space` finds `PT_INTERP` (normally `/lib/ld-musl-x86_64.so.1`) in the program and maps the program at `ELF_PIE_BASE` as usual.
2. `elf_load_interp` calls `jc_find_lib(path)` (`core/proc/jobs.c`). A name ending in `ld-musl-x86_64.so.1`, `libc.so` or `libc.musl-x86_64.so.1` returns the shared musl that is embedded in the kernel (compressed, `user_libc_so_bin.o`, inflated once through `jc_unz`). Any other path is looked up in the file system, so an interpreter installed on disk also works.
3. The interpreter's `PT_LOAD` segments are copied to `ELF_INTERP_BASE` (`0x7F000000000`). The process starts at the interpreter's entry point with `AT_BASE = 0x7F000000000`, and `AT_PHDR`, `AT_PHNUM` and `AT_ENTRY` describing the program.
4. musl's dynamic linker relocates itself, finds the program through `AT_PHDR`, and satisfies `DT_NEEDED: libc.so` with itself. Other libraries are searched in `LD_LIBRARY_PATH`, `/etc/ld-musl-x86_64.path` and `/lib:/usr/local/lib:/usr/lib`; it opens them and maps them with `mmap` on the file (a private copy, section 3.10) and `MAP_FIXED` for the segments. `dlopen` uses the same path.

The library's pages are copied for every process; nothing is shared between processes yet, so dynamic linking saves space in the image and on disk, not RAM.

### Building a dynamic program

```sh
export NVX_MUSL=$PWD/build/musl NVX_MUSL_SH=$PWD/build/musl-sh
tools/mcc-dyn -O2 -o hello hello.c
```

For a shared library, compile with `-fPIC` against the musl headers and link with `gcc -shared -nostdlib ... -L$NVX_MUSL_SH/lib -lc`, as `build.sh` does for `libnvxdemo.so`. Copy the library to `/lib` (or set `LD_LIBRARY_PATH`) on the NoovexOS side.

### Limits

- Only PIE (`ET_DYN`) programs can have an interpreter; a non-PIE `ET_EXEC` with `PT_INTERP` is refused.
- `mprotect` is a no-op, so RELRO does not make pages read-only.
- Text relocations work (every page is writable) but are not needed by gcc's default PIE code.

---

## 6.11 TLS for programs: BearSSL, `fetch` and the root store

### Pieces

| Piece | Where | What it is |
|---|---|---|
| BearSSL | `third_party/bearssl` | Upstream BearSSL (master, commit 3c04036, June 2024; `VERSION.txt`), MIT licence. TLS 1.0-1.2 client and server, X.509 validation, no TLS 1.3. `tools/build_bearssl.sh` compiles its 294 files with `mcc-dyn` into `build/bearssl/libbearssl.a`, with `BR_USE_GETENTROPY` and `BR_USE_URANDOM` |
| The kernel's root store | `/etc/ssl/nvx-anchors.pem` | Written at boot by `ux_write_anchors` (`fs/vfs/unixfs.c`) from the kernel's 120 Mozilla roots (`lib/crypto/nvxroots.c`), and rewritten only when the kernel's store has changed. Each root is two PEM objects, `TRUST ANCHOR NAME` (the subject name, DER) and `PUBLIC KEY` (SubjectPublicKeyInfo), which is exactly what a trust anchor needs |
| Local CAs | `/etc/ssl/local-ca.pem` | The same file the kernel's `https` command reads: ordinary PEM certificates |
| Helper | `usr/musl/nvxtls.c`, `nvxtls.h` | `nvx_anchors_load(&a, roots, local_ca)` turns both files into an array of `br_x509_trust_anchor` (RSA and EC P-256/P-384/P-521 keys); `nvx_tls_error(err)` gives a readable message for a BearSSL error code |
| `fetch` | `usr/musl/fetch.c` | Downloads a URL over HTTP or HTTPS |

### `fetch`

```
fetch [-k] [-q] [-v] [-s] [-o FILE] URL
```

- `URL` is `http://host[:port]/path` or `https://...`; without a scheme HTTPS is used.
- HTTPS: `br_ssl_client_init_full` with the anchors, SNI with the host name, full chain validation, host name check, and the dates against the real-time clock. For a host given as an IPv4 address, a small wrapper around BearSSL's validator (`ipcheck_vt`) checks the address against the `iPAddress` entries of the certificate's subjectAltName, because BearSSL itself only matches DNS names.
- `-k` replaces the validator with one that only extracts the server key (`nocheck_vt`): encrypted, but not authenticated.
- HTTP/1.1 with `Connection: close`; `Content-Length`, chunked transfer encoding and bodies that end when the connection closes; up to 5 redirects (301, 302, 303, 307, 308; absolute or relative `Location`).
- Messages go to standard error: the TLS version and cipher suite and whether the certificate was verified, redirects, and a final line `fetch: URL -> HTTP code, N bytes`. `-s` adds the SHA-256 of the body, `-v` the response headers and the number of trust anchors loaded, `-q` silences everything.
- Exit status: 0 success, 1 usage or bad URL, 2 name lookup or connection failure, 3 TLS or certificate failure, 4 HTTP status 400 or above or too many redirects, 5 cannot write the output.
- `NVX_ROOTS` and `NVX_LOCAL_CA` override the two file names (the host test uses them).

Examples:

```
fetch https://example.org/ > page.html
fetch -o file.tar.gz https://example.org/file.tar.gz
fetch -s -o /dev/null https://noovex.test:9443/big
```

### Using BearSSL in your own program

1. Link with `-I third_party/bearssl/inc -I usr/musl usr/musl/nvxtls.c build/bearssl/libbearssl.a`.
2. Load the anchors: `nvx_anchors a; nvx_anchors_load(&a, "/etc/ssl/nvx-anchors.pem", "/etc/ssl/local-ca.pem");`
3. Set up the client as in BearSSL's `samples/client_basic.c`: `br_ssl_client_init_full(&sc, &xc, a.v, a.n)`, `br_ssl_engine_set_buffer`, `br_ssl_client_reset(&sc, host, 0)`, then `br_sslio_init` with `read`/`write` callbacks on the socket.
4. After a failure, `nvx_tls_error(br_ssl_engine_last_error(&sc.eng))` explains it.

The kernel's own TLS 1.3 client (`https` command, section 9.9) and BearSSL in user space are separate implementations that share the root store. BearSSL speaks TLS 1.2 at most, so a server that only accepts TLS 1.3 cannot be reached with `fetch`; most servers accept both.

## 6.12 FFmpeg and the `play` command

`play` (`usr/musl/play.c`) is a video and audio player built on FFmpeg 8.1.2. The source in `third_party/ffmpeg` is the ffmpeg-rockchip fork; its Rockchip hardware decoders only run on Rockchip ARM chips, so they are left out and everything is decoded on the CPU.

Build. `build.sh` configures FFmpeg once into `build/ffmpeg` with `tools/mcc-dyn` as a cross compiler for `x86_64-linux`, then builds the static libraries and links `play` as a dynamic musl program like the other ports (section 6.9):

| Option | Value |
|---|---|
| Libraries | `libavformat`, `libavcodec`, `libswscale`, `libswresample`, `libavutil` (static, PIC); no programs, filters, devices or network |
| Demuxers | `mov` (MP4, M4A, MOV), `matroska` (MKV, WebM), `ogg`, `mp3`, `aac`, `wav` |
| Decoders | `h264`, `hevc`, `vp9`, `vp8`, `aac`, `aac_latm`, `mp3float`, `opus`, `vorbis`, `pcm_s16le` |
| Speed | `--enable-x86asm` (NASM; SSE2 to AVX2, chosen at run time by CPU flags) and `--enable-pthreads` (frame and slice threads on all cores) |

The stripped program is 9.6 MB, 3.4 MB after the embedded-program compression. It did not fit the 8 MiB kernel limit, which is why v9.11 raised the limit to 15 MiB (section 2.13). Delete `build/ffmpeg` to rebuild FFmpeg and `build/ports-dyn/play` to relink the player.

Using it:

```
play video.mp4                 fullscreen with sound
play -b video.mp4              decode only, print frames per second
play -s 12.5 frame.ppm a.mkv   save the frame at 12.5 s as a PPM image
```

Keys while playing: space or `p` pauses, left/right seek 10 s, up/down seek 60 s, `q` or Esc quits.

How it works:

| Part | Implementation |
|---|---|
| Demux | the main thread reads packets with `av_read_frame`; audio packets go to a queue for the audio thread |
| Video | decoded in the main thread with frame threads; each frame is converted and scaled by `libswscale` straight into a screen-sized BGRA buffer (aspect ratio kept, letterboxed, bilinear) and written to `/dev/fb0` |
| Audio | the audio thread decodes, converts to 16-bit stereo at the device rate with `libswresample` and writes to `/dev/dsp`, whose writes block while the hardware buffer is full |

| Sync | a monotonic clock started at the first frame is the master; video frames wait until their time, frames more than 80 ms late are dropped (one in eight is still shown), and when the video falls 300 ms behind the decoder skips non-reference frames until it catches up |
| Screen | `ioctl(FBIOGET_VSCREENINFO)` and `ioctl(FBIOGET_FSCREENINFO)` on `/dev/fb0` give the size and line length; while `/dev/fb0` is open the desktop or live terminal stops drawing, and repaints fully when it is closed |

Measured decode speed (`play -b`, 3 decoder threads):

| Clip | Linux test machine, NoovexOS musl build | NoovexOS in QEMU (emulated CPU) |
|---|---|---|
| H.264 1280x720 | 500 fps | 38 fps |
| H.264 1920x1080 | 234 fps | n/a |
| H.265 1280x720 | 283 fps | n/a |
| VP9 1280x720 | 504 fps | n/a |

QEMU's default CPU model has no SSSE3, so the colour conversion falls back to plain C there; run QEMU with `-cpu max` to get numbers closer to real hardware.

Limits: no subtitles and no hardware decoding. The Video Player window and the browser's `<video>` use the same program (sections 6.13 and 6.14).

Since v9.12 `play` also accepts an `http://` or `https://` URL (read with Range requests through `usr/musl/httpio.c`, which keeps the last 8 MB in a ring so the demuxer can seek back without a new request), and has an embedded mode used by the media service (section 6.13).

## 6.13 The media service (`/dev/media`)

The kernel cannot run FFmpeg, so every video is decoded by a `play` process. `core/media.c` keeps up to 8 media slots (`MEDIA_MAX`). Each slot has:

| Field | Meaning |
|---|---|
| two streams | growable byte buffers for data handed to the kernel (one for a normal file, two for split video and audio). The first append that starts with `ftyp` or the EBML magic is remembered as the init segment. Data the decoder has passed by more than 8 MB is dropped (`compact`), keeping the init segment |
| frame | the last decoded frame, BGRA, already scaled by the process to the size the viewer asked for, and a sequence number |
| status | `struct media_status`: current and total time in ms, flags (`PLAYING`, `PAUSED`, `ENDED`, `ERROR`, `VIDEO`, `AUDIO`, `WAITING`, `READY`), video width and height, percent buffered |
| commands | a 32-entry queue: `PLAY 1`, `PAUSE 2`, `SEEK 3` (ms), `VOLUME 4` (0-1000), `SIZE 5` (w, h), `QUIT 6`, `LOOP 7`, `DURATION 8` (a duration hint from the page) |

`media_start` runs `play --embed ID --gen G [--split] [--start MS] [--paused] [--size WxH] [--url]` with stdin on `/dev/null` and stdout/stderr on `/dev/kmsg`. The process opens `/dev/media` and binds it to its slot (the generation number makes a stale process unable to touch a reused slot). The `ioctl` calls:

| Request | Argument | Use |
|---|---|---|
| `MIO_BIND 0x4D01` | `id \| gen<<8 \| stream<<24` | attach this descriptor to a slot |
| `MIO_READAT 0x4D02` | `{off, buf, len, stream}` | read stream data at an offset; returns the byte count, 0 at the end, or -EAGAIN when the data is not there yet |
| `MIO_INFO 0x4D03` | `{len, complete, split, total}` | how much data there is and whether it is complete |
| `MIO_STATUS 0x4D04` | `struct media_status` | the process reports its state (about every 100 ms) |
| `MIO_GETCMD 0x4D05` | `{cmd, a, b}` | take the next command; returns 1 if there was one |
| `MIO_FRAME 0x4D06` | `{buf, w, h, pts}` | copy a frame into the kernel |
| `MIO_URL 0x4D07` | `{buf, cap}` | fetch the slot's URL or file path (it can be longer than a command line) |

Inside `play`, the input is an `AVIOContext`: a local file, `httpio`, or `ks_read`, which polls `MIO_READAT` and sleeps 8 ms when no data has arrived, checking commands while it waits. If it waits more than 150 ms, the playback clock stops (`WAITING`) and restarts when data comes. With two streams there are two demuxers; the main loop reads from the one whose last packet is earlier, so audio stays a little ahead of video. Seeking a URL or file uses `av_seek_frame` and then skips frames until the target time. Seeking in MSE mode is done by the kernel: `media_restart` kills the process, keeps the init segments (and, if the page says the target is already buffered, all data) and starts a new one with `--start`. When the input is a kernel stream that is not complete, the duration is reported as unknown and the page's `MediaSource.duration` is used instead.

`media_tick` runs in the kernel main loop and compacts the stream buffers. `media_free` sends `QUIT` and kills the process.

## 6.14 The Video Player window (`apps/media/videoplayer.c`)

`W_VIDEO` is a window like the others. `video_open_url` starts a media slot for a path or URL, sized to the window; `video_open_slot` builds the full path of a file from the directory tree. Files with the extensions mp4, m4v, mov, mkv, webm, mp3, m4a, aac, ogg, oga, ogv, opus and wav open in it from Files and the desktop, and `video` (or `player`) does the same from the terminal (in the live terminal it switches to the desktop first). Closing the window frees the slot.

`media_blit(id, x, y, w, h, bg)` draws a slot's frame into `SCENE`, keeping the aspect ratio and filling the rest with `bg`; the 1:1 case copies whole rows. `video_tick` asks for a full redraw only when the status changes (every second for the clock, or a new flag). New frames take the fast path `video_fast`: if `desk_win_exposed(W_VIDEO)` says no other window, menu, pop-up, toast or drag is over the window, the frame is drawn into `SCENE` and `present_rect` copies only that rectangle to the screen (with the night-light tint and the cursor drawn over it if needed). Otherwise a full frame is composed.

The kernel main loop calls `video_fast` and then `browser_media_fast` when nothing else changed. It also no longer recomposes the desktop after every scheduler slice in which a program ran: `jc_step` returns bit 0 when a program ran and bit 1 when one marked the screen dirty; the desktop redraws on bit 1, on terminal output, or every 50 ticks. Before v9.12 a decoding process got only the time between full redraws.

## 6.15 glibc programs

### Building glibc

`tools/build_glibc.sh` builds glibc 2.44 from `third_party/src/glibc-2.44.tar.gz` (`--prefix=/usr`, `--libdir=/usr/lib/x86_64-linux-gnu`, `--enable-kernel=4.4`) into `build/glibc-root` and is skipped when that directory already holds `lib64/libc.so.6`. glibc's `configure` needs GNU awk.

### The sysroot (`fs/vfs/sysroot.c`, `tools/mksysroot.py`)

`build.sh` copies into `build/sysroot-stage`:

| Path | Content |
|---|---|
| `/lib64` | the glibc loader and libraries (`libc`, `libm`, `libpthread`, `libdl`, `librt`, `libresolv`, `libutil`, `libanl`, `libnss_files`, `libnss_dns`, `libmvec`), stripped, plus every other library the programs below need (found with `ldd` on the build machine) |
| `/lib/x86_64-linux-gnu`, `/usr/lib/x86_64-linux-gnu`, `/usr/lib64` | symbolic links to `/lib64` |
| `/opt/gnu/bin` | bash, coreutils (ls, cat, sort, wc, ...), grep, sed, awk, find, xargs, diff and a glibc test program `gthreads` |
| `/etc/nsswitch.conf` | `files` for passwd and group, `files dns` for hosts |

`mksysroot.py` packs the tree into one file, format `NVS2`: a 16-byte header (magic, entry count, block count, string table offset), a block table (type, offset, compressed and original size), 24-byte entries (name and link target as string table offsets, mode, block, offset in the block, size), the string table, then the blocks. Files are grouped into blocks of up to 8 MiB; ELF files go into blocks compressed with xz and the x86 branch filter, other files into plain xz blocks. It is linked into the kernel as `sysroot_bin.o`. The old `NVSR` format (one raw-deflate stream per file) is still read.

The sysroot is a stack of layers. Layer 0 is the one in the kernel; every installed Debian package is one more layer (section 6.16). `sysroot_add_layer` and `sysroot_del_layer` add and remove layers; after each change the index is rebuilt:

- every path is a node in a hash table (FNV-1a, open addressing); directories are implicit, created from the paths of their files, and keep a list of their children;
- links in layer 0 are placed first, then all package layers (newest slot first), then the files of layer 0; the first file placed at a path wins, so a package can replace a library from layer 0;
- a file's parent directory is resolved through existing links before it is placed, so a package file `/usr/lib/x86_64-linux-gnu/libfoo.so.1` lands in `/lib64`, as on a Debian system with merged `/usr`.

A block is decompressed on the first use of any file in it and kept. Lookups walk the path one component at a time and follow links (up to 16), also through `..`. `sysroot_mkdirs` (end of `ux_boot_layout`) creates sysroot directories up to three levels deep in the directory tree; deeper ones are still listed and entered through `LX_SRDIR`.

| Kernel part | What it does with sysroot files |
|---|---|
| `jc_find_lib` | the ELF interpreter (`/lib64/ld-linux-x86-64.so.2`) comes from the sysroot |
| `jc_find_prog` | absolute paths to executable sysroot files run first; a bare name is tried in `/opt/gnu/bin`, `/usr/local/bin`, `/usr/bin`, `/bin`, `/usr/sbin` and `/sbin` |
| `jc_build`, `lx_execve` | a file starting with `#!` runs its interpreter with the script path inserted (`#!/usr/bin/env python3`, `#!/bin/sh`); `/bin/sh`, `/bin/bash`, `/usr/bin/bash` and `/usr/bin/env` are sysroot links to the GNU programs |
| `ux_stat2` | file mode, size and a per-path inode number (an FNV hash) for files and directories; the loader compares inode numbers to tell libraries apart, so they must differ. Without following links (`lstat`) a sysroot link reports mode `0120777` |
| `ux_readlink` | sysroot link targets (`readlink`, used by Python to find its prefix) |
| `lx_open` | a sysroot directory that is not in the directory tree opens as `LX_SRDIR` |
| `lx_getdents` | lists `LX_SRDIR` directories, and adds sysroot entries after the tree's own entries in mixed directories |
| `lx_path` | relative paths against an `LX_SRDIR` directory descriptor |
| `lx_mmap` | file mappings of sysroot files (how the loader maps libraries) |
| `lx_fstat` | per-path inode numbers |

### Limits

| Limit | Reason |
|---|---|
| Only position-independent executables (PIE) | user programs live at `0x40000000000`; the low 4 GiB are the kernel's identity map, so a fixed-address program (`ET_EXEC`, for example Ubuntu's `python3` and `gcc`) cannot be placed. Debian builds nearly all programs as PIE, and the built-in Python is built as PIE. |
| `statx` returns `ENOSYS` | glibc then uses `newfstatat`; slower but correct |
| The sysroot is read-only and in memory | files written by programs go to the normal file system; decompressed blocks stay in memory |

### Tracing

`lxtrace N` in the Terminal logs the next N Linux system calls as `sc NUMBER ARG1 ARG2 = RESULT` to the kernel log (`dmesg`, serial port).

### Terminal ioctls

glibc 2.44 asks `isatty` with `TCGETS2` (`0x802C542A`, a 44-byte `termios2`), not `TCGETS`. `lx_ioctl` answers `TCGETS2` and the `TCSETS2`, `TCSETSW2` and `TCSETSF2` requests on the Terminal; before v9.23 glibc programs saw no terminal on standard input, so the Python prompt did not appear.

## 6.16 Debian packages: apt and dpkg

`apt` (also `apt-get`, `apt-cache`) and `dpkg` are Terminal commands in the kernel (`fs/pkg/apt.c`). Commands that change the system need root (`sudo apt install ...`).

| Command | What it does |
|---|---|
| `apt update` | for every `deb URI SUITE COMPONENT...` line in `/etc/apt/sources.list` downloads `dists/SUITE/COMPONENT/binary-amd64/Packages.xz` (or `.gz`, or plain), and saves it as `/var/lib/apt/lists/srcN.pkgs` with the source URI in its first line. A missing `sources.list` is created with `deb http://deb.debian.org/debian trixie main`. `deb URI DIR/` (flat repositories) works too. |
| `apt install [-y] PKG...` | resolves `Pre-Depends` and `Depends`, prints the plan, asks when extra packages are needed, downloads each `.deb`, checks size and SHA256 against the index, unpacks it and marks it installed |
| `apt reinstall`, `remove`, `purge`, `autoremove`, `upgrade` | as on Debian; `purge` is `remove` |
| `apt search WORD`, `show PKG`, `list [--installed|--upgradable] [PATTERN]` | read the saved lists |
| `dpkg -i FILE.deb` | installs a local package (from the file system or the FAT boot disk); dependencies are not fetched |
| `dpkg -l`, `-L PKG`, `-s PKG`, `-r PKG`, `--print-architecture` | list, files, status, remove, `amd64` |

### Resolving

- The index is decompressed into memory and parsed into records (name, version, architecture, dependencies, provides, file name, size, SHA256, description). Only `amd64` and `all` are kept. Names are hashed; for several versions the highest wins, compared with the Debian rules (epoch, upstream, revision, `~` sorts first).
- A dependency group (`a | b (>= 1)`) is met when any alternative is installed, already planned, provided by a planned package, `Essential: yes` or `Priority: required`, or part of NoovexOS. Otherwise the first alternative found (by name or `Provides`) is planned and resolved in turn. Version limits and architecture qualifiers are read but not enforced. `Recommends` are not installed.
- Packages that NoovexOS provides itself are never installed: `libc6`, `libc-bin`, `libgcc-s1`, `python3`, `python3.13`, `python3-minimal`, `libpython3.13-stdlib` and the other Python core packages, `bash`, `coreutils`, `dpkg`, `apt`, `base-files`, `debconf` and a few more (`g_prot`). `apt install python3` answers that it is already the newest version.

### Downloading

`ap_get` sends an HTTP/1.0 request through `tcp_get` (port from the URI, DNS through `dns_query`), follows up to five redirects, decodes chunked bodies and returns the body. `https://` sources use `https_fetch_to` (port 443). The buffer is the expected size plus 256 KiB (64 MiB for indexes). The index is not signature-checked: there is no OpenPGP code, so trust rests on the mirror and HTTPS; each package is checked against the SHA256 in the index.

### Unpacking

- `ar` members `control.tar.*` and `data.tar.*`; compression gzip (`pz_inflate_raw`), xz (`lib/compress/xzk.c`, xz-embedded) or zstd (`lib/compress/zstdk.c`, the zstd decoder); `lib/compress/unpack.c` picks by magic bytes.
- The tar reader handles ustar prefixes, GNU long names (`L`, `K`), pax headers (`path=`, `linkpath=`), symlinks (relative targets made absolute) and hard links (copied).
- The files become one `NVS2` layer with a single stored block, written to `/var/lib/nvpkg/PKG.nvs` and added with `sysroot_add_layer("pkg:PKG", ...)`. Programs in `/usr/bin` run at once; libraries are found by the glibc loader.
- `/var/lib/dpkg/status` keeps Package, Version, Architecture, Depends, Description and Auto-Installed.
- At boot `apt_boot` (in `ux_boot_layout`) loads every `/var/lib/nvpkg/*.nvs`, so installed packages survive restarts on an installed system.

### Limits

| Limit | Reason |
|---|---|
| Maintainer scripts (`preinst`, `postinst`, ...) are not run | no `dpkg` trigger and `debconf` machinery; packages that create files, users or `update-alternatives` links in `postinst` miss those |
| Package files are read-only | they are in the sysroot; a program that edits its own `/etc` file must use a copy |
| No OpenPGP check of `InRelease` | see Downloading |
| Fixed-address (`ET_EXEC`) programs do not run | section 6.15 |
| Up to 1024 packages per install, 255 installed layers | `g_plan`, `SR_MAXL` |

## 6.17 Python 3.13

CPython 3.13 is built from `third_party/src/cpython-3.13.tar.xz` by `tools/build_python.sh` (cached in `build/python-root`; `--prefix=/usr`, position-independent like every program the build machine's gcc makes). `tools/stage_python.sh` puts into the sysroot:

| Path | Content |
|---|---|
| `/usr/bin/python3.13` | the interpreter, stripped; `python3` and `python` link to it |
| `/usr/lib/python3.13` | the standard library without tests, `idlelib`, `tkinter`, `turtledemo` and `lib2to3`, with `.pyc` files made with `--invalidation-mode unchecked-hash` (used without comparing modification times, so imports from the read-only sysroot are fast) |
| `/usr/lib/python3.13/lib-dynload` | extension modules (`_ssl`, `_sqlite3`, `_ctypes`, `_decimal`, `_lzma`, `_bz2`, `readline`, `_curses`, ...), stripped; their libraries (OpenSSL 3, libffi, SQLite, readline, ncurses, ...) go to `/lib64` |
| `/usr/lib/python3.13/ensurepip` | with the bundled pip wheel |
| `site-packages/debian-dist-packages.pth` | adds `/usr/lib/python3/dist-packages`, where Debian's `python3-*` packages install, so modules from `apt install python3-...` import |

`PYTHON_BASIC_REPL=1` is set in the default environment: the new 3.13 prompt needs a terminfo database, the basic prompt does not. `python3 FILE.py`, `python3 -c`, scripts starting with `#!/usr/bin/env python3` and the interactive prompt work in the Terminal and in bash.

## 6.18 The Python IDE package (`python3-ide`)

The sysroot carries a small local package repository in `/usr/share/noovex/repo` (`Packages` plus `pool/*.deb`), built by `build.sh` from `usr/packages/*/` with `dpkg-deb` and `tools/mkrepo.py`. `apt` always reads it, after the downloaded lists, as source `file:/usr/share/noovex/repo/`; `file:` URIs are read from the sysroot or the file system, so it works without a network and without `apt update`.

`sudo apt install python3-ide` (also `ide`, `idle`, `idle3`, `pyide`, `python-ide`, `python3-idle`; so `sudo apt install python3 ide` works) installs the package: `/usr/share/applications/python3-ide.desktop`, an example program and a README. Debian's own `idle3` needs Tk and an X server, which NoovexOS does not have, so these names are redirected.

| Where | What changes once the package is installed (`pyide_installed`: the `.desktop` file exists) |
|---|---|
| Start menu, classic style | Development lists "Python IDE" |
| Start menu, modern style | Productivity lists "Python IDE"; searching "python" or "idle" finds it |
| Terminal | `python3-ide`, `idle`, `idle3` and `pyide` open it |

The IDE is the Editor window in Python mode (`ed_py`, `apps/office/editor.c`): the title reads "Python IDE - main.py", keywords, built-ins, strings, numbers, comments and `def`/`class` names are coloured, Save writes `/root/main.py`, and Run saves and runs `python3 /root/main.py` in the Terminal, so `input()` works. Opening the plain Editor or a file leaves Python mode. Removing the package (`sudo apt remove python3-ide`) takes the entries away again.

## 6.19 X11: the X.Org server (`startx`, `apps/x11/xdisplay.c`)

The server is X.Org's: Xvfb is the same program as Xorg built with a memory framebuffer as its output instead of a graphics card driver, and it implements the whole X11 protocol and its extensions (XTEST, XKB, RENDER, RANDR, ...).

The sysroot carries X.Org's `Xvfb` (the X server with a memory framebuffer), `xkbcomp`, the XKB keyboard data (`/usr/share/X11/xkb`), `XErrorDB`, `protocol.txt`, an Xlib test program `xdemo` (`usr/x11/xdemo.c`) and every library they need (libX11, libxcb, pixman, libXfont2, freetype, ...), copied from the build machine like the GNU tools.

`startx` in the Terminal creates `/tmp` and `/var/lib/xkb`, sets `DISPLAY=127.0.0.1:0` and starts

    Xvfb :0 -screen 0 800x600x24 -fbdir /tmp -fp built-ins -listen tcp -nolisten unix -nolisten local -ac -xkbdir /usr/share/X11/xkb > /tmp/Xvfb.log 2>&1 &

then opens the **X Display** window (also in the start menu: Development, or System in the modern menu). Clients connect over TCP to `127.0.0.1:6000` through the kernel's loopback sockets; Unix-domain sockets are not used. `-fp built-ins` uses the fonts compiled into libXfont2, so no font files are needed.

| Piece | How it works |
|---|---|
| Framebuffer | With `-fbdir` Xvfb keeps its screen in the file `/tmp/Xvfb_screen0` (an XWD image) and maps it with `MAP_SHARED`. `lx_mmap` notices a shared mapping of a file named `Xvfb_screen*` and passes the list of physical pages to `xfb_register`. Unmapping one of them (`lx_unmap_range`) calls `xfb_drop`. |
| Display window | `xdisp_draw` reads the XWD header from the first page (byte order detected from `header_size`), finds width, height, bytes per line and the pixel offset (header plus 12 bytes per colour entry), and copies 32-bit pixels page by page into the window. `xdisp_fast` (main loop, next to `video_fast`) refreshes the window every 50 ms while it is open. |
| `epoll` | The X server's main loop uses `epoll`. `epoll_create`, `epoll_create1`, `epoll_ctl`, `epoll_wait`, `epoll_pwait` and `epoll_pwait2` are implemented on top of `lx_poll_one` (up to 64 instances, 64 descriptors each, level-triggered, `EPOLLONESHOT` honoured). |

### Keyboard and mouse (v9.26)

| Piece | How it works |
|---|---|
| Kernel queue | `xdisp_mouse` (called from the mouse handler in `graphics/ui/input.c`) and `xdisp_key` (keyboard dispatch when the X Display window has focus) push 8-byte records into a 512-entry ring in `apps/x11/xdisplay.c`: type (1 motion, 2 button, 3 key), button or character, pressed, x and y in X screen coordinates. Wheel steps become buttons 4 and 5. |
| `/dev/nvxxin` | device 16; `read` returns whole records, `poll` and `epoll` report it readable while records wait |
| `nvx-xinput` | started by `startx`; an X client (`usr/x11/nvx-xinput.c`) that reads `/dev/nvxxin` and replays the records with the XTEST extension (`XTestFakeMotionEvent`, `XTestFakeButtonEvent`, `XTestFakeKeyEvent`). Characters are turned into keysyms (Return, BackSpace, Tab, Escape and the arrow keys included); Shift is pressed around characters that need it. Log in `/tmp/nvx-xinput.log`. |

### The full-screen X session (`startx full`)

`startx full` (also `xserver`) switches the live console to the desktop state, starts the server at the display's own size (for example 1280x1024) with the classic X background (`-retro`), starts `nvx-xinput`, and runs `/root/.xinitrc` with bash if it exists, otherwise `xdemo`. While the session is shown (`g_xfull`), `xfull_tick` copies the X framebuffer to the whole screen every 40 ms through the same path `/dev/fb0` programs use (`g_fb0_open`, `g_fb0_dirty`), all mouse input goes to X (`xfull_mouse`) and all keys go to X. F12 or Ctrl+Alt+Backspace (the keyboard drivers turn them into codes 31 and 30) returns to the NoovexOS desktop; the server keeps running, and `startx full` shows it again. If the server was started in a window first, `startx full` shows that 800x600 screen in the top-left corner.

### Limits and next stages

| Limit | Next step |
|---|---|
| No window manager: X windows cannot be moved or resized with the mouse | ship a small window manager (twm-like) |
| Only characters the NoovexOS keyboard produces reach X (no separate key-up timing, no function keys other than the arrows) | forward scancodes instead of characters |
| GTK 3 programs are not in the sysroot | about 60 libraries plus GLib schemas, fonts and fontconfig: 2-4 h |
| Chromium | needs GTK, NSS, NSPR, CUPS, D-Bus, a replacement for its sandbox, many more system calls and much memory: 15-30 h in stages |
| One screen, 800x600 at 24 bits, no OpenGL | Xvfb renders in software |

## 6.20 Tkinter, Tk and IDLE

| Piece | Where it comes from |
|---|---|
| Tcl/Tk 8.6 | `tools/build_tcltk.sh` builds Tcl and Tk 8.6 from `third_party/src/tcltk-8.6.tar.xz` (cached in `build/tcltk-root`). `libtcl8.6.so` and `libtk8.6.so` go to `/lib64`, the script libraries to `/usr/lib/tcl8.6` and `/usr/lib/tk8.6`, `wish` to `/usr/bin`. |
| `_tkinter` | built by the same script from CPython 3.13's `Modules/_tkinter.c` against that Tk, into `lib-dynload` |
| `tkinter`, `idlelib` | the standard library packages, now staged (without `idlelib/idle_test`), with IDLE's `.def` configuration files and icons |
| `/usr/bin/idle3.13` | `from idlelib.pyshell import main`; `idle3` and `idle` link to it |
| `nvxwm` | a small window manager (`usr/x11/nvxwm.c`), started by `startx`: a title bar with the window name and a close button (sends `WM_DELETE_WINDOW`), drag the title bar to move, click to raise and focus, the new window gets the focus |

Tcl is built **without threads** (`--disable-threads`). The threaded Tcl notifier waits in `pthread_cond_timedwait` and only woke up on the next X event here, so IDLE showed its prompt, colours and output one keystroke late. Without threads, Tcl waits in `select` with a timeout, and `_tkinter` polls its interpreter every 20 ms, which is what CPython does with a non-threaded Tcl.

Tk is built without Xft, so it uses the X server's core fonts; the server only has its built-in fonts (`-fp built-ins`), so Tk text uses the `fixed` font.

### Starting X programs

The Terminal starts the X server by itself when a command needs it: `idle`, `idle3`, `idle3.13`, `wish`, `xdemo`, and any command line that mentions `tkinter`, `idlelib` or `turtle` (`x_needs` in `shell/commands.c`). If the server is not running, `x_launch` runs `startx`, then the command through bash after waiting for `/tmp/Xvfb_screen0` plus 2 seconds, with output in `/tmp/xapp.log`. The start menu has **IDLE (Python 3.13)** (Development in the classic menu, Productivity in the modern menu, and in search for "idle", "python" or "tkinter"), which does the same.

IDLE runs in its normal mode, with the user code in a second Python process connected over a loopback TCP socket.

## 6.21 X programs as normal desktop apps (v9.28)

`startx` (and every automatic start, for example `idle3`) now runs the X server at the size of the screen in **overlay mode** (`g_xover`, `apps/x11/xdisplay.c`): there is no X Display window, X windows are drawn straight onto the NoovexOS desktop.

| Piece | How it works |
|---|---|
| Transparency | `nvxwm` paints the X root window in the key colour `0x010203` and gives it an invisible cursor. `xover_draw` (called by the desktop just before the start menu and pop-ups are drawn) copies every X pixel that is not the key colour onto the desktop, above the taskbar line. |
| Refresh | `xover_tick` (main loop, every 60 ms) takes a sampled checksum of the X framebuffer and asks for a desktop redraw when it changed. |
| Mouse | `xover_mouse`: a press on a non-transparent X pixel goes to X and is held there until all buttons are released; motion and the wheel go to X while the pointer is over an X window. Presses on transparent pixels, the taskbar and NoovexOS windows are handled by the desktop as before. |
| Keyboard | after a press on an X window, keys go to X (`g_xkbd`) until the next press elsewhere |

`startx window` keeps the old 800x600 X Display window, `startx full` the full-screen session.

### Apps from packages in the start menu

`apps/x11/xapps.c` reads every `/usr/share/applications/*.desktop` file in the system image and installed packages (Name, Exec without `%` codes, Icon, Terminal, NoDisplay). Each appears in the start menu (Development in the classic menu, Productivity in the modern menu) and in search, with its own icon: `Icon=` is looked up as a PNG in `/usr/share/icons/hicolor/{48,64,128,32,256}x.../apps/` and `/usr/share/pixmaps/`, decoded with `png_decode` and scaled to 48x48 with its alpha channel. IDLE uses CPython's own icon (`idlelib/Icons/idle_48.png`). Launching runs the `Exec` line through `x_launch` (or the Terminal for `Terminal=true`). So `sudo apt install vlc` adds **VLC media player** with its cone icon to the menu; whether VLC itself runs is not tested (it needs Qt, which needs more of the Linux layer).

### Opening Python files

A double click on a `.py` file (Files, desktop icons, search results) opens it in IDLE (`idle3 'path'`) instead of the NoovexOS Editor.
