# Part 4: CPU, interrupts, timers and multi-core

This part describes everything between the bare CPU and the scheduler: the inline CPU helpers, descriptor tables, the interrupt and system-call entry paths, the legacy PIC/PIT, the high-resolution timers, and the symmetric-multiprocessing (SMP) layer with its big kernel lock. All paths are relative to the repository root. Each `.c` file is its own compilation unit with an interface header under `include/nvx/` (Part 1, section 3); functions another unit calls are declared there, everything else stays `static`. The four assembly files (`boot64.S`, `isr64.S`, `userspace.S`, `ap_trampoline.S`) are separate objects as before.

Include order matters for this part. `arch/x86_64/cpu/cpu.h` and `arch/x86_64/cpu/percpu.c` come first (`kernel64.c` lines 25-26), so every later file can use `PCPU`, `cpu_id()` and the BKL. `ring3.c` and `smp.c` come early, `interrupts.c` and `core/panic.c` come in the middle, `hrtimer.c` comes late, and `smp_sched.c` and `cpu_init.c` come last, just before `core/init/main.c`.

---

## 4.1 `arch/x86_64/cpu/cpu.h`, the CPU interface

`cpu.h` is the only header portable code uses for privileged instructions. `tools/check.py` rejects inline assembly outside `arch/`, and `tools/tests/linux/cpu_host.h` provides host stand-ins for the Linux-hosted test build. Every helper is `static inline`.

| Helper | Instruction(s) | Notes / main users |
|---|---|---|
| `cpu_irq_on` / `cpu_irq_off` | `sti` / `cli` | memory clobber |
| `cpu_pause` | `pause` | spin loops (BKL, IPI busy-wait) |
| `cpu_halt` | `hlt` | main loop idle (between `bkl_drop`/`bkl_take`), panic |
| `cpu_idle` | `sti; hlt` | atomic "enable and wait": an IRQ cannot slip in between |
| `cpu_stop` | `cli; hlt` | parks a core for good (`ap_main`, `IPI_HALT`) |
| `cpu_wmb` | `sfence` | store barrier |
| `cpu_irq_save` | `pushfq; popq; cli` | returns RFLAGS |
| `cpu_irq_restore(f)` | `sti` if `f & 0x200` | restores IF only |
| `cpu_get_pt` / `cpu_set_pt` | `mov cr3` | writing CR3 flushes the TLB (no CR4.PGE is set anywhere, so there are no global pages) |
| `cpu_flush_page(v)` | `invlpg (v)` | `mem/pmm_vmm.c` `vm_map`/`vm_unmap`/COW, `sys/linux/linux.c` munmap |
| `cpu_cache_flush` | `wbinvd` | `apps/system/files_classic.c` |
| `cpu_tsc` | `rdtsc` | |
| `cpu_rand32` / `cpu_seed32` | `rdrand` / `rdseed` + `setc` | return CF; used by `net/http/http.c`, `nvxcrypto2.c` |
| `cpu_cpuid(leaf,sub,...)` | `cpuid` | |
| `cpu_sp` | `mov rsp` | |
| `cpu_fpu_save` / `cpu_fpu_restore` | `fxsave` / `fxrstor` | per-process `fx` area in `jc_enter` / `jc_leave` |
| `cpu_bios_ebda_seg` | `movw 0x40E` | BDA EBDA segment, `core/power.c` RSDP scan |
| `cpu_triple_fault` | `lidt` of a null IDT, then `int3` | last-resort reboot (`core/power.c`) |
| `cpu_rdmsr` / `cpu_wrmsr` | `rdmsr` / `wrmsr` | |
| `cpu_set_fsbase(v)` | `wrmsr 0xC0000100` (FS_BASE) | `arch_prctl(ARCH_SET_FS)` in `sys/linux/linux.c` case 158 |
| `cpu_percpu()` | `mov %gs:16, ...` | reads `pcpu.self` (see 4.9) |
| `cpu_set_percpu(p)` | `wrmsr 0xC0000101 = p`, `wrmsr 0xC0000102 = 0` | GS_BASE = per-CPU block, KERNEL_GS_BASE = 0 |
| `cpu_cr2` | `mov cr2` | page-fault address |
| `cpu_copy(d,s,n)` | `rep movsb` | `fs/vfs/ramfs.c` `kmemcpy` |

`cpu_set_percpu` puts the per-CPU pointer in the *active* GS base and zero in the *shadow* one. The first `swapgs` on the way to ring 3 swaps them, so user code always runs with GS base 0 and the kernel pointer waits in KERNEL_GS_BASE until the next entry.

---

## 4.2 `arch/x86_64/cpu/cpu_init.c`

### `arch_fpu_init`

The BSP calls this early in `kmain` (`core/init/main.c`, after `percpu_init_bsp` and `mm_load`), and every AP calls it again in `ap_entry_c`.

```c
cr0 &= ~(1<<2);            /* EM=0: x87/SSE not emulated            */
cr0 |=  (1<<1)|(1<<16);    /* MP=1, WP=1 (supervisor honours R/O)   */
cr4 |=  (1<<9)|(1<<10);    /* OSFXSR, OSXMMEXCPT: SSE + fxsave on   */
g_ssse3 = (cpuid(1).ecx >> 9) & 1;
fninit; ldmxcsr 0x1F80;    /* all SSE exceptions masked             */
```

Bit 16 is CR0.WP (Write Protect), not NE. With WP set, kernel writes to read-only user pages also fault, which the copy-on-write logic in `mem/pmm_vmm.c` relies on. `g_ssse3` selects SIMD paths (`simd.h`). XSAVE and AVX are not enabled. Only `fxsave` state is managed.

### `apic_virtual_wire`

This runs only on UEFI boots: `kmain` checks the boot-info magic `*(u32*)0x502C == 0x49464555` ("UEFI"). It reads IA32_APIC_BASE (MSR `0x1B`). If the APIC is globally disabled (bit 11 clear), it does nothing. Otherwise it programs the local APIC as a legacy "virtual wire", so 8259 interrupts still reach the BSP:

