#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/arch/x86_64/user/ring3.h"
#include "include/nvx/arch/x86_64/smp/smp.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/arch/x86_64/interrupts/interrupts.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/vfs/vfs.h"
#include "include/nvx/fs/nvx/nvxfs.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/arch/x86_64/smp/smp_sched.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/fs/nvx/nvx4.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/arch/x86_64/cpu/cpu_init.h"
  

static volatile int g_smp_sched=0;
volatile int g_ncpu=1;
#define IPI_RESCHED 0xF0
#define IPI_TLB     0xF1
#define IPI_HALT    0xF2

static void idt_load(void){ struct idt_ptr ip={(u16)(sizeof(idt)-1),(u64)idt}; __asm__ volatile("lidt %0"::"m"(ip)); }
static void lapic_eoi(void){ lapic_wr(0xB0,0); }
static void ipi_send(int cpu,u32 vec){
    if(cpu<0||cpu>=g_ncpu||!g_cpu[cpu].online)return;
    if(g_lapic_x2){ cpu_wrmsr(0x830,((u64)g_cpu[cpu].apic_id<<32)|(vec&0xFFu)); return; }
    for(int i=0;i<200000&&(lapic_rd(0x300)&(1u<<12));i++)cpu_pause();
    lapic_wr(0x310,g_cpu[cpu].apic_id<<24); lapic_wr(0x300,vec&0xFFu);
}
static void ipi_others(u32 vec){ int me=cpu_id(); for(int i=0;i<g_ncpu;i++) if(i!=me&&g_cpu[i].online)ipi_send(i,vec); }
void smp_kick(int cpu){ if(g_smp_on&&cpu>=0&&cpu!=cpu_id())ipi_send(cpu,IPI_RESCHED); }
void smp_kick_idle(void){ if(!g_smp_on)return; int me=cpu_id(); for(int i=1;i<g_ncpu;i++) if(i!=me&&g_cpu[i].online&&!g_cpu[i].js_running&&!g_cpu[i].stop)ipi_send(i,IPI_RESCHED); }
void smp_tlb_shootdown(void){ if(!g_smp_on)return; int me=cpu_id(); for(int i=0;i<g_ncpu;i++) if(i!=me&&g_cpu[i].online){ g_cpu[i].tlb_pending=1; ipi_send(i,IPI_TLB); } }
void smp_tlb_sync(u64 cr3){
    if(!g_smp_on||!cr3)return; int me=cpu_id(); u64 a=cr3&~0xFFFULL; int sent[NCPU]; int any=0;
    for(int i=0;i<g_ncpu&&i<NCPU;i++){ sent[i]=0; if(i!=me&&g_cpu[i].online&&g_cpu[i].js_running&&(g_cpu[i].ucr3&~0xFFFULL)==a){ g_cpu[i].tlb_pending=1; ipi_send(i,IPI_TLB); sent[i]=1; any=1; } }
    if(!any)return;
    for(int spin=0;spin<200000;spin++){ int wait=0;
        for(int i=0;i<g_ncpu&&i<NCPU;i++) if(sent[i]&&g_cpu[i].tlb_pending&&g_cpu[i].js_running&&(g_cpu[i].ucr3&~0xFFFULL)==a)wait=1;
        if(!wait)return; __asm__ volatile("pause"); }
}
void smp_halt_others(void){ if(g_ncpu>1)ipi_others(IPI_HALT); }

