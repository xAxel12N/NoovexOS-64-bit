#include "include/nvx/base.h"
#include "include/nvx/drivers/input/input.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/drivers/input/evdev.h"
volatile int mouse_hscroll; volatile int g_gesture;
volatile int mouse_xback, mouse_xfwd; static int g_xb_prev;
volatile u32 mouse_btnmask; volatile u32 mouse_xpress[16]; volatile int mouse_scroll_hr, mouse_hscroll_hr; static int g_hr_v, g_hr_h;
static inline void in_hr_clamp(void){ if(mouse_scroll_hr>(1<<28)||mouse_scroll_hr<-(1<<28))mouse_scroll_hr=0; if(mouse_hscroll_hr>(1<<28)||mouse_hscroll_hr<-(1<<28))mouse_hscroll_hr=0; }
void in_xbtn(int b4,int b5){ b4=b4?1:0; b5=b5?1:0; if(b4&&!(g_xb_prev&1)){ mouse_xback++; mouse_evt=1; } if(b5&&!(g_xb_prev&2)){ mouse_xfwd++; mouse_evt=1; } g_xb_prev=b4|(b5<<1); in_btnmask((mouse_btnmask&~0x18u)|(u32)(b4<<3)|(u32)(b5<<4)); }
static volatile int g_in_sl;
static inline u64 in_lock(void){ u64 f=cpu_irq_save(); while(__sync_lock_test_and_set(&g_in_sl,1)) __builtin_ia32_pause(); return f; }
static inline void in_unlock(u64 f){ __sync_lock_release(&g_in_sl); if(f&0x200)cpu_irq_on(); }
void in_rel(int dx,int dy){ if(!dx&&!dy)return; u64 f=in_lock(); mouse_dx+=dx; mouse_dy-=dy; mouse_evt=1; in_unlock(f); evdev_rel2(dx,dy); }
#define MQ_N IN_MQ_N
static struct { int dx, dy, b; u64 t; } g_inq[MQ_N]; static volatile u32 g_mq_w=0, g_mq_r=0; volatile u32 g_mq_lost=0;
static void in_btn_locked(int l,int r,int m){ l=l?1:0; r=r?1:0; if((l&&!mouse_btn)||(r&&!mouse_rbtn))mouse_press_t=g_ticks; if(l&&!mouse_btn)mouse_lpress++; if(r&&!mouse_rbtn)mouse_rpress++;
    if(l!=mouse_btn||r!=mouse_rbtn){ if(g_mq_w-g_mq_r<MQ_N){ u32 i=g_mq_w%MQ_N; g_inq[i].dx=mouse_dx; g_inq[i].dy=mouse_dy; g_inq[i].b=l|(r<<1); g_inq[i].t=g_ticks; g_mq_w++; } else g_mq_lost++; mouse_evt=1; }
    m=m?1:0; if(m&&!mouse_mid){ mouse_mpress++; mouse_evt=1; } mouse_btn=l; mouse_rbtn=r; mouse_mid=m; }
