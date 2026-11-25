#include "drivers/net/wifi/mediatek/mt7921/usb/mt7921_usb.h"
#define MWU_BUF 0x2000u
static struct { int ok, ctl, slot, port, fails, dci[8], mps[8]; u32 mem, inctx, devctx, ep0, dbuf, ring[8], enq[8], cyc[8], e0_enq, e0_cyc, txb, rxb; u16 vid, pid; } mwu;
static struct mt7921u_hc mwu_hc;
static int mwu_mem(void){
    if(mwu.mem)return 1;
    u8* p=(u8*)dma_alloc_pages(12); u8* q=(u8*)dma_alloc_pages(4); if(!p||!q)return 0;
    mwu.mem=(u32)(u64)p; mwu.inctx=mwu.mem; mwu.devctx=mwu.mem+0x1000u; mwu.ep0=mwu.mem+0x2000u; mwu.dbuf=mwu.mem+0x3000u;
    for(int i=0;i<8;i++)mwu.ring[i]=mwu.mem+0x4000u+(u32)i*0x1000u;
    mwu.txb=(u32)(u64)q; mwu.rxb=mwu.txb+MWU_BUF; return 1;
}
static int mwu_gone(void){ if(!mwu.ok)return 1; u32 pv=XMR(xhci_op+0x400+(u32)mwu.port*0x10); if(!(pv&1)||mwu.fails>=20){ mwu.ok=0; mwu.slot=0; mt7921u_detach(); klog(mwu.fails>=20?"mt7921u: adapter stopped answering - released":"mt7921u: adapter removed"); return 1; } return 0; }
static void mwu_ep0_swap(int in){
    static int s_slot; static u32 s_ep0, s_db, s_enq, s_cyc;
    if(in){ s_slot=cur_slot; s_ep0=cur_ep0; s_db=cur_dbuf; s_enq=e0_enq; s_cyc=e0_cyc; cur_slot=mwu.slot; cur_ep0=mwu.ep0; cur_dbuf=mwu.dbuf; e0_enq=mwu.e0_enq; e0_cyc=mwu.e0_cyc; }
    else { mwu.e0_enq=e0_enq; mwu.e0_cyc=e0_cyc; cur_slot=s_slot; cur_ep0=s_ep0; cur_dbuf=s_db; e0_enq=s_enq; e0_cyc=s_cyc; }
}
static int mwu_ctrl(void* ctx,u8 rt,u8 req,u16 val,u16 idx,u8* buf,u16 len){
    (void)ctx; if(!mwu.ok||len>256||!btu_lock())return -1;
    int prev=g_xcur; xhc_select(mwu.ctl); int in=(rt&0x80)!=0;
    volatile u8* d=(volatile u8*)(u64)mwu.dbuf; for(int i=0;i<len;i++)d[i]=in?0:buf[i];
    mwu_ep0_swap(1); int r=xh_ctrl(rt,req,val,idx,len,in,0); mwu_ep0_swap(0);
    if(r&&in)for(int i=0;i<len;i++)buf[i]=d[i];
    if(r)mwu.fails=0; else mwu.fails++;
    xhc_select(prev); btu_unlock();
    if(!r)mwu_gone();
    return r?len:-1;
}
static void mwu_ep_fix(int ri,int halted){
    u8 cc=0,sl=0; int dci=mwu.dci[ri];
    xh_cmd_s(0,0,0,(halted?14u:15u)|((u32)dci<<6),(u32)mwu.slot,&cc,&sl);
    xh_cmd_s((mwu.ring[ri]+mwu.enq[ri]*16)|mwu.cyc[ri],0,0,16u|((u32)dci<<6),(u32)mwu.slot,&cc,&sl);
    if(halted){ mwu_ep0_swap(1); xh_ctrl(0x02,1,0,(u16)((dci>>1)|((dci&1)?0x80:0)),0,0,0); mwu_ep0_swap(0); }
}
static int mwu_td(int ri,u32 buf,int len,int ms){
    u32 ring=mwu.ring[ri]; int dci=mwu.dci[ri];
    volatile u32* tr=(volatile u32*)(u64)(ring+mwu.enq[ri]*16);
    tr[0]=buf; tr[1]=0; tr[2]=(u32)len; __sync_synchronize(); tr[3]=(1u<<10)|(1u<<5)|((dci&1)?(1u<<2):0)|mwu.cyc[ri];
    mwu.enq[ri]++; if(mwu.enq[ri]>=255){ volatile u32* lk=(volatile u32*)(u64)(ring+255*16); lk[3]=(lk[3]&~1u)|mwu.cyc[ri]; mwu.enq[ri]=0; mwu.cyc[ri]^=1; }
    __sync_synchronize(); xh_db(mwu.slot,(u32)dci);
    u8 cc=0; int ok=0; u64 t0=hr_ns(), lim=(u64)(ms<1?1:ms)*1000000ULL;
    xh_want_slot=mwu.slot; xh_want_ep=dci; xh_want_ptr[0]=(u32)(u64)tr; xh_want_pn=1;
    while(!ok&&hr_ns()-t0<lim)ok=xh_event_w(32,0,&cc,0,0,1,4000);
    xh_want_slot=-1; xh_want_ep=-1; xh_want_pn=0;
    if(!ok){ mwu_ep_fix(ri,0); return -2; }
    if(cc!=1&&cc!=13){ mwu_ep_fix(ri,cc==6); return -1; }
    u32 res=((volatile u32*)(u64)(XHCI_EVTRING+((ev_deq+255)&255)*16))[2]&0xFFFFFF;
    int got=len-(int)res; return got<0?0:got;
}
static int mwu_ri(u8 ep){ for(int i=0;i<8;i++) if(mwu.dci[i]&&mwu.dci[i]==((ep&0x0F)*2+((ep&0x80)?1:0)))return i; return -1; }
static int mwu_out(void* ctx,u8 ep,const u8* p,u32 n,u32 ms){
    (void)ctx; int ri=mwu_ri(ep); if(!mwu.ok||ri<0||!n||n>MWU_BUF||!btu_lock())return -1;
    int prev=g_xcur; xhc_select(mwu.ctl);
    volatile u8* d=(volatile u8*)(u64)mwu.txb; for(u32 i=0;i<n;i++)d[i]=p[i];
    int r=mwu_td(ri,mwu.txb,(int)n,(int)ms);
    if(r>=0&&mwu.mps[ri]&&!(n%(u32)mwu.mps[ri]))mwu_td(ri,mwu.txb,0,50);
    if(r<0)mwu.fails++; else mwu.fails=0;
    xhc_select(prev); btu_unlock();
    if(r<0)mwu_gone();
    return r<0?-1:(int)n;
}
static int mwu_in(void* ctx,u8 ep,u8* p,u32 cap,u32 ms){
    (void)ctx; int ri=mwu_ri(ep); if(!mwu.ok||ri<0||!cap||!btu_lock())return -1;
    if(cap>MWU_BUF)cap=MWU_BUF;
    int prev=g_xcur; xhc_select(mwu.ctl);
    int r=mwu_td(ri,mwu.rxb,(int)cap,(int)ms);
    if(r>0){ volatile u8* d=(volatile u8*)(u64)mwu.rxb; for(int i=0;i<r;i++)p[i]=d[i]; }
    if(r==-1)mwu.fails++; else if(r>=0)mwu.fails=0;
    xhc_select(prev); btu_unlock();
    if(r<0&&mwu_gone())return -1;
    return r<0?0:r;
}
#define MFAIL(m) do{ xh_set_fail(m); return 0; }while(0)
static int mwu_enum(const usbloc_t* L){
    if(!mwu_mem())return 0;
    xhci_failstep[0]=0; xhci_failcc=0; mwu.slot=0;
    int speed=L->speed; int mps0=(speed==3)?64:(speed>=4)?512:8;
    cur_ep0=mwu.ep0; cur_dbuf=mwu.dbuf; e0_enq=0; e0_cyc=1; ring_init(mwu.ep0);
    u8 cc=0,sl=0; if(!xh_cmd_s(0,0,0,9,0,&cc,&sl)||!sl){ xhci_failcc=cc; MFAIL("Enable Slot"); }
    cur_slot=sl; mwu.slot=sl;
    ctx_zero(mwu.devctx,(int)ctx_sz()*32/4); *(volatile u32*)(u64)(XHCI_DCBAA+sl*8)=mwu.devctx; *(volatile u32*)(u64)(XHCI_DCBAA+sl*8+4)=0;
    ctx_zero(mwu.inctx,(int)ctx_sz()*33/4); volatile u32* ic=(volatile u32*)(u64)mwu.inctx; ic[1]=3u;
    volatile u32* sc=(volatile u32*)(u64)(mwu.inctx+ctx_sz()); xh_slot_ctx(sc,L,1);
    volatile u32* e0=(volatile u32*)(u64)(mwu.inctx+ctx_sz()*2); e0[1]=(4u<<3)|(3u<<1)|((u32)mps0<<16); e0[2]=mwu.ep0|1u; e0[3]=0; e0[4]=8;
    if(!xh_cmd_s(mwu.inctx,0,0,11,(u32)sl,&cc,&sl)){ xhci_failcc=cc; MFAIL("Address Device"); }
    xh_delay_ms(10);
    if(!xh_fix_mps0(mwu.inctx,L,mwu.ep0))MFAIL("GET device descriptor (8)");
    if(!xh_ctrl(0x80,6,0x0100,0,18,1,0))MFAIL("GET device descriptor");
    volatile u8* db=(volatile u8*)(u64)mwu.dbuf;
    mwu.vid=(u16)(db[8]|(db[9]<<8)); mwu.pid=(u16)(db[10]|(db[11]<<8));
    if(!mt7921u_match(mwu.vid,mwu.pid))MFAIL("not an MT7921AU adapter");
    if(!xh_ctrl(0x80,6,0x0200,0,9,1,0))MFAIL("GET config(9)");
    int wt=db[2]|(db[3]<<8), cv=db[5]; if(wt<9)MFAIL("bad config"); if(wt>1024)wt=1024;
    if(!xh_ctrl(0x80,6,0x0200,0,(u16)wt,1,0))MFAIL("GET config(full)");
    static u8 full[1024]; for(int i=0;i<wt;i++)full[i]=db[i];
    for(int i=0;i<8;i++){ mwu.dci[i]=0; mwu.mps[i]=0; }
    if(mt7921u_parse_eps(full,wt,&mwu_hc,0)<0)MFAIL("no MT7921AU bulk endpoints");
    for(int k=0;k<MTU_NIN+MTU_NOUT;k++){ u8 a=k<MTU_NIN?mwu_hc.in_ep[k]:mwu_hc.out_ep[k-MTU_NIN]; mwu.dci[k]=(a&0x0F)*2+((a&0x80)?1:0);
        for(int i=0;i+2<=wt;){ int bl=full[i]; if(bl<2||i+bl>wt)break; if(full[i+1]==5&&bl>=7&&full[i+2]==a)mwu.mps[k]=(full[i+4]|(full[i+5]<<8))&0x7FF; i+=bl; } }
    if(!xh_ctrl(0x00,9,(u16)cv,0,0,0,0))MFAIL("SET_CONFIGURATION");
    ctx_zero(mwu.inctx,(int)ctx_sz()*33/4);
    ic=(volatile u32*)(u64)mwu.inctx; ic[1]=1u; int maxd=1;
    for(int k=0;k<8;k++){ ic[1]|=1u<<mwu.dci[k]; if(mwu.dci[k]>maxd)maxd=mwu.dci[k]; }
    sc=(volatile u32*)(u64)(mwu.inctx+ctx_sz()); xh_slot_ctx(sc,L,maxd);
    for(int k=0;k<8;k++){ int dci=mwu.dci[k], mps=mwu.mps[k]; if(!mps)mps=speed>=4?1024:512;
        ring_init(mwu.ring[k]); mwu.enq[k]=0; mwu.cyc[k]=1;
        volatile u32* ec=(volatile u32*)(u64)(mwu.inctx+ctx_sz()*(u32)(dci+1)); ec[0]=0; ec[1]=(u32)((dci&1)?6:2)<<3|(3u<<1)|((u32)mps<<16); ec[2]=mwu.ring[k]|1u; ec[3]=0; ec[4]=2048; }
    if(!xh_cmd_s(mwu.inctx,0,0,12,(u32)mwu.slot,&cc,&sl)){ xhci_failcc=cc; MFAIL("Configure Endpoint"); }
    mwu.e0_enq=e0_enq; mwu.e0_cyc=e0_cyc; mwu.ctl=g_xcur; mwu.port=L->root; mwu.fails=0; mwu.ok=1;
    return 1;
}
#undef MFAIL
int mt7921u_xhci_open(void){
    if(mwu.ok)return 1;
    if(!mt7921u_enable||!g_xhc_n||!btu_lock())return 0;
    int prev=g_xcur; int s_slot=cur_slot; u32 s_ep0=cur_ep0, s_db=cur_dbuf, s_enq=e0_enq, s_cyc=e0_cyc;
    for(int c=0;c<g_xhc_n&&!mwu.ok;c++){ xhc_select(c); if(!xhci_present||!xhci_init_ok)continue;
        for(int u=0;u<USBDEV_MAX&&!mwu.ok;u++){ usbdev_t* ud=&g_usbdev[u]; if(!ud->used||ud->ctl!=c||ud->hid||ud->cam||ud->prn||!mt7921u_match(ud->vid,ud->pid))continue;
            int p=ud->port; if(p<0||p>=xhci_ports||!xhci_port_conn[p]||btu_owns(c,p))continue;
            int hub=0; for(int h=0;h<XHUB_MAX;h++) if(xhub[h].used&&xhub[h].ctl==c&&xhub[h].root==p)hub=1; if(hub)continue;
            int sp=xh_root_reset(p); usbloc_t L; L.root=p; L.depth=0; L.hub=-1; L.hport=0; L.speed=sp; L.route=0; L.tt_slot=0; L.tt_port=0;
            if(!sp||!mwu_enum(&L)){ if(mwu.slot){ xh_disable_slot(mwu.slot); mwu.slot=0; } char b[96],*q=b; q=scopy(q,"mt7921u: adapter setup failed: "); q=scopy(q,xhci_failstep); *q=0; klog(b); }
            else usb_seen[p]=1; } }
    xhc_select(prev); cur_slot=s_slot; cur_ep0=s_ep0; cur_dbuf=s_db; e0_enq=s_enq; e0_cyc=s_cyc;
    btu_unlock();
    if(!mwu.ok)return 0;
    mwu_hc.ctx=0; mwu_hc.ctrl=mwu_ctrl; mwu_hc.bulk_out=mwu_out; mwu_hc.bulk_in=mwu_in;
    if(mt7921u_attach(&mwu_hc,mwu.vid,mwu.pid)<0){ mwu.ok=0; return 0; }
    { char b[96],*q=b; q=scopy(q,"mt7921u: USB adapter "); q=hx(q,mwu.vid,4); *q++=':'; q=hx(q,mwu.pid,4); q=scopy(q," claimed"); *q=0; klog(b); }
    return 1;
}
