/* ---- xHCI USB controller: stage 1 (init + port detection), ported from Noovex9 32-bit ---- */
#define XHCI_DCBAA   0x00990000u
#define XHCI_CMDRING 0x00991000u
#define XHCI_EVTRING 0x00992000u
#define XHCI_ERST    0x00993000u
#define XHCI_SCRATCH 0x00994000u
#define XMR(a)   (*(volatile u32*)(u64)(a))
#define XMW(a,v) (*(volatile u32*)(u64)(a)=(u32)(v))
static int xhci_present=0, xhci_init_ok=0, xhci_slots=0, xhci_ports=0; static u32 xhci_base=0, xhci_op=0;
static u8 xhci_port_conn[16], xhci_port_speed[16]; static u16 xhci_vendor=0;
static void xdelay(int n){ for(int i=0;i<n;i++) io_wait(); }
static const char* xhci_spd(int s){ switch(s){case 1:return "FULL";case 2:return "LOW";case 3:return "HIGH";case 4:return "SUPER";default:return "-";} }
static void xhci_init(void){
    xhci_present=0; xhci_init_ok=0; xhci_slots=0; xhci_ports=0; for(int i=0;i<16;i++){xhci_port_conn[i]=0;xhci_port_speed[i]=0;}
    if(!pci_n)pci_scan();
    int idx=-1; for(int i=0;i<pci_n;i++){ if(pci_cls[i]==0x0C && pci_sub[i]==0x03){ int prog=(pci_cfg(pci_bus[i],pci_slot[i],0,0x08)>>8)&0xFF; if(prog==0x30){ idx=i; break; } } }
    if(idx<0)return; xhci_present=1;
    int bus=pci_bus[idx], slot=pci_slot[idx];
    xhci_vendor=(u16)(pci_cfg(bus,slot,0,0)&0xFFFF);
    u32 bar=pci_cfg(bus,slot,0,0x10); if(bar&1)return; u32 base=bar&0xFFFFFFF0u; if(!base)return;
    u32 cmd=pci_cfg(bus,slot,0,0x04); pci_cfgw(bus,slot,0,0x04,cmd|0x06);
    if(xhci_vendor==0x8086){ u32 u3=pci_cfg(bus,slot,0,0xDC); pci_cfgw(bus,slot,0,0xD8,u3); u32 u2=pci_cfg(bus,slot,0,0xD4); pci_cfgw(bus,slot,0,0xD0,u2); }
    xhci_base=base;
    u8 caplen=*(volatile u8*)(u64)base;
    u32 hcs1=XMR(base+0x04); int maxslots=hcs1&0xFF; int maxports=(hcs1>>24)&0xFF; if(maxports>16)maxports=16; if(maxports<1)maxports=1; xhci_ports=maxports; xhci_slots=maxslots;
    u32 hcs2=XMR(base+0x08); int spb=(((hcs2>>21)&0x1F)<<5)|((hcs2>>27)&0x1F); if(spb>32)spb=32;
    u32 rtsoff=XMR(base+0x18)&~0x1Fu;
    u32 op=base+caplen;
    volatile u32* usbcmd=(volatile u32*)(u64)(op+0x00);
    volatile u32* usbsts=(volatile u32*)(u64)(op+0x04);
    for(int t=0;t<200000;t++){ if(!(*usbsts&(1u<<11)))break; xdelay(1); }
    *usbcmd &= ~1u; for(int t=0;t<200000;t++){ if(*usbsts&1u)break; xdelay(1); }
    *usbcmd |= (1u<<1); int rok=0;
    for(int t=0;t<400000;t++){ if(!(*usbcmd&(1u<<1)) && !(*usbsts&(1u<<11))){ rok=1; break; } xdelay(1); }
    XMW(op+0x38,(u32)maxslots);
    { volatile u32* dc=(volatile u32*)(u64)XHCI_DCBAA; for(int i=0;i<(maxslots+1)*2;i++)dc[i]=0; }
    if(spb>0){ volatile u32* sa=(volatile u32*)(u64)XHCI_SCRATCH; for(int i=0;i<spb*2;i++)sa[i]=0; for(int i=0;i<spb;i++){ u32 b=XHCI_SCRATCH+0x1000u+(u32)i*0x1000u; sa[i*2]=b; sa[i*2+1]=0; volatile u8* bb=(volatile u8*)(u64)b; for(int k=0;k<512;k++)bb[k]=0; } *(volatile u32*)(u64)XHCI_DCBAA=XHCI_SCRATCH; *(volatile u32*)(u64)(XHCI_DCBAA+4)=0; }
    XMW(op+0x30,XHCI_DCBAA); XMW(op+0x34,0);
    { volatile u32* cr=(volatile u32*)(u64)XHCI_CMDRING; for(int i=0;i<256*4;i++)cr[i]=0; int li=255*4; cr[li]=XHCI_CMDRING; cr[li+1]=0; cr[li+2]=0; cr[li+3]=(6u<<10)|(1u<<1)|1u; }
    XMW(op+0x18,XHCI_CMDRING|1u); XMW(op+0x1C,0);
    { volatile u32* er=(volatile u32*)(u64)XHCI_EVTRING; for(int i=0;i<256*4;i++)er[i]=0; }
    { volatile u32* erst=(volatile u32*)(u64)XHCI_ERST; erst[0]=XHCI_EVTRING; erst[1]=0; erst[2]=256; erst[3]=0; }
    { u32 rt=base+rtsoff+0x20; XMW(rt+0x08,1); XMW(rt+0x18,XHCI_EVTRING); XMW(rt+0x1C,0); XMW(rt+0x10,XHCI_ERST); XMW(rt+0x14,0); }
    *usbcmd |= 1u; for(int t=0;t<200000;t++){ if(!(*usbsts&1u))break; xdelay(1); }
    for(int p=0;p<maxports;p++){ u32 pa=op+0x400+(u32)p*0x10; u32 v=XMR(pa); if(!(v&(1u<<9))){ XMW(pa,(v&0x0E00C3E0u)|(1u<<9)); } }
    for(int t=0;t<40000;t++) xdelay(1);
    for(int p=0;p<maxports;p++){ u32 v=XMR(op+0x400+(u32)p*0x10); xhci_port_conn[p]=(u8)(v&1); xhci_port_speed[p]=(u8)((v>>10)&0xF); }
    xhci_op=op; xhci_init_ok=rok;
}
/* mouse state (defined later, near the PS/2 handler) */
static volatile int mouse_dx, mouse_dy, mouse_btn, mouse_rbtn, mouse_mid, mouse_scroll, mouse_evt;
static int g_cursor_moved=0;   /* pure pointer motion (cheap redraw, no full rebuild) */
/* keyboard input path (defined later): held-key array + char push */
static volatile unsigned char kdown[128]; static void kpush(char c);
static int ec_detect(void); static int ec_read(u8 off,u8* out);   /* EC battery (defined near topbar) */
static int bat_present, bat_pct, bat_charging, bat_via_ec; static void battery_tick(void);   /* battery (defined near topbar) */
/* ================= xHCI USB mouse: enumeration + HID + interrupt transfers =================
   Reasoned from the xHCI spec + Linux drivers/usb. Contexts/rings live at fixed identity-mapped
   low addresses (the controller DMAs to them; phys==virt here). Poll-based (no MSI). */