void in_btn_raw(int l,int r,int m){ u64 f=in_lock(); in_btn_locked(l,r,m); in_unlock(f); in_btnmask((mouse_btnmask&~7u)|(l?1u:0)|(r?2u:0)|(m?4u:0)); }
static inline int mq_sane(void){ u32 n=g_mq_w-g_mq_r; if(n>MQ_N){ g_mq_r=g_mq_w; g_mq_lost++; return 0; } return (int)n; }
int in_mq_take_locked(int* dx,int* dy,int* b,u64* t){ if(!mq_sane())return 0; u32 i=g_mq_r%MQ_N; *dx=g_inq[i].dx; *dy=g_inq[i].dy; *b=g_inq[i].b; *t=g_inq[i].t; g_mq_r++; return 1; }
int in_mq_take(int* dx,int* dy,int* b,u64* t){ u64 f=in_lock(); int ok=in_mq_take_locked(dx,dy,b,t); in_unlock(f); return ok; }
u64 in_lock_x(void){ return in_lock(); }
void in_unlock_x(u64 f){ in_unlock(f); }
int in_mq_pending(void){ return (u32)(g_mq_w-g_mq_r)-1u<MQ_N; }
void in_mq_clear(void){ u64 f=in_lock(); g_mq_r=g_mq_w; in_unlock(f); }
void in_mq_rebase_locked(int dx,int dy){ if(!dx&&!dy)return; u32 n=(u32)mq_sane(), r=g_mq_r; for(u32 k=r;k!=r+n;k++){ g_inq[k%MQ_N].dx-=dx; g_inq[k%MQ_N].dy-=dy; } }
void in_mq_rebase(int dx,int dy){ if(!dx&&!dy)return; u64 f=in_lock(); in_mq_rebase_locked(dx,dy); in_unlock(f); }
void in_buttons(int l,int r,int m){ u64 f=in_lock(); in_btn_locked(l,r,m); mouse_evt=1; in_unlock(f); in_btnmask((mouse_btnmask&~7u)|(l?1u:0)|(r?2u:0)|(m?4u:0)); }
void in_scroll(int v,int h){ if(!v&&!h)return; u64 f=in_lock(); in_hr_clamp(); mouse_scroll+=v; mouse_hscroll+=h; mouse_scroll_hr+=v*120; mouse_hscroll_hr+=h*120; mouse_evt=1; in_unlock(f); evdev_wheel(v*120,h*120,v,h); }
void in_gesture(int g){ if(g)g_gesture=g; }
void in_btnmask(u32 m){ m&=0xFFFF; u32 o=mouse_btnmask, c=o^m; if(!c)return; for(int i=0;i<16;i++) if((c>>i)&1){ if((m>>i)&1)mouse_xpress[i]++; evdev_mouse_btn(i,(m>>i)&1); } mouse_btnmask=m; mouse_evt=1; evdev_syn(); }
void in_scroll_hr(int v,int h){ if(!v&&!h)return; u64 f=in_lock(); in_hr_clamp(); mouse_scroll_hr+=v; mouse_hscroll_hr+=h; g_hr_v+=v; g_hr_h+=h; int nv=g_hr_v/120, nh=g_hr_h/120; g_hr_v-=nv*120; g_hr_h-=nh*120; mouse_scroll+=nv; mouse_hscroll+=nh; mouse_evt=1; in_unlock(f); evdev_wheel(v,h,nv,nh); }
#define GKEY_N 256
extern volatile unsigned char kdown[128];
static volatile u16 g_gk[GKEY_N]; static volatile u32 g_gk_w; static u32 g_gk_r[2]; static u8 g_gk_prev[2][128]; static u64 g_gk_t[2];
void gkey_push(int sc,int down){ if(sc<=0||sc>=128)return; g_gk[g_gk_w%GKEY_N]=(u16)(sc|(down?0x100:0)); __sync_synchronize(); g_gk_w++; }
void gkey_reset(int who){ if(who<0||who>1)return; g_gk_r[who]=g_gk_w; g_gk_t[who]=g_ticks; for(int i=0;i<128;i++)g_gk_prev[who][i]=kdown[i]?1:0; }
int gkey_pop(int who,int* down){
    if(who<0||who>1)return -1;
    u32 w=g_gk_w; if(w-g_gk_r[who]>GKEY_N||g_ticks-g_gk_t[who]>50)g_gk_r[who]=w; g_gk_t[who]=g_ticks;
    if(g_gk_r[who]!=w){ u16 e=g_gk[g_gk_r[who]%GKEY_N]; g_gk_r[who]++; int sc=e&0x7F, d=(e>>8)&1; g_gk_prev[who][sc]=(u8)d; *down=d; return sc; }
    for(int sc=1;sc<128;sc++){ u8 st=kdown[sc]?1:0; if(st!=g_gk_prev[who][sc]){ g_gk_prev[who][sc]=st; *down=st; return sc; } }
    return -1; }
int gkey_pending(int who){ return who>=0&&who<=1&&g_gk_w!=g_gk_r[who]; }
