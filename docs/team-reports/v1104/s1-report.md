# S1 report: sys/linux/ user-pointer sweep (v11.03 tree, read-only)

Helpers (sys/linux/syscalls/fifos_and_names.c:10-19): lx_ok / lx_okw (jc_umapped[_w] plus the g_kb kernel-buffer allow-list), lx_str (string read with a size limit), lx_get / lx_put (copy from / to user after a range check).
jc_ustr is a static in core/proc/jobs/thread_groups_and_exit.c:274 and only the native ABI uses it. The Linux ABI uses lx_str.

Overall: the Linux ABI is disciplined. Almost every user access goes through lx_get, lx_put or lx_str, or comes right after an lx_ok/lx_okw range check.
Counts and sizes are capped: iovec <=1024, semop nsops <=64, select n <=1024, poll n <=FDMAX*4, sethostname <=64, clone3 copy <=88, getdents cap 64K, execve args XA_CAP.
iov lengths are limited by jc_uok (user window 2^39), so their sums cannot overflow in lx_msg or lx_rwv.

## MEDIUM-1  fds_and_system_info.c:311 (getdents, nr 78): kernel walks a user buffer using record lengths read back from user memory
After lx_getdents fills the buffer, the reshuffle loop rereads `rl=*(u16*)(d+o+16)` and `lx_slen(d+o+19)` from user memory. It does not check rl or bound the string.
Scenario (SMP, threaded program): a second thread sets d_reclen to 0 while the call runs. The kernel then loops forever with the kernel lock held, which freezes the whole system. Or it sets rl large or removes the NUL, so `d[o+rl-1]` and the byte-shift loop write up to ~64K past the checked range. That leads to a kernel #PF near the top of the user window, or overwrites other user memory.
Patch (replace the loop body):
```c
case 78: { long n=lx_getdents((int)a1,a2,a3); if(n<=0)return n; u8* d=(u8*)a2; long o=0; while(o<n){ u16 rl=*(u16*)(d+o+16); if(rl<20||rl>n-o)break; u8 ty=d[o+18]; int L=0; while(L<rl-20&&d[o+19+L])L++; for(int i=0;i<L;i++)d[o+18+i]=d[o+19+i]; d[o+18+L]=0; d[o+rl-1]=ty; o+=rl; } return n; }
```
(Cleaner fix: build the old-format records in a kmalloc buffer and copy them out with lx_put.)

## LOW-2  inotify.c:244 (writev to dev 13, cnt>1): return value is the sum of unchecked user iov lengths
`all+=((u64*)iov)[i*2+1]` adds every user length. Only the first 1024 bytes are checked and written. The sum can wrap around or claim terabytes were written, which breaks the caller's partial-write retry logic. It also rereads user iov memory directly after lx_write.
Patch: `u64 all=0; for(int i=0;i<cnt;i++){ u64 l; if(lx_get(&l,iov+(u64)i*16+8,8))break; all+=l; if(all>0x7FFFF000ULL){ all=0x7FFFF000ULL; break; } } return (long)all;`

## LOW-3  fds_and_system_info.c:325-329 (waitid, nr 247): direct user reads and writes after jc_waitpid
`lx_zero((void*)a3,128)`, `*(int*)a3` and `lx_zero((void*)a5,144)` rely on the lx_okw check made before jc_waitpid. That check is only valid if jc_waitpid never returns here after blocking. (I believe it restarts the call through jc_block, so this is defence in depth.)
Patch: replace with lx_put(a3,zero128,128), lx_get(&st,a3,4) and lx_put(a5,zero144,144).

## Note (not verified): check-then-copy pattern under SMP
lx_get and lx_put check the range, then copy with plain byte loops. If threads of the same process run in user mode on other cores during a syscall, a racing munmap gives a kernel-mode #PF inside the copy. Worth confirming that the kernel #PF path (core/proc/jobs/faults.c, k_setjmp g_js_kern) recovers from faults in user-copy loops rather than panicking.

## Reviewed, no finding
g_kb allow-list (all set/reset sites, plus reset on every syscall entry at fds_and_system_info.c:525), lx_path0 long-name mangling bounds, lx_msg / lx_rwv iov handling, semop, SysV msg / sem / mq, select / poll / epoll_wait sizes, clone / clone3, execve argv / env, sethostname, readlink /proc/self/exe, uts, rt_sigqueueinfo, sched_getaffinity, socketpair.
Not covered in 9 min: lxsock.c (425 lines), sysfs.c write paths, the ioctl tables in timers_and_handles.c.