/* per-device HID state: contexts + rings at fixed identity-mapped addresses, 64KB per device */
#define XHID_MAX 4
#define XHID_INPUTCTX(d) (0x00A00000u + (u32)(d)*0x10000u)
#define XHID_DEVCTX(d)   (0x00A01000u + (u32)(d)*0x10000u)
#define XHID_EP0RING(d)  (0x00A02000u + (u32)(d)*0x10000u)
#define XHID_EPINRING(d) (0x00A03000u + (u32)(d)*0x10000u)
#define XHID_DATABUF(d)  (0x00A04000u + (u32)(d)*0x10000u)
static int xhci_csz=0; static u32 xhci_dboff=0, xhci_rtoff=0;
static u32 cmd_enq=0, cmd_cyc=1, ev_deq=0, ev_cyc=1;
/* "current device" scratch used by xh_ctrl during enumeration */
static int cur_slot=0; static u32 cur_ep0=0, cur_dbuf=0, e0_enq=0, e0_cyc=1;
/* persistent per-device arrays */
static int xhid_n=0, xhci_mouse_ok=0;
static int xhid_slot[XHID_MAX], xhid_dci[XHID_MAX], xhid_rlen[XHID_MAX], xhid_iskbd[XHID_MAX];
static u32 xhid_epin[XHID_MAX], xhid_ein_enq[XHID_MAX], xhid_ein_cyc[XHID_MAX], xhid_dbuf[XHID_MAX];
static char xhci_failstep[48]=""; static int xhci_failcc=0;
static void xh_db(int slot,u32 target){ *(volatile u32*)(u64)(xhci_base+xhci_dboff+(u32)slot*4)=target; }
static int xh_event_t(u32 want_type,u32* p0,u8* cc,u8* slot,u8* epid){
    for(int t=0;t<400000;t++){ volatile u32* e=(volatile u32*)(u64)(XHCI_EVTRING+ev_deq*16);
        if((e[3]&1)==ev_cyc){ u32 ty=(e[3]>>10)&0x3F; u8 c=(u8)((e[2]>>24)&0xFF), sl=(u8)((e[3]>>24)&0xFF), ep=(u8)((e[3]>>16)&0x1F); u32 pp=e[0];
            ev_deq++; if(ev_deq>=256){ev_deq=0;ev_cyc^=1;}
            u32 rt=xhci_base+xhci_rtoff+0x20; XMW(rt+0x18,(XHCI_EVTRING+ev_deq*16)|(1u<<3)); XMW(rt+0x1C,0);
            if(ty==want_type){ if(p0)*p0=pp; if(cc)*cc=c; if(slot)*slot=sl; if(epid)*epid=ep; return 1; }
        } else io_wait();
    }
    return 0;
}
static int xh_cmd_s(u32 p0,u32 p1,u32 p2,u32 type,u32 slotid,u8* ccode,u8* slotret){
    volatile u32* tr=(volatile u32*)(u64)(XHCI_CMDRING+cmd_enq*16);
    tr[0]=p0; tr[1]=p1; tr[2]=p2; tr[3]=(type<<10)|((slotid&0xFF)<<24)|cmd_cyc;
    cmd_enq++; if(cmd_enq>=255){ volatile u32* lk=(volatile u32*)(u64)(XHCI_CMDRING+255*16); lk[3]=(lk[3]&~1u)|cmd_cyc; cmd_enq=0; cmd_cyc^=1; }
    xh_db(0,0);
    u8 cc=0,sl=0; if(!xh_event_t(33,0,&cc,&sl,0))return 0; if(ccode)*ccode=cc; if(slotret)*slotret=sl;
    return cc==1;
}
/* control transfer on the CURRENT device's EP0 */
static int xh_ctrl(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in,u8* data){
    u32 R=cur_ep0, DB=cur_dbuf; volatile u32* r;
    r=(volatile u32*)(u64)(R+e0_enq*16);
    r[0]=(u32)rt|((u32)req<<8)|((u32)val<<16); r[1]=(u32)idx|((u32)len<<16);
    r[2]=8; r[3]=(2u<<10)|(1u<<6)|((len?(in?3u:2u):0u)<<16)|e0_cyc;
    e0_enq++; if(e0_enq>=255){volatile u32* lk=(volatile u32*)(u64)(R+255*16); lk[3]=(lk[3]&~1u)|e0_cyc; e0_enq=0; e0_cyc^=1;}
    if(len){ r=(volatile u32*)(u64)(R+e0_enq*16); r[0]=DB; r[1]=0; r[2]=len; r[3]=(3u<<10)|((in?1u:0u)<<16)|e0_cyc;
        e0_enq++; if(e0_enq>=255){volatile u32* lk=(volatile u32*)(u64)(R+255*16); lk[3]=(lk[3]&~1u)|e0_cyc; e0_enq=0; e0_cyc^=1;} }
    r=(volatile u32*)(u64)(R+e0_enq*16); r[0]=0;r[1]=0;r[2]=0; r[3]=(4u<<10)|(1u<<5)|(((len&&in)?0u:1u)<<16)|e0_cyc;
    e0_enq++; if(e0_enq>=255){volatile u32* lk=(volatile u32*)(u64)(R+255*16); lk[3]=(lk[3]&~1u)|e0_cyc; e0_enq=0; e0_cyc^=1;}
    xh_db(cur_slot,1);
    u8 cc=0; if(!xh_event_t(32,0,&cc,0,0))return 0;
    if(in&&data){ volatile u8* db=(volatile u8*)(u64)DB; for(int i=0;i<len;i++)data[i]=db[i]; }
    return cc==1||cc==13;
}
static u32 ctx_sz(void){ return xhci_csz?64u:32u; }
static void ctx_zero(u32 base,int n){ volatile u32* c=(volatile u32*)(u64)base; for(int i=0;i<n;i++)c[i]=0; }
/* enumerate one HID device on a port into the next free device index; detect kbd vs mouse */
static int xhci_hid_enum(int port){
    if(xhid_n>=XHID_MAX) return 0;
    int d=xhid_n; xhci_failstep[0]=0; xhci_failcc=0;
    #define XFAIL(msg) do{ int _i=0; while(msg[_i]&&_i<47){xhci_failstep[_i]=msg[_i];_i++;} xhci_failstep[_i]=0; return 0; }while(0)
    xhci_csz=(XMR(xhci_base+0x10)>>2)&1;
    xhci_dboff=XMR(xhci_base+0x14)&~3u; xhci_rtoff=XMR(xhci_base+0x18)&~0x1Fu;
    u32 op=xhci_op; u32 pa=op+0x400+(u32)port*0x10; u32 pv=XMR(pa);
    if(!(pv&1)) XFAIL("no device on port");
    XMW(pa,(pv&0x0E00C3E0u)|(1u<<4));                                  /* port reset */
    for(int t=0;t<200000;t++){ if(XMR(pa)&(1u<<21))break; io_wait(); }
    for(volatile int dl=0;dl<50000;dl++) io_wait();
    pv=XMR(pa); int speed=(pv>>10)&0xF; int mps0=(speed==3)?64:(speed>=4)?512:8;
    cur_ep0=XHID_EP0RING(d); cur_dbuf=XHID_DATABUF(d); e0_enq=0; e0_cyc=1;
    { volatile u32* r0=(volatile u32*)(u64)XHID_EP0RING(d); for(int i=0;i<256*4;i++)r0[i]=0;   /* EP0 ring + Link TRB at 255 */
      int li=255*4; r0[li]=XHID_EP0RING(d); r0[li+1]=0; r0[li+2]=0; r0[li+3]=(6u<<10)|(1u<<1)|1u; }
    u8 cc=0,sl=0; if(!xh_cmd_s(0,0,0,9,0,&cc,&sl)){xhci_failcc=cc; XFAIL("Enable Slot");}
    cur_slot=sl; if(!cur_slot) XFAIL("no slot id");
    ctx_zero(XHID_DEVCTX(d),(int)ctx_sz()*8/4);
    *(volatile u32*)(u64)(XHCI_DCBAA+cur_slot*8)=XHID_DEVCTX(d); *(volatile u32*)(u64)(XHCI_DCBAA+cur_slot*8+4)=0;
    ctx_zero(XHID_INPUTCTX(d),(int)ctx_sz()*3/4);
    volatile u32* ic=(volatile u32*)(u64)XHID_INPUTCTX(d); ic[1]=(1u<<0)|(1u<<1);
    volatile u32* sc=(volatile u32*)(u64)(XHID_INPUTCTX(d)+ctx_sz()); sc[0]=(1u<<27)|((u32)speed<<20); sc[1]=((u32)(port+1)<<16);
    volatile u32* e0=(volatile u32*)(u64)(XHID_INPUTCTX(d)+ctx_sz()*2); e0[1]=(4u<<3)|(3u<<1)|((u32)mps0<<16); e0[2]=XHID_EP0RING(d)|1u; e0[3]=0;
    if(!xh_cmd_s(XHID_INPUTCTX(d),0,0,11,(u32)cur_slot,&cc,&sl)){xhci_failcc=cc; XFAIL("Address Device");}
    for(volatile int dl=0;dl<20000;dl++) io_wait();
    if(!xh_ctrl(0x80,6,0x0100,0,18,1,0)) XFAIL("GET device descriptor");
    if(!xh_ctrl(0x80,6,0x0200,0,9,1,0)) XFAIL("GET config(9)");
    u8 cfg9[9]; { volatile u8* db=(volatile u8*)(u64)cur_dbuf; for(int i=0;i<9;i++)cfg9[i]=db[i]; }
    int wtot=cfg9[2]|(cfg9[3]<<8); if(wtot>256)wtot=256; u8 cfgval=cfg9[5];
    if(!xh_ctrl(0x80,6,0x0200,0,(u16)wtot,1,0)) XFAIL("GET config(full)");
    u8 full[256]; { volatile u8* db=(volatile u8*)(u64)cur_dbuf; for(int i=0;i<wtot;i++)full[i]=db[i]; }
    int ep_addr=0, ep_mps=8, ep_intv=8, iface=0, isHID=0, proto=0;
    for(int i=0;i+2<wtot;){ int blen=full[i], btype=full[i+1]; if(blen<2)break;
        if(btype==4){ iface=full[i+2]; if(full[i+5]==3){isHID=1; proto=full[i+7];} else isHID=0; }   /* class 3 HID, bInterfaceProtocol */
        else if(btype==5 && isHID){ if(full[i+2]&0x80){ ep_addr=full[i+2]; ep_mps=full[i+4]|(full[i+5]<<8); ep_intv=full[i+6]; break; } }
        i+=blen; }
    if(!ep_addr) XFAIL("no HID interrupt endpoint");
    if(!xh_ctrl(0x00,9,cfgval,0,0,0,0)) XFAIL("SET_CONFIGURATION");
    xh_ctrl(0x21,0x0B,0,(u16)iface,0,0,0);                             /* SET_PROTOCOL boot */
    int epnum=ep_addr&0x0F; int dci=epnum*2+1;
    ctx_zero(XHID_INPUTCTX(d),(int)ctx_sz()*(dci+2)/4);
    ic=(volatile u32*)(u64)XHID_INPUTCTX(d); ic[1]=(1u<<0)|(1u<<dci);
    sc=(volatile u32*)(u64)(XHID_INPUTCTX(d)+ctx_sz()); sc[0]=((u32)dci<<27)|((u32)speed<<20); sc[1]=((u32)(port+1)<<16);
    volatile u32* ep=(volatile u32*)(u64)(XHID_INPUTCTX(d)+ctx_sz()*(dci+1));
    ep[0]=((u32)ep_intv<<16); ep[1]=(7u<<3)|(3u<<1)|((u32)ep_mps<<16); ep[2]=XHID_EPINRING(d)|1u; ep[3]=0;
    if(!xh_cmd_s(XHID_INPUTCTX(d),0,0,12,(u32)cur_slot,&cc,&sl)){xhci_failcc=cc; XFAIL("Configure Endpoint");}
    int rlen=(ep_mps<8)?ep_mps:8; if(proto==1)rlen=8; if(rlen<3)rlen=3;
    /* store persistent state */
    xhid_slot[d]=cur_slot; xhid_dci[d]=dci; xhid_rlen[d]=rlen; xhid_iskbd[d]=(proto==1);
    xhid_epin[d]=XHID_EPINRING(d); xhid_dbuf[d]=XHID_DATABUF(d); xhid_ein_enq[d]=0; xhid_ein_cyc[d]=1;
    /* arm first interrupt transfer */
    { volatile u32* er=(volatile u32*)(u64)xhid_epin[d]; for(int i=0;i<256*4;i++)er[i]=0;   /* interrupt ring + Link TRB at 255 (fixes freeze on wrap) */
      int li=255*4; er[li]=xhid_epin[d]; er[li+1]=0; er[li+2]=0; er[li+3]=(6u<<10)|(1u<<1)|1u; }
    { volatile u32* tr=(volatile u32*)(u64)(xhid_epin[d]+xhid_ein_enq[d]*16);
      tr[0]=xhid_dbuf[d]; tr[1]=0; tr[2]=(u32)rlen; tr[3]=(1u<<10)|(1u<<5)|xhid_ein_cyc[d];
      xhid_ein_enq[d]++; if(xhid_ein_enq[d]>=255){volatile u32* lk=(volatile u32*)(u64)(xhid_epin[d]+255*16); lk[3]=(lk[3]&~1u)|xhid_ein_cyc[d]; xhid_ein_enq[d]=0; xhid_ein_cyc[d]^=1;}
      xh_db(cur_slot,(u32)dci); }
    xhid_n++; xhci_mouse_ok=1;
    #undef XFAIL
    return 1;
}
/* USB HID keyboard usage -> ASCII (unshifted / shifted) */
static char hid2ascii(u8 u,int shift){
    if(u>=0x04&&u<=0x1D){ char c=(char)('a'+(u-0x04)); return shift?(char)(c-32):c; }
    if(u>=0x1E&&u<=0x26){ const char* n="!@#$%^&*("; return shift?n[u-0x1E]:(char)('1'+(u-0x1E)); }
    if(u==0x27) return shift?')':'0';
    switch(u){ case 0x2C:return ' '; case 0x28:return '\n'; case 0x2A:return '\b'; case 0x2B:return '\t';
        case 0x2D:return shift?'_':'-'; case 0x2E:return shift?'+':'='; case 0x2F:return shift?'{':'[';
        case 0x30:return shift?'}':']'; case 0x31:return shift?'|':'\\'; case 0x33:return shift?':':';';
        case 0x34:return shift?'"':'\''; case 0x36:return shift?'<':','; case 0x37:return shift?'>':'.';
        case 0x38:return shift?'?':'/'; case 0x35:return shift?'~':'`'; }
    return 0;
}
/* USB HID usage -> PS/2 set-1 scancode (for kdown[]: Ctrl/Shift/Alt/arrows/letters used by shortcuts) */
static u8 hid2scan(u8 u){
    static const u8 letter[26]={0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C};
    if(u>=0x04&&u<=0x1D) return letter[u-0x04];
    switch(u){ case 0x28:return 0x1C; case 0x29:return 0x01; case 0x2A:return 0x0E; case 0x2B:return 0x0F; case 0x2C:return 0x39;
        case 0x4F:return 0x4D; case 0x50:return 0x4B; case 0x51:return 0x50; case 0x52:return 0x48;   /* arrows R/L/D/U */
        case 0x1E:return 0x02; case 0x1F:return 0x03; case 0x20:return 0x04; case 0x21:return 0x05; case 0x22:return 0x06;
        case 0x23:return 0x07; case 0x24:return 0x08; case 0x25:return 0x09; case 0x26:return 0x0A; case 0x27:return 0x0B; }
    return 0;
}
static u8 kbd_prev[8]={0};
static void kbd_report(volatile u8* d){
    u8 mod=d[0]; int shift=(mod&0x22)!=0;                              /* L/R shift */
    /* modifiers -> kdown (Ctrl 0x1D, Shift 0x2A, Alt 0x38) */
    kdown[0x1D]=(mod&0x11)?1:0; kdown[0x2A]=(mod&0x22)?1:0; kdown[0x38]=(mod&0x44)?1:0;
    /* newly pressed keys: in current report but not previous */
    for(int i=2;i<8;i++){ u8 k=d[i]; if(!k)continue; int wasdown=0; for(int j=2;j<8;j++) if(kbd_prev[j]==k){wasdown=1;break;}
        if(!wasdown){ char c=hid2ascii(k,shift); if(c)kpush(c); u8 sc=hid2scan(k); if(sc)kdown[sc]=1; } }
    /* released keys: in previous but not current */
    for(int i=2;i<8;i++){ u8 k=kbd_prev[i]; if(!k)continue; int still=0; for(int j=2;j<8;j++) if(d[j]==k){still=1;break;}
        if(!still){ u8 sc=hid2scan(k); if(sc)kdown[sc]=0; } }
    for(int i=0;i<8;i++) kbd_prev[i]=d[i];
}
/* poll: drain the shared event ring, route each transfer event to its device by slot */
static void xhci_mouse_poll(void){
    if(!xhid_n)return;
    for(int guard=0; guard<128; guard++){
        volatile u32* e=(volatile u32*)(u64)(XHCI_EVTRING+ev_deq*16);
        if((e[3]&1)!=ev_cyc)return;
        u32 type=(e[3]>>10)&0x3F; u8 sl=(u8)((e[3]>>24)&0xFF); u8 cc=(u8)((e[2]>>24)&0xFF);
        ev_deq++; if(ev_deq>=256){ev_deq=0;ev_cyc^=1;}
        u32 rt=xhci_base+xhci_rtoff+0x20; XMW(rt+0x18,(XHCI_EVTRING+ev_deq*16)|(1u<<3)); XMW(rt+0x1C,0);
        if(type!=32)continue;
        int d=-1; for(int i=0;i<xhid_n;i++) if(xhid_slot[i]==sl){d=i;break;} if(d<0)continue;
        if(cc==1||cc==13){                                        /* only success/short-packet carry a valid report */
        volatile u8* db=(volatile u8*)(u64)xhid_dbuf[d];
        if(xhid_iskbd[d]) kbd_report(db);
        else { u8 btn=db[0]; int dx=(int)(int8_t)db[1]; int dy=(int)(int8_t)db[2];
            if(dx||dy){ mouse_dx+=dx; mouse_dy-=dy; mouse_evt=1; }
            mouse_btn=btn&1; mouse_rbtn=(btn>>1)&1; mouse_mid=(btn>>2)&1; if(btn)mouse_evt=1;
            if(xhid_rlen[d]>=4){ int wz=(int)(int8_t)db[3]; if(wz)mouse_scroll+=wz; } }
        }
        /* re-arm this device's interrupt transfer (never write into the link-TRB slot 255) */
        volatile u32* tr=(volatile u32*)(u64)(xhid_epin[d]+xhid_ein_enq[d]*16);
        tr[0]=xhid_dbuf[d]; tr[1]=0; tr[2]=(u32)xhid_rlen[d]; tr[3]=(1u<<10)|(1u<<5)|xhid_ein_cyc[d];
        xhid_ein_enq[d]++; if(xhid_ein_enq[d]>=255){volatile u32* lk=(volatile u32*)(u64)(xhid_epin[d]+255*16); lk[3]=(lk[3]&~1u)|xhid_ein_cyc[d]; xhid_ein_enq[d]=0; xhid_ein_cyc[d]^=1;}
        xh_db(xhid_slot[d],(u32)xhid_dci[d]);
    }
}
/* auto-detect HID devices at boot: init the controller, enumerate every connected port */
static void xhci_mouse_autoinit(void){
    xhci_init();
    if(!xhci_present || !xhci_init_ok) return;
    cmd_enq=0; cmd_cyc=1; ev_deq=0; ev_cyc=1; xhid_n=0; xhci_mouse_ok=0;
    for(int p=0;p<xhci_ports && xhid_n<XHID_MAX;p++){ if(!xhci_port_conn[p]) continue; xhci_hid_enum(p); }
}
/* ================= USB Mass Storage: Bulk-Only Transport + SCSI + FAT ================= */
#define MSD_INPUTCTX 0x00A50000u
#define MSD_DEVCTX   0x00A51000u
#define MSD_EP0RING  0x00A52000u
#define MSD_INRING   0x00A53000u
#define MSD_OUTRING  0x00A54000u
#define MSD_CBW      0x00A55000u
#define MSD_CSW      0x00A55100u
#define MSD_DATABUF  0x00A56000u          /* up to 64 KB sector buffer */
static int msd_ok=0, msd_slot=0, msd_in_dci=0, msd_out_dci=0;
static u32 msd_in_enq=0, msd_in_cyc=1, msd_out_enq=0, msd_out_cyc=1, msd_tag=1;
static u32 msd_lastlba=0, msd_bsize=512;
static void ring_init(u32 ring){ volatile u32* r=(volatile u32*)(u64)ring; for(int i=0;i<256*4;i++)r[i]=0;
    int li=255*4; r[li]=ring; r[li+1]=0; r[li+2]=0; r[li+3]=(6u<<10)|(1u<<1)|1u; }