| Register | xAPIC MMIO offset | x2APIC MSR | Value |
|---|---|---|---|
| TPR | `0x80` | `0x808` | 0 |
| Spurious vector (SVR) | `0xF0` | `0x80F` | `0x1FF` (enable + vector 0xFF) |
| LVT LINT0 | `0x350` | `0x835` | `0x700` (ExtINT) |
| LVT LINT1 | `0x360` | `0x836` | `0x400` (NMI) |
| LVT Timer | `0x320` | `0x832` | OR `0x10000` (masked) |

If bit 10 (x2APIC) is set, the MSR path is used. Otherwise the MMIO base must lie below 4 GB.

### `arch_idt_init`

```c
for(i=0;i<48;i++)   set_gate(i, isr_stubs[i]);   /* exceptions + PIC IRQs */
for(i=48;i<256;i++) set_gate(i, isr_ignore);     /* everything else      */
lidt {sizeof(idt)-1, idt}
```

Other gates are filled in later (see 4.4). There is one IDT, `static struct idt_entry idt[256]` in `interrupts.c`, and all cores share it. APs load it with `idt_load()` (`smp_sched.c`).

---

## 4.3 GDT, selectors, TSS and IST stacks (`arch/x86_64/user/ring3.c`)

At boot the kernel runs on the bootloader's GDT. `us_init()` installs the final GDT the first time anything needs ring 3. It is called from `ring3_run`, `elf.c`, `sched.c`, `jobs.c` (`jc_enter`), `sys/native/syscall.c`, the shell, and every AP's `ap_entry_c`. `us_init` returns early if `g_us_ready` is set. Because `g_us_ready`, `g_gdt`, `g_gdtp` and `g_tss` are all per-CPU macros (4.9), each core builds and loads its own GDT and TSS exactly once.

`gdt_set(i, base, limit, access, flags)` packs a legacy 8-byte descriptor into `g_gdt[i]`.

| Index | Selector | Access | Flags | Meaning |
|---|---|---|---|---|
| 0 | `0x00` |, |, | null |
| 1 | `0x08` | `0x9A` | `0xA` (G, L) | kernel code, 64-bit |
| 2 | `0x10` | `0x92` | `0xC` (G, D/B) | kernel data |
| 3 | `0x18` to `0x1B` with RPL 3 | `0xFA` | `0xA` | user code, DPL 3, 64-bit |
| 4 | `0x20` to `0x23` with RPL 3 | `0xF2` | `0xC` | user data, DPL 3 |
| 5-6 | `0x28` | `0x89` |, | 64-bit available TSS (16-byte descriptor; slot 6 holds base bits 63:32) |

`gdt_flush` (`userspace.S`) runs `lgdt`, reloads DS/ES/SS with `0x10`, and far-returns (`lretq`) through CS `0x08`. `tss_flush(0x28)` runs `ltr`.

The TSS is `struct tss64` in `percpu.h` (packed: `rsp0..2`, `ist[7]`, `iomap`). `us_init` zeroes it and sets these fields:

| Field | Value | Purpose |
|---|---|---|
| `rsp0` | top of `g_kstack` (per-CPU 16 KB) | ring 3 to 0 stack; `jc_enter` later points it at `g_jkstack` |
| `iomap` | `sizeof(g_tss)` | no I/O permission bitmap, so all ring-3 port I/O faults |
| `ist[0]` (IST1) | top of `g_ist_nmi` (4 KB) | NMI, vector 2 |
| `ist[1]` (IST2) | top of `g_ist_df` (8 KB) | double fault, vector 8 |

After that, `us_init` calls `idt_set128()`, `lsys_init()`, `idt_use_ist(2,1)` and `idt_use_ist(8,2)`. The IDT is shared, so the IST indices are global, but each core's TSS points them at its own stacks.

---

## 4.4 IDT gates and the vector map (`arch/x86_64/interrupts/interrupts.c`)

```c
struct idt_entry{ u16 lo; u16 sel; u8 ist; u8 attr; u16 mid; u32 hi; u32 z; };
static void set_gate(int n,void*h){ ... sel=0x08; ist=0; attr=0x8E; ... }  /* P, DPL0, interrupt gate */
static void idt_set128(void){ ... attr=0xEE; }                             /* P, DPL3, interrupt gate */
static void idt_use_ist(int n,int ist){ idt[n].ist=(u8)ist; }
```

Every gate is an *interrupt* gate (type `0xE`), so the CPU clears IF on entry. The only DPL-3 gate is `int 0x80`. A software `int n` from ring 3 to any other vector raises `#GP`.

