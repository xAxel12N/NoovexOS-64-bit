#include "drivers/usb/class/usbser_proto.h"
#define US_MAX 4
#define US_PORTS 8
#define US_NRX 8
#define US_RXSZ 512
#define US_NTX 8
#define US_TXSZ 512
#define US_Q 8192
struct usdev { int ok, ctl, slot, chip, ftf, nport, depth;
    u32 mem, inctx, devctx, ep0, dbuf, route, e0_enq, e0_cyc, err; u64 rec_t; u16 vid, pid, bcd; char name[40]; usbloc_t L; };
struct usport { int used, dev, sub, acm, num, in_dci, out_dci, halt_in, halt_out, txout, txi, dtr, rts, brk, mst, opens;
    u32 inring, outring, in_enq, in_cyc, out_enq, out_cyc, rxbase, txbase, gen, rxb, txb, drops; struct usr_port P; struct usr_line line; volatile u32 qw, qr; u8 q[US_Q]; };
static struct usdev g_usd[US_MAX];
static struct usport g_usp[US_PORTS];
static u32 g_us_gen;
int sysfeat_get(int f);
int sysfeat_state(int f);
void sysfeat_fail(int f,const char* why);
static void us_trb(struct usdev* d,u32 ring,u32* enq,u32* cyc,u32 buf,int len,int dci){
    volatile u32* tr=(volatile u32*)(u64)(ring+(*enq)*16);
    tr[0]=buf; tr[1]=0; tr[2]=(u32)len; __sync_synchronize(); tr[3]=(1u<<10)|(1u<<5)|(*cyc);
    (*enq)++; if(*enq>=255){ volatile u32* lk=(volatile u32*)(u64)(ring+255*16); lk[3]=(lk[3]&~1u)|(*cyc); *enq=0; *cyc^=1; }
    __sync_synchronize(); xh_db(d->slot,(u32)dci); }
static void us_arm(struct usdev* d,struct usport* u,u32 buf){ us_trb(d,u->inring,&u->in_enq,&u->in_cyc,buf,US_RXSZ,u->in_dci); }
static void us_rx(struct usdev* d,struct usport* u,const u8* p,int got){
    u8 t[US_RXSZ]; int n=got;
    if(d->chip==USR_FTDI){ int st=-1; n=usr_ftdi_strip(p,got,u->P.im,t,&st); if(st>=0){ u8 sb=(u8)st; u->mst=usr_modem_in(USR_FTDI,&sb,1); } p=t; }
    for(int i=0;i<n;i++){ if(u->qw-u->qr>=US_Q){ u->drops++; continue; } u->q[u->qw%US_Q]=p[i]; u->qw++; }
    u->rxb+=(u32)n; }
static int us_xfer(u8 sl,int ep,u8 cc,u32 res,u32 ptr){
    struct usdev* d=0; for(int k=0;k<US_MAX;k++) if(g_usd[k].ok&&g_usd[k].ctl==g_xcur&&sl==(u8)g_usd[k].slot){ d=&g_usd[k]; break; }
    if(!d)return 0;
    int di=(int)(d-g_usd);
    for(int k=0;k<US_PORTS;k++){ struct usport* u=&g_usp[k]; if(!u->used||u->dev!=di)continue;
        if(ep==u->out_dci){ if(u->txout>0)u->txout--; if(cc!=1&&cc!=13){ d->err++; u->halt_out=1; } return 1; }
        if(ep!=u->in_dci)continue;
        u32 buf=0; if(ptr>=u->inring&&ptr<u->inring+255*16)buf=((volatile u32*)(u64)ptr)[0];
        if(buf<u->rxbase||buf>=u->rxbase+(u32)(US_NRX*US_RXSZ)||((buf-u->rxbase)%US_RXSZ)){ d->err++; return 1; }
        if(cc==1||cc==13){ int got=US_RXSZ-(int)(res&0xFFFFFF); if(got<0)got=0; if(got>US_RXSZ)got=US_RXSZ; us_rx(d,u,(const u8*)(u64)buf,got); if(!u->halt_in)us_arm(d,u,buf); }
        else { d->err++; u->halt_in=1; }
        return 1; }
    return 1; }
