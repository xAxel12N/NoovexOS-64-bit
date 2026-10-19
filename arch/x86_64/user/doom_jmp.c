#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/user/doom_jmp.h"
__asm__(".globl doom_setjmp\ndoom_setjmp:\n mov %rbx,0(%rdi)\n mov %rbp,8(%rdi)\n mov %r12,16(%rdi)\n mov %r13,24(%rdi)\n mov %r14,32(%rdi)\n mov %r15,40(%rdi)\n lea 8(%rsp),%rax\n mov %rax,48(%rdi)\n mov (%rsp),%rax\n mov %rax,56(%rdi)\n xor %eax,%eax\n ret\n"
        ".globl doom_longjmp\ndoom_longjmp:\n mov 0(%rdi),%rbx\n mov 8(%rdi),%rbp\n mov 16(%rdi),%r12\n mov 24(%rdi),%r13\n mov 32(%rdi),%r14\n mov 40(%rdi),%r15\n mov 48(%rdi),%rsp\n mov %esi,%eax\n test %eax,%eax\n jnz 1f\n mov $1,%eax\n1: jmp *56(%rdi)\n");
