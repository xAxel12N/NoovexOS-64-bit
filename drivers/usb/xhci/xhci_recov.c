extern int msd_port;
#define XRC_TRIES 3
#define XRC_WIN 1000u
#define XRC_EP_MAX 8
#define XRC_REENUM_MAX 3
#define XRC_REENUM_WIN 3000u
enum { XRC_DONE, XRC_RETRY, XRC_REENUM, XRC_GIVEUP };
typedef struct { u32 t0; u16 n; } xrc_win_t;
u32 xrc_fix_n, xrc_clear_n, xrc_retry_n, xrc_reenum_n, xrc_giveup_n, xrc_evrec_n, xrc_bounce_n;
static u32 xrc_rq[XHC_MAX], xrc_ev_seen;
static xrc_win_t xrc_rw[XHC_MAX][32], xrc_msd_w[32], xrc_hid_w[XHID_MAX];
static u8 xrc_hid_cc[XHID_MAX];
static u32 xrc_msd_e0q, xrc_msd_e0c=1;
#ifndef XRC_EPSTATE
#define XRC_EPSTATE(s,d) xrc_epstate(s,d)
static u32 xrc_epstate(int slot,int dci){ u32 dc=*(volatile u32*)(u64)(XHCI_DCBAA+(u32)slot*8); return dc?*(volatile u32*)(u64)(dc+ctx_sz()*(u32)dci)&7u:0xFFu; }
#endif
static int xrc_win_hit(xrc_win_t* w,u32 now,u32 win,int max){ if(!w->n||now-w->t0>win){ w->t0=now; w->n=0; } if(w->n<0xFFFF)w->n++; return w->n>max; }
static int xrc_plan(int cc,int eptype,int tries,xrc_win_t* w,u32 now){
    if(cc<0)return xrc_win_hit(w,now,XRC_WIN,XRC_EP_MAX)||tries>=1?XRC_REENUM:XRC_RETRY;
    int a=xhr_cc_action(cc,eptype,tries);
    if(a==XHR_OK||a==XHR_SKIP)return XRC_DONE;
    if(a==XHR_FAIL)return XRC_GIVEUP;
    if(xrc_win_hit(w,now,XRC_WIN,XRC_EP_MAX)||a==XHR_RESET_DEV||tries>=XRC_TRIES)return XRC_REENUM;
    return XRC_RETRY; }
static int xrc_reenum_req(int c,int p){
    if(c<0||c>=XHC_MAX||p<0||p>=32)return 0; if((xrc_rq[c]>>p)&1u)return 1;
    if(xrc_win_hit(&xrc_rw[c][p],(u32)g_ticks,XRC_REENUM_WIN,XRC_REENUM_MAX)){ xrc_giveup_n++; return 0; }
    xrc_rq[c]|=1u<<p; return 1; }
static int xrc_ep_fix(int slot,int dci,u32 ring,u32 enq,u32 cyc){
    u8 c2=0,s2=0; xrc_fix_n++; if(slot<=0||dci<2||dci>31)return 0;
    if(!xh_cmd_s(0,0,0,14u|((u32)dci<<6),(u32)slot,&c2,&s2)){
        if(c2!=19)return 0;
        u32 st=XRC_EPSTATE(slot,dci);
        if(st==1){ c2=0; if(!xh_cmd_s(0,0,0,15u|((u32)dci<<6),(u32)slot,&c2,&s2))return 0; }
        else if(st!=3)return 0; }
    c2=0; return xh_cmd_s((ring+enq*16u)|(cyc&1u),0,0,16u|((u32)dci<<6),(u32)slot,&c2,&s2); }
static int xrc_clear_halt(int slot,u32 ep0,u32* enq,u32* cyc,u32 dbuf,int epaddr){
    int os=cur_slot; u32 oe=cur_ep0, od=cur_dbuf, oq=e0_enq, oc=e0_cyc, live=oe==ep0;
    cur_slot=slot; cur_ep0=ep0; cur_dbuf=dbuf; if(!live){ e0_enq=*enq; e0_cyc=*cyc; }
    int ok=xh_ctrl(0x02,1,0,(u16)(epaddr&0xFF),0,0,0); xrc_clear_n++;
    *enq=e0_enq; *cyc=e0_cyc; if(live){ oq=e0_enq; oc=e0_cyc; }
    cur_slot=os; cur_ep0=oe; cur_dbuf=od; e0_enq=oq; e0_cyc=oc; return ok; }
