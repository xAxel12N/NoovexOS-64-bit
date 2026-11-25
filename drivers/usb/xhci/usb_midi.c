#include "drivers/usb/xhci/usbmidi_codec.h"
#define UM_NRX 8
#define UM_RXSZ 512
#define UM_NTX 32
#define UM_TXSZ 64
#define UM_Q 1024
#define UM_MAX 4
#define UM_PORTS 16
struct umdev { int ok, ctl, slot, ifn, alt, in_ep, out_ep, in_dci, out_dci, in_mps, out_mps, txout, txi, halt_in, halt_out, depth, ncin, ncout;
    u32 mem, inctx, devctx, ep0, dbuf, inring, outring, in_enq, in_cyc, out_enq, out_cyc, rxbase, txbase, route, rxp, txp, err, drops, e0_enq, e0_cyc; u64 rec_t; u16 vid, pid; char name[40]; usbloc_t L; };
struct umport { int used, dev, cable, in, out; volatile u32 qw, qr; u32 seq; struct umc_enc enc; u8 q[UM_Q]; };
static struct umdev g_um[UM_MAX];
static struct umport g_ump[UM_PORTS];
u32 g_umidi_seq;
void msynth_event(u8 st,u8 d1,u8 d2);
static void um_rx_pkt(struct umdev* d,const u8* p){
    u8 b[3]; int n=umc_decode(p,b); if(n<=0)return; d->rxp++; g_umidi_seq++;
    if(b[0]>=0x80&&b[0]<0xF0)msynth_event(b[0],n>1?b[1]:0,n>2?b[2]:0);
    int di=(int)(d-g_um), cb=p[0]>>4;
    for(int k=0;k<UM_PORTS;k++){ struct umport* u=&g_ump[k]; if(!u->used||u->dev!=di||u->cable!=cb||!u->in)continue;
        u->seq++; for(int i=0;i<n;i++){ if(u->qw-u->qr>=UM_Q){ d->drops++; continue; } u->q[u->qw%UM_Q]=b[i]; u->qw++; } return; }
    d->drops++; }
static void um_trb(struct umdev* d,u32 ring,u32* enq,u32* cyc,u32 buf,int len,int dci){
    volatile u32* tr=(volatile u32*)(u64)(ring+(*enq)*16);
    tr[0]=buf; tr[1]=0; tr[2]=(u32)len; __sync_synchronize(); tr[3]=(1u<<10)|(1u<<5)|(*cyc);
    (*enq)++; if(*enq>=255){ volatile u32* lk=(volatile u32*)(u64)(ring+255*16); lk[3]=(lk[3]&~1u)|(*cyc); *enq=0; *cyc^=1; }
    __sync_synchronize(); xh_db(d->slot,(u32)dci); }
static void um_arm(struct umdev* d,u32 buf){ um_trb(d,d->inring,&d->in_enq,&d->in_cyc,buf,UM_RXSZ,d->in_dci); }
static int um_xfer(u8 sl,int ep,u8 cc,u32 res,u32 ptr){
    struct umdev* d=0; for(int k=0;k<UM_MAX;k++) if(g_um[k].ok&&g_um[k].ctl==g_xcur&&sl==(u8)g_um[k].slot){ d=&g_um[k]; break; }
    if(!d)return 0;
    if(ep==d->out_dci){ if(d->txout>0)d->txout--; if(cc!=1&&cc!=13){ d->err++; d->halt_out=1; } return 1; }
    if(ep!=d->in_dci)return 0;
    u32 buf=0; if(ptr>=d->inring&&ptr<d->inring+255*16)buf=((volatile u32*)(u64)ptr)[0];
    if(buf<d->rxbase||buf>=d->rxbase+(u32)(UM_NRX*UM_RXSZ)||((buf-d->rxbase)%UM_RXSZ)){ d->err++; return 1; }
    if(cc==1||cc==13){ int got=UM_RXSZ-(int)(res&0xFFFFFF); if(got<0)got=0; if(got>UM_RXSZ)got=UM_RXSZ; const u8* p=(const u8*)(u64)buf; for(int i=0;i+3<got;i+=4)um_rx_pkt(d,p+i); if(!d->halt_in)um_arm(d,buf); }
    else { d->err++; d->halt_in=1; }
    return 1; }
