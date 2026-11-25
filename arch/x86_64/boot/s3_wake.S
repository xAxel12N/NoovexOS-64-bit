#define TB 0x9000
#define NMSR 7
.code16
.section .s3tramp,"ax"
.global s3_wake_start
.global s3_wake_end
s3_wake_start:
    cli
    cld
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    lgdtl   (s3gdtp - s3_wake_start + TB)
    movl    %cr0, %eax
    orl     $1, %eax
    movl    %eax, %cr0
    ljmpl   $0x08, $(s3pm32 - s3_wake_start + TB)
.code32
s3pm32:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movl    $0x620, %eax
    movl    %eax, %cr4
    movl    (s3_ctx+80), %eax
    andl    $0xFFFFF000, %eax
    movl    %eax, %cr3
    movl    $0xC0000080, %ecx
    movl    (s3_ctx+136), %eax
    andl    $0xFFFFFBFF, %eax
    orl     $0x100, %eax
    movl    (s3_ctx+140), %edx
    wrmsr
    movl    %cr0, %eax
    andl    $0xFFFFFFF3, %eax
    orl     $0x80010002, %eax
    movl    %eax, %cr0
    ljmpl   $0x18, $(s3lm64 - s3_wake_start + TB)
.code64
s3lm64:
    movw    $0x20, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movabsq $s3_resume64, %rax
    jmpq    *%rax
.align 16
s3gdt:
    .quad   0x0000000000000000
    .quad   0x00CF9A000000FFFF
    .quad   0x00CF92000000FFFF
    .quad   0x00AF9A000000FFFF
    .quad   0x00AF92000000FFFF
s3gdtp:
    .word   (s3gdtp - s3gdt - 1)
    .long   (s3gdt - s3_wake_start + TB)
s3_wake_end:

.data
.align 16
.global s3_ctx
s3_ctx:
    .fill   144 + NMSR*8, 1, 0
s3_msrs:
    .long   0xC0000081, 0xC0000082, 0xC0000084, 0xC0000100, 0xC0000101, 0xC0000102, 0x277

.text
.global s3_save
.type s3_save,@function
s3_save:
    leaq    s3_ctx(%rip), %rsi
    movq    %rbx, 0(%rsi)
    movq    %rbp, 8(%rsi)
    movq    %r12, 16(%rsi)
    movq    %r13, 24(%rsi)
    movq    %r14, 32(%rsi)
    movq    %r15, 40(%rsi)
    leaq    8(%rsp), %rdx
    movq    %rdx, 48(%rsi)
    movq    (%rsp), %rdx
    movq    %rdx, 56(%rsi)
    pushfq
    popq    %rdx
    movq    %rdx, 64(%rsi)
    movq    %cr0, %rdx
    movq    %rdx, 72(%rsi)
    movq    %cr3, %rdx
    movq    %rdx, 80(%rsi)
    movq    %cr4, %rdx
    movq    %rdx, 88(%rsi)
    sgdt    96(%rsi)
    sidt    112(%rsi)
    str     128(%rsi)
    movl    $0xC0000080, %ecx
    rdmsr
    movl    %eax, 136(%rsi)
    movl    %edx, 140(%rsi)
    xorl    %edi, %edi
1:  leaq    s3_msrs(%rip), %r8
    movl    (%r8,%rdi,4), %ecx
    rdmsr
    movl    %eax, 144(%rsi,%rdi,8)
    movl    %edx, 148(%rsi,%rdi,8)
    incl    %edi
    cmpl    $NMSR, %edi
    jb      1b
    xorl    %eax, %eax
    ret

.global s3_resume64
.type s3_resume64,@function
s3_resume64:
    leaq    s3_ctx(%rip), %rsi
    lgdt    96(%rsi)
    lidt    112(%rsi)
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    xorl    %eax, %eax
    movw    %ax, %fs
    movw    %ax, %gs
    movq    48(%rsi), %rsp
    pushq   $0x08
    leaq    2f(%rip), %rax
    pushq   %rax
    lretq
2:  movq    88(%rsi), %rax
    movq    %rax, %cr4
    movq    80(%rsi), %rax
    movq    %rax, %cr3
    movq    72(%rsi), %rax
    movq    %rax, %cr0
    movzwl  128(%rsi), %eax
    testl   %eax, %eax
    jz      3f
    movq    98(%rsi), %rdx
    andb    $0xFD, 5(%rdx,%rax)
    ltr     %ax
3:  xorl    %edi, %edi
4:  leaq    s3_msrs(%rip), %r8
    movl    (%r8,%rdi,4), %ecx
    movl    144(%rsi,%rdi,8), %eax
    movl    148(%rsi,%rdi,8), %edx
    wrmsr
    incl    %edi
    cmpl    $NMSR, %edi
    jb      4b
    movq    0(%rsi), %rbx
    movq    8(%rsi), %rbp
    movq    16(%rsi), %r12
    movq    24(%rsi), %r13
    movq    32(%rsi), %r14
    movq    40(%rsi), %r15
    pushq   56(%rsi)
    pushq   64(%rsi)
    popfq
    movl    $1, %eax
    ret
.section .note.GNU-stack,"",@progbits
