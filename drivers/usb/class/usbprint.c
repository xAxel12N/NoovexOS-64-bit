#include "drivers/usb/class/ippusb_proto.h"
#define UP_OBUF 0x10000
#define UP_IBUF 0x4000
struct updev { int ok, ctl, slot, ifn, alt, proto, in_dci, out_dci, in_ep, out_ep, in_pend, nif, ippif; u32 mem, inctx, devctx, ep0, dbuf, inring, outring, obuf, ibuf, in_enq, in_cyc, out_enq, out_cyc, e0_enq, e0_cyc, in_trb, sent, rcvd, err; u16 vid, pid; char name[40], devid[256]; usbloc_t L; struct uprn_if f[4]; };
static struct updev g_up;
char g_uprn_status[96]="No USB printer";
static int up_ctrl(struct updev* d,u8 rt,u8 req,u16 val,u16 idx,u16 len,int in){
    int s_slot=cur_slot; u32 s_ep0=cur_ep0, s_db=cur_dbuf, s_enq=e0_enq, s_cyc=e0_cyc;
    cur_slot=d->slot; cur_ep0=d->ep0; cur_dbuf=d->dbuf; e0_enq=d->e0_enq; e0_cyc=d->e0_cyc;
    int r=xh_ctrl(rt,req,val,idx,len,in,0);
    d->e0_enq=e0_enq; d->e0_cyc=e0_cyc; cur_slot=s_slot; cur_ep0=s_ep0; cur_dbuf=s_db; e0_enq=s_enq; e0_cyc=s_cyc; return r; }
static u32 up_trb(struct updev* d,u32 ring,u32* enq,u32* cyc,u32 buf,int len,int dci,int in){
    u32 at=ring+(*enq)*16; volatile u32* tr=(volatile u32*)(u64)at;
    tr[0]=buf; tr[1]=0; tr[2]=(u32)len; __sync_synchronize(); tr[3]=(1u<<10)|(1u<<5)|(in?(1u<<2):0)|(*cyc);
    (*enq)++; if(*enq>=255){ volatile u32* lk=(volatile u32*)(u64)(ring+255*16); lk[3]=(lk[3]&~1u)|(*cyc); *enq=0; *cyc^=1; }
    __sync_synchronize(); xh_db(d->slot,(u32)dci); return at; }
static int up_wait(struct updev* d,int dci,u32 trb,u32 ms,u32* res){
    u64 t0=hr_ns(); u8 cc=0; int ok=0;
    while(!ok&&hr_ns()-t0<(u64)ms*1000000ULL){ xh_want_slot=d->slot; xh_want_ep=dci; xh_want_ptr[0]=trb; xh_want_pn=1; ok=xh_event_w(32,0,&cc,0,0,XH_T_BULK/4+1,XH_IT_BULK/4); xh_want_slot=-1; xh_want_ep=-1; xh_want_pn=0; }
    if(!ok)return -1;
    if(res){ u32 ix=ev_deq?ev_deq-1:255; *res=((volatile u32*)(u64)(XHCI_EVTRING+ix*16))[2]&0xFFFFFF; }
    return cc; }
static int up_present(struct updev* d){
    if(!d->ok)return 0;
    xhc_select(d->ctl); if(d->L.depth==0&&!(XMR(xhci_op+0x400+(u32)d->L.root*0x10)&1))return 0;
    for(int k=0;k<USBDEV_MAX;k++) if(g_usbdev[k].used&&g_usbdev[k].ctl==d->ctl&&g_usbdev[k].port==d->L.root&&g_usbdev[k].prn)return 1;
    return 0; }
static void up_gone(struct updev* d){ if(!d->ok)return; d->ok=0; if(d->slot){ xh_disable_slot(d->slot); d->slot=0; } scopy(g_uprn_status,"USB printer removed")[0]=0; klog("usbprint: printer removed"); }
static int up_lock(void){ if(xh_wdepth>0)return 0; for(int i=0;i<2000;i++){ if(!__sync_lock_test_and_set(&xh_poll_busy,1)){ xh_poll_t0=g_ticks; return 1; } xh_delay_ms(1); } return 0; }
static void up_unlock(void){ __sync_lock_release(&xh_poll_busy); }
static int up_write(struct updev* d,const u8* p,int n,u32 ms){
    int off=0; while(off<n){ int c=n-off; if(c>UP_OBUF)c=UP_OBUF; volatile u8* ob=(volatile u8*)(u64)d->obuf; for(int i=0;i<c;i++)ob[i]=p[off+i];
        u32 t=up_trb(d,d->outring,&d->out_enq,&d->out_cyc,d->obuf,c,d->out_dci,0); int cc=up_wait(d,d->out_dci,t,ms,0);
        if(cc!=1){ d->err++; return cc<0?-1:-2; } off+=c; d->sent+=(u32)c; }
    return off; }