static int um_ctrl(struct umdev* d,u8 rt,u8 req,u16 val,u16 idx,u16 len,int in){
    int s_slot=cur_slot; u32 s_ep0=cur_ep0, s_db=cur_dbuf, s_enq=e0_enq, s_cyc=e0_cyc;
    cur_slot=d->slot; cur_ep0=d->ep0; cur_dbuf=d->dbuf; e0_enq=d->e0_enq; e0_cyc=d->e0_cyc;
    int r=xh_ctrl(rt,req,val,idx,len,in,0);
    d->e0_enq=e0_enq; d->e0_cyc=e0_cyc;
    cur_slot=s_slot; cur_ep0=s_ep0; cur_dbuf=s_db; e0_enq=s_enq; e0_cyc=s_cyc; return r; }
static void um_unhalt(struct umdev* d,int dci,int epaddr,u32 ring,u32* enq,u32* cyc){
    u8 cc=0,sl=0;
    xh_cmd_s(0,0,0,14u|((u32)dci<<6),(u32)d->slot,&cc,&sl);
    xh_cmd_s((ring+(*enq)*16)|(*cyc),0,0,16u|((u32)dci<<6),(u32)d->slot,&cc,&sl);
    um_ctrl(d,0x02,1,0,(u16)epaddr,0,0); }
static void um_gone(struct umdev* d);
static void um_recover(struct umdev* d){
    if(d->L.depth==0){ u32 pv=XMR(xhci_op+0x400+(u32)d->L.root*0x10); if(!(pv&1)){ um_gone(d); return; } }
    if(hr_ns()-d->rec_t<50000000ULL)return;
    d->rec_t=hr_ns();
    if(d->halt_in){ d->halt_in=0; um_unhalt(d,d->in_dci,d->in_ep,d->inring,&d->in_enq,&d->in_cyc); for(int i=0;i<UM_NRX;i++)um_arm(d,d->rxbase+(u32)(i*UM_RXSZ)); klog("midi: receive endpoint recovered"); }
    if(d->halt_out){ d->halt_out=0; um_unhalt(d,d->out_dci,d->out_ep,d->outring,&d->out_enq,&d->out_cyc); d->txout=0; klog("midi: send endpoint recovered"); } }
static int um_any(void){ for(int k=0;k<UM_MAX;k++) if(g_um[k].ok)return 1; return 0; }
void umidi_poll(void){
    if(!um_any())return;
    if(xh_wdepth>0||__sync_lock_test_and_set(&xh_poll_busy,1))return;
    xh_poll_t0=g_ticks;
    int prev=g_xcur, done=0;
    for(int k=0;k<UM_MAX;k++){ struct umdev* d=&g_um[k]; if(!d->ok)continue;
        if(!((done>>d->ctl)&1)){ done|=1<<d->ctl; xhc_select(d->ctl); xhci_poll_one(); }
        if(d->ok&&(d->halt_in||d->halt_out)){ xhc_select(d->ctl); um_recover(d); } }
    xhc_select(prev);
    __sync_lock_release(&xh_poll_busy); }
static int um_mem(struct umdev* d){
    if(d->mem)return 1;
    u8* p=(u8*)dma_alloc_pages(16); if(!p){ klog("midi: no memory"); return 0; }
    d->mem=(u32)(u64)p; d->inctx=d->mem; d->devctx=d->mem+0x1000u; d->ep0=d->mem+0x2000u; d->inring=d->mem+0x3000u; d->outring=d->mem+0x4000u; d->dbuf=d->mem+0x5000u; d->txbase=d->mem+0x6000u; d->rxbase=d->mem+0x8000u;
    return 1; }
int umidi_has(const u8* c,int n){ for(int i=0;i+7<n;){ int bl=c[i]; if(bl<2)break; if(c[i+1]==4&&bl>=9&&c[i+5]==1&&c[i+6]==3)return 1; i+=bl; } return 0; }
static int um_free_ports(void){ int f=0; for(int k=0;k<UM_PORTS;k++) if(!g_ump[k].used)f++; return f; }
static int um_add_ports(int di,int ncin,int ncout,int hin,int hout){
    int nc=ncin>ncout?ncin:ncout, first=-1; if(nc<1)nc=1;
    for(int c=0;c<nc;c++){ int k=0; while(k<UM_PORTS&&g_ump[k].used)k++; if(k>=UM_PORTS)break;
        struct umport* u=&g_ump[k]; u->qw=u->qr=0; u->seq=0; u->dev=di; u->cable=c; u->in=hin&&c<ncin; u->out=hout&&c<ncout; umc_init(&u->enc,(u8)c); u->used=1; if(first<0)first=k; }
    return first; }
