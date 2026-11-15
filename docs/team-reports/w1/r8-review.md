# r8 review of c8 (branch x-c8, commit e9706f50, merged into main as v11.07)

Files: arch/x86_64/cpu/percpu.c:23-49, arch/x86_64/smp/smp_sched.c:79-94, core/proc/jobs/faults.c:78-88.
No other wave-1 coder touched these three files. I diffed x-c8 against main and they are identical. c2's main.c/crash.c changes
do not touch the bkl_* calls.

## Verdict
The lock logic is sound. I found no deadlock and no lost ticket, and the apq counter cannot leak.
- The CPU0 flag cannot starve APs, because CPU0 clears it as soon as it gets the lock.
- The fix removes the QEMU convoy that was measured. It is a real improvement.
- But it is probably NOT the owner's freeze. On 12 real threads, an idle AP's jc_pick hold is microseconds, so even 11 queued
  APs cost CPU0 well under 1 ms. q-report itself calls the 4-15 s crawl a "host oversubscription artifact": 8 vCPUs ran on 2
  host cores, and the next ticket holder's vCPU had no host core. The owner's symptom was Start/taskbar-specific and worked in
  v11.02, which fits the input lock or a nested loop better. Do not close the freeze on c8 alone. It needs a test on the
  owner's hardware.

Two real defects make idle APs fall back into the old behaviour (MEDIUM), and there are a few LOW items.

## MEDIUM 1: jc_ap_peek reports a due itimer for a job that is running on another CPU, so every idle AP takes bkl_lock every tick (faults.c:83)
`if(p->it_next&&now>=p->it_next)return 1;` comes BEFORE `if(p->runcpu)continue;`. jc_pick skips runcpu jobs
(faults.c:142) and never calls jc_runnable/jc_itimer on them, so it_next is not advanced while the job runs. This happens with
any job that has a periodic ITIMER_REAL (setitimer: SDL games, ping, some X clients). While such a job is running on an AP,
peek is always 1. Every idle AP then does a full bkl_lock on every 100 Hz tick and every kick, for a jc_pick that cannot pick
that job. This is the original convoy again, capped at 2 tickets, with the other APs spinning in bkl_ap_admit instead of hlt.
Patch (faults.c, in jc_ap_peek):
```
-        if(p->it_next&&now>=p->it_next)return 1;
-        if(p->runcpu)continue;
+        if(p->runcpu)continue;
+        if(p->it_next&&now>=p->it_next)return 1;
```
This matches what jc_pick can actually act on. The itimer of a running job is still serviced when ap_timer preempts it
(every 2 ticks) and it becomes runcpu=0.

## MEDIUM 2: POSIX timers (timer_create) are only serviced by the bkl_try sweep, which can fail for a long time (smp_sched.c:91, faults.c:78)
jc_runnable -> jc_itimer -> lx_ptimer_tick (sys/linux/syscalls/inotify.c:123) is what fires timer_create signals. jc_ap_peek does
not check g_lxpt. So a job sleeping until a POSIX-timer signal only wakes when the one-per-tick sweep's bkl_try succeeds.
- bkl_try succeeds only if nobody holds or queues on the lock at that instant.
- With SMP and 3 or more cores, jc_bsp_off() is true, so CPU0 never runs jc_pick itself.
- CPU0's main loop holds the BKL for the whole pass except the idle section and the bkl_yield windows.
So under desktop load (dragging, animation) the timer signal can be late for an unbounded time. Before c8 it was a
guaranteed bkl_lock every tick. The same applies to the S_WAIT wdl/pollwake and lxn_cpu_hold paths that the sweep is meant
to keep alive.
Patch: bound the latency. After 5 ticks without a successful sweep, one AP blocks for it:
```
-static volatile u64 g_ap_sweep;
+static volatile u64 g_ap_sweep, g_ap_sweep_ok;
...
-            else { u64 t=g_ticks, o=g_ap_sweep; if(t!=o&&bkl_try()){ if(__atomic_compare_exchange_n(&g_ap_sweep,&o,t,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED)){ got=1; g_ap_sweeps++; } else bkl_unlock(); } else g_ap_skip++; }
+            else { u64 t=g_ticks, o=g_ap_sweep;
+                if(t!=o&&__atomic_compare_exchange_n(&g_ap_sweep,&o,t,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED)){
+                    if(t-g_ap_sweep_ok>=5){ bkl_lock(); got=1; } else got=bkl_try();
+                    if(got){ g_ap_sweeps++; g_ap_sweep_ok=t; } else g_ap_skip++;
+                } else g_ap_skip++; }
```
The CAS comes first now, so only one AP per tick even tries, and at most one AP per 50 ms ever queues. That keeps the CPU0
guarantee. If you prefer, also add an exported `int lx_ptimer_due(int tg)` (a read-only scan of g_lxpt.next against
lx_now_ns/hr_ns) to jc_ap_peek. The fallback above covers the timer paths either way.