static int xh_bulk(int dci,u32 ring,u32* enq,u32* cyc,u32 buf,int len){
    volatile u32* tr=(volatile u32*)(u64)(ring+(*enq)*16);
    tr[0]=buf; tr[1]=0; tr[2]=(u32)len; tr[3]=(1u<<10)|(1u<<5)|(*cyc);   /* Normal TRB, IOC */
    (*enq)++; if(*enq>=255){ volatile u32* lk=(volatile u32*)(u64)(ring+255*16); lk[3]=(lk[3]&~1u)|(*cyc); *enq=0; *cyc^=1; }
    xh_db(msd_slot,(u32)dci);
    u8 cc=0; if(!xh_event_t(32,0,&cc,0,0)) return -1; return cc;
}
/* one Bulk-Only Transport command: CBW -> optional data -> CSW.  returns CSW status (0=good) or -1 */
static int msd_bot(const u8* cb,int cblen,u32 dbuf,int dlen,int dir_in){
    volatile u8* w=(volatile u8*)(u64)MSD_CBW; for(int i=0;i<31;i++)w[i]=0;
    *(volatile u32*)(u64)MSD_CBW=0x43425355u; *(volatile u32*)(u64)(MSD_CBW+4)=msd_tag++;
    *(volatile u32*)(u64)(MSD_CBW+8)=(u32)dlen; w[12]=dir_in?0x80:0x00; w[13]=0; w[14]=(u8)cblen;
    for(int i=0;i<cblen&&i<16;i++)w[15+i]=cb[i];
    if(xh_bulk(msd_out_dci,MSD_OUTRING,&msd_out_enq,&msd_out_cyc,MSD_CBW,31)<0) return -1;
    if(dlen){ int cc=xh_bulk(dir_in?msd_in_dci:msd_out_dci, dir_in?MSD_INRING:MSD_OUTRING,
                             dir_in?&msd_in_enq:&msd_out_enq, dir_in?&msd_in_cyc:&msd_out_cyc, dbuf, dlen); if(cc<0)return -1; }
    if(xh_bulk(msd_in_dci,MSD_INRING,&msd_in_enq,&msd_in_cyc,MSD_CSW,13)<0) return -1;
    if(*(volatile u32*)(u64)MSD_CSW != 0x53425355u) return -1;   /* 'USBS' */
    return *(volatile u8*)(u64)(MSD_CSW+12);                      /* bCSWStatus */
}
static int scsi_tur(void){ u8 cb[6]={0,0,0,0,0,0}; return msd_bot(cb,6,0,0,1); }
static int scsi_capacity(void){ u8 cb[10]={0x25,0,0,0,0,0,0,0,0,0};
    if(msd_bot(cb,10,MSD_DATABUF,8,1)!=0)return -1; volatile u8* d=(volatile u8*)(u64)MSD_DATABUF;
    msd_lastlba=((u32)d[0]<<24)|((u32)d[1]<<16)|((u32)d[2]<<8)|d[3];
    msd_bsize=((u32)d[4]<<24)|((u32)d[5]<<16)|((u32)d[6]<<8)|d[7]; if(!msd_bsize)msd_bsize=512; return 0; }