static struct umdev* um_slot_free(void){ for(int k=0;k<UM_MAX;k++) if(!g_um[k].ok)return &g_um[k]; return 0; }
#define MFAIL(m) do{ xh_set_fail(m); d->ok=0; return 0; }while(0)
static int um_enum_loc(const usbloc_t* L){
    struct umdev* d=um_slot_free(); if(!d||!um_free_ports()||!um_mem(d))return 0;
    xhci_failstep[0]=0; xhci_failcc=0; d->slot=0;
    int speed=L->speed; int mps0=(speed==3)?64:(speed>=4)?512:8;
    cur_ep0=d->ep0; cur_dbuf=d->dbuf; e0_enq=0; e0_cyc=1; ring_init(d->ep0);
    u8 cc=0,sl=0; if(!xh_cmd_s(0,0,0,9,0,&cc,&sl)||!sl){ xhci_failcc=cc; MFAIL("Enable Slot"); }
    cur_slot=sl; d->slot=sl;
    ctx_zero(d->devctx,(int)ctx_sz()*32/4); *(volatile u32*)(u64)(XHCI_DCBAA+sl*8)=d->devctx; *(volatile u32*)(u64)(XHCI_DCBAA+sl*8+4)=0;
    ctx_zero(d->inctx,(int)ctx_sz()*33/4); volatile u32* ic=(volatile u32*)(u64)d->inctx; ic[1]=(1u<<0)|(1u<<1);
    volatile u32* sc=(volatile u32*)(u64)(d->inctx+ctx_sz()); xh_slot_ctx(sc,L,1);
    volatile u32* e0=(volatile u32*)(u64)(d->inctx+ctx_sz()*2); e0[1]=(4u<<3)|(3u<<1)|((u32)mps0<<16); e0[2]=d->ep0|1u; e0[3]=0; e0[4]=8;
    if(!xh_cmd_s(d->inctx,0,0,11,(u32)sl,&cc,&sl)){ xhci_failcc=cc; MFAIL("Address Device"); }
    xh_delay_ms(10);
    if(!xh_fix_mps0(d->inctx,L,d->ep0))MFAIL("GET device descriptor (8)");
    if(!xh_ctrl(0x80,6,0x0100,0,18,1,0))MFAIL("GET device descriptor");
    u8 dd[18]; { volatile u8* db=(volatile u8*)(u64)d->dbuf; for(int i=0;i<18;i++)dd[i]=db[i]; }
    if(dd[0]<18||dd[1]!=1)MFAIL("bad device descriptor");
    d->vid=(u16)(dd[8]|(dd[9]<<8)); d->pid=(u16)(dd[10]|(dd[11]<<8)); int ip=dd[15];
    if(!xh_ctrl(0x80,6,0x0200,0,9,1,0))MFAIL("GET config(9)");
    static u8 full[1024]; volatile u8* db=(volatile u8*)(u64)d->dbuf; int wt=db[2]|(db[3]<<8), cv=db[5]; if(wt<9||db[1]!=2)MFAIL("bad config"); if(wt>1024)wt=1024;
    if(!xh_ctrl(0x80,6,0x0200,0,(u16)wt,1,0))MFAIL("GET config(full)");
    for(int i=0;i<wt;i++)full[i]=db[i];
    usbreg_probe(L,full,wt);
    struct umc_cfg cf;
    if(!umc_parse_cfg(full,wt,&cf))MFAIL("no MIDI streaming interface");
    d->name[0]=0;
    if(ip&&xh_ctrl(0x80,6,(u16)(0x0300|ip),0x0409,64,1,0)){ int ln=db[0], o=0; if(ln>64)ln=64; for(int q=2;q+1<ln&&o<39;q+=2){ u8 ch=db[q]; if(db[q+1]||ch<32||ch>126)ch='?'; d->name[o++]=(char)ch; } d->name[o]=0; }
    if(!xh_ctrl(0x00,9,(u16)cv,0,0,0,0))MFAIL("SET_CONFIGURATION");
    if(cf.alt)xh_ctrl(0x01,11,(u16)cf.alt,(u16)cf.ifn,0,0,0);
    d->ifn=cf.ifn; d->alt=cf.alt; d->in_ep=cf.ie; d->out_ep=cf.oe; d->in_mps=cf.im; d->out_mps=cf.om; d->ncin=cf.ncin; d->ncout=cf.ncout;
    d->in_dci=cf.ie?(cf.ie&0x0F)*2+1:0; d->out_dci=cf.oe?(cf.oe&0x0F)*2:0; int maxdci=d->in_dci>d->out_dci?d->in_dci:d->out_dci;
    ctx_zero(d->inctx,(int)ctx_sz()*33/4);
    ic=(volatile u32*)(u64)d->inctx; ic[1]=(1u<<0)|(d->in_dci?(1u<<d->in_dci):0)|(d->out_dci?(1u<<d->out_dci):0);
    sc=(volatile u32*)(u64)(d->inctx+ctx_sz()); xh_slot_ctx(sc,L,maxdci);
    ring_init(d->inring); ring_init(d->outring); d->in_enq=0; d->in_cyc=1; d->out_enq=0; d->out_cyc=1;
    if(d->in_dci){ volatile u32* ei=(volatile u32*)(u64)(d->inctx+ctx_sz()*(u32)(d->in_dci+1)); ei[1]=(6u<<3)|(3u<<1)|((u32)d->in_mps<<16); ei[2]=d->inring|1u; ei[3]=0; ei[4]=64; }
    if(d->out_dci){ volatile u32* eo=(volatile u32*)(u64)(d->inctx+ctx_sz()*(u32)(d->out_dci+1)); eo[1]=(2u<<3)|(3u<<1)|((u32)d->out_mps<<16); eo[2]=d->outring|1u; eo[3]=0; eo[4]=64; }
    if(!xh_cmd_s(d->inctx,0,0,12,(u32)d->slot,&cc,&sl)){ xhci_failcc=cc; MFAIL("Configure Endpoint"); }
    d->e0_enq=e0_enq; d->e0_cyc=e0_cyc;
    d->ctl=g_xcur; d->L=*L; d->route=L->route; d->depth=L->depth; d->txout=0; d->txi=0; d->halt_in=d->halt_out=0; d->rxp=d->txp=d->err=d->drops=0;
    int first=um_add_ports((int)(d-g_um),cf.ncin,cf.ncout,d->in_dci!=0,d->out_dci!=0);
    d->ok=1;
    if(d->in_dci) for(int i=0;i<UM_NRX;i++)um_arm(d,d->rxbase+(u32)(i*UM_RXSZ));
    usbdev_note(L->root,d->vid,d->pid,full,wt,d->name[0]?d->name:"USB MIDI device");
    { char b[112],*p=b; p=scopy(p,"midi: "); p=scopy(p,d->name[0]?d->name:"USB MIDI device"); *p++=' '; p=hx(p,d->vid,4); *p++=':'; p=hx(p,d->pid,4); p=scopy(p," attached as /dev/midi"); p=udec(p,(u32)first);
        int nc=cf.ncin>cf.ncout?cf.ncin:cf.ncout; if(nc>1){ p=scopy(p," ("); p=udec(p,(u32)nc); p=scopy(p," cables)"); } *p=0; klog(b); }
    return 1; }