static int up_read(struct updev* d,u8* out,int cap,u32 ms){
    if(!d->in_dci)return -2;
    if(!d->in_pend){ d->in_trb=up_trb(d,d->inring,&d->in_enq,&d->in_cyc,d->ibuf,UP_IBUF,d->in_dci,1); d->in_pend=1; }
    u32 res=0; int cc=up_wait(d,d->in_dci,d->in_trb,ms,&res); if(cc<0)return 0;
    d->in_pend=0; if(cc!=1&&cc!=13){ d->err++; return -2; }
    int got=UP_IBUF-(int)res; if(got<0)got=0; if(got>cap)got=cap; volatile u8* ib=(volatile u8*)(u64)d->ibuf; for(int i=0;i<got;i++)out[i]=ib[i]; d->rcvd+=(u32)got; return got; }
static int up_mem(struct updev* d){
    if(d->mem)return 1; u8* p=(u8*)dma_alloc_pages(32); if(!p)return 0;
    d->mem=(u32)(u64)p; d->inctx=d->mem; d->devctx=d->mem+0x1000u; d->ep0=d->mem+0x2000u; d->inring=d->mem+0x3000u; d->outring=d->mem+0x4000u; d->dbuf=d->mem+0x5000u; d->obuf=d->mem+0x8000u; d->ibuf=d->mem+0x18000u; return 1; }
static int up_setif(struct updev* d,int j){
    struct uprn_if* f=&d->f[j]; u8 cc=0,sl=0;
    if(f->alt)up_ctrl(d,0x01,11,(u16)f->alt,(u16)f->ifn,0,0);
    d->ifn=f->ifn; d->alt=f->alt; d->proto=f->proto; d->in_ep=f->ie; d->out_ep=f->oe;
    d->in_dci=f->ie?(f->ie&15)*2+1:0; d->out_dci=(f->oe&15)*2; int maxdci=d->in_dci>d->out_dci?d->in_dci:d->out_dci;
    ctx_zero(d->inctx,(int)ctx_sz()*33/4); volatile u32* ic=(volatile u32*)(u64)d->inctx; ic[1]=1u|(d->in_dci?(1u<<d->in_dci):0)|(1u<<d->out_dci);
    volatile u32* sc=(volatile u32*)(u64)(d->inctx+ctx_sz()); xh_slot_ctx(sc,&d->L,maxdci);
    ring_init(d->inring); ring_init(d->outring); d->in_enq=0; d->in_cyc=1; d->out_enq=0; d->out_cyc=1; d->in_pend=0;
    if(d->in_dci){ volatile u32* ei=(volatile u32*)(u64)(d->inctx+ctx_sz()*(u32)(d->in_dci+1)); ei[1]=(6u<<3)|(3u<<1)|((u32)f->im<<16); ei[2]=d->inring|1u; ei[3]=0; ei[4]=1024; }
    volatile u32* eo=(volatile u32*)(u64)(d->inctx+ctx_sz()*(u32)(d->out_dci+1)); eo[1]=(2u<<3)|(3u<<1)|((u32)f->om<<16); eo[2]=d->outring|1u; eo[3]=0; eo[4]=1024;
    return xh_cmd_s(d->inctx,0,0,12,(u32)d->slot,&cc,&sl); }