/* read `cnt` 512-byte blocks from `lba` into `buf` */
static int scsi_read10(u32 lba,u16 cnt,u32 buf){
    u8 cb[10]={0x28,0,(u8)(lba>>24),(u8)(lba>>16),(u8)(lba>>8),(u8)lba,0,(u8)(cnt>>8),(u8)cnt,0};
    return msd_bot(cb,10,buf,cnt*512,1);
}
/* write `cnt` 512-byte blocks from `buf` to `lba` (used later for write/delete) */
static int scsi_write10(u32 lba,u16 cnt,u32 buf){
    u8 cb[10]={0x2A,0,(u8)(lba>>24),(u8)(lba>>16),(u8)(lba>>8),(u8)lba,0,(u8)(cnt>>8),(u8)cnt,0};
    return msd_bot(cb,10,buf,cnt*512,0);
}
/* enumerate a mass-storage device on `port` (separate slot from HID) */
static int xhci_msd_enum(int port){
    msd_ok=0; xhci_failstep[0]=0; xhci_failcc=0;
    #define MFAIL(m) do{ int _i=0; while(m[_i]&&_i<47){xhci_failstep[_i]=m[_i];_i++;} xhci_failstep[_i]=0; return 0; }while(0)
    xhci_csz=(XMR(xhci_base+0x10)>>2)&1; xhci_dboff=XMR(xhci_base+0x14)&~3u; xhci_rtoff=XMR(xhci_base+0x18)&~0x1Fu;
    u32 op=xhci_op, pa=op+0x400+(u32)port*0x10, pv=XMR(pa);
    if(!(pv&1)) MFAIL("no device on port");
    XMW(pa,(pv&0x0E00C3E0u)|(1u<<4)); for(int t=0;t<200000;t++){ if(XMR(pa)&(1u<<21))break; io_wait(); }
    for(volatile int dl=0;dl<50000;dl++) io_wait();
    pv=XMR(pa); int speed=(pv>>10)&0xF; int mps0=(speed==3)?64:(speed>=4)?512:8;
    cur_ep0=MSD_EP0RING; cur_dbuf=MSD_DATABUF; e0_enq=0; e0_cyc=1; ring_init(MSD_EP0RING);
    u8 cc=0,sl=0; if(!xh_cmd_s(0,0,0,9,0,&cc,&sl)){xhci_failcc=cc; MFAIL("Enable Slot");} cur_slot=sl; msd_slot=sl; if(!sl) MFAIL("no slot");
    ctx_zero(MSD_DEVCTX,(int)ctx_sz()*8/4); *(volatile u32*)(u64)(XHCI_DCBAA+sl*8)=MSD_DEVCTX; *(volatile u32*)(u64)(XHCI_DCBAA+sl*8+4)=0;
    ctx_zero(MSD_INPUTCTX,(int)ctx_sz()*3/4); volatile u32* ic=(volatile u32*)(u64)MSD_INPUTCTX; ic[1]=(1u<<0)|(1u<<1);
    volatile u32* sc=(volatile u32*)(u64)(MSD_INPUTCTX+ctx_sz()); sc[0]=(1u<<27)|((u32)speed<<20); sc[1]=((u32)(port+1)<<16);
    volatile u32* e0=(volatile u32*)(u64)(MSD_INPUTCTX+ctx_sz()*2); e0[1]=(4u<<3)|(3u<<1)|((u32)mps0<<16); e0[2]=MSD_EP0RING|1u; e0[3]=0;
    if(!xh_cmd_s(MSD_INPUTCTX,0,0,11,(u32)sl,&cc,&sl)){xhci_failcc=cc; MFAIL("Address Device");}
    for(volatile int dl=0;dl<20000;dl++) io_wait();
    if(!xh_ctrl(0x80,6,0x0100,0,18,1,0)) MFAIL("GET device desc");
    if(!xh_ctrl(0x80,6,0x0200,0,9,1,0)) MFAIL("GET config(9)");
    u8 c9[9]; { volatile u8* db=(volatile u8*)(u64)cur_dbuf; for(int i=0;i<9;i++)c9[i]=db[i]; }
    int wtot=c9[2]|(c9[3]<<8); if(wtot>256)wtot=256; u8 cfgval=c9[5];
    if(!xh_ctrl(0x80,6,0x0200,0,(u16)wtot,1,0)) MFAIL("GET config(full)");
    u8 full[256]; { volatile u8* db=(volatile u8*)(u64)cur_dbuf; for(int i=0;i<wtot;i++)full[i]=db[i]; }
    int isMSD=0, in_ep=0,in_mps=512,out_ep=0,out_mps=512;
    for(int i=0;i+2<wtot;){ int blen=full[i],bt=full[i+1]; if(blen<2)break;
        if(bt==4){ isMSD=(full[i+5]==0x08); }                                  /* interface class 8 = mass storage */
        else if(bt==5 && isMSD){ int a=full[i+2]; int mps=full[i+4]|(full[i+5]<<8); if((full[i+3]&3)==2){ /* bulk */
            if(a&0x80){ in_ep=a; in_mps=mps; } else { out_ep=a; out_mps=mps; } } }
        i+=blen; }
    if(!in_ep||!out_ep) MFAIL("no bulk endpoints");
    if(!xh_ctrl(0x00,9,cfgval,0,0,0,0)) MFAIL("SET_CONFIGURATION");
    int in_n=in_ep&0x0F, out_n=out_ep&0x0F; msd_in_dci=in_n*2+1; msd_out_dci=out_n*2;   /* IN=2n+1, OUT=2n */
    int maxdci=(msd_in_dci>msd_out_dci)?msd_in_dci:msd_out_dci;
    ctx_zero(MSD_INPUTCTX,(int)ctx_sz()*(maxdci+2)/4);
    ic=(volatile u32*)(u64)MSD_INPUTCTX; ic[1]=(1u<<0)|(1u<<msd_in_dci)|(1u<<msd_out_dci);
    sc=(volatile u32*)(u64)(MSD_INPUTCTX+ctx_sz()); sc[0]=((u32)maxdci<<27)|((u32)speed<<20); sc[1]=((u32)(port+1)<<16);
    ring_init(MSD_INRING); ring_init(MSD_OUTRING); msd_in_enq=0; msd_in_cyc=1; msd_out_enq=0; msd_out_cyc=1;
    volatile u32* epi=(volatile u32*)(u64)(MSD_INPUTCTX+ctx_sz()*(msd_in_dci+1));   /* bulk IN: type 6 */
    epi[1]=(6u<<3)|(3u<<1)|((u32)in_mps<<16); epi[2]=MSD_INRING|1u; epi[3]=0;
    volatile u32* epo=(volatile u32*)(u64)(MSD_INPUTCTX+ctx_sz()*(msd_out_dci+1));  /* bulk OUT: type 2 */
    epo[1]=(2u<<3)|(3u<<1)|((u32)out_mps<<16); epo[2]=MSD_OUTRING|1u; epo[3]=0;
    if(!xh_cmd_s(MSD_INPUTCTX,0,0,12,(u32)msd_slot,&cc,&sl)){xhci_failcc=cc; MFAIL("Configure Endpoint");}
    for(int t=0;t<8;t++){ if(scsi_tur()==0)break; for(volatile int dl=0;dl<40000;dl++)io_wait(); }   /* wait ready */
    if(scsi_capacity()!=0) MFAIL("READ CAPACITY");
    msd_ok=1; return 1;
    #undef MFAIL
}
static int msd_autoinit(void){ if(msd_ok)return 1; if(!xhci_present){ xhci_init(); if(!xhci_present)return 0; }
    for(int p=0;p<xhci_ports;p++){ if(!xhci_port_conn[p])continue; if(xhci_msd_enum(p))return 1; } return 0; }