static int us_ctrl(struct usdev* d,u8 rt,u8 req,u16 val,u16 idx,u16 len,int in){
    int s_slot=cur_slot; u32 s_ep0=cur_ep0, s_db=cur_dbuf, s_enq=e0_enq, s_cyc=e0_cyc;
    cur_slot=d->slot; cur_ep0=d->ep0; cur_dbuf=d->dbuf; e0_enq=d->e0_enq; e0_cyc=d->e0_cyc;
    int r=xh_ctrl(rt,req,val,idx,len,in,0);
    d->e0_enq=e0_enq; d->e0_cyc=e0_cyc;
    cur_slot=s_slot; cur_ep0=s_ep0; cur_dbuf=s_db; e0_enq=s_enq; e0_cyc=s_cyc; return r; }
static int us_req(struct usdev* d,const struct usr_req* q){
    int in=(q->rt&0x80)!=0; volatile u8* b=(volatile u8*)(u64)d->dbuf;
    if(!in) for(int i=0;i<q->len&&i<8;i++)b[i]=q->data[i];
    return us_ctrl(d,q->rt,q->req,q->val,q->idx,q->len,in); }
static int us_reqs(struct usdev* d,const struct usr_req* q,int n){ int ok=1; for(int i=0;i<n;i++) if(!us_req(d,&q[i])){ ok=0; if(!(q[i].rt&0x80))break; } return ok; }
static void us_unhalt(struct usdev* d,int dci,int epaddr,u32 ring,u32* enq,u32* cyc){
    u8 cc=0,sl=0;
    xh_cmd_s(0,0,0,14u|((u32)dci<<6),(u32)d->slot,&cc,&sl);
    xh_cmd_s((ring+(*enq)*16)|(*cyc),0,0,16u|((u32)dci<<6),(u32)d->slot,&cc,&sl);
    us_ctrl(d,0x02,1,0,(u16)epaddr,0,0); }
static void us_gone(struct usdev* d);
static void us_recover(struct usdev* d){
    if(d->L.depth==0){ u32 pv=XMR(xhci_op+0x400+(u32)d->L.root*0x10); if(!(pv&1)){ us_gone(d); return; } }
    if(hr_ns()-d->rec_t<50000000ULL)return;
    d->rec_t=hr_ns(); int di=(int)(d-g_usd);
    for(int k=0;k<US_PORTS;k++){ struct usport* u=&g_usp[k]; if(!u->used||u->dev!=di)continue;
        if(u->halt_in){ u->halt_in=0; us_unhalt(d,u->in_dci,u->P.ie,u->inring,&u->in_enq,&u->in_cyc); for(int i=0;i<US_NRX;i++)us_arm(d,u,u->rxbase+(u32)(i*US_RXSZ)); klog("serial: receive endpoint recovered"); }
        if(u->halt_out){ u->halt_out=0; us_unhalt(d,u->out_dci,u->P.oe,u->outring,&u->out_enq,&u->out_cyc); u->txout=0; klog("serial: send endpoint recovered"); } } }
static int us_any(void){ for(int k=0;k<US_MAX;k++) if(g_usd[k].ok)return 1; return 0; }
static int us_halted(int di){ for(int k=0;k<US_PORTS;k++) if(g_usp[k].used&&g_usp[k].dev==di&&(g_usp[k].halt_in||g_usp[k].halt_out))return 1; return 0; }
void usbser_poll(void){
    if(!us_any())return;
    if(xh_wdepth>0||__sync_lock_test_and_set(&xh_poll_busy,1))return;
    xh_poll_t0=g_ticks;
    int prev=g_xcur, done=0;
    for(int k=0;k<US_MAX;k++){ struct usdev* d=&g_usd[k]; if(!d->ok)continue;
        if(!((done>>d->ctl)&1)){ done|=1<<d->ctl; xhc_select(d->ctl); xhci_poll_one(); }
        if(d->ok&&us_halted(k)){ xhc_select(d->ctl); us_recover(d); } }
    xhc_select(prev);
    __sync_lock_release(&xh_poll_busy); }
