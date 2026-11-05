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

static int usbk_rep_code=0, usbk_rep_dev=-1; static u8 usbk_rep_usage=0; static u64 usbk_rep_t0=0, usbk_rep_last=0;
static int usb_key_code(u8 u,int shift){
    switch(u){ case 0x4F: return 18; case 0x50: return 17; case 0x51: return 20; case 0x52: return 19; case 0x4A: return 2; case 0x4D: return 5; case 0x4B: return 6; case 0x4E: return 7; case 0x4C: return 14; case 0x49: return 15; case 0x29: return 27; case 0x58: return '\n';
        case 0x54: return '/'; case 0x55: return '*'; case 0x56: return '-'; case 0x57: return '+'; case 0x62: return '0'; case 0x63: return '.'; }
    if(u>=0x59&&u<=0x61) return '1'+(u-0x59);
    int c=hid2ascii(u,shift); if(kcaps&&c>='a'&&c<='z')c-=32; else if(kcaps&&c>='A'&&c<='Z'&&shift)c+=32;
    return c;
}
static void kbd_report(int e,volatile u8* d){
    u8* prev=xhid_kprev[e]; u8 mod=d[0]; int shift=(mod&0x22)!=0;
    kdown[0x1D]=(mod&0x11)?1:0; kdown[0x2A]=(mod&0x22)?1:0; kdown[0x38]=(mod&0x44)?1:0; kdown[0x5B]=(mod&0x88)?1:0;
    if(d[2]==1)return;
    for(int i=2;i<8;i++){ u8 k=d[i]; if(!k)continue; int was=0; for(int j=2;j<8;j++) if(prev[j]==k){was=1;break;}
        if(!was){ u8 sc=hid2scan(k); if(sc)kdown[sc]=1; { extern volatile u32 g_kbd_last_tick; if(k>=4&&k<0xE0)g_kbd_last_tick=(u32)g_ticks; }
            if(k==0x39){ kcaps^=1; continue; }
            if(k==0x45){ kpush(31); continue; }
            if(k==0x46){ extern void hk_push(int); hk_push((mod&0x22)?30:(mod&0x44)?29:28); continue; }
            if(k==0x2A&&(mod&0x11)&&(mod&0x44)){ kpush(30); continue; }
            int c=usb_key_code(k,shift); if(c){ kpush(c); usbk_rep_code=c; usbk_rep_usage=k; usbk_rep_dev=e; usbk_rep_t0=g_ticks; usbk_rep_last=g_ticks; } } }
    for(int i=2;i<8;i++){ u8 k=prev[i]; if(!k)continue; int still=0; for(int j=2;j<8;j++) if(d[j]==k){still=1;break;}
        if(!still){ u8 sc=hid2scan(k); if(sc)kdown[sc]=0; if(usbk_rep_dev==e&&usbk_rep_usage==k){ usbk_rep_code=0; usbk_rep_dev=-1; } } }
    for(int i=0;i<8;i++) prev[i]=d[i];
}
static void usb_kbd_repeat(void){
    if(!usbk_rep_code)return;
    extern int g_krep_delay, g_krep_rate, g_krep_flag;
    if(g_ticks-usbk_rep_t0>=(u64)g_krep_delay&&g_ticks-usbk_rep_last>=(u64)g_krep_rate){ g_krep_flag=1; kpush(usbk_rep_code); g_krep_flag=0; usbk_rep_last=g_ticks; }
}
static void hid_report_decode(int e,const u8* b,int len){
    int id=0; const u8* p=b; int pl=len; if(xhid_hasid[e]){ if(len<1)return; id=b[0]; p=b+1; pl=len-1; }
    for(int q=0;q<xhid_nlay[e];q++){ hidlay_t* L=&xhid_lay[e][q]; if(L->id!=id)continue;
        if(L->kind==2){ int btn=0; for(int i=0;i<L->btn_cnt&&i<8;i++) if(hid_bits(p,pl,L->btn_off+i,1,0))btn|=1<<i;
            int dx=hid_bits(p,pl,L->x_off,L->x_sz,1), dy=hid_bits(p,pl,L->y_off,L->y_sz,1), wz=L->w_off>=0?hid_bits(p,pl,L->w_off,L->w_sz,1):0;
            if(dx||dy){ mouse_dx+=dx; mouse_dy-=dy; mouse_evt=1; }
            in_btn_raw(btn&1,(btn>>1)&1,(btn>>2)&1); in_xbtn((btn>>3)&1,(btn>>4)&1); if(btn)mouse_evt=1; if(wz)mouse_scroll+=wz; }
        else { u8 r[8]={0,0,0,0,0,0,0,0}; if(L->mod_off>=0) for(int i=0;i<8;i++) if(hid_bits(p,pl,L->mod_off+i,1,0))r[0]|=(u8)(1<<i);
            int n=2;
            if(L->arr_cnt>0) for(int i=0;i<L->arr_cnt&&n<8;i++){ int k=hid_bits(p,pl,L->arr_off+i*8,8,0); if(k)r[n++]=(u8)k; }
            else for(int i=0;i<L->bm_cnt&&n<8;i++) if(hid_bits(p,pl,L->bm_off+i,1,0)){ int u=L->bm_min+i; if(u>=4)r[n++]=(u8)u; }
            kbd_report(e,r); }
        return; }
}
static void hid_xfer(u8 sl,int epid,u8 cc,u32 res,u32 ptr){
    int d=-1; for(int i=0;i<xhid_n;i++) if(xhid_used[i]&&xhid_ctl[i]==g_xcur&&xhid_slot[i]==sl&&xhid_dci[i]==epid){d=i;break;} if(d<0)return;
    if(cc==1||cc==13){
        u32 ba=xhid_dbuf[d]; if(ptr>=xhid_epin[d]&&ptr<xhid_epin[d]+256*16){ u32 b2=((volatile u32*)(u64)ptr)[0]; if(b2>=xhid_dbuf[d]&&b2<xhid_dbuf[d]+XHID_INFLIGHT*64u)ba=b2; }
        volatile u8* db=(volatile u8*)(u64)ba;
        int got=xhid_rlen[d]-(int)res; if(got<0)got=0;
        xhid_cnt[d]++; { int nn=got<16?got:16; for(int q=0;q<nn;q++)xhid_last[d][q]=db[q]; xhid_lastn[d]=nn; }
        if(xhid_rep[d]) hid_report_decode(d,(const u8*)db,got);
        else if(xhid_iskbd[d]) kbd_report(d,db);
        else { u8 btn=db[0]; int dx=(int)(int8_t)db[1]; int dy=(int)(int8_t)db[2];
            if(dx||dy){ mouse_dx+=dx; mouse_dy-=dy; mouse_evt=1; }
            in_btn_raw(btn&1,(btn>>1)&1,(btn>>2)&1); in_xbtn((btn>>3)&1,(btn>>4)&1); if(btn)mouse_evt=1;
            if(xhid_rlen[d]>=4){ int wz=(int)(int8_t)db[3]; if(wz)mouse_scroll+=wz; } }
    }
    xhid_arm(d);
}
static void xhci_poll_one(void){
    for(int i=0;i<xh_qn;){ if(xh_q[i].ctl!=(u8)g_xcur){ i++; continue; } xhq_t q=xh_q[i]; for(int k=i+1;k<xh_qn;k++)xh_q[k-1]=xh_q[k]; xh_qn--; hid_xfer(q.sl,q.ep,q.cc,q.res,q.ptr); }
    for(int guard=0; guard<128; guard++){
        volatile u32* e=(volatile u32*)(u64)(XHCI_EVTRING+ev_deq*16);
        if((e[3]&1)!=ev_cyc)return;
        u32 type=(e[3]>>10)&0x3F; u8 sl=(u8)((e[3]>>24)&0xFF); u8 cc=(u8)((e[2]>>24)&0xFF); int epid=(int)((e[3]>>16)&0x1F); u32 res=e[2]&0xFFFFFF;
        ev_deq++; if(ev_deq>=256){ev_deq=0;ev_cyc^=1;}
        u64 rt=xhci_base+xhci_rtoff+0x20; XMW(rt+0x18,(XHCI_EVTRING+ev_deq*16)|(1u<<3)); XMW(rt+0x1C,0);
        if(type==32)hid_xfer(sl,epid,cc,res,e[0]);
    }
}
void xhci_mouse_poll(void){
    if(!xhid_n)return;
    usb_kbd_repeat();
    for(int c=0;c<g_xhc_n;c++){ xhc_select(c); if(xhci_init_ok)xhci_poll_one(); }
}
static void xhub_remove_below(int root,u32 R,int dR);
static void xhid_remove_port(int port){ xhub_remove_below(port,0,0); }
void xhci_mouse_autoinit(void){
    xhci_init(); xhid_reset();
    for(int c=0;c<g_xhc_n;c++){ xhc_select(c); if(!xhci_present||!xhci_init_ok)continue;
        for(int p=0;p<xhci_ports;p++){ if(!xhci_port_conn[p]) continue; if(xhid_free_region(-1,-1)<0)break; xhci_hid_enum(p); } }
    if(g_xhc_n)xhc_select(0);
}
/* ================= USB Mass Storage: Bulk-Only Transport + SCSI + FAT ================= */
#define MSD_INPUTCTX DMW(0xD0000u)
#define MSD_DEVCTX   DMW(0xD1000u)
#define MSD_EP0RING  DMW(0xD2000u)
#define MSD_INRING   DMW(0xD3000u)
#define MSD_OUTRING  DMW(0xD4000u)
#define MSD_CBW      DMW(0xD5000u)
#define MSD_CSW      DMW(0xD5100u)
#define MSD_DATABUF  DMW(0xD6000u)
int msd_ok=0, msd_slot=0, msd_in_dci=0, msd_out_dci=0;
static u32 msd_in_enq=0, msd_in_cyc=1, msd_out_enq=0, msd_out_cyc=1, msd_tag=1;
u32 msd_lastlba=0, msd_bsize=512;
static void ring_init(u32 ring){ volatile u32* r=(volatile u32*)(u64)ring; for(int i=0;i<256*4;i++)r[i]=0;
    int li=255*4; r[li]=ring; r[li+1]=0; r[li+2]=0; r[li+3]=(6u<<10)|(1u<<1)|1u; }