#undef MFAIL
static int um_enum_root(int port){
    int sp=xh_root_reset(port); if(!sp)return 0;
    usbloc_t L; L.root=port; L.depth=0; L.hub=-1; L.hport=0; L.speed=sp; L.route=0; L.tt_slot=0; L.tt_port=0;
    return um_enum_loc(&L); }
static void um_gone(struct umdev* d){
    if(!d->ok)return;
    d->ok=0; if(d->slot){ xh_disable_slot(d->slot); d->slot=0; }
    int di=(int)(d-g_um); for(int k=0;k<UM_PORTS;k++) if(g_ump[k].used&&g_ump[k].dev==di)g_ump[k].used=0;
    msynth_event(0xB0,123,0); klog("midi: device removed"); }
static int umidi_owns(int c,int p){ for(int k=0;k<UM_MAX;k++) if(g_um[k].ok&&g_um[k].ctl==c&&g_um[k].L.root==p)return 1; return 0; }
static int um_has_audio(int c,int p){ for(int k=0;k<USBDEV_MAX;k++) if(g_usbdev[k].used&&g_usbdev[k].ctl==c&&g_usbdev[k].port==p&&g_usbdev[k].cls==1)return 1; return 0; }
static int um_claimable(int c,int p){
    if(msd_ok&&msd_ctl==c&&msd_port==p)return 0;
    if(cam.ok&&cam.ctl==c&&cam.root==p)return 0;
    if(aud.ok&&aud.ctl==c&&aud.root==p)return 0;
    if(usn.ok&&usn.ctl==c&&usn.L.root==p)return 0;
    if(umidi_owns(c,p)||btu_owns(c,p))return 0;
    for(int k=0;k<USBDEV_MAX;k++) if(g_usbdev[k].used&&g_usbdev[k].ctl==c&&g_usbdev[k].port==p){ if(g_usbdev[k].hid||g_usbdev[k].cam||g_usbdev[k].prn)return 0; if(g_usbdev[k].cls!=1)return 0; }
    for(int h=0;h<XHUB_MAX;h++) if(xhub[h].used&&xhub[h].ctl==c&&xhub[h].root==p)return 0;
    return 1; }
