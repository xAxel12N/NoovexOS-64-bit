#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/percpu.h"
#include "include/nvx/arch/x86_64/smp.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/core/power.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "include/nvx/arch/x86_64/hrtimer.h"
#include "include/nvx/arch/x86_64/smp_sched.h"
/* ================= multi-core: detect CPU cores via ACPI MADT (read-only) ================= */
int acpi_count_cores(void){
    /* find RSDP ("RSD PTR ") in the EBDA + BIOS area */
    u64 rsdp=0;
    for(u64 a=0x000E0000; a<0x00100000; a+=16){ const char* p=(const char*)a;
        if(p[0]=='R'&&p[1]=='S'&&p[2]=='D'&&p[3]==' '&&p[4]=='P'&&p[5]=='T'&&p[6]=='R'&&p[7]==' '){ rsdp=a; break; } }
    if(!rsdp) return 0;
    u8 rev=*(u8*)(rsdp+15); u64 sdt; int use64=0;
    if(rev>=2){ sdt=*(u32*)(rsdp+24); if(*(u64*)(rsdp+24)) sdt=*(u64*)(rsdp+24); use64=1; }   /* XSDT */
    else sdt=*(u32*)(rsdp+16);                                                                  /* RSDT */
    if(!sdt||sdt>=0xF0000000ULL) return 0;
    u32 len=*(u32*)(sdt+4); int n=(int)((len-36)/(use64?8:4));
    u64 madt=0;
    for(int i=0;i<n && i<64;i++){ u64 e=use64?*(u64*)(sdt+36+(u64)i*8):(u64)*(u32*)(sdt+36+(u64)i*4);
        if(!e||e>=0xF0000000ULL)continue; const char* sig=(const char*)e;
        if(sig[0]=='A'&&sig[1]=='P'&&sig[2]=='I'&&sig[3]=='C'){ madt=e; break; } }
    if(!madt) return 0;
    u32 mlen=*(u32*)(madt+4); u64 p=madt+44; int cores=0;
    while(p < madt+mlen){ u8 type=*(u8*)p, elen=*(u8*)(p+1); if(elen==0)break;
        if(type==0){ u8 flags=*(u8*)(p+4); if(flags&1)cores++; }   /* Local APIC, enabled */
        p+=elen; }
    return cores;
}
/* ================= SMP: start the secondary cores (opt-in, parks them) ================= */
extern const unsigned char ap_tramp_start[], ap_tramp_end[];
volatile int g_cores_online=1;                 /* BSP counts as 1 */
volatile unsigned long g_core_load[8]={0};   /* jobs completed per core (per-core run-queue stats) */
volatile unsigned long g_core_cycles[8]={0}; /* approx work units per core */
static u8 g_ap_stacks[NCPU][16384] __attribute__((aligned(16)));
/* ---- a lock-protected work queue so the extra cores do REAL parallel work ----
   Jobs are self-contained compute (touch only their own struct + stack), so this
   is safe without making the whole kernel SMP-safe: one spinlock guards claiming. */

  /* state: 0 empty,1 pending,2 running,3 done */
struct sjob g_jobs[NJOBS];
static volatile int g_smp_lock=0;
void smp_lock(void){ while(__sync_lock_test_and_set(&g_smp_lock,1)) __asm__ volatile("pause"); }
void smp_unlock(void){ __sync_lock_release(&g_smp_lock); }
u64 smp_compute(u64 n){ u64 c=0; for(u64 i=2;i<=n;i++){ int p=1; for(u64 j=2;j*j<=i;j++) if(i%j==0){p=0;break;} if(p)c++; } return c; }  /* count primes <= n (pure) */
int smp_submit(u64 arg){ int id=-1; smp_lock();
    for(int i=0;i<NJOBS;i++) if(g_jobs[i].state==0){ g_jobs[i].arg=arg; g_jobs[i].result=0; g_jobs[i].core=-1; g_jobs[i].state=1; id=i; break; } smp_unlock(); return id; }
volatile int g_smp_work_on=0;

