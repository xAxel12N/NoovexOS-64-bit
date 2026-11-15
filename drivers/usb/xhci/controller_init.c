static u32 xhc_mem_for(int i){ if(!g_xhc_mem[i]){ void* p=dma_alloc_pages(0x26); g_xhc_mem[i]=p?(u32)(u64)p:0u; } return g_xhc_mem[i]; }
 static void xhci_init(void);
static u64 g_xspb_mem[XHC_MAX]; int g_xspb_n[XHC_MAX], g_xspb_need[XHC_MAX];
#define XHCI_DCBAA   (g_xmem+0x0000u)
#define XHCI_CMDRING (g_xmem+0x1000u)
#define XHCI_EVTRING (g_xmem+0x2000u)
#define XHCI_ERST    (g_xmem+0x3000u)
#define XHCI_SCRATCH (g_xmem+0x4000u)

#define XMW(a,v) (*(volatile u32*)(u64)(a)=(u32)(v))
int xhci_present=0, xhci_init_ok=0, xhci_slots=0, xhci_ports=0; u64 xhci_base=0, xhci_op=0;
static u8 xhci_port_conn[32], xhci_port_speed[32]; int xhci_ports_hw=0, xhci_pci_bus=-1, xhci_pci_slot=-1; static u16 xhci_vendor=0;
static void xdelay(int n){ for(int i=0;i<n;i++) io_wait(); }
static const char* xhci_spd(int s){ switch(s){case 1:return "FULL";case 2:return "LOW";case 3:return "HIGH";case 4:return "SUPER";default:return "-";} }
static void xhci_init_one(int bus,int slot,int fn){
    xhci_present=0; xhci_init_ok=0; xhci_slots=0; xhci_ports=0; xhci_hi4g=0; for(int i=0;i<32;i++){xhci_port_conn[i]=0;xhci_port_speed[i]=0;}
    xhci_present=1; xhci_pci_bus=bus; xhci_pci_slot=slot; xhci_pci_fn=fn;
    xhci_vendor=(u16)(pci_cfg(bus,slot,fn,0)&0xFFFF);
    u32 bar=pci_cfg(bus,slot,fn,0x10); if(bar&1)return; u64 base=bar&0xFFFFFFF0u; if((bar&6)==4)base|=(u64)pci_cfg(bus,slot,fn,0x14)<<32; if(!base)return;
    if(base>=0x100000000ULL){ extern u64 acpi_hi_map(u64); if(!acpi_hi_map(base)||((base+0xFFFFULL)>>30)!=(base>>30)){ xhci_hi4g=1; return; } }
    u32 cmd=pci_cfg(bus,slot,fn,0x04); pci_cfgw(bus,slot,fn,0x04,cmd|0x06);
    if(xhci_vendor==0x8086){ u32 u3=pci_cfg(bus,slot,fn,0xDC); pci_cfgw(bus,slot,fn,0xD8,u3); u32 u2=pci_cfg(bus,slot,fn,0xD4); pci_cfgw(bus,slot,fn,0xD0,u2); }
    xhci_base=base;
    { u32 hcc=XMR(base+0x10); u32 x=(hcc>>16)<<2; int g=0;
      while(x&&g++<64){ u64 a=base+x; u32 v=XMR(a);
        if((v&0xFF)==1){ *(volatile u8*)(u64)(a+3)=1;
            for(int t=0;t<400000&&(XMR(a)&(1u<<16));t++)io_wait();
            if(XMR(a)&(1u<<16)){ *(volatile u8*)(u64)(a+2)=0; klog("usb: firmware did not release the controller, taking it"); } else klog("usb: controller handed over by firmware");
            u32 c=XMR(a+4); XMW(a+4,(c&~0x0000E011u)|0xE0000000u); break; }
        u32 nx=(v>>8)&0xFF; if(!nx)break; x+=nx<<2; } }
    u8 caplen=*(volatile u8*)(u64)base;
    u32 hcs1=XMR(base+0x04); int maxslots=hcs1&0xFF; int maxports=(hcs1>>24)&0xFF; xhci_ports_hw=maxports; if(maxports>32)maxports=32; if(maxports<1)maxports=1; xhci_ports=maxports; xhci_slots=maxslots;
    u32 hcs2=XMR(base+0x08); int spb=(((hcs2>>21)&0x1F)<<5)|((hcs2>>27)&0x1F); g_xspb_need[g_xcur]=spb;
    u32 rtsoff=XMR(base+0x18)&~0x1Fu;
    u64 op=base+caplen;
    volatile u32* usbcmd=(volatile u32*)(u64)(op+0x00);
    volatile u32* usbsts=(volatile u32*)(u64)(op+0x04);
    for(int t=0;t<200000;t++){ if(!(*usbsts&(1u<<11)))break; xdelay(1); }
    *usbcmd &= ~1u; for(int t=0;t<200000;t++){ if(*usbsts&1u)break; xdelay(1); }
    *usbcmd |= (1u<<1); int rok=0;
    for(int t=0;t<400000;t++){ if(!(*usbcmd&(1u<<1)) && !(*usbsts&(1u<<11))){ rok=1; break; } xdelay(1); }
    XMW(op+0x38,(u32)maxslots);
    { volatile u32* dc=(volatile u32*)(u64)XHCI_DCBAA; for(int i=0;i<(maxslots+1)*2;i++)dc[i]=0; }
    if(spb>0){ u64 arr=XHCI_SCRATCH, bufs=XHCI_SCRATCH+0x1000u;
        if(spb>32){ if(!g_xspb_mem[g_xcur]||g_xspb_n[g_xcur]<spb){ u64 m=(u64)dma_alloc_pages((unsigned long)spb+1); if(m){ g_xspb_mem[g_xcur]=m; g_xspb_n[g_xcur]=spb; } }
            if(!g_xspb_mem[g_xcur]){ klog("usb: not enough memory for the controller's scratchpad, controller not used"); return; }
            arr=g_xspb_mem[g_xcur]; bufs=arr+0x1000u; }
        volatile u32* sa=(volatile u32*)arr; for(int i=0;i<1024;i++)sa[i]=0;
        for(int i=0;i<spb;i++){ u64 b=bufs+(u64)i*0x1000u; sa[i*2]=(u32)b; sa[i*2+1]=(u32)(b>>32); volatile u64* bb=(volatile u64*)b; for(int k=0;k<512;k++)bb[k]=0; }
        *(volatile u32*)(u64)XHCI_DCBAA=(u32)arr; *(volatile u32*)(u64)(XHCI_DCBAA+4)=(u32)(arr>>32); }
    XMW(op+0x30,XHCI_DCBAA); XMW(op+0x34,0);
    { volatile u32* cr=(volatile u32*)(u64)XHCI_CMDRING; for(int i=0;i<256*4;i++)cr[i]=0; int li=255*4; cr[li]=XHCI_CMDRING; cr[li+1]=0; cr[li+2]=0; cr[li+3]=(6u<<10)|(1u<<1)|1u; }
    XMW(op+0x18,XHCI_CMDRING|1u); XMW(op+0x1C,0);
    { volatile u32* er=(volatile u32*)(u64)XHCI_EVTRING; for(int i=0;i<256*4;i++)er[i]=0; }
    { volatile u32* erst=(volatile u32*)(u64)XHCI_ERST; erst[0]=XHCI_EVTRING; erst[1]=0; erst[2]=256; erst[3]=0; }
    { u64 rt=base+rtsoff+0x20; XMW(rt+0x08,1); XMW(rt+0x18,XHCI_EVTRING); XMW(rt+0x1C,0); XMW(rt+0x10,XHCI_ERST); XMW(rt+0x14,0); }
    *usbcmd |= 1u; for(int t=0;t<200000;t++){ if(!(*usbsts&1u))break; xdelay(1); }
    for(int p=0;p<maxports;p++){ u64 pa=op+0x400+(u32)p*0x10; u32 v=XMR(pa); if(!(v&(1u<<9))){ XMW(pa,(v&0x0E00C3E0u)|(1u<<9)); } }
    for(int t=0;t<40000;t++) xdelay(1);
    for(int p=0;p<maxports;p++){ u32 v=XMR(op+0x400+(u32)p*0x10); xhci_port_conn[p]=(u8)(v&1); xhci_port_speed[p]=(u8)((v>>10)&0xF); }
    xhci_op=op; xhci_init_ok=rok;
}
/* mouse state (defined later, near the PS/2 handler) */