#define PFAIL(m) do{ xh_set_fail(m); d->ok=0; return 0; }while(0)
static int up_enum_loc(const usbloc_t* L){
    struct updev* d=&g_up; if(d->ok||!up_mem(d))return 0;
    xhci_failstep[0]=0; xhci_failcc=0; d->slot=0; d->L=*L;
    int speed=L->speed; int mps0=(speed==3)?64:(speed>=4)?512:8;
    cur_ep0=d->ep0; cur_dbuf=d->dbuf; e0_enq=0; e0_cyc=1; ring_init(d->ep0);
    u8 cc=0,sl=0; if(!xh_cmd_s(0,0,0,9,0,&cc,&sl)||!sl){ xhci_failcc=cc; PFAIL("Enable Slot"); }
    cur_slot=sl; d->slot=sl;
    ctx_zero(d->devctx,(int)ctx_sz()*32/4); *(volatile u32*)(u64)(XHCI_DCBAA+sl*8)=d->devctx; *(volatile u32*)(u64)(XHCI_DCBAA+sl*8+4)=0;
    ctx_zero(d->inctx,(int)ctx_sz()*33/4); volatile u32* ic=(volatile u32*)(u64)d->inctx; ic[1]=3u;
    volatile u32* sc=(volatile u32*)(u64)(d->inctx+ctx_sz()); xh_slot_ctx(sc,L,1);
    volatile u32* e0=(volatile u32*)(u64)(d->inctx+ctx_sz()*2); e0[1]=(4u<<3)|(3u<<1)|((u32)mps0<<16); e0[2]=d->ep0|1u; e0[3]=0; e0[4]=8;
    if(!xh_cmd_s(d->inctx,0,0,11,(u32)sl,&cc,&sl)){ xhci_failcc=cc; PFAIL("Address Device"); }
    xh_delay_ms(10);
    if(!xh_fix_mps0(d->inctx,L,d->ep0))PFAIL("GET device descriptor (8)");
    if(!xh_ctrl(0x80,6,0x0100,0,18,1,0))PFAIL("GET device descriptor");
    volatile u8* db=(volatile u8*)(u64)d->dbuf; u8 dd[18]; for(int i=0;i<18;i++)dd[i]=db[i];
    if(dd[0]<18||dd[1]!=1)PFAIL("bad device descriptor");
    d->vid=(u16)(dd[8]|(dd[9]<<8)); d->pid=(u16)(dd[10]|(dd[11]<<8)); int ip=dd[15];
    if(!xh_ctrl(0x80,6,0x0200,0,9,1,0))PFAIL("GET config(9)");
    static u8 full[1024]; int wt=db[2]|(db[3]<<8), cv=db[5]; if(wt<9||db[1]!=2)PFAIL("bad config"); if(wt>1024)wt=1024;
    if(!xh_ctrl(0x80,6,0x0200,0,(u16)wt,1,0))PFAIL("GET config(full)");
    for(int i=0;i<wt;i++)full[i]=db[i];
    d->nif=upp_parse_cfg(full,wt,d->f,4); if(!d->nif)PFAIL("no printer interface");
    d->name[0]=0;
    if(ip&&xh_ctrl(0x80,6,(u16)(0x0300|ip),0x0409,64,1,0)){ int ln=db[0], o=0; if(ln>64)ln=64; for(int q=2;q+1<ln&&o<39;q+=2){ u8 ch=db[q]; if(db[q+1]||ch<32||ch>126)ch='?'; d->name[o++]=(char)ch; } d->name[o]=0; }
    if(!xh_ctrl(0x00,9,(u16)cv,0,0,0,0))PFAIL("SET_CONFIGURATION");
    d->e0_enq=e0_enq; d->e0_cyc=e0_cyc; d->ctl=g_xcur;
    d->ippif=upp_pick(d->f,d->nif,1); int j=d->ippif>=0?d->ippif:0;
    d->devid[0]=0; if(up_ctrl(d,0xA1,0,0,(u16)((d->f[j].ifn<<8)|d->f[j].alt),255,1)){ u8 t[255]; for(int i=0;i<255;i++)t[i]=db[i]; int l=upp_devid_len(t,255); if(l>254)l=254; for(int i=0;i<l;i++)d->devid[i]=(char)t[i+2]; d->devid[l]=0; }
    if(!up_setif(d,j))PFAIL("Configure Endpoint");
    d->ok=1; d->sent=d->rcvd=d->err=0;
    usbdev_note(L->root,d->vid,d->pid,full,wt,d->name[0]?d->name:"USB printer");
    char mdl[64]; if(upp_devid(d->devid,"MDL",mdl,64)||upp_devid(d->devid,"MODEL",mdl,64)){ int q=0; for(;mdl[q]&&q<39;q++)d->name[q]=mdl[q]; d->name[q]=0; }
    { char* p=g_uprn_status; p=scopy(p,d->name[0]?d->name:"USB printer"); p=scopy(p,d->ippif>=0?" - IPP over USB":" - USB printer class"); *p=0;
      char b[140]; p=b; p=scopy(p,"usbprint: "); p=scopy(p,g_uprn_status); *p++=' '; p=hx(p,d->vid,4); *p++=':'; p=hx(p,d->pid,4); *p=0; klog(b); }
    return 1; }
