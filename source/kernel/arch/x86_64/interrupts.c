/* =========================================================================
   IDT / PIC / PIT
   ========================================================================= */
static int sig_self_check(void); extern int g_sig_ok;
struct idt_entry{ u16 lo; u16 sel; u8 ist; u8 attr; u16 mid; u32 hi; u32 z; } __attribute__((packed));
struct idt_ptr{ u16 limit; u64 base; } __attribute__((packed));
static struct idt_entry idt[256];
extern void* isr_stubs[48];
extern void  isr_ignore(void);
static void set_gate(int n,void*h){
    u64 a=(u64)h;
    idt[n].lo=a&0xFFFF; idt[n].sel=0x08; idt[n].ist=0; idt[n].attr=0x8E;
    idt[n].mid=(a>>16)&0xFFFF; idt[n].hi=(u32)(a>>32); idt[n].z=0;
}
static void idt_set128(void){   /* INT 0x80 gate, DPL=3 (callable from ring 3) */
    u64 a=(u64)isr128;
    idt[0x80].lo=a&0xFFFF; idt[0x80].sel=0x08; idt[0x80].ist=0; idt[0x80].attr=0xEE;
    idt[0x80].mid=(a>>16)&0xFFFF; idt[0x80].hi=(u32)(a>>32); idt[0x80].z=0;
}
static void pic_remap(void){
    outb(0x20,0x11);io_wait(); outb(0xA0,0x11);io_wait();
    outb(0x21,0x20);io_wait(); outb(0xA1,0x28);io_wait();
    outb(0x21,0x04);io_wait(); outb(0xA1,0x02);io_wait();
    outb(0x21,0x01);io_wait(); outb(0xA1,0x01);io_wait();
    outb(0x21,0xF8);io_wait();            /* master: enable IRQ0,1,2      */
    outb(0xA1,0xEF);io_wait();            /* slave:  enable IRQ12         */
}
static void pit_init(u32 hz){
    u32 div=1193182u/hz; outb(0x43,0x36);
    outb(0x40,div&0xFF); outb(0x40,(div>>8)&0xFF);
}
static inline void pic_eoi(int irq){ if(irq>=8)outb(0xA0,0x20); outb(0x20,0x20); }

/* register frame pushed by isr_common (order must match the asm) */
typedef struct {
    u64 r15,r14,r13,r12,r11,r10,r9,r8,rbp,rdi,rsi,rdx,rcx,rbx,rax;
    u64 vector,errcode,rip,cs,rflags,rsp,ss;
} regs_t;

void panic(regs_t*r,const char*msg);   /* fwd */
#define JS_BLOCK (-1000)
static volatile int g_js_running; static int g_jcur; static int g_umask; extern int g_dcwd; extern int g_uid; static void dt_setmode(int n,unsigned m);
static char g_hostname[32]; static int ux_uid_name(int uid,char* out,int cap); static int ux_lookup(const char* p,int* isdir,int follow); static void ux_touch(int k,int data); static int k_split(const char* p,int* dir,char* base);
static void nvx3_flush_meta(void); static int nvx3_sync(int k); static void nvx3_sync_all(void); static int nvx3_try_load(void); static int g_n3_conv; static int g_fb0_open, g_mouse_open, g_fb0_dirty;

static int ux_unlink(const char* p); static int ux_link(const char* a,const char* b); static int ux_symlink(const char* a,const char* b); static int ux_readlink(const char* p,char* o,int c); static int ux_stat2(const char* p,u32* o,int f); static int ux_chown(const char* p,int u,int g,int f); static int ux_chmod(const char* p,unsigned m); static int ux_utime(const char* p,u32 a,u32 m); static int ux_mount(const char* s,const char* t); static int ux_umount(const char* t);
struct vfile; static int ux_dev_read(struct vfile* v,char* buf,int len); static int ux_dev_write(struct vfile* v,const char* buf,int len);
static void jc_fault(regs_t* r); static void jc_timer(regs_t* r);

void isr_dispatch(regs_t*r){
    if(r->vector<32){ if(g_js_running&&(r->cs&3)==3){ jc_fault(r); return; } panic(r,0); return; }        /* exception -> RSOD */
    int irq=(int)r->vector-32;
    if(irq==0){ g_ticks++;
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
    if(r->vector!=255) pic_eoi(irq);
}

/* ---- PS/2 mouse init (bounded waits so a missing mouse never hangs) ---- */
static int pw_write(void){ int t=200000; while((inb(0x64)&2)&&--t)io_wait(); return t; }
static int pw_read (void){ int t=200000; while(!(inb(0x64)&1)&&--t)io_wait(); return t; }
static int mcmd(u8 c){ if(!pw_write())return 0; outb(0x64,0xD4); if(!pw_write())return 0; outb(0x60,c);
    if(!pw_read())return 0; inb(0x60); return 1; }
static int g_mouse_ok=0;
static void mouse_rate(u8 r){ mcmd(0xF3); mcmd(r); }
static void mouse_init(void){
    if(!pw_write())return; outb(0x64,0xA8); io_wait();           /* enable aux */
    if(!pw_write())return; outb(0x64,0x20);                       /* read cfg   */
    if(!pw_read())return; u8 st=inb(0x60);
    st|=2; st&=~0x20;
    if(!pw_write())return; outb(0x64,0x60);
    if(!pw_write())return; outb(0x60,st);
    if(!mcmd(0xF6))return;                                        /* defaults   */
    mouse_rate(200); mouse_rate(100); mouse_rate(80);            /* magic knock */
    if(mcmd(0xF2) && pw_read()){ u8 id=inb(0x60); if(id==3) g_mbytes=4; }
    if(!mcmd(0xF4))return;                                        /* enable rpt */
    g_mouse_ok=1;
}


