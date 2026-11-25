#ifndef NVX_ARCH_X86_64_DOOM_JMP_H
#define NVX_ARCH_X86_64_DOOM_JMP_H

#include "include/nvx/base.h"

typedef long doom_jmpbuf[8];
int doom_setjmp(doom_jmpbuf) __attribute__((returns_twice));
void doom_longjmp(doom_jmpbuf,int) __attribute__((noreturn));

#endif
