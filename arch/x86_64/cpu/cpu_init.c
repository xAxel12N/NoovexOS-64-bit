#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/arch/x86_64/cpu/simd.h"
#include "include/nvx/arch/x86_64/cpu/cpuinfo.h"
#include "include/nvx/arch/x86_64/interrupts/interrupts.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/arch/x86_64/cpu/cpu_init.h"
#include "include/nvx/arch/x86_64/cpu/harden.h"
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
static u32 s3_lap[10], s3_ioa[4][1+2*120]; static u64 s3_ioab[4], s3_apicmsr; static int s3_nioa, s3_ioan[4];
static const u16 s3_lapr[10]={0x80,0xF0,0x320,0x330,0x340,0x350,0x360,0x370,0x3E0,0xD0};
static u32 lap_rd(u64 m,u32 off){ if(m&(1u<<10)){ u32 a,d; __asm__ volatile("rdmsr":"=a"(a),"=d"(d):"c"(0x800+(off>>4))); return a; } return *(volatile u32*)((m&0xFFFFFF000ULL)+off); }
static void lap_wr(u64 m,u32 off,u32 v){ if(m&(1u<<10)){ if(off==0xD0)return; __asm__ volatile("wrmsr"::"a"(v),"d"(0),"c"(0x800+(off>>4))); return; } *(volatile u32*)((m&0xFFFFFF000ULL)+off)=v; }
static int ioa_ok(u64 b){ if(!b||b>=0x100000000ULL)return 0; volatile u32* sel=(volatile u32*)b; volatile u32* win=(volatile u32*)(b+0x10); *sel=1; u32 v=*win; return !(v==0xFFFFFFFFu||(v&0xFF)<0x10||(v&0xFF)>0x2F); }
void apic_s3_save(void){
    u32 lo,hi; __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(0x1B)); s3_apicmsr=((u64)hi<<32)|lo;
    if((lo&(1u<<11))&&((lo&(1u<<10))||((s3_apicmsr&0xFFFFFF000ULL)&&(s3_apicmsr&0xFFFFFF000ULL)<0x100000000ULL))) for(int i=0;i<10;i++)s3_lap[i]=lap_rd(s3_apicmsr,s3_lapr[i]);
    s3_nioa=0; const u8* m=acpi_find_table("APIC",0); u64 bs[4]; int nb=0;
    if(m){ u32 len=*(const u32*)(m+4); if(len>4096)len=4096; for(u32 o=44;o+2<=len&&nb<4;){ u8 t=m[o], l=m[o+1]; if(l<2)break; if(t==1&&l>=12)bs[nb++]=*(const u32*)(m+o+4); o+=l; } }
    if(!nb)bs[nb++]=0xFEC00000ULL;
    for(int k=0;k<nb;k++){ u64 b=bs[k]; if(!ioa_ok(b))continue; volatile u32* sel=(volatile u32*)b; volatile u32* win=(volatile u32*)(b+0x10);
        *sel=1; int n=(int)((*win>>16)&0xFF)+1; if(n>120)n=120; u32* r=s3_ioa[s3_nioa]; *sel=0; r[0]=*win;
        for(int i=0;i<n;i++){ *sel=0x10u+2u*(u32)i; r[1+2*i]=*win; *sel=0x11u+2u*(u32)i; r[2+2*i]=*win; }
        s3_ioab[s3_nioa]=b; s3_ioan[s3_nioa]=n; s3_nioa++; } }
int apic_s3_restore(void){
    int bad=0; u64 m=s3_apicmsr;
    if(m&(1u<<11)){ u32 lo,hi; __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(0x1B));
        u64 cur=((u64)hi<<32)|lo; u64 want=(cur&~0xFFFFFF000ULL)|(m&0xFFFFFF000ULL)|(1u<<11);
        if((cur|want)!=cur||(cur&0xFFFFFF000ULL)!=(m&0xFFFFFF000ULL)){ cur=want&~(u64)(1u<<10); __asm__ volatile("wrmsr"::"a"((u32)cur),"d"((u32)(cur>>32)),"c"(0x1B)); }
        if(m&(1u<<10)){ cur|=(1u<<10); __asm__ volatile("wrmsr"::"a"((u32)cur),"d"((u32)(cur>>32)),"c"(0x1B)); }
        lap_wr(m,0xF0,s3_lap[1]|0x100); lap_wr(m,0x80,s3_lap[0]); lap_wr(m,0xD0,s3_lap[9]); lap_wr(m,0x3E0,s3_lap[8]);
        for(int i=3;i<8;i++){ lap_wr(m,s3_lapr[i],s3_lap[i]); } lap_wr(m,0x380,0); lap_wr(m,0x320,s3_lap[2]|0x10000u);
        lap_wr(m,0x280,0); lap_wr(m,0x280,0); lap_wr(m,0xB0,0); lap_wr(m,0xF0,s3_lap[1]);
        if((lap_rd(m,0x350)&0x700)!=(s3_lap[5]&0x700))bad|=1; }
    for(int k=0;k<s3_nioa;k++){ u64 b=s3_ioab[k]; if(!ioa_ok(b)){ bad|=2; continue; } volatile u32* sel=(volatile u32*)b; volatile u32* win=(volatile u32*)(b+0x10); const u32* r=s3_ioa[k];
        *sel=0; *win=r[0];
        for(int i=0;i<s3_ioan[k];i++){ *sel=0x10u+2u*(u32)i; *win=r[1+2*i]|0x10000u; *sel=0x11u+2u*(u32)i; *win=r[2+2*i]; *sel=0x10u+2u*(u32)i; *win=r[1+2*i]; } }
    return bad; }
