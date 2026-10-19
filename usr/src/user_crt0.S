.code64
.global _start
_start:
    xor %rbp, %rbp
    /* kernel placed argc in rdi, argv in rsi already */
    call main
    mov %eax, %ebx     /* return value -> exit status */
    xor %eax, %eax     /* syscall 0 = exit */
    int $0x80
1:  jmp 1b
.section .note.GNU-stack,"",@progbits