static int us_lock(void){
    u64 t0=hr_ns();
    while(__sync_lock_test_and_set(&xh_poll_busy,1)){ if(xh_wdepth>0||hr_ns()-t0>50000000ULL)return 0; __builtin_ia32_pause(); }
    return 1; }
static int us_mem(struct usdev* d){
    if(d->mem)return 1;
    u8* p=(u8*)dma_alloc_pages(4); if(!p){ klog("serial: no memory"); return 0; }
    d->mem=(u32)(u64)p; d->inctx=d->mem; d->devctx=d->mem+0x1000u; d->ep0=d->mem+0x2000u; d->dbuf=d->mem+0x3000u;
    return 1; }
static int us_port_mem(struct usport* u){
    if(u->inring)return 1;
    u8* p=(u8*)dma_alloc_pages(4); if(!p)return 0;
    u32 b=(u32)(u64)p; u->inring=b; u->outring=b+0x1000u; u->rxbase=b+0x2000u; u->txbase=b+0x3000u; return 1; }
static int us_free_ports(void){ int f=0; for(int k=0;k<US_PORTS;k++) if(!g_usp[k].used)f++; return f; }
static struct usdev* us_slot_free(void){ for(int k=0;k<US_MAX;k++) if(!g_usd[k].ok)return &g_usd[k]; return 0; }
static int us_num(int acm){ for(int n=0;n<US_PORTS;n++){ int used=0; for(int k=0;k<US_PORTS;k++) if(g_usp[k].used&&g_usp[k].acm==acm&&g_usp[k].num==n)used=1; if(!used)return n; } return -1; }
static void us_unclaim(int di){ for(int k=0;k<US_PORTS;k++) if(g_usp[k].used==2&&g_usp[k].dev==di)g_usp[k].used=0; }
#define SFAIL(m) do{ xh_set_fail(m); us_unclaim((int)(d-g_usd)); d->ok=0; return 0; }while(0)
static int us_enum_loc(const usbloc_t* L){
    struct usdev* d=us_slot_free(); if(!d||!us_free_ports()||!us_mem(d))return 0;
    xhci_failstep[0]=0; xhci_failcc=0; d->slot=0;
    int speed=L->speed; int mps0=(speed==3)?64:(speed>=4)?512:8;
    cur_ep0=d->ep0; cur_dbuf=d->dbuf; e0_enq=0; e0_cyc=1; ring_init(d->ep0);
    u8 cc=0,sl=0; if(!xh_cmd_s(0,0,0,9,0,&cc,&sl)||!sl){ xhci_failcc=cc; SFAIL("Enable Slot"); }
    cur_slot=sl; d->slot=sl;
    ctx_zero(d->devctx,(int)ctx_sz()*32/4); *(volatile u32*)(u64)(XHCI_DCBAA+sl*8)=d->devctx; *(volatile u32*)(u64)(XHCI_DCBAA+sl*8+4)=0;
    ctx_zero(d->inctx,(int)ctx_sz()*33/4); volatile u32* ic=(volatile u32*)(u64)d->inctx; ic[1]=(1u<<0)|(1u<<1);
    volatile u32* sc=(volatile u32*)(u64)(d->inctx+ctx_sz()); xh_slot_ctx(sc,L,1);
    volatile u32* e0=(volatile u32*)(u64)(d->inctx+ctx_sz()*2); e0[1]=(4u<<3)|(3u<<1)|((u32)mps0<<16); e0[2]=d->ep0|1u; e0[3]=0; e0[4]=8;
    if(!xh_cmd_s(d->inctx,0,0,11,(u32)sl,&cc,&sl)){ xhci_failcc=cc; SFAIL("Address Device"); }
    xh_delay_ms(10);
    if(!xh_fix_mps0(d->inctx,L,d->ep0))SFAIL("GET device descriptor (8)");
    if(!xh_ctrl(0x80,6,0x0100,0,18,1,0))SFAIL("GET device descriptor");
    u8 dd[18]; { volatile u8* db=(volatile u8*)(u64)d->dbuf; for(int i=0;i<18;i++)dd[i]=db[i]; }
    if(dd[0]<18||dd[1]!=1)SFAIL("bad device descriptor");
    d->vid=(u16)(dd[8]|(dd[9]<<8)); d->pid=(u16)(dd[10]|(dd[11]<<8)); d->bcd=(u16)(dd[12]|(dd[13]<<8)); int ip=dd[15];
    if(!xh_ctrl(0x80,6,0x0200,0,9,1,0))SFAIL("GET config(9)");
    static u8 full[1024]; volatile u8* db=(volatile u8*)(u64)d->dbuf; int wt=db[2]|(db[3]<<8), cv=db[5]; if(wt<9||db[1]!=2)SFAIL("bad config"); if(wt>1024)wt=1024;
    if(!xh_ctrl(0x80,6,0x0200,0,(u16)wt,1,0))SFAIL("GET config(full)");
    for(int i=0;i<wt;i++)full[i]=db[i];
    int chip=usr_chip(d->vid,d->pid,full,wt); if(!chip)SFAIL("not a serial adapter");
    struct usr_cfg cf; if(!usr_parse(chip,full,wt,&cf))SFAIL("no serial data endpoints");
    if(cf.n>us_free_ports())cf.n=us_free_ports();
    d->chip=chip; d->ftf=chip==USR_FTDI?usr_ft_flags(d->bcd,cf.n):0; d->name[0]=0;
    if(ip&&xh_ctrl(0x80,6,(u16)(0x0300|ip),0x0409,64,1,0)){ int ln=db[0], o=0; if(ln>64)ln=64; for(int q=2;q+1<ln&&o<39;q+=2){ u8 ch=db[q]; if(db[q+1]||ch<32||ch>126)ch='?'; d->name[o++]=(char)ch; } d->name[o]=0; }
    if(!xh_ctrl(0x00,9,(u16)cv,0,0,0,0))SFAIL("SET_CONFIGURATION");
    int pk[USR_MAXIF], maxdci=1, di=(int)(d-g_usd); u32 add=1u;
    for(int i=0;i<cf.n;i++){ int k=0; while(k<US_PORTS&&g_usp[k].used)k++; pk[i]=k; struct usport* u=&g_usp[k]; if(!us_port_mem(u))SFAIL("no memory for serial port");
        u->P=cf.p[i]; u->in_dci=(cf.p[i].ie&0x0F)*2+1; u->out_dci=(cf.p[i].oe&0x0F)*2; if(u->in_dci>maxdci)maxdci=u->in_dci; if(u->out_dci>maxdci)maxdci=u->out_dci; add|=(1u<<u->in_dci)|(1u<<u->out_dci);
        u->dev=di; u->used=2; }
    ctx_zero(d->inctx,(int)ctx_sz()*33/4);
    ic=(volatile u32*)(u64)d->inctx; ic[1]=add;
    sc=(volatile u32*)(u64)(d->inctx+ctx_sz()); xh_slot_ctx(sc,L,maxdci);
    for(int i=0;i<cf.n;i++){ struct usport* u=&g_usp[pk[i]];
        ring_init(u->inring); ring_init(u->outring); u->in_enq=0; u->in_cyc=1; u->out_enq=0; u->out_cyc=1;
        volatile u32* ei=(volatile u32*)(u64)(d->inctx+ctx_sz()*(u32)(u->in_dci+1)); ei[1]=(6u<<3)|(3u<<1)|((u32)u->P.im<<16); ei[2]=u->inring|1u; ei[3]=0; ei[4]=US_RXSZ;
        volatile u32* eo=(volatile u32*)(u64)(d->inctx+ctx_sz()*(u32)(u->out_dci+1)); eo[1]=(2u<<3)|(3u<<1)|((u32)u->P.om<<16); eo[2]=u->outring|1u; eo[3]=0; eo[4]=US_TXSZ; }
    if(!xh_cmd_s(d->inctx,0,0,12,(u32)d->slot,&cc,&sl)){ xhci_failcc=cc; SFAIL("Configure Endpoint"); }
    d->e0_enq=e0_enq; d->e0_cyc=e0_cyc;
    d->ctl=g_xcur; d->L=*L; d->route=L->route; d->depth=L->depth; d->err=0; d->nport=cf.n; d->ok=1;
    char b[128],*p=b; p=scopy(p,"serial: "); p=scopy(p,usr_chipname[chip]); *p++=' '; p=hx(p,d->vid,4); *p++=':'; p=hx(p,d->pid,4); if(d->name[0]){ p=scopy(p," \""); p=scopy(p,d->name); *p++='"'; } p=scopy(p," attached as");
    for(int i=0;i<cf.n;i++){ struct usport* u=&g_usp[pk[i]]; struct usr_req q[USR_MAXREQ];
        u->acm=chip==USR_ACM; u->num=us_num(u->acm); u->sub=i; u->halt_in=u->halt_out=0; u->txout=u->txi=0; u->dtr=u->rts=0; u->brk=0; u->mst=-1; u->opens=0; u->qw=u->qr=0; u->rxb=u->txb=u->drops=0; u->gen=++g_us_gen;
        u->line.baud=9600; u->line.bits=8; u->line.parity=0; u->line.stop=0;
        int n=usr_init_reqs(chip,&u->P,d->ftf,q); us_reqs(d,q,n);
        n=usr_line_reqs(chip,&u->P,d->ftf,&u->line,q); if(n>0)us_reqs(d,q,n);
        if(usr_mctl_req(chip,&u->P,d->ftf,0,0,q))us_req(d,q);
        u->used=1;
        for(int r=0;r<US_NRX;r++)us_arm(d,u,u->rxbase+(u32)(r*US_RXSZ));
        p=scopy(p,u->acm?" /dev/ttyACM":" /dev/ttyUSB"); p=udec(p,(u32)u->num); }
    *p=0; klog(b);
    usbdev_note(L->root,d->vid,d->pid,full,wt,d->name[0]?d->name:"USB serial adapter");
    return 1; }
