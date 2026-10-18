#ifndef NVX_ARCH_X86_64_RING3_H
#define NVX_ARCH_X86_64_RING3_H

#include "include/nvx/base.h"

struct sysregs { u64 r15,r14,r13,r12,r11,r10,r9,r8,rbp,rdi,rsi,rdx,rcx,rbx,rax; };

extern u64 g_kjmp[8];
extern char g_r3out[256];
extern int g_r3out_n;
extern int g_sched_active;

extern void isr128(void);
extern void enter_ring3(u64 rip, u64 rsp);
extern void enter_ring3_args(u64 rip, u64 rsp, u64 argc, u64 argv);
extern int k_setjmp(u64* buf);
extern void k_longjmp(u64* buf, int v);
void us_init(void);
int ring3_run(void);

#endif
