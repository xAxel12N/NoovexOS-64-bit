#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
static u64 cpu_irq_save(void){ return 0; } static void cpu_irq_on(void){}
static u64 g_ns=1500000000ull; static u64 hr_ns(void){ return g_ns; }
static u64 g_ticks; static volatile int mouse_dx,mouse_dy,mouse_evt,mouse_btn,mouse_rbtn,mouse_mid,mouse_scroll,mouse_lpress,mouse_rpress,mouse_mpress; static volatile u64 mouse_press_t;
volatile int mouse_hscroll; volatile int g_gesture;
#define IN_MQ_N 64
#include "evdev_h.h"
void in_btnmask(u32 m); void in_scroll_hr(int v,int h);
#include "evdev_k.c"
#include "input_k.c"
volatile unsigned char kdown[128];
typedef struct { u64 sec, usec; u16 type, code; int value; } ev_t;
static int fails, checks;
static void chk(const char* n,int ok){ checks++; if(!ok)fails++; printf("  %-74s %s\n",n,ok?"ok":"FAIL"); }
static int rd(ev_t* e,int max){ int n=evdev_read((char*)e,(int)sizeof(ev_t)*max); return n/(int)sizeof(ev_t); }
static int has(ev_t* e,int n,int t,int c,int v){ for(int i=0;i<n;i++) if(e[i].type==t&&e[i].code==c&&e[i].value==v)return 1; return 0; }
int main(void){
  ev_t e[600]; int n;
  printf("== Linux evdev pointer (/dev/input/event0) ==\n");
  chk("struct input_event is 24 bytes (x86-64 timeval + type/code/value)",sizeof(lxev_t)==24);
  in_rel(3,4); chk("nothing queued while no reader has the node open",!evdev_ready());
  evdev_opened();
  in_rel(5,-2); n=rd(e,16); chk("relative motion: REL_X 5, REL_Y 2 (screen-down positive), SYN_REPORT",n==3&&has(e,n,2,0,5)&&has(e,n,2,1,2)&&e[2].type==0&&e[2].code==0);
  chk("timestamp from the monotonic clock: 1 s 500000 us",e[0].sec==1&&e[0].usec==500000);
  in_btn_raw(1,0,0); n=rd(e,16); chk("left button: BTN_LEFT 1 + SYN",n==2&&has(e,n,1,0x110,1));
  in_btnmask(1|(1u<<3)|(1u<<4)); n=rd(e,16); chk("buttons 4/5: BTN_SIDE, BTN_EXTRA",has(e,n,1,0x113,1)&&has(e,n,1,0x114,1)&&!has(e,n,1,0x110,1));
  in_btnmask(1|(1u<<5)|(1u<<6)|(1u<<7)); n=rd(e,32); chk("buttons 6/7/8: BTN_FORWARD, BTN_BACK, BTN_TASK; 4/5 released",has(e,n,1,0x115,1)&&has(e,n,1,0x116,1)&&has(e,n,1,0x117,1)&&has(e,n,1,0x113,0)&&has(e,n,1,0x114,0));
  in_btnmask(1|(1u<<8)|(1u<<15)); n=rd(e,32); chk("buttons 9 and 16: BTN_0 and BTN_7",has(e,n,1,0x100,1)&&has(e,n,1,0x107,1));
  chk("desktop: press counters for buttons 9 and 16, mask kept",mouse_xpress[8]==1&&mouse_xpress[15]==1&&mouse_btnmask==(1u|(1u<<8)|(1u<<15)));
  { u8 k[96]; int r=evdev_ioctl(0x80604518ull,k,96); chk("EVIOCGKEY: BTN_LEFT, BTN_0, BTN_7 held",r==96&&(k[0x110>>3]&1)&&(k[0x100>>3]&1)&&(k[0x107>>3]&0x80)&&!(k[0x113>>3]&8)); }
  in_btnmask(0); n=rd(e,32); chk("all released: three key-up events, one SYN",n==4&&has(e,n,1,0x110,0)&&has(e,n,1,0x100,0)&&has(e,n,1,0x107,0)&&!mouse_btnmask);
  in_scroll(1,0); n=rd(e,16); chk("notch wheel: REL_WHEEL 1 + REL_WHEEL_HI_RES 120",has(e,n,2,8,1)&&has(e,n,2,11,120)&&mouse_scroll==1); mouse_scroll=0; mouse_scroll_hr=0;
  int lo=0, hi=0; for(int i=0;i<8;i++){ in_scroll_hr(15,0); n=rd(e,16); for(int j=0;j<n;j++){ if(e[j].type==2&&e[j].code==8)lo+=e[j].value; if(e[j].type==2&&e[j].code==11)hi+=e[j].value; } }
  chk("hi-res 8 x 15/120: REL_WHEEL_HI_RES every report (120), REL_WHEEL once (1)",hi==120&&lo==1&&mouse_scroll==1&&mouse_scroll_hr==120);
  in_scroll_hr(-50,30); in_scroll_hr(-50,30); in_scroll_hr(-50,30); n=rd(e,32); { int w=0,h=0,hh=0; for(int j=0;j<n;j++){ if(e[j].code==8&&e[j].type==2)w+=e[j].value; if(e[j].code==6&&e[j].type==2)h+=e[j].value; if(e[j].code==12&&e[j].type==2)hh+=e[j].value; }
    chk("hi-res -150 / +90: one notch down, remainder -30 kept; pan HI_RES 90, no pan notch yet",w==-1&&h==0&&hh==90); }
  for(int i=0;i<2000;i++)in_rel(1,1); n=rd(e,600); chk("ring overflow: bounded, SYN_DROPPED reported",n>0&&n<EVDEV_RING&&has(e,n,0,3,0));
  evdev_opened(); chk("re-open starts empty",!evdev_ready());
  { int v=0; chk("EVIOCGVERSION 0x010001",evdev_ioctl(0x80044501ull,(u8*)&v,4)==4&&v==0x010001); }
  { u16 id[4]; chk("EVIOCGID: bus USB",evdev_ioctl(0x80084502ull,(u8*)id,8)==8&&id[0]==3); }
  { char nm[64]; int r=evdev_ioctl(0x80404506ull,(u8*)nm,64); chk("EVIOCGNAME",r>0&&!strcmp(nm,"NoovexOS Pointer")); }
  { u8 b[8]; int r=evdev_ioctl(0x80084520ull,b,8); chk("EVIOCGBIT(0): EV_SYN, EV_KEY, EV_REL",r==1&&b[0]==7); }
  { u8 b[96]; int r=evdev_ioctl(0x80604521ull,b,96); int c=0; for(int i=0;i<768;i++)c+=(b[i>>3]>>(i&7))&1; chk("EVIOCGBIT(EV_KEY): 16 buttons, BTN_LEFT..BTN_TASK + BTN_0..BTN_7",r==96&&c==16&&(b[0x110>>3]&1)&&(b[0x117>>3]&0x80)&&(b[0x100>>3]&1)); }
  { u8 b[4]={0xff,0xff,0xff,0xff}; int r=evdev_ioctl(0x80044522ull,b,4); chk("EVIOCGBIT(EV_REL): X, Y, HWHEEL, WHEEL, WHEEL_HI_RES, HWHEEL_HI_RES",r==2&&b[0]==0x43&&b[1]==0x19&&b[2]==0xff); }
  chk("EVIOCGRAB accepted, unknown ioctl -> ENOTTY",evdev_ioctl(0x40044590ull,0,0)==0&&evdev_ioctl(0x80045401ull,0,0)==-25);
  chk("EVIOCGPHYS -> ENOENT",evdev_ioctl(0x80404507ull,(u8*)e,64)==-2);
  printf("== game key ring (gkey_push/gkey_pop) ==\n");
  { int d=-1; gkey_reset(0); gkey_reset(1); chk("empty ring: pop returns -1",gkey_pop(0,&d)==-1&&gkey_pop(1,&d)==-1);
    kdown[0x11]=1; gkey_push(0x11,1); kdown[0x11]=0; gkey_push(0x11,0);
    int s1=gkey_pop(1,&d), d1=d, s2=gkey_pop(1,&d), d2=d; chk("a tap between two polls: down then up, nothing lost",s1==0x11&&d1==1&&s2==0x11&&d2==0&&gkey_pop(1,&d)==-1);
    s1=gkey_pop(0,&d); d1=d; s2=gkey_pop(0,&d); d2=d; chk("the second consumer sees the same tap independently",s1==0x11&&d1==1&&s2==0x11&&d2==0&&gkey_pop(0,&d)==-1);
    kdown[0x1E]=1; chk("state change without a ring event (USB keyboard): reported from kdown",gkey_pop(1,&d)==0x1E&&d==1&&gkey_pop(1,&d)==-1);
    kdown[0x1E]=0; chk("...and its release",gkey_pop(1,&d)==0x1E&&d==0&&gkey_pop(1,&d)==-1);
    kdown[0x20]=1; for(int i=0;i<600;i++){ gkey_push(0x21,1); gkey_push(0x21,0); } chk("overflow: the ring drops to the live state, held key still reported",gkey_pop(1,&d)==0x20&&d==1&&gkey_pop(1,&d)==-1); kdown[0x20]=0; chk("...and released",gkey_pop(1,&d)==0x20&&d==0);
    gkey_push(0x11,1); gkey_push(0x11,0); g_ticks+=200; chk("a consumer that stopped polling for a while starts from the live state",gkey_pop(1,&d)==-1);
    gkey_push(0,1); gkey_push(128,1); gkey_push(200,1); chk("scancodes outside 1..127 are ignored",gkey_pop(1,&d)==-1&&gkey_pop(5,&d)==-1&&gkey_pop(-1,&d)==-1); }
  printf(fails?"FAILURES (%d checks, %d failed)\n":"ALL PASS (%d checks, %d failed)\n",checks,fails); return fails?1:0; }
