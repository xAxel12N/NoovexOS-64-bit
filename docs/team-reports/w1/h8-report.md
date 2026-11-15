# h8: SMP / core / arch / mem / native syscalls (v11.06)

Scope: core/, arch/x86_64 (interrupts, timers, SMP, BKL, NMI watchdog, IST), mem/, sys/native. Read-only review.

## What runs on the APs (checked; most of the suspects turn out to be clean)
- `ap_loop` (arch/x86_64/smp/smp_sched.c:79) does four things. It runs jc jobs with `jc_pick`/`jc_enter` under `bkl_lock`; user code runs with the BKL dropped, and every syscall, fault and preempt path takes the BKL again. It calls `wk_ap_turn` (CPU 1 only, no BKL). It calls `smp_work_step` (own smp_lock). It calls `cpupower_tick` (per-CPU arrays).
- AP interrupts: `ap_timer` (only `nmiwd_check`, which has its own lock) and `smp_ipi` (RESCHED takes the BKL). Exceptions on an AP take `bkl_lock`, then `ps2_drain` + `isr_body`. So `mouse_feed`/`kbd_feed`/`kpush` can run on an AP, but always under the BKL.
- `kidle` on an AP cannot be reached. Native syscall 51 from a jc job is handled in `jc_syscall` (thread_groups_and_exit.c:315, `jc_tty_read`) and never gets to `kidle`. Legacy g_proc native processes run only on the BSP.
- The xHCI is polled only from BSP loops that hold the BKL (main loop, `busy_pump`/`ui_pump`/`desk_nested_step`, sleep loop, panic wait). There is no MSI anywhere (no 0xFEE0 programming), so no device IRQ reaches an AP.
- Writers of `mouse_dx/dy` (ps2.c:110, hid_keyboard_mouse.c:72/84/108) and of `kpush` all hold the BKL. The only exceptions are BSP interrupt handlers, which are serialised by cli/irq_save. The `in_lock` in input.c only adds safety on top. Readers also hold the BKL. I found no cross-CPU race here like the v11.06 mouse queue one.
- klog ring, crumbs, toasts: klog/toast are called only with the BKL held (`freeze_tick` uses `bkl_try`). `crumb()` is lock-free with an atomic index. The worst case is garbled text when a BSP interrupt lands inside a klog; nothing crashes.
- The web worker on CPU 1 (`wk_ap_turn`, no BKL) is self-contained: build/wkx_all.o needs only mem*/str* functions, the `wk_k_*` rings, `nwk_ms`, `nwk_epoch_ms` and `nwk_tz_min`. Its malloc comes from its own arena (w_krt). The only shared hardware access is `k_realtime_ms` → `read_clock` (CMOS ports 0x70/0x71), and only while `g_rt_base==0`.
- The heap (mem/heap.c) is protected only by irq_save plus the BKL. Every caller I found holds the BKL. The unlocked decoders in lib/compress/unpack.c use `XZ_SINGLE` and `ZSTD_decompressDCtx`, which do not allocate. OK.
- TLB sync (`smp_tlb_sync`): the `bkl_wait` skip plus the `tlb_pending` reload in `bkl_lock` is correct.

## Findings (most important first)

### 1. MEDIUM (70%): after a kernel panic on SMP, a "NoovexOS is stuck" box is drawn over the crash screen
core/crash.c:274 (`crash_panic`) and core/crash.c:301 (`frz_bkl_wait`).
- Every exception takes `bkl_lock` before it panics, so the panicking CPU holds the BKL forever.
- `smp_halt_others` sends a normal maskable IPI (0xF2). A CPU already spinning in `bkl_lock` has interrupts off and never takes it. This includes the BSP's own timer interrupt, which spins in `bkl_lock`.
- After 5 s, `frz_bkl_wait` on that CPU draws "NoovexOS is stuck (N s): CPU x waits for the kernel lock held by CPU y" at y=30, every second, over the top of the panic screen.
- `frz_bkl_wait` only goes quiet for `g_frz_hold`/`g_frz_sleep`. The panic code clears `g_nmiwd_on` but sets neither of those.
- Result: crash photos from the owner show a misleading "stuck" box instead of a clean red screen.

