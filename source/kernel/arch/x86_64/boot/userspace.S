/* NoovexOS 64 - userspace support (Stage 4 + 5)
   GDT with ring-3 segments + TSS, an INT 0x80 syscall gate, ring-3 entry via
   iretq, and a kernel setjmp/longjmp so the "exit" syscall returns to the kernel. */
.code64

/* void gdt_flush(void* gdtptr) - load GDT (rdi) and reload all segment registers */
.global gdt_flush
gdt_flush:
    lgdt    (%rdi)
    mov     $0x10, %ax          /* kernel data */
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %ss
    mov     %ax, %fs
    mov     %ax, %gs
    leaq    .gdt_reload(%rip), %rax
    pushq   $0x08               /* kernel code */
    pushq   %rax
    lretq
.gdt_reload:
    ret

/* void tss_flush(unsigned short sel) - load the task register */
.global tss_flush
tss_flush:
    movw    %di, %ax
    ltr     %ax
    ret

/* INT 0x80 syscall gate: save GP regs, call syscall_dispatch(&regs), restore, iretq */
.global isr128
.extern syscall_dispatch
isr128:
    push    %rax
    push    %rbx
    push    %rcx
    push    %rdx
    push    %rsi
    push    %rdi
    push    %rbp
    push    %r8
    push    %r9
    push    %r10
    push    %r11
    push    %r12
    push    %r13
    push    %r14
    push    %r15
    mov     %rsp, %rdi          /* rdi -> saved-register frame (sysregs) */
    call    syscall_dispatch
    pop     %r15
    pop     %r14
    pop     %r13
    pop     %r12
    pop     %r11
    pop     %r10
    pop     %r9
    pop     %r8
    pop     %rbp
    pop     %rdi
    pop     %rsi
    pop     %rdx
    pop     %rcx
    pop     %rbx
    pop     %rax
    iretq

/* void enter_ring3(u64 rip, u64 rsp) - drop to ring 3 via iretq */
.global enter_ring3
.global enter_ring3_args
enter_ring3_args:
    /* rdi=rip, rsi=rsp, rdx=argc, rcx=argv -> user main(argc,argv) */
    mov     $0x23, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %fs
    mov     %ax, %gs
    pushq   $0x23
    pushq   %rsi
    pushq   $0x202
    pushq   $0x1B
    pushq   %rdi
    mov     %rdx, %rdi          /* argc -> user rdi */
    mov     %rcx, %rsi          /* argv -> user rsi */
    iretq

enter_ring3:
    mov     $0x23, %ax          /* user data | RPL 3 */
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %fs
    mov     %ax, %gs
    pushq   $0x23               /* SS  = user data | 3 */
    pushq   %rsi                /* RSP (user)          */
    pushq   $0x202              /* RFLAGS (IF set)     */
    pushq   $0x1B               /* CS  = user code | 3 */
    pushq   %rdi                /* RIP (user)          */
    iretq

/* int k_setjmp(u64* buf)  - returns 0 first time, or the value from k_longjmp */
.global k_setjmp
k_setjmp:
    mov     %rbx, 0(%rdi)
    mov     %rbp, 8(%rdi)
    mov     %r12, 16(%rdi)
    mov     %r13, 24(%rdi)
    mov     %r14, 32(%rdi)
    mov     %r15, 40(%rdi)
    leaq    8(%rsp), %rax
    mov     %rax, 48(%rdi)      /* rsp after ret */
    mov     (%rsp), %rax
    mov     %rax, 56(%rdi)      /* return address */
    xor     %eax, %eax
    ret

/* void k_longjmp(u64* buf, int val) */
.global k_longjmp
k_longjmp:
    mov     0(%rdi), %rbx
    mov     8(%rdi), %rbp
    mov     16(%rdi), %r12
    mov     24(%rdi), %r13
    mov     32(%rdi), %r14
    mov     40(%rdi), %r15
    mov     48(%rdi), %rsp
    mov     %esi, %eax
    testl   %eax, %eax
    jnz     1f
    movl    $1, %eax
1:  jmp     *56(%rdi)
/* void sched_swap(u64* save_ksp, u64 load_ksp) - cooperative context switch:
   save callee regs + rsp to *save_ksp, load rsp from load_ksp, restore, ret */
.global sched_swap
sched_swap:
    push    %rbx
    push    %rbp
    push    %r12
    push    %r13
    push    %r14
    push    %r15
    mov     %rsp, (%rdi)
    mov     %rsi, %rsp
    pop     %r15
    pop     %r14
    pop     %r13
    pop     %r12
    pop     %rbp
    pop     %rbx
    ret

/* first entry into a new process: the iret frame sits on the stack right here */
.global user_trampoline
user_trampoline:
    iretq
/* fork child: force rax=0 (fork returns 0 in the child), then iretq */
.global user_trampoline_fork
user_trampoline_fork:
    xor     %eax, %eax
    iretq
.section .note.GNU-stack,"",@progbits

.text
.global jc_resume
jc_resume:
    mov     $0x23, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %fs
    mov     %ax, %gs
    mov     %rdi, %rsp
    pop     %r15
    pop     %r14
    pop     %r13
    pop     %r12
    pop     %r11
    pop     %r10
    pop     %r9
    pop     %r8
    pop     %rbp
    pop     %rdi
    pop     %rsi
    pop     %rdx
    pop     %rcx
    pop     %rbx
    pop     %rax
    add     $16, %rsp
    iretq
.section .note.GNU-stack,"",@progbits
