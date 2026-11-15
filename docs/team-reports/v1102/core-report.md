# core report (b-core)

## Changed (commit on b-core)
1. mem/heap.c kmalloc()/kfree(): heap list/free-list edits ran with IRQs enabled; reentry from an IRQ path on the same CPU was only counted (g_heap_reent), not prevented -> free-list corruption -> later hang/crash. BKL does not help (it is recursive on the same CPU). Now wrapped in cpu_irq_save/restore. Severity high (if any IRQ path allocates), confidence high that the change is safe. Check `memcheck` "re-entered" counter on hardware: nonzero means this was live.
2. sys/native/syscall.c:205 int 0x80 sleep(ms) (case 71): spun in cpu_idle while holding the BKL, stalling every other CPU (AP-run Linux jobs, any bkl_lock caller) for the full sleep; huge rbx = effectively forever. Now bkl_drop() around the wait, cli + bkl_take() after, ms clamped to 1 h. Severity medium, confidence high.
Both files compile with ccheck.sh. Not run in QEMU (15-min slot).

## Found, not changed
- sys/native/syscall.c (whole native int 0x80 ABI, e.g. :171, :134-139): user pointers dereferenced with no range/mapping check -> ring3 can crash or write kernel memory. Large fix (needs a copy_from_user helper), not a freeze candidate for the Start-menu bug. Severity medium (security), confidence high.
- arch/x86_64/interrupts/interrupts.c:78-80: spurious PIC IRQ7/IRQ15 (vectors 39/47) get a normal EOI; IRQ15 spurious should EOI master only, IRQ7 spurious none. Low severity.
- arch/x86_64/interrupts/interrupts.c:66-75: preempt path saves `r` into g_proc ctx even when the interrupted code was ring 0 (cs&3==0), and k_longjmp(g_preempt_kern) at :71 leaves the ISR with BKL depth from isr_dispatch:92 un-unwound. Needs owner review; low-medium confidence.
- arch/x86_64/cpu/percpu.c:28 bkl_lock spins with IRQs disabled; any path that holds BKL and waits for another CPU's IRQ/IPI completion (smp_tlb_sync has a timeout, so OK today) would deadlock. Note for future code.
- mem/heap.c kfree: hblk_ok/used/guard checks run before the IRQ-off section (benign TOCTOU).
Nothing in core/ found that explains the Start-menu freeze directly; prime suspect remains the USB path (not my slice).
