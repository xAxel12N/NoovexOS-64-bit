.text
.globl _start
_start:
    xor %rbp,%rbp
    mov (%rsp),%rdi
    lea 8(%rsp),%rsi
    lea 8(%rsi,%rdi,8),%rdx
    mov %rdx,__host_envp(%rip)
    and $-16,%rsp
    call main
    mov %eax,%edi
    call exit
1:  jmp 1b
.section .note.GNU-stack,"",@progbits
