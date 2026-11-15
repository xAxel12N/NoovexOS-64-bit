# r3-review (v11.06, v11.05..HEAD: mem/ drivers/input/ graphics/)

## Queue index: how can r pass w?
- Writers of g_inq: in_btn_raw. USB calls it BARE with IRQs ON (hid_keyboard_mouse.c:73/85/108); ps2.c:111 from IRQ 1/12 drain (interrupts.c:92, under bkl_try, which nests on CPU 0 if the BKL is per-CPU recursive); in_buttons (locked, IRQs off only, no SMP lock).
- Reader: graphics/ui/input.c:288 (cli, in_mq_take loop). Clearers: in_mq_clear (main.c:327, shell usbhid.c:44, network_and_apps.c:246).
- g_mq_w++ / g_mq_r++ are plain load/add/store on volatile u32. On ONE CPU: a nested IRQ writer between USB's load and store of w only LOSES an event (w ends equal), and in_mq_clear vs take only moves r BACKWARD. Neither makes r>w.
- r>w needs: writer A loads w, then another writer adds >=2 AND the reader takes them, then A stores stale w. That needs two CPUs in the input code at once: kidle() (http.c:249, reached from syscall 51 waitkey/shell on an AP once the BKL is dropped via sched pk_bkl_restore), the xhci_mouse_poll "taking it over" path after 500 ticks (a stuck poller resumes beside the new one), xh_event_w -> hid_xfer (controller_init.c:235) from a non-poll xh_ctrl caller, or crash.c:226.
- Conclusion: possible but rare and needs SMP. A still left click on one CPU does NOT do it. I do not think this is the owner's freeze. With r>w the old loop is 2^32 steps with IRQs off (a few seconds) on EVERY pump, and in_mq_take then feeds 64 garbage button events per pump (ghost clicks). That looks like a freeze, so harden it anyway.

## Root-cause fix (HIGH): one lock, one producer API, a self-healing reader
drivers/input/input.c:
```c
static volatile int g_in_sl;
static inline u64 in_lock(void){ u64 f=cpu_irq_save(); while(__sync_lock_test_and_set(&g_in_sl,1)) __builtin_ia32_pause(); return f; }
static inline void in_unlock(u64 f){ __sync_lock_release(&g_in_sl); cpu_irq_restore(f); }
static void in_btn_locked(int l,int r,int m){ /* old in_btn_raw body */ }
void in_btn_raw(int l,int r,int m){ u64 f=in_lock(); in_btn_locked(l,r,m); in_unlock(f); }
void in_buttons(int l,int r,int m){ u64 f=in_lock(); in_btn_locked(l,r,m); mouse_evt=1; in_unlock(f); }
static inline int mq_sane(void){ u32 n=g_mq_w-g_mq_r; if(n>MQ_N){ g_mq_r=g_mq_w; g_mq_lost++; return 0; } return (int)n; }
int in_mq_take(int* dx,int* dy,int* b,u64* t){ u64 f=in_lock(); int ok=0; if(mq_sane()){ u32 i=g_mq_r%MQ_N; *dx=g_inq[i].dx; *dy=g_inq[i].dy; *b=g_inq[i].b; *t=g_inq[i].t; g_mq_r++; ok=1; } in_unlock(f); return ok; }
int in_mq_pending(void){ return (u32)(g_mq_w-g_mq_r)-1u<MQ_N; }
void in_mq_rebase_locked(int dx,int dy){ if(!dx&&!dy)return; int n=mq_sane(); for(u32 k=g_mq_r,e=g_mq_r+(u32)n;k!=e;k++){ g_inq[k%MQ_N].dx-=dx; g_inq[k%MQ_N].dy-=dy; } }
```
- The lock is not recursive: no caller may hold in_lock and call in_btn_raw again. in_buttons now uses in_btn_locked. ps2.c:111 and the USB sites stay as they are, because in_btn_raw now locks itself.
- input.c:288 and present_and_pump.c:127/135: hold the same lock instead of a bare cli. Export `u64 in_lock_x(void)/void in_unlock_x(u64)` wrappers and use them in place of cpu_irq_off()/cpu_irq_on(). Then the cli/sti problem below goes away too.
- The mouse_dx/mouse_dy/mouse_scroll += in the USB decode (hid_keyboard_mouse.c:72/84/106, ps2.c:110) are also unlocked RMW. That loses motion only. MEDIUM: route them through in_rel()/in_scroll().

## MEDIUM
- present_and_pump.c:127/135 and graphics/ui/input.c:288 do a bare cli/sti. If a caller had IRQs off (desk_nested_step/busy_pump reached from xh_event_w or another IRQ-off wait), the sti turns them on in the middle of that region. Patch: `u64 f=cpu_irq_save(); ... cpu_irq_restore(f);` (or the in_lock_x() above).
- heap_grow double growth: two contexts that both miss in kmalloc_raw each claim 64 MiB. That is bounded by HEAP_EX_MAX, so it only wastes memory. If g_nex reaches HEAP_EX_MAX between the claim and the install, the claimed run leaks. Patch: before installing, retry `p=kmalloc_raw(n)` under IRQ-off. If that works or g_nex==HEAP_EX_MAX, free the run (hp_free_run, or keep it as a spare g_ex slot) and do not install it.
- heap_grow half-initialized: safe. The arena becomes visible only after the zeroing, under IRQ-off, and IRQ-context kmalloc in the meantime only sees the old arenas. On SMP hp_alloc_run_nz has no lock other than the BKL (same as before).

## LOW
- kmalloc called with IRQs already off still zeroes 64 MiB with IRQs off (irqc noted this). Could skip the grow when !(irqf&0x200) and return 0, or log it.
- desk: the `g_marq&&!btn&&!mouse_btn_prev` clear is correct. Redraw only on motion is OK because release still sets changed=1 in the release branch. A still click draws nothing (w<2).
- in_mq_rebase_locked cap at MQ_N: correct but only hides the bug. It still rebases slots that are garbage when r>w. The fix above resyncs r=w instead.
