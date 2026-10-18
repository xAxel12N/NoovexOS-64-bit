#include "include/nvx/base.h"

__asm__(
    ".text\n"
    ".globl nwe_call_stack\n"
    "nwe_call_stack:\n"
    "  pushq %rbp\n"
    "  movq %rsp, %rbp\n"
    "  movq %rcx, %rsp\n"
    "  andq $-16, %rsp\n"
    "  movq %rdi, %rax\n"
    "  movq %rsi, %rdi\n"
    "  movq %rdx, %rsi\n"
    "  callq *%rax\n"
    "  movq %rbp, %rsp\n"
    "  popq %rbp\n"
    "  ret\n");
