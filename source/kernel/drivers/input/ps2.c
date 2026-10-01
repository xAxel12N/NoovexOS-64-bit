/* ---- keyboard: scancode set 1 -> ASCII ---- */
static int kshift=0, kcaps=0, kext=0, kalt=0;
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
static void kbd_feed(u8 sc){
    if(sc==0xE0){ kext=1; return; }
    if(!kext){ if(sc&0x80) kdown[sc&0x7F]=0; else kdown[sc]=1; }
    if(kext){ kext=0; if(sc&0x80) kdown[sc&0x7F]=0; else kdown[sc]=1; if(sc==0x38){kalt=1;return;} if(sc==0xB8){kalt=0;return;} if(sc&0x80) return;
        if(sc==0x4B) kpush(17); else if(sc==0x4D) kpush(18);
        else if(sc==0x48) kpush(19); else if(sc==0x50) kpush(20);
        return; }
    if(sc==0x2A||sc==0x36){ kshift=1; return; }
    if(sc==0xAA||sc==0xB6){ kshift=0; return; }
    if(sc==0x38){ kalt=1; return; }
    if(sc==0xB8){ kalt=0; return; }
    if(sc==0x3A){ kcaps^=1; return; }
    if(sc&0x80) return;
    char c = kshift ? kmap_up[sc] : kmap_lo[sc];
    if(kcaps){ if(c>='a'&&c<='z')c-=32; else if(c>='A'&&c<='Z'&&!kshift)c+=32; }
    if(c) kpush(c);
}
/* ---- pointer input: portable-touchpad PS/2 core (ported), fed by IRQ12 ----
   Parses the i8042 Intellimouse stream (3- or 4-byte). NoovexOS drives it from
   the IRQ handler rather than polling, so the parser only consumes bytes. The
   PTP multitouch layer needs I2C-HID (which this machine's pad doesn't expose to
   us), so on PS/2 this is the driver's documented relative-motion path. */
static int mpk=0; static u8 mb[4];
static void mouse_feed(u8 v){
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
static void ps2_byte(void){
    u8 st=inb(0x64); if(!(st&1)) return;
    u8 v=inb(0x60);
    if(st&0x20) mouse_feed(v); else kbd_feed(v);
}