#undef SFAIL
static int us_enum_root(int port){
    int sp=xh_root_reset(port); if(!sp)return 0;
    usbloc_t L; L.root=port; L.depth=0; L.hub=-1; L.hport=0; L.speed=sp; L.route=0; L.tt_slot=0; L.tt_port=0;
    return us_enum_loc(&L); }
static void us_gone(struct usdev* d){
    if(!d->ok)return;
    d->ok=0; if(d->slot){ xh_disable_slot(d->slot); d->slot=0; }
    int di=(int)(d-g_usd); for(int k=0;k<US_PORTS;k++) if(g_usp[k].used&&g_usp[k].dev==di)g_usp[k].used=0;
    klog("serial: adapter removed"); }
static int usbser_owns(int c,int p){ for(int k=0;k<US_MAX;k++) if(g_usd[k].ok&&g_usd[k].ctl==c&&g_usd[k].L.root==p)return 1; return 0; }
static int us_candidate(int c,int p){
    if(msd_ok&&msd_ctl==c&&msd_port==p)return 0;
    if(cam.ok&&cam.ctl==c&&cam.root==p)return 0;
    if(aud.ok&&aud.ctl==c&&aud.root==p)return 0;
    if(usn.ok&&usn.ctl==c&&usn.L.root==p)return 0;
    if(umidi_owns(c,p)||btu_owns(c,p)||usbser_owns(c,p))return 0;
    for(int h=0;h<XHUB_MAX;h++) if(xhub[h].used&&xhub[h].ctl==c&&xhub[h].root==p)return 0;
    int hit=0;
    for(int k=0;k<USBDEV_MAX;k++) if(g_usbdev[k].used&&g_usbdev[k].ctl==c&&g_usbdev[k].port==p){ if(g_usbdev[k].hid||g_usbdev[k].cam||g_usbdev[k].prn)return 0; if(usr_lookup(g_usbdev[k].vid,g_usbdev[k].pid)||g_usbdev[k].cls==2)hit=1; }
    return hit; }