/* ---- FAT filesystem over the USB stick: read / write / delete
       (ported from the proven Noovex9 32-bit implementation; namespaced ufat_* to avoid the IDE FAT reader) ---- */
static int umsd_read(u32 lba,int cnt,u8* out){ if(!msd_ok)return -1; if(scsi_read10(lba,(u16)cnt,MSD_DATABUF)!=0)return -1;
    volatile u8* s=(volatile u8*)(u64)MSD_DATABUF; for(int i=0;i<cnt*512;i++)out[i]=s[i]; return cnt*512; }
static int umsd_write(u32 lba,int cnt,const u8* in){ if(!msd_ok)return -1; volatile u8* d=(volatile u8*)(u64)MSD_DATABUF;
    for(int i=0;i<cnt*512;i++)d[i]=in[i]; if(scsi_write10(lba,(u16)cnt,MSD_DATABUF)!=0)return -1; return cnt*512; }
static int ufat_ok=0, ufat_type=0, ufat_nfats=2; static u32 ufat_part_lba=0, ufat_fatstart=0, ufat_dataclus=0, ufat_rootclus=0, ufat_rootlba=0, ufat_rootsecs=0, ufat_spc=1, ufat_fatsz=0, ufat_resv=0;
static struct { char name[16]; u32 size, clus; } ufs[32]; static int ufs_n=0;
static u8 ufatcache[512]; static u32 ufc_si=0xFFFFFFFFu; static int ufc_valid=0, ufc_dirty=0;
static void ufat_flush(void){ if(ufc_valid&&ufc_dirty&&ufat_ok){ for(int f=0;f<ufat_nfats;f++) umsd_write(ufat_fatstart+f*ufat_fatsz+ufc_si,1,ufatcache); ufc_dirty=0; } }
static int ufat_load(u32 si){ if(ufc_valid&&ufc_si==si)return 0; ufat_flush(); if(umsd_read(ufat_fatstart+si,1,ufatcache)<0){ufc_valid=0;return -1;} ufc_si=si; ufc_valid=1; ufc_dirty=0; return 0; }
static u32 ufat_next(u32 clus){
    if(ufat_type==16){ u32 off=clus*2; if(ufat_load(off/512)<0)return 0x0FFFFFFFu; u32 o=off%512; u32 v=ufatcache[o]|(ufatcache[o+1]<<8); return (v>=0xFFF8)?0x0FFFFFFFu:v; }
    u32 off=clus*4; if(ufat_load(off/512)<0)return 0x0FFFFFFFu; u32 o=off%512; u32 v=(ufatcache[o]|(ufatcache[o+1]<<8)|(ufatcache[o+2]<<16)|((u32)ufatcache[o+3]<<24))&0x0FFFFFFFu; return (v>=0x0FFFFFF8u)?0x0FFFFFFFu:v;
}
static int ufat_dir_sector(u8* sec){
    for(int e=0;e<16;e++){ u8* d=sec+e*32; if(d[0]==0)return 1; if(d[0]==0xE5||(d[0x0B]&0x0F)==0x0F||(d[0x0B]&0x08))continue;
        if(ufs_n>=32)return 1;
        char nm[16]; int o=0; for(int k=0;k<8;k++) if(d[k]!=' ')nm[o++]=(char)d[k]; if(d[8]!=' '){nm[o++]='.'; for(int k=8;k<11;k++) if(d[k]!=' ')nm[o++]=(char)d[k];} nm[o]=0;
        u32 sz=(u32)d[0x1C]|((u32)d[0x1D]<<8)|((u32)d[0x1E]<<16)|((u32)d[0x1F]<<24);
        u32 clus=(d[0x1A]|(d[0x1B]<<8))|((u32)(d[0x14]|(d[0x15]<<8))<<16);
        int i=ufs_n,k=0; while(nm[k]&&k<15){ufs[i].name[k]=nm[k];k++;} ufs[i].name[k]=0; ufs[i].size=sz; ufs[i].clus=clus; ufs_n++;
    }
    return 0;
}
static void ufat_mount(void){
    ufat_ok=0; ufs_n=0; ufc_valid=0; ufc_dirty=0; if(!msd_ok)return; u8 sec[512];
    if(umsd_read(0,1,sec)<0)return;
    u32 plba=0;
    if(sec[510]==0x55&&sec[511]==0xAA){ u8 ty=sec[450]; if(ty==0x0B||ty==0x0C||ty==0x0E||ty==0x06||ty==0x04||ty==0x01){ plba=sec[454]|(sec[455]<<8)|(sec[456]<<16)|((u32)sec[457]<<24); } }
    if(umsd_read(plba,1,sec)<0)return;
    int bps=sec[0x0B]|(sec[0x0C]<<8); if(bps!=512)return;
    ufat_spc=sec[0x0D]; if(ufat_spc==0)ufat_spc=1;
    ufat_resv=sec[0x0E]|(sec[0x0F]<<8); ufat_nfats=sec[0x10]; if(ufat_nfats==0)ufat_nfats=2;
    int rootent=sec[0x11]|(sec[0x12]<<8); u32 fatsz16=sec[0x16]|(sec[0x17]<<8);
    u32 fatsz32=sec[0x24]|(sec[0x25]<<8)|(sec[0x26]<<16)|((u32)sec[0x27]<<24);
    ufat_fatsz=fatsz16?fatsz16:fatsz32; u32 rootsecs=((rootent*32)+(bps-1))/bps;
    ufat_part_lba=plba; ufat_fatstart=plba+ufat_resv; ufat_dataclus=plba+ufat_resv+ufat_nfats*ufat_fatsz+rootsecs; ufat_rootsecs=rootsecs;
    if(fatsz16){ ufat_type=16; ufat_rootlba=plba+ufat_resv+ufat_nfats*ufat_fatsz; }
    else { ufat_type=32; ufat_rootclus=sec[0x2C]|(sec[0x2D]<<8)|(sec[0x2E]<<16)|((u32)sec[0x2F]<<24); }
    ufat_ok=1;
    if(ufat_type==16){ for(u32 s=0;s<rootsecs&&ufs_n<32;s++){ if(umsd_read(ufat_rootlba+s,1,sec)<0)break; if(ufat_dir_sector(sec))break; } }
    else { u32 clus=ufat_rootclus; int guard=0; while(clus>=2&&clus<0x0FFFFFF8u&&ufs_n<32&&guard++<256){ u32 base=ufat_dataclus+(clus-2)*ufat_spc; int stop=0; for(u32 s=0;s<ufat_spc&&ufs_n<32;s++){ if(umsd_read(base+s,1,sec)<0){stop=1;break;} if(ufat_dir_sector(sec)){stop=1;break;} } if(stop)break; clus=ufat_next(clus); } }
}
static int ufat_read(int fi,char* out,int max){
    if(fi<0||fi>=ufs_n)return 0; u32 clus=ufs[fi].clus; int left=(int)ufs[fi].size; if(left>max)left=max; int got=0,guard=0; u8 sec[512];
    while(clus>=2&&clus<0x0FFFFFF8u&&left>0&&guard++<1048576){ u32 base=ufat_dataclus+(clus-2)*ufat_spc; int stop=0;
        for(u32 s=0;s<ufat_spc&&left>0;s++){ if(umsd_read(base+s,1,sec)<0){stop=1;break;} int n=(left<512)?left:512; for(int k=0;k<n;k++)out[got++]=(char)sec[k]; left-=n; }
        if(stop)break; clus=ufat_next(clus); }
    return got;
}
static void ufat_set(u32 clus,u32 val){
    if(ufat_type==16){ u32 off=clus*2; if(ufat_load(off/512)<0)return; u32 o=off%512; ufatcache[o]=val&0xFF; ufatcache[o+1]=(val>>8)&0xFF; ufc_dirty=1; }
    else { u32 off=clus*4; if(ufat_load(off/512)<0)return; u32 o=off%512; u32 cur=(ufatcache[o]|(ufatcache[o+1]<<8)|(ufatcache[o+2]<<16)|((u32)ufatcache[o+3]<<24)); cur=(cur&0xF0000000u)|(val&0x0FFFFFFFu); ufatcache[o]=cur&0xFF;ufatcache[o+1]=(cur>>8)&0xFF;ufatcache[o+2]=(cur>>16)&0xFF;ufatcache[o+3]=(cur>>24)&0xFF; ufc_dirty=1; }
}
static u32 ufat_alloc(void){ u32 maxc=(ufat_type==16)?(ufat_fatsz*256u):(ufat_fatsz*128u); if(maxc>65536)maxc=65536;
    for(u32 c=2;c<maxc;c++){ u32 v=ufat_next(c); if(v==0){ ufat_set(c, ufat_type==16?0xFFFFu:0x0FFFFFFFu); return c; } } return 0; }
