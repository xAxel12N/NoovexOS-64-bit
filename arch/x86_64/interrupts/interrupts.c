#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/lib/base/mem.h"
#include "include/nvx/arch/x86_64/user/ring3.h"
#include "include/nvx/core/proc/sched.h"
#include "include/nvx/lib/js/js.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/drivers/input/ps2/ps2.h"
#include "include/nvx/arch/x86_64/interrupts/interrupts.h"
#include "include/nvx/core/panic.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/sys/native/syscall.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/arch/x86_64/smp/smp_sched.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/sys/linux/linux.h"
volatile u32 g_stray_n=0; volatile int g_stray_vec=-1;
/* =========================================================================
   IDT / PIC / PIT
   ========================================================================= */

struct idt_entry idt[256];
extern void* isr_stubs[48];

void set_gate(int n,void*h){
    u64 a=(u64)h;
    idt[n].lo=a&0xFFFF; idt[n].sel=0x08; idt[n].ist=0; idt[n].attr=0x8E;
    idt[n].mid=(a>>16)&0xFFFF; idt[n].hi=(u32)(a>>32); idt[n].z=0;
}
void idt_set128(void){   /* INT 0x80 gate, DPL=3 (callable from ring 3) */
    u64 a=(u64)isr128;
    idt[0x80].lo=a&0xFFFF; idt[0x80].sel=0x08; idt[0x80].ist=0; idt[0x80].attr=0xEE;
    idt[0x80].mid=(a>>16)&0xFFFF; idt[0x80].hi=(u32)(a>>32); idt[0x80].z=0;
}
void idt_use_ist(int n,int ist){ idt[n].ist=(u8)ist; }
void pic_remap(void){
    outb(0x20,0x11);io_wait(); outb(0xA0,0x11);io_wait();
    outb(0x21,0x20);io_wait(); outb(0xA1,0x28);io_wait();
    outb(0x21,0x04);io_wait(); outb(0xA1,0x02);io_wait();
    outb(0x21,0x01);io_wait(); outb(0xA1,0x01);io_wait();
    outb(0x21,0xF8);io_wait();            /* master: enable IRQ0,1,2      */
    outb(0xA1,0xEF);io_wait();            /* slave:  enable IRQ12         */
}
void pit_init(u32 hz){
    u32 div=1193182u/hz; outb(0x43,0x36);
    outb(0x40,div&0xFF); outb(0x40,(div>>8)&0xFF);
}

/* register frame pushed by isr_common (order must match the asm) */

   /* fwd */

struct vfile;  

static void isr_body(regs_t*r){
    if(r->vector==14&&vm_user_pf(r))return;
    if(r->vector<32){ if(g_js_running&&(r->cs&3)==3){ jc_fault(r); return; } { extern int web_fault_redirect(u64*,u64*); if((r->cs&3)==0&&r->vector!=2&&r->vector!=8&&r->vector!=18&&web_fault_redirect(&r->rip,&r->rsp))return; } panic(r,0); return; }        /* exception -> RSOD */
    if(r->vector==HR_LAPIC_VEC){ hr_lapic_isr(); return; }
    int irq=(int)r->vector-32;
    if(irq==0){
        if(g_js_running&&(r->cs&3)==3) jc_timer(r);
        if(g_preempt_on){
            int cur=g_curproc;
            if(cur>=0 && cur<MAXPROC && g_proc[cur].used){ memcpy(&g_proc[cur].ctx, r, sizeof(g_proc[cur].ctx)); g_proc[cur].started=1; }
            int nxt=-1; for(int i=1;i<=MAXPROC;i++){ int j=(((cur<0?0:cur)+i)%MAXPROC); if(g_proc[j].used&&g_proc[j].alive){ nxt=j; break; } }
            pic_eoi(0);
            if(nxt<0){ g_preempt_on=0; k_longjmp(g_preempt_kern,1); }   /* all processes exited -> back to kernel */
            g_curproc=nxt; g_tss.rsp0=(u64)(g_pstack[nxt]+16384);
            __asm__ volatile("mov %0,%%cr3"::"r"(g_proc[nxt].cr3):"memory");
            memcpy(r, &g_proc[nxt].ctx, sizeof(g_proc[nxt].ctx));   /* isr_common restores this + iretq into the next proc */
            return;
        }
    }
    else if(irq==1||irq==12) ps2_byte();
    if(r->vector==255){ extern void lapic_eoi_any(void); extern int lapic_isr_top(void); int tv=lapic_isr_top(); if(tv>=0)g_stray_vec=tv; g_stray_n++; lapic_eoi_any(); return; }
    pic_eoi(irq);
    if(irq==0){ extern volatile u32 g_ml_beat; extern volatile int g_ml_stage; static u32 lb=0; static u32 still=0; static int shown=0;
        if(g_ml_beat!=lb){ lb=g_ml_beat; still=0; } else if(++still==600&&!shown){ shown=1; extern void early_note(const char*,const char*,u64,int); early_note("NoovexOS main loop has not run for 6 seconds - please take a photo.","stage",r->rip,g_ml_stage); } }
}

void isr_dispatch(regs_t*r){
    if(r->vector==2)return;
    u64 v=r->vector; int user=(r->cs&3)==3;
    if(v>=0xF0&&v<=0xF2){ smp_ipi(r,user); return; }
    if(v==HR_LAPIC_VEC&&cpu_id()!=0){ ap_timer(r,user); return; }
    if(v==HR_LAPIC_VEC&&!user){ hr_lapic_isr(); return; }
    if(v==32)g_ticks++;
    bkl_lock(); isr_body(r); bkl_unlock();
}
 
void lsys_c(struct sysregs* r){ bkl_lock(); PCPU->syscalls++; linux_syscall(r); bkl_unlock(); }
void isr128_c(struct sysregs* r){ bkl_lock(); PCPU->syscalls++; syscall_dispatch(r); bkl_unlock(); }