static void xrc_msd_note(void){ xrc_msd_e0q=e0_enq; xrc_msd_e0c=e0_cyc; for(int i=0;i<32;i++)xrc_msd_w[i].n=0; }
static int xrc_msd_bulk(int dci,u32 ring,u32* enq,u32* cyc,u32 buf,int len,int phase){
    for(int t=0;t<=XRC_TRIES;t++){ int cc=xh_bulk(dci,ring,enq,cyc,buf,len);
        if(cc==1||cc==13)return cc;
        int a=xrc_plan(cc,(dci&1)?6:2,t,&xrc_msd_w[dci&31],(u32)g_ticks);
        if(a==XRC_DONE)return cc;
        if(a==XRC_GIVEUP){ xrc_giveup_n++; return -1; }
        int ok=xrc_ep_fix(msd_slot,dci,ring,*enq,*cyc)&&xrc_clear_halt(msd_slot,MSD_EP0RING,&xrc_msd_e0q,&xrc_msd_e0c,MSD_DATABUF,(dci&1)?0x80|(dci>>1):dci>>1);
        if(a==XRC_REENUM||!ok){ if(msd_depth==0&&msd_port>=0)xrc_reenum_req(g_xcur,msd_port); return -1; }
        if(phase==1&&cc==6)return cc;
        xrc_retry_n++; }
    return -1; }
static void xrc_hid_note(int e,u8 cc){
    if(e<0||e>=XHID_MAX)return; xrc_hid_cc[e]=cc?cc:4;
    if(xrc_plan(cc,7,0,&xrc_hid_w[e],(u32)g_ticks)==XRC_REENUM&&xhid_depth[e]==0)xrc_reenum_req(xhid_ctl[e],xhid_port[e]); }
static void xrc_hid_cleared(int e){
    if(e<0||e>=XHID_MAX||!xrc_hid_cc[e])return; xrc_hid_cc[e]=0; int d=xhid_own[e]; if(d>=XHID_MAX)return;
    xrc_clear_halt(xhid_slot[e],XHID_EP0RING(d),&xhid_e0enq[d],&xhid_e0cyc[d],XHID_DATABUF(d),0x80|(xhid_dci[e]>>1)); }
static void xrc_poll(void){
    u32 n=xhr_evfull_n+xhr_evlost_n; if(n==xrc_ev_seen)return;
    if(__sync_lock_test_and_set(&xh_poll_busy,1))return;
    xrc_ev_seen=n; xrc_evrec_n++;
    for(int k=0;k<4;k++)xhci_poll_one();
    for(int i=0;i<xhid_n;i++) if(xhid_used[i]&&xhid_ctl[i]==g_xcur)xhid_recpend[i]=1;
    __sync_lock_release(&xh_poll_busy);
    klog("usb: event ring overflowed, completions re-armed"); }
static int xrc_root_conn(int p,u32 v,int seen){
    int c=g_xcur; if(p<0||p>=32||c<0||c>=XHC_MAX)return (int)(v&1u);
    if((xrc_rq[c]>>p)&1u){ if(seen&&(v&1u))return 0; xrc_rq[c]&=~(1u<<p); if(v&1u){ xrc_reenum_n++; klog("usb: device stopped responding, port reset and enumerated again"); } }
    if(!(v&1u)||seen)return (int)(v&1u);
    u64 pa=xhci_op+0x400+(u32)p*0x10; XMW(pa,(v&0x0E00C3E0u)|(1u<<17));
    xh_delay_ms(XHR_DEBOUNCE_MS);
    u32 w=XMR(pa); if(!(w&1u)||(w&(1u<<17))){ xrc_bounce_n++; return 0; }
    return 1; }