static void ufat_free_chain(u32 clus){ int g=0; while(clus>=2&&clus<0x0FFFFFF8u&&g++<4096){ u32 nx=ufat_next(clus); ufat_set(clus,0); clus=nx; } }
static void ufat_make83(const char* nm,char out[11]){ for(int i=0;i<11;i++)out[i]=' ';
    int i=0,o=0; while(nm[i]&&nm[i]!='.'&&o<8){ char c=nm[i]; if(c>='a'&&c<='z')c-=32; out[o++]=c; i++; }
    while(nm[i]&&nm[i]!='.')i++; if(nm[i]=='.'){ i++; int e=8; while(nm[i]&&e<11){ char c=nm[i]; if(c>='a'&&c<='z')c-=32; out[e++]=c; i++; } } }
static int ufat_dirent_scan(const char* n83,int findfree,u32* olba,int* ooff){ u8 sec[512];
    if(ufat_type==16){ for(u32 s=0;s<ufat_rootsecs;s++){ u32 lba=ufat_rootlba+s; if(umsd_read(lba,1,sec)<0)return 0;
        for(int e=0;e<16;e++){ u8* d=sec+e*32;
            if(findfree){ if(d[0]==0x00||d[0]==0xE5){ *olba=lba; *ooff=e*32; return 1; } }
            else { if(d[0]==0x00)return 0; if(d[0]==0xE5||(d[0x0B]&0x0F)==0x0F)continue; int m=1; for(int k=0;k<11;k++)if(d[k]!=(u8)n83[k]){m=0;break;} if(m){ *olba=lba; *ooff=e*32; return 1; } } } } return 0; }
    u32 clus=ufat_rootclus; int guard=0;
    while(clus>=2&&clus<0x0FFFFFF8u&&guard++<4096){ u32 base=ufat_dataclus+(clus-2)*ufat_spc;
        for(u32 s=0;s<ufat_spc;s++){ u32 lba=base+s; if(umsd_read(lba,1,sec)<0)return 0;
            for(int e=0;e<16;e++){ u8* d=sec+e*32;
                if(findfree){ if(d[0]==0x00||d[0]==0xE5){ *olba=lba; *ooff=e*32; return 1; } }
                else { if(d[0]==0x00)return 0; if(d[0]==0xE5||(d[0x0B]&0x0F)==0x0F)continue; int m=1; for(int k=0;k<11;k++)if(d[k]!=(u8)n83[k]){m=0;break;} if(m){ *olba=lba; *ooff=e*32; return 1; } } } }
        clus=ufat_next(clus); } return 0; }
