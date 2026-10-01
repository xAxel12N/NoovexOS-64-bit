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

isr_common:
    pushq %rax
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
    addq  $16, %rsp           /* discard vector + error code           */
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