static int msd_wait_rounds=1;
static int xh_bulk(int dci,u32 ring,u32* enq,u32* cyc,u32 buf,int len){
    volatile u32* tr=(volatile u32*)(u64)(ring+(*enq)*16);
    tr[0]=buf; tr[1]=0; tr[2]=(u32)len; tr[3]=(1u<<10)|(1u<<5)|(*cyc);   /* Normal TRB, IOC */
    (*enq)++; if(*enq>=255){ volatile u32* lk=(volatile u32*)(u64)(ring+255*16); lk[3]=(lk[3]&~1u)|(*cyc); *enq=0; *cyc^=1; }
    xh_db(msd_slot,(u32)dci);
    u8 cc=0; xh_want_slot=msd_slot; xh_want_ep=dci; int ok=0; for(int r=0;r<msd_wait_rounds&&!ok;r++)ok=xh_event_t(32,0,&cc,0,0); xh_want_slot=-1; xh_want_ep=-1; if(!ok) return -1; return cc;
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
static u32 msd_route=0; static int msd_depth=0;
static int xhci_msd_enum_loc(const usbloc_t* L);
static int xhci_msd_enum(int port){
    msd_ok=0; xhci_failstep[0]=0; xhci_failcc=0;
    int sp=xh_root_reset(port); if(!sp){ xh_set_fail("no device on port"); return 0; }
    usbloc_t L; L.root=port; L.depth=0; L.hub=-1; L.hport=0; L.speed=sp; L.route=0; L.tt_slot=0; L.tt_port=0;
    return xhci_msd_enum_loc(&L); }
static int xhci_msd_enum_loc(const usbloc_t* L){
    msd_ok=0; msd_slot=0; xhci_failstep[0]=0; xhci_failcc=0;
    #define MFAIL(m) do{ int _i=0; while(m[_i]&&_i<47){xhci_failstep[_i]=m[_i];_i++;} xhci_failstep[_i]=0; return 0; }while(0)
    int speed=L->speed; int mps0=(speed==3)?64:(speed>=4)?512:8;
    cur_ep0=MSD_EP0RING; cur_dbuf=MSD_DATABUF; e0_enq=0; e0_cyc=1; ring_init(MSD_EP0RING);
    u8 cc=0,sl=0; if(!xh_cmd_s(0,0,0,9,0,&cc,&sl)){xhci_failcc=cc; MFAIL("Enable Slot");} cur_slot=sl; msd_slot=sl; if(!sl) MFAIL("no slot");
    ctx_zero(MSD_DEVCTX,(int)ctx_sz()*8/4); *(volatile u32*)(u64)(XHCI_DCBAA+sl*8)=MSD_DEVCTX; *(volatile u32*)(u64)(XHCI_DCBAA+sl*8+4)=0;
    ctx_zero(MSD_INPUTCTX,(int)ctx_sz()*3/4); volatile u32* ic=(volatile u32*)(u64)MSD_INPUTCTX; ic[1]=(1u<<0)|(1u<<1);
    volatile u32* sc=(volatile u32*)(u64)(MSD_INPUTCTX+ctx_sz()); xh_slot_ctx(sc,L,1);
    volatile u32* e0=(volatile u32*)(u64)(MSD_INPUTCTX+ctx_sz()*2); e0[1]=(4u<<3)|(3u<<1)|((u32)mps0<<16); e0[2]=MSD_EP0RING|1u; e0[3]=0;
    if(!xh_cmd_s(MSD_INPUTCTX,0,0,11,(u32)sl,&cc,&sl)){xhci_failcc=cc; MFAIL("Address Device");}
    for(volatile int dl=0;dl<20000;dl++) io_wait();
    if(!xh_fix_mps0(MSD_INPUTCTX,L,MSD_EP0RING)) MFAIL("GET device desc (8)");
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
    sc=(volatile u32*)(u64)(MSD_INPUTCTX+ctx_sz()); xh_slot_ctx(sc,L,maxdci);
    ring_init(MSD_INRING); ring_init(MSD_OUTRING); msd_in_enq=0; msd_in_cyc=1; msd_out_enq=0; msd_out_cyc=1;
    volatile u32* epi=(volatile u32*)(u64)(MSD_INPUTCTX+ctx_sz()*(msd_in_dci+1));   /* bulk IN: type 6 */
    epi[1]=(6u<<3)|(3u<<1)|((u32)in_mps<<16); epi[2]=MSD_INRING|1u; epi[3]=0;
    volatile u32* epo=(volatile u32*)(u64)(MSD_INPUTCTX+ctx_sz()*(msd_out_dci+1));  /* bulk OUT: type 2 */
    epo[1]=(2u<<3)|(3u<<1)|((u32)out_mps<<16); epo[2]=MSD_OUTRING|1u; epo[3]=0;
    if(!xh_cmd_s(MSD_INPUTCTX,0,0,12,(u32)msd_slot,&cc,&sl)){xhci_failcc=cc; MFAIL("Configure Endpoint");}
    for(int t=0;t<8;t++){ if(scsi_tur()==0)break; for(volatile int dl=0;dl<40000;dl++)io_wait(); }   /* wait ready */
    if(scsi_capacity()!=0) MFAIL("READ CAPACITY");
    msd_route=L->route; msd_depth=L->depth; msd_ok=1; return 1;
    #undef MFAIL
}
static void xhci_port_refresh(void){ if(!xhci_op)return; for(int p=0;p<xhci_ports;p++){ u32 v=XMR(xhci_op+0x400+(u32)p*0x10); if((v&1)&&!xhci_port_conn[p]){ xhci_port_conn[p]=1; xhci_port_speed[p]=(u8)((v>>10)&0xF); } } }
int msd_autoinit(void){ if(msd_ok){ xhc_select(msd_ctl); return 1; } if(!g_xhc_n){ xhci_init(); if(!g_xhc_n)return 0; }
    for(int c=0;c<g_xhc_n;c++){ xhc_select(c); if(!xhci_present||!xhci_init_ok)continue; xhci_port_refresh(); for(int p=0;p<xhci_ports;p++){ if(!xhci_port_conn[p])continue; if(xhci_msd_enum(p)){ msd_ctl=c; return 1; } } } return 0; }
/* ---- FAT filesystem over the USB stick: read / write / delete
       (ported from the proven Noovex9 32-bit implementation; namespaced ufat_* to avoid the IDE FAT reader) ---- */
static int umsd_read(u32 lba,int cnt,u8* out){ if(!msd_ok)return -1; xhc_select(msd_ctl); msd_wait_rounds=25; int rr=scsi_read10(lba,(u16)cnt,MSD_DATABUF); msd_wait_rounds=1; if(rr!=0)return -1;
    volatile u8* s=(volatile u8*)(u64)MSD_DATABUF; for(int i=0;i<cnt*512;i++)out[i]=s[i]; return cnt*512; }
static int umsd_write(u32 lba,int cnt,const u8* in){ if(!msd_ok)return -1; xhc_select(msd_ctl); volatile u8* d=(volatile u8*)(u64)MSD_DATABUF;
    for(int i=0;i<cnt*512;i++)d[i]=in[i]; msd_wait_rounds=25; int rr=scsi_write10(lba,(u16)cnt,MSD_DATABUF); msd_wait_rounds=1; if(rr!=0)return -1; return cnt*512; }
int usb_msd_read512(u32 lba,u8* out){ return umsd_read(lba,1,out)==512?0:-1; }
u64 usb_msd_sectors(void){ return msd_ok?(u64)msd_lastlba+1:0; }
int usb_msd_read(u32 lba,int cnt,u8* out){ return umsd_read(lba,cnt,out)==cnt*512?0:-1; }
static int umsd_chunk(void){ u32 a=(u32)MSD_DATABUF; int c=(int)((0x10000u-(a&0xFFFFu))/512u); return c>64?64:c<1?1:c; }
int usb_msd_rdn(u32 lba,int cnt,u8* out){ if(!msd_ok||msd_bsize!=512)return -1; int ch=umsd_chunk();
    while(cnt>0){ int c=cnt>ch?ch:cnt; if(umsd_read(lba,c,out)!=c*512&&umsd_read(lba,c,out)!=c*512)return -1; lba+=(u32)c; out+=c*512; cnt-=c; } return 0; }
int usb_msd_wrn(u32 lba,int cnt,const u8* in){ if(!msd_ok||msd_bsize!=512)return -1; int ch=umsd_chunk();
    while(cnt>0){ int c=cnt>ch?ch:cnt; if(umsd_write(lba,c,in)!=c*512&&umsd_write(lba,c,in)!=c*512)return -1; lba+=(u32)c; in+=c*512; cnt-=c; } return 0; }
int usb_msd_sync(void){ if(!msd_ok)return -1; xhc_select(msd_ctl); u8 cb[10]={0x35,0,0,0,0,0,0,0,0,0}; msd_wait_rounds=150; int r=msd_bot(cb,10,0,0,0); msd_wait_rounds=1; return r==0?0:-1; }
static u32 ufat_nclus=0, ufat_hint=2;
int ufat_ok=0, ufat_type=0, ufat_nfats=2; static u32 ufat_part_lba=0, ufat_fatstart=0, ufat_dataclus=0, ufat_rootclus=0, ufat_rootlba=0, ufat_rootsecs=0, ufat_spc=1, ufat_fatsz=0, ufat_resv=0;
struct nvx_ufs_t ufs[32]; int ufs_n=0;
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
void ufat_sync(void){ ufat_flush(); }
void ufat_mount(void){
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
    { u32 ts=sec[0x13]|(sec[0x14]<<8); if(!ts)ts=sec[0x20]|(sec[0x21]<<8)|(sec[0x22]<<16)|((u32)sec[0x23]<<24); u32 ds=ufat_resv+ufat_nfats*ufat_fatsz+rootsecs; ufat_nclus=(ts>ds&&ufat_spc)?(ts-ds)/ufat_spc:0; }
    ufat_hint=2;
    if(ufat_type==32){ u32 fsl=sec[0x30]|(sec[0x31]<<8); u8 fs[512]; if(fsl&&fsl<ufat_resv&&umsd_read(plba+fsl,1,fs)>=0){ u32 s1=fs[0]|(fs[1]<<8)|(fs[2]<<16)|((u32)fs[3]<<24), s2=fs[484]|(fs[485]<<8)|(fs[486]<<16)|((u32)fs[487]<<24), nx=fs[492]|(fs[493]<<8)|(fs[494]<<16)|((u32)fs[495]<<24); if(s1==0x41615252u&&s2==0x61417272u&&nx>=2&&nx!=0xFFFFFFFFu)ufat_hint=nx; } }
    ufat_ok=1;
    if(ufat_type==16){ for(u32 s=0;s<rootsecs&&ufs_n<32;s++){ if(umsd_read(ufat_rootlba+s,1,sec)<0)break; if(ufat_dir_sector(sec))break; } }
    else { u32 clus=ufat_rootclus; int guard=0; while(clus>=2&&clus<0x0FFFFFF8u&&ufs_n<32&&guard++<256){ u32 base=ufat_dataclus+(clus-2)*ufat_spc; int stop=0; for(u32 s=0;s<ufat_spc&&ufs_n<32;s++){ if(umsd_read(base+s,1,sec)<0){stop=1;break;} if(ufat_dir_sector(sec)){stop=1;break;} } if(stop)break; clus=ufat_next(clus); } }
}
int ufat_read(int fi,char* out,int max){
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
static u32 ufat_alloc(void){ u32 maxc=(ufat_type==16)?(ufat_fatsz*256u):(ufat_fatsz*128u); if(ufat_nclus&&ufat_nclus+2<maxc)maxc=ufat_nclus+2; if(maxc>0x0FFFFFF0u)maxc=0x0FFFFFF0u; if(maxc<3)return 0;
    if(ufat_hint<2||ufat_hint>=maxc)ufat_hint=2;
    for(u32 k=0,c=ufat_hint;k<maxc-2;k++,c=(c+1<maxc)?c+1:2){ u32 v=ufat_next(c); if(v==0){ ufat_set(c, ufat_type==16?0xFFFFu:0x0FFFFFFFu); ufat_hint=c+1; return c; } } return 0; }
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
int ufat_write_file(const char* name,const char* data,int len){
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