int g_cursor_moved=0;   /* pure pointer motion (cheap redraw, no full rebuild) */
/* keyboard input path (defined later): held-key array + char push */
 
    /* EC battery (defined near topbar) */
    /* battery (defined near topbar) */
/* ================= xHCI USB mouse: enumeration + HID + interrupt transfers =================
   Reasoned from the xHCI spec + Linux drivers/usb. Contexts/rings live at fixed identity-mapped
   low addresses (the controller DMAs to them; phys==virt here). Poll-based (no MSI). */
/* per-entry HID state: one entry per keyboard/mouse interface; region r holds contexts + rings (32 KB) */

#define XHID_BASE(r)     (DMW(0x80000u) + (u32)(r)*0x8000u)
#define XHID_INPUTCTX(r) (XHID_BASE(r))
#define XHID_DEVCTX(r)   (XHID_BASE(r)+0x1000u)
#define XHID_EP0RING(r)  (XHID_BASE(r)+0x2000u)
#define XHID_EPINRING(r) (XHID_BASE(r)+0x3000u)
#define XHID_DATABUF(r)  (XHID_BASE(r)+0x4000u)
static int xhci_csz=0; static u32 xhci_dboff=0, xhci_rtoff=0;
static u32 cmd_enq=0, cmd_cyc=1, ev_deq=0, ev_cyc=1;
static int cur_slot=0; u32 cur_ep0=0, cur_dbuf=0, e0_enq=0, e0_cyc=1;
int xhid_n=0, xhci_mouse_ok=0;
int xhid_used[XHID_MAX], xhid_slot[XHID_MAX], xhid_dci[XHID_MAX], xhid_rlen[XHID_MAX], xhid_iskbd[XHID_MAX], xhid_port[XHID_MAX];
static u32 xhid_epin[XHID_MAX], xhid_ein_enq[XHID_MAX], xhid_ein_cyc[XHID_MAX], xhid_dbuf[XHID_MAX];
static u8 xhid_kprev[XHID_MAX][8], xhid_kprevx[XHID_MAX][HIDLAY_MAX-1][8]; static u16 xhid_ccp[XHID_MAX][16]; static int xhid_ccn[XHID_MAX];
int xhid_nlay[XHID_MAX], xhid_rep[XHID_MAX], xhid_hasid[XHID_MAX], xhid_ctl[XHID_MAX];
typedef struct { u8 sl, ep, cc, ctl; u32 res, ptr; } xhq_t; static xhq_t xh_q[32]; static int xh_qn=0, xh_want_slot=-1, xh_want_ep=-1;
static u32 xh_want_ptr[3]; static int xh_want_pn=0; static u32 xh_want_cmd=0; u32 xh_q_drop=0, xh_stale_ev=0, xh_bo_skips=0;
static u8 xh_bo_n[XHC_MAX][256]; static u32 xh_bo_last[XHC_MAX][256], xh_bo_hold[XHC_MAX][256];
#define XH_T_CMD 10
#define XH_T_FIX 5
#define XH_T_CTRL 15
#define XH_T_BULK 40
#define XH_IT_CMD 200000
#define XH_IT_BULK 400000
static void hid_xfer(u8 sl,int epid,u8 cc,u32 res,u32 ptr);
static int xh_wdepth; static u8 xhid_recpend[XHID_MAX]; static u64 xhid_rect[XHID_MAX]; static u8 xhid_recn[XHID_MAX]; static u64 xhid_hold[XHID_MAX]; static int xhid_rbudget=-1; static volatile int xh_poll_busy; static volatile u64 xh_poll_t0;
u32 xhid_cnt[XHID_MAX]; u8 xhid_last[XHID_MAX][16]; int xhid_lastn[XHID_MAX];
xhdiag_t xhid_diag[XHID_MAX]; static u8 xhd_rd[6][512]; static u16 xhd_rdn[6], xhd_rdl[6];
static void xhid_note_fmt(int e,const volatile u8* b,int len);
static void xhid_btn_track(void);
static void xhid_diag_rx(int e,volatile u8* b,int got,int dx,int dy){ xhdiag_t* D=&xhid_diag[e]; xhid_btn_track(); if(xhid_rep[e])xhid_note_fmt(e,b,got); int s=(int)(D->rxi&3u), n=got<64?got:64; for(int i=0;i<n;i++)D->rx[s][i]=b[i]; D->rxn[s]=(u16)got; D->rdx[s]=(short)dx; D->rdy[s]=(short)dy; if(dx||dy){ D->ldx=dx; D->ldy=dy; } D->rxi++; }
static int xh_ctrl(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in,u8* data);
static void xhid_bo_reset(void){ if(cur_slot>0&&cur_slot<256&&g_xcur>=0&&g_xcur<XHC_MAX){ xh_bo_n[g_xcur][cur_slot]=0; xh_bo_hold[g_xcur][cur_slot]=0; } }
static u8 xhid_own[XHID_MAX], xhid_ppend[XHID_MAX]; static u64 xhid_pnext[XHID_MAX]; static u32 xhid_e0enq[XHID_MAX], xhid_e0cyc[XHID_MAX]; static u8 xhid_haud[XHID_MAX];
static u8 xhid_mid[XHID_MAX], xhid_mneed[XHID_MAX]; static u32 xhid_midm[XHID_MAX][8];
static u8 xhid_pdead[XHID_MAX], xhid_pused[XHID_MAX];
int g_usbproto_off, g_usbproto_late_off=1; static int xh_ctrl_to; static int xhid_btn_s; static u64 xhid_btn_t;
#define XHID_BTN_QUIET 50
int xhci_usbproto(int on){ if(on==0||on==1){ g_usbproto_off=!on; g_usbproto_late_off=!on; klog(on?"usb: late SET_PROTOCOL/SET_IDLE retries on":"usb: late SET_PROTOCOL/SET_IDLE retries off"); if(!on) for(int e=0;e<XHID_MAX;e++)xhid_ppend[e]=0; } return !g_usbproto_late_off; }
static int xh_irq_on(void){ u64 f=cpu_irq_save(); if(f&0x200)cpu_irq_on(); return (int)((f>>9)&1u); }
static void xhid_btn_track(void){ int s=(mouse_btn?1:0)|(mouse_rbtn?2:0)|(mouse_mid?4:0); if(s!=xhid_btn_s){ xhid_btn_s=s; xhid_btn_t=g_ticks; } }
void xhid_note_boot(int e){
    if(e<0||e>=XHID_MAX||!xhid_used[e]||!xhid_rep[e])return;
    xhdiag_t* D=&xhid_diag[e]; D->pfmt=1; if(D->nboot<0xFFFFu)D->nboot++;
    int d=xhid_own[e]; if(g_usbproto_late_off||xhid_ppend[e]||D->ptry>=1||!xhid_mneed[e]||d>=XHID_MAX||xhid_pdead[d]||xhid_pused[d]||(D->pset!=1&&D->pset!=3))return;
    xhid_ppend[e]=1; if(xhid_pnext[e]<g_ticks+XHID_BTN_QUIET)xhid_pnext[e]=g_ticks+XHID_BTN_QUIET; }
static void xhid_note_fmt(int e,const volatile u8* b,int len){
    int need=xhid_mneed[e]; if(!need||len<1)return; int hid=xhid_hasid[e], id=hid?b[0]:0;
    if(hid&&id!=xhid_mid[e]&&((xhid_midm[e][(id>>5)&7]>>(id&31))&1))return;
    if(len>=need){ xhid_diag[e].pfmt=2; return; }
    if(len==3||len==4)xhid_note_boot(e); }