| Vector(s) | Stub | Installed by | Handling |
|---|---|---|---|
| 0-31 | `isr0`-`isr31` | `arch_idt_init` | user-mode PF to `vm_user_pf`; other user faults to `jc_fault`; kernel faults to `panic` (RSOD) |
| 2 (NMI) | `isr2`, IST1 | `us_init` | `isr_dispatch` returns at once (ignored) |
| 8 (#DF) | `isr8`, IST2 | `us_init` | panic |
| 32-47 | `isr32`-`isr47` | `arch_idt_init` | PIC IRQ 0-15 |
| `0x40` | `isr_lapic_timer` | `kmain`: `set_gate(HR_LAPIC_VEC,...)` | BSP: `hr_lapic_isr`; AP: `ap_timer` |
| `0x80` | `isr128` (DPL 3) | `us_init` to `idt_set128` | legacy NoovexOS syscall ABI |
| `0xF0` | `isr_ipi_resched` | `smp_enable` | `IPI_RESCHED` |
| `0xF1` | `isr_ipi_tlb` | `smp_enable` | `IPI_TLB` |
| `0xF2` | `isr_ipi_halt` | `smp_enable` | `IPI_HALT` |
| all others | `isr_ignore` (pushes 255) | `arch_idt_init` | no EOI; also catches the APIC spurious vector 0xFF |

### Stubs (`boot/isr64.S`)

`STUB_NOERR n` pushes a dummy error code 0 and then `n`. `STUB_ERR n` pushes only `n`, because the CPU already pushed an error code. The error-code vectors are 8, 10, 11, 12, 13, 14, 17 and 21. Vectors 29 (#VC) and 30 (#SX) are AMD-specific and push an error code on hardware, but this table uses `STUB_NOERR` for them, so their frame would be off by one slot. Neither vector is expected in practice. `isr_stubs[48]` is the `.rodata` table that `arch_idt_init` reads.

---

## 4.5 Interrupt frame and `isr_common`

```c
/* interrupts.c - "order must match the asm" */
typedef struct {
    u64 r15,r14,r13,r12,r11,r10,r9,r8,rbp,rdi,rsi,rdx,rcx,rbx,rax;   /* pushed by isr_common */
    u64 vector,errcode;                                              /* pushed by the stub   */
    u64 rip,cs,rflags,rsp,ss;                                        /* pushed by the CPU    */
} regs_t;
```

| Offset | Field | Offset | Field |
|---|---|---|---|
| 0x00 | r15 | 0x60 | rcx |
| 0x08 | r14 | 0x68 | rbx |
| 0x10 | r13 | 0x70 | rax |
| 0x18 | r12 | 0x78 | vector |
| 0x20 | r11 | 0x80 | errcode |
| 0x28 | r10 | 0x88 | rip |
| 0x30 | r9 | 0x90 | cs |
| 0x38 | r8 | 0x98 | rflags |
| 0x40 | rbp | 0xA0 | rsp |
| 0x48 | rdi | 0xA8 | ss |
| 0x50 | rsi | | |
| 0x58 | rdx | | |

```
 CPU (ring3->0: loads TSS.rsp0, or IST1/IST2 for NMI/#DF)
   push ss, rsp, rflags, cs, rip [, errcode]
 stub:          [push 0] ; push vector ; jmp isr_common
 isr_common:    testb $3, 24(%rsp)      ; cs RPL at vec(0) err(8) rip(16) cs(24)
                jz 1f ; swapgs          ; only when coming from ring 3
             1: push rax..r15           ; regs_t complete, rdi = rsp
                mov rsp,rbp ; and $-16,rsp ; call isr_dispatch ; mov rbp,rsp
                pop r15..rax
                cli
                testb $3, 24(%rsp) ; jz 1f ; swapgs   ; only when returning to ring 3
             1: add $16,%rsp ; iretq
```

swapgs rule. GS is swapped only when the saved CS has RPL 3, on both entry and exit. The exit check reads the CS in the frame *after* `isr_dispatch`, and the scheduler may have rewritten that frame. So a frame changed to target another ring-3 context still gets the correct swap. Kernel-to-kernel interrupts never swap, so GS base always holds the `pcpu` pointer while kernel code runs.

`isr_common` does not run `cld`. The C code is built for SysV and expects DF clear, and DF is clear in every context this kernel creates.

---

## 4.6 `isr_dispatch` and `isr_body`

```c
void isr_dispatch(regs_t*r){
    if(r->vector==2)return;                                   /* NMI: ignore            */
    u64 v=r->vector; int user=(r->cs&3)==3;
    if(v>=0xF0&&v<=0xF2){ smp_ipi(r,user); return; }          /* IPIs: lock-free        */
    if(v==HR_LAPIC_VEC&&cpu_id()!=0){ ap_timer(r,user); return; }
    if(v==HR_LAPIC_VEC&&!user){ hr_lapic_isr(); return; }     /* BSP one-shot wake      */
    if(v==32)g_ticks++;                                       /* PIT tick, before lock  */
    bkl_lock(); isr_body(r); bkl_unlock();
}
```

`g_ticks` is incremented *before* the lock is taken, so the 100 Hz clock keeps counting while another core holds the BKL. IPIs and AP timer ticks never block on the lock up front. They take it only when they are about to preempt a user process.

`isr_body` (called with the BKL held):

1. Vector 14 and `vm_user_pf(r)` (`core/proc/jobs.c`). If a job is running, CR2 is in the user window and `vm_fault_user` resolves the fault (lazy allocation or COW), the handler returns and the instruction is retried. A result of `-2` (out of memory) kills the job with signal 9.
2. Other exceptions (< 32). If `g_js_running` is set and the fault came from ring 3, `jc_fault(r)` runs. It saves the frame into the job's `ctx` and maps the vector to a signal: 0/16/19 to SIGFPE (8), 6 to SIGILL (4), 1/3 to SIGTRAP (5), anything else to SIGSEGV (11). If the signal is unhandled or masked, the job is finished; otherwise the signal is marked pending. Then `jc_leave()` runs. Any kernel-mode exception calls `panic(r,0)`.
3. `HR_LAPIC_VEC` (BSP, user mode) to `hr_lapic_isr()`.
4. IRQ0 (PIT). If a job is in ring 3, `jc_timer(r)` runs. It preempts after two ticks (`g_ticks - g_js_slice >= 2`): it saves `ctx`, sends `pic_eoi(0)` and calls `jc_leave()`. The older demo scheduler (`g_preempt_on`, used by `preempt`/`ipc`/`compositor`) round-robins over `g_proc[]`. It copies the frame into `g_proc[cur].ctx`, sets `g_tss.rsp0`, loads the next CR3 and overwrites `*r`, so `isr_common` irets into the next process. If no process is left, it calls `k_longjmp(g_preempt_kern,1)`.
5. IRQ1 / IRQ12 to `ps2_byte()` (keyboard and mouse share one byte reader in `drivers/input/ps2/ps2.c`).
6. EOI. `pic_eoi(irq)` runs unless the vector is 255 (`isr_ignore`). EOI is sent to the slave for IRQ at least 8, then always to the master.

### RSOD (`core/panic.c`)

`panic(regs_t*, const char*)` does the following in order:

1. Calls `smp_halt_others()`, which sends `IPI_HALT` to every other core.
2. Disables interrupts.
3. Guards against recursion with `static in_panic`. A fault during the panic just halts.
4. Fills `SCENE` with a red gradient and prints `:(`, the exception name from `EXC[32]`, the vector, error code, RIP, RSP, RFLAGS and all 16 GPRs. When called with `r==0` (or a non-exception), it prints `msg` instead.
5. Calls `blit_all()` and halts forever.

The shell `panic` command uses the message path.

---

## 4.7 PIC and PIT

```c
static void pic_remap(void){
    ICW1 0x11 -> 0x20/0xA0;  ICW2 0x20 / 0x28;  ICW3 0x04 / 0x02;  ICW4 0x01 / 0x01;
    outb(0x21,0xF8);   /* master: IRQ0 timer, IRQ1 kbd, IRQ2 cascade unmasked */
    outb(0xA1,0xEF);   /* slave:  IRQ12 PS/2 mouse only                       */
}
static void pit_init(u32 hz){ div=1193182/hz; outb(0x43,0x36); lo; hi; }  /* ch0, mode 3 */
```

`kmain` runs `pic_remap(); pit_init(100);`, so `g_ticks` advances at 100 Hz (10 ms per tick). `g_ticks` is a `volatile u64` defined in `net/http/http.c` and declared in `include/nvx/net/http/http.h`. Everything else (USB, NVMe, Wi-Fi) is polled, so only IRQ 0, 1, 2 and 12 are unmasked.

After `cpu_irq_on()`, `kmain` checks that ticks are actually arriving. If `g_ticks` does not move within 3,000,000 pauses, it clears `g_ticks_live` and the main loop calls `pit_poll_tick()` instead (`core/init/main.c`). That function latches channel 0 and counts reloads.

Boot order (`core/init/main.c`):

```
percpu_init_bsp -> arch_fpu_init -> ... -> arch_idt_init -> [UEFI] apic_virtual_wire
 -> pic_remap; pit_init(100) -> set_gate(0x40,isr_lapic_timer); hr_init()
 -> PS/2, xHCI, touchpad, ACPI AML ... -> cpu_irq_on -> tick check -> smp_boot_auto_c -> main loop
```

---

## 4.8 High-resolution timers (`arch/x86_64/time/hrtimer.c`)

All state lives in `static struct {...} g_hr`. `src` is the clock source (0 = PIT ticks, 1 = HPET, 2 = TSC). `ref` records what the calibration was measured against (0 nothing, 1 HPET, 2 PIT channel 2, 3 CPUID 0x15, 4 CPUID 0x16), and `timers_cmd` in `shell/commands.c` shows these names. `why[48]` holds the reason the HPET was rejected.

### `hr_init` sequence

1. CPUID. `0x80000007` EDX bit 8 sets `tsc_inv` (invariant TSC). Leaf 1 EDX bit 4 sets `tsc_ok`.
2. `hr_hpet_init`. It finds `HPET` with `acpi_find_table`. It requires length at least 56 and an address space of 0 (memory), with the address at table offset 44 non-zero and 8-byte aligned, and maps it with `acpi_hi_map` (fails above 512 GB). It reads capabilities: the period in femtoseconds is `cap>>32` and must be between 1 and 10^8, `hpet_64` is bit 13, and `ntim` is bits 12:8 plus 1. It sets ENABLE_CNF (and clears LEG_RT) in config `+0x10` and checks that main counter `+0xF0` moves within 20,000 reads. Then `hpet_mult = (fs<<32)/10^6`. A 32-bit counter is extended to 64 bits in `hr_hpet_count` by detecting wraps (`hpet_hi += 2^32`) with interrupts disabled.
3. `hr_lapic_init`. It reads MSR `0x1B`. A globally disabled APIC is left alone. With x2APIC (bit 10), registers are reached as MSR `0x800 + off/16`. Otherwise MMIO at `g_lapic_base` is used, which must be below 4 GB. If the SVR enable bit is clear, it does the same virtual-wire setup as 4.2. It sets the timer LVT to `0x10000|0x40` (masked), divide `0x3E0 = 3` (/16), and initial count 0.
4. `hr_calibrate`. It takes the best of 3 runs with interrupts off. The local APIC counter starts at `0xFFFFFFFF`, and TSC and APIC current count (`0x390`) are sampled across a reference window:
   - with an HPET: wait until 20,000,000 ns of HPET time have passed;
   - without one: PIT channel 2 one-shot through port `0x61`/`0x42`, count `HR_PIT_COUNT = 59659`, which is `HR_PIT_WIN_NS = 49,999,930 ns` (59659 / 1,193,182 Hz). Wait for OUT2 (bit 5 of port `0x61`).

   The run with the smallest TSC-per-window wins. Then `tsc_hz = dt*10^9/win` and `lapic_hz = dl*10^9/win`, using `hr_muldiv`, which avoids overflow.
5. Fallback `hr_cpuid_tsc`. CPUID `0x15` gives `tsc_hz = ecx*ebx/eax`, and `lapic_hz = ecx/16` if it is still unknown. CPUID `0x16` gives `tsc_hz = eax[15:0] MHz`.
6. Accept. If `tsc_hz >= 10 MHz`, then `tsc_mult = (10^9<<32)/tsc_hz` and `tsc0 = rdtsc`. If `lapic_hz < 100 kHz`, the APIC timer is disabled.
7. Pick `src`. Invariant TSC first, then HPET, then a non-invariant TSC, then PIT ticks. The choice is logged ("timers: clock = ...").

### API

| Function | Behaviour |
|---|---|
| `hr_ns()` | `src==2`: `((rdtsc-tsc0)*tsc_mult)>>32` (128-bit multiply); `src==1`: `hr_hpet_ns()`; else `g_ticks*10,000,000` |
| `hr_us()`, `hr_precise()` | `hr_ns()/1000`; `src!=0` |
| `hr_delay_us(us)` | busy-waits on `hr_ns()` with `pause`, bounded by an iteration limit; without a precise clock, `us` x `io_wait()` |
| `hr_delay_ms(ms)` | repeated `hr_delay_us(1000)` |
| `hr_wake_at(ns)` | arms an APIC one-shot for `min(ns-now, 1 s)` (`0x320 = 0x40`, unmasked); returns 1 if armed |
| `hr_wake_cancel()` | count 0, mask the LVT |
| `hr_lapic_periodic(hz)` | periodic mode (`bit 17`); defined but currently unused (APs program their own) |
| `hr_lapic_isr()` | `lapic_irqs++`, EOI (`0xB0`) |

The main loop uses `hr_wake_at` on the desktop when the cursor moved. It arms a wake for the next 60 Hz frame (`frame_ns = 16,666,667`), then runs `bkl_drop(); cpu_halt(); bkl_take()`, so the BSP sleeps until either the PIT tick or the precise wake. The `timers` and `timertest` shell commands report and cross-check the sources.

`hr_ns` uses the TSC base of the BSP (`tsc0`). Code on APs that calls it (`ap_udelay`, `hr_delay_us` in `smp_disable`) assumes the TSCs are synchronised. That holds on invariant-TSC hardware.

---

## 4.9 Per-CPU state (`arch/x86_64/cpu/percpu.c`)

```c
#define NCPU 16
struct pcpu {
    u64 kstack_top;          /* +0   syscall stack (lsys_entry: %gs:0)          */
    u64 user_rsp;            /* +8   user rsp scratch (lsys_entry: %gs:8)       */
    struct pcpu* self;       /* +16  read by cpu_percpu(): %gs:16               */
    u64 fsbase;              /* +24  user FS base (jc_resume: %gs:24)           */
    int id; u32 apic_id; int jcur; volatile int js_running;
    u64 js_kern[8]; u64 js_slice;
    int dcwd, uid, gid, umask; u64 ubrk, mmap_next; int us_ready;
    volatile int online, stop, parked, tlb_pending, in_user;
    volatile u64 ticks, user_ticks, idle_ticks, switches, syscalls, ipis;
    struct tss64 tss; u64 gdt[7]; struct{u16 limit; u64 base;} gdtp;
    u8 kstack[16384], jkstack[16384], ist_nmi[4096], ist_df[8192];
};
static struct pcpu g_cpu[NCPU] __attribute__((aligned(64)));
#define PCPU ((struct pcpu*)cpu_percpu())
static inline int cpu_id(void){ return PCPU->id; }
```

`_Static_assert`s fix the first four offsets (0, 8, 16, 24), because the assembly addresses them directly. `PCPU` is one `mov %gs:16` instruction. GS base points at the struct, and `self` points back at it.

The old global names are kept as macros over `PCPU->...`, so pre-SMP code became per-CPU without being rewritten:

| Macro | Field | Macro | Field |
|---|---|---|---|
| `g_jcur` | `jcur` (running job, -1 none) | `g_us_ready` | `us_ready` |
| `g_js_running` | `js_running` | `lsys_kstack` | `kstack_top` |
| `g_js_kern` | `js_kern` (setjmp buffer) | `lsys_fsbase` | `fsbase` |
| `g_js_slice` | `js_slice` | `g_tss` | `tss` |
| `g_dcwd`, `g_uid`, `g_gid`, `g_umask` | current credentials and cwd | `g_gdt`, `g_gdtp` | `gdt`, `gdtp` |
| `g_ubrk`, `g_mmap_next` | brk and mmap cursor | `g_kstack`, `g_jkstack` | stacks |
| | | `g_ist_nmi`, `g_ist_df` | IST stacks |

`percpu_setup(i, apic)` sets `self`, `id`, `apic_id`, `jcur=-1`, `umask=022`, `ubrk=0x40002000000`, `mmap_next=0x40004000000` and `kstack_top`. `percpu_init_bsp()` sets up slot 0 with APIC ID 0 (corrected later in `smp_start_aps`), loads GS and marks it online. It is the first statement after BSS clearing in `kmain`, so `PCPU` is valid everywhere afterwards.

---

## 4.10 The big kernel lock (`arch/x86_64/cpu/percpu.c`)

```c
struct bkl { volatile u32 next, serving; volatile int owner; volatile int depth; };
static struct bkl g_bkl={1,0,0,1};       /* CPU 0 owns it from boot, depth 1 */
```

- Ticket lock. `bkl_lock` takes a ticket with `__atomic_fetch_add(&next,1)` and spins (`pause`) until `serving == ticket` (acquire). Tickets make it FIFO-fair.
- Recursion. If `owner == cpu_id()`, it only increments `depth`. Nested entry is common, for example a syscall that ends up in code that locks again.
- Interrupts while spinning. `cpu_irq_save` disables interrupts during the spin and `cpu_irq_restore` restores the previous IF afterwards.
- Deferred TLB flush. After acquiring, if `PCPU->tlb_pending` is set, it clears it and reloads CR3.
- Unlock. `bkl_unlock` ignores callers that are not the owner, decrements `depth`, and at zero sets `owner=-1` and does `serving+1` (release).
- Helpers. `bkl_held()` tells whether this CPU owns it. `bkl_drop()` releases fully and returns the old depth, and `bkl_take(d)` re-acquires and restores that depth. Together they hand the lock over around blocking waits. `bkl_yield()` (only when SMP is on, the lock is held at depth 1, and someone is queued, i.e. `next != serving+1`) unlocks, pauses 64 times, and relocks.

Where it is taken or dropped.

| Site | Action |
|---|---|
| `isr_dispatch` | around `isr_body` (all PIC IRQs, exceptions, BSP-user LAPIC) |
| `lsys_c`, `isr128_c` | around `linux_syscall` / `syscall_dispatch` (also `PCPU->syscalls++`) |
| `ap_loop` | around `jc_pick` + `jc_enter` |
| `smp_ipi` (RESCHED), `ap_timer` | `bkl_lock()` just before `jc_preempt` (released by the `bkl_take` in `jc_enter`) |
| `jc_enter` (`jobs.c`) | `bd=bkl_drop()` right before `jc_resume`; `bkl_take(bd)` after the longjmp back |
| `kmain` main loop | `bkl_drop(); cpu_halt(); bkl_take()` when idle; `bkl_yield()` once per iteration |
| `smp_enable` / `smp_disable` | dropped around `smp_start_aps` and the 5 ms park waits |

The model: user code runs without the lock, kernel code runs with it. A job enters ring 3 with the BKL released. The next kernel entry on that core (interrupt, `syscall`, `int 0x80`, fault) acquires it. When a job leaves (`jc_leave` to `k_longjmp(g_js_kern)`), the code is still inside the handler that took the lock. `bkl_take(bd)` then finds `owner == me`, increments, and overwrites `depth` with the saved `bd`, so the nesting count from before `jc_enter` is restored exactly.

---

## 4.11 System calls: `int 0x80` and `syscall`

### MSRs (`lsys_init`, run per CPU from `us_init`)

| MSR | Address | Value | Meaning |
|---|---|---|---|
| EFER | `0xC0000080` | `|= 1` | SCE: enable `syscall`/`sysret` |
| STAR | `0xC0000081` | `(0x08<<32) | (0x10<<48)` | SYSCALL CS=`0x08`, SS=`0x10` |
| LSTAR | `0xC0000082` | `lsys_entry` | 64-bit entry point |
| FMASK | `0xC0000084` | `0x47700` | clears TF, IF, DF, IOPL, NT, AC on entry |
| FS_BASE | `0xC0000100` | per job | `jc_resume`, `cpu_set_fsbase` |
| GS_BASE / KERNEL_GS_BASE | `0xC0000101/2` | `pcpu` / 0 | `cpu_set_percpu` |

`lsys_init` also sets `lsys_kstack = top of g_kstack`. STAR[63:48] = `0x10` would make `sysretq` load CS = `0x23` and SS = `0x1B`, which is the wrong way round for this GDT. That does not matter because the kernel never runs `sysret`: both syscall paths return with `iretq`.

### `lsys_entry` (Linux ABI, `userspace.S`)

```
syscall:  rcx=user rip, r11=user rflags, CS/SS from STAR, IF=0 (FMASK)
lsys_entry:
   swapgs                        ; GS -> pcpu
   mov %rsp,%gs:8                ; pcpu.user_rsp
   mov %gs:0,%rsp                ; pcpu.kstack_top (= jkstack top while a job runs)
   push $0x23 ; push %gs:8 ; push %r11 ; push $0x1B ; push %rcx   ; fake iret frame
   push rax..r15                 ; struct sysregs
   mov %rsp,%rdi ; cld ; call lsys_c        ; bkl_lock; linux_syscall; bkl_unlock
   pop r15..rax ; cli ; swapgs ; iretq
```

The fake frame is the same shape `isr128` produces: `struct sysregs` (15 GPRs, same order as the top of `regs_t`) followed directly by the 5-word iret frame. Code that blocks, forks or delivers signals can therefore treat both entry types the same way. 20 pushes on a 16-aligned stack keep the call aligned.

### `isr128` (legacy NoovexOS ABI, number in `rax`)

`isr128` checks `testb $3, 8(%rsp)` (CS sits at +8 because there is no vector or error code) and swaps GS when coming from ring 3. It pushes the 15 GPRs and calls `isr128_c` to `syscall_dispatch`. On the way out it pops, runs `cli`, swaps GS back if returning to ring 3, and does `iretq`. Because the gate is an interrupt gate, IF is already 0.

### Entering and resuming ring 3

| Routine | Use |
|---|---|
| `enter_ring3(rip,rsp)` | `cli`, DS/ES=`0x23`, push SS `0x23`, RSP, RFLAGS `0x202`, CS `0x1B`, RIP; `swapgs; iretq` (`ring3_run` demo) |
| `enter_ring3_args(rip,rsp,argc,argv)` | same, with `rdi=argc`, `rsi=argv` |
| `user_trampoline` / `user_trampoline_fork` | first run of a `sched_swap`-based process (fork variant zeroes `rax`) |
| `jc_resume(regs_t* ctx)` | job-control resume (below) |
| `k_setjmp` / `k_longjmp` | save/restore rbx, rbp, r12-r15, rsp, return address; `longjmp(...,0)` returns 1 |
| `sched_swap(save,load)` | cooperative kernel-stack switch |

```
jc_resume(ctx):  cli ; DS=ES=FS=0x23
                 FS_BASE <- %gs:24 (pcpu.fsbase)    ; still kernel GS here
                 rsp <- ctx ; pop r15..rax ; add $16 (vector,errcode)
                 swapgs ; iretq                    ; iret frame = ctx->rip..ss
```

`jc_enter` (`core/proc/jobs.c`) prepares a resume this way:

1. Points `g_tss.rsp0` and `lsys_kstack` at the top of `g_jkstack`, sets `lsys_fsbase`, `g_js_slice`, `p->runcpu = cpu_id()+1` and `switches++`.
2. Disables interrupts and calls `k_setjmp(g_js_kern)`.
3. On the first return: `fxrstor`, load the job's CR3, `bkl_drop`, `jc_resume`.

Any later kernel entry that decides to leave the job calls `jc_leave()`. It saves FPU state (`fxsave`), switches back to the kernel CR3 and longjmps to `g_js_kern`.

---

## 4.12 Multi-core discovery (`arch/x86_64/smp/smp.c`)

- `acpi_count_cores()` is the older, read-only scanner. It searches `0xE0000-0xFFFFF` for `"RSD PTR "`, follows the XSDT (revision at least 2) or the RSDT (both must be below `0xF0000000`), finds `APIC`, and counts MADT type-0 entries with the enabled flag. It cannot see a UEFI RSDP outside the BIOS area. The `cores`/`cpu` shell command uses it.
- `madt_apic_ids(out,max)` is used for real startup. It reads the MADT through `acpi_find_table("APIC",0)`, which also works on UEFI, and walks the entries from offset 44 with bounds checks (`el>=2`, `p+el<=len`). It collects the APIC IDs of enabled type-0 (Local APIC, `id=m[p+3]`) and type-9 (Local x2APIC, 32-bit ID at `+4`, flags at `+8`) entries. x2APIC IDs at least 255 and duplicates are skipped.

### `smp_start_aps()`

1. `nc = madt_apic_ids(ids, NCPU)`. If `nc <= 1`, there is nothing to do.
2. Read IA32_APIC_BASE. `g_lapic_x2` = bit 10. The base is sanity-checked, with `0xFEE00000` as the default. The APIC is globally enabled if needed, and SVR bit 8 is set.
3. Copy `ap_tramp_start..ap_tramp_end` to physical `0x8000`. The code is in section `.aptramp`, which the linker scripts do not name, so it is placed as an orphan section and copied at runtime.
4. Write the parameter block (all physical, identity-mapped):

| Address | Size | Content | Written by |
|---|---|---|---|
| `0x8FD8` | int | logical CPU index `next` | per AP, before INIT |
| `0x8FE0` | u64 | stack top `g_ap_stacks[next]+16384` | per AP |
| `0x8FF0` | u64 | BSP CR3 (shared kernel page tables) | once |
| `0x8FF8` | u64 | `ap_main` address | once |

5. Record the BSP APIC ID in `g_cpu[0].apic_id`.
6. For each other ID: `percpu_setup(next, id)`, `g_ncpu = next+1`, then the INIT-SIPI-SIPI sequence:

```
ICR = 0x4500 (INIT, assert)          ; wait 10 ms
ICR = 0x4608 (Start-up, vector 0x08) ; wait 200 us     -> CS:IP = 0x0800:0000 = 0x8000
ICR = 0x4608                         ; second SIPI
poll g_cores_online for up to 2000 x 100 us (200 ms)
```

   In x2APIC mode this is one 64-bit `wrmsr 0x830` with the destination in bits 63:32. In xAPIC mode it writes `0x310` (destination << 24) and then `0x300`. The `lapic_wr` wrapper also emulates the xAPIC ICR pair on x2APIC: a write to `0x310` is buffered in `g_lapic_icr_dest`, and a write to `0x300` becomes one `0x830` MSR write. `lapic_rd(0x20)` shifts the x2APIC ID left by 24 so callers can treat it like xAPIC. If the core does not answer, `g_ncpu` is rolled back and that slot is reused. Delays come from `ap_udelay`, which uses `hr_delay_us` when a precise clock exists and a calibrated-free pause loop otherwise.

`smp.c` also contains an older lock-protected compute queue (`g_jobs[NJOBS=16]`, `smp_lock` test-and-set spinlock, `smp_submit`, `smp_work_step`, `smp_compute` counting primes). The `smpwork`/`parallel` and `smpsched` commands use it to show isolated parallel work and per-core counters (`g_core_load`, `g_core_cycles`).

---

## 4.13 AP trampoline (`arch/x86_64/boot/ap_trampoline.S`)

```
0x8000 .code16  cli; cld; DS=ES=SS=0
                lgdtl %cs:gdtptr              ; local 5-entry GDT
                CR0.PE=1 ; ljmpl $0x08, pm32
       .code32  DS=ES=SS=0x10
                CR4 |= 0x620                  ; PAE | OSFXSR | OSXMMEXCPT
                CR3  = [0x8FF0]               ; BSP page tables (low 32 bits)
                EFER |= 0x100                 ; LME
                CR0 &= ~0xC (EM,TS) ; CR0 |= 0x80010002 (PG | WP | MP)
                ljmpl $0x18, lm64
       .code64  DS=ES=SS=0x20 ; rsp=[0x8FE0] ; rbp=0
                call *[0x8FF8]                ; ap_main
                1: cli; hlt; jmp 1b
```

The trampoline GDT has null, `0x08` 32-bit code, `0x10` 32-bit data, `0x18` 64-bit code and `0x20` 64-bit data. These selectors differ from the kernel GDT, and `us_init` replaces them shortly afterwards. CR3 is loaded with a 32-bit `movl`, so the BSP's PML4 must be below 4 GB. EFER.NXE is not set on any core, which matches the kernel's page tables (no NX bits are used).

`ap_main()` reads its index from `0x8FD8`, calls `ap_entry_c(id)`, and parks with `cpu_stop()` if that ever returns.

---

## 4.14 AP runtime (`arch/x86_64/smp/smp_sched.c`)

### `ap_entry_c(id)`

```c
cpu_set_percpu(&g_cpu[id]);              /* GS -> own pcpu                         */
arch_fpu_init();  idt_load();            /* shared IDT                             */
APIC_BASE |= enable (+x2APIC bit if the BSP uses x2APIC)
lapic: TPR=0, SVR=0x1FF, LINT0=LINT1=masked (0x10000)  /* only the BSP takes ExtINT/NMI */
us_init();                               /* own GDT, TSS(rsp0, IST1/2), syscall MSRs */
LVT timer = 0x40 | periodic, divide 16, count = g_hr.lapic_hz/100   /* 100 Hz tick */
c->online=1; g_cores_online++ (atomic);  sti;  ap_loop();
```

The APs reuse the BSP's `lapic_hz` measurement. All cores are assumed to have the same APIC timer clock.

### `ap_loop`

```c
for(;;){
  if(c->stop){ c->parked=1; while(c->stop){ cpu_idle(); cpu_irq_off(); } c->parked=0; }
  int ran=0;
  if(g_smp_sched){ bkl_lock(); int pick=g_smp_sched?jc_pick():-1;
                   if(pick>=0){ jc_enter(pick); ran=1; } bkl_unlock(); }
  if(!ran && !smp_work_step()){ cpu_idle(); cpu_irq_off(); }
}
```

An idle AP sleeps in `sti; hlt` and wakes on its own 100 Hz timer or an IPI. `jc_runnable` refuses a job whose `runcpu` is non-zero, so each job runs on at most one core at a time.

### Timer and IPIs

| Vector | Name | Sender | Handler (`smp_ipi`, no BKL up front) |
|---|---|---|---|
| `0xF0` | `IPI_RESCHED` | `smp_kick(cpu)` (signal to a job running elsewhere, `jobs.c`), `ipi_others` in `smp_enable`/`smp_disable`, and `smp_kick_idle()` at the end of `jc_create` and `jc_fork`, which wakes every idle online core so a new process starts within microseconds instead of at the next 10 ms timer tick | EOI; if the core is in user mode running a job: `bkl_lock(); jc_preempt(r)` (sends the job back to the scheduler so the signal or stop is seen); otherwise just wakes the `hlt` |
| `0xF1` | `IPI_TLB` | `smp_tlb_shootdown()`, called by `acpi_hi_map` after it installs a new page directory for MMIO above 4 GiB | `tlb_pending=0`, reload CR3 |
| `0xF2` | `IPI_HALT` | `smp_halt_others()` from `panic` | `parked=1`, then `cli; hlt` forever |
| `0x40` | APIC timer | own LVT | `ap_timer`: `ticks++`, `user_ticks` or `idle_ticks` (idle means no job and no BKL), EOI; if user and `ticks - js_slice >= 2` to `bkl_lock(); jc_preempt(r)` |

`ipi_send` skips offline cores. In x2APIC mode it is a single `wrmsr 0x830`. In xAPIC mode it waits for the ICR delivery-status bit 12 to clear (up to 200,000 pauses) and then writes `0x310`/`0x300`. `lapic_eoi()` writes `0xB0`.

On APs the time slice is counted in the core's own `ticks` (`jc_now()` returns `PCPU->ticks` off-BSP and `g_ticks` on CPU 0). Both run at 100 Hz, so a slice is about 20 ms on every core.

### `smp_enable` / `smp_disable`

- `smp_enable` returns early (with the CPU count) if SMP is already on. It returns `-1` without a calibrated APIC timer (`g_hr.lapic_ok && lapic_hz`). It installs gates `0xF0`-`0xF2`, starts the APs if they are not already up, and returns `-2` if none came online. Otherwise it clears `stop` on every AP, sets `g_smp_on = g_smp_sched = 1` and sends RESCHED to the others. APs started once are never restarted. Later `smp on` calls only unpark them.
- `smp_disable` clears `g_smp_sched`, sets `stop=1` on every online AP and sends RESCHED. It then polls `parked` for up to 400 x 5 ms (2 s), dropping the BKL during each wait so a core finishing a syscall can leave. It returns `-1` if some core did not park, and otherwise clears `g_smp_on`.

### `smp_boot_auto` and the `/etc/smp` hang guard

`kmain` calls `smp_boot_auto_c()` just before the main loop, after the integrity check (section 2.12) has had its chance to stop the boot. (The old comment in `core/init/main.c` saying the cores start only on demand was removed in v8.95.)

```
/etc/smp == "off"       -> stay single-core
/etc/smp == "starting"  -> previous start never finished: write "off", log + notify, stay single-core
madt_apic_ids <= 1 && acpi_count_cores <= 1 -> nothing to start
write "starting" (smp_pref_set syncs the filesystem with nvx3_sync_all)
smp_enable()
write "on"
```

If the machine hangs during INIT/SIPI, the next boot reads "starting" and falls back to one core. `smp_pref_set` does nothing without a disk (`disk_ok`).

---

## 4.15 TLB coherence

Coherence relies on these properties in the current design:

- One kernel address space. APs load the BSP's CR3 (`0x8FF0`). Kernel mappings are set up before SMP starts and are not changed per core.
- Per-thread scheduling. `jc_runnable` requires `runcpu == 0`, so one thread never runs on two cores. `jc_enter` loads `p->cr3` and `jc_leave` loads the kernel CR3. With no CR4.PGE, every CR3 write flushes the whole TLB, so no stale user translation survives a switch to another core.
- Threads share page tables. Since v8.97 the threads of one process (section 5.15) have the same `cr3` and may run on several cores at the same time. `jc_enter` records the running address space in `PCPU->ucr3` while the thread is in user mode.
- Local invalidation. `vm_map`, `vm_unmap`, the COW fault path and `munmap` change the current address space and invalidate locally with `invlpg`.
- Shootdown mechanism. `smp_tlb_shootdown()` sets `tlb_pending` on every other online core and sends `IPI_TLB`; the receiver reloads CR3 in `smp_ipi`. `acpi_hi_map` calls it after adding a kernel-wide mapping for high MMIO.
- Synchronous shootdown for shared address spaces. `smp_tlb_sync(cr3)` sends `IPI_TLB` only to cores whose `ucr3` is that address space and then waits (a bounded spin) until each has cleared `tlb_pending` or stopped running it. It is called where a page that another thread may hold in its TLB is taken away or changes: the COW copy in `vm_fault_user` (before the old frame is released), `vm_clone_user` when it write-protects the parent's pages, and `lx_unmap_range` when the process has other threads (before the frames are freed). `linux_syscall` also clears a pending flag on entry, so a core that took the system call with interrupts off cannot use a stale entry.

---

## 4.16 The `smp` shell commands (`shell/commands.c`)

| Command | Effect |
|---|---|
| `smp` | Prints cores in the system (MADT, falling back to the legacy scan), cores online and the scheduling mode, then one row per online CPU: `cpu`, `apic`, state (`boot` / `parked` / `running` / `idle`), `switches`, `syscalls`, `user-ticks`, `idle-ticks`, `ipis` (from `struct pcpu`). |
| `smp on` / `smpstart` | Writes "starting", runs `smp_enable()`, writes "on". It reports "no calibrated local APIC timer" for `-1`, "no other cores came online" for at most 1, or "N cores schedule programs (big kernel lock, preemptive user mode)". |
| `smp off` | Runs `smp_disable()`. On success it writes "off" and reports "programs run on core 0 only"; on timeout, "some cores did not stop in time". |
| `cores` / `cpu` | Legacy MADT count and `g_cores_online`. |
| `smpwork` / `parallel`, `smpsched` | Prime-counting job queue demo and per-core counters (4.12). |

Other places that report the core count: `/proc/cpuinfo` (`fs/vfs/vfs.c`, one entry per online `g_cpu[]`), `/sys/devices/system/cpu/online` (`fs/vfs/unixfs.c`), and Linux `sched_getaffinity` (syscall 204 in `sys/linux/linux.c`). All of them report `g_cores_online` only while `g_smp_on` is set, and 1 otherwise. The user program `usr/musl/smptest.c` (named in the `smp` help line) forks workers, records which CPUs they ran on, and checks results as a stress test.
