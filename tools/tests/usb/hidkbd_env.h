#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
static u64 g_ticks; static u8 kdown[256]; static int kcaps;
static int mouse_dx, mouse_dy, mouse_evt, mouse_scroll, mouse_btn;
static void in_btn_raw(int l,int r,int m){ mouse_btn=(l?1:0)|(r?2:0)|(m?4:0); }
static void in_xbtn(int a,int b){ (void)a; (void)b; }
static void in_rel(int dx,int dy){ if(!dx&&!dy)return; mouse_dx+=dx; mouse_dy-=dy; mouse_evt=1; }
static void in_scroll(int v,int h){ (void)h; if(v){ mouse_scroll+=v; mouse_evt=1; } }
static char kq[512]; static int kqn; static void kpush(int c){ if(kqn<511)kq[kqn++]=(char)c; kq[kqn]=0; }
static int g_hk[32], g_hkn; void hk_push(int id){ if(g_hkn<32)g_hk[g_hkn++]=id; }
volatile u32 g_kbd_last_tick; int g_krep_delay=50, g_krep_rate=3, g_krep_flag;