static void xhid_proto_poll(void){
    extern int g_pi_depth __attribute__((weak));
    xhid_btn_track();
    if(g_usbproto_late_off||xh_wdepth>0||(&g_pi_depth&&g_pi_depth>0)||!xh_irq_on())return;
    if(xhid_btn_s||g_ticks-xhid_btn_t<XHID_BTN_QUIET||g_ticks-mouse_press_t<XHID_BTN_QUIET)return;
    for(int e=0;e<xhid_n;e++){ if(!xhid_ppend[e]||!xhid_used[e]||xhid_ctl[e]!=g_xcur||g_ticks<xhid_pnext[e])continue;
        xhdiag_t* D=&xhid_diag[e]; int d=xhid_own[e]; xhid_ppend[e]=0; if(D->ptry>=1||d<0||d>=XHID_MAX||xhid_haud[d]||xhid_pdead[d]||xhid_pused[d])continue;
        xhid_pused[d]=1; D->ptry=1;
        int s_slot=cur_slot; u32 s_ep0=cur_ep0, s_db=cur_dbuf, s_enq=e0_enq, s_cyc=e0_cyc;
        cur_slot=xhid_slot[e]; cur_ep0=XHID_EP0RING(d); cur_dbuf=XHID_DATABUF(d); e0_enq=xhid_e0enq[d]; e0_cyc=xhid_e0cyc[d];
        int ok=xh_ctrl(0x21,0x0B,1,(u16)D->iface,0,0,0), to=xh_ctrl_to;
        xhid_e0enq[d]=e0_enq; xhid_e0cyc[d]=e0_cyc; cur_slot=s_slot; cur_ep0=s_ep0; cur_dbuf=s_db; e0_enq=s_enq; e0_cyc=s_cyc;
        if(!ok)xhid_pdead[d]=1;
        klog(ok?"usb: asked receiver for full reports (1 retry)":to==1?"usb: asked receiver for full reports (timeout, giving up)":to==2?"usb: asked receiver for full reports (EP0 on hold, giving up)":"usb: asked receiver for full reports (refused, giving up)");
        return; }
}
typedef struct { int root, depth, hub, hport, speed; u32 route; int tt_slot, tt_port; } usbloc_t;
static int usn_xfer(u8 sl,int ep,u8 cc,u32 res,u32 ptr);
static int btu_xfer(u8 sl,int ep,u8 cc,u32 res,u32 ptr);
static int btu_active(void);
static int btu_owns(int c,int p);
static void btu_remove_below(int root,u32 R,int dR);
static void usn_remove_below(int root,u32 R,int dR);
static int usn_enum_loc(const usbloc_t* L);
static int usn_enum_root(int port);
static int usn_active(void);
static void usn_noslot(void);
static int aud_incam_next=0; static int aud_reg_hold=-1; static void uvc_add_mic(const usbloc_t* L,int d,const u8* big,int tw,u8 cfgval,u16 vid,u16 pid,const char* nm); static int aud_has_audio(const u8* c,int n); static int aud_attach(const usbloc_t* L,int d,const u8* full,int wtot,u8 cfgval,u16 vid,u16 pid,const char* nm); static void aud_reset(void); static void aud_unplug(int port); static void aud_sync_e0(void); static void aud_mark_sync(void);
static int uvc_reg_hold=-1; static int uvc_attach(const usbloc_t* L,int d,const u8* full,int wtot,u8 cfgval,u16 vid,u16 pid,const char* nm); static int uvc_has_video(const u8* c,int n); static void uvc_reset(void); static void uvc_unplug(int port);

xhub_t xhub[XHUB_MAX]; static int xreg_hub[XHID_MAX]; u32 xhid_route[XHID_MAX]; int xhid_depth[XHID_MAX];
static int xhub_attach(const usbloc_t* L,int d,const u8* full,int wtot,u8 cfgval);
static void xhid_reset(void){ { extern void usn_reset_all(void); usn_reset_all(); extern void btu_reset_all(void); btu_reset_all(); } uvc_reset(); aud_reset(); for(int h=0;h<XHUB_MAX;h++)xhub[h].used=0; for(int i=0;i<XHID_MAX;i++){ xreg_hub[i]=0; xhid_route[i]=0; xhid_depth[i]=0; xhid_cnt[i]=0; xhid_lastn[i]=0; xhid_rep[i]=0; xhid_nlay[i]=0; xhid_used[i]=0; xhid_slot[i]=0; for(int k=0;k<8;k++)xhid_kprev[i][k]=0; xhid_ccn[i]=0; } xhid_n=0; xhci_mouse_ok=0; }
typedef struct { u8 iface, proto, sub, ep; u16 mps; u8 intv; u16 rdlen; } hidif_t;