static void us_noslot(void){ for(int k=0;k<US_MAX;k++){ struct usdev* d=&g_usd[k]; if(!d->ok&&d->slot){ xh_disable_slot(d->slot); d->slot=0; } } }
static int us_on(void){ return sysfeat_get(SYSF_USBSER)==1&&sysfeat_state(SYSF_USBSER)!=SYSF_ST_FAILED; }
int usbser_scan(void){
    if(!g_xhc_n||!us_on())return us_any();
    if(xh_wdepth>0||__sync_lock_test_and_set(&xh_poll_busy,1))return us_any();
    int prev=g_xcur;
    for(int c=0;c<g_xhc_n&&us_slot_free();c++){ xhc_select(c); if(!xhci_present||!xhci_init_ok)continue; xhci_port_refresh();
        for(int p=0;p<xhci_ports&&us_slot_free();p++){ if(!xhci_port_conn[p]||!us_candidate(c,p))continue;
            if(!us_enum_root(p))us_noslot();
            else { usb_seen[p]=1; xhci_port_conn[p]=1; } } }
    xhc_select(prev);
    __sync_lock_release(&xh_poll_busy);
    return us_any(); }
static int us_active(void){ return us_any(); }
void us_reset_all(void){ int any=0; for(int k=0;k<US_MAX;k++) if(g_usd[k].ok){ g_usd[k].ok=0; g_usd[k].slot=0; for(int q=0;q<US_PORTS;q++) if(g_usp[q].used&&g_usp[q].dev==k)g_usp[q].used=0; any=1; } if(any)klog("serial: controller reset, adapter released"); }
static int us_try_port(int p){
    if(!us_on()||!us_slot_free()||!us_free_ports()||!us_candidate(g_xcur,p))return 0;
    if(us_enum_root(p))return 1;
    us_noslot(); return 0; }
