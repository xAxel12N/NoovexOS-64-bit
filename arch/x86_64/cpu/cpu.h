static inline void cpu_irq_on(void){ __asm__ volatile("sti":::"memory"); }
static inline void cpu_irq_off(void){ __asm__ volatile("cli":::"memory"); }
static inline void cpu_pause(void){ __asm__ volatile("pause"); }
static inline void cpu_halt(void){ __asm__ volatile("hlt"); }
static inline void cpu_idle(void){ __asm__ volatile("sti; hlt":::"memory"); }
static inline void cpu_stop(void){ __asm__ volatile("cli; hlt":::"memory"); }
static inline void cpu_wmb(void){ __asm__ volatile("sfence":::"memory"); }
static inline void cpu_mb(void){ __asm__ volatile("mfence":::"memory"); }
static inline void cpu_clflush(u64 a){ __asm__ volatile("clflush (%0)"::"r"(a):"memory"); }
static inline void cpu_copy64(void* d,const void* s,unsigned long n){ __asm__ volatile("rep movsq":"+D"(d),"+S"(s),"+c"(n)::"memory"); }
static inline void cpu_vmw_backdoor(u32* a,u32* b,u32* c,u32* d,u32* si,u32* di){ __asm__ volatile("inl %%dx,%%eax":"+a"(*a),"+b"(*b),"+c"(*c),"+d"(*d),"+S"(*si),"+D"(*di)::"memory"); }
static inline u64 cpu_irq_save(void){ u64 f; __asm__ volatile("pushfq; popq %0; cli":"=r"(f)::"memory"); return f; }
static inline void cpu_irq_restore(u64 f){ if(f&0x200)__asm__ volatile("sti":::"memory"); }
static inline u64 cpu_get_pt(void){ u64 v; __asm__ volatile("mov %%cr3,%0":"=r"(v)); return v; }
static inline void cpu_set_pt(u64 v){ __asm__ volatile("mov %0,%%cr3"::"r"(v):"memory"); }
static inline void cpu_flush_page(u64 v){ __asm__ volatile("invlpg (%0)"::"r"(v):"memory"); }
static inline void cpu_cache_flush(void){ __asm__ volatile("wbinvd":::"memory"); }
static inline u64 cpu_tsc(void){ u32 a,d; __asm__ volatile("rdtsc":"=a"(a),"=d"(d)); return ((u64)d<<32)|a; }
static inline int cpu_rand32(u32* x){ unsigned char ok=0; __asm__ volatile("rdrand %0; setc %1":"=r"(*x),"=qm"(ok)::"cc"); return ok; }
static inline int cpu_seed32(u32* x){ unsigned char ok=0; __asm__ volatile("rdseed %0; setc %1":"=r"(*x),"=qm"(ok)::"cc"); return ok; }
static inline void cpu_cpuid(u32 leaf,u32 sub,u32* a,u32* b,u32* c,u32* d){ __asm__ volatile("cpuid":"=a"(*a),"=b"(*b),"=c"(*c),"=d"(*d):"a"(leaf),"c"(sub)); }
static inline u64 cpu_sp(void){ u64 v; __asm__ volatile("mov %%rsp,%0":"=r"(v)); return v; }
static inline void cpu_fpu_save(void* p){ __asm__ volatile("fxsave (%0)"::"r"(p):"memory"); }
static inline void cpu_fpu_restore(const void* p){ __asm__ volatile("fxrstor (%0)"::"r"(p):"memory"); }
static inline u16 cpu_bios_ebda_seg(void){ u16 v; __asm__ volatile("movw 0x40E, %0":"=r"(v)); return v; }
static inline void cpu_triple_fault(void){ struct { u16 l; u64 b; } __attribute__((packed)) nul={0,0}; __asm__ volatile("lidt %0; int3"::"m"(nul)); }
static inline u64 cpu_rdmsr(u32 m){ u32 a,d; __asm__ volatile("rdmsr":"=a"(a),"=d"(d):"c"(m)); return ((u64)d<<32)|a; }
static inline void cpu_wrmsr(u32 m,u64 v){ __asm__ volatile("wrmsr"::"a"((u32)v),"d"((u32)(v>>32)),"c"(m)); }
static inline void cpu_set_fsbase(u64 v){ cpu_wrmsr(0xC0000100,v); }
static inline void* cpu_percpu(void){ void* p; __asm__("mov %%gs:16,%0":"=r"(p)); return p; }
static inline void cpu_set_percpu(void* p){ cpu_wrmsr(0xC0000101,(u64)p); cpu_wrmsr(0xC0000102,0); }
static inline u64 cpu_cr2(void){ u64 v; __asm__ volatile("mov %%cr2,%0":"=r"(v)); return v; }
static inline void cpu_copy(void* d,const void* s,unsigned long n){ __asm__ volatile("rep movsb":"+D"(d),"+S"(s),"+c"(n)::"memory"); }
