.code64
.global _start
_start:
    xor %rbp, %rbp
    and $-16,%rsp
    call main
    mov %eax,%edi
    call exit
1:  jmp 1b
.section .note.GNU-stack,"",@progbits