void lapic_eoi_any(void){ u32 lo,hi; __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(0x1B)); if(!(lo&(1u<<11)))return;
    if(lo&(1u<<10)){ __asm__ volatile("wrmsr"::"a"(0),"d"(0),"c"(0x80B)); return; }
    u64 b=(((u64)hi<<32)|lo)&0xFFFFFF000ULL; if(!b||b>=0x100000000ULL)return; *(volatile u32*)(b+0xB0)=0; }
int lapic_isr_top(void){ u32 lo,hi; __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(0x1B)); if(!(lo&(1u<<11)))return -1;
    for(int i=7;i>=0;i--){ u32 v; if(lo&(1u<<10)){ u32 a,d; __asm__ volatile("rdmsr":"=a"(a),"=d"(d):"c"(0x810+i)); v=a; } else { u64 b=(((u64)hi<<32)|lo)&0xFFFFFF000ULL; if(!b||b>=0x100000000ULL)return -1; v=*(volatile u32*)(b+0x100+0x10*(u32)i); }
        if(v){ for(int k=31;k>=0;k--) if(v&(1u<<k))return i*32+k; } } return -1; }
int g_xsave_on; u32 g_xsave_size; u64 g_xcr0;
static void fpu_xsave_setup(void){
    u32 a,b,c,d; cpuid(1,&a,&b,&c,&d); if(!((c>>26)&1))return;
    u64 cr4; __asm__ volatile("mov %%cr4,%0":"=r"(cr4)); cr4|=(1ULL<<18); __asm__ volatile("mov %0,%%cr4"::"r"(cr4));
    u64 want=3; if((c>>28)&1){ u32 a2,b2,c2,d2; cpuid(0xD,&a2,&b2,&c2,&d2); if(a2&4)want|=4; }
    if(g_xcr0)want=g_xcr0;
    __asm__ volatile("xsetbv"::"c"(0),"a"((u32)want),"d"((u32)(want>>32)));
    { u32 a3,b3,c3,d3; cpuid(0xD,&a3,&b3,&c3,&d3); if(b3>1024&&want!=3){ want=3; __asm__ volatile("xsetbv"::"c"(0),"a"(3u),"d"(0u)); cpuid(0xD,&a3,&b3,&c3,&d3); } if(b3>1024)return; g_xsave_size=b3; }
    g_xcr0=want; g_xsave_on=1; }
void cpu_avx_clean(void){ if(g_xcr0&4)__asm__ volatile("vzeroupper"); }
void arch_fpu_init(void){
    u64 cr0,cr4; __asm__ volatile("mov %%cr0,%0":"=r"(cr0)); cr0&=~(1ULL<<2); cr0|=(1ULL<<1)|(1ULL<<16); __asm__ volatile("mov %0,%%cr0"::"r"(cr0));
    __asm__ volatile("mov %%cr4,%0":"=r"(cr4)); cr4|=(1ULL<<9)|(1ULL<<10); __asm__ volatile("mov %0,%%cr4"::"r"(cr4));
    { u32 a,b,c,d; cpuid(1,&a,&b,&c,&d); g_ssse3=(c>>9)&1; }
    fpu_xsave_setup();
    __asm__ volatile("fninit"); u32 mx=0x1F80; __asm__ volatile("ldmxcsr %0"::"m"(mx));
    harden_cpu();
}
void arch_idt_init(void){
    for(int i=0;i<48;i++) set_gate(i,isr_stubs[i]);
    for(int i=48;i<256;i++) set_gate(i,isr_ignore);
    struct idt_ptr ip={(u16)(sizeof(idt)-1),(u64)idt}; __asm__ volatile("lidt %0"::"m"(ip));
}
__asm__(".text\n.globl nf_swap\n.type nf_swap,@function\nnf_swap:\n\tpush %rbp\n\tpush %rbx\n\tpush %r12\n\tpush %r13\n\tpush %r14\n\tpush %r15\n\tmov %rsp,(%rdi)\n\tmov %rsi,%rsp\n\tpop %r15\n\tpop %r14\n\tpop %r13\n\tpop %r12\n\tpop %rbx\n\tpop %rbp\n\tret\n.size nf_swap,.-nf_swap\n");
