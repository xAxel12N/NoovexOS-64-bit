#ifndef NVX_ARCH_X86_64_CPU_INIT_H
#define NVX_ARCH_X86_64_CPU_INIT_H

#include "include/nvx/base.h"

void apic_virtual_wire(void);
void arch_fpu_init(void);
void arch_idt_init(void);

#endif