void smp_ipi(regs_t* r,int user){
    struct pcpu* c=PCPU; c->ipis++;
    u64 v=r->vector; lapic_eoi();
    if(v==IPI_TLB){ c->tlb_pending=0; cpu_set_pt(cpu_get_pt()); return; }
    if(v==IPI_HALT){ c->parked=1; for(;;)cpu_stop(); }
    if(v==IPI_RESCHED&&user&&c->js_running&&c->jcur>=0){ bkl_lock(); jc_preempt(r); }
}
void ap_timer(regs_t* r,int user){
    struct pcpu* c=PCPU; c->ticks++;
    if(user)c->user_ticks++; else if(!c->js_running&&!bkl_held())c->idle_ticks++;
    lapic_eoi();
    if(user&&c->js_running&&c->jcur>=0&&c->ticks-c->js_slice>=2){ bkl_lock(); jc_preempt(r); }
}
static int smp_work_step(void){
    if(!g_smp_work_on)return 0;
    int found=-1; smp_lock();
    for(int i=0;i<NJOBS;i++) if(g_jobs[i].state==1){ g_jobs[i].state=2; g_jobs[i].core=cpu_id(); found=i; break; }
    smp_unlock();
    if(found<0)return 0;
    u64 r=smp_compute(g_jobs[found].arg); g_jobs[found].result=r; __sync_synchronize(); g_jobs[found].state=3;
    int ci=cpu_id()&7; __sync_fetch_and_add(&g_core_load[ci],1); __sync_fetch_and_add(&g_core_cycles[ci],g_jobs[found].arg/1000);
    return 1;
}
static void ap_loop(void){
    struct pcpu* c=PCPU;
    for(;;){
        if(c->stop){ c->parked=1; while(c->stop){ cpu_idle(); cpu_irq_off(); } c->parked=0; }
        int ran=0;
        if(g_smp_sched){ bkl_lock(); int pick=g_smp_sched?jc_pick():-1; if(pick>=0){ jc_enter(pick); ran=1; } bkl_unlock(); }
        if(!ran&&!smp_work_step()){ cpu_idle(); cpu_irq_off(); }
    }
}
void ap_entry_c(int id){
    struct pcpu* c=&g_cpu[id];
    cpu_set_percpu(c);
    arch_fpu_init();
    { extern void pat_cpu_init(void); pat_cpu_init(); }
    idt_load();
    { u64 b=cpu_rdmsr(0x1B); if(g_lapic_x2&&!(b&(1ULL<<10)))cpu_wrmsr(0x1B,b|(1ULL<<10)|(1ULL<<11)); else if(!(b&(1ULL<<11)))cpu_wrmsr(0x1B,b|(1ULL<<11)); }
    lapic_wr(0x80,0); lapic_wr(0xF0,0x1FF); lapic_wr(0x350,0x10000); lapic_wr(0x360,0x10000);
    us_init();
    { u64 cnt=g_hr.lapic_hz/100ULL; if(cnt>0&&cnt<=0xFFFFFFFFULL){ lapic_wr(0x3E0,0x3); lapic_wr(0x320,HR_LAPIC_VEC|(1u<<17)); lapic_wr(0x380,(u32)cnt); } }
    c->online=1; __sync_fetch_and_add(&g_cores_online,1);
    cpu_irq_on();
    ap_loop();
}
int smp_enable(void){
    if(g_smp_on)return g_ncpu;
    if(!g_hr.lapic_ok||!g_hr.lapic_hz)return -1;
    set_gate(IPI_RESCHED,isr_ipi_resched); set_gate(IPI_TLB,isr_ipi_tlb); set_gate(IPI_HALT,isr_ipi_halt);
    if(g_cores_online<=1){ int d=bkl_drop(); smp_start_aps(); bkl_take(d); }
    if(g_cores_online<=1)return -2;
    for(int i=1;i<g_ncpu;i++)g_cpu[i].stop=0;
    g_smp_on=1; g_smp_sched=1;
    ipi_others(IPI_RESCHED);
    klog("smp: scheduling on all cores");
    return g_cores_online;
}
int smp_disable(void){
    if(!g_smp_on)return 0;
    g_smp_sched=0;
    for(int i=1;i<g_ncpu;i++) if(g_cpu[i].online)g_cpu[i].stop=1;
    ipi_others(IPI_RESCHED);
    int left=0;
    for(int t=0;t<400;t++){ left=0; for(int i=1;i<g_ncpu;i++) if(g_cpu[i].online&&!g_cpu[i].parked)left++; if(!left)break;
        int d=bkl_drop(); hr_delay_us(5000); bkl_take(d); }
    if(left)return -1;
    g_smp_on=0; klog("smp: secondary cores parked");
    return 0;
}

void smp_pref_set(const char* v){
    if(!disk_ok)return;
    int fd=vfs_open("/etc/smp",O_WRONLY|O_CREAT|O_TRUNC); if(fd<0)return;
    int n=0; while(v[n])n++; vfs_write(fd,v,n); vfs_write(fd,"\n",1); vfs_close(fd); nvx3_sync_all();
}
static int smp_pref_is(const char* v){ char b[16]; int n=ux_read_file("/etc/smp",b,sizeof b); if(n<=0)return 0; int i=0; while(v[i]&&b[i]==v[i])i++; return !v[i]&&(b[i]=='\n'||b[i]==0); }
static void smp_boot_auto(void){
    if(smp_pref_is("off")){ klog("smp: disabled in /etc/smp - staying on one core"); return; }
    if(smp_pref_is("starting")){ smp_pref_set("off"); klog("smp: the last start did not finish - multi-core turned off (smp on to retry)");
        notify("Multi-core turned off","Starting the other cores did not finish last time. Type smp on to try again."); return; }
    { u8 ids[NCPU]; if(madt_apic_ids(ids,NCPU)<=1&&acpi_count_cores()<=1){ klog("smp: one core - nothing to start"); return; } }
    smp_pref_set("starting");
    int r=smp_enable();
    smp_pref_set("on");
    if(r>1)klog("smp: started automatically at boot"); else klog("smp: no other cores came online");
}
void smp_boot_auto_c(void){ smp_boot_auto(); }