static void um_noslot(void){ for(int k=0;k<UM_MAX;k++){ struct umdev* d=&g_um[k]; if(!d->ok&&d->slot){ xh_disable_slot(d->slot); d->slot=0; } } }
int sysfeat_get(int f);
int umidi_scan(void){
    if(!g_xhc_n||!sysfeat_get(1))return um_any();
    if(xh_wdepth>0||__sync_lock_test_and_set(&xh_poll_busy,1))return um_any();
    int prev=g_xcur;
    for(int c=0;c<g_xhc_n&&um_slot_free();c++){ xhc_select(c); if(!xhci_present||!xhci_init_ok)continue; xhci_port_refresh();
        for(int p=0;p<xhci_ports&&um_slot_free();p++){ if(!xhci_port_conn[p]||!um_claimable(c,p)||!um_has_audio(c,p))continue;
            if(!um_enum_root(p))um_noslot();
            else { usb_seen[p]=1; xhci_port_conn[p]=1; } } }
    xhc_select(prev);
    __sync_lock_release(&xh_poll_busy);
    return um_any(); }
int umidi_ports(void){ int n=0; for(int k=0;k<UM_PORTS;k++) if(g_ump[k].used)n=k+1; return n; }
int umidi_port_ok(int k){ return k>=0&&k<UM_PORTS&&g_ump[k].used&&g_um[g_ump[k].dev].ok; }
int umidi_port_caps(int k){ if(!umidi_port_ok(k))return 0; return (g_ump[k].in?1:0)|(g_ump[k].out?2:0); }
static int um_first(void){ for(int k=0;k<UM_PORTS;k++) if(umidi_port_ok(k))return k; return -1; }
int umidi_present(void){ return um_first()>=0; }
int umidi_port_name(int k,char* out,int cap){
    if(cap<=0)return 0;
    out[0]=0; if(!umidi_port_ok(k))return 0;
    struct umdev* d=&g_um[g_ump[k].dev]; const char* nm=d->name[0]?d->name:"USB MIDI device"; int i=0;
    for(;nm[i]&&i<cap-1;i++)out[i]=nm[i];
    int nc=d->ncin>d->ncout?d->ncin:d->ncout; if(nc>1&&i+8<cap){ const char* s=" MIDI "; for(int j=0;s[j];j++)out[i++]=s[j]; int c=g_ump[k].cable+1; if(c>=10)out[i++]=(char)('0'+c/10); out[i++]=(char)('0'+c%10); }
    out[i]=0; return i; }
int umidi_port_read(int k,u8* out,int max){
    if(k<0||k>=UM_PORTS||!g_ump[k].used)return -1;
    struct umport* u=&g_ump[k];
    if(u->qw==u->qr){ if(!g_um[u->dev].ok)return -1; umidi_poll(); }
    int n=0; while(n<max&&u->qr!=u->qw){ out[n++]=u->q[u->qr%UM_Q]; u->qr++; } return n; }
int umidi_port_avail(int k){ if(k<0||k>=UM_PORTS||!g_ump[k].used)return -1; struct umport* u=&g_ump[k]; if(u->qw==u->qr&&g_um[u->dev].ok)umidi_poll(); return (int)(u->qw-u->qr); }
void umidi_port_drop(int k){ if(k>=0&&k<UM_PORTS&&g_ump[k].used)g_ump[k].qr=g_ump[k].qw; }
static int um_tx_lock(void){
    u64 t0=hr_ns();
    while(__sync_lock_test_and_set(&xh_poll_busy,1)){ if(xh_wdepth>0||hr_ns()-t0>50000000ULL)return 0; __builtin_ia32_pause(); }
    return 1; }