hidlay_t xhid_lay[XHID_MAX][HIDLAY_MAX];
static int hid_inlen;
static int hid_parse(const u8* d,int n,hidlay_t* out,int max,int* hasid){
    u32 upage=0, rsize=0, rcount=0, rid=0; int lmin=0; u32 ulo[32], uhi[32]; u8 usz[32]; int nus=0; u32 umin=0, umax=0; int hasrange=0, pmin=0, pmax=0; u8 uminsz=0, umaxsz=0;
    u32 gst[4][5]; int gsp=0; int depth=0, appkind[8]={0}; int nl=0; *hasid=0; hid_inlen=0;
    int bitpos[256]; for(int i=0;i<256;i++)bitpos[i]=0; u32 idm[8]={0,0,0,0,0,0,0,0};
    for(int i=0;i<n;){
        u8 b=d[i]; if(b==0xFE){ if(i+2>=n)break; i+=3+d[i+1]; continue; }
        int sz=b&3; if(sz==3)sz=4; int type=(b>>2)&3, tag=(b>>4)&0xF; if(i+1+sz>n)break;
        u32 v=0; for(int k=0;k<sz;k++)v|=(u32)d[i+1+k]<<(8*k); i+=1+sz;
        if(type==1){ if(tag==1)lmin=(sz==1)?(int)(signed char)v:(sz==2)?(int)(short)v:(int)v; if(tag==0)upage=v; else if(tag==7)rsize=v; else if(tag==8){ rid=v&0xFF; *hasid=1; } else if(tag==9)rcount=v;
            else if(tag==10&&gsp<4){ gst[gsp][0]=upage; gst[gsp][1]=rsize; gst[gsp][2]=rcount; gst[gsp][3]=rid; gst[gsp][4]=(u32)lmin; gsp++; } else if(tag==11&&gsp>0){ gsp--; upage=gst[gsp][0]; rsize=gst[gsp][1]; rcount=gst[gsp][2]; rid=gst[gsp][3]; lmin=(int)gst[gsp][4]; } }
        else if(type==2){ u32 u=(sz==4)?v:((upage<<16)|v);
            if(tag==0){ if(nus<32){ ulo[nus]=uhi[nus]=u; usz[nus]=(u8)sz; nus++; } }
            else if(tag==1||tag==2){ if(tag==1){ umin=u; uminsz=(u8)sz; pmin=1; } else { umax=u; umaxsz=(u8)sz; pmax=1; } hasrange=1;
                if(pmin&&pmax){ if(nus<32&&umax>=umin){ ulo[nus]=umin; uhi[nus]=umax; usz[nus]=(u8)(uminsz>umaxsz?uminsz:umaxsz); nus++; } pmin=pmax=0; } } }
        else if(type==0){
            for(int j=nus-1;j>=0;j--){ if(usz[j]>2)continue; if((ulo[j]>>16)==upage)break; ulo[j]=(upage<<16)|(ulo[j]&0xFFFF); uhi[j]=(upage<<16)|(uhi[j]&0xFFFF); }
            if(tag==10){ int kind=depth>0?appkind[depth-1]:0; if(v==1){ u32 u=nus?ulo[0]:0; kind=(u==0x10002u||u==0x10001u)?2:(u==0x10006u||u==0x10007u)?1:(u==0xC0001u)?3:0; } if(depth<7){ depth++; appkind[depth-1]=kind; } }
            else if(tag==12){ if(depth>0)depth--; }
            else if(tag==8){
                int kind=depth>0?appkind[depth-1]:0; int flags=(int)v; int pos=bitpos[rid]; idm[(rid>>5)&7]|=1u<<(rid&31); if(rsize>32||rcount>1024||pos>8192)kind=0;
                if(kind&&!(flags&1)){
                    hidlay_t* L=0; for(int k=0;k<nl;k++) if(out[k].id==rid&&out[k].kind==kind)L=&out[k];
                    if(!L&&nl<max){ L=&out[nl++]; for(int k=0;k<(int)(sizeof(hidlay_t)/sizeof(short));k++)((short*)L)[k]=-1; L->id=(u8)rid; L->kind=(u8)kind; L->btn_cnt=0; L->arr_cnt=0; L->bm_cnt=0; L->cc_cnt=0; L->cv_cnt=0; }
                    if(L){ int var=(flags&2)!=0;
                        if(var){ u32 rcl=rcount>256?256:rcount; for(u32 c=0;c<rcl;c++){ u32 u=0, cc=c; int j=0; for(;j<nus;j++){ u32 w=uhi[j]-ulo[j]+1; if(cc<w){ u=ulo[j]+cc; break; } cc-=w; } if(j==nus&&nus&&ulo[nus-1]==uhi[nus-1])u=ulo[nus-1];
                                int at=pos+(int)(c*rsize); u32 pg=u>>16, us=u&0xFFFF;
                                if(pg==9&&rsize==1){ if(L->btn_cnt==0)L->btn_off=(short)at; if(L->btn_cnt<16)L->btn_cnt++; }
                                else if(pg==1&&us==0x30){ if(L->x_off<0){ L->x_off=(short)at; L->x_sz=(short)rsize; } }
                                else if(pg==1&&us==0x31){ if(L->y_off<0){ L->y_off=(short)at; L->y_sz=(short)rsize; } }
                                else if(pg==1&&us==0x38){ if(L->w_off<0){ L->w_off=(short)at; L->w_sz=(short)rsize; } }
                                else if(pg==7&&rsize==1&&us>=0xE0&&us<=0xE7){ if(us==0xE0)L->mod_off=(short)at; }
                                else if(kind==3&&pg==0x0C&&rsize==1&&us&&L->cv_cnt<12){ L->cv_us[L->cv_cnt]=(u16)us; L->cv_off[L->cv_cnt]=(short)at; L->cv_cnt++; }
                                else if(pg==7&&rsize==1&&us<0xE0){ if(L->bm_cnt==0){ L->bm_off=(short)at; L->bm_min=(short)us; } L->bm_cnt++; } } }
                        else if(kind==3&&L->cc_cnt==0&&rsize>=8&&rsize<=16){ L->cc_off=(short)pos; L->cc_sz=(short)rsize; L->cc_cnt=(short)(rcount>4?4:rcount); L->cc_base=(short)((hasrange?(int)(umin&0xFFFF):0)-lmin); }
                        else if((upage==7||(hasrange&&(umin>>16)==7))&&rsize==8&&L->arr_cnt==0){ L->arr_off=(short)pos; L->arr_cnt=(short)(rcount>6?6:rcount); } }
                }
                { long np=(long)pos+(long)rsize*(long)rcount; bitpos[rid]=np>16384?16384:(int)np; }
            }
            nus=0; hasrange=0; pmin=pmax=0; umin=umax=0;
        }
    }
    int k=0; for(int j=0;j<nl;j++){ hidlay_t* L=&out[j]; int ok=(L->kind==2)?(L->x_off>=0&&L->y_off>=0):(L->kind==1)?(L->arr_cnt>0||L->bm_cnt>0):(L->kind==3)?(L->cc_cnt>0||L->cv_cnt>0):0; if(ok){ out[k]=*L; for(int w=0;w<8;w++)out[k].idm[w]=idm[w]; out[k].bseen=0; out[k].nil=0;
        if(*hasid) for(int r=1;r<256&&out[k].nil<8;r++) if((idm[r>>5]>>(r&31))&1){ int b=1+(bitpos[r]+7)/8; out[k].iid[out[k].nil]=(u8)r; out[k].ilen[out[k].nil]=(u8)(b>255?255:b); out[k].nil++; }
        k++; } }
    for(int j=0;j<k;j++){ int r=out[j].id; int b=(bitpos[r]+7)/8+(*hasid?1:0); if(bitpos[r]&&b>hid_inlen)hid_inlen=b; }
    return k;
}
/* DEBUG usbsim biltema: make the first relative USB mouse look like the Biltema 248A:16BB receiver (default off) */
int xhid_sim_e=-1;
static const u8 xhid_sim_rd[]={0x05,0x01,0x09,0x02,0xA1,0x01,0x85,0x01,0x09,0x01,0xA1,0x00,0x05,0x09,0x19,0x01,
 0x29,0x05,0x15,0x00,0x25,0x01,0x95,0x05,0x75,0x01,0x81,0x02,0x95,0x01,0x75,0x03,0x81,0x01,0x05,0x01,0x09,0x30,0x09,0x31,0x15,0x81,0x25,0x7F,0x75,0x08,0x95,0x02,
 0x81,0x06,0x09,0x38,0x15,0x81,0x25,0x7F,0x75,0x08,0x95,0x01,0x81,0x06,0xC0,0xC0,0x05,0x0C,0x09,0x01,0xA1,0x01,0x85,0x03,0x75,0x10,0x95,0x02,0x15,0x01,0x26,0x8C,
 0x02,0x19,0x01,0x2A,0x8C,0x02,0x81,0x00,0xC0,0x05,0x01,0x09,0x80,0xA1,0x01,0x85,0x04,0x75,0x02,0x95,0x01,0x15,0x01,0x25,0x03,0x09,0x82,0x09,0x81,0x09,0x83,0x81,
 0x60,0x75,0x06,0x81,0x03,0xC0,0x05,0x01,0x09,0x00,0xA1,0x01,0x85,0x05,0x06,0x00,0xFF,0x09,0x01,0x15,0x81,0x25,0x7F,0x75,0x08,0x95,0x07,0xB1,0x02,0xC0};
