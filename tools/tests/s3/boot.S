.set MB_MAGIC, 0x1BADB002
.set MB_FLAGS, 0x00010003
.section .text
.code32
.global _start
mbhdr:
    .long MB_MAGIC
    .long MB_FLAGS
    .long -(MB_MAGIC + MB_FLAGS)
    .long mbhdr
    .long mbhdr
    .long _edata
    .long _end
    .long _start
_start:
    cli
    movl $stack_top, %esp
    movl $pdpt, %eax
    orl $3, %eax
    movl %eax, pml4
    movl $pd, %eax
    orl $3, %eax
    movl %eax, pdpt
    xorl %ecx, %ecx
1:  movl %ecx, %eax
    shll $21, %eax
    orl $0x83, %eax
    movl %eax, pd(,%ecx,8)
    incl %ecx
    cmpl $512, %ecx
    jb 1b
    movl $pml4, %eax
    movl %eax, %cr3
    movl %cr4, %eax
    orl $0x620, %eax
    movl %eax, %cr4
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr
    movl %cr0, %eax
    orl $0x80000001, %eax
    movl %eax, %cr0
    lgdt gdtp
    ljmp $0x08, $lm
.code64
lm:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movabsq $stack_top, %rsp
    call s3t_main
2:  hlt
    jmp 2b
.section .data
.align 16
gdt:
    .quad 0
    .quad 0x00AF9A000000FFFF
    .quad 0x00AF92000000FFFF
gdtp:
    .word gdtp - gdt - 1
    .long gdt
.section .bss
.align 4096
pml4: .fill 4096,1,0
pdpt: .fill 4096,1,0
pd:   .fill 4096,1,0
      .fill 16384,1,0
stack_top:
.section .note.GNU-stack,"",@progbits
