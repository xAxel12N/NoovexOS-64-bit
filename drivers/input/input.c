#include "include/nvx/base.h"
#include "include/nvx/drivers/input/input.h"
#include "include/nvx/net/http/http.h"
volatile int mouse_hscroll; volatile int g_gesture;
volatile int mouse_xback, mouse_xfwd; static int g_xb_prev;
void in_xbtn(int b4,int b5){ b4=b4?1:0; b5=b5?1:0; if(b4&&!(g_xb_prev&1)){ mouse_xback++; mouse_evt=1; } if(b5&&!(g_xb_prev&2)){ mouse_xfwd++; mouse_evt=1; } g_xb_prev=b4|(b5<<1); }
static inline u64 in_lock(void){ u64 f; f=cpu_irq_save(); return f; }
static inline void in_unlock(u64 f){ if(f&0x200)cpu_irq_on(); }
void in_rel(int dx,int dy){ if(!dx&&!dy)return; u64 f=in_lock(); mouse_dx+=dx; mouse_dy-=dy; mouse_evt=1; in_unlock(f); }
#define MQ_N IN_MQ_N
static struct { int dx, dy, b; u64 t; } g_inq[MQ_N]; static volatile u32 g_mq_w=0, g_mq_r=0; volatile u32 g_mq_lost=0;
void in_btn_raw(int l,int r,int m){ l=l?1:0; r=r?1:0; if((l&&!mouse_btn)||(r&&!mouse_rbtn))mouse_press_t=g_ticks; if(l&&!mouse_btn)mouse_lpress++; if(r&&!mouse_rbtn)mouse_rpress++;
    if(l!=mouse_btn||r!=mouse_rbtn){ if(g_mq_w-g_mq_r<MQ_N){ u32 i=g_mq_w%MQ_N; g_inq[i].dx=mouse_dx; g_inq[i].dy=mouse_dy; g_inq[i].b=l|(r<<1); g_inq[i].t=g_ticks; g_mq_w++; } else g_mq_lost++; mouse_evt=1; }
    m=m?1:0; if(m&&!mouse_mid){ mouse_mpress++; mouse_evt=1; } mouse_btn=l; mouse_rbtn=r; mouse_mid=m; }
int in_mq_take(int* dx,int* dy,int* b,u64* t){ if(g_mq_r==g_mq_w)return 0; u32 i=g_mq_r%MQ_N; *dx=g_inq[i].dx; *dy=g_inq[i].dy; *b=g_inq[i].b; *t=g_inq[i].t; g_mq_r++; return 1; }
int in_mq_pending(void){ return g_mq_r!=g_mq_w; }
void in_mq_clear(void){ u64 f=in_lock(); g_mq_r=g_mq_w; in_unlock(f); }
void in_mq_rebase_locked(int dx,int dy){ if(!dx&&!dy)return; for(u32 k=g_mq_r;k!=g_mq_w;k++){ g_inq[k%MQ_N].dx-=dx; g_inq[k%MQ_N].dy-=dy; } }
void in_mq_rebase(int dx,int dy){ if(!dx&&!dy)return; u64 f=in_lock(); for(u32 k=g_mq_r;k!=g_mq_w;k++){ g_inq[k%MQ_N].dx-=dx; g_inq[k%MQ_N].dy-=dy; } in_unlock(f); }
void in_buttons(int l,int r,int m){ u64 f=in_lock(); in_btn_raw(l,r,m); mouse_evt=1; in_unlock(f); }
void in_scroll(int v,int h){ if(!v&&!h)return; u64 f=in_lock(); mouse_scroll+=v; mouse_hscroll+=h; mouse_evt=1; in_unlock(f); }
void in_gesture(int g){ if(g)g_gesture=g; }