static void us_unplug(int p){ for(int k=0;k<US_MAX;k++){ struct usdev* d=&g_usd[k]; if(d->ok&&d->ctl==g_xcur&&d->L.root==p)us_gone(d); } }
int usbser_ok(int k){ return k>=0&&k<US_PORTS&&g_usp[k].used==1&&g_usd[g_usp[k].dev].ok; }
int usbser_find(int acm,int num){ for(int k=0;k<US_PORTS;k++) if(usbser_ok(k)&&g_usp[k].acm==acm&&g_usp[k].num==num)return k; return -1; }
int usbser_list(int i,int* acm,int* num){ int c=0; for(int k=0;k<US_PORTS;k++){ if(!usbser_ok(k))continue; if(c==i){ *acm=g_usp[k].acm; *num=g_usp[k].num; return k; } c++; } return -1; }
u32 usbser_gen(int k){ return usbser_ok(k)?g_usp[k].gen:0; }
static int us_ctl_run(struct usport* u,const struct usr_req* q,int n){
    struct usdev* d=&g_usd[u->dev]; if(!us_lock())return 0;
    int prev=g_xcur; xhc_select(d->ctl); int r=us_reqs(d,q,n); xhc_select(prev);
    __sync_lock_release(&xh_poll_busy); return r; }
