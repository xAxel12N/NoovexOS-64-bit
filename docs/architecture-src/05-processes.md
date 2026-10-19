# Part 5: Processes, scheduling, signals and the native system-call ABI

This part covers how NoovexOS 64 runs user programs: the process table and its life cycle, the setjmp/longjmp scheduler, signals, job control on the terminal, pipes and file descriptors, the ELF loader, the program table, and the native `int 0x80` system-call ABI. Most of the code is in `core/proc/jobs.c` (the "job control" or `jc_*` layer). `core/proc/elf.c` loads programs, `sys/native/syscall.c` holds the legacy dispatcher, and `core/proc/sched.c` holds the older demo schedulers. Each of these files is its own compilation unit; they share functions through `include/nvx/core/proc/jobs.h`, `elf.h`, `sched.h` and `include/nvx/sys/native/syscall.h`. Every path below is relative to the repository root.

There are two process systems in the tree, and they should not be confused:

| System | Table | Where | Status |
|---|---|---|---|
| Job-control processes | `struct jproc g_jp[JMAX]` (256 slots, one per thread) | `core/proc/jobs.c` | The live system. It runs the shell's programs, init, the Linux ABI and SMP scheduling. |
| Legacy staged processes | `struct proc g_proc[MAXPROC]` (6 slots) | `core/proc/sched.c`, `sys/native/syscall.c` | Older demos: a cooperative scheduler, a preemption demo, IPC, a compositor demo and threads. |

`syscall_dispatch` (`sys/native/syscall.c`) chooses between them on every `int 0x80`. If a job process is running on this CPU (`g_js_running && g_jcur>=0`), the call goes to `jc_syscall` first. A call that `jc_syscall` does not handle falls through to the legacy `switch`.

---

## 5.1 Per-CPU scheduling state

Several names in `jobs.c` that look like globals are really macros for fields of the current CPU's `struct pcpu` (`arch/x86_64/cpu/percpu.c`). The `%gs` base points at that struct, and `PCPU` is defined as `((struct pcpu*)cpu_percpu())`.

| Macro | Field | Meaning |
|---|---|---|
| `g_jcur` | `PCPU->jcur` | Index into `g_jp` of the process running on this CPU, or `-1` |
| `g_js_running` | `PCPU->js_running` | 1 while this CPU is inside a job process |
| `g_js_kern` | `PCPU->js_kern[8]` | The `k_setjmp` buffer that `jc_leave` jumps back to |
| `g_js_slice` | `PCPU->js_slice` | Tick count when the current time slice started |
| `g_dcwd`, `g_uid`, `g_gid`, `g_umask` | same names | Working directory and credentials for the current context |
| `g_ubrk`, `g_mmap_next` | `ubrk`, `mmap_next` | Heap break and next anonymous-mmap address |
| `g_jkstack` | `PCPU->jkstack[16384]` | Kernel stack used while a job process runs (`TSS.rsp0`) |
| `lsys_kstack`, `lsys_fsbase` | `kstack_top`, `fsbase` | Kernel stack for the `syscall` entry, and the FS base `jc_resume` restores |

`percpu_setup` sets the defaults: `jcur=-1`, `umask=022`, `ubrk=0x40002000000`, `mmap_next=0x40004000000`.

---

## 5.2 The process table: `struct jproc`

```c
#define JMAX 256
#define S_FREE 0
#define S_RUN 1
#define S_WAIT 2
#define S_SLEEP 3
#define S_STOP 4
#define S_ZOMB 5
#define S_TDEAD 6
struct jproc {
    int state, pid, ppid, pgid, sid, uid, gid, cwd, umask;
    u64 cr3, brk, mmnext, wake;
    regs_pt ctx;
    int fd[FDMAX]; u8 cloexec[FDMAX];
    u32 sigpend, sigmask, sigamask[32], saflags[32]; u64 sighand[32], sigrest[32];
    u64 fsbase, pollwake, lxbrk; u32 suspold; int susp, lx;
    int exitst, stopnote;
    volatile int runcpu; u64 cputicks; int lastcpu;
    u64 it_next, it_int;
    int tg, tgid, gexit, tkill;
    u64 ctid, robust, fx_addr, fx_dead; int fx_wait, fx_woken, fx_tg; u32 fx_bits;
    char cmd[48];
    u8 fx[512] __attribute__((aligned(16)));
};
struct jproc g_jp[JMAX];
int g_next_pid=2, g_js_dirty=0;
```

### Field reference

