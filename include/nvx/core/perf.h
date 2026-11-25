#ifndef NVX_CORE_PERF_H
#define NVX_CORE_PERF_H

#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/interrupts/interrupts.h"
#include "include/nvx/core/perf_codec.h"

int perf_init(void);
int perf_hw(void);
int perf_nmi(regs_t* r);
void perf_tick(regs_t* r);
long perf_ev_open(u32 type,u64 cfg,u32 sfmt,u32 rfmt,int disabled,int pid,int cpu,int owner);
int perf_ev_read(int i,u64* out,int cap);
int perf_ev_ctl(int i,u32 cmd,u64* io);
void perf_ev_close(int i);
void perf_cpu_drain(void);
int perf_defer_pending(int c);
void perf_cmd(const char* a);

#endif
