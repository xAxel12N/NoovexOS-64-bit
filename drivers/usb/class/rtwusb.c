#include "include/nvx/drivers/net/wifi/rtw_usb.h"
static struct { int ok, ctl, slot, port, nout, speed, fails, idci, odci[4], imps, omps[4]; u32 mem, data, inctx, devctx, ep0, dbuf, ring[5], enq[5], cyc[5], e0_enq, e0_cyc, txb, rxb; u16 vid, pid; } rwu;
static int rwu_mem(void){
    if(rwu.mem)return 1;
    u8* p=(u8*)dma_alloc_pages(9); u8* q=(u8*)dma_alloc_pages(24); if(!p||!q)return 0;
    rwu.mem=(u32)(u64)p; rwu.inctx=rwu.mem; rwu.devctx=rwu.mem+0x1000u; rwu.ep0=rwu.mem+0x2000u; rwu.dbuf=rwu.mem+0x3000u;
    for(int i=0;i<5;i++)rwu.ring[i]=rwu.mem+0x4000u+(u32)i*0x1000u;
    u32 a=(u32)(u64)q, e=a+24u*4096u, b=(a+0xFFFFu)&~0xFFFFu; u32 s=(b>a&&b<e&&b-a<e-b)?b:a;
    rwu.data=s; rwu.txb=s; rwu.rxb=s+RTWU_TXBUF; return 1;
}
static int rwu_gone(void){ if(!rwu.ok)return 1; u32 pv=XMR(xhci_op+0x400+(u32)rwu.port*0x10); if(!(pv&1)||rwu.fails>=20){ rwu.ok=0; rwu.slot=0; rtwu_detach(); klog(rwu.fails>=20?"wifi usb: adapter stopped answering - released":"wifi usb: adapter removed"); return 1; } return 0; }
static void rwu_ep0_swap(int in){
    static int s_slot; static u32 s_ep0, s_db, s_enq, s_cyc;
    if(in){ s_slot=cur_slot; s_ep0=cur_ep0; s_db=cur_dbuf; s_enq=e0_enq; s_cyc=e0_cyc; cur_slot=rwu.slot; cur_ep0=rwu.ep0; cur_dbuf=rwu.dbuf; e0_enq=rwu.e0_enq; e0_cyc=rwu.e0_cyc; }
    else { rwu.e0_enq=e0_enq; rwu.e0_cyc=e0_cyc; cur_slot=s_slot; cur_ep0=s_ep0; cur_dbuf=s_db; e0_enq=s_enq; e0_cyc=s_cyc; }
}
static int rwu_vreq(u8 rt,u16 val,u16 idx,u8* buf,u16 len){
    if(!rwu.ok||len>254||!btu_lock())return -1;
    int prev=g_xcur; xhc_select(rwu.ctl); int in=(rt&0x80)!=0;
    volatile u8* d=(volatile u8*)(u64)rwu.dbuf; for(int i=0;i<len;i++)d[i]=in?0:buf[i];
    rwu_ep0_swap(1); int r=xh_ctrl(rt,0x05,val,idx,len,in,0); rwu_ep0_swap(0);
    if(r&&in)for(int i=0;i<len;i++)buf[i]=d[i];
    if(r)rwu.fails=0; else rwu.fails++;
    xhc_select(prev); btu_unlock();
    if(!r)rwu_gone();
    return r?len:-1;
}
static void rwu_ep_fix(int ri,int dci,int halted){
    u8 cc=0,sl=0;
    if(!halted)xh_cmd_s(0,0,0,15u|((u32)dci<<6),(u32)rwu.slot,&cc,&sl);
    else xh_cmd_s(0,0,0,14u|((u32)dci<<6),(u32)rwu.slot,&cc,&sl);
    xh_cmd_s((rwu.ring[ri]+rwu.enq[ri]*16)|rwu.cyc[ri],0,0,16u|((u32)dci<<6),(u32)rwu.slot,&cc,&sl);
    if(halted){ rwu_ep0_swap(1); xh_ctrl(0x02,1,0,(u16)((dci>>1)|((dci&1)?0x80:0)),0,0,0); rwu_ep0_swap(0); }
}
static int rwu_td(int ri,int dci,u32 buf,int len,int ms){
    u32 ring=rwu.ring[ri];
    volatile u32* tr=(volatile u32*)(u64)(ring+rwu.enq[ri]*16);
    tr[0]=buf; tr[1]=0; tr[2]=(u32)len; __sync_synchronize(); tr[3]=(1u<<10)|(1u<<5)|((dci&1)?(1u<<2):0)|rwu.cyc[ri];
    rwu.enq[ri]++; if(rwu.enq[ri]>=255){ volatile u32* lk=(volatile u32*)(u64)(ring+255*16); lk[3]=(lk[3]&~1u)|rwu.cyc[ri]; rwu.enq[ri]=0; rwu.cyc[ri]^=1; }
    __sync_synchronize(); xh_db(rwu.slot,(u32)dci);
    u8 cc=0; int ok=0; u64 t0=hr_ns(), lim=(u64)(ms<1?1:ms)*1000000ULL;
    xh_want_slot=rwu.slot; xh_want_ep=dci; xh_want_ptr[0]=(u32)(u64)tr; xh_want_pn=1;
    while(!ok&&hr_ns()-t0<lim)ok=xh_event_w(32,0,&cc,0,0,1,4000);
    xh_want_slot=-1; xh_want_ep=-1; xh_want_pn=0;
    if(!ok){ rwu_ep_fix(ri,dci,0); return -2; }
    if(cc!=1&&cc!=13){ rwu_ep_fix(ri,dci,cc==6); return -1; }
    u32 res=((volatile u32*)(u64)(XHCI_EVTRING+((ev_deq+255)&255)*16))[2]&0xFFFFFF;
    int got=len-(int)res; return got<0?0:got;
}
static int rwu_bulk_out(int ep,const u8* p,int n){
    if(!rwu.ok||ep<0||ep>=rwu.nout||n<1||n>RTWU_TXBUF||!btu_lock())return -1;
    int prev=g_xcur; xhc_select(rwu.ctl);
    volatile u8* d=(volatile u8*)(u64)rwu.txb; for(int i=0;i<n;i++)d[i]=p[i];
    int r=rwu_td(1+ep,rwu.odci[ep],rwu.txb,n,200);
    if(r>=0&&rwu.omps[ep]&&!(n%rwu.omps[ep]))rwu_td(1+ep,rwu.odci[ep],rwu.txb,0,50);
    if(r<0)rwu.fails++; else rwu.fails=0;
    xhc_select(prev); btu_unlock();
    if(r<0)rwu_gone();
    return r<0?-1:n;
}
static int rwu_bulk_in(u8* p,int cap,int ms){
    if(!rwu.ok||cap<1||!btu_lock())return -1;
    if(cap>RTWU_RXBUF)cap=RTWU_RXBUF;
    int prev=g_xcur; xhc_select(rwu.ctl);
    int r=rwu_td(0,rwu.idci,rwu.rxb,cap,ms);
    if(r>0){ volatile u8* d=(volatile u8*)(u64)rwu.rxb; for(int i=0;i<r;i++)p[i]=d[i]; }
    if(r==-1)rwu.fails++; else rwu.fails=0;
    xhc_select(prev); btu_unlock();
    if(r<0&&rwu_gone())return -1;
    return r<0?0:r;
}
static rtwu_lo rwu_lo={ rwu_vreq, rwu_bulk_out, rwu_bulk_in, 0, 0, 0, 0 };
#define RFAIL(m) do{ xh_set_fail(m); return 0; }while(0)
static int rwu_enum(const usbloc_t* L){
    if(!rwu_mem())return 0;
    xhci_failstep[0]=0; xhci_failcc=0; rwu.slot=0;
    int speed=L->speed; int mps0=(speed==3)?64:(speed>=4)?512:8;
    cur_ep0=rwu.ep0; cur_dbuf=rwu.dbuf; e0_enq=0; e0_cyc=1; ring_init(rwu.ep0);
    u8 cc=0,sl=0; if(!xh_cmd_s(0,0,0,9,0,&cc,&sl)||!sl){ xhci_failcc=cc; RFAIL("Enable Slot"); }
    cur_slot=sl; rwu.slot=sl;
    ctx_zero(rwu.devctx,(int)ctx_sz()*32/4); *(volatile u32*)(u64)(XHCI_DCBAA+sl*8)=rwu.devctx; *(volatile u32*)(u64)(XHCI_DCBAA+sl*8+4)=0;
    ctx_zero(rwu.inctx,(int)ctx_sz()*33/4); volatile u32* ic=(volatile u32*)(u64)rwu.inctx; ic[1]=3u;
    volatile u32* sc=(volatile u32*)(u64)(rwu.inctx+ctx_sz()); xh_slot_ctx(sc,L,1);
    volatile u32* e0=(volatile u32*)(u64)(rwu.inctx+ctx_sz()*2); e0[1]=(4u<<3)|(3u<<1)|((u32)mps0<<16); e0[2]=rwu.ep0|1u; e0[3]=0; e0[4]=8;
    if(!xh_cmd_s(rwu.inctx,0,0,11,(u32)sl,&cc,&sl)){ xhci_failcc=cc; RFAIL("Address Device"); }
    xh_delay_ms(10);
    if(!xh_fix_mps0(rwu.inctx,L,rwu.ep0))RFAIL("GET device descriptor (8)");
    if(!xh_ctrl(0x80,6,0x0100,0,18,1,0))RFAIL("GET device descriptor");
    volatile u8* db=(volatile u8*)(u64)rwu.dbuf;
    rwu.vid=(u16)(db[8]|(db[9]<<8)); rwu.pid=(u16)(db[10]|(db[11]<<8));
    if(!rtwu_chip_for(rwu.vid,rwu.pid))RFAIL("not a supported Realtek USB Wi-Fi adapter");
    if(!xh_ctrl(0x80,6,0x0200,0,9,1,0))RFAIL("GET config(9)");
    int wt=db[2]|(db[3]<<8), cv=db[5]; if(wt<9)RFAIL("bad config"); if(wt>1024)wt=1024;
    if(!xh_ctrl(0x80,6,0x0200,0,(u16)wt,1,0))RFAIL("GET config(full)");
    static u8 full[1024]; for(int i=0;i<wt;i++)full[i]=db[i];
    int mine=0, done=0; rwu.idci=0; rwu.nout=0;
    for(int i=0;i+2<=wt;){ int bl=full[i]; if(bl<2||i+bl>wt)break;
        if(full[i+1]==4&&bl>=9){ if(mine&&rwu.idci&&rwu.nout)done=1; mine=!done&&full[i+3]==0&&full[i+5]==0xFF; if(mine){ rwu.idci=0; rwu.nout=0; } }
        else if(full[i+1]==5&&bl>=7&&mine&&(full[i+3]&3)==2){ int a=full[i+2], m=(full[i+4]|(full[i+5]<<8))&0x7FF;
            if((a&0x80)&&!rwu.idci){ rwu.idci=(a&0x0F)*2+1; rwu.imps=m; }
            else if(!(a&0x80)&&rwu.nout<4){ rwu.odci[rwu.nout]=(a&0x0F)*2; rwu.omps[rwu.nout]=m; rwu.nout++; } }
        i+=bl; }
    if(!rwu.idci||!rwu.nout)RFAIL("no Wi-Fi bulk endpoints");
    if(!xh_ctrl(0x00,9,(u16)cv,0,0,0,0))RFAIL("SET_CONFIGURATION");
    ctx_zero(rwu.inctx,(int)ctx_sz()*33/4);
    ic=(volatile u32*)(u64)rwu.inctx; ic[1]=1u|(1u<<rwu.idci); int maxd=rwu.idci;
    for(int k=0;k<rwu.nout;k++){ ic[1]|=1u<<rwu.odci[k]; if(rwu.odci[k]>maxd)maxd=rwu.odci[k]; }
    sc=(volatile u32*)(u64)(rwu.inctx+ctx_sz()); xh_slot_ctx(sc,L,maxd);
    for(int k=0;k<=rwu.nout;k++){ int dci=k?rwu.odci[k-1]:rwu.idci, mps=k?rwu.omps[k-1]:rwu.imps; if(!mps)mps=speed>=4?1024:512;
        ring_init(rwu.ring[k]); rwu.enq[k]=0; rwu.cyc[k]=1;
        volatile u32* ec=(volatile u32*)(u64)(rwu.inctx+ctx_sz()*(u32)(dci+1)); ec[0]=0; ec[1]=(u32)(k?2:6)<<3|(3u<<1)|((u32)mps<<16); ec[2]=rwu.ring[k]|1u; ec[3]=0; ec[4]=2048; }
    if(!xh_cmd_s(rwu.inctx,0,0,12,(u32)rwu.slot,&cc,&sl)){ xhci_failcc=cc; RFAIL("Configure Endpoint"); }
    rwu.e0_enq=e0_enq; rwu.e0_cyc=e0_cyc; rwu.ctl=g_xcur; rwu.port=L->root; rwu.speed=speed; rwu.fails=0; rwu.ok=1;
    return 1;
}
#undef RFAIL
int rtwusb_xhci_open(void){
    if(rwu.ok)return 1;
    if(!g_xhc_n||!btu_lock())return 0;
    int prev=g_xcur; int s_slot=cur_slot; u32 s_ep0=cur_ep0, s_db=cur_dbuf, s_enq=e0_enq, s_cyc=e0_cyc;
    for(int c=0;c<g_xhc_n&&!rwu.ok;c++){ xhc_select(c); if(!xhci_present||!xhci_init_ok)continue;
        for(int u=0;u<USBDEV_MAX&&!rwu.ok;u++){ usbdev_t* ud=&g_usbdev[u]; if(!ud->used||ud->ctl!=c||ud->hid||ud->cam||ud->prn||!rtwu_chip_for(ud->vid,ud->pid))continue;
            int p=ud->port; if(p<0||p>=xhci_ports||!xhci_port_conn[p]||btu_owns(c,p))continue;
            int hub=0; for(int h=0;h<XHUB_MAX;h++) if(xhub[h].used&&xhub[h].ctl==c&&xhub[h].root==p)hub=1; if(hub)continue;
            int sp=xh_root_reset(p); usbloc_t L; L.root=p; L.depth=0; L.hub=-1; L.hport=0; L.speed=sp; L.route=0; L.tt_slot=0; L.tt_port=0;
            if(!sp||!rwu_enum(&L)){ if(rwu.slot){ xh_disable_slot(rwu.slot); rwu.slot=0; } char b[96],*q=b; q=scopy(q,"wifi usb: adapter setup failed: "); q=scopy(q,xhci_failstep); *q=0; klog(b); }
            else usb_seen[p]=1; } }
    xhc_select(prev); cur_slot=s_slot; cur_ep0=s_ep0; cur_dbuf=s_db; e0_enq=s_enq; e0_cyc=s_cyc;
    btu_unlock();
    if(!rwu.ok)return 0;
    rwu_lo.vid=rwu.vid; rwu_lo.pid=rwu.pid; rwu_lo.nout=(u8)rwu.nout; rwu_lo.speed=(u8)rwu.speed;
    if(rtwu_attach(&rwu_lo)<0){ rwu.ok=0; return 0; }
    { char b[96],*q=b; q=scopy(q,"wifi usb: Realtek adapter "); q=hx(q,rwu.vid,4); *q++=':'; q=hx(q,rwu.pid,4); q=scopy(q," claimed, bulk-out endpoints "); q=udec(q,(u32)rwu.nout); *q=0; klog(b); }
    return 1;
}