int xhid_sim_biltema(void){
    int e=-1; for(int i=0;i<xhid_n&&e<0;i++) if(xhid_used[i]&&!xhid_iskbd[i]&&xhid_diag[i].rdlen==52)e=i;
    if(e<0) return -1;
    hidlay_t L[HIDLAY_MAX]; int hid=0; int nl=hid_parse(xhid_sim_rd,(int)sizeof(xhid_sim_rd),L,HIDLAY_MAX,&hid); if(nl<=0) return -2;
    int mi=-1; for(int q=0;q<nl;q++) if(L[q].kind==2){ mi=q; break; } if(mi<0) return -3;
    hidlay_t* M=&L[mi]; int bits=M->x_off+M->x_sz; if(M->y_off+M->y_sz>bits)bits=M->y_off+M->y_sz; if(M->w_off>=0&&M->w_off+M->w_sz>bits)bits=M->w_off+M->w_sz;
    u64 f=cpu_irq_save(); xhid_nlay[e]=0; for(int q=0;q<nl;q++)xhid_lay[e][q]=L[q]; xhid_hasid[e]=hid; xhid_rep[e]=1; xhid_nlay[e]=nl;
    xhid_mid[e]=M->id; xhid_mneed[e]=(u8)((bits+7)/8+(hid?1:0)); for(int z=0;z<8;z++)xhid_midm[e][z]=M->idm[z];
    xhdiag_t* D=&xhid_diag[e]; D->vid=0x248A; D->pid=0x16BB; D->pfmt=0; D->ptry=0; D->nboot=0; D->pset=1; D->rdlen=(u16)sizeof(xhid_sim_rd); D->rdn=D->rdlen; for(int q=0;q<D->rdn;q++)D->rd[q]=xhid_sim_rd[q];
    xhid_ppend[e]=0; xhid_sim_e=e; if(f&0x200)cpu_irq_on(); return e;
}
static int hid_bits(const u8* b,int len,int off,int sz,int sgn){ if(off<0||sz<=0||sz>32||off+sz>len*8)return 0; u32 v=0; for(int i=0;i<sz;i++){ int p=off+i; if(b[p>>3]&(1<<(p&7)))v|=1u<<i; } if(sgn&&sz<32&&(v&(1u<<(sz-1))))v|=~0u<<sz; return (int)v; }
static int hid_pick(const u8* f,int len,hidif_t* out,int max){
    int n=0, cur=-1;
    for(int i=0;i+1<len;){ int bl=f[i], bt=f[i+1]; if(bl<2||i+bl>len)break;
        if(bt==4&&bl>=9){ cur=-1; if(f[i+3]==0&&f[i+5]==3&&n<max){ cur=n; out[n].iface=f[i+2]; out[n].proto=f[i+7]; out[n].sub=f[i+6]; out[n].ep=0; out[n].mps=0; out[n].intv=0; out[n].rdlen=0; n++; } }
        else if(bt==0x21&&bl>=9&&cur>=0&&f[i+6]==0x22){ out[cur].rdlen=(u16)(f[i+7]|(f[i+8]<<8)); }
        else if(bt==5&&bl>=7&&cur>=0&&!out[cur].ep){ u8 a=f[i+2]; if((a&0x80)&&(f[i+3]&3)==3){ out[cur].ep=a; out[cur].mps=(u16)((f[i+4]|(f[i+5]<<8))&0x7FF); out[cur].intv=f[i+6]; } }
        i+=bl; }
    int k=0; for(int j=0;j<n;j++) if(out[j].ep&&out[j].mps) out[k++]=out[j];
    return k;
}
static u32 xh_interval(int speed,int bint){
    if(bint<1)bint=1;
    if(speed>=3){ int v=bint-1; if(v>15)v=15; return (u32)v; }
    int v=3; while(v<10&&(1<<(v+1))<=bint*8)v++; return (u32)v;
}
char xhci_failstep[48]=""; int xhci_failcc=0;
static void xh_db(int slot,u32 target){ *(volatile u32*)(u64)(xhci_base+xhci_dboff+(u32)slot*4)=target; }
static int xh_hid_ep(u8 sl,u8 ep){ for(int i=0;i<xhid_n;i++) if(xhid_used[i]&&xhid_ctl[i]==g_xcur&&xhid_slot[i]==sl&&xhid_dci[i]==ep)return 1; return 0; }
static int xh_ptr_ok(u32 p){ if(!xh_want_pn||!p)return 1; for(int i=0;i<xh_want_pn;i++) if(xh_want_ptr[i]==p)return 1; return 0; }
static int xh_event_w_in(u32 want_type,u32* p0,u8* cc,u8* slot,u8* epid,u32 ticks,int iters);
static int xh_event_w(u32 want_type,u32* p0,u8* cc,u8* slot,u8* epid,u32 ticks,int iters){ xh_wdepth++; int r=xh_event_w_in(want_type,p0,cc,slot,epid,ticks,iters); xh_wdepth--; return r; }
static int xh_event_w_in(u32 want_type,u32* p0,u8* cc,u8* slot,u8* epid,u32 ticks,int iters){
    u64 t0=g_ticks;
    for(int t=0;t<iters;t++){ volatile u32* e=(volatile u32*)(u64)(XHCI_EVTRING+ev_deq*16);
        if((e[3]&1)==ev_cyc){ u32 ty=(e[3]>>10)&0x3F; u8 c=(u8)((e[2]>>24)&0xFF), sl=(u8)((e[3]>>24)&0xFF), ep=(u8)((e[3]>>16)&0x1F); u32 pp=e[0], rs=e[2]&0xFFFFFF;
            ev_deq++; if(ev_deq>=256){ev_deq=0;ev_cyc^=1;}
            u64 rt=xhci_base+xhci_rtoff+0x20; XMW(rt+0x18,(XHCI_EVTRING+ev_deq*16)|(1u<<3)); XMW(rt+0x1C,0);
            if(ty==32&&(want_type!=32||(xh_want_slot>=0&&(sl!=(u8)xh_want_slot||ep!=(u8)xh_want_ep)))){
                if(usn_xfer(sl,ep,c,rs,pp)||btu_xfer(sl,ep,c,rs,pp)){ if(g_ticks-t0>(u64)ticks)break; continue; }
                if(xh_hid_ep(sl,ep)){ hid_xfer(sl,ep,c,rs,pp); if(g_ticks-t0>(u64)ticks)break; continue; }
                if(xh_qn<32){ xh_q[xh_qn].ctl=(u8)g_xcur; xh_q[xh_qn].sl=sl; xh_q[xh_qn].ep=ep; xh_q[xh_qn].cc=c; xh_q[xh_qn].res=rs; xh_q[xh_qn].ptr=pp; xh_qn++; } else xh_q_drop++;
                if(g_ticks-t0>(u64)ticks)break;
                continue; }
            if(ty==32&&!xh_ptr_ok(pp)){ xh_stale_ev++; continue; }
            if(ty==33&&want_type==33&&xh_want_cmd&&pp&&pp!=xh_want_cmd){ xh_stale_ev++; continue; }
            if(ty==want_type){ if(p0)*p0=pp; if(cc)*cc=c; if(slot)*slot=sl; if(epid)*epid=ep; return 1; }
        } else { if(g_ticks-t0>(u64)ticks)break; io_wait(); }
    }
    return 0;
}
static int xh_event_t(u32 want_type,u32* p0,u8* cc,u8* slot,u8* epid){ return xh_event_w(want_type,p0,cc,slot,epid,XH_T_BULK,XH_IT_BULK); }
static void xh_bo_clear(int slot){ if(slot>0&&slot<256&&g_xcur>=0&&g_xcur<XHC_MAX){ xh_bo_n[g_xcur][slot]=0; xh_bo_hold[g_xcur][slot]=0; } }
static int xh_cmd_w(u32 p0,u32 p1,u32 p2,u32 type,u32 slotid,u8* ccode,u8* slotret,u32 ticks){
    u32 at=XHCI_CMDRING+cmd_enq*16; volatile u32* tr=(volatile u32*)(u64)at;
    tr[0]=p0; tr[1]=p1; tr[2]=p2; tr[3]=(type<<10)|((slotid&0xFF)<<24)|cmd_cyc;
    cmd_enq++; if(cmd_enq>=255){ volatile u32* lk=(volatile u32*)(u64)(XHCI_CMDRING+255*16); lk[3]=(lk[3]&~1u)|cmd_cyc; cmd_enq=0; cmd_cyc^=1; }
    xh_db(0,0);
    u8 cc=0,sl=0; xh_want_cmd=at; int ok=xh_event_w(33,0,&cc,&sl,0,ticks,XH_IT_CMD); xh_want_cmd=0;
    if(!ok)return 0; if(ccode)*ccode=cc; if(slotret)*slotret=sl;
    if((type&0x3F)==9&&cc==1)xh_bo_clear(sl);
    return cc==1;
}
static int xh_cmd_s(u32 p0,u32 p1,u32 p2,u32 type,u32 slotid,u8* ccode,u8* slotret){ return xh_cmd_w(p0,p1,p2,type,slotid,ccode,slotret,XH_T_CMD); }
static void xh_ep0_recover(u32 R,int stalled){
    u8 c2=0,s2=0; int ok;
    if(stalled) ok=xh_cmd_w(0,0,0,14u|(1u<<6),(u32)cur_slot,&c2,&s2,XH_T_FIX);
    else { ok=xh_cmd_w(0,0,0,15u|(1u<<6),(u32)cur_slot,&c2,&s2,XH_T_FIX);
        if(!ok&&c2==19) ok=xh_cmd_w(0,0,0,14u|(1u<<6),(u32)cur_slot,&c2,&s2,XH_T_FIX); }
    if(!ok&&c2!=19)return;
    xh_cmd_w((R+e0_enq*16)|e0_cyc,0,0,16u|(1u<<6),(u32)cur_slot,&c2,&s2,XH_T_FIX); }
static int xh_bo_held(void){ if(cur_slot<=0||cur_slot>255||g_xcur<0||g_xcur>=XHC_MAX)return 0; u32 h=xh_bo_hold[g_xcur][cur_slot]; if(h&&(int)(h-(u32)g_ticks)>0){ xh_bo_skips++; return 1; } if(h)xh_bo_hold[g_xcur][cur_slot]=0; return 0; }
static void xh_bo_note(int fail){ if(cur_slot<=0||cur_slot>255||g_xcur<0||g_xcur>=XHC_MAX)return; int c=g_xcur, s=cur_slot;
    if(!fail){ xh_bo_n[c][s]=0; return; }
    u32 now=(u32)g_ticks; if(now-xh_bo_last[c][s]>300u)xh_bo_n[c][s]=0; xh_bo_last[c][s]=now;
    if(++xh_bo_n[c][s]>=3){ xh_bo_n[c][s]=0; xh_bo_hold[c][s]=(now+500u)|1u; } }