int usbser_set_line(int k,u32 baud,int bits,int parity,int stop){
    if(!usbser_ok(k))return -1;
    struct usport* u=&g_usp[k]; struct usdev* d=&g_usd[u->dev]; struct usr_line l; l.baud=baud; l.bits=(u8)bits; l.parity=(u8)parity; l.stop=(u8)stop;
    if(l.baud==u->line.baud&&l.bits==u->line.bits&&l.parity==u->line.parity&&l.stop==u->line.stop)return 0;
    struct usr_req q[USR_MAXREQ]; int n=usr_line_reqs(d->chip,&u->P,d->ftf,&l,q); if(n<0)return -2;
    if(!us_ctl_run(u,q,n)){ d->err++; return -3; }
    u->line=l; return 0; }
int usbser_get_line(int k,u32* baud,int* bits,int* parity,int* stop){ if(!usbser_ok(k))return -1; struct usport* u=&g_usp[k]; *baud=u->line.baud; *bits=u->line.bits; *parity=u->line.parity; *stop=u->line.stop; return 0; }
int usbser_mctl(int k,int dtr,int rts){
    if(!usbser_ok(k))return -1;
    struct usport* u=&g_usp[k]; struct usdev* d=&g_usd[u->dev]; struct usr_req q;
    if(!usr_mctl_req(d->chip,&u->P,d->ftf,dtr,rts,&q))return -2;
    if(!us_ctl_run(u,&q,1)){ d->err++; return -3; }
    u->dtr=dtr?1:0; u->rts=rts?1:0; return 0; }
int usbser_break(int k,int on){
    if(!usbser_ok(k))return -1;
    struct usport* u=&g_usp[k]; struct usdev* d=&g_usd[u->dev]; struct usr_req q; u8 r05=0xFF, r18=0xC3;
    if(d->chip==USR_CH341){ struct usr_req rq; usr_r(&rq,0xC0,0x95,0x1805,0,2); if(us_ctl_run(u,&rq,1)){ volatile u8* b=(volatile u8*)(u64)d->dbuf; r05=b[0]; r18=b[1]; } }
    if(!usr_break_req(d->chip,&u->P,d->ftf,&u->line,on,r05,r18,&q))return -2;
    if(!us_ctl_run(u,&q,1)){ d->err++; return -3; }
    u->brk=on?1:0; return 0; }
int usbser_modem(int k){
    if(!usbser_ok(k))return -1;
    struct usport* u=&g_usp[k]; struct usdev* d=&g_usd[u->dev]; int m=-1;
    if(d->chip==USR_CP210X||d->chip==USR_CH341){ struct usr_req q; if(d->chip==USR_CP210X)usr_r(&q,0xC1,0x08,0,u->P.ifn,1); else usr_r(&q,0xC0,0x95,0x0706,0,2);
        if(us_ctl_run(u,&q,1)){ u8 b[2]; volatile u8* s=(volatile u8*)(u64)d->dbuf; b[0]=s[0]; b[1]=s[1]; m=usr_modem_in(d->chip,b,2); } }
    else if(d->chip==USR_FTDI)m=u->mst;
    if(m<0)m=USR_TIOCM_CAR|USR_TIOCM_DSR|USR_TIOCM_CTS;
    return m|(u->dtr?USR_TIOCM_DTR:0)|(u->rts?USR_TIOCM_RTS:0); }
int usbser_read(int k,u8* out,int max){
    if(k<0||k>=US_PORTS||!g_usp[k].used)return -1;
    struct usport* u=&g_usp[k];
    if(u->qw==u->qr){ if(!g_usd[u->dev].ok)return -1; usbser_poll(); }
    int n=0; while(n<max&&u->qr!=u->qw){ out[n++]=u->q[u->qr%US_Q]; u->qr++; } return n; }
