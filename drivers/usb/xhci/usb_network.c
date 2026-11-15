#include "drivers/usb/usbnet_proto.h"
#include "include/drivers.h"
#define UN_NRX 8
#define UN_NTX 16
#define UN_TXSZ 2048
#define UN_QN 64
#define UN_QSZ 1536
#define UN_PAGES 96
int nic_add(const struct nvx_nic* n);
void nic_offer(int i);
void nic_drop(int i);
static int usn_nrx=UN_NRX;
typedef struct { int kind, cfgval, ctlif, dataif, alt, in_ep, in_mps, out_ep, out_mps, imac; } usn_cand_t;
static struct { int ok, ctl, slot, kind, ctlif, dataif, in_ep, out_ep, in_dci, out_dci, in_mps, out_mps, rxsz, rxon, txout, txi, nicidx, ndiv, nrem, halt_in, halt_out, depth;
    u32 mem, inctx, devctx, ep0, dbuf, inring, outring, in_enq, in_cyc, out_enq, out_cyc, rxbase, txbase, e0_enq, e0_cyc, rndis_id, route, rxf, txf, err, drops; u16 seq, vid, pid; u8 mac[6]; usbloc_t L; } usn;
static u8* usn_q; static int usn_qlen[UN_QN]; static int usn_qh, usn_qt;
int g_usn_pref=0;
static const char* usn_kname(int k){ return k==1?"CDC-ECM":k==2?"CDC-NCM":k==3?"RNDIS":"none"; }
static void usn_q_put(const u8* f,int n){
    if(!usn_q||n<14||n>UN_QSZ){ usn.drops++; return; }
    int nt=(usn_qt+1)%UN_QN; if(nt==usn_qh){ usn.drops++; return; }
    u8* d=usn_q+usn_qt*UN_QSZ; for(int i=0;i<n;i++)d[i]=f[i]; usn_qlen[usn_qt]=n; usn_qt=nt; usn.rxf++;
}
static void usn_rx_data(const u8* b,int n){
    if(n<=0)return;
    if(usn.kind==1)usn_q_put(b,n);
    else if(usn.kind==2){ if(ncm_parse(b,n)<0)usn.err++; }
    else if(usn.kind==3){ if(rndis_parse(b,n)<=0)usn.err++; }
}
static void usn_trb(u32 ring,u32* enq,u32* cyc,u32 buf,int len,int dci){
    volatile u32* tr=(volatile u32*)(u64)(ring+(*enq)*16);
    tr[0]=buf; tr[1]=0; tr[2]=(u32)len; __sync_synchronize(); tr[3]=(1u<<10)|(1u<<5)|(*cyc);
    (*enq)++; if(*enq>=255){ volatile u32* lk=(volatile u32*)(u64)(ring+255*16); lk[3]=(lk[3]&~1u)|(*cyc); *enq=0; *cyc^=1; }
    __sync_synchronize(); xh_db(usn.slot,(u32)dci);
}
static void usn_arm(u32 buf){ usn_trb(usn.inring,&usn.in_enq,&usn.in_cyc,buf,usn.rxsz,usn.in_dci); }
static int usn_xfer(u8 sl,int ep,u8 cc,u32 res,u32 ptr){
    if(!usn.ok||usn.ctl!=g_xcur||sl!=(u8)usn.slot)return 0;
    if(ep==usn.out_dci){ if(usn.txout>0)usn.txout--; if(cc!=1&&cc!=13){ usn.err++; usn.halt_out=1; } return 1; }
    if(ep!=usn.in_dci)return 0;
    u32 buf=0; if(ptr>=usn.inring&&ptr<usn.inring+255*16)buf=((volatile u32*)(u64)ptr)[0];
    if(buf<usn.rxbase||buf>=usn.rxbase+(u32)(usn_nrx*usn.rxsz)||((buf-usn.rxbase)%(u32)usn.rxsz)){ usn.err++; return 1; }
    if(cc==1||cc==13){ int got=usn.rxsz-(int)(res&0xFFFFFF); if(got<0)got=0; if(got>usn.rxsz)got=usn.rxsz; usn_rx_data((const u8*)(u64)buf,got); if(usn.rxon&&!usn.halt_in)usn_arm(buf); }
    else { usn.err++; usn.halt_in=1; }
    return 1;
}
static int usn_ctrl(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in){
    int s_slot=cur_slot; u32 s_ep0=cur_ep0, s_db=cur_dbuf, s_enq=e0_enq, s_cyc=e0_cyc;
    cur_slot=usn.slot; cur_ep0=usn.ep0; cur_dbuf=usn.dbuf; e0_enq=usn.e0_enq; e0_cyc=usn.e0_cyc;
    int r=xh_ctrl(rt,req,val,idx,len,in,0);
    usn.e0_enq=e0_enq; usn.e0_cyc=e0_cyc;
    cur_slot=s_slot; cur_ep0=s_ep0; cur_dbuf=s_db; e0_enq=s_enq; e0_cyc=s_cyc;
    return r;
}
static void usn_unhalt(int dci,int epaddr,u32 ring,u32* enq,u32* cyc){
    u8 cc=0,sl=0;
    xh_cmd_s(0,0,0,14u|((u32)dci<<6),(u32)usn.slot,&cc,&sl);
    xh_cmd_s((ring+(*enq)*16)|(*cyc),0,0,16u|((u32)dci<<6),(u32)usn.slot,&cc,&sl);
    usn_ctrl(0x02,1,0,(u16)epaddr,0,0);
}
static void usn_gone(void);
static u64 usn_rec_t;
static void usn_recover(void){
    if(usn.L.depth==0){ u32 pv=XMR(xhci_op+0x400+(u32)usn.L.root*0x10); if(!(pv&1)){ usn_gone(); return; } }
    if(hr_ns()-usn_rec_t<50000000ULL)return;
    usn_rec_t=hr_ns();
    if(usn.halt_in){ usn.halt_in=0; usn_unhalt(usn.in_dci,usn.in_ep,usn.inring,&usn.in_enq,&usn.in_cyc); for(int i=0;i<usn_nrx;i++)usn_arm(usn.rxbase+(u32)(i*usn.rxsz)); klog("usb-net: receive endpoint recovered"); }
    if(usn.halt_out){ usn.halt_out=0; usn_unhalt(usn.out_dci,usn.out_ep,usn.outring,&usn.out_enq,&usn.out_cyc); usn.txout=0; klog("usb-net: send endpoint recovered"); }
}
static void usn_events(void){
    if(!usn.ok)return;
    if(xh_wdepth>0||__sync_lock_test_and_set(&xh_poll_busy,1))return;
    xh_poll_t0=g_ticks;
    int prev=g_xcur; xhc_select(usn.ctl);
    if(usn.rxon&&!usn.halt_in)xh_db(usn.slot,(u32)usn.in_dci);
    xhci_poll_one();
    if(usn.halt_in||usn.halt_out)usn_recover();
    xhc_select(prev);
    __sync_lock_release(&xh_poll_busy);
}
static int usn_send(const u8* d,int len){
    if(!usn.ok||len<14||len>1518)return -1;
    usn_events();
    if(usn.txout>=UN_NTX){ u64 t0=hr_ns(); while(usn.ok&&usn.txout>=UN_NTX&&hr_ns()-t0<100000000ULL)usn_events(); }
    if(!usn.ok||usn.txout>=UN_NTX){ usn.err++; return -1; }
    u8* b=(u8*)(u64)(usn.txbase+(u32)(usn.txi*UN_TXSZ)); int n;
    if(usn.kind==2)n=ncm_build(b,UN_TXSZ-1,usn.seq++,d,len,usn.ndiv,usn.nrem);
    else if(usn.kind==3)n=rndis_wrap(b,UN_TXSZ-1,d,len);
    else { for(int i=0;i<len;i++)b[i]=d[i]; n=len; }
    if(n<0){ usn.err++; return -1; }
    if(usn.out_mps>0&&n%usn.out_mps==0){ b[n]=0; n++; if(usn.kind==2)usn_p16(b+8,(u16)n); }
    int prev=g_xcur; xhc_select(usn.ctl);
    usn.txout++; usn_trb(usn.outring,&usn.out_enq,&usn.out_cyc,(u32)(u64)b,n,usn.out_dci);
    xhc_select(prev);
    usn.txi=(usn.txi+1)%UN_NTX; usn.txf++;
    return len;
}
static int usn_recv(u8* out,int max){
    if(!usn.ok)return 0;
    if(usn_qh==usn_qt)usn_events();
    if(usn_qh==usn_qt)return 0;
    int n=usn_qlen[usn_qh]; if(n>max)n=max; const u8* s=usn_q+usn_qh*UN_QSZ; for(int i=0;i<n;i++)out[i]=s[i];
    usn_qh=(usn_qh+1)%UN_QN; return n;
}
static int usn_link(void){ return usn.ok; }
static int usn_parse(const u8* c,int n,int cfgval,usn_cand_t* best){
    int curif=-1, curalt=0, cls=0, sub=0, pro=0, kind=0, ctlif=-1, dataif=-1, imac=0, found=0;
    int dalt=-1, din=0, dout=0, dinm=0, doutm=0, ain=0, aout=0, ainm=0, aoutm=0;
    for(int i=0;i+2<=n;){ int bl=c[i]; if(bl<2||i+bl>n)break; int ty=c[i+1];
        if(ty==4&&bl>=9){
            if(cls==0x0A&&curif==dataif&&ain&&aout&&dalt<0){ dalt=curalt; din=ain; dout=aout; dinm=ainm; doutm=aoutm; }
            curif=c[i+2]; curalt=c[i+3]; cls=c[i+5]; sub=c[i+6]; pro=c[i+7]; ain=aout=ainm=aoutm=0;
            int k=0;
            if(cls==2&&sub==6)k=1; else if(cls==2&&sub==0x0D)k=2; else if(cls==2&&sub==2&&pro==0xFF)k=3; else if(cls==0xE0&&sub==1&&pro==3)k=3; else if(cls==0xEF&&sub==4&&pro==1)k=3;
            if(k&&!kind){ kind=k; ctlif=curif; dataif=-1; }
            else if(cls==0x0A&&kind&&dataif<0)dataif=curif;
        }
        else if(ty==0x24&&bl>=5&&kind&&curif==ctlif){ int st=c[i+2]; if(st==6)dataif=c[i+4]; else if(st==0x0F&&bl>=4)imac=c[i+3]; }
        else if(ty==5&&bl>=7&&cls==0x0A&&curif==dataif&&(c[i+3]&3)==2){ int a=c[i+2], m=c[i+4]|(c[i+5]<<8); if(a&0x80){ ain=a; ainm=m; } else { aout=a; aoutm=m; } }
        i+=bl; }
    if(cls==0x0A&&curif==dataif&&ain&&aout&&dalt<0){ dalt=curalt; din=ain; dout=aout; dinm=ainm; doutm=aoutm; }
    if(kind&&dataif>=0&&dalt>=0&&din&&dout){
        int score=(kind==g_usn_pref?10:0)+(kind==1?3:kind==2?2:1), bs=best->kind?((best->kind==g_usn_pref?10:0)+(best->kind==1?3:best->kind==2?2:1)):-1;
        if(score>bs){ best->kind=kind; best->cfgval=cfgval; best->ctlif=ctlif; best->dataif=dataif; best->alt=dalt; best->in_ep=din; best->in_mps=dinm&0x7FF; best->out_ep=dout; best->out_mps=doutm&0x7FF; best->imac=imac; found=1; }
    }
    return found;
}
static int usn_rndis(u32 type,u32 oid,const u8* val,int vlen,u8* out,int cap){
    u32 id=++usn.rndis_id; u8* db=(u8*)(u64)usn.dbuf; int len=rndis_msg(db,type,id,oid,val,vlen);
    if(!usn_ctrl(0x21,0,0,(u16)usn.ctlif,(u16)len,0))return -1;
    for(int t=0;t<50;t++){
        for(int i=0;i<1100;i++)db[i]=0;
        if(usn_ctrl(0xA1,1,0,(u16)usn.ctlif,1025,1)){ const u8* dp=0; int dl=0; int r=rndis_reply(db,1025,type|0x80000000u,id,&dp,&dl);
            if(r==0)return -1;
            if(r==1){ if(out&&dp){ if(dl>cap)dl=cap; for(int i=0;i<dl;i++)out[i]=dp[i]; } return dl; } }
        xh_delay_ms(20);
    }
    return -1;
}
static int usn_mem(void){
    if(usn.mem)return 1;
    u8* p=(u8*)dma_alloc_pages(UN_PAGES); u8* q=(u8*)dma_alloc_pages((UN_QN*UN_QSZ+4095)/4096);
    if(!p||!q){ klog("usb-net: no memory"); return 0; }
    usn.mem=(u32)(u64)p; usn_q=q;
    usn.inctx=usn.mem; usn.devctx=usn.mem+0x1000u; usn.ep0=usn.mem+0x2000u; usn.inring=usn.mem+0x3000u; usn.outring=usn.mem+0x4000u; usn.dbuf=usn.mem+0x5000u; usn.txbase=usn.mem+0x6000u;
    usn.rxbase=(usn.mem+0xE000u+0xFFFFu)&~0xFFFFu;
    return 1;
}
#define UFAIL(m) do{ xh_set_fail(m); usn.ok=0; return 0; }while(0)
static int usn_enum_loc(const usbloc_t* L){
    if(usn.ok||!usn_mem())return 0;
    xhci_failstep[0]=0; xhci_failcc=0; usn.slot=0;
    int speed=L->speed; int mps0=(speed==3)?64:(speed>=4)?512:8;
    cur_ep0=usn.ep0; cur_dbuf=usn.dbuf; e0_enq=0; e0_cyc=1; ring_init(usn.ep0);
    u8 cc=0,sl=0; if(!xh_cmd_s(0,0,0,9,0,&cc,&sl)||!sl){ xhci_failcc=cc; UFAIL("Enable Slot"); }
    cur_slot=sl; usn.slot=sl;
    ctx_zero(usn.devctx,(int)ctx_sz()*32/4); *(volatile u32*)(u64)(XHCI_DCBAA+sl*8)=usn.devctx; *(volatile u32*)(u64)(XHCI_DCBAA+sl*8+4)=0;
    ctx_zero(usn.inctx,(int)ctx_sz()*33/4); volatile u32* ic=(volatile u32*)(u64)usn.inctx; ic[1]=(1u<<0)|(1u<<1);
    volatile u32* sc=(volatile u32*)(u64)(usn.inctx+ctx_sz()); xh_slot_ctx(sc,L,1);
    volatile u32* e0=(volatile u32*)(u64)(usn.inctx+ctx_sz()*2); e0[1]=(4u<<3)|(3u<<1)|((u32)mps0<<16); e0[2]=usn.ep0|1u; e0[3]=0; e0[4]=8;
    if(!xh_cmd_s(usn.inctx,0,0,11,(u32)sl,&cc,&sl)){ xhci_failcc=cc; UFAIL("Address Device"); }
    xh_delay_ms(10);
    if(!xh_fix_mps0(usn.inctx,L,usn.ep0))UFAIL("GET device descriptor (8)");
    if(!xh_ctrl(0x80,6,0x0100,0,18,1,0))UFAIL("GET device descriptor");
    u8 dd[18]; { volatile u8* db=(volatile u8*)(u64)usn.dbuf; for(int i=0;i<18;i++)dd[i]=db[i]; }
    usn.vid=(u16)(dd[8]|(dd[9]<<8)); usn.pid=(u16)(dd[10]|(dd[11]<<8)); int ncfg=dd[17]; if(ncfg<1)ncfg=1; if(ncfg>4)ncfg=4;
    static u8 full[1024]; usn_cand_t best; for(int i=0;i<(int)sizeof best;i++)((u8*)&best)[i]=0;
    int bestn=0; static u8 bestc[1024];
    for(int ci=0;ci<ncfg;ci++){
        if(!xh_ctrl(0x80,6,(u16)(0x0200|ci),0,9,1,0))continue;
        volatile u8* db=(volatile u8*)(u64)usn.dbuf; int wt=db[2]|(db[3]<<8), cv=db[5]; if(wt<9)continue; if(wt>1024)wt=1024;
        if(!xh_ctrl(0x80,6,(u16)(0x0200|ci),0,(u16)wt,1,0))continue;
        for(int i=0;i<wt;i++)full[i]=db[i];
        if(usn_parse(full,wt,cv,&best)){ for(int i=0;i<wt;i++)bestc[i]=full[i]; bestn=wt; }
    }
    if(!best.kind)UFAIL("no network interface");
    if(!xh_ctrl(0x00,9,(u16)best.cfgval,0,0,0,0))UFAIL("SET_CONFIGURATION");
    usn.kind=best.kind; usn.ctlif=best.ctlif; usn.dataif=best.dataif; usn.in_ep=best.in_ep; usn.out_ep=best.out_ep; usn.in_mps=best.in_mps?best.in_mps:512; usn.out_mps=best.out_mps?best.out_mps:512;
    usn.ndiv=4; usn.nrem=0; usn.rxsz=usn.kind==1?2048:16384;
    if(usn.kind==2){
        if(xh_ctrl(0xA1,0x80,0,(u16)usn.ctlif,28,1,0)){ volatile u8* p=(volatile u8*)(u64)usn.dbuf; u32 inmax=(u32)p[4]|((u32)p[5]<<8)|((u32)p[6]<<16)|((u32)p[7]<<24); int dv=p[20]|(p[21]<<8), rm=p[22]|(p[23]<<8);
            if(dv>=4&&dv<=256){ usn.ndiv=dv; usn.nrem=rm%dv; }
            if(inmax>16384u){ volatile u8* w=(volatile u8*)(u64)usn.dbuf; w[0]=0; w[1]=0x40; w[2]=0; w[3]=0; if(!xh_ctrl(0x21,0x86,0,(u16)usn.ctlif,4,0,0)){ usn.rxsz=inmax>32768u?65536:32768; klog("usb-net: NCM device keeps its own block size, using bigger buffers"); } } }
    }
    if(usn.kind!=3){ xh_ctrl(0x01,11,0,(u16)usn.dataif,0,0,0); if(best.alt)xh_ctrl(0x01,11,(u16)best.alt,(u16)usn.dataif,0,0,0); }
    else if(best.alt)xh_ctrl(0x01,11,(u16)best.alt,(u16)usn.dataif,0,0,0);
    usn.in_dci=(usn.in_ep&0x0F)*2+1; usn.out_dci=(usn.out_ep&0x0F)*2; int maxdci=usn.in_dci>usn.out_dci?usn.in_dci:usn.out_dci;
    ctx_zero(usn.inctx,(int)ctx_sz()*33/4);
    ic=(volatile u32*)(u64)usn.inctx; ic[1]=(1u<<0)|(1u<<usn.in_dci)|(1u<<usn.out_dci);
    sc=(volatile u32*)(u64)(usn.inctx+ctx_sz()); xh_slot_ctx(sc,L,maxdci);
    ring_init(usn.inring); ring_init(usn.outring); usn.in_enq=0; usn.in_cyc=1; usn.out_enq=0; usn.out_cyc=1;
    volatile u32* ei=(volatile u32*)(u64)(usn.inctx+ctx_sz()*(u32)(usn.in_dci+1)); ei[1]=(6u<<3)|(3u<<1)|((u32)usn.in_mps<<16); ei[2]=usn.inring|1u; ei[3]=0; ei[4]=1024;
    volatile u32* eo=(volatile u32*)(u64)(usn.inctx+ctx_sz()*(u32)(usn.out_dci+1)); eo[1]=(2u<<3)|(3u<<1)|((u32)usn.out_mps<<16); eo[2]=usn.outring|1u; eo[3]=0; eo[4]=1024;
    if(!xh_cmd_s(usn.inctx,0,0,12,(u32)usn.slot,&cc,&sl)){ xhci_failcc=cc; UFAIL("Configure Endpoint"); }
    usn.e0_enq=e0_enq; usn.e0_cyc=e0_cyc;
    int macok=0;
    if(usn.kind==3){
        if(usn_rndis(2,0,0,0,0,0)<0)UFAIL("RNDIS initialise");
        u8 m[8]; if(usn_rndis(4,0x01010101u,0,0,m,8)==6){ for(int i=0;i<6;i++)usn.mac[i]=m[i]; macok=!(m[0]&1); }
        u8 flt[4]={0x0F,0,0,0}; if(usn_rndis(5,0x0001010Eu,flt,4,0,0)<0)UFAIL("RNDIS packet filter");
    } else {
        if(best.imac&&usn_ctrl(0x80,6,(u16)(0x0300|best.imac),0x0409,255,1)){ u8 s[64]; volatile u8* db=(volatile u8*)(u64)usn.dbuf; for(int i=0;i<64;i++)s[i]=db[i]; macok=usn_strmac(s,64,usn.mac); }
        usn_ctrl(0x21,0x43,0x000E,(u16)usn.ctlif,0,0);
    }
    if(!macok){ u64 s=hr_ns()*0x9E3779B97F4A7C15ULL; for(int i=0;i<6;i++)usn.mac[i]=(u8)(s>>(8*i+3)); usn.mac[0]=(u8)((usn.mac[0]&0xFE)|0x02); klog("usb-net: no MAC address from the device, using a random local one"); }
    usn.ctl=g_xcur; usn.L=*L; usn.route=L->route; usn.depth=L->depth; usn_qh=usn_qt=0; usn.txout=0; usn.txi=0; usn.halt_in=usn.halt_out=0; usn.seq=0;
    usn.ok=1; usn.rxon=1;
    usn_nrx=(int)(0x20000u/(u32)usn.rxsz); if(usn_nrx>UN_NRX)usn_nrx=UN_NRX;
    for(int i=0;i<usn_nrx;i++)usn_arm(usn.rxbase+(u32)(i*usn.rxsz));
    usbdev_note(L->root,usn.vid,usn.pid,bestc,bestn,"USB network adapter");
    struct nvx_nic n; n.name=usn.kind==1?"USB Ethernet (CDC-ECM)":usn.kind==2?"USB Ethernet (CDC-NCM)":"USB RNDIS (phone or gadget)"; for(int i=0;i<6;i++)n.mac[i]=usn.mac[i]; n.send=usn_send; n.recv=usn_recv; n.link=usn_link;
    usn.nicidx=nic_add(&n);
    { char b[96],*p=b; p=scopy(p,"usb-net: "); p=scopy(p,usn_kname(usn.kind)); p=scopy(p," adapter "); p=hx(p,usn.vid,4); *p++=':'; p=hx(p,usn.pid,4); p=scopy(p," up"); *p=0; klog(b); }
    if(usn.nicidx>=0)nic_offer(usn.nicidx);
    return 1;
}
#undef UFAIL
static int usn_enum_root(int port){
    int sp=xh_root_reset(port); if(!sp)return 0;
    usbloc_t L; L.root=port; L.depth=0; L.hub=-1; L.hport=0; L.speed=sp; L.route=0; L.tt_slot=0; L.tt_port=0;
    return usn_enum_loc(&L);
}
static void usn_gone(void){
    if(!usn.ok)return;
    usn.ok=0; usn.rxon=0; if(usn.slot){ xh_disable_slot(usn.slot); usn.slot=0; }
    klog("usb-net: adapter removed");
    if(usn.nicidx>=0)nic_drop(usn.nicidx);
    usn.nicidx=-1;
}
static int usn_claimable(int c,int p){
    if(msd_ok&&msd_ctl==c&&msd_port==p)return 0;
    if(cam.ok&&cam.ctl==c&&cam.root==p)return 0;
    if(aud.ok&&aud.ctl==c&&aud.root==p)return 0;
    if(usn.ok&&usn.ctl==c&&usn.L.root==p)return 0;
    if(btu_owns(c,p))return 0;
    for(int k=0;k<USBDEV_MAX;k++) if(g_usbdev[k].used&&g_usbdev[k].ctl==c&&g_usbdev[k].port==p){ int cl=g_usbdev[k].cls; if(g_usbdev[k].hid||g_usbdev[k].cam||g_usbdev[k].prn)return 0; if(cl!=0&&cl!=2&&cl!=0x0A&&cl!=0xE0&&cl!=0xEF&&cl!=0xFF)return 0; }
    for(int h=0;h<XHUB_MAX;h++) if(xhub[h].used&&xhub[h].ctl==c&&xhub[h].root==p)return 0;
    return 1;
}
int usbnet_scan(void){
    if(usn.ok)return 1; if(!g_xhc_n)return 0;
    int prev=g_xcur;
    for(int c=0;c<g_xhc_n&&!usn.ok;c++){ xhc_select(c); if(!xhci_present||!xhci_init_ok)continue; xhci_port_refresh();
        for(int p=0;p<xhci_ports&&!usn.ok;p++){ if(!xhci_port_conn[p]||!usn_claimable(c,p))continue;
            if(!usn_enum_root(p)){ if(usn.slot){ xh_disable_slot(usn.slot); usn.slot=0; } }
            else { usb_seen[p]=1; xhci_port_conn[p]=1; } } }
    xhc_select(prev);
    return usn.ok;
}
int usbnet_info(char* out,int cap){
    char b[200],*p=b;
    if(!usn.ok){ p=scopy(p,"no USB network adapter"); }
    else { p=scopy(p,usn_kname(usn.kind)); p=scopy(p," "); p=hx(p,usn.vid,4); *p++=':'; p=hx(p,usn.pid,4); p=scopy(p,"  frames in "); p=udec(p,usn.rxf); p=scopy(p," out "); p=udec(p,usn.txf); p=scopy(p,"  errors "); p=udec(p,usn.err); p=scopy(p," dropped "); p=udec(p,usn.drops); }
    p=scopy(p,"  (preferred: "); p=scopy(p,g_usn_pref?usn_kname(g_usn_pref):"automatic"); p=scopy(p,")"); *p=0;
    int i=0; for(;b[i]&&i<cap-1;i++)out[i]=b[i]; out[i]=0; return usn.ok;
}
int usbnet_prefer(int kind){
    if(kind<0||kind>3)return 0; g_usn_pref=kind;
    if(!usn.ok)return usbnet_scan();
    if(usn.depth>0)return 1;
    int c=usn.ctl, port=usn.L.root; int prev=g_xcur; xhc_select(c);
    usn_gone();
    int r=usn_enum_root(port); if(!r&&usn.slot){ xh_disable_slot(usn.slot); usn.slot=0; }
    xhc_select(prev);
    return r;
}
static int usn_active(void){ return usn.ok; }
static void usn_remove_below(int root,u32 R,int dR){ if(usn.ok&&usn.ctl==g_xcur&&usn.L.root==root&&xh_under(usn.route,usn.depth,R,dR))usn_gone(); }
static void usn_noslot(void){ if(!usn.ok&&usn.slot){ xh_disable_slot(usn.slot); usn.slot=0; } }
void usn_reset_all(void){ if(usn.ok){ usn.ok=0; usn.rxon=0; usn.slot=0; if(usn.nicidx>=0)nic_drop(usn.nicidx); usn.nicidx=-1; klog("usb-net: controller reset, adapter released"); } }
