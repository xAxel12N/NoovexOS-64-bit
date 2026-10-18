#include "include/nvx/base.h"
#include "include/nvx/net/http.h"
#include "include/nvx/drivers/input/ps2_mouse.h"
#include "include/nvx/drivers/input/elan_ps2.h"
/* ---- PS/2 mouse init (bounded waits so a missing mouse never hangs) ---- */
static int pw_write(void){ int t=200000; while((inb(0x64)&2)&&--t)io_wait(); return t; }
int pw_read (void){ int t=200000; while(!(inb(0x64)&1)&&--t)io_wait(); return t; }
int mcmd(u8 c){ if(!pw_write())return 0; outb(0x64,0xD4); if(!pw_write())return 0; outb(0x60,c);
    if(!pw_read())return 0; inb(0x60); return 1; }
static int g_mouse_ok=0;

static void mouse_rate(u8 r){ mcmd(0xF3); mcmd(r); }
void mouse_init(void){
    if(!pw_write())return; outb(0x64,0xA8); io_wait();           /* enable aux */
    if(!pw_write())return; outb(0x64,0x20);                       /* read cfg   */
    if(!pw_read())return; u8 st=inb(0x60);
    st|=2; st&=~0x20;
    if(!pw_write())return; outb(0x64,0x60);
    if(!pw_write())return; outb(0x60,st);
    if(etp_detect()){ g_mbytes=6; if(!mcmd(0xF4))return; g_mouse_ok=1; return; }
    { extern int syn_detect(void); if(syn_detect()){ g_mbytes=6; if(!mcmd(0xF4))return; g_mouse_ok=1; return; } }
    if(!mcmd(0xF6))return;                                        /* defaults   */
    mouse_rate(200); mouse_rate(100); mouse_rate(80);            /* magic knock */
    if(mcmd(0xF2) && pw_read()){ u8 id=inb(0x60); if(id==3) g_mbytes=4; }
    if(!mcmd(0xF4))return;                                        /* enable rpt */
    g_mouse_ok=1;
}
