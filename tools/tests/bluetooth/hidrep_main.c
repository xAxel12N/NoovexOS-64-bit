#include <stdio.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef int64_t i64;
#define NVX_BASE_H
#define NVX_DRIVERS_INPUT_INPUT_H
#define NVX_NET_BLUETOOTH_BT_HOST_H
typedef struct btconn btconn_t;
volatile unsigned char kdown[128]; volatile u64 g_ticks; volatile u32 g_kbd_last_tick; int kcaps, g_krep_delay=50, g_krep_rate=3, g_krep_flag;
static char kq[64]; static int kqn; void kpush(char c){ if(kqn<63)kq[kqn++]=c; }
static int hk; void hk_push(int id){ hk=id; }
static int dx,dy,bl,br,wz; void in_rel(int a,int b){ dx+=a; dy+=b; } void in_btn_raw(int l,int r,int m){ (void)m; bl=l; br=r; } void in_xbtn(int a,int b){ (void)a; (void)b; } void in_scroll(int v,int h){ (void)h; wz+=v; }
int usb_hid_key(u8 u,int s){ return u>=4&&u<=0x1D?(s?'A':'a')+u-4:0; } u8 usb_hid_sc(u8 u){ return u==4?0x1E:0; }
#include "../../../net/bluetooth/profiles/hid_report.c"
static int fails; static void chk(const char* n,int ok){ printf("%s %s\n",ok?"PASS":"FAIL",n); if(!ok)fails++; }
int main(void){
  static const u8 combo[]={0x05,0x01,0x09,0x06,0xA1,0x01,0x85,0x01,0x05,0x07,0x19,0xE0,0x29,0xE7,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,0x95,0x01,0x75,0x08,0x81,0x01,0x95,0x06,0x75,0x08,0x15,0x00,0x25,0x65,0x19,0x00,0x29,0x65,0x81,0x00,0xC0,
    0x05,0x01,0x09,0x02,0xA1,0x01,0x85,0x02,0x09,0x01,0xA1,0x00,0x05,0x09,0x19,0x01,0x29,0x05,0x95,0x05,0x75,0x01,0x81,0x02,0x95,0x01,0x75,0x03,0x81,0x01,0x05,0x01,0x09,0x30,0x09,0x31,0x16,0x01,0xF8,0x26,0xFF,0x07,0x75,0x0C,0x95,0x02,0x81,0x06,0x09,0x38,0x15,0x81,0x25,0x7F,0x75,0x08,0x95,0x01,0x81,0x06,0xC0,0xC0,
    0x05,0x0C,0x09,0x01,0xA1,0x01,0x85,0x03,0x15,0x00,0x26,0xFF,0x03,0x19,0x00,0x2A,0xFF,0x03,0x75,0x10,0x95,0x01,0x81,0x00,0xC0};
  hidr_t r; memset(&r,0,sizeof r);
  int k=hidr_parse(&r,combo,sizeof combo); chk("combo map yields 3 layouts",k==3&&r.hasid);
  u8 a[8]={0x02,0,0x04,0,0,0,0,0}; hidr_input(&r,1,a,8); chk("shift+a -> 'A', key down",kqn==1&&kq[0]=='A'&&kdown[0x1E]&&kdown[0x2A]);
  u8 z[8]={0}; hidr_input(&r,1,z,8); chk("release clears kdown",!kdown[0x1E]&&!kdown[0x2A]);
  u8 m[5]={0x01,0xFB,0xFF,0x00,0x01}; hidr_input(&r,2,m,5); chk("12-bit mouse dx=-5 dy=15 left btn wheel",dx==-5&&dy==15&&bl==1&&wz==1);
  u8 cc[2]={0xE9,0}; hidr_input(&r,3,cc,2); chk("consumer volume up hotkey",hk==1);
  hidr_t b; memset(&b,0,sizeof b); hidr_boot(&b,1,1); dx=dy=0;
  u8 bm[4]={0x02,3,(u8)-4,0}; hidr_input(&b,2,bm,4); chk("boot mouse right btn dx=3 dy=-4",br==1&&dx==3&&dy==-4);
  u8 err[8]={0,0,1,1,1,1,1,1}; kqn=0; hidr_input(&b,1,err,8); chk("rollover error ignored",kqn==0);
  hidr_release(&b); chk("release drops buttons",br==0&&bl==0);
  u8 junk[300]; for(int i=0;i<300;i++)junk[i]=(u8)(i*37+11); hidr_parse(&r,junk,300); for(int i=0;i<r.nl;i++)hidr_input(&r,r.l[i].id,junk,9); chk("junk map does not crash",1);
  printf(fails?"HIDREP: FAIL\n":"HIDREP: ALL PASS\n"); return fails!=0;
}
