# r7 review of c7 (branch x-c7, merged in main as v11.07)

Scope: `git log -p v11.06..x-c7` (5 commits), c7-report.md, ctasks/c7.md. I checked the merged files in main.
No other wave-1 branch touched interrupts.c, startup.c (apart from the UA bump), jpeg.h, cssparse.c, timers_and_handles.c, fetch.c or fetch_queue.c.
c6 changed lxsock.c in the DHCP part only, so there is no conflict with c7's netlink line (:398).

## Verdict
- The jpeg, fetch method cap, CSS depth, ALSA and netlink fixes are correct. None of them is a false fix.
- The #DF redirect works for the case it targets.
- Main question, can the #DF redirect misfire for non-web double faults: **yes, in narrow cases.**
  - The 56 KB guard is fine.
  - The problem is the `web_df_ok()` window and the context check. They were copied from the #PF/#GP redirect, and they are too loose for an abort-class exception.

## Findings

### MEDIUM 1: the #DF window covers memory that is not the engine stack, and #DF can hit while some other context is running
Location: apps/internet/browser/glue/startup.c:16 (`web_df_ok`) and arch/x86_64/interrupts/interrupts.c:60.

`web_df_ok` returns true when all of these hold:
- `g_in_engine` is set,
- the CPU is in ring 0,
- `lo-1MB <= rsp < lo+64KB`.

None of these tie the #DF to the engine's own execution on CPU 0. Concrete misfires:

a) **[lo-1MB, lo) is someone else's heap.**
   - On the engine path, an overflow that jumps below `lo` lands in mapped heap. That gives a silent write or a plain #PF, never a #DF, so this part of the window only ever matches foreign stacks.
   - Example: a 512 KB fetch fiber stack (fetch_queue.c:123, `nf_init`) or another kmalloc'd stack sitting right below `g_estack`.
   - Fibers are resumed from `busy_pump` -> `nf_round` while `g_in_engine==1` (the `nwk_busy` else-branch, startup.c:52).
   - If a fiber hits its own guard page and double faults, the fault is redirected to `web_fault_land`. That longjmps into the engine from the fiber, which leaves `g_nf_cur` and `g_nf_stepping` set. `nf_round` then returns early forever, so all networking is silently dead. Before c7 the result was an RSOD.
   - The worker stack (webwork.c:78) and the other CPUs are exposed the same way.
   - `web_fault_redirect` has no CPU check, and the web worker on CPU 1 runs while CPU 0 has `g_in_engine==1`.
   - Any AP #DF with rsp in the window would send CPU 1 to `web_fault_land` on CPU 0's live engine stack, so two CPUs would run the engine on one stack.

b) **An IRQ handler running on the engine stack.**
   - Interrupts at CPL0 with IST=0 run on the engine stack. Two cases:
     - An IRQ arrives while rsp is already inside the guard (c7 mentions this).
     - A handler's own frames cross into the guard while the engine is deep.
   - In both cases the #DF is redirected and the handler is abandoned mid-way. Its EOI never happens: the LAPIC ISR bit stays set, so that vector and every lower priority class on CPU 0 are blocked. If the PIC is in use, the PIC ISR bit stays set the same way. Any spinlock the handler held also stays held, for example `in_lock`, which is a real non-recursive spinlock since v11.06.
   - The result is a freeze or a "ticks stopped" box instead of the clean RSOD the user had before.
   - This is the freeze class the owner is chasing. The #PF path already had this hazard. c7 widens it to the most common overflow mode, because a push into the guard gives #PF, then a fault while delivering the #PF, then #DF.

**Patch.** Narrow the #DF window to the engine's own guard memory, and refuse to redirect while an IRQ is in service:

```diff
--- a/apps/internet/browser/glue/startup.c
+++ b/apps/internet/browser/glue/startup.c
@@ -16 +16 @@
-int web_df_ok(u64 sp) { u64 lo = (u64)g_estack; return g_in_engine && g_estack && sp < lo + 0x10000 && sp + 0x100000 > lo; }
+int web_df_ok(u64 sp) { u64 lo = (u64)g_estack; return g_in_engine && g_estack && !g_web_nest && sp >= lo && sp < lo + 0x10000; }
--- a/arch/x86_64/interrupts/interrupts.c
+++ b/arch/x86_64/interrupts/interrupts.c
@@ -56,2 +56,3 @@
 struct vfile;  
+static int irq_in_service(void){ extern int lapic_isr_top(void); if(lapic_isr_top()>=0)return 1; outb(0x20,0x0B); if(inb(0x20))return 1; outb(0xA0,0x0B); if(inb(0xA0))return 1; return 0; }
 
@@ -60 +61 @@
-...&&(r->vector!=8||web_df_ok(r->rsp))&&web_fault_redirect(&r->rip,&r->rsp))return; } panic(r,0); return; }
+...&&(r->vector!=8||(web_df_ok(r->rsp)&&!irq_in_service()))&&web_fault_redirect(&r->rip,&r->rsp))return; } panic(r,0); return; }
```

