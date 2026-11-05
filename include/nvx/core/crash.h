#ifndef NVX_CORE_CRASH_H
#define NVX_CORE_CRASH_H

#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/interrupts/interrupts.h"

#define CRASH_PANIC  1
#define CRASH_FREEZE 2
#define CRASH_STACK  3
#define CRASH_SMASH  4

extern u8 g_ksyms[];
extern volatile u32 g_frz_still;
extern int g_bsg_on;
extern volatile int g_frz_hold;
extern u32 g_heap_bad, g_heap_dfree, g_heap_badfree, g_heap_reent;

int kname_find(u64 a,const char** name,u64* off);
int kname_ready(void);
int kname_fmt(char* out,int cap,u64 a);
int crash_bt(u64 rbp,u64* out,int max);
void crash_logline(const char* s);
void crash_panic(regs_t* r,const char* msg,u64 rip,u64 rbp);
void freeze_tick(regs_t* r);
int crash_tick(void);
void crash_init(void);
void crash_cmd(const char* args);

void stk_watch(const char* nm,int idx,void* base);
void stk_cpu(int i);
void stk_check(void);
void stk_guard_boot(void);

void drv_boot_check(void);
int drv_begin(const char* id);
void drv_end(void);
void drv_boot_done(void);
int drv_skipped(const char* id);
const char* drv_current(void);

#endif