#undef PFAIL
int sysfeat_get(int f);
int uprn_scan(void){
    struct updev* d=&g_up;
    if(d->ok){ if(!up_lock())return 1; int prev=g_xcur; if(!up_present(d))up_gone(d); xhc_select(prev); up_unlock(); if(d->ok)return 1; }
    if(!g_xhc_n||!sysfeat_get(1))return 0;
    if(!up_lock())return 0;
    int prev=g_xcur;
    for(int c=0;c<g_xhc_n&&!d->ok;c++){ xhc_select(c); if(!xhci_present||!xhci_init_ok)continue; xhci_port_refresh();
        for(int p=0;p<xhci_ports&&!d->ok;p++){ if(!xhci_port_conn[p])continue; int prn=0;
            for(int k=0;k<USBDEV_MAX;k++) if(g_usbdev[k].used&&g_usbdev[k].ctl==c&&g_usbdev[k].port==p&&g_usbdev[k].prn)prn=1;
            if(!prn||(msd_ok&&msd_ctl==c&&msd_port==p))continue;
            int sp=xh_root_reset(p); if(!sp)continue;
            usbloc_t L; L.root=p; L.depth=0; L.hub=-1; L.hport=0; L.speed=sp; L.route=0; L.tt_slot=0; L.tt_port=0;
            if(!up_enum_loc(&L)&&d->slot){ xh_disable_slot(d->slot); d->slot=0; } } }
    xhc_select(prev); up_unlock();
    if(!d->ok)scopy(g_uprn_status,"No USB printer")[0]=0;
    return d->ok; }
int uprn_present(void){ return g_up.ok; }
int uprn_has_ipp(void){ return g_up.ok&&g_up.ippif>=0; }
const char* uprn_devid(void){ return g_up.devid; }
const char* uprn_name(void){ return g_up.name; }
int uprn_http(const u8* req,int len,u8* resp,int cap,int* blen_out,int* body_off){
    struct updev* d=&g_up; if(!d->ok||d->ippif<0)return -1;
    if(!up_lock())return -2; int prev=g_xcur;
    if(!up_present(d)){ up_gone(d); xhc_select(prev); up_unlock(); return -3; }
    int r=-4;
    for(int drain=0;drain<4;drain++){ u8 t[64]; if(up_read(d,t,64,5)<=0)break; }
    if(up_write(d,req,len,10000)==len){
        int n=0; struct uph h={0,0,-1,0,0,0}; u64 t0=hr_ns(); r=-5;
        while(hr_ns()-t0<30000000000ULL&&n<cap){ int g=up_read(d,resp+n,cap-n,500); if(g<0){ r=-6; break; } if(!g){ if(n&&upp_http_eof(n,&h)&&hr_ns()-t0>2000000000ULL){ r=h.status; break; } continue; }
            n+=g; int c=upp_http(resp,n,&h); if(c<0){ r=-7; break; } if(c>0){ r=h.status; break; } }
        if(r>0){ if(blen_out)*blen_out=h.blen; if(body_off)*body_off=h.hlen; } }
    xhc_select(prev); up_unlock(); return r; }
int uprn_raw(const u8* p,int n){
    struct updev* d=&g_up; if(!d->ok)return -1;
    if(!up_lock())return -2; int prev=g_xcur; int r=-3;
    if(up_present(d)){ if(d->ippif>=0){ int j=upp_pick(d->f,d->nif,0); if(j<0||!up_setif(d,j)){ xhc_select(prev); up_unlock(); return -4; } }
        r=up_write(d,p,n,20000);
        if(d->ippif>=0)up_setif(d,d->ippif); }
    else up_gone(d);
    xhc_select(prev); up_unlock(); return r; }
int uprn_info(char* o,int cap){
    struct updev* d=&g_up; char* p=o; char* e=o+cap-80; if(cap<120){ if(cap)o[0]=0; return 0; }
    if(!d->ok){ p=scopy(p,"No USB printer"); *p=0; return (int)(p-o); }
    p=scopy(p,d->name[0]?d->name:"USB printer"); p=scopy(p,"  "); p=hx(p,d->vid,4); *p++=':'; p=hx(p,d->pid,4); p=scopy(p,"  port "); p=udec(p,(u32)d->L.root+1);
    p=scopy(p,d->ippif>=0?"  IPP over USB":d->proto==2?"  bidirectional":"  unidirectional"); p=scopy(p,"\nsent "); p=udec(p,d->sent); p=scopy(p," bytes, received "); p=udec(p,d->rcvd); p=scopy(p," bytes, errors "); p=udec(p,d->err);
    if(d->devid[0]){ p=scopy(p,"\nID: "); for(int i=0;d->devid[i]&&p<e;i++)*p++=d->devid[i]; }
    *p=0; return (int)(p-o); }