Why this patch is safe:
- **The guard is still covered.** The guard spans [align(lo)+4K, align(lo)+60K). Its top is at most lo+65535, so it lies entirely inside the new window [lo, lo+64K).
- **No foreign stack can match.** Nothing except the engine ever has rsp in [lo, lo+64K), so the fiber, worker and other-CPU cases (a) cannot match.
- **ISR check.**
  - The engine never runs from IRQ context, so on a genuine engine #DF the LAPIC ISR is empty. The #DF itself is an exception and does not set an ISR bit.
  - A set ISR bit therefore means the #DF interrupted an IRQ delivery or a handler, so we panic as before.
  - `lapic_isr_top()` (cpu_init.c:28) already returns -1 when the LAPIC is disabled.
  - interrupts.c already uses `outb`/`inb` at :80 for the PIC ISR read.
- **Optional hardening.** Also check `r->rip` against the IRQ handler range. With the ISR check this is not needed.

### LOW 2: the #DF path has never been run
c7 did not run QEMU. Nothing has executed the #DF -> IST2 -> iretq-to-`web_fault_land` sequence, and the 8 KB per-CPU `ist_df` stack (percpu.h:31) now also has to hold `web_fault_redirect`'s `klog` and all its hooks. klog takes no locks, so there is no deadlock risk, but the stack depth through the hooks has not been checked.

Intel documents the saved CS:RIP of a #DF as undefined. In practice it is the original instruction. Only rsp is used here, and only for a range check, so a bad value fails safe (panic).

Recommended QEMU test: a page with `function f(){f()}` plus a CSS file with 30 levels of nested `@media{`.
- With the deep CSS nesting, expect "page crashed the engine ... stopped safely".
- Expect no RSOD.
- Expect ticks to keep advancing afterwards.

No code patch.

### LOW 3 (pre-existing, not c7): a guard page is left inside freed heap
fetch_queue.c:125-126. `nf_init` guards `f->stk`, and then, if `f->scr` fails to allocate, it calls `kfree(f->stk)`. The not-present page goes back into the heap, and the next user of that memory gets a #PF.

Patch: allocate and check both buffers before guarding.
```diff
-    { extern int mm_guard_range_bottom(void*, unsigned long); if (f->stk) mm_guard_range_bottom(f->stk, NF_STK); }
     if (!f->stk || !f->scr) { if (f->stk) kfree(f->stk); if (f->scr) kfree(f->scr); f->stk = 0; f->scr = 0; return 0; }
+    { extern int mm_guard_range_bottom(void*, unsigned long); mm_guard_range_bottom(f->stk, NF_STK); }
```

## Checked and OK (no action)
- **56 KB guard** (startup.c:10-15):
  - The guard is the 14 pages [align(lo)+4K, align(lo)+60K).
  - The guard is entirely below lo+0x10000, so in `web_fault_redirect` a fault anywhere in it takes the "reset to lo+8MB" branch, never `sp-=2048`. c7's reasoning about 56 KB versus 64 KB is correct.
  - `g_estack` is never freed, so the not-present pages never return to the heap.
  - If `mm_guard` fails (1 GB pages), the guard is partial or absent. That is the same as before and fails safe.
  - `frame_call` (webcache_and_frame_errors.c:69) is in the same translation unit (it is included from webglue.c:18 onwards), so the static `estack_guard` is visible there.
- **#DF still panics when it should:**
  - While `g_web_nest` is set: `web_fault_redirect` returns 0.
  - Outside the engine.
  - After 64 faults.
  - For ring 3.
  - The `wk_fault_redirect` clause still excludes vector 8.
- **jpeg.h:**
  - The DHT, DQT, SOF and SOS bounds are right.
  - The `&3` masks match the 4-entry tables.
  - Se>63 and Ss>Se are rejected.
  - `JPROG=0` is reset per decode.
  - `W*H*4` fits in an int (W, H <= 4096).
  - No caller depends on the old -12 code: progressive images that are too large were a heap overflow before and are now rejected with -11.
- **fetch.c:11 / fetch_queue.c:188:**
  - `putn` exists.
  - `nq_dup(p, len)` with an explicit length is already used at fetch_queue.c:38.
  - 15 characters is longer than any real HTTP method.
  - The error line now fits in 1200 bytes: about 35 + 15 + 1100 + 25.
- **cssparse.c:827:**
  - All five recursive `parse_rules` sites now pass depth+1.
  - `parse_body` keeps its own cap of 12, and `@import` keeps depth<4.
  - Worst case is 25 x ~8.5 KB, about 212 KB of stack.
- **timers_and_handles.c:66/180:** the guard runs before any `%boundary`. Returning -EBADFD for an unconfigured stream is the correct ALSA errno.
- **lxsock.c:398:** `L>(u32)(n-off)` closes the signed-overflow bypass. n-off >= 16 is guaranteed by the loop condition.
