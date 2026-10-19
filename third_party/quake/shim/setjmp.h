#ifndef _SHIM_SETJMP_H
#define _SHIM_SETJMP_H
typedef long jmp_buf[8];
int setjmp(jmp_buf);
void longjmp(jmp_buf,int) __attribute__((noreturn));
#endif