## LOW 3: stale wdl keeps peek true (faults.c:86)
For S_WAIT, peek treats any p->wdl as a deadline. jc_wdl_due (faults.c:53) only does that for S_SLEEP, pollwake or fx_wait,
and otherwise clears wdl, but only when jc_pick happens to evaluate that job. Patch:
```
-if(p->wdl){
+if(p->wdl&&(st==S_SLEEP||p->pollwake||p->fx_wait)){
```

## LOW 4: an AP spinning in bkl_ap_admit is not fair and is invisible to diagnostics (percpu.c:26-36)
- **The 3rd CPU.** A 3rd AP that needs the lock for a syscall (lsys_c), a page fault, or a preempt IPI spins in admit holding
  nothing. Its IF state stays as the caller had it: IF=0 for faults, so no IPIs arrive. It has bkl_wait=1, so smp_tlb_sync
  skips it, and that is correct because it reloads CR3 at acquire (percpu.c:47). This is safe.
- **Fairness.** Admission is a CAS race, not FIFO. Idle "peek" APs compete on equal terms with an AP that has a user thread
  blocked in a syscall, so that thread can lose many times in a row. With MEDIUM 1 fixed, few idle APs compete, so this is
  acceptable.
- **Diagnostics.** frz_bkl_wait (crash.c:302) is never called from the admit spin, so a stuck AP there shows no "waits for
  the kernel lock" box. The 2 admitted APs still call it and still drive nmiwd_check, so the NMI watchdog keeps working.
  Patch (keep it cheap):
```
-        for(int i=0;i<16;i++)cpu_pause();
+        for(int i=0;i<16;i++)cpu_pause();
+        if(!(++sp&0xFFFFF)){ extern void frz_bkl_wait(int,int,u64) __attribute__((weak)); if(frz_bkl_wait)frz_bkl_wait(cpu_id(),g_bkl.owner,0); }
```
  The w0=0 makes the box count since boot. Better: record w0=bkl_tsc() on the first failure and pass that.
  `g_bkl_apq_waits++` is not atomic. It is only a statistic.

## LOW 5: AP sweeps through bkl_try give false "longest hold" stats (percpu.c:59-66)
bkl_try does not set g_bklt0, so bkl_unlock (percpu.c:54) measures the hold from the last bkl_lock. Before c8 that was rare
(only the ps2 IRQ path). Now an AP does it every tick, so "lat: bkl cpu N ... longest hold" (fds_and_system_info.c:135)
reports huge holds that never happened, which misleads the next freeze hunt. Patch in bkl_try:
```
-    g_bkl.owner=me; g_bkl.depth=1;
+    g_bkl.owner=me; g_bkl.depth=1; g_bklt0=bkl_tsc();
```

## Checked and OK
- **CPU0 flag ordering (percpu.c:43-46).** An AP can read want=0 just before CPU0 sets it. That AP's ticket is then behind
  CPU0's, so there is no harm. All the RMWs are locked on x86.
- **CPU0 wait bound.** CPU0 waits for at most the owner plus 2 admitted APs. If an AP holds the BKL a long time (lsys_c holds it
  for the whole syscall), CPU0 still waits for that. c8 says this openly.
- **No deadlock.** apq is incremented in admit and decremented only after acquire, and there is no other exit.
- **bkl_yield and bkl_take still work.** The admitted APs hold tickets, so bkl_yield's next!=serving+1 test still sees them.
  bkl_take goes through bkl_lock and so through admit.
- **g_smp_on.** The `ap` flag is latched per call, so the counter stays balanced if g_smp_on changes.
- **Tests.** The before/after QEMU numbers are believable for QEMU: 36 lags with a max of 1066 ms, against 0 lags, with every
  AP sampled at hlt. c8 did not test real hardware or -smp 8 on a host with at least 8 cores, so the claim that this is the
  owner's freeze is unproven (see Verdict).