static int ufat_write_file(const char* name,const char* data,int len){
    if(!ufat_ok||!msd_ok)return -1; char n83[11]; ufat_make83(name,n83); u32 elba; int eoff; u8 sec[512];
    int exists=ufat_dirent_scan(n83,0,&elba,&eoff);
    if(exists){ if(umsd_read(elba,1,sec)<0)return -1; u8* d=sec+eoff; u32 oc=(d[0x1A]|(d[0x1B]<<8))|((u32)(d[0x14]|(d[0x15]<<8))<<16); if(oc>=2)ufat_free_chain(oc); }
    else { if(!ufat_dirent_scan(n83,1,&elba,&eoff))return -1; }
    int spb=ufat_spc*512; int nclus=(len+spb-1)/spb; if(nclus<1)nclus=1; u32 first=0,prev=0; int wr=0;
    for(int c=0;c<nclus;c++){ u32 cl=ufat_alloc(); if(cl==0)return -1; if(!first)first=cl; if(prev)ufat_set(prev,cl); prev=cl;
        u32 base=ufat_dataclus+(cl-2)*ufat_spc;
        for(u32 s=0;s<ufat_spc;s++){ u8 ds[512]; for(int k=0;k<512;k++)ds[k]=0; int n=len-wr; if(n>512)n=512; if(n<0)n=0; for(int k=0;k<n;k++)ds[k]=(u8)data[wr+k]; wr+=n; if(umsd_write(base+s,1,ds)<0)return -1; } }
    if(prev)ufat_set(prev, ufat_type==16?0xFFFFu:0x0FFFFFFFu);
    if(umsd_read(elba,1,sec)<0)return -1; u8* d=sec+eoff;
    for(int i=0;i<11;i++)d[i]=(u8)n83[i]; d[0x0B]=0x20; for(int i=0x0C;i<0x20;i++)d[i]=0;
    d[0x1A]=first&0xFF;d[0x1B]=(first>>8)&0xFF;d[0x14]=(first>>16)&0xFF;d[0x15]=(first>>24)&0xFF;
    d[0x1C]=len&0xFF;d[0x1D]=(len>>8)&0xFF;d[0x1E]=(len>>16)&0xFF;d[0x1F]=(len>>24)&0xFF;
    if(umsd_write(elba,1,sec)<0)return -1; ufat_flush(); ufat_mount(); return 0; }