/* control transfer on the CURRENT device's EP0 */
static int xh_ctrl(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in,u8* data){
    u32 R=cur_ep0, DB=cur_dbuf; volatile u32* r;
    xh_ctrl_to=0; if(xh_bo_held()){ xh_ctrl_to=2; return 0; }
    u32 tp[3]; int tn=0;
    tp[tn++]=R+e0_enq*16; r=(volatile u32*)(u64)(R+e0_enq*16);
    r[0]=(u32)rt|((u32)req<<8)|((u32)val<<16); r[1]=(u32)idx|((u32)len<<16);
    r[2]=8; r[3]=(2u<<10)|(1u<<6)|((len?(in?3u:2u):0u)<<16)|e0_cyc;
    e0_enq++; if(e0_enq>=255){volatile u32* lk=(volatile u32*)(u64)(R+255*16); lk[3]=(lk[3]&~1u)|e0_cyc; e0_enq=0; e0_cyc^=1;}
    if(len){ tp[tn++]=R+e0_enq*16; r=(volatile u32*)(u64)(R+e0_enq*16); r[0]=DB; r[1]=0; r[2]=len; r[3]=(3u<<10)|((in?1u:0u)<<16)|e0_cyc;
        e0_enq++; if(e0_enq>=255){volatile u32* lk=(volatile u32*)(u64)(R+255*16); lk[3]=(lk[3]&~1u)|e0_cyc; e0_enq=0; e0_cyc^=1;} }
    tp[tn++]=R+e0_enq*16; r=(volatile u32*)(u64)(R+e0_enq*16); r[0]=0;r[1]=0;r[2]=0; r[3]=(4u<<10)|(1u<<5)|(((len&&in)?0u:1u)<<16)|e0_cyc;
    e0_enq++; if(e0_enq>=255){volatile u32* lk=(volatile u32*)(u64)(R+255*16); lk[3]=(lk[3]&~1u)|e0_cyc; e0_enq=0; e0_cyc^=1;}
    xh_db(cur_slot,1);
    u8 cc=0; xh_want_slot=cur_slot; xh_want_ep=1; for(int i=0;i<tn;i++)xh_want_ptr[i]=tp[i]; xh_want_pn=tn;
    int ok=xh_event_w(32,0,&cc,0,0,XH_T_CTRL,XH_IT_CMD); xh_want_slot=-1; xh_want_ep=-1; xh_want_pn=0;
    if(!ok){ xh_ctrl_to=1; xh_ep0_recover(R,0); xh_bo_note(1); return 0; }
    xh_bo_note(0);
    if(cc==3||cc==4||cc==6){ xh_ep0_recover(R,1); return 0; }
    if(in&&data){ volatile u8* db=(volatile u8*)(u64)DB; for(int i=0;i<len;i++)data[i]=db[i]; }
    return cc==1||cc==13;
}
static u32 ctx_sz(void){ return xhci_csz?64u:32u; }
static void ctx_zero(u32 base,int n){ volatile u32* c=(volatile u32*)(u64)base; for(int i=0;i<n;i++)c[i]=0; }
/* enumerate one HID device on a port into the next free device index; detect kbd vs mouse */
static int xhid_free_region(int skip0,int skip1){ for(int r=0;r<XHID_MAX;r++) if(!xhid_used[r]&&!xreg_hub[r]&&r!=skip0&&r!=skip1&&r!=uvc_reg_hold&&r!=aud_reg_hold)return r; return -1; }
static void xh_disable_slot(int slot){ if(slot<=0)return; u8 cc=0,sl=0; xh_cmd_s(0,0,0,10,(u32)slot,&cc,&sl); *(volatile u32*)(u64)(XHCI_DCBAA+slot*8)=0; }
#define XHID_INFLIGHT 16
static u8 xhid_bs[64];
static void xhid_arm(int e){
    volatile u32* tr=(volatile u32*)(u64)(xhid_epin[e]+xhid_ein_enq[e]*16);
    u32 buf=xhid_dbuf[e]+(u32)(xhid_bs[e&63]&(XHID_INFLIGHT-1))*64u; xhid_bs[e&63]++;
    tr[0]=buf; tr[1]=0; tr[2]=(u32)xhid_rlen[e]&0x1FFFFu; tr[3]=(1u<<10)|(1u<<5)|(1u<<2)|xhid_ein_cyc[e];
    xhid_ein_enq[e]++; if(xhid_ein_enq[e]>=255){ volatile u32* lk=(volatile u32*)(u64)(xhid_epin[e]+255*16); lk[3]=(lk[3]&~1u)|xhid_ein_cyc[e]; xhid_ein_enq[e]=0; xhid_ein_cyc[e]^=1; }
    xh_db(xhid_slot[e],(u32)xhid_dci[e]);
}
static void xhid_recover_now(int e);
static void xhid_recover(int e){
    if(e<0||e>=XHID_MAX)return;
    if(xh_wdepth>0){ xhid_recpend[e]=1; return; }
    if(xhid_hold[e]&&g_ticks<xhid_hold[e]){ xhid_recpend[e]=1; return; }
    if(xhid_rbudget==0){ xhid_recpend[e]=1; return; }
    if(g_ticks-xhid_rect[e]<100){ if(xhid_recn[e]>=3){ xhid_recpend[e]=1; return; } } else xhid_recn[e]=0;
    if(xhid_rbudget>0)xhid_rbudget--;
    xhid_hold[e]=0; xhid_recn[e]++; xhid_rect[e]=g_ticks; xhid_recpend[e]=0; xhid_recover_now(e);
}
static void xhid_recover_now(int e){
    u8 c2=0,s2=0; u32 dci=(u32)xhid_dci[e]&0x1Fu;
    if(!xh_cmd_s(0,0,0,14u|(dci<<6),(u32)xhid_slot[e],&c2,&s2)){
        if(!c2){ xhid_hold[e]=g_ticks+1000; xhid_recpend[e]=1; return; }
        if(c2!=19)return;
        u32 dc=*(volatile u32*)(u64)(XHCI_DCBAA+(u32)xhid_slot[e]*8); if(!dc)return;
        u32 st=*(volatile u32*)(u64)(dc+ctx_sz()*dci)&7u;
        if(st==1){ c2=0; if(!xh_cmd_s(0,0,0,15u|(dci<<6),(u32)xhid_slot[e],&c2,&s2)){ if(!c2){ xhid_hold[e]=g_ticks+1000; xhid_recpend[e]=1; } return; } st=3; }
        if(st!=3&&st!=4)return; }
    c2=0; if(!xh_cmd_s((xhid_epin[e]+xhid_ein_enq[e]*16)|xhid_ein_cyc[e],0,0,16u|(dci<<6),(u32)xhid_slot[e],&c2,&s2)){ if(!c2){ xhid_hold[e]=g_ticks+1000; xhid_recpend[e]=1; } return; }
    for(int q=0;q<XHID_INFLIGHT;q++)xhid_arm(e);
}
static void xh_set_fail(const char* m){ int i=0; while(m[i]&&i<47){ xhci_failstep[i]=m[i]; i++; } xhci_failstep[i]=0; }
static int xh_root_reset(int port){
    xhci_csz=(XMR(xhci_base+0x10)>>2)&1; xhci_dboff=XMR(xhci_base+0x14)&~3u; xhci_rtoff=XMR(xhci_base+0x18)&~0x1Fu;
    u64 pa=xhci_op+0x400+(u32)port*0x10; u32 pv=XMR(pa);
    if(!(pv&1))return 0;
    XMW(pa,(pv&0x0E00C3E0u)|(1u<<4));
    for(int t=0;t<200000;t++){ if(XMR(pa)&(1u<<21))break; io_wait(); }
    for(volatile int dl=0;dl<50000;dl++) io_wait();
    pv=XMR(pa); int sp=(int)((pv>>10)&0xF); return sp?sp:1; }
static void xh_slot_ctx(volatile u32* sc,const usbloc_t* L,int entries){ sc[0]=((u32)entries<<27)|((u32)L->speed<<20)|(L->route&0xFFFFFu); sc[1]=(u32)(L->root+1)<<16; sc[2]=(u32)(L->tt_slot&0xFF)|((u32)(L->tt_port&0xFF)<<8); sc[3]=0; }
static int xh_fix_mps0(u32 inctx,const usbloc_t* L,u32 ring){
    if(L->speed!=1)return 1;
    if(!xh_ctrl(0x80,6,0x0100,0,8,1,0))return 0;
    int m=((volatile u8*)(u64)cur_dbuf)[7]; if(m!=16&&m!=32&&m!=64)return 1;
    ctx_zero(inctx,(int)ctx_sz()*3/4); volatile u32* ic=(volatile u32*)(u64)inctx; ic[1]=1u<<1;
    volatile u32* e0=(volatile u32*)(u64)(inctx+ctx_sz()*2); e0[1]=(4u<<3)|(3u<<1)|((u32)m<<16); e0[2]=ring|1u; e0[4]=8;
    u8 cc=0,sl=0; xh_cmd_s(inctx,0,0,13,(u32)cur_slot,&cc,&sl); return 1; }
