.code64
.section .text
.global kstart
kstart:
    lea stack_top(%rip), %rsp
    call kmain
1:  cli
    hlt
    jmp 1b

.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:

.section .note.GNU-stack,"",@progbits