int umidi_port_write(int k,const u8* s,int len){
    if(!umidi_port_ok(k)||!g_ump[k].out)return -1;
    struct umport* u=&g_ump[k]; struct umdev* d=&g_um[u->dev];
    if(!d->out_dci)return -1;
    int done=0;
    for(int i=0;i<len;i++){ u8 pk[4]; if(!umc_encode(&u->enc,s[i],pk)){ done++; continue; }
        umidi_poll();
        if(d->txout>=UM_NTX){ u64 t0=hr_ns(); while(d->ok&&d->txout>=UM_NTX&&hr_ns()-t0<100000000ULL)umidi_poll(); }
        if(!d->ok||d->txout>=UM_NTX||!um_tx_lock()){ d->err++; return done?done:-1; }
        u8* b=(u8*)(u64)(d->txbase+(u32)(d->txi*UM_TXSZ)); for(int q=0;q<4;q++)b[q]=pk[q];
        int prev=g_xcur; xhc_select(d->ctl); d->txout++; um_trb(d,d->outring,&d->out_enq,&d->out_cyc,(u32)(u64)b,4,d->out_dci); xhc_select(prev);
        __sync_lock_release(&xh_poll_busy);
        d->txi=(d->txi+1)%UM_NTX; d->txp++; done++; }
    return done; }
int umidi_read(u8* out,int max){ int k=um_first(); if(k<0)return -1; return umidi_port_read(k,out,max); }
int umidi_avail(void){ int k=um_first(); return k<0?0:umidi_port_avail(k); }
int umidi_write(const u8* s,int len){ for(int k=0;k<UM_PORTS;k++) if(umidi_port_ok(k)&&g_ump[k].out)return umidi_port_write(k,s,len); return -1; }
int umidi_info(char* out,int cap){
    if(cap<=0)return 0;
    char b[640],*p=b; int any=0;
    for(int k=0;k<UM_PORTS;k++){ if(!umidi_port_ok(k)||p>b+sizeof b-160)continue; struct umport* u=&g_ump[k]; struct umdev* d=&g_um[u->dev];
        if(any)*p++='\n';
        any=1;
        char nm[48]; umidi_port_name(k,nm,(int)sizeof nm);
        p=scopy(p,"midi"); p=udec(p,(u32)k); p=scopy(p,": "); p=scopy(p,nm); *p++=' '; p=hx(p,d->vid,4); *p++=':'; p=hx(p,d->pid,4); p=scopy(p,u->in&&u->out?"  in+out":u->in?"  in":"  out");
        if(u->cable==0){ p=scopy(p,"  packets in "); p=udec(p,d->rxp); p=scopy(p," out "); p=udec(p,d->txp); p=scopy(p,"  errors "); p=udec(p,d->err); p=scopy(p," dropped "); p=udec(p,d->drops); } }
    if(!any)p=scopy(p,"no USB MIDI device");
    *p=0; int i=0; for(;b[i]&&i<cap-1;i++)out[i]=b[i]; out[i]=0; return any; }
static int um_active(void){ return um_any(); }
static void um_remove_below(int root,u32 R,int dR){ for(int k=0;k<UM_MAX;k++){ struct umdev* d=&g_um[k]; if(d->ok&&d->ctl==g_xcur&&d->L.root==root&&xh_under(d->route,d->depth,R,dR))um_gone(d); } }
void um_reset_all(void){ int any=0; for(int k=0;k<UM_MAX;k++) if(g_um[k].ok){ g_um[k].ok=0; g_um[k].slot=0; for(int q=0;q<UM_PORTS;q++) if(g_ump[q].used&&g_ump[q].dev==k)g_ump[q].used=0; any=1; } if(any)klog("midi: controller reset, device released"); }
static int um_try_port(int p){
    if(!um_slot_free()||!um_claimable(g_xcur,p))return 0;
    if(!um_has_audio(g_xcur,p))return 0;
    if(um_enum_root(p))return 1;
    um_noslot(); return 0; }
static int um_hub_try(int hi,int p){
    if(!sysfeat_get(1)||!um_slot_free())return 0;
    int sp=xhub_port_reset(hi,p); if(!sp)return 0;
    usbloc_t C; xhub_child_loc(hi,p,sp,&C);
    if(um_enum_loc(&C)){ klog("usb: MIDI device behind a hub"); return 1; }
    um_noslot(); return 0; }
static void um_unplug(int p){ for(int k=0;k<UM_MAX;k++){ struct umdev* d=&g_um[k]; if(d->ok&&d->ctl==g_xcur&&d->L.root==p)um_gone(d); } }
