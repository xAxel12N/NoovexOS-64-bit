/* NoovexOS 64 - AP (secondary core) startup trampoline.
   Copied to physical 0x8000 and started via SIPI (vector 0x08). Brings a core
   from 16-bit real mode -> 32-bit protected -> 64-bit long mode, then jumps to
   the C entry ap_main. Reuses the BSP's CR3 (shares the kernel address space).
   Parameter block (written by the BSP before SIPI):
     0x8FE0 = per-AP stack top (u64)
     0x8FF0 = CR3            (u64)
     0x8FF8 = ap_main addr   (u64)  */
#define TB 0x8000
.code16
.section .aptramp,"ax"
.global ap_tramp_start
.global ap_tramp_end
ap_tramp_start:
    cli
    cld
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    lgdtl   %cs:(gdtptr - ap_tramp_start)
    movl    %cr0, %eax
    orl     $1, %eax
    movl    %eax, %cr0
    ljmpl   $0x08, $(pm32 - ap_tramp_start + TB)

.code32
pm32:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movl    %cr4, %eax
    orl     $0x20, %eax              /* CR4.PAE */
    movl    %eax, %cr4
    movl    (TB+0xFF0), %eax         /* CR3 (BSP's page tables) */
    movl    %eax, %cr3
    movl    $0xC0000080, %ecx        /* EFER */
    rdmsr
    orl     $0x100, %eax             /* EFER.LME */
    wrmsr
    movl    %cr0, %eax
    orl     $0x80000000, %eax        /* CR0.PG */
    movl    %eax, %cr0
    ljmpl   $0x18, $(lm64 - ap_tramp_start + TB)

.code64
lm64:
    movw    $0x20, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movq    (TB+0xFE0), %rsp         /* per-AP stack */
    movq    (TB+0xFF8), %rax         /* ap_main */
    xorq    %rbp, %rbp
    callq   *%rax
1:  cli
    hlt
    jmp     1b

.align 16
gdt:
    .quad   0x0000000000000000        /* null            */
    .quad   0x00CF9A000000FFFF        /* 0x08 32-bit code */
    .quad   0x00CF92000000FFFF        /* 0x10 32-bit data */
    .quad   0x00AF9A000000FFFF        /* 0x18 64-bit code */
    .quad   0x00AF92000000FFFF        /* 0x20 64-bit data */
gdtptr:
    .word   (gdtptr - gdt - 1)
    .long   (gdt - ap_tramp_start + TB)
ap_tramp_end:
.section .note.GNU-stack,"",@progbits
