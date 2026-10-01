#ifndef _NVX_SETJMP_H
#define _NVX_SETJMP_H
typedef unsigned long jmp_buf[8];
int k_setjmp(unsigned long*);
void k_longjmp(unsigned long*,int);
#define setjmp(b) k_setjmp(b)
#define longjmp(b,v) k_longjmp(b,v)
#endif
