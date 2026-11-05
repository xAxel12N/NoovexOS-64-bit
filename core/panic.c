#include "include/nvx/base.h"
#include "include/nvx/core/panic.h"

void crash_panic(regs_t* r,const char* msg,u64 rip,u64 rbp) __attribute__((weak));
void panic(regs_t*r,const char*msg){ if(crash_panic)crash_panic(r,msg,(u64)__builtin_return_address(0),(u64)__builtin_frame_address(0)); for(;;)cpu_stop(); }