static int xhci_enum_loc(const usbloc_t* L);
static int xhci_hid_enum(int port){
    xhci_failstep[0]=0; xhci_failcc=0;
    int sp=xh_root_reset(port); if(!sp){ xh_set_fail("no device on port"); return 0; }
    usbloc_t L; L.root=port; L.depth=0; L.hub=-1; L.hport=0; L.speed=sp; L.route=0; L.tt_slot=0; L.tt_port=0;
    int r=xhci_enum_loc(&L); aud_sync_e0(); return r; }
static int xhci_enum_loc(const usbloc_t* L){
    int d=xhid_free_region(-1,-1); if(d<0){ xh_set_fail("no free USB device memory"); return 0; }
    int slot_ok=0; xhci_failstep[0]=0; xhci_failcc=0;
    #define XFAIL(msg) do{ int _i=0; while(msg[_i]&&_i<47){xhci_failstep[_i]=msg[_i];_i++;} xhci_failstep[_i]=0; if(slot_ok)xh_disable_slot(cur_slot); return 0; }while(0)
    int port=L->root, speed=L->speed; int mps0=(speed==3)?64:(speed>=4)?512:8;
    cur_ep0=XHID_EP0RING(d); cur_dbuf=XHID_DATABUF(d); e0_enq=0; e0_cyc=1;
    { volatile u32* r0=(volatile u32*)(u64)XHID_EP0RING(d); for(int i=0;i<256*4;i++)r0[i]=0;
      int li=255*4; r0[li]=XHID_EP0RING(d); r0[li+1]=0; r0[li+2]=0; r0[li+3]=(6u<<10)|(1u<<1)|1u; }
    u8 cc=0,sl=0; if(!xh_cmd_s(0,0,0,9,0,&cc,&sl)){xhci_failcc=cc; XFAIL("Enable Slot");}
    cur_slot=sl; if(!cur_slot) XFAIL("no slot id"); slot_ok=1;
    ctx_zero(XHID_DEVCTX(d),(int)ctx_sz()*32/4);
    *(volatile u32*)(u64)(XHCI_DCBAA+cur_slot*8)=XHID_DEVCTX(d); *(volatile u32*)(u64)(XHCI_DCBAA+cur_slot*8+4)=0;
    ctx_zero(XHID_INPUTCTX(d),(int)ctx_sz()*3/4);
    volatile u32* ic=(volatile u32*)(u64)XHID_INPUTCTX(d); ic[1]=(1u<<0)|(1u<<1);
    volatile u32* sc=(volatile u32*)(u64)(XHID_INPUTCTX(d)+ctx_sz()); xh_slot_ctx(sc,L,1);
    volatile u32* e0=(volatile u32*)(u64)(XHID_INPUTCTX(d)+ctx_sz()*2); e0[1]=(4u<<3)|(3u<<1)|((u32)mps0<<16); e0[2]=XHID_EP0RING(d)|1u; e0[3]=0; e0[4]=8;
    if(!xh_cmd_s(XHID_INPUTCTX(d),0,0,11,(u32)cur_slot,&cc,&sl)){xhci_failcc=cc; XFAIL("Address Device");}
    for(volatile int dl=0;dl<20000;dl++) io_wait();
    if(!xh_fix_mps0(XHID_INPUTCTX(d),L,XHID_EP0RING(d))) XFAIL("GET device descriptor (8)");
    if(!xh_ctrl(0x80,6,0x0100,0,18,1,0)) XFAIL("GET device descriptor");
    if(!xh_ctrl(0x80,6,0x0200,0,9,1,0)) XFAIL("GET config(9)");
    u8 cfg9[9]; { volatile u8* db=(volatile u8*)(u64)cur_dbuf; for(int i=0;i<9;i++)cfg9[i]=db[i]; }
    int wtot=cfg9[2]|(cfg9[3]<<8); if(wtot>256)wtot=256; u8 cfgval=cfg9[5];
    if(!xh_ctrl(0x80,6,0x0200,0,(u16)wtot,1,0)) XFAIL("GET config(full)");
    u8 full[256]; { volatile u8* db=(volatile u8*)(u64)cur_dbuf; for(int i=0;i<wtot;i++)full[i]=db[i]; }
    for(int i=0;i+5<wtot;){ int bl=full[i]; if(bl<2)break; if(full[i+1]==4&&full[i+5]==9){ int r=xhub_attach(L,d,full,wtot,cfgval); if(r<0){ slot_ok=1; XFAIL("USB hub setup"); } return r; } i+=bl; }
    u16 cvid=0,cpid=0; char cnm[40]; cnm[0]=0;
    { u8 dd[18]; volatile u8* db0=(volatile u8*)(u64)cur_dbuf; (void)db0; u16 vid=0,pid=0; u8 ip=0;
      if(xh_ctrl(0x80,6,0x0100,0,18,1,0)){ for(int q=0;q<18;q++)dd[q]=((volatile u8*)(u64)cur_dbuf)[q]; vid=(u16)(dd[8]|(dd[9]<<8)); pid=(u16)(dd[10]|(dd[11]<<8)); ip=dd[15]; }
      char nm[40]; nm[0]=0;
      if(ip&&xh_ctrl(0x80,6,(u16)(0x0300|ip),0x0409,64,1,0)){ volatile u8* sb=(volatile u8*)(u64)cur_dbuf; int L=sb[0]; int o=0; for(int q=2;q+1<L&&q<64&&o<39;q+=2){ u8 c=sb[q]; if(sb[q+1]||c<32||c>126)c='?'; nm[o++]=(char)c; } nm[o]=0; }
      usbdev_note(L->root,vid,pid,full,wtot,nm); cvid=vid; cpid=pid; for(int q=0;q<40;q++)cnm[q]=nm[q]; }
    if(uvc_has_video(full,wtot)){ int tw=cfg9[2]|(cfg9[3]<<8); if(tw>16000)tw=16000; if(tw<9)tw=9;
        u8* big=(u8*)kmalloc((unsigned long)tw+16); if(!big) XFAIL("no memory for camera descriptors");
        if(!xh_ctrl(0x80,6,0x0200,0,(u16)tw,1,0)){ kfree(big); XFAIL("GET camera config"); }
        { volatile u8* db=(volatile u8*)(u64)cur_dbuf; for(int i=0;i<tw;i++)big[i]=db[i]; }
        int ok=uvc_attach(L,d,big,tw,cfgval,cvid,cpid,cnm); if(!ok){ kfree(big); XFAIL("camera setup"); }
        if(aud_has_audio(big,tw))uvc_add_mic(L,d,big,tw,cfgval,cvid,cpid,cnm);
        kfree(big); uvc_reg_hold=d; return 1; }
    int aok=0;
    if(aud_has_audio(full,wtot)){ int tw=cfg9[2]|(cfg9[3]<<8); if(tw>16000)tw=16000; if(tw<9)tw=9;
        u8* big=(u8*)kmalloc((unsigned long)tw+16);
        if(big&&xh_ctrl(0x80,6,0x0200,0,(u16)tw,1,0)){ volatile u8* db=(volatile u8*)(u64)cur_dbuf; for(int i=0;i<tw;i++)big[i]=db[i]; aok=aud_attach(L,d,big,tw,cfgval,cvid,cpid,cnm); }
        if(big)kfree(big); }
    hidif_t hf[6]; int nh=hid_pick(full,wtot,hf,6);
    if(!nh&&aok){ aud_reg_hold=d; return 1; }
    if(aok)aud_mark_sync();
    if(!nh) XFAIL("no keyboard or mouse interface");
    if(!xh_ctrl(0x00,9,cfgval,0,0,0,0)) XFAIL("SET_CONFIGURATION");
    hidlay_t lays[6][HIDLAY_MAX]; int nlays[6]={0,0,0,0,0,0}, hasid[6]={0,0,0,0,0,0}, mode[6]={0,0,0,0,0,0}, inl[6]={0,0,0,0,0,0}, pres[6]={0,0,0,0,0,0};
    { int k=0;
      for(int j=0;j<nh;j++){ int keep=0, m=0, nl=0, hid=0, il=0, bk=(hf[j].sub==1&&hf[j].proto==1);
        {
            xhd_rdn[k]=0; xhd_rdl[k]=0;
            if(hf[j].rdlen>0&&hf[j].rdlen<=1024){ volatile u8* z=(volatile u8*)(u64)cur_dbuf; for(int q=0;q<hf[j].rdlen;q++)z[q]=0; }
            if(hf[j].rdlen>0&&hf[j].rdlen<=1024&&xh_ctrl(0x81,6,0x2200,(u16)hf[j].iface,hf[j].rdlen,1,0)){
                u8 rd[1024]; volatile u8* db=(volatile u8*)(u64)cur_dbuf; for(int q=0;q<hf[j].rdlen;q++)rd[q]=db[q];
                nl=hid_parse(rd,hf[j].rdlen,lays[k],HIDLAY_MAX,&hid); il=hid_inlen; xhd_rdl[k]=hf[j].rdlen; xhd_rdn[k]=(u16)(hf[j].rdlen<512?hf[j].rdlen:512); for(int q=0;q<xhd_rdn[k];q++)xhd_rd[k][q]=rd[q]; }
            if(bk){ int ik=0; for(int q=0;q<nl;q++) if(lays[k][q].kind==1)ik=1; if(!hid||!ik){ nl=0; hid=0; il=0; } keep=1; m=nl>0; }
            else if(nl>0){ keep=1; m=1; } else if(hf[j].sub==1&&hf[j].proto==2){ keep=1; m=0; nl=0; il=0; } }
        if(keep){ if(k!=j)hf[k]=hf[j]; mode[k]=m; nlays[k]=nl; hasid[k]=hid; inl[k]=il; k++; } }
      nh=k; }
    int pdead=0, pused=0, pto[6]={0,0,0,0,0,0};
    for(int j=0;j<nh;j++){
        if(hf[j].sub==1&&!pdead){ pres[j]=xh_ctrl(0x21,0x0B,mode[j]?1:0,(u16)hf[j].iface,0,0,0)?1:2; pto[j]=xh_ctrl_to; if(xh_ctrl_to==1)pdead=1; }
        if(!pdead&&(!g_usbproto_off||hf[j].proto==1)){ xh_ctrl(0x21,0x0A,0,(u16)hf[j].iface,0,0,0); if(xh_ctrl_to==1)pdead=1; } }
    if(!nh) XFAIL("no keyboard or mouse interface");
    if(!g_usbproto_off&&!pdead) for(int j=0;j<nh;j++) if(pres[j]==2&&pto[j]==2){ pused=1; xhid_bo_reset(); pres[j]=xh_ctrl(0x21,0x0B,mode[j]?1:0,(u16)hf[j].iface,0,0,0)?3:4; if(xh_ctrl_to==1)pdead=1; break; }
    int reg[6]; reg[0]=d; int got=1;
    for(int j=1;j<nh;j++){ int r=-1; for(int c=0;c<XHID_MAX&&r<0;c++){ if(xhid_used[c]||xreg_hub[c]||c==uvc_reg_hold||c==aud_reg_hold)continue; int taken=0; for(int q=0;q<j;q++) if(reg[q]==c)taken=1; if(!taken)r=c; } if(r<0)break; reg[j]=r; got++; }
    nh=got;
    int maxdci=1; for(int j=0;j<nh;j++){ int dci=(hf[j].ep&0x0F)*2+1; if(dci>maxdci)maxdci=dci; }
    ctx_zero(XHID_INPUTCTX(d),(int)ctx_sz()*(maxdci+2)/4);
    ic=(volatile u32*)(u64)XHID_INPUTCTX(d); ic[1]=1u;
    sc=(volatile u32*)(u64)(XHID_INPUTCTX(d)+ctx_sz()); xh_slot_ctx(sc,L,maxdci);
    for(int j=0;j<nh;j++){ int r=reg[j]; int dci=(hf[j].ep&0x0F)*2+1; u32 mps=hf[j].mps; ic[1]|=(1u<<dci);
        volatile u32* er=(volatile u32*)(u64)XHID_EPINRING(r); for(int i=0;i<256*4;i++)er[i]=0;
        int li=255*4; er[li]=XHID_EPINRING(r); er[li+1]=0; er[li+2]=0; er[li+3]=(6u<<10)|(1u<<1)|1u;
        volatile u32* ep=(volatile u32*)(u64)(XHID_INPUTCTX(d)+ctx_sz()*(u32)(dci+1));
        ep[0]=xh_interval(speed,hf[j].intv)<<16; ep[1]=(7u<<3)|(3u<<1)|(mps<<16); ep[2]=XHID_EPINRING(r)|1u; ep[3]=0; ep[4]=mps|(mps<<16); }
    if(!xh_cmd_s(XHID_INPUTCTX(d),0,0,12,(u32)cur_slot,&cc,&sl)){xhci_failcc=cc; XFAIL("Configure Endpoint");}
    for(int j=0;j<nh;j++){ int e=reg[j]; int dci=(hf[j].ep&0x0F)*2+1; int rlen=hf[j].proto==1?8:(hf[j].mps<8?hf[j].mps:8); if(rlen<3)rlen=3;
        if(mode[j]){ rlen=hf[j].mps; if(inl[j]>rlen)rlen=inl[j]; if(rlen>64)rlen=64; if(rlen<1)rlen=8; }
        xhid_rep[e]=mode[j]; xhid_hasid[e]=hasid[j]; xhid_nlay[e]=nlays[j]; for(int q=0;q<nlays[j];q++)xhid_lay[e][q]=lays[j][q];
        xhid_ctl[e]=g_xcur; xhid_used[e]=1; xhid_recpend[e]=0; xhid_recn[e]=0; xhid_rect[e]=0; xhid_hold[e]=0; xhid_slot[e]=cur_slot; xhid_dci[e]=dci; xhid_rlen[e]=rlen; { int ik=0; for(int q=0;q<nlays[j];q++) if(lays[j][q].kind==1)ik=1; xhid_iskbd[e]=mode[j]?ik:(hf[j].proto==1); } xhid_ccn[e]=0; xhid_port[e]=port; xhid_route[e]=L->route; xhid_depth[e]=L->depth;
        xhid_epin[e]=XHID_EPINRING(e); xhid_dbuf[e]=XHID_DATABUF(e); xhid_ein_enq[e]=0; xhid_ein_cyc[e]=1; for(int k=0;k<8;k++){ xhid_kprev[e][k]=0; for(int z=0;z<HIDLAY_MAX-1;z++)xhid_kprevx[e][z][k]=0; }
        if(e+1>xhid_n)xhid_n=e+1;
        { xhdiag_t* D=&xhid_diag[e]; D->vid=cvid; D->pid=cpid; D->iface=hf[j].iface; D->ep=hf[j].ep; D->proto=hf[j].proto; D->sub=hf[j].sub; D->mps=hf[j].mps; D->rdlen=xhd_rdl[j]; D->rdn=xhd_rdn[j]; for(int q=0;q<D->rdn;q++)D->rd[q]=xhd_rd[j][q]; D->rxi=0; D->ldx=0; D->ldy=0; D->pset=(u8)pres[j]; D->pfmt=0; D->ptry=0; D->nboot=0; }
        xhid_own[e]=(u8)d; xhid_ppend[e]=0; xhid_pnext[e]=0; xhid_mneed[e]=0;
        for(int q=0;q<nlays[j];q++){ hidlay_t* M=&lays[j][q]; if(M->kind!=2||!mode[j])continue; int bits=M->x_off+M->x_sz; if(M->y_off+M->y_sz>bits)bits=M->y_off+M->y_sz; if(M->w_off>=0&&M->w_off+M->w_sz>bits)bits=M->w_off+M->w_sz;
            xhid_mid[e]=M->id; xhid_mneed[e]=(u8)((bits+7)/8+(hasid[j]?1:0)); for(int z=0;z<8;z++)xhid_midm[e][z]=M->idm[z]; break; }
        xhid_bs[e&63]=0; { volatile u8* z=(volatile u8*)(u64)XHID_DATABUF(e); for(int q=0;q<XHID_INFLIGHT*64;q++)z[q]=0; } for(int q=0;q<XHID_INFLIGHT;q++)xhid_arm(e); }
    xhid_e0enq[d]=e0_enq; xhid_e0cyc[d]=e0_cyc; xhid_haud[d]=(u8)(aok!=0); xhid_pdead[d]=(u8)pdead; xhid_pused[d]=(u8)pused; xhci_mouse_ok=1;
    #undef XFAIL
    return nh;
}