Patch, core/crash.c:274:
```
old: void crash_panic(regs_t* r,const char* msg,u64 rip,u64 rbp){ { extern volatile int g_nmiwd_on; g_nmiwd_on=0; }
new: void crash_panic(regs_t* r,const char* msg,u64 rip,u64 rbp){ { extern volatile int g_nmiwd_on; extern volatile int g_frz_hold; g_nmiwd_on=0; g_frz_hold=1; }
```
This also silences `freeze_tick` during the panic wait, which is what we want.

### 2. MEDIUM, diagnostic (owner's "no red box"): the NMI watchdog stays silent when CPU 0's ticks stop while interrupts are ON
core/crash.c:335, `if(r->rflags&0x200)return;` in `nmiwd_nmi`.
- If IRQ0 stops arriving with IF=1 on CPU 0, nothing ever reports it. Ways this can happen: a lost PIC EOI, IRQ0 masked, or the PIT reprogrammed. Every IRQ0 path I checked does send its EOI, including `jc_timer`, which EOIs before `jc_leave`.
- In that state `freeze_tick` never runs (it lives in the tick), and the NMI handler returns without a box. That matches "frozen, no box at all". The quiet cases in `nmiwd_check` already exclude polled-timer, sleep and hold states.
- Suggested patch: draw the box anyway, with a different first line.
```
old:     if(r->rflags&0x200)return;
new:     int nw_ifon=(r->rflags&0x200)!=0;
old:     { cx_buf b={g_nw_l[0],g_nw_l[0]+255}; cx_s(&b,"NoovexOS froze: CPU 0 has had interrupts off for 3 s (NMI watchdog, sent by another CPU)"); *b.p=0; }
new:     { cx_buf b={g_nw_l[0],g_nw_l[0]+255}; cx_s(&b,nw_ifon?"NoovexOS froze: CPU 0 timer ticks stopped for 3 s with interrupts ON (timer IRQ lost; NMI watchdog)":"NoovexOS froze: CPU 0 has had interrupts off for 3 s (NMI watchdog, sent by another CPU)"); *b.p=0; }
```
Confidence that this is the owner's actual cause: low (about 15%). It closes the only remaining blind spot of the two boxes, apart from a nested loop that keeps beating the heartbeat.

### 3. LOW-MEDIUM (35%): `ipi_send` (xAPIC mode) is not interrupt-safe
arch/x86_64/smp/smp_sched.c:30-32.
- Sending an IPI takes two register writes: ICR-high (0x310, destination) and ICR-low (0x300, send).
- Thread-context callers can run with IF=1: `smp_kick` from `nwk_wk_send` (main loop), `smp_tlb_shootdown` (igpu.c:40, xdisplay.c:38, acpi_dev.c:169), and `smp_kick_idle`. An interrupt between the two writes that sends its own IPI overwrites 0x310.
- The thread then sends its vector to the interrupt handler's target CPU: the wakeup is lost, or a TLB IPI goes to the wrong CPU.
- `smp_nmi_cpu` already uses `cpu_irq_save`; `ipi_send` does not. x2APIC mode is a single MSR write and is safe.
```
old:     for(int i=0;i<200000&&(lapic_rd(0x300)&(1u<<12));i++)cpu_pause();
old:     lapic_wr(0x310,g_cpu[cpu].apic_id<<24); lapic_wr(0x300,vec&0xFFu);
new:     u64 f=cpu_irq_save(); for(int i=0;i<200000&&(lapic_rd(0x300)&(1u<<12));i++)cpu_pause();
new:     lapic_wr(0x310,g_cpu[cpu].apic_id<<24); lapic_wr(0x300,vec&0xFFu); cpu_irq_restore(f);
```

