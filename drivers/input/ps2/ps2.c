#include "include/nvx/base.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/drivers/input/ps2/ps2.h"
#include "include/nvx/drivers/input/ps2/elan_ps2.h"
#include "include/nvx/drivers/input/input.h"
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
int g_kbd_layout=0; int kaltgr=0;
typedef struct { unsigned char sc, lo, up, ag; } kov;
static const kov KL_SV[]={{0x29,0xA7,0xBD,0},{0x03,'2','"','@'},{0x04,'3','#',0xA3},{0x05,'4',0xA4,'$'},{0x06,'5','%',0},{0x07,'6','&',0},{0x08,'7','/','{'},{0x09,'8','(','['},{0x0A,'9',')',']'},{0x0B,'0','=','}'},{0x0C,'+','?','\\'},{0x0D,0xB4,'`',0},{0x1A,0xE5,0xC5,0},{0x1B,0xA8,'^','~'},{0x27,0xF6,0xD6,0},{0x28,0xE4,0xC4,0},{0x2B,'\'','*',0},{0x33,',',';',0},{0x34,'.',':',0},{0x35,'-','_',0},{0x56,'<','>','|'},{0,0,0,0}};
static const kov KL_DE[]={{0x15,'z','Z',0},{0x2C,'y','Y',0},{0x29,'^',0xB0,0},{0x03,'2','"',0xB2},{0x04,'3',0xA7,0xB3},{0x07,'6','&',0},{0x08,'7','/','{'},{0x09,'8','(','['},{0x0A,'9',')',']'},{0x0B,'0','=','}'},{0x0C,0xDF,'?','\\'},{0x0D,0xB4,'`',0},{0x10,'q','Q','@'},{0x1A,0xFC,0xDC,0},{0x1B,'+','*','~'},{0x27,0xF6,0xD6,0},{0x28,0xE4,0xC4,0},{0x2B,'#','\'',0},{0x33,',',';',0},{0x34,'.',':',0},{0x35,'-','_',0},{0x56,'<','>','|'},{0,0,0,0}};
static const kov KL_FR[]={{0x10,'a','A',0},{0x11,'z','Z',0},{0x1E,'q','Q',0},{0x2C,'w','W',0},{0x27,'m','M',0},{0x32,',','?',0},{0x33,';','.',0},{0x34,':','/',0},{0x35,'!',0xA7,0},{0x02,'&','1',0},{0x03,0xE9,'2','~'},{0x04,'"','3','#'},{0x05,'\'','4','{'},{0x06,'(','5','['},{0x07,'-','6','|'},{0x08,0xE8,'7','`'},{0x09,'_','8','\\'},{0x0A,0xE7,'9','^'},{0x0B,0xE0,'0','@'},{0x0C,')',0xB0,']'},{0x0D,'=','+','}'},{0x1A,'^',0xA8,0},{0x1B,'$',0xA3,0xA4},{0x28,0xF9,'%',0},{0x2B,'*',0xB5,0},{0x29,0xB2,0,0},{0x56,'<','>',0},{0,0,0,0}};
static const kov KL_ES[]={{0x29,0xBA,0xAA,'\\'},{0x02,'1','!','|'},{0x03,'2','"','@'},{0x04,'3',0xB7,'#'},{0x05,'4','$','~'},{0x06,'5','%',0},{0x07,'6','&',0xAC},{0x08,'7','/',0},{0x09,'8','(',0},{0x0A,'9',')',0},{0x0B,'0','=',0},{0x0C,'\'','?',0},{0x0D,0xA1,0xBF,0},{0x1A,'`','^','['},{0x1B,'+','*',']'},{0x27,0xF1,0xD1,0},{0x28,0xB4,0xA8,'{'},{0x2B,0xE7,0xC7,'}'},{0x33,',',';',0},{0x34,'.',':',0},{0x35,'-','_',0},{0x56,'<','>',0},{0,0,0,0}};
static const kov KL_UK[]={{0x03,'2','"',0},{0x04,'3',0xA3,0},{0x28,'\'','@',0},{0x2B,'#','~',0},{0x29,'`',0xAC,'|'},{0x56,'\\','|',0},{0,0,0,0}};
static const kov* const KLAY[6]={0,KL_SV,KL_DE,KL_FR,KL_ES,KL_UK};
static int kbd_char(u8 sc, int sh, int ag){
    const kov* t=(g_kbd_layout>0&&g_kbd_layout<6)?KLAY[g_kbd_layout]:0;
    if(t) for(;t->sc;t++) if(t->sc==sc){ if(ag) return t->ag; return sh?t->up:t->lo; }
    if(ag) return 0;
    return (unsigned char)(sh?kmap_up[sc]:kmap_lo[sc]);
}
static int is_letter(int c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>=0xC0&&c<=0xFE&&c!=0xD7&&c!=0xF7&&c!=0xDF); }
extern volatile u32 g_kbd_last_tick;
volatile u8 g_hk_q[32]; volatile int g_hk_w=0, g_hk_r=0; volatile u32 g_klog[16]; volatile int g_klog_n=0;
void hk_push(int id){ int n=(g_hk_w+1)&31; if(n!=g_hk_r){ g_hk_q[g_hk_w]=(u8)id; g_hk_w=n; } }
void klog_key(u32 v){ g_klog[g_klog_n&15]=v; g_klog_n++; }
static int hk_of(u8 sc){ switch(sc){ case 0x30: return 1; case 0x2E: return 2; case 0x20: return 3; case 0x22: return 4; case 0x19: return 5; case 0x10: return 6; case 0x24: return 7; case 0x21: return 8; case 0x32: return 9; case 0x6C: return 10; case 0x6B: return 11; case 0x5F: return 12; case 0x6D: return 21; case 0x65: return 22; case 0x66: return 23; case 0x67: return 24; case 0x68: return 25; case 0x69: return 26; case 0x6A: return 27; case 0x37: return kshift?30:kalt?29:28; } return 0; }
int g_krep_delay=50, g_krep_rate=3, g_krep_flag=0;
static int krep_sc=0, krep_ext=0; static u64 krep_t0=0, krep_last=0;
static int krep_ok(int sc,int ext){
    if(ext) return !(sc==0x1D||sc==0x38||sc==0x2A||sc==0x36||sc==0x5B||sc==0x5C||sc==0x5D||sc==0x37||sc==0x46||hk_of(sc));
    return !(sc==0x01||sc==0x1D||sc==0x2A||sc==0x36||sc==0x38||sc==0x3A||sc==0x45||sc==0x46||sc==0x54);
}
static void kbd_feed2(u8 sc);
static void kbd_feed(u8 sc){
    if(sc==0xE0){ kext=1; return; }
    int ext=kext, k=sc&0x7F;
    if(!(sc&0x80)&&kdown[k]&&!g_krep_flag&&(krep_ok(k,ext)||(!ext&&(k==0x3A||k==0x45||k==0x46)))){ if(ext) kext=0; return; }
    if(sc&0x80){ if(krep_sc==k&&krep_ext==ext) krep_sc=0; }
    else if(!g_krep_flag){ if(krep_ok(k,ext)){ krep_sc=k; krep_ext=ext; krep_t0=g_ticks; krep_last=g_ticks; } else if(!(ext==0&&(k==0x1D||k==0x2A||k==0x36||k==0x38))&&!(ext&&(k==0x1D||k==0x38))) krep_sc=0; }
    kbd_feed2(sc);
}
void ps2_kbd_tick(void){
    if(!krep_sc) return;
    if(!kdown[krep_sc]){ krep_sc=0; return; }
    if(g_ticks-krep_t0<(u64)g_krep_delay||g_ticks-krep_last<(u64)g_krep_rate) return;
    krep_last=g_ticks; g_krep_flag=1; if(krep_ext) kext=1; kbd_feed2((u8)krep_sc); g_krep_flag=0;
}
static void kbd_feed2(u8 sc){
    if(!kext&&!(sc&0x80)&&sc!=0x1D&&sc!=0x2A&&sc!=0x36&&sc!=0x38&&sc!=0x3A)g_kbd_last_tick=(u32)g_ticks;
    if(!kext){ if(sc&0x80) kdown[sc&0x7F]=0; else kdown[sc]=1; }
    if(kext){ kext=0; if(sc&0x80) kdown[sc&0x7F]=0; else kdown[sc]=1; if(sc==0x38){ if(g_kbd_layout>0&&g_kbd_layout!=5)kaltgr=1; else kalt=1; return;} if(sc==0xB8){kalt=0;kaltgr=0;return;} if(sc&0x80) return;
        if(sc==0x35){ kpush('/'); return; } if(sc==0x1C){ kpush('\n'); return; }
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
    if(sc==0x54){ hk_push(kshift?30:29); return; }
    if(kdown[0x5B]) return;
    if(sc==0x58){ kpush(31); return; }
    if(sc==0x0E&&kalt&&kdown[0x1D]){ kpush(30); return; }
    int ag = kaltgr || (g_kbd_layout>0 && kalt && kdown[0x1D]);
    int c = kbd_char(sc, kshift, ag);
    if(ag && !c) c = kbd_char(sc, kshift, 0);
    if(kcaps && is_letter(c)){ int lo=(c>='a'&&c<='z')||(c>=0xE0&&c<=0xFE); c = lo ? c-32 : c+32; }
    if(c) kpush((char)c);
}
/* ---- pointer input: portable-touchpad PS/2 core (ported), fed by IRQ12 ----
   Parses the i8042 Intellimouse stream (3- or 4-byte). NoovexOS drives it from
   the IRQ handler rather than polling, so the parser only consumes bytes. The
   PTP multitouch layer needs I2C-HID (which this machine's pad doesn't expose to
   us), so on PS/2 this is the driver's documented relative-motion path. */
static int mpk=0; u8 mb[4];
 
int g_mimex;
static void mouse_feed(u8 v){
    if(g_etp_hw>=3){ etp_irq_byte(v); return; }
    { extern int g_syn_ready; extern void syn_irq_byte(u8); if(g_syn_ready){ syn_irq_byte(v); return; } }
    if(mpk==0 && !(v&0x08)) return;              /* resync on the sync bit    */
    mb[mpk++]=v;
    int len = (g_mbytes==4)?4:3;
    if(mpk<len) return;
    mpk=0;
    if(mb[0]&0xC0) return;                        /* X/Y overflow: drop packet */
    { extern int g_vmm_mode; if(g_vmm_mode==1){ mouse_evt=1; return; } }
    int dx=mb[1], dy=mb[2];
    if(mb[0]&0x10) dx-=256;                       /* sign-extend from the flags*/
    if(mb[0]&0x20) dy-=256;
    mouse_dx+=dx; mouse_dy+=dy;                   /* loops apply my -= dy, so pass raw dy */
    in_btn_raw(mb[0]&1,(mb[0]>>1)&1,(mb[0]>>2)&1); mouse_evt=1;
    if(g_mbytes==4){
        extern int g_mimex; u8 b3=mb[3];
        if(!g_mimex){ int z=(int8_t)b3; if(z){ mouse_scroll-=z; } }
        else { int top=b3&0xC0;
            if(top==0x40){ int h=(int)(b3&32)-(int)(b3&31); if(h){ mouse_hscroll+=h; mouse_evt=1; } }
            else if(top==0x80){ int v=(int)(b3&32)-(int)(b3&31); if(v) mouse_scroll+=v; }
            else { int z=b3&0x0F; if(z&8)z-=16; if(z) mouse_scroll-=z; in_xbtn((b3>>4)&1,(b3>>5)&1); } } }
}
void ps2_byte(void){
    u8 st=inb(0x64); if(!(st&1)) return;
    u8 v=inb(0x60);
    if(st&0x20) mouse_feed(v); else kbd_feed(v);
}
