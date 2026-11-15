#include "include/nvx/drivers/bluetooth/bt.h"
#include "drivers/bluetooth/mtkbt_fw.h"
#include "drivers/bluetooth/bcmbt_fw.h"
#include "drivers/bluetooth/rtlbt_fw.h"
static int btu_vclass(u16 vid,u16 pid){ return bcmbt_usb_vendor(vid)||rtlbt_usb_listed(vid,pid); }
#define BTU_NEV 32
#define BTU_EVSZ 260
#define BTU_NARM 8
#define BTU_BUFSZ 64
#define BTU_PAGES 8
static struct { int ok, ctl, slot, iface, int_ep, int_mps, int_intv, bin_ep, bout_ep, bin_mps, bout_mps, int_dci, bin_dci, bout_dci, halt;
    u32 mem, inctx, devctx, ep0, dbuf, intring, binring, boutring, int_enq, int_cyc, e0_enq, e0_cyc, evbase, route, nev, ndrop, nerr, ncmd; u16 vid, pid; usbloc_t L; } btu;
static u8 btu_q[BTU_NEV][BTU_EVSZ]; static int btu_qlen[BTU_NEV]; static int btu_qh, btu_qt;
static u8 btu_acc[BTU_EVSZ]; static int btu_accn;
static int btu_active(void){ return btu.ok; }
static int btu_owns(int c,int p){ return btu.ok&&btu.ctl==c&&btu.L.root==p; }
static void btu_qput(const u8* e,int n){
    int nt=(btu_qt+1)%BTU_NEV; if(nt==btu_qh){ btu.ndrop++; return; }
    for(int i=0;i<n;i++)btu_q[btu_qt][i]=e[i];
    btu_qlen[btu_qt]=n; btu_qt=nt; btu.nev++;
}
static void btu_rx(const u8* b,int n,int shortpkt){
    for(int i=0;i<n;i++){
        if(btu_accn<BTU_EVSZ)btu_acc[btu_accn++]=b[i]; else { btu.nerr++; btu_accn=0; }
        if(btu_accn>=2&&btu_accn>=2+btu_acc[1]){ btu_qput(btu_acc,btu_accn); btu_accn=0; }
    }
    if(shortpkt&&btu_accn){ btu.nerr++; btu_accn=0; }
}
static void btu_arm(u32 buf){
    volatile u32* tr=(volatile u32*)(u64)(btu.intring+btu.int_enq*16);
    tr[0]=buf; tr[1]=0; tr[2]=(u32)btu.int_mps; __sync_synchronize(); tr[3]=(1u<<10)|(1u<<5)|btu.int_cyc;
    btu.int_enq++; if(btu.int_enq>=255){ volatile u32* lk=(volatile u32*)(u64)(btu.intring+255*16); lk[3]=(lk[3]&~1u)|btu.int_cyc; btu.int_enq=0; btu.int_cyc^=1; }
    __sync_synchronize(); xh_db(btu.slot,(u32)btu.int_dci);
}
static struct { u32 oenq, ocyc, ienq, icyc, ires; volatile int odone, idone, iarm, obusy; u8 occ, icc; } btk;
static void btk_reset(void){ btk.oenq=btk.ienq=0; btk.ocyc=btk.icyc=1; btk.odone=btk.idone=btk.iarm=btk.obusy=0; }
static void btk_done(int ep,u8 cc,u32 res){ if(ep==btu.bout_dci){ btk.occ=cc; btk.odone=1; btk.obusy=0; } else { btk.icc=cc; btk.ires=res; btk.idone=1; } }
static int btu_xfer(u8 sl,int ep,u8 cc,u32 res,u32 ptr){
    if(!btu.ok||btu.ctl!=g_xcur||sl!=(u8)btu.slot)return 0;
    if(ep==btu.bin_dci||ep==btu.bout_dci){ btk_done(ep,cc,res); return 1; }
    if(ep!=btu.int_dci)return 0;
    u32 buf=0; if(ptr>=btu.intring&&ptr<btu.intring+255*16)buf=((volatile u32*)(u64)ptr)[0];
    if(buf<btu.evbase||buf>=btu.evbase+BTU_NARM*BTU_BUFSZ||((buf-btu.evbase)%BTU_BUFSZ)){ btu.nerr++; return 1; }
    if(cc==1||cc==13){ int got=btu.int_mps-(int)(res&0xFFFFFF); if(got<0)got=0; if(got>btu.int_mps)got=btu.int_mps; btu_rx((const u8*)(u64)buf,got,got<btu.int_mps); if(!btu.halt)btu_arm(buf); }
    else { btu.nerr++; btu.halt=1; }
    return 1;
}
static int btu_ctrl(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in){
    int s_slot=cur_slot; u32 s_ep0=cur_ep0, s_db=cur_dbuf, s_enq=e0_enq, s_cyc=e0_cyc;
    cur_slot=btu.slot; cur_ep0=btu.ep0; cur_dbuf=btu.dbuf; e0_enq=btu.e0_enq; e0_cyc=btu.e0_cyc;
    int r=xh_ctrl(rt,req,val,idx,len,in,0);
    btu.e0_enq=e0_enq; btu.e0_cyc=e0_cyc;
    cur_slot=s_slot; cur_ep0=s_ep0; cur_dbuf=s_db; e0_enq=s_enq; e0_cyc=s_cyc;
    return r;
}
static void btu_gone(const char* why){
    if(!btu.ok)return;
    btu.ok=0; if(btu.slot){ xh_disable_slot(btu.slot); btu.slot=0; }
    btu_qh=btu_qt=0; btu_accn=0; klog(why);
}
static u64 btu_rec_t;
static void btu_recover(void){
    if(btu.L.depth==0){ u32 pv=XMR(xhci_op+0x400+(u32)btu.L.root*0x10); if(!(pv&1)){ btu_gone("bluetooth: adapter removed"); return; } }
    if(!btu.halt||hr_ns()-btu_rec_t<50000000ULL)return;
    btu_rec_t=hr_ns(); btu.halt=0;
    u8 cc=0,sl=0; int dci=btu.int_dci;
    xh_cmd_s(0,0,0,14u|((u32)dci<<6),(u32)btu.slot,&cc,&sl);
    xh_cmd_s((btu.intring+btu.int_enq*16)|btu.int_cyc,0,0,16u|((u32)dci<<6),(u32)btu.slot,&cc,&sl);
    btu_ctrl(0x02,1,0,(u16)btu.int_ep,0,0);
    btu_accn=0; for(int i=0;i<BTU_NARM;i++)btu_arm(btu.evbase+(u32)(i*BTU_BUFSZ));
    klog("bluetooth: event endpoint recovered");
}
static int btu_lock(void){
    if(xh_wdepth>0)return 0;
    for(int t=0;t<2000;t++){ if(!__sync_lock_test_and_set(&xh_poll_busy,1)){ xh_poll_t0=g_ticks; return 1; } hr_delay_us(50); }
    return 0;
}
static void btu_unlock(void){ __sync_lock_release(&xh_poll_busy); }
static void btu_pump(void){
    if(!btu.ok||!btu_lock())return;
    int prev=g_xcur; xhc_select(btu.ctl);
    xhci_poll_one();
    btu_recover();
    xhc_select(prev);
    btu_unlock();
}
static int btu_mem(void){
    if(btu.mem)return 1;
    u8* p=(u8*)dma_alloc_pages(BTU_PAGES); if(!p){ klog("bluetooth: no memory"); return 0; }
    btu.mem=(u32)(u64)p;
    btu.inctx=btu.mem; btu.devctx=btu.mem+0x1000u; btu.ep0=btu.mem+0x2000u; btu.intring=btu.mem+0x3000u; btu.binring=btu.mem+0x4000u; btu.boutring=btu.mem+0x5000u; btu.dbuf=btu.mem+0x6000u; btu.evbase=btu.mem+0x7000u;
    return 1;
}
static int btu_parse(const u8* c,int n){
    int curif, alt, mine=0, found=0; btu.int_ep=btu.bin_ep=btu.bout_ep=0;
    for(int i=0;i+2<=n;){ int bl=c[i]; if(bl<2||i+bl>n)break; int ty=c[i+1];
        if(ty==4&&bl>=9){ if(btu.int_ep&&btu.bin_ep&&btu.bout_ep)found=1; curif=c[i+2]; alt=c[i+3]; mine=((c[i+5]==0xE0||(c[i+5]==0xFF&&btu_vclass(btu.vid,btu.pid)))&&c[i+6]==1&&c[i+7]==1&&alt==0&&!found); if(mine){ btu.iface=curif; btu.int_ep=btu.bin_ep=btu.bout_ep=0; } }
        else if(ty==5&&bl>=7&&mine){ int a=c[i+2], at=c[i+3]&3, m=(c[i+4]|(c[i+5]<<8))&0x7FF;
            if(at==3&&(a&0x80)&&!btu.int_ep){ btu.int_ep=a; btu.int_mps=m; btu.int_intv=c[i+6]; }
            else if(at==2&&(a&0x80)&&!btu.bin_ep){ btu.bin_ep=a; btu.bin_mps=m; }
            else if(at==2&&!(a&0x80)&&!btu.bout_ep){ btu.bout_ep=a; btu.bout_mps=m; } }
        i+=bl; }
    return btu.int_ep&&btu.bin_ep&&btu.bout_ep;
}
#define BFAIL(m) do{ xh_set_fail(m); btu.ok=0; return 0; }while(0)
static int btu_enum_loc(const usbloc_t* L){
    if(btu.ok||!btu_mem())return 0;
    xhci_failstep[0]=0; xhci_failcc=0; btu.slot=0;
    int speed=L->speed; int mps0=(speed==3)?64:(speed>=4)?512:8;
    cur_ep0=btu.ep0; cur_dbuf=btu.dbuf; e0_enq=0; e0_cyc=1; ring_init(btu.ep0);
    u8 cc=0,sl=0; if(!xh_cmd_s(0,0,0,9,0,&cc,&sl)||!sl){ xhci_failcc=cc; BFAIL("Enable Slot"); }
    cur_slot=sl; btu.slot=sl;
    ctx_zero(btu.devctx,(int)ctx_sz()*32/4); *(volatile u32*)(u64)(XHCI_DCBAA+sl*8)=btu.devctx; *(volatile u32*)(u64)(XHCI_DCBAA+sl*8+4)=0;
    ctx_zero(btu.inctx,(int)ctx_sz()*33/4); volatile u32* ic=(volatile u32*)(u64)btu.inctx; ic[1]=(1u<<0)|(1u<<1);
    volatile u32* sc=(volatile u32*)(u64)(btu.inctx+ctx_sz()); xh_slot_ctx(sc,L,1);
    volatile u32* e0=(volatile u32*)(u64)(btu.inctx+ctx_sz()*2); e0[1]=(4u<<3)|(3u<<1)|((u32)mps0<<16); e0[2]=btu.ep0|1u; e0[3]=0; e0[4]=8;
    if(!xh_cmd_s(btu.inctx,0,0,11,(u32)sl,&cc,&sl)){ xhci_failcc=cc; BFAIL("Address Device"); }
    xh_delay_ms(10);
    if(!xh_fix_mps0(btu.inctx,L,btu.ep0))BFAIL("GET device descriptor (8)");
    if(!xh_ctrl(0x80,6,0x0100,0,18,1,0))BFAIL("GET device descriptor");
    volatile u8* db=(volatile u8*)(u64)btu.dbuf;
    btu.vid=(u16)(db[8]|(db[9]<<8)); btu.pid=(u16)(db[10]|(db[11]<<8));
    if(!xh_ctrl(0x80,6,0x0200,0,9,1,0))BFAIL("GET config(9)");
    int wt=db[2]|(db[3]<<8), cv=db[5]; if(wt<9)BFAIL("bad config"); if(wt>1024)wt=1024;
    if(!xh_ctrl(0x80,6,0x0200,0,(u16)wt,1,0))BFAIL("GET config(full)");
    static u8 full[1024]; for(int i=0;i<wt;i++)full[i]=db[i];
    if(!btu_parse(full,wt))BFAIL("no Bluetooth HCI interface");
    if(!xh_ctrl(0x00,9,(u16)cv,0,0,0,0))BFAIL("SET_CONFIGURATION");
    if(btu.int_mps<8)btu.int_mps=16;
    if(btu.int_mps>BTU_BUFSZ)btu.int_mps=BTU_BUFSZ;
    if(!btu.bin_mps)btu.bin_mps=64;
    if(!btu.bout_mps)btu.bout_mps=64;
    btu.int_dci=(btu.int_ep&0x0F)*2+1; btu.bin_dci=(btu.bin_ep&0x0F)*2+1; btu.bout_dci=(btu.bout_ep&0x0F)*2;
    int maxdci=btu.int_dci; if(btu.bin_dci>maxdci)maxdci=btu.bin_dci; if(btu.bout_dci>maxdci)maxdci=btu.bout_dci;
    ctx_zero(btu.inctx,(int)ctx_sz()*33/4);
    ic=(volatile u32*)(u64)btu.inctx; ic[1]=(1u<<0)|(1u<<btu.int_dci)|(1u<<btu.bin_dci)|(1u<<btu.bout_dci);
    sc=(volatile u32*)(u64)(btu.inctx+ctx_sz()); xh_slot_ctx(sc,L,maxdci);
    ring_init(btu.intring); ring_init(btu.binring); ring_init(btu.boutring); btu.int_enq=0; btu.int_cyc=1; btk_reset();
    volatile u32* ei=(volatile u32*)(u64)(btu.inctx+ctx_sz()*(u32)(btu.int_dci+1)); ei[0]=xh_interval(speed,btu.int_intv)<<16; ei[1]=(7u<<3)|(3u<<1)|((u32)btu.int_mps<<16); ei[2]=btu.intring|1u; ei[3]=0; ei[4]=(u32)btu.int_mps|((u32)btu.int_mps<<16);
    volatile u32* bi=(volatile u32*)(u64)(btu.inctx+ctx_sz()*(u32)(btu.bin_dci+1)); bi[1]=(6u<<3)|(3u<<1)|((u32)btu.bin_mps<<16); bi[2]=btu.binring|1u; bi[3]=0; bi[4]=1024;
    volatile u32* bo=(volatile u32*)(u64)(btu.inctx+ctx_sz()*(u32)(btu.bout_dci+1)); bo[1]=(2u<<3)|(3u<<1)|((u32)btu.bout_mps<<16); bo[2]=btu.boutring|1u; bo[3]=0; bo[4]=1024;
    if(!xh_cmd_s(btu.inctx,0,0,12,(u32)btu.slot,&cc,&sl)){ xhci_failcc=cc; BFAIL("Configure Endpoint"); }
    btu.e0_enq=e0_enq; btu.e0_cyc=e0_cyc;
    btu.ctl=g_xcur; btu.L=*L; btu.route=L->route; btu_qh=btu_qt=0; btu_accn=0; btu.halt=0; btu.nev=btu.ndrop=btu.nerr=btu.ncmd=0;
    btu.ok=1;
    for(int i=0;i<BTU_NARM;i++)btu_arm(btu.evbase+(u32)(i*BTU_BUFSZ));
    { char b[96],*p=b; p=scopy(p,"bluetooth: USB adapter "); p=hx(p,btu.vid,4); *p++=':'; p=hx(p,btu.pid,4); p=scopy(p," claimed, event endpoint 0x"); p=hx(p,(u32)btu.int_ep,2); *p=0; klog(b); }
    return 1;
}
#undef BFAIL
static int btu_candidate(int c,int p,int vend){
    int bt=0;
    for(int k=0;k<USBDEV_MAX;k++) if(g_usbdev[k].used&&g_usbdev[k].ctl==c&&g_usbdev[k].port==p){ if(g_usbdev[k].hid||g_usbdev[k].cam||g_usbdev[k].prn)return 0; if(vend?(g_usbdev[k].cls==0xFF&&btu_vclass(g_usbdev[k].vid,g_usbdev[k].pid)):g_usbdev[k].cls==0xE0)bt=1; }
    if(!bt)return 0;
    if(msd_ok&&msd_ctl==c&&msd_port==p)return 0;
    if(cam.ok&&cam.ctl==c&&cam.root==p)return 0;
    if(aud.ok&&aud.ctl==c&&aud.root==p)return 0;
    if(usn.ok&&usn.ctl==c&&usn.L.root==p)return 0;
    for(int h=0;h<XHUB_MAX;h++) if(xhub[h].used&&xhub[h].ctl==c&&xhub[h].root==p)return 0;
    for(int i=0;i<xhid_n;i++) if(xhid_used[i]&&xhid_ctl[i]==c&&xhid_port[i]==p)return 0;
    return 1;
}
int btusb_listed(u16* vid,u16* pid){
    for(int k=0;k<USBDEV_MAX;k++) if(g_usbdev[k].used&&(g_usbdev[k].cls==0xE0||(g_usbdev[k].cls==0xFF&&btu_vclass(g_usbdev[k].vid,g_usbdev[k].pid)))){ if(vid)*vid=g_usbdev[k].vid; if(pid)*pid=g_usbdev[k].pid; return 1; }
    return 0;
}
int btusb_probe(void){
    if(btu.ok)return 1;
    if(!g_xhc_n)return 0;
    if(!btu_lock())return 0;
    int prev=g_xcur; int s_slot=cur_slot; u32 s_ep0=cur_ep0, s_db=cur_dbuf, s_enq=e0_enq, s_cyc=e0_cyc;
    for(int vend=0;vend<2&&!btu.ok;vend++)
    for(int c=0;c<g_xhc_n&&!btu.ok;c++){ xhc_select(c); if(!xhci_present||!xhci_init_ok)continue; xhci_port_refresh();
        for(int p=0;p<xhci_ports&&!btu.ok;p++){ if(!xhci_port_conn[p]||!btu_candidate(c,p,vend))continue;
            int sp=xh_root_reset(p); usbloc_t L; L.root=p; L.depth=0; L.hub=-1; L.hport=0; L.speed=sp; L.route=0; L.tt_slot=0; L.tt_port=0;
            if(!sp||!btu_enum_loc(&L)){ if(btu.slot){ xh_disable_slot(btu.slot); btu.slot=0; } char b[96],*q=b; q=scopy(q,"bluetooth: adapter setup failed: "); q=scopy(q,xhci_failstep); *q=0; klog(b); }
            else { usb_seen[p]=1; xhci_port_conn[p]=1; } } }
    xhc_select(prev);
    cur_slot=s_slot; cur_ep0=s_ep0; cur_dbuf=s_db; e0_enq=s_enq; e0_cyc=s_cyc;
    btu_unlock();
    if(btu.ok)mtkbt_attach(btu.vid,btu.pid);
    return btu.ok;
}
int btusb_ok(void){ return btu.ok; }
void btusb_ids(u16* vid,u16* pid){ *vid=btu.vid; *pid=btu.pid; }
void btusb_stats(u32* nev,u32* ndrop,u32* nerr,u32* ncmd){ *nev=btu.nev; *ndrop=btu.ndrop; *nerr=btu.nerr; *ncmd=btu.ncmd; }
int btusb_cmd(const u8* pkt,int n){
    if(!btu.ok||n<3||n>258)return -1;
    if(!btu_lock())return -1;
    int prev=g_xcur; xhc_select(btu.ctl);
    volatile u8* db=(volatile u8*)(u64)btu.dbuf; for(int i=0;i<n;i++)db[i]=pkt[i];
    int r=btu_ctrl(0x20,0,0,0,(u16)n,0);
    btu_recover();
    xhc_select(prev);
    btu_unlock();
    if(!r){ btu.nerr++; return -1; }
    btu.ncmd++; return 0;
}
int btusb_vctrl(u8 rt,u8 req,u16 val,u16 idx,u8* buf,u16 len){
    if(!btu.ok||len>1024||!btu_lock())return 0;
    int prev=g_xcur; xhc_select(btu.ctl);
    volatile u8* db=(volatile u8*)(u64)btu.dbuf; int in=(rt&0x80)!=0;
    for(int i=0;i<len;i++)db[i]=in?0:buf[i];
    int r=btu_ctrl(rt,req,val,idx,len,in);
    if(r&&in)for(int i=0;i<len;i++)buf[i]=db[i];
    xhc_select(prev);
    btu_unlock();
    return r;
}
int btusb_evt(u8* out,int cap){
    if(btu_qh==btu_qt)btu_pump();
    if(btu_qh==btu_qt)return 0;
    int n=btu_qlen[btu_qh]; if(n>cap)n=cap; for(int i=0;i<n;i++)out[i]=btu_q[btu_qh][i];
    btu_qh=(btu_qh+1)%BTU_NEV; return n;
}
static void btu_remove_below(int root,u32 R,int dR){ if(btu.ok&&btu.ctl==g_xcur&&btu.L.root==root&&xh_under(btu.route,btu.L.depth,R,dR))btu_gone("bluetooth: adapter removed"); }
void btu_reset_all(void){ if(btu.ok){ btu.ok=0; btu.slot=0; btu_qh=btu_qt=0; klog("bluetooth: controller reset, adapter released"); } }
#define BTK_OBUF (btu.evbase+0x400u)
#define BTK_IBUF (btu.evbase+0x800u)
#define BTK_ILEN 1024
static void btk_trb(u32 ring,u32* enq,u32* cyc,u32 buf,int len,int dci){
    volatile u32* tr=(volatile u32*)(u64)(ring+*enq*16);
    tr[0]=buf; tr[1]=0; tr[2]=(u32)len; __sync_synchronize(); tr[3]=(1u<<10)|(1u<<5)|(1u<<2)|*cyc;
    (*enq)++; if(*enq>=255){ volatile u32* lk=(volatile u32*)(u64)(ring+255*16); lk[3]=(lk[3]&~1u)|*cyc; *enq=0; *cyc^=1; }
    __sync_synchronize(); xh_db(btu.slot,(u32)dci);
}
static int btk_wait(volatile int* f,int ms){ u64 t0=hr_ns(), lim=(u64)ms*1000000ULL; while(!*f&&btu.ok){ btu_pump(); if(*f||hr_ns()-t0>=lim)break; hr_delay_us(50); } return *f; }
int btusb_bulk_out(const u8* p,int n,int ms){
    if(!btu.ok||btk.obusy||n<1||n>1024)return -1;
    if(!btu_lock())return -1;
    int prev=g_xcur; xhc_select(btu.ctl);
    volatile u8* d=(volatile u8*)(u64)BTK_OBUF; for(int i=0;i<n;i++)d[i]=p[i];
    btk.odone=0; btk.obusy=1; btk_trb(btu.boutring,&btk.oenq,&btk.ocyc,BTK_OBUF,n,btu.bout_dci);
    xhc_select(prev); btu_unlock();
    if(!btk_wait(&btk.odone,ms))return -1;
    if(btk.occ!=1){ btu.nerr++; return -1; }
    return n;
}
int btusb_bulk_in(u8* out,int cap,int ms){
    if(!btu.ok)return -1;
    if(!btk.iarm){ if(!btu_lock())return 0; int prev=g_xcur; xhc_select(btu.ctl); btk.idone=0; btk.iarm=1; btk_trb(btu.binring,&btk.ienq,&btk.icyc,BTK_IBUF,BTK_ILEN,btu.bin_dci); xhc_select(prev); btu_unlock(); }
    if(!btk_wait(&btk.idone,ms))return 0;
    btk.iarm=0; btk.idone=0;
    if(btk.icc!=1&&btk.icc!=13){ btu.nerr++; return -1; }
    int got=BTK_ILEN-(int)(btk.ires&0xFFFFFF); if(got<0)got=0; if(got>cap)got=cap;
    volatile u8* d=(volatile u8*)(u64)BTK_IBUF; for(int i=0;i<got;i++)out[i]=d[i];
    return got;
}
