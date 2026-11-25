#include "include/nvx/base.h"
#include "include/nvx/drivers/input/evdev.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
typedef struct { u64 sec, usec; u16 type, code; int value; } lxev_t;
volatile int g_ev_open;
static lxev_t g_evq[EVDEV_RING]; static volatile u32 g_ev_w, g_ev_r; static volatile int g_ev_sl; static int g_ev_pend; static u8 g_ev_keys[96];
static inline u64 evd_lock(void){ u64 f=cpu_irq_save(); while(__sync_lock_test_and_set(&g_ev_sl,1)) __builtin_ia32_pause(); return f; }
static inline void evd_unlock(u64 f){ __sync_lock_release(&g_ev_sl); if(f&0x200)cpu_irq_on(); }
static void evd_put_locked(u16 t,u16 c,int v){ u64 ns=hr_ns(); if(g_ev_w-g_ev_r>=EVDEV_RING-1){ g_ev_r=g_ev_w; t=0; c=3; v=0; }
    lxev_t* e=&g_evq[g_ev_w%EVDEV_RING]; e->sec=ns/1000000000ull; e->usec=(ns%1000000000ull)/1000ull; e->type=t; e->code=c; e->value=v; g_ev_w++; }
static void evd_put(u16 t,u16 c,int v){ if(!g_ev_open)return; u64 f=evd_lock(); evd_put_locked(t,c,v); if(t)g_ev_pend=1; evd_unlock(f); }
void evdev_opened(void){ u64 f=evd_lock(); g_ev_open++; g_ev_r=g_ev_w; g_ev_pend=0; evd_unlock(f); }
int evdev_btn_code(int idx){ return idx<0||idx>15?-1:idx<8?0x110+idx:0x100+idx-8; }
void evdev_mouse_btn(int idx,int down){ int c=evdev_btn_code(idx); if(c<0)return; if(down)g_ev_keys[c>>3]|=(u8)(1u<<(c&7)); else g_ev_keys[c>>3]&=(u8)~(1u<<(c&7)); evd_put(1,(u16)c,down?1:0); }
void evdev_syn(void){ if(!g_ev_open)return; u64 f=evd_lock(); if(g_ev_pend){ evd_put_locked(0,0,0); g_ev_pend=0; } evd_unlock(f); }
void evdev_rel2(int dx,int dy){ if(dx)evd_put(2,0,dx); if(dy)evd_put(2,1,-dy); evdev_syn(); }
void evdev_wheel(int v120,int h120,int vn,int hn){ if(vn)evd_put(2,8,vn); if(v120)evd_put(2,0x0b,v120); if(hn)evd_put(2,6,hn); if(h120)evd_put(2,0x0c,h120); evdev_syn(); }
int evdev_ready(void){ return g_ev_w!=g_ev_r; }
int evdev_read(char* buf,int len){ int c=0; u64 f=evd_lock(); while(c+(int)sizeof(lxev_t)<=len&&g_ev_r!=g_ev_w){ __builtin_memcpy(buf+c,&g_evq[g_ev_r%EVDEV_RING],sizeof(lxev_t)); g_ev_r++; c+=(int)sizeof(lxev_t); } evd_unlock(f); return c; }
static int evd_bits(u8* out,int cap,const u8* b,int n){ int k=cap<n?cap:n; for(int i=0;i<k;i++)out[i]=b[i]; return k; }
int evdev_ioctl(u64 req,u8* out,int cap){
    u32 r=(u32)req, dir=r>>30, sz=(r>>16)&0x3FFF, ty=(r>>8)&0xFF, nr=r&0xFF; if(ty!=0x45)return -25;
    if(dir==1){ if(nr==0x90||nr==0xa0||nr==0x03)return 0; return -22; }
    int n=(int)sz<cap?(int)sz:cap; if(dir!=2)return -25;
    if(nr==0x01){ int v=0x010001; if(n<4)return -22; __builtin_memcpy(out,&v,4); return 4; }
    if(nr==0x02){ u16 id[4]={3,0x1d6b,0x0001,0x0111}; if(n<8)return -22; __builtin_memcpy(out,id,8); return 8; }
    if(nr==0x03){ int rp[2]={250,33}; if(n<8)return -22; __builtin_memcpy(out,rp,8); return 8; }
    if(nr==0x84){ int z=0; if(n<4)return -22; __builtin_memcpy(out,&z,4); return 4; }
    if(nr==0x06){ static const char nm[]="NoovexOS Pointer"; int k=n<(int)sizeof nm?n:(int)sizeof nm; for(int i=0;i<k;i++)out[i]=(u8)nm[i]; if(k&&k==n)out[k-1]=0; return k; }
    if(nr==0x07||nr==0x08)return -2;
    if(nr==0x09||nr==0x19||nr==0x1a||nr==0x1b){ for(int i=0;i<n;i++)out[i]=0; return n; }
    if(nr==0x18){ u8 k[96]; u64 f=evd_lock(); __builtin_memcpy(k,g_ev_keys,96); evd_unlock(f); return evd_bits(out,n,k,96); }
    if(nr>=0x20&&nr<0x40){ int ev=(int)nr-0x20; u8 b[96]; for(int i=0;i<96;i++)b[i]=0;
        if(ev==0){ b[0]=0x07; return evd_bits(out,n,b,1); }
        if(ev==1){ for(int i=0;i<16;i++){ int c=evdev_btn_code(i); b[c>>3]|=(u8)(1u<<(c&7)); } return evd_bits(out,n,b,96); }
        if(ev==2){ b[0]=0x43; b[1]=0x19; return evd_bits(out,n,b,2); }
        return evd_bits(out,n,b,0); }
    return -25; }