### 4. LOW (25%): a slow AP shares its per-CPU block and stack with the next AP
arch/x86_64/smp/smp.c:93.
- If an AP does not report within the timeout, the code does `g_ncpu=next; continue;` without incrementing `next`.
- The next AP is then started with the same slot: same `percpu_setup(next)`, same 0x8FD8 id, same 0x8FE0 stack `g_ap_stacks[next]`.
- If the first AP arrives late, two CPUs run on one stack and one `struct pcpu`. The result is random corruption or a triple fault right after boot, with SMP on automatically.
```
old:         if(g_cores_online==before){ g_ncpu=next; continue; }
new:         if(g_cores_online==before){ next++; continue; }
```
This leaves the slot reserved with `online=0`. All IPI and scheduler loops check `g_cpu[i].online`, but please check `jc_pick`/assignment loops before applying.

### 5. LOW (60% that the bug is real, but it needs OOM): guard page inside a freed heap block causes a random kernel #PF later
apps/internet/browser/glue/fetch_queue.c:125-126, together with mem/pmm_vmm.c:228 (`mm_guard` has no undo).
- `nf_init` guards a page inside `f->stk`. If `f->scr` then fails to allocate, it `kfree`s `f->stk` with the page still unmapped.
- The next `kmalloc` that reuses this block faults inside unrelated kernel code. With `g_heap_poison` on, the `kfree` itself faults.
```
old:     { extern int mm_guard_range_bottom(void*, unsigned long); if (f->stk) mm_guard_range_bottom(f->stk, NF_STK); }
old:     if (!f->stk || !f->scr) { if (f->stk) kfree(f->stk); if (f->scr) kfree(f->scr); f->stk = 0; f->scr = 0; return 0; }
new:     if (!f->stk || !f->scr) { if (f->stk) kfree(f->stk); if (f->scr) kfree(f->scr); f->stk = 0; f->scr = 0; return 0; }
new:     { extern int mm_guard_range_bottom(void*, unsigned long); mm_guard_range_bottom(f->stk, NF_STK); }
```
Note: `mm_guard` also flushes only the local TLB, so on other CPUs the guard is not active until their TLB entry is evicted. That costs protection but does not crash.

### 6. LOW (20%): the NMI handler trusts GS
core/crash.c:333 (`nmiwd_nmi`, via isr_common).
- `isr_common` decides on swapgs from CS alone. An NMI that lands between `syscall` and `swapgs` in `lsys_entry`, or between `swapgs` and `iretq` on return to user, runs with the user GS base. `cpu_id()`/`PCPU` then read user memory, which can cause a #PF inside the NMI and turn it into a panic.
- The window is a couple of instructions, and the watchdog sends only one NMI. Hardening: check `cpu_rdmsr(0xC0000101)==(u64)&g_cpu[0]` before using `cpu_id()` in `nmiwd_nmi`, and return otherwise.

### 7. LOW, hardening: `bkl_try` is not interrupt-safe
arch/x86_64/cpu/percpu.c:41-45.
- An interrupt on the same CPU between the CAS succeeding and `g_bkl.owner=me` would take a new ticket and spin on itself forever with IF=0.
- The two current callers (PS/2 IRQ path, `freeze_tick`) already run with IF=0, so this is not live today. Wrap the CAS + owner store in `cpu_irq_save()`/`cpu_irq_restore()` before anyone calls it from thread context.

## Notes, not patched
- `bkl_yield()` inside `compose_present_body` (present_and_pump.c:65), `sh_rect`/`cpu_rect` (framebuffer.c:120/126) and xdisplay lets another CPU take the BKL in the middle of a present.
  - A native (non-lx) jc job on an AP falls through `jc_syscall` into native `syscall_dispatch`. Syscalls 40-45 and 90-94 there do `fill_rect`/`blit_all` from the AP and overwrite the static `g_dr[]` while the BSP is still looping over it.
  - The rects stay within the screen, so the effect is visual tearing only.
- Native syscall 16 (`sleep_ms`, legacy g_proc path) busy-waits with `rtw_delay_us` while holding the BKL with IF=0. A large value hangs the OS; the NMI box will catch it. jc jobs are not affected (case 16 is handled in `jc_syscall`).
- Several native syscalls write to unchecked user pointers (18, 52, 110, 120, 170) when reached from legacy g_proc processes. The jc path validates most of them in the first switch of `jc_syscall`.
- `us_init()` on each AP resets the global static `g_ufd[]` (ring3.c:51). This is harmless at boot but would close legacy user fds if APs start later.
