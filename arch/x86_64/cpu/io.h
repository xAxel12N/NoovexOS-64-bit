/* ---- port I/O ---- */
static inline void outb(u16 p,u8 v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p):"memory"); }
static inline u8   inb(u16 p){ u8 v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p):"memory"); return v; }
static inline void io_wait(void){ outb(0x80,0); }
static inline void outl(u16 p,u32 v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p):"memory"); }
static inline u32  inl(u16 p){ u32 v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p):"memory"); return v; }
static inline u16  inw(u16 p){ u16 v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p):"memory"); return v; }
static inline void outw(u16 p,u16 v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p):"memory"); }
