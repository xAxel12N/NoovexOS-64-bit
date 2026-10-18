#ifndef NVX_ARCH_X86_64_INTERRUPTS_H
#define NVX_ARCH_X86_64_INTERRUPTS_H

#include "include/nvx/base.h"

struct idt_entry{ u16 lo; u16 sel; u8 ist; u8 attr; u16 mid; u32 hi; u32 z; } __attribute__((packed));
struct idt_ptr{ u16 limit; u64 base; } __attribute__((packed));
#define HR_LAPIC_VEC 0x40
typedef struct {
    u64 r15,r14,r13,r12,r11,r10,r9,r8,rbp,rdi,rsi,rdx,rcx,rbx,rax;
    u64 vector,errcode,rip,cs,rflags,rsp,ss;
} regs_t;
#define JS_BLOCK (-1000)

extern struct idt_entry idt[256];
extern void* isr_stubs[48];

extern void isr_ignore(void);
extern void isr_lapic_timer(void);
extern void isr_ipi_resched(void);
extern void isr_ipi_tlb(void);
extern void isr_ipi_halt(void);
void set_gate(int n,void*h);
void idt_set128(void);
void idt_use_ist(int n,int ist);
void pic_remap(void);
void pit_init(u32 hz);
void isr_dispatch(regs_t*r);
void lsys_c(struct sysregs* r);
void isr128_c(struct sysregs* r);

static inline void pic_eoi(int irq){ if(irq>=8)outb(0xA0,0x20); outb(0x20,0x20); }

#endif
