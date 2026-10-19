#ifndef NVX_SETJMP_H
#define NVX_SETJMP_H
#include <nvx/_std.h>
__attribute__((returns_twice)) int setjmp(jmp_buf b);
__attribute__((noreturn)) void longjmp(jmp_buf b,int v);
#endif
