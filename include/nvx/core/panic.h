#ifndef NVX_CORE_PANIC_H
#define NVX_CORE_PANIC_H

#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/interrupts/interrupts.h"

void panic(regs_t*r,const char*msg);

#endif