| Field | Type | Meaning, and where it is set or used |
|---|---|---|
| `state` | int | One of `S_FREE..S_TDEAD` (see 5.3) |
| `pid` | int | For a thread this is its thread ID (`gettid`). Taken from `g_next_pid++`. It starts at 2 because PID 1 is reserved for init, and `jc_start_init` writes `pid=1` after creating init |
| `ppid` | int | Parent's PID. `0` means the kernel shell, nsh. `jc_finish` changes the `ppid` of orphans to 1 (or to 0 if init is not alive) |
| `pgid` | int | Process group. `jc_create` uses the given group, or the new PID if none is given. Can change through `setpgid` (150) and `setsid` (152) |
| `sid` | int | Session. Always `1` at creation. Only session 1 owns the terminal: `jc_tty_read`, `tcgetpgrp` and `tcsetpgrp` check `sid==1` |
| `uid`, `gid`, `umask` | int | Credentials, copied from the per-CPU `g_uid/g_gid/g_umask` when the process is created or forked |
| `cwd` | int | Directory-tree node index (the `DT[]` index, same as `g_dcwd`) |
| `cr3` | u64 | Physical address of the process's PML4. User memory lives in PML4 slot 8 (`0x40000000000`-`0x7FFFFFFFFFF`, see `jc_uok`) |
| `brk`, `mmnext` | u64 | Heap break (`0x40002000000` at start) and next mmap address (`0x40004000000`). `jc_enter` copies them into `g_ubrk/g_mmap_next`, and they are written back when the process leaves the CPU |
| `wake` | u64 | Tick deadline when the state is `S_WAIT` or `S_SLEEP` |
| `ctx` | `regs_pt` | Saved user registers in the interrupt-frame layout: 15 general-purpose registers, then `vector`, `errcode`, `rip`, `cs`, `rflags`, `rsp`, `ss` (`sched.c`) |
| `fd[FDMAX]` | int[256] | Descriptor table. Each entry is an index into the global `g_vf[]`, or `-1`. Only the thread-group leader's table is used; `fdt()` returns `g_jp[g_jp[g_jcur].tg].fd` |
| `cloexec[FDMAX]` | u8[256] | Close-on-exec flag for each descriptor (leader's copy, through `fdce()`) |
| `sigpend` | u32 | Bitmap of pending signals (bit *n* = signal *n*) |
| `sigmask` | u32 | Bitmap of blocked signals. Bits 9 and 19 are always cleared |
| `sighand[32]` | u64 | `0` = `SIG_DFL`, `1` = `SIG_IGN`, anything above 1 = handler address |
| `sigrest[32]` | u64 | Restorer (trampoline) address pushed as the handler's return address |
| `sigamask[32]` | u32 | Extra mask applied while the handler runs (`sa_mask`) |
| `saflags[32]` | u32 | `sa_flags`, written only by the Linux `rt_sigaction` (`sys/linux/linux.c`). Bit 2 (`SA_SIGINFO`) makes the kernel build a siginfo block |
| `fsbase` | u64 | TLS base. `jc_enter` loads it into `lsys_fsbase`, and `jc_resume` writes it to `MSR 0xC0000100` |
| `pollwake` | u64 | Deadline for Linux `poll`/`select` (`sys/linux/linux.c`) |
| `lxbrk` | u64 | Linux-ABI program break (`sys/linux/linux.c`) |
| `susp`, `suspold` | int, u32 | `rt_sigsuspend` state: the temporary mask is active, and `suspold` holds the mask to restore |
| `lx` | int | Set when the image is static-PIE (`g_elf_pie`) or when the process has made any Linux system call (`linux_syscall` sets `p->lx=1`). After that, native `int 0x80` calls are refused (see 5.12) |
| `exitst` | int | Wait status in `wait()` encoding (see 5.4) |
| `stopnote` | int | `1` = a stop has not been reported yet, `2` = a continue has not been reported yet (for `WUNTRACED`/`WCONTINUED`) |
| `runcpu` | volatile int | `cpu_id()+1` while the process runs on a CPU, otherwise `0`. Stops two CPUs from picking the same process |
| `cputicks` | u64 | Total ticks the process has run, added up in `jc_enter` |
| `lastcpu` | int | The last CPU that ran the process |
| `it_next`, `it_int` | u64 | `ITIMER_REAL`: tick of the next SIGALRM (0 = off) and the reload interval in ticks (section 5.16). Kept in the leader |
| `tg` | int | Slot index of the thread-group leader. A process that never created a thread has `tg` equal to its own slot. `jc_lead(p)` returns `&g_jp[p->tg]` |
| `tgid` | int | PID of the process (the leader's `pid`). `getpid` returns it |
| `gexit` | int | Set in the leader while the whole group is exiting (exit_group, fatal signal); `exitst` then holds the status the last thread reports |
| `tkill` | int | Set on the other threads by `execve`; the pending SIGKILL then ends only that thread |
| `ctid` | u64 | `CLONE_CHILD_CLEARTID` / `set_tid_address` address: zeroed and futex-woken when the thread ends |
| `robust` | u64 | Head registered with `set_robust_list` (stored, not walked) |
| `fx_wait`, `fx_woken`, `fx_tg`, `fx_addr`, `fx_bits`, `fx_dead` | | Futex wait state (section 5.15): waiting flag, wake flag, group and address of the futex word, bitset, timeout tick |
| `cmd[48]` | char | Command line, shown by `ps` and in the jobs list |
| `fx[512]` | u8 | FXSAVE area. `jc_fx_init` sets FCW to `0x037F` and MXCSR to `0x1F80` (mask `0xFFFF`) |

`jc_idx(pid)` finds the slot for a PID among slots that are not free. `jc_alloc()` returns the first `S_FREE` slot.

---

## 5.3 Process states

| State | `ps` STAT | Meaning |
|---|---|---|
| `S_FREE` (0) | - | Slot is unused |
| `S_RUN` (1) | `R` | Ready to run, or running (`runcpu!=0`) |
| `S_WAIT` (2) | `S` | Blocked in a restartable call (read on a pipe or tty, waitpid). Retried on every tick |
| `S_SLEEP` (3) | `S` | Timed sleep or `pause`. Wakes at `wake`, or earlier when a signal can be delivered |
| `S_STOP` (4) | `T` | Stopped by SIGSTOP, SIGTSTP, SIGTTIN or SIGTTOU. `jc_launch` also creates pipeline members in this state while it wires them together |
| `S_ZOMB` (5) | `Z` | Has exited and is waiting to be reaped. Address space and descriptors are already freed |
| `S_TDEAD` (6) | `S` | A thread-group leader whose thread has exited while other threads still run. It is never scheduled, but it keeps the shared state (descriptors, signal handlers, brk, timers) and the process ID until the last thread exits, and then becomes `S_ZOMB` |

`ps` adds `+` after the state when `pgid==g_tty_fg` (`jc_builtin`).

```
            jc_create / jc_fork
                    |
                    v
   +---------->  S_RUN  <------------- SIGCONT (jc_signal_proc) / SIGKILL
   |             |  |  |                                      ^
   |  wake<=tick |  |  | stop signal, default action          |
   |  or signal  |  |  +------------------------------>  S_STOP
   |             |  | jc_block(S_WAIT/S_SLEEP)
   |             v  v
   +---- S_WAIT / S_SLEEP
                 |
   exit (0), fatal signal, fault --> jc_finish --> S_ZOMB --> waitpid / jc_poll --> S_FREE
```

---

## 5.4 Life cycle: create, build, fork, exec, finish, wait

### `jc_build(args, &entry, &rsp, &argc, &argv)`
1. Takes the first word of `args` as the program name and calls `jc_find_prog` (5.11).
2. Copies `args` into `g_exec_args`. That string becomes `argv` in `elf_build_space`.
3. Calls `elf_build_space`, which returns a new CR3 (5.10).
4. Maps the rest of the 16-page stack lazily: pages `USTK_V - p*0x1000` for `p=1..15` (`JS_STK_PAGES=16`) get `vm_map_lazy`, unless a page is already mapped.
5. Returns `rsp = g_arg_rsp`, plus `argc` and `argv`.

### `jc_create(args, ppid, pgid)`
Allocates a slot, builds the image, and zeroes the whole `jproc`. It then sets `pid`, `ppid`, `pgid` (the new PID if `pgid` is 0), `sid=1`, credentials, `cwd`, `brk=0x40002000000`, `mmnext=0x40004000000` and `lx=g_elf_pie`. `jc_ctx_fresh` builds the user context:

```c
c->rip=entry; c->cs=0x1B; c->rflags=0x202; c->rsp=rsp; c->ss=0x23; c->rdi=argc; c->rsi=argv;
```

All descriptors start as `-1`. Descriptors 0, 1 and 2 then point at the shared console vfile from `jc_con()`, which is `dev=5`, `O_RDWR`, and never freed. The state becomes `S_RUN`. It returns the slot index, `-1` if the table is full, or `-2` if the build failed.

### `jc_fork(r)` (syscall 2)
- `vm_clone_user` copies the parent's page tables for slot 8 using copy-on-write. Writable pages lose `RW` and gain `PTE_COW` in both the parent and the child, and `pframe_ref` adds a reference to each frame. Pages that are not present but are in use are recorded as `PTE_LAZY`. Frames outside `[PF_BASE, PF_TOP)` are copied immediately. If any parent entry changed, the TLB is flushed by reloading CR3.
- The child starts as a copy of the parent's `jproc` (`*c=*par`) with an FPU snapshot. It gets a new PID, `ppid`, `cr3` and credentials from the per-CPU globals, and `brk/mmnext` from `g_ubrk/g_mmap_next`. `sigpend`, `exitst`, `stopnote`, `wake`, `runcpu` and `cputicks` are cleared.
- The reference count of every open vfile goes up by one (`vf_ref`).
- `jc_save_sys` copies the `int 0x80` frame into `c->ctx`, and `c->ctx.rax=0`, so the child sees fork return 0. The parent gets the child's PID.

### `jc_exec(r, args)` (syscalls 4 and 157)
Builds the new image first. If that fails, it returns `-1` in `rax` and the old image keeps running. If it succeeds:
- switches to the kernel CR3, calls `vm_free_user` on the old space and installs the new `cr3`
- closes each descriptor that has `cloexec` set
- resets caught handlers (`sighand>1`) to `SIG_DFL`. Ignored signals stay ignored. All `saflags` are cleared
- clears `fsbase`, `lxbrk`, `pollwake` and `susp`, and sets `lx=g_elf_pie`
- resets the heap and mmap pointers, makes a fresh context, records the command name, and reinitializes the FPU
- does `k_longjmp(g_js_kern,1)` straight back to the scheduler. The process stays `S_RUN` and starts at the new entry point the next time it is picked.

Syscall 157 builds the command line from `path` plus `argv[1..7]`, joined by spaces.

### `jc_finish(i, status)` and zombies
```c
jc_close_all(p);                          /* vf_unref every fd */
if (current CR3 == p->cr3) cpu_set_pt(kcr3);
vm_free_user(p->cr3); p->cr3=0;
p->exitst=status; p->state=S_ZOMB;
/* reparent children to init (1) if init is alive and we're not init, else to 0 */
jc_wake_parent(p->ppid);                  /* clear parent's wake if S_WAIT, send SIGCHLD */
```
`vm_free_user` walks PML4 slot 8 and frees every page frame, page table, page directory and the PDPT, and then the PML4 itself.

Status encoding (Linux `wait` layout):

| Event | `exitst` | Source |
|---|---|---|
| `exit(code)` | `(code & 0xFF) << 8` | syscall 0 |
| Killed by signal *s* | `s` (low 7 bits) | `jc_deliver`, `jc_fault` |
| Stopped by *s* | `(s << 8) \| 0x7F` | `jc_stop` |
| Continued (reported by `WCONTINUED`) | `0xFFFF` | `jc_waitpid` |

### `jc_waitpid(r, pid, statusp, opts)` (syscalls 6 and 156)
Matches children (`ppid == me->pid`) using POSIX rules: `pid>0` is that child, `0` is the caller's group, `-1` is any child, and `<-1` is group `-pid`.
- A zombie that matches has its status stored and its slot set to `S_FREE`, and its PID is returned. This is the reaping step.
- With `opts&2` (`WUNTRACED`) and `stopnote==1`, the stop status is reported once.
- With `opts&8` (`WCONTINUED`) and `stopnote==2`, `0xFFFF` is reported once.
- If no child matches, it returns `-1`. With `opts&1` (`WNOHANG`) and nothing to report, it returns `0`.
- Otherwise `jc_block(r,S_WAIT,g_ticks+1)` blocks the caller. The syscall instruction runs again when the process is next scheduled (5.6).

`jc_poll` reaps zombies that nobody else will: those with `ppid==0` (children of the kernel shell), and those with `ppid==1` when init is dead. It remembers the last fatal signal, other than SIGINT and SIGPIPE, so it can print a message such as "Segmentation fault".

---

## 5.5 The setjmp/longjmp scheduling model

Job processes are not switched from one to another directly. Each CPU runs a scheduler loop on its own kernel stack. That loop enters a process with `jc_enter`, and the process leaves by `k_longjmp`ing back to the `k_setjmp` point inside `jc_enter`. Every way of leaving (blocking, exit, preemption, fault or exec) goes through `jc_leave`:

```c
static void jc_leave(void){ if(g_jcur>=0)cpu_fpu_save(g_jp[g_jcur].fx); cpu_set_pt(g_kern_cr3); k_longjmp(g_js_kern,1); }
```

`k_setjmp`/`k_longjmp` (`arch/x86_64/boot/userspace.S`) save and restore `rbx, rbp, r12-r15`, `rsp` and the return address. The buffer holds 8 qwords.

### `jc_enter(i)`, step by step
1. If the process is `S_SLEEP` and its deadline has not passed (so a signal woke it), set `ctx.rax = -4` (EINTR). A process in `S_WAIT` or `S_SLEEP` becomes `S_RUN`.
2. Save the kernel's CR3 in `g_kern_cr3`.
3. If any signal can be delivered, run `jc_deliver(i)`. Return if the process stopped or died.
4. Restore a `susp` mask if one is set. Call `us_init()`.
5. Save the per-CPU `g_dcwd/g_uid/g_gid/g_umask/g_ubrk/g_mmap_next` in `volatile` locals, then load the process's values.
6. Set `g_jcur=i`, `TSS.rsp0 = top of g_jkstack`, `lsys_kstack`, `lsys_fsbase = p->fsbase`, `g_js_slice = jc_now()`, `runcpu = cpu_id()+1` and `lastcpu`, and increment `PCPU->switches`.
7. `cli`, then `if(k_setjmp(g_js_kern)==0)`: set `g_js_running=1`, restore the FPU, load `p->cr3`, release the big kernel lock with `bd=bkl_drop()`, and call `jc_resume(&p->ctx)`.
8. Return path (after `k_longjmp`): take the lock again with `bkl_take(bd)`, set `g_js_running=0` and `runcpu=0`, add the elapsed ticks to `cputicks`, and reload the kernel CR3. If the process is still alive, copy the per-CPU cwd, credentials, brk and mmnext back into it. Restore the kernel's own values, set `g_jcur=-1`, and run `sti`.

`jc_resume` (`userspace.S`) loads the user data segments (`0x23`) and writes `%gs:24` (the per-CPU `fsbase`) to `IA32_FS_BASE`. It then points `rsp` at the `regs_pt`, pops the 15 general-purpose registers, skips `vector/errcode`, runs `swapgs` and `iretq`.

### Picking and running
```c
static int jc_runnable(int i){
    if(p->runcpu)return 0;                         /* already on some CPU */
    if(p->state==S_RUN)return 1;
    if((p->state==S_WAIT||p->state==S_SLEEP)&&(p->wake<=g_ticks||jc_deliverable(p))){
        if(p->state==S_SLEEP&&!jc_deliverable(p))p->ctx.rax=0; return 1; }
    if(p->state==S_STOP&&(p->sigpend&(1u<<9)))return 1;   /* SIGKILL reaches stopped procs */
    return 0;
}
```
- `jc_pick()` is a round-robin search that starts after `g_js_rr` and returns the first slot that is neither free nor a zombie and is runnable.
- `jc_run()` calls `jc_pick`/`jc_enter` up to `JMAX*2` times, and stops early once 2 ticks have passed or nothing is runnable.
- `jc_step()` is `jc_run()`, then `jc_poll()`, and returns whether anything ran or `g_js_dirty` was set. `core/init/main.c` calls `jc_step()` from the main loop in `ST_DESKTOP` and `ST_LIVE`. When `!jc_any_runnable()`, the BSP halts instead.
- On application processors, `ap_loop` (`arch/x86_64/smp/smp_sched.c`) takes the lock, calls `jc_pick()` and `jc_enter()` while `g_smp_sched` is set, and releases the lock.

### Time slices and preemption
- The PIT runs at 100 Hz (`pit_init(100)` in `core/init/main.c`), so one tick is 10 ms.
- BSP: IRQ0 in `isr_body` calls `jc_timer(r)` when the interrupt came from ring 3. `jc_timer` does nothing for 2 ticks after `g_js_slice`. After that it copies the frame into `ctx`, sends EOI and calls `jc_leave()`. The slice is therefore about 20 ms.
- APs: `ap_timer` counts per-CPU `ticks` (the LAPIC timer at `lapic_hz/100`) and calls `jc_preempt(r)` after 2 ticks. `IPI_RESCHED` (0xF0) also calls `jc_preempt` when it lands in user mode. `jc_signal_proc` sends this IPI with `smp_kick` when the target process is running on another CPU.
- `jc_now()` returns `g_ticks` on CPU 0 and `PCPU->ticks` on the APs.

### Faults
`isr_body` sends CPU exceptions from ring 3 to `jc_fault` while `g_js_running` is set. Before that, page faults go through `vm_user_pf`, which resolves lazy and COW pages through `vm_fault_user`. If that returns `-2`, the process is killed with status 9. `jc_fault` turns the vector into a signal:

| Vector | Signal |
|---|---|
| 0 (#DE), 16 (#MF), 19 (#XM) | 8 SIGFPE |
| 6 (#UD) | 4 SIGILL |
| 1 (#DB), 3 (#BP) | 5 SIGTRAP |
| anything else (#GP, #PF, ...) | 11 SIGSEGV |

If the process has no handler (`sighand<=1`) or the signal is blocked, `jc_finish(g_jcur, s)` ends it at once. Otherwise the signal is marked pending and delivered on the next entry. Either way, `jc_leave()` follows.

---

## 5.6 Blocking and restartable system calls

A driver or VFS read or write that cannot finish returns `JS_BLOCK` (`-1000`, defined in `arch/x86_64/interrupts/interrupts.c`). The syscall code then calls:

```c
static void jc_block(struct sysregs* r,int state,u64 wake){
    struct jproc* p=&g_jp[g_jcur]; jc_save_sys(r,&p->ctx);
    if(state==S_WAIT)p->ctx.rip-=2;          /* re-execute "int $0x80" (CD 80) / "syscall" (0F 05) */
    p->state=state; p->wake=wake; jc_leave();
}
```

- `S_WAIT` is a polling retry. The saved `rip` is moved back by the length of the 2-byte trap instruction, and `wake=g_ticks+1`. On the next tick the process is runnable again, and when it resumes it runs the same system call with the same registers. No per-object wait queues are needed: a pipe read simply returns `JS_BLOCK` again until data arrives. `jc_wake_parent` sets `wake=0` so a waiting parent rechecks at once.
- `S_SLEEP` does not rewind. The call completes when the process resumes. If the deadline passes, `jc_runnable` sets `rax=0`. If a signal wakes it early, `jc_enter` sets `rax=-4`. `pause` (158) sleeps with `wake = g_ticks+0x7FFFFFFFFFFF`.
- `jc_save_sys` copies the `struct sysregs` frame (15 registers, then the hardware iret frame at `fr[15..19]`) into `regs_pt`.
- `jc_yield_sys(r)` saves the context without changing the state, and leaves. Syscall 20 uses it, as do the paths that deliver a pending signal after a syscall.

The Linux ABI uses the same mechanism. `sys/linux/linux.c` calls `jc_block(r,S_WAIT,g_ticks+1)` for blocking I/O and poll/select, and `S_SLEEP` for nanosleep, pause and `rt_sigsuspend`. A futex wait (section 5.15) is the one `S_WAIT` that does not poll: it blocks with `wake` at its timeout (or far in the future), and `FUTEX_WAKE` sets `wake=0` on the threads it wakes, so they run at once without retrying every tick.

---

## 5.7 Signals

Signal numbers follow Linux x86-64: 2 INT, 9 KILL, 11 SEGV, 13 PIPE, 17 CHLD, 18 CONT, 19 STOP, 20 TSTP, 21 TTIN, 22 TTOU, 31 SYS. Valid numbers are 1-31.

### Default actions (`sig_default`)

| Return | Action | Signals |
|---|---|---|
| 0 | ignore | 17 CHLD, 23 URG, 28 WINCH |
| 3 | continue | 18 CONT |
| 2 | stop | 19 STOP, 20 TSTP, 21 TTIN, 22 TTOU |
| 1 | terminate | all others |

### Sending: `jc_signal_proc(i, sig)`
- Ignored when the target is free or a zombie, or when `sig` is outside 1-31.
- SIGCONT: a stopped process becomes `S_RUN` with `stopnote=2`, and its parent is notified. Pending STOP, TSTP, TTIN and TTOU are cleared. CONT is only queued if the process has a handler for it.
- A stop signal clears any pending SIGCONT.
- Signals that are ignored (`SIG_IGN`, or `SIG_DFL` with an "ignore" default) are dropped when sent. SIGKILL and SIGSTOP are never dropped.
- Sets `sigpend |= 1<<sig`. SIGKILL also moves a stopped process back to `S_RUN`.
- If the target is running on another CPU (`runcpu-1 != cpu_id()`), `smp_kick` interrupts that CPU.

`jc_kill(pid, sig, self)` applies the POSIX target rules (`>0`, `0`, `-1` meaning all except `self`, and `<-1`). When `g_kill_check` is set, a non-root caller cannot signal processes owned by other users; these are counted in `g_kill_denied`. It returns 0 if any process matched. `sig==0` only checks that the target exists.

### Delivery: `jc_deliver(i)`
`jc_deliverable(p) = (sigpend & ~sigmask) | (sigpend & (SIGKILL|SIGSTOP))`. The loop runs at most 32 times and always takes the lowest pending signal first:

1. SIGKILL: `jc_finish(i,9)`. SIGSTOP: `jc_stop(i,19)`.
2. `SIG_IGN`: skip to the next signal.
3. `SIG_DFL`: ignore or continue (skip), terminate (`jc_finish(i,s)`), or stop (`jc_stop(i,s)`).
4. Handler: a process in `S_WAIT` or `S_SLEEP` becomes `S_RUN`, then `jc_push_handler`. If that fails, the process is killed with SIGSEGV (11). Only one handler is set up per entry.

Delivery happens in `jc_enter` before the process resumes, and after any system call that leaves a signal deliverable (`syscall_dispatch` checks `jc_sig_pending_cur()` and then calls `jc_yield_sys`). `kill` (17) and `sigprocmask` (142) also yield at once when they make a signal deliverable to the caller.

### Handler frame: `jc_push_handler`
```
 high   old user rsp (top)
        [128 bytes red zone skipped]
        [128 bytes siginfo, only if SA_SIGINFO]   <- info; info[0]=signo
        saved regs_pt (whole interrupted context)  <- frame (16-byte aligned)
        u64 saved mask (suspold if in sigsuspend)
 low    u64 return address = sigrest[s]             <- new rsp
```
The code checks that the 1 KB below `rsp` and the handler address are in the user window (`jc_uok`). It then switches to the process's CR3 and checks that the frame is mapped and writable (`jc_umapped`, which can also resolve COW and lazy faults). After writing the frame, it sets `rip=sighand[s]`, `rsp=sp`, `rdi=signo`, `rsi=info or 0` and `rdx=rax=0`, and `sigmask |= (1<<s) | sigamask[s]`. KILL and STOP can never be masked.

### Returning: `sigreturn` (143)
The user library installs a trampoline (`usr/libc/src/libc.c`, and also `legacy_posix.h`):
```asm
__nvx_sigtramp: mov %rsp,%rbx ; mov $143,%eax ; int $0x80 ; hlt
```
When the handler runs `ret`, it pops `sigrest` and lands on the trampoline with `rsp` pointing at the saved frame. Syscall 143 checks that the frame is mapped and that the saved `rip` is in the user window; if either check fails, the process gets SIGSEGV. It then forces `cs=0x1B` and `ss=0x23`, keeps only safe RFLAGS bits (`(rflags&0xCD5)|0x202`), and restores `ctx` and `sigmask`. Finally it calls `jc_leave()`, so the restored context is resumed through the scheduler. The libc `sigaction` passes the handler, `__nvx_sigtramp` and `sa_mask` to syscall 141.

### Stop and continue
`jc_stop(i,sig)` sets the state to `S_STOP`, `exitst=(sig<<8)|0x7F` and `stopnote=1`, then wakes the parent and sends it SIGCHLD. A stopped process is not runnable unless SIGKILL is pending. SIGCONT is handled entirely when it is sent (see above).

---

## 5.8 Job control and the terminal

### Terminal ownership
- `g_tty_fg` is the process group in the foreground. `0` means the kernel shell owns the terminal.
- Input goes through two buffers: the edit line `g_ted[256]` (`g_ted_n`) and the cooked input queue `g_tin[1024]` (`g_tin_n`, plus the `g_tin_eof` flag).
- `live_input` (`shell/commands.c`) maps Ctrl+C, Z, D and `\` to the codes 3, 26, 4 and 28. While `g_tty_fg` is set, every key goes to `tty_key`.

`tty_key(c)`:

| Key | Action |
|---|---|
| Ctrl-C (3) | Echoes `^C` and sends SIGINT (2) to the group `-g_tty_fg`. If a program-to-builtin collector is running, it is cancelled |
| Ctrl-Z (26) | Echoes `^Z` and sends SIGTSTP (20) to the foreground group |
| Ctrl-\ (28) | Echoes `^\` and sends SIGQUIT (3) to the foreground group |
| Ctrl-D (4) | On an empty edit line, sets `g_tin_eof`. Otherwise sends the partial line without a newline |
| Backspace | Deletes from `g_ted` |
| Enter | Moves `g_ted` plus `'\n'` into `g_tin` |
| Printable | Added to `g_ted` and echoed |

`jc_tty_read(buf,len)`, used by `vfs_read` on `dev==5`:
- returns `-1` if the caller is not in session 1
- if the caller's group is not in the foreground, sends SIGTTIN (21) to it and returns `JS_BLOCK`
- returns at most one line from `g_tin`. A pending EOF returns 0 once. Otherwise it returns `JS_BLOCK`.

`jc_tty_out(s,n)`, used by `vfs_write` on `dev==5` and by syscalls 1 and 3:
- prints white text on the kernel terminal
- turns UTF-8 box-drawing characters, typographic quotes and dashes, `...` and NBSP into ASCII (`jc_u8_ascii`)
- removes ANSI escape sequences (ESC `[` ... final byte)
- drops `\r`, turns `\t` into two spaces, and drops other control characters.

### Launching from the shell: `jc_launch(line)`
`run_cmd` in `shell/commands.c` calls `jc_launch` first, unless output is being captured. The steps:

1. A trailing `&` makes the job a background job (`>&` does not count).
2. The line is split on `|`, respecting quotes, into up to 6 stages. A stage is a *program* when its first token passes `jc_is_prog`. If no stage is a program, `jc_launch` returns 0 and the shell runs its built-in pipeline.
3. Program stages must be contiguous. If built-in stages come first, they run through `run_cmd` with `g_capture=1`, and their output fills a pipe (sized to the output) that becomes the first program's stdin. If built-in stages come after the programs (the *tail*), the last program's stdout goes into a collector pipe `g_coll_vi`. `jc_coll_drain` copies that pipe into `g_coll_buf` (up to `COLL_CAP` = 64 KB). When the job ends, `jc_coll_finish` runs the remaining built-ins through `run_cmd(g_coll_rest)` with `g_run_feed` pointing at the collected text. A tail cannot be combined with `&`, and only one collector can run at a time.
4. For each program stage, the tokens are scanned for redirections: `< f`, `> f`, `>> f`, `>f`, `2> f`, `2>> f` and `2>&1`. Spaces inside a quoted argument are stored as `0x1F`, and `elf_args_from_string` turns them back into spaces.
5. `jc_create(args, 0, pgid)` creates each process. Its parent is the kernel shell (`ppid 0`), and the first process's PID becomes the group ID. Each process starts in `S_STOP` so it cannot run before it is fully wired. The previous stage's pipe read end becomes its stdin, and a new 4 KB pipe's write end becomes its stdout. `jc_openinto` opens redirections in the context of the new process and moves them onto descriptor 0, 1 or 2. `2>&1` makes descriptor 2 share descriptor 1's vfile.
6. If starting any stage fails, the stages already created get SIGKILL.
7. All stages are set to `S_RUN`, and a job entry is created. Foreground: `g_tty_fg=pgid` and the input buffers are cleared. Background: prints `[id] lastpid`.

### The job table: `g_jobs2`
```c
#define JOBMAX 16
struct job { int used, id, pgid, state, bg, notified_stop; char cmd[64]; };
```
`state` is 1 for running and 2 for stopped. `jc_job_new` uses the smallest free job ID. On each step, `jc_poll`:
- if no process in the group is alive: for the foreground job, gives the terminal back (`g_tty_fg=0`), prints the name of the fatal signal if there was one, finishes the collector and shows the prompt again. For a background job, prints `[n]+  Done  cmd`. The entry is then freed.
- if every live process in the group is stopped: marks the job stopped, gives the terminal back if it was in the foreground, and prints `[n]+  Stopped  cmd`.

### Builtins (`jc_builtin`)

| Command | Behaviour |
|---|---|
| `jobs` | Lists `[id]  Running/Stopped  cmd`, or "no jobs" |
| `fg [%n]` / `bg [%n]` | Picks job *n*, or the highest ID if none is given (`jc_jobarg`). Sends SIGCONT to `-pgid`. `fg` also gives that group the terminal |
| `kill [-SIG\|-n] pid\|-pgid\|%job` | Default signal is 15. Accepts names 1-22 (`HUP`...`TTOU`) with or without the `SIG` prefix. Permissions are checked |
| `ps` | Columns: PID, PPID, PGID, STAT, USER, COMMAND. Always shows a fake PID 0 row for "nsh (kernel shell)" |
| `umask [ooo]` | Shows or sets `g_umask` |
| `wait` | Only reports how many jobs are still running |

`dt_busy(d)` (also in `jobs.c`) stops a directory from being removed while it is the working directory of any live process. For a process running on another CPU, it reads that CPU's `dcwd`.

---

## 5.9 File descriptors and pipes

### Descriptor tables
- `FDMAX=256` per process and `VMAXOPEN=1024` system-wide (`fs/vfs/ramfs.c`; 32 and 32 before v8.97). Open files are `struct vfile g_vf[VMAXOPEN]`, shared by all processes, with a reference count `ref`. The threads of a process share the leader's table.
- `fdt()` returns the descriptor table of the running process (the thread-group leader's `fd[]`, so all threads share it) while a job process runs, and the kernel shell's `g_fd` otherwise. `fdce()` does the same for the cloexec arrays (`g_kcloexec` for the kernel).
- `vf_ref`/`vf_unref` manage the count. When it reaches zero: a pipe end decrements its pipe's `readers` or `writers`, and the pipe buffer is freed once both are zero. The console vfile is never freed (its `ref` is reset to 1). Framebuffer and mouse open counts go down. A dirty file is written back (`fs_wb_mark`), and an orphaned inode is released (`fs_orphan_close`).
- `vfile.dev` codes used in this part: `0` regular file, `5` console tty, `6` pipe read end, `7` pipe write end, `9` `/dev/fb0`, `10` mouse.
- Inheritance: fork increments every reference. Exec closes descriptors marked cloexec. `dup2` (144), `F_DUPFD` and `pipe` clear the flag on the new descriptor. `fcntl` (146): `cmd 0` duplicates to the lowest free descriptor `>= rdx`, `1` reads the cloexec flag, `2` sets it (`rdx&1`), `3` returns the `flags`, and any other value returns 0.

### Pipes
```c
#define PIPE_MAX 128
struct jpipe { int used, r, w, n, cap, readers, writers; char* buf; };
```
`jc_pipe_new(cap,&rvi,&wvi)` allocates a `kmalloc` ring buffer and two vfiles (`dev 6` with `O_RDONLY`, and `dev 7` with `O_WRONLY`), with `fs_idx` pointing at the pipe.

| Operation | Condition | Result |
|---|---|---|
| `jc_pipe_read` | `n==0`, writers > 0 | `JS_BLOCK`, so the caller blocks in `S_WAIT` |
| | `n==0`, no writers | `0` (EOF) |
| | data present | Copies `min(len, n)` bytes |
| `jc_pipe_write` | no readers | SIGPIPE (13) to the caller, returns `-1` |
| | buffer full | `JS_BLOCK` |
| | otherwise | Copies as much as fits (a short write is possible) |

Syscall 145 `pipe(int fds[2])` creates a 4096-byte pipe and puts the read and write ends in the two lowest free descriptors.

---

## 5.10 The ELF loader (`core/proc/elf.c`)

### Address-space constants

| Constant | Value | Meaning |
|---|---|---|
| `ELF_IMG_LO` | `0x40000000000` | Lowest address for a loadable segment |
| `ELF_IMG_TOP` | `0x40002000000` | Highest end address for a segment. The heap starts here |
| `ELF_PIE_BASE` | `0x40000400000` | Load bias for `ET_DYN` (static-PIE) |
| `USTK_V` | `0x40000100000` | Top stack page. The initial stack ends at `USTK_V+0x1000` |
| `ELF_STK_LO` | `USTK_V - 15*0x1000` | Bottom of the 16-page stack area. Segments may not overlap it |
| `XA_MAX` / `XA_CAP` | 32 / 2048 | Maximum argv or envp entries, and bytes of strings |

### `elf_build_space(elf, len, &entry)`
1. Header checks: magic `\x7FELF`, `EI_CLASS=2` (64-bit), `EI_DATA=1` (little-endian), `e_type` 2 (`ET_EXEC`) or 3 (`ET_DYN`), `e_machine=62` (x86-64). `elf_ph_ok` requires `phentsize>=56`, `1<=phnum<=64`, and a program-header table that fits inside the file.
2. Static and static-PIE: for `ET_DYN`, `base=ELF_PIE_BASE` and `g_elf_pie=1`. No dynamic linking is done. Programs must be static.
3. Segment checks for each `PT_LOAD`: `filesz<=memsz`, the file range is inside the image, the virtual range is inside `[ELF_IMG_LO, ELF_IMG_TOP)`, and the segment does not overlap the stack area. The entry point must be inside a `PT_LOAD` that has `PF_X`. `PT_PHDR` (type 6) sets `phdr_va`. Without it, the address is computed from the segment at file offset 0 plus `e_phoff`.
4. Mapping: a new PML4 from `vm_new_pml4()`. Every page of every segment gets a frame mapped with flags `0x7` (present, RW, user). Bytes are copied from the file up to `filesz`, and the rest is zero (BSS). Pages shared by two segments reuse the frame already mapped.
5. Stack: two pages are mapped right away (`USTK_V` and `USTK_V-0x1000`). The other 14 are mapped lazily by `jc_build`.
6. SysV initial stack, built in the 8 KB `g_stkimg` and copied into the two stack frames:

```
USTK_V+0x1000 -+  16 random bytes (xorshift over TSC)            <- AT_RANDOM
               |  envp strings ("NAME=value\0" ...)
               |  argv strings
               |  (16-byte alignment)
               |  auxv pairs ... AT_NULL
               |  NULL
               |  envp[0..n-1]
               |  NULL
               |  argv[0..argc-1]               <- g_arg_argv = rsp+8
     rsp ------+  argc                          <- g_arg_rsp
```

Auxiliary vector (`aux[]`):

| Tag | Name | Value |
|---|---|---|
| 3 | `AT_PHDR` | `phdr_va` |
| 4 | `AT_PHENT` | `e_phentsize` |
| 5 | `AT_PHNUM` | `e_phnum` |
| 6 | `AT_PAGESZ` | 4096 |
| 7 | `AT_BASE` | 0 (no interpreter) |
| 8 | `AT_FLAGS` | 0 |
| 9 | `AT_ENTRY` | `e_entry + base` |
| 11 / 12 | `AT_UID` / `AT_EUID` | `g_uid` |
| 13 / 14 | `AT_GID` / `AT_EGID` | `g_gid` |
| 16 | `AT_HWCAP` | 0 |
| 17 | `AT_CLKTCK` | 100 |
| 23 | `AT_SECURE` | 0 |
| 25 | `AT_RANDOM` | address of the 16 random bytes |
| 31 | `AT_EXECFN` | `argv[0]` |
| 0 | `AT_NULL` | 0 |

argv and env sources: unless a caller has already filled `g_xa_*` (`g_xa_set`, which the Linux `execve` does), `elf_args_from_string(g_exec_args)` splits the command line on spaces. Unless `g_xe_set` is set, `env_pack` fills the environment from the kernel's `g_env[24]`. `env_init` sets the defaults: `PATH=/bin`, `HOME=/home`, `PWD=/home`, `USER=root`, `SHELL=/bin/nsh`, `TERM=noovex` and `XZ_DEFAULTS=--memlimit-compress=20MiB` (`sys/native/syscall.c`). The native context also passes `argc` in `rdi` and `argv` in `rsi` (`jc_ctx_fresh`), so the native crt0 can use either the registers or the stack.

### Other loaders in `elf.c`
- `elf_load_run(elf,len)`: a single-run path with no scheduler. It calls `k_setjmp(g_kjmp)`, loads the new CR3 and calls `enter_ring3_args`. Legacy exit (syscall 0 with no scheduler) does `k_longjmp(g_kjmp,1)` to come back. `sys_exec` uses it.
- `nvx_dlload`/`nvx_dlcall`: a relocating loader for kernel-side modules. It copies `PT_LOAD` segments into DMA memory and applies `R_X86_64_RELATIVE`, `64`, `GLOB_DAT` and `JUMP_SLOT` relocations. Symbols resolve against `NVX_KSYMS` (`nvx_puts`, `nvx_putc`, `nvx_ticks`, `nvx_alloc`). It is not used for user processes.

---

## 5.11 Embedded programs: `JS_BIN` and `jc_find_prog`

User programs are linked into the kernel as `_binary_user_<name>_elf_start/_end` blobs. `JS_BIN[]` (`jobs.c`) maps names to them:

| Name(s) | Blob |
|---|---|
| `count`, `spin`, `sigdemo`, `upper`, `greet`, `pipetest` | their own demo ELFs |
| `ls cat grep wc head tail echo mkdir rmdir rm mv cp touch ln chmod chown stat pwd hostname whoami id uname date sleep mount umount readlink true false tee sort nvxbox cut tr uniq seq basename dirname rev tac nl fold paste comm cmp md5sum sha256sum base64 find yes printenv` | all `nvxbox` (the multi-call binary, defined with the `NVXB()` macro) |
| `libc8`, `mhello`, `musltest`, `smptest`, `sqlite3` | their own ELFs |
| `gzip gunzip zcat` / `bzip2 bunzip2 bzcat` / `xz unxz xzcat lzma unlzma lzcat` / `zstd unzstd zstdcat` | `gzip`, `bzip2`, `xz` and `zstd` blobs |
| `+init` | `init` (the leading `+` keeps it out of `/bin` listings) |

Lookup order in `jc_find_prog(name,&len)`:
1. Remove a `/bin/`, `/usr/bin/` or `/sbin/` prefix. `/sbin/init` becomes `+init`. Remove a `.elf` suffix.
2. A base name of `init` (case-insensitive) always resolves to the `+init` entry. The condition ends in `||1`, so this always applies.
3. Compare case-insensitively (`jc_ieq`) with every `JS_BIN` name.
4. Look up the path in the Unix file system (`ux_lookup`). If the file has ELF magic and is at least 64 bytes, it is used, but a non-root user also needs an execute bit (`mode & 0111`).
5. Try `fat_read_file(name, g_spawnbuf, 64 KB)` for an ELF on the FAT volume.

`jc_is_prog` is how `jc_launch` decides whether a stage is a program. `fs/vfs/unixfs.c` (`k_readdir_extra`) lists the `JS_BIN` names in `/bin` and `init` in `/sbin`, and `ux_stat2` reports them as mode `0100755`.

---

## 5.12 The native `int 0x80` system-call ABI

Entry: `idt_set128` installs vector `0x80` with DPL 3 (`attr=0xEE`). `isr128` (`userspace.S`) runs `swapgs` if the call came from ring 3, pushes `rax...r15` (a `struct sysregs`: `r15,...,rbx,rax` from low to high addresses), and calls `isr128_c`, which takes the big kernel lock, counts `PCPU->syscalls`, and calls `syscall_dispatch`.

Convention (`usr/src/user.h`, `nvx_sys`): the number goes in `rax` and the arguments in `rbx, rcx, rdx, rsi, rdi`. The result comes back in `rax`. Errors are `-1` unless noted otherwise; the native layer has no errno.

Guards in `jc_syscall` run before its main switch:
- If `p->lx` is set (a static-PIE image, or a process that has used the Linux ABI), the kernel sends SIGSYS (31) and returns `-38`.
- 1, 4, 5: `rbx` must be a mapped string of at most 4 KB.
- 157: the path, and up to 8 `argv` pointers with their strings, must be mapped.
- 31, 32: `0 <= rdx <= 16 MB`, and the buffer `[rcx, rcx+rdx)` must be mapped. `jc_umapped` resolves COW and lazy pages as it checks.
- 145: 8 bytes at `rbx` must be mapped.

### Calls handled by `jc_syscall` (job processes)

| # | Name | Arguments | Behaviour |
|---|---|---|---|
| 0 | exit | rbx=code | `jc_finish(cur, (code&0xFF)<<8)` and leave |
| 1 | puts | rbx=str | Writes the string (at most 4096) through `jc_tty_out` |
| 2 | fork | - | `jc_fork`. Returns the child's PID, or 0 in the child |
| 3 | putc | rbx=ch | One character through `jc_tty_out` |
| 4 | exec | rbx=cmdline | `jc_exec`. Returns `-1` on failure |
| 5 | spawn | rbx=cmdline | `jc_create(cmd, mypid, mypgid)`. Returns the child's PID |
| 6 | waitpid | rbx=pid | `jc_waitpid(pid, NULL, 0)` |
| 12 | brk | rbx=addr | 0 returns the current break. Otherwise maps frames eagerly up to the new break and returns it |
| 16, 71 | sleep_ms | rbx=ms | `S_SLEEP` for `(ms+9)/10` ticks. Returns 0, or -4 if interrupted |
| 17 | kill | rbx=pid, rcx=sig | `jc_kill` with permission check. Yields if this makes a signal deliverable to the caller |
| 20 | yield | - | `jc_yield_sys` |
| 31 | read | rbx=fd, rcx=buf, rdx=len | `vfs_read`. Blocks (`S_WAIT`, restartable) on `JS_BLOCK` |
| 32 | write | rbx=fd, rcx=buf, rdx=len | `vfs_write`. Blocks on `JS_BLOCK`. A regular file is marked dirty and written back when the last descriptor is closed (before v8.97 every write called `fs_sync`) |
| 39 | getpid | - | `p->pid` |
| 50 | getkey | - | Returns one byte from `g_tin` if the caller is in the foreground and input is waiting, else `-1` |
| 51 | waitkey | - | `jc_tty_read` one byte. Blocks |
| 131 | setuid | rbx=uid | Allowed for root, or when setting the same UID |
| 132 | getppid | - | `p->ppid` |
| 141 | signal | rbx=sig, rcx=handler, rdx=restorer, rsi=mask | Fails for 9 and 19. Returns the old handler |
| 142 | sigprocmask | rbx=how (0 block, 1 unblock, 2 set), rcx=set | Returns the old mask. Yields if a signal becomes deliverable |
| 143 | sigreturn | rbx=frame | Restores the context and mask (5.7) |
| 144 | dup2 | rbx=old, rcx=new | `jc_dup2` |
| 145 | pipe | rbx=int[2] | 4 KB pipe |
| 146 | fcntl | rbx=fd, rcx=cmd, rdx=arg | `F_DUPFD`/`GETFD`/`SETFD`/`GETFL` as 0/1/2/3 |
| 147 | umask | rbx=mask | Returns the old value |
| 148 | getgid | - | `g_gid` |
| 149 | setgid | rbx=gid | Allowed for root, or when setting the same GID |
| 150 | setpgid | rbx=pid (0 = self), rcx=pgid (0 = pid) | Only for self or a child in the same session |
| 151 | getpgid | rbx=pid (0 = self) | |
| 152 | setsid | - | Fails if the caller already leads a group. Otherwise `sid=pgid=pid` |
| 153 | getsid | rbx=pid (0 = self) | |
| 154 | tcgetpgrp | - | `g_tty_fg` (or 1) for session 1, else -1 |
| 155 | tcsetpgrp | rbx=pgid | Session 1 only |
| 156 | waitpid | rbx=pid, rcx=status*, rdx=opts | WNOHANG=1, WUNTRACED=2, WCONTINUED=8 |
| 157 | execv | rbx=path, rcx=argv | Joins `path argv[1..7]` and calls `jc_exec` |
| 158 | pause | - | Sleeps until a signal arrives. Returns -4 |
| 172 | isatty | rbx=fd | 1 if the descriptor is the console (`dev 5`) |

Any number not in this table falls through to the legacy dispatcher. For a job process, file calls there (30, 33, 36, 37, ...) still use that process's descriptor table, because the VFS calls `fdt()`.

### Legacy dispatcher calls (`syscall_dispatch` switch)

| # | Name | Arguments | Notes |
|---|---|---|---|
| 0 | exit | rbx=status | Behaves differently in preempt mode, in the cooperative scheduler, and in single-run mode (longjmp to `g_kjmp`) |
| 1 / 3 | puts / putc | rbx | Appends to the 256-byte `g_r3out` buffer |
| 2 | fork | - | `do_fork`: full copy, legacy table only |
| 4 | exec | rbx=path | `sys_exec` to `elf_load_run` |
| 5 / 6 | spawn / waitpid | rbx | `sys_spawn` (FAT, cooperative run to completion) / `g_last_exit[pid]` |
| 7 | sysinfo | rbx=what | 0 width, 1 height, 2 RAM MB, 3 ticks, 4 disk present |
| 9 / 11 / 12 / 60 | mmap / munmap / brk / sbrk | rcx=len /, / rbx=addr / rbx=inc | Anonymous memory only. munmap is a no-op |
| 15 | rand | - | `g_ticks*2654435761+12345` |
| 16 / 71 | sleep_ms | rbx=ms | Busy-waits (`rtw_delay_us`, or `cpu_idle` until the tick deadline) |
| 17 | kill | - | Returns 0 and does nothing |
| 18 / 19 / 21 | getcwd / chdir / mkdir | rbx=buf, rcx=n / rbx=path / rbx=path | `mkdir` applies `umask` |
| 30 | open | rbx=name, rcx=flags | `flags==1` is treated as `O_CREAT\|O_RDWR` |
| 31-37 | read, write, close, fstat (size), readdir(n,buf), lseek, dup | | VFS |
| 38 | unlink | rbx=path | |
| 39 / 132 / 130 / 131 | getpid / getppid / getuid / setuid | | Legacy PIDs are slot+1 |
| 40-45 | fb_clear, fb_pixel, fb_rect, (43 unused), fb_text, fb_present | rbx... | Draws into the compositor scene |
| 50 / 51 / 52 | getkey / waitkey / getmouse | | Raw keyboard queue (`kpop`) |
| 61 | thread_create | rbx=fn | `thread_create_k` |
| 70 | ticks | - | 100 Hz tick counter |
| 80-83 | shm_get, shm_map, msg_send, msg_recv | | IPC demo (5.13) |
| 90-95 | screen_clear, blit_shm, screen_rect, screen_text, screen_present, wm_poll | | Compositor demo |
| 100-105 | socket, connect(sid,ip,port), send, recv, close, resolve(host) | | TCP stack |
| 110 | stat | rbx=path, rcx=u32[3] | `{mode,size,isdir}` |
| 120 / 121 | getenv(name,buf,cap) / setenv(name,val) | | `g_env` |
| 133 / 140 | realtime ms / monotonic ms | - | RTC / `g_ticks*10` |
| 134-139 | rename, rmdir, truncate, ftruncate, fsync, readdir_at(path,n,name,type*) | | |
| 160-163 | link, symlink, readlink, stat2(path,u32[10],follow) | | `ux_*` |
| 165-169 | chown(path,uid,gid,follow), chmod, utime, mount(src,tgt), umount | | |
| 170 / 171 | gethostname(buf,n) / sethostname(str) (root only) | | |
| default | | | `-1` |

---

## 5.13 The legacy scheduler and demos (`core/proc/sched.c`)

These "stage" and "rung" demos come from before `jobs.c` and are still in the tree.

- Table: `MAXPROC 6`, `struct proc { used, alive, cr3, ksp, ctx, started, exit_status, zombie, ppid }`, each with its own 16 KB kernel stack `g_pstack[i]`.
- Cooperative scheduling: `proc_create` builds a kernel stack holding an iret frame (`ss=0x23`, `rsp=USTK_V+0x1000`, `rflags=0x202`, `cs=0x1B`, `rip=entry`), then `user_trampoline` as the return address, then six zeroed callee-saved registers. `sched_swap(&save_ksp, load_ksp)` swaps kernel stacks. `sched_run_all` switches into the first process and comes back once all have exited. `sched_yield_now` and `sched_exit_now` pick the next process round-robin with `sched_next`. `user_trampoline` runs `swapgs; iretq`. `user_trampoline_fork` clears `eax` first.
- Preempt demo (`preempt_run`): two copies of `user_preempt`. Each has an ID byte (`'1'`, `'2'`) mapped at `0x40000050000`. While `g_preempt_on` is set, IRQ0 in `isr_body` saves the frame into `g_proc[cur].ctx`, picks the next live process, writes CR3 and copies the next process's context into the interrupt frame, so `iretq` resumes it. When none are left, it does `k_longjmp(g_preempt_kern,1)`.
- IPC: shared memory (`SHM_MAX 4`, up to 24 frames each, mapped at `0x40000600000 + id*1 MB`) and message queues (`MSGQ_MAX 6`, 32 deep, 64-byte messages).
- Compositor demo (`compositor_run`): three copies of `user_compdemo`. Messages are `struct wmsg` with types `WM_CREATE/WM_DAMAGE/WM_CLOSE`, sent on queue 0.
- Fork and threads: `do_fork` copies every user page eagerly. `thread_create_k` shares the caller's CR3 and gives each thread a 64 KB-spaced user stack from `0x40000200000`.
- `sys/native/syscall.c` helpers: `sys_spawn` loads from FAT and checks `perm_can_exec`, `sys_waitpid` returns the status stored in `g_last_exit[]`, and `sys_exec` uses `elf_load_run`.

---

## 5.14 init (PID 1) and `/etc/rc`

- Start: `jc_start_init()` (`fs/vfs/unixfs.c`) is called from `ux_desktop_start` when the main loop enters `ST_DESKTOP`. If PID 1 does not exist yet, it runs `jc_create("init",0,0)`, which resolves to `+init`, and then sets `pid=1`, `pgid=1`, `ppid=0` and `uid=gid=0`. The live terminal (`ST_LIVE`) sets up the Unix layer with `ux_core_start` but does not start init.
- `/etc/rc` is created by `ux_boot_layout` with this content:
  ```
  # /etc/rc - run by init (PID 1) at boot
  echo NoovexOS: init started
  ```
- The init program (`usr/src/init.c`):
  1. installs a SIGCHLD handler
  2. reads `/etc/rc` (up to 4 KB) line by line. `run_line` splits each line on spaces and tabs (up to 11 arguments), skips `#` comments, and treats a final `&` as background. Then it runs `fork()` and `execv(argv[0], argv)`; on failure it prints "init: cannot run ..." and exits with 127. Foreground lines are waited for with `waitpid`.
  3. appends `boot: init started, /etc/rc done at <ctime>` to `/var/log/boot.log`
  4. loops forever: `waitpid(-1,&st,WNOHANG)` until no more children, then `pause()`.
- Orphan handling: `jc_finish` gives orphans to init while it is alive. Init reaps them in its `waitpid(-1)` loop. If init has died, `jc_poll` frees zombies whose parent is 1.

---

## 5.15 Threads and futexes

Since v8.97 a process can have several threads that run at the same time on different cores. A thread is an ordinary `struct jproc` slot that shares the address space and the per-process state of its thread-group leader.

### What is shared and where it lives

| State | Shared by | Kept in |
|---|---|---|
| Address space | all threads | the same `cr3` value in every thread's slot; freed once, when the last thread exits |
| Descriptors and cloexec flags | all threads | the leader's `fd[]` and `cloexec[]`; `fdt()`/`fdce()` always return the leader's arrays |
| Working directory, umask, `brk`, mmap high-water mark, `lxbrk` | all threads | the leader; `linux_syscall` loads them into the per-CPU variables before each call and stores them back after it |
| Signal handlers (`sighand`, `sigrest`, `sigamask`, `saflags`) | all threads | copied to every thread's slot by `rt_sigaction`, so each slot can be read directly |
| Interval timer (`it_next`, `it_int`) | the process | the leader |
| PID, parent, process group, session | the process | the leader; `jc_tg_sync_ids` copies `pgid`, `sid` and `ppid` to the other slots when they change |
| Registers, FPU state, TLS base, signal mask, pending thread signals, futex wait state | one thread | each slot |

`jc_lead(p)` returns the leader, `jc_tg_member(j, L)` tells whether slot `j` is a live thread of group `L` (not free, zombie or `S_TDEAD`), and `jc_tg_live(L, except)` counts live threads.

### Creating a thread: `clone` with `CLONE_THREAD`

`lx_clone` (`sys/linux/linux.c`) handles the flags musl's `pthread_create` uses: `CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM | CLONE_SETTLS | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID | CLONE_DETACHED`. `CLONE_THREAD` requires `CLONE_VM` and `CLONE_SIGHAND` (EINVAL otherwise).

`jc_clone_thread(r, stack, tls, settls)` (`core/proc/jobs.c`) copies the calling slot, gives the copy a new ID from `g_next_pid`, keeps `tg` (so it joins the same group), copies `tgid`, `ppid`, `pgid` and `sid` from the leader, clears pending signals, timers, futex state and its unused descriptor array, saves the caller's registers with `rax = 0`, and sets `rsp` and the TLS base if asked. The new slot is `S_RUN` and `smp_kick_idle` wakes an idle core for it. Then `lx_clone` stores `ctid` for `CLONE_CHILD_CLEARTID`, writes the new ID through `CLONE_CHILD_SETTID` and `CLONE_PARENT_SETTID`, and returns it.

A `clone` without `CLONE_THREAD` is still a fork (copy-on-write), also when `CLONE_VM | CLONE_VFORK` is given, as musl's `posix_spawn` does; the child gets its own copy of the stack it is given, which is enough because it only calls `execve` or `_exit`.

### IDs

`getpid` returns the leader's `pid` (the process ID), `gettid` the slot's own `pid`. `getppid`, `getpgid`, `getsid` and `setpgid`/`setsid` work on the leader. `set_tid_address` stores `ctid` and returns the thread ID. `set_robust_list` stores the head and `get_robust_list` returns it; the list is not walked when a thread dies.

### Signals

- Process-directed signals (`kill`, the terminal's ^C/^Z, SIGCHLD, SIGALRM from the interval timer, `jc_kill`) go through `jc_signal_proc`. It delivers to the given slot unless that slot is an `S_TDEAD` leader or blocks the signal; then it picks the first live thread that does not block it. SIGKILL and SIGCONT go to every thread.
- `jc_kill` only looks at leaders (and at a thread whose own ID was named), so a signal sent to a group is delivered once per process, not once per thread.
- Thread-directed signals: `tgkill` (234) and `tkill` (200) call `jc_signal_thread`, which never redirects. `tgkill` checks that the thread belongs to the given process.
- A stop signal stops the whole process: `jc_stop` marks the thread stopped, sets SIGSTOP pending on the other threads, and records the stop in the leader, where `waitpid` sees it.

### Ending threads and processes

| Event | What happens |
|---|---|
| `exit` (60) in a thread while other threads live | `jc_thread_exit` drops only this thread: `ctid` is set to 0 and futex-woken, a leader becomes `S_TDEAD`, any other slot is freed at once (threads are not waited for) |
| `exit` of the last thread, or `exit_group` (231) | `jc_finish` |
| Fatal signal or fault in any thread | `jc_finish` |
| `jc_finish(i, status)` with other live threads | the leader records `gexit` and the status, every other thread gets SIGKILL, and thread `i` is dropped. Each thread ends when it next enters the kernel or is picked (a thread running on another core is kicked with an IPI) |
| `jc_finish` for the last live thread | closes the leader's descriptors, frees the address space once, makes the leader `S_ZOMB` with the group status, reparents children and wakes the parent |

Because the address space is only freed by the last thread, and a thread that is still running on another core counts as live, no core can be using page tables that have been freed.

`waitpid` and `wait4` are answered for the whole process: the caller's leader is the parent that is matched, and only leader slots are reported as children. A fork from any thread makes a child whose parent is the process.

### `execve` from a threaded process

`jc_exec` first gives every other thread `tkill` and SIGKILL, then blocks (`S_WAIT`) and restarts the call until it is the only live thread. If the caller is not the leader, the new program then takes over the leader's slot, so the process keeps its PID: the leader gets the caller's signal mask and pending signals, the caller's slot is freed, and `g_jcur` is switched to the leader before the new image starts.

### Futexes

`lx_futex` (`sys/linux/linux.c`) implements `FUTEX_WAIT`, `FUTEX_WAKE`, `FUTEX_REQUEUE`, `FUTEX_CMP_REQUEUE`, `FUTEX_WAKE_OP`, `FUTEX_WAIT_BITSET` and `FUTEX_WAKE_BITSET`, with or without `FUTEX_PRIVATE_FLAG`, and `FUTEX_CLOCK_REALTIME` for the bitset wait. A futex is identified by the thread group and the user address (`fx_tg`, `fx_addr`), so futexes work between the threads of one process but not between processes through shared memory.

- Wait: under the big kernel lock the call reads the word; if it differs from the expected value it returns EAGAIN. Otherwise it records the wait in the thread (`fx_wait = 1`, address, bitset, and `fx_dead` for a timeout: relative for `FUTEX_WAIT`, absolute for `FUTEX_WAIT_BITSET`), and blocks in `S_WAIT` with `wake` set to the timeout tick (or far in the future).
- Because blocked calls restart (section 5.6), the thread re-enters `lx_futex` when it is woken. With `fx_wait` already set it returns 0 if `fx_woken` is set, ETIMEDOUT (110) once `fx_dead` has passed, or EINTR if a signal is deliverable; otherwise it blocks again.
- Wake: `jc_futex_wake(tg, addr, n, bits)` marks up to `n` waiting threads of the group on that address `fx_woken` and sets their `wake` to 0, so `jc_runnable` picks them at once, and kicks idle cores. Requeue moves waiters to the second address by changing `fx_addr`.
- `FUTEX_WAKE_OP` performs the operation on the second word with an atomic compare-exchange loop, because threads on other cores may change that word in user mode at the same time.
- Delivering a signal handler clears `fx_wait`, so after `sigreturn` the restarted call checks the word again.
- No wake-up can be lost: checking the word and recording the wait happen under the big kernel lock, and a waker changes the word in user mode before calling `FUTEX_WAKE`, which also takes the lock.

### Thread-safety of the rest of the kernel

All kernel code still runs under the big kernel lock (section 4.11), so two threads in system calls at the same time are serialized. Threads only run in parallel in user mode. The changes that make that safe are the TLB rules in section 4.15 and the leader-held process state above.

### Tests

`featest threads` (section 12.2) checks a mutex-protected counter in 4 threads, thread-local storage, a condition variable with one producer and two consumers, `getpid`/`gettid`, threads spread over several cores, `pthread_kill`, a process signal reaching a thread that does not block it, semaphores, a blocking `read` woken from another thread, an `mmap` seen by all threads, `fork` from a thread, `pthread_cond_timedwait` timing out, 200 short threads, `exit` ending all threads, the main thread exiting first, SIGTERM to a threaded process, and `execve` from a threaded process. The ports xz (`-T`), zstd (`-T`) and SQLite are now built with thread support.

---

## 5.16 Interval timers and alarm

`alarm` (37), `setitimer` (38) and `getitimer` (36) are implemented for `ITIMER_REAL`. `ITIMER_VIRTUAL` and `ITIMER_PROF` are accepted and reported as disarmed, but never fire.

- The timer belongs to the process: `it_next` (tick of the next expiry, 0 = off) and `it_int` (interval in ticks) live in the leader. Times are converted with `lx_tv_ticks`, rounding up to the 10 ms tick.
- `jc_itimer(i)` runs at the top of `jc_runnable`, which the scheduler calls for every slot on every pick. When `g_ticks` has reached `it_next` it sends SIGALRM (14) with `jc_signal_proc` and either reloads the timer (`it_next += it_int`, skipping missed periods) or turns it off.
- `alarm(sec)` returns the seconds left on the previous timer (rounded up) and sets a one-shot timer; `alarm(0)` cancels it.
- `fork` clears both fields in the child; `execve` keeps them, as Linux does.
- The default action of SIGALRM ends the process ("Alarm clock" in the shell).
