extern void h_set_pt(u64 v); extern u64 h_get_pt(void); extern void h_flush_page(u64 v);
extern void h_fpu_save(void* p); extern void h_fpu_restore(const void* p); extern void h_fatal(const char* m);
static inline void cpu_irq_on(void){}
static inline void cpu_irq_off(void){}
static inline void cpu_pause(void){ __asm__ volatile("pause"); }
static inline void cpu_halt(void){}
static inline void cpu_idle(void){}
static inline void cpu_stop(void){ h_fatal("cpu_stop"); }
static inline void cpu_wmb(void){ __asm__ volatile("sfence":::"memory"); }
static inline u64 cpu_irq_save(void){ return 0x202; }
static inline void cpu_irq_restore(u64 f){ (void)f; }
static inline u64 cpu_get_pt(void){ return h_get_pt(); }
static inline void cpu_set_pt(u64 v){ h_set_pt(v); }
static inline void cpu_flush_page(u64 v){ h_flush_page(v); }
static inline void cpu_cache_flush(void){}
static inline u64 cpu_tsc(void){ u32 a,d; __asm__ volatile("rdtsc":"=a"(a),"=d"(d)); return ((u64)d<<32)|a; }
static inline int cpu_rand32(u32* x){ *x=(u32)cpu_tsc()*2654435761u; return 1; }
static inline int cpu_seed32(u32* x){ return cpu_rand32(x); }
static inline void cpu_cpuid(u32 leaf,u32 sub,u32* a,u32* b,u32* c,u32* d){ __asm__ volatile("cpuid":"=a"(*a),"=b"(*b),"=c"(*c),"=d"(*d):"a"(leaf),"c"(sub)); }
static inline u64 cpu_sp(void){ u64 v; __asm__ volatile("mov %%rsp,%0":"=r"(v)); return v; }
static inline void cpu_fpu_save(void* p){ h_fpu_save(p); }
static inline void cpu_fpu_restore(const void* p){ h_fpu_restore(p); }
static inline u16 cpu_bios_ebda_seg(void){ return 0; }
static inline void cpu_triple_fault(void){ h_fatal("triple fault"); }
static inline u64 cpu_rdmsr(u32 m){ (void)m; return 0; }
static inline void cpu_wrmsr(u32 m,u64 v){ (void)m; (void)v; }
static inline void cpu_set_fsbase(u64 v){ (void)v; }
static inline void cpu_copy(void* d,const void* s,unsigned long n){ __asm__ volatile("rep movsb":"+D"(d),"+S"(s),"+c"(n)::"memory"); }
static void* g_host_pcpu;
static inline void* cpu_percpu(void){ return g_host_pcpu; }
static inline void cpu_set_percpu(void* p){ g_host_pcpu=p; }
