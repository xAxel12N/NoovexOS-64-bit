/* ===========================================================================
   NoovexOS 64  -  isr64.S
   64-bit interrupt stubs.  Each pushes a (dummy) error code + vector number,
   then jumps to a common path that saves all GP registers, calls the C
   dispatcher isr_dispatch(regs*), restores, and iretqs.

   Exceptions 0-31 route to the kernel panic (Red Screen of Death); vectors
   32-47 are the remapped PIC IRQs.
   =========================================================================== */

.code64
.section .text

.macro STUB_NOERR n
.global isr\n
isr\n:
    pushq $0
    pushq $\n
    jmp isr_common
.endm
.macro STUB_ERR n
.global isr\n
isr\n:
    pushq $\n
    jmp isr_common
.endm

/* CPU exceptions (8,10,11,12,13,14,17,21 push a hardware error code) */
STUB_NOERR 0
STUB_NOERR 1
STUB_NOERR 2
STUB_NOERR 3
STUB_NOERR 4
STUB_NOERR 5
STUB_NOERR 6
STUB_NOERR 7
STUB_ERR   8
STUB_NOERR 9
STUB_ERR   10
STUB_ERR   11
STUB_ERR   12
STUB_ERR   13
STUB_ERR   14
STUB_NOERR 15
STUB_NOERR 16
STUB_ERR   17
STUB_NOERR 18
STUB_NOERR 19
STUB_NOERR 20
STUB_ERR   21
STUB_NOERR 22
STUB_NOERR 23
STUB_NOERR 24
STUB_NOERR 25
STUB_NOERR 26
STUB_NOERR 27
STUB_NOERR 28
STUB_NOERR 29
STUB_NOERR 30
STUB_NOERR 31
/* IRQ vectors 32..47 */
STUB_NOERR 32
STUB_NOERR 33
STUB_NOERR 34
STUB_NOERR 35
STUB_NOERR 36
STUB_NOERR 37
STUB_NOERR 38
STUB_NOERR 39
STUB_NOERR 40
STUB_NOERR 41
STUB_NOERR 42
STUB_NOERR 43
STUB_NOERR 44
STUB_NOERR 45
STUB_NOERR 46
STUB_NOERR 47

/* catch-all for unused vectors 48..255 */
.global isr_ignore
isr_ignore:
    pushq $0
    pushq $255
    jmp isr_common

.global isr_ipi_resched
isr_ipi_resched:
    pushq $0
    pushq $0xF0
    jmp isr_common
.global isr_ipi_tlb
isr_ipi_tlb:
    pushq $0
    pushq $0xF1
    jmp isr_common
.global isr_ipi_halt
isr_ipi_halt:
    pushq $0
    pushq $0xF2
    jmp isr_common

.global isr_lapic_timer
isr_lapic_timer:
    pushq $0
    pushq $0x40
    jmp isr_common

isr_common:
    testb $3, 24(%rsp)
    jz    1f
    swapgs
1:  pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq  %rsp, %rdi          /* arg: pointer to saved register frame */
    movq  %rsp, %rbp          /* remember unaligned rsp                */
    andq  $-16, %rsp          /* 16-byte align for the SysV call       */
    call  isr_dispatch
    movq  %rbp, %rsp          /* restore                               */
    popq  %r15
    popq  %r14
    popq  %r13
    popq  %r12
    popq  %r11
    popq  %r10
    popq  %r9
    popq  %r8
    popq  %rbp
    popq  %rdi
    popq  %rsi
    popq  %rdx
    popq  %rcx
    popq  %rbx
    popq  %rax
    cli
    testb $3, 24(%rsp)
    jz    1f
    swapgs
1:  addq  $16, %rsp           /* discard vector + error code           */
    iretq

.section .rodata
.align 8
.global isr_stubs
isr_stubs:
    .quad isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7
    .quad isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15
    .quad isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    .quad isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    .quad isr32, isr33, isr34, isr35, isr36, isr37, isr38, isr39
    .quad isr40, isr41, isr42, isr43, isr44, isr45, isr46, isr47

.section .note.GNU-stack,"",@progbits

.section .text
.macro ESTUB_NOERR n
eexc\n:
    pushq $0
    pushq $\n
    jmp eexc_common
.endm
.macro ESTUB_ERR n
eexc\n:
    pushq $\n
    jmp eexc_common
.endm
ESTUB_NOERR 0
ESTUB_NOERR 1
ESTUB_NOERR 2
ESTUB_NOERR 3
ESTUB_NOERR 4
ESTUB_NOERR 5
ESTUB_NOERR 6
ESTUB_NOERR 7
ESTUB_ERR   8
ESTUB_NOERR 9
ESTUB_ERR   10
ESTUB_ERR   11
ESTUB_ERR   12
ESTUB_ERR   13
ESTUB_ERR   14
ESTUB_NOERR 15
ESTUB_NOERR 16
ESTUB_ERR   17
ESTUB_NOERR 18
ESTUB_NOERR 19
ESTUB_NOERR 20
ESTUB_ERR   21
ESTUB_NOERR 22
ESTUB_NOERR 23
ESTUB_NOERR 24
ESTUB_NOERR 25
ESTUB_NOERR 26
ESTUB_NOERR 27
ESTUB_NOERR 28
ESTUB_ERR   29
ESTUB_ERR   30
ESTUB_NOERR 31
eexc_common:
    cli
    movq %rsp, %rdi
    andq $-16, %rsp
    call early_fault
1:  hlt
    jmp 1b
.section .rodata
.global early_exc_stubs
early_exc_stubs:
    .quad eexc0, eexc1, eexc2, eexc3, eexc4, eexc5, eexc6, eexc7
    .quad eexc8, eexc9, eexc10, eexc11, eexc12, eexc13, eexc14, eexc15
    .quad eexc16, eexc17, eexc18, eexc19, eexc20, eexc21, eexc22, eexc23
    .quad eexc24, eexc25, eexc26, eexc27, eexc28, eexc29, eexc30, eexc31