int usbser_avail(int k){ if(!usbser_ok(k))return -1; struct usport* u=&g_usp[k]; if(u->qw==u->qr)usbser_poll(); return (int)(u->qw-u->qr); }
void usbser_drop(int k){ if(k>=0&&k<US_PORTS&&g_usp[k].used)g_usp[k].qr=g_usp[k].qw; }
int usbser_outq(int k){ if(!usbser_ok(k))return 0; return g_usp[k].txout*US_TXSZ; }
int usbser_drain(int k){ if(!usbser_ok(k))return -1; struct usport* u=&g_usp[k]; u64 t0=hr_ns(); while(usbser_ok(k)&&u->txout>0&&hr_ns()-t0<2000000000ULL)usbser_poll(); return u->txout?-1:0; }
int usbser_write(int k,const u8* s,int len){
    if(!usbser_ok(k))return -1;
    struct usport* u=&g_usp[k]; struct usdev* d=&g_usd[u->dev]; int done=0;
    while(done<len){ int c=len-done; if(c>US_TXSZ)c=US_TXSZ;
        usbser_poll();
        if(u->txout>=US_NTX){ u64 t0=hr_ns(); while(d->ok&&u->used&&u->txout>=US_NTX&&hr_ns()-t0<500000000ULL)usbser_poll(); }
        if(!d->ok||!u->used||u->txout>=US_NTX||!us_lock()){ d->err++; return done?done:-1; }
        u8* b=(u8*)(u64)(u->txbase+(u32)(u->txi*US_TXSZ)); for(int i=0;i<c;i++)b[i]=s[done+i];
        int prev=g_xcur; xhc_select(d->ctl); u->txout++; us_trb(d,u->outring,&u->out_enq,&u->out_cyc,(u32)(u64)b,c,u->out_dci); xhc_select(prev);
        __sync_lock_release(&xh_poll_busy);
        u->txi=(u->txi+1)%US_NTX; u->txb+=(u32)c; done+=c; }
    return done; }
int usbser_open(int k){ if(!usbser_ok(k))return -1; g_usp[k].opens++; return g_usp[k].opens; }
int usbser_close(int k){ if(k<0||k>=US_PORTS||!g_usp[k].used)return 0; if(g_usp[k].opens>0)g_usp[k].opens--; return g_usp[k].opens; }
int usbser_info(char* out,int cap){
    if(cap<=0)return 0;
    char b[900],*p=b; int any=0;
    for(int k=0;k<US_PORTS;k++){ if(!usbser_ok(k)||p>b+sizeof b-180)continue; struct usport* u=&g_usp[k]; struct usdev* d=&g_usd[u->dev];
        if(any)*p++='\n';
        any=1;
        p=scopy(p,u->acm?"ttyACM":"ttyUSB"); p=udec(p,(u32)u->num); p=scopy(p,": "); p=scopy(p,usr_chipname[d->chip]); *p++=' '; p=hx(p,d->vid,4); *p++=':'; p=hx(p,d->pid,4);
        if(d->name[0]){ p=scopy(p," \""); p=scopy(p,d->name); *p++='"'; }
        p=scopy(p,"  "); p=udec(p,u->line.baud); *p++=' '; *p++=(char)('0'+u->line.bits); *p++="NOEMS"[u->line.parity%5]; *p++=u->line.stop==2?'2':'1';
        p=scopy(p,u->dtr?" DTR":" dtr"); p=scopy(p,u->rts?" RTS":" rts");
        p=scopy(p,"  in "); p=udec(p,u->rxb); p=scopy(p," out "); p=udec(p,u->txb); p=scopy(p," errors "); p=udec(p,d->err); p=scopy(p," dropped "); p=udec(p,u->drops); }
    if(!any)p=scopy(p,"no USB serial adapter");
    *p=0; int i=0; for(;b[i]&&i<cap-1;i++)out[i]=b[i]; out[i]=0; return any; }