static void ap_main(void){ int id=*(volatile int*)0x8FD8ULL; ap_entry_c(id); for(;;)cpu_stop(); }
u64 g_lapic_base=0xFEE00000ULL;
int g_lapic_x2=0; u32 g_lapic_icr_dest=0;

static void ap_udelay(u32 us){ if(hr_precise()){ hr_delay_us(us); return; } for(volatile u32 i=0;i<us*1000u;i++)__asm__ volatile("pause"); }
/* fill out[] with the APIC IDs of enabled cores from the MADT; returns count */
 
int madt_apic_ids(u8* out,int max){
    const u8* m=acpi_find_table("APIC",0); if(!m)return 0; u32 len=acpi_r32(m+4); if(len<44)return 0; u32 p=44; int c=0;
    while(p+2<=len){ u8 type=m[p], el=m[p+1]; if(el<2||p+el>len)break;
        if(type==0&&el>=8){ u8 id=m[p+3]; u32 fl=acpi_r32(m+p+4); if((fl&1)&&c<max)out[c++]=id; }
        else if(type==9&&el>=16){ u32 id=acpi_r32(m+p+4), fl=acpi_r32(m+p+8); int dup=0; for(int k=0;k<c;k++) if(out[k]==id)dup=1; if((fl&1)&&id<255&&!dup&&c<max)out[c++]=(u8)id; }
        p+=el; }
    return c;
}

int smp_start_aps(void){
    u8 ids[NCPU]; int nc=madt_apic_ids(ids,NCPU); if(nc<=1) return nc;
    { u32 lo,hi; __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(0x1B));
      g_lapic_x2=(lo>>10)&1; g_lapic_base=((u64)hi<<32)|(u64)(lo&0xFFFFF000u);
      if(g_lapic_base<0x1000ULL||g_lapic_base>=0xFFFFF000ULL) g_lapic_base=0xFEE00000ULL;
      if(!(lo&(1u<<11))){ lo|=(1u<<11); __asm__ volatile("wrmsr"::"a"(lo),"d"(hi),"c"(0x1B)); }
      lapic_wr(0xF0, lapic_rd(0xF0)|0x100u);
    }
    u32 tlen=(u32)(ap_tramp_end-ap_tramp_start); u8* dst=(u8*)0x8000ULL;
    for(u32 i=0;i<tlen;i++)dst[i]=ap_tramp_start[i];
    u64 cr3; __asm__ volatile("mov %%cr3,%0":"=r"(cr3));
    *(volatile u64*)0x8FF0ULL=cr3; *(volatile u64*)0x8FF8ULL=(u64)ap_main;
    u32 bsp=g_lapic_x2?lapic_rd(0x20):(lapic_rd(0x20)>>24); g_cpu[0].apic_id=bsp;
    int next=1;
    for(int i=0;i<nc&&next<NCPU;i++){ if(ids[i]==bsp) continue;
        percpu_setup(next,ids[i]); g_ncpu=next+1;
        *(volatile u64*)0x8FE0ULL=(u64)(g_ap_stacks[next]+16384);
        *(volatile int*)0x8FD8ULL=next;
        int before=g_cores_online;
        if(g_lapic_x2){ cpu_wrmsr(0x830,((u64)ids[i]<<32)|0x4500); ap_udelay(10000); cpu_wrmsr(0x830,((u64)ids[i]<<32)|0x4608); ap_udelay(200); cpu_wrmsr(0x830,((u64)ids[i]<<32)|0x4608); }
        else { lapic_wr(0x310,(u32)ids[i]<<24); lapic_wr(0x300,0x4500); ap_udelay(10000);
            lapic_wr(0x310,(u32)ids[i]<<24); lapic_wr(0x300,0x4608); ap_udelay(200);
            lapic_wr(0x310,(u32)ids[i]<<24); lapic_wr(0x300,0x4608); }
        for(int t=0;t<2000&&g_cores_online==before;t++)ap_udelay(100);
        if(g_cores_online==before){ g_ncpu=next; continue; }
        next++;
    }
    return nc;
}
