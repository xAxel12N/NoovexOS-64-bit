static u8 g_hio_cmos_idx;
static inline u8 hio_cmos(u8 r){ switch(r){ case 0: return 0x30; case 2: return 0x15; case 4: return 0x12; case 7: return 0x08; case 8: return 0x10; case 9: return 0x26; case 0x0A: return 0; case 0x0B: return 0x02; } return 0; }
static inline void outb(u16 p,u8 v){ if(p==0x70)g_hio_cmos_idx=v&0x7F; }
static inline u8   inb(u16 p){ if(p==0x71)return hio_cmos(g_hio_cmos_idx); return 0xFF; }
static inline void io_wait(void){}
static inline void outl(u16 p,u32 v){ (void)p; (void)v; }
static inline u32  inl(u16 p){ (void)p; return 0xFFFFFFFFu; }
static inline u16  inw(u16 p){ (void)p; return 0xFFFF; }
static inline void outw(u16 p,u16 v){ (void)p; (void)v; }
