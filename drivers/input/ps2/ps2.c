#include "include/nvx/base.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/drivers/input/ps2/ps2.h"
#include "include/nvx/drivers/input/ps2/elan_ps2.h"
/* ---- keyboard: scancode set 1 -> ASCII ---- */
int kshift=0, kcaps=0, kext=0, kalt=0;
static const char kmap_lo[128]={
0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
'd','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v',
'b','n','m',',','.','/',0,'*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
'7','8','9','-','4','5','6','+','1','2','3','0','.',0,0,0,0,0};
static const char kmap_up[128]={
0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S',
'D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V',
'B','N','M','<','>','?',0,'*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
'7','8','9','-','4','5','6','+','1','2','3','0','.',0,0,0,0,0};
extern volatile u32 g_kbd_last_tick;
volatile u8 g_hk_q[32]; volatile int g_hk_w=0, g_hk_r=0; volatile u32 g_klog[16]; volatile int g_klog_n=0;
void hk_push(int id){ int n=(g_hk_w+1)&31; if(n!=g_hk_r){ g_hk_q[g_hk_w]=(u8)id; g_hk_w=n; } }
void klog_key(u32 v){ g_klog[g_klog_n&15]=v; g_klog_n++; }
static int hk_of(u8 sc){ switch(sc){ case 0x30: return 1; case 0x2E: return 2; case 0x20: return 3; case 0x22: return 4; case 0x19: return 5; case 0x10: return 6; case 0x24: return 7; case 0x21: return 8; case 0x32: return 9; case 0x6C: return 10; case 0x6B: return 11; case 0x5F: return 12; case 0x6D: return 21; case 0x65: return 22; case 0x66: return 23; case 0x67: return 24; case 0x68: return 25; case 0x69: return 26; case 0x6A: return 27; } return 0; }
static void kbd_feed(u8 sc){
    if(sc==0xE0){ kext=1; return; }
    if(!kext&&!(sc&0x80)&&sc!=0x1D&&sc!=0x2A&&sc!=0x36&&sc!=0x38&&sc!=0x3A)g_kbd_last_tick=(u32)g_ticks;
    if(!kext){ if(sc&0x80) kdown[sc&0x7F]=0; else kdown[sc]=1; }
    if(kext){ kext=0; if(sc&0x80) kdown[sc&0x7F]=0; else kdown[sc]=1; if(sc==0x38){kalt=1;return;} if(sc==0xB8){kalt=0;return;} if(sc&0x80) return;
        if(sc!=0x2A&&sc!=0x5B&&sc!=0x5C&&sc!=0x1D)klog_key(0x10000u|sc);
        { int h=hk_of(sc); if(h){ g_kbd_last_tick=(u32)g_ticks; hk_push(h); return; } }
        if(sc==0x4B) kpush(17); else if(sc==0x4D) kpush(18);
        else if(sc==0x48) kpush(19); else if(sc==0x50) kpush(20);
        else if(sc==0x47) kpush(2); else if(sc==0x4F) kpush(5); else if(sc==0x49) kpush(6); else if(sc==0x51) kpush(7); else if(sc==0x53) kpush(14); else if(sc==0x52) kpush(15);
        return; }
    if(sc==0x2A||sc==0x36){ kshift=1; return; }
    if(sc==0xAA||sc==0xB6){ kshift=0; return; }
    if(sc==0x38){ kalt=1; return; }
    if(sc==0xB8){ kalt=0; return; }
    if(sc==0x3A){ kcaps^=1; return; }
    if(sc&0x80) return;
    if(sc>=0x54&&sc!=0x57&&sc!=0x58)klog_key(0x20000u|sc);
    if(kdown[0x5B]) return;
    if(sc==0x58){ kpush(31); return; }
    if(sc==0x0E&&kalt&&kdown[0x1D]){ kpush(30); return; }
    char c = kshift ? kmap_up[sc] : kmap_lo[sc];
    if(kcaps){ if(c>='a'&&c<='z')c-=32; else if(c>='A'&&c<='Z'&&!kshift)c+=32; }
    if(c) kpush(c);
}
/* ---- pointer input: portable-touchpad PS/2 core (ported), fed by IRQ12 ----
   Parses the i8042 Intellimouse stream (3- or 4-byte). NoovexOS drives it from
   the IRQ handler rather than polling, so the parser only consumes bytes. The
   PTP multitouch layer needs I2C-HID (which this machine's pad doesn't expose to
   us), so on PS/2 this is the driver's documented relative-motion path. */
static int mpk=0; u8 mb[4];
 
static void mouse_feed(u8 v){
    if(g_etp_hw>=3){ etp_irq_byte(v); return; }
    { extern int g_syn_ready; extern void syn_irq_byte(u8); if(g_syn_ready){ syn_irq_byte(v); return; } }
    if(mpk==0 && !(v&0x08)) return;              /* resync on the sync bit    */
    mb[mpk++]=v;
    int len = (g_mbytes==4)?4:3;
    if(mpk<len) return;
    mpk=0;
    if(mb[0]&0xC0) return;                        /* X/Y overflow: drop packet */
    int dx=mb[1], dy=mb[2];
    if(mb[0]&0x10) dx-=256;                       /* sign-extend from the flags*/
    if(mb[0]&0x20) dy-=256;
    mouse_dx+=dx; mouse_dy+=dy;                   /* loops apply my -= dy, so pass raw dy */
    mouse_btn=mb[0]&1; mouse_rbtn=(mb[0]>>1)&1; mouse_mid=(mb[0]>>2)&1; mouse_evt=1;
    if(g_mbytes==4){ int z=(int8_t)mb[3]; if(z){ mouse_scroll+=z; } }   /* wheel */
}
void ps2_byte(void){
    u8 st=inb(0x64); if(!(st&1)) return;
    u8 v=inb(0x60);
    if(st&0x20) mouse_feed(v); else kbd_feed(v);
}
