# arch

CPU-specific code for x86-64.

| Folder | What is in it |
|---|---|
| `x86_64/boot/` | assembly entry (`boot64.S`), interrupt stubs (`isr64.S`), user-mode entry, AP trampoline |
| `x86_64/cpu/` | `cpu.h` and `io.h` helpers, CPU start-up, CPUID information, per-CPU data, SSE state |
| `x86_64/interrupts/` | IDT, exception and IRQ handling, the early fault screen |
| `x86_64/smp/` | multi-core start-up, IPIs, the scheduler side of SMP |
| `x86_64/time/` | HPET, TSC and local APIC timers |
| `x86_64/user/` | ring 3 entry, stack switching, setjmp/longjmp for DOOM and Quake |
| `x86_64/*.ld` | kernel linker scripts |
