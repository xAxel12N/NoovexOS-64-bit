#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/arch/x86_64/cpu/simd.h"
#include "include/nvx/arch/x86_64/cpu/cpuinfo.h"
#include "include/nvx/arch/x86_64/interrupts/interrupts.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/arch/x86_64/cpu/cpu_init.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
int g_ioapic_n=0, g_ioapic_masked=0;
static void ioapic_mask_one(u64 base){ if(!base||base>=0x100000000ULL)return; volatile u32* sel=(volatile u32*)base; volatile u32* win=(volatile u32*)(base+0x10);
    *sel=1; u32 ver=*win; if(ver==0xFFFFFFFFu||(ver&0xFF)<0x10||(ver&0xFF)>0x2F)return; int n=(int)((ver>>16)&0xFF)+1; if(n>120)n=120; g_ioapic_n++;
    for(int i=0;i<n;i++){ *sel=0x10u+2u*(u32)i; u32 lo=*win; if(!(lo&0x10000u)){ *win=lo|0x10000u; g_ioapic_masked++; } } }
void ioapic_mask_all(void){ const u8* m=acpi_find_table("APIC",0); int any=0;
    if(m){ u32 len=*(const u32*)(m+4); if(len>4096)len=4096; for(u32 o=44;o+2<=len;){ u8 t=m[o], l=m[o+1]; if(l<2)break; if(t==1&&l>=12){ ioapic_mask_one(*(const u32*)(m+o+4)); any=1; } o+=l; } }
    if(!any)ioapic_mask_one(0xFEC00000ULL); }
void apic_virtual_wire(void){
    u32 lo,hi; __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(0x1B));
    if(!(lo&(1u<<11)))return;
    if(lo&(1u<<10)){ u32 m[5]={0x808,0x80F,0x835,0x836,0x832}; u32 v[4]={0,0x1FF,0x700,0x400}; for(int i=0;i<4;i++)__asm__ volatile("wrmsr"::"a"(v[i]),"d"(0),"c"(m[i]));
        u32 a,d; __asm__ volatile("rdmsr":"=a"(a),"=d"(d):"c"(m[4])); __asm__ volatile("wrmsr"::"a"(a|0x10000u),"d"(d),"c"(m[4])); klog("boot: local APIC (x2APIC) set to virtual-wire mode"); return; }
    u64 b=(((u64)hi<<32)|lo)&0xFFFFFF000ULL; if(!b||b>=0x100000000ULL)return;
    volatile u32* a=(volatile u32*)b; a[0x80/4]=0; a[0xF0/4]=0x1FF; a[0x350/4]=0x700; a[0x360/4]=0x400; a[0x320/4]|=0x10000;
    klog("boot: local APIC set to virtual-wire mode");
}
void lapic_eoi_any(void){ u32 lo,hi; __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(0x1B)); if(!(lo&(1u<<11)))return;
    if(lo&(1u<<10)){ __asm__ volatile("wrmsr"::"a"(0),"d"(0),"c"(0x80B)); return; }
    u64 b=(((u64)hi<<32)|lo)&0xFFFFFF000ULL; if(!b||b>=0x100000000ULL)return; *(volatile u32*)(b+0xB0)=0; }
int lapic_isr_top(void){ u32 lo,hi; __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(0x1B)); if(!(lo&(1u<<11)))return -1;
    for(int i=7;i>=0;i--){ u32 v; if(lo&(1u<<10)){ u32 a,d; __asm__ volatile("rdmsr":"=a"(a),"=d"(d):"c"(0x810+i)); v=a; } else { u64 b=(((u64)hi<<32)|lo)&0xFFFFFF000ULL; if(!b||b>=0x100000000ULL)return -1; v=*(volatile u32*)(b+0x100+0x10*(u32)i); }
        if(v){ for(int k=31;k>=0;k--) if(v&(1u<<k))return i*32+k; } } return -1; }
void arch_fpu_init(void){
    u64 cr0,cr4; __asm__ volatile("mov %%cr0,%0":"=r"(cr0)); cr0&=~(1ULL<<2); cr0|=(1ULL<<1)|(1ULL<<16); __asm__ volatile("mov %0,%%cr0"::"r"(cr0));
    __asm__ volatile("mov %%cr4,%0":"=r"(cr4)); cr4|=(1ULL<<9)|(1ULL<<10); __asm__ volatile("mov %0,%%cr4"::"r"(cr4));
    { u32 a,b,c,d; cpuid(1,&a,&b,&c,&d); g_ssse3=(c>>9)&1; }
    __asm__ volatile("fninit"); u32 mx=0x1F80; __asm__ volatile("ldmxcsr %0"::"m"(mx));
}
void arch_idt_init(void){
    for(int i=0;i<48;i++) set_gate(i,isr_stubs[i]);
    for(int i=48;i<256;i++) set_gate(i,isr_ignore);
    struct idt_ptr ip={(u16)(sizeof(idt)-1),(u64)idt}; __asm__ volatile("lidt %0"::"m"(ip));
}