static int ufat_delete_file(int idx){
    if(idx<0||idx>=ufs_n||!ufat_ok)return -1; char n83[11]; ufat_make83(ufs[idx].name,n83); u32 lba; int off; u8 sec[512];
    if(!ufat_dirent_scan(n83,0,&lba,&off))return -1; if(umsd_read(lba,1,sec)<0)return -1; u8* d=sec+off;
    u32 oc=(d[0x1A]|(d[0x1B]<<8))|((u32)(d[0x14]|(d[0x15]<<8))<<16); if(oc>=2)ufat_free_chain(oc);
    d[0]=0xE5; if(umsd_write(lba,1,sec)<0)return -1; ufat_flush(); ufat_mount(); return 0; }
/* ---- USB hotplug: auto-mount + auto-open the file manager when a stick is inserted ---- */
static int g_files_usb=0, g_usb_arrived=0, msd_port=-1;static u8 usb_seen[16]={0}; static u64 usb_poll_t=0; static int usb_seen_init=0;
static void usb_hotplug_poll(void){
    if(!xhci_present||!xhci_init_ok)return;
    if(!usb_seen_init){ for(int p=0;p<xhci_ports;p++){ u32 v=XMR(xhci_op+0x400+p*0x10); usb_seen[p]=(u8)(v&1); } usb_seen_init=1; return; }  /* boot-time devices are "known" (protects the mouse) */
    if(g_ticks-usb_poll_t<80)return; usb_poll_t=g_ticks;                     /* ~0.8s throttle */
    for(int p=0;p<xhci_ports;p++){ u32 v=XMR(xhci_op+0x400+p*0x10); int cc=v&1;
        if(cc && !usb_seen[p]){ usb_seen[p]=1;                               /* a NEW device appeared on this port */
            if(!msd_ok){ xhci_port_conn[p]=1; if(xhci_msd_enum(p)){ ufat_mount(); if(ufat_ok){ g_usb_arrived=1; msd_port=p; } } } }
        else if(!cc && usb_seen[p]){ usb_seen[p]=0;                          /* device removed */
            if(msd_ok && msd_port==p){ msd_ok=0; ufat_ok=0; ufs_n=0; msd_port=-1; g_files_usb=0; } }
    }
}
