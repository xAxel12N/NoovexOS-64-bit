#include "include/nvx/base.h"
#include "include/nvx/drivers/input/input.h"
#include "include/nvx/net/http.h"
volatile int mouse_hscroll; volatile int g_gesture;
static inline u64 in_lock(void){ u64 f; f=cpu_irq_save(); return f; }
static inline void in_unlock(u64 f){ if(f&0x200)cpu_irq_on(); }
void in_rel(int dx,int dy){ if(!dx&&!dy)return; u64 f=in_lock(); mouse_dx+=dx; mouse_dy-=dy; mouse_evt=1; in_unlock(f); }
void in_buttons(int l,int r,int m){ u64 f=in_lock(); mouse_btn=l?1:0; mouse_rbtn=r?1:0; mouse_mid=m?1:0; mouse_evt=1; in_unlock(f); }
void in_scroll(int v,int h){ if(!v&&!h)return; u64 f=in_lock(); mouse_scroll+=v; mouse_hscroll+=h; mouse_evt=1; in_unlock(f); }
void in_gesture(int g){ if(g)g_gesture=g; }
