#include "include/nvx/core/sysfeat.h"
#include "drivers/input/gamepad/gpad_proto.h"
#include "drivers/input/gamepad/gamepad.h"
#define GPU_MAX 4
#define GPU_NRX 4
#define GPU_RXSZ 512
#define GPU_TXSZ 128
#define GPU_NTX 16
struct gpu { int ok, ctl, slot, type, ifn, in_ep, out_ep, in_dci, out_dci, in_mps, out_mps, in_intv, out_intv, halt_in, halt_out, depth, pad, txout, txi, init;
    u32 mem, inctx, devctx, ep0, dbuf, inring, outring, in_enq, in_cyc, out_enq, out_cyc, rxbase, txbase, route, e0_enq, e0_cyc, err, rxp;
    u64 rec_t; u16 vid, pid; u8 seq, swc; char name[40]; usbloc_t L; struct gphid hid; struct gpst st; };
static struct gpu g_gpu[GPU_MAX];
static struct featguard gpu_fg={"Game controllers","feat-gamepad.cfg",1,0,3,0,0};
static int xh_cfg_gp;
static volatile int gpu_inrx;
static int gpu_lock(void){ u64 t0=hr_ns(); while(__sync_lock_test_and_set(&xh_poll_busy,1)){ if(xh_wdepth>0||hr_ns()-t0>50000000ULL)return 0; __builtin_ia32_pause(); } return 1; }
static int gpu_cfg_is_pad(const u8* c,int n){
    for(int i=0;i+8<n;){ int bl=c[i]; if(bl<2)break; if(c[i+1]==4&&bl>=9){ int k=c[i+5], s=c[i+6], p=c[i+7]; if((k==0xFF&&s==0x5D&&p==1)||(k==0xFF&&s==0x47&&p==0xD0)||(k==3&&p==0))return 1; } i+=bl; }
    return 0; }
static void gpu_trb(struct gpu* d,u32 ring,u32* enq,u32* cyc,u32 buf,int len,int dci){
    volatile u32* tr=(volatile u32*)(u64)(ring+(*enq)*16);
    tr[0]=buf; tr[1]=0; tr[2]=(u32)len; __sync_synchronize(); tr[3]=(1u<<10)|(1u<<5)|(1u<<2)|(*cyc);
    (*enq)++; if(*enq>=255){ volatile u32* lk=(volatile u32*)(u64)(ring+255*16); lk[3]=(lk[3]&~1u)|(*cyc); *enq=0; *cyc^=1; }
    __sync_synchronize(); xh_db(d->slot,(u32)dci); }
static void gpu_arm(struct gpu* d,u32 buf){ gpu_trb(d,d->inring,&d->in_enq,&d->in_cyc,buf,d->in_mps>GPU_RXSZ?GPU_RXSZ:d->in_mps,d->in_dci); }
static int gpu_ctrl(struct gpu* d,u8 rt,u8 req,u16 val,u16 idx,u16 len,int in,const u8* data){
    int s_slot=cur_slot; u32 s_ep0=cur_ep0, s_db=cur_dbuf, s_enq=e0_enq, s_cyc=e0_cyc;
    cur_slot=d->slot; cur_ep0=d->ep0; cur_dbuf=d->dbuf; e0_enq=d->e0_enq; e0_cyc=d->e0_cyc;
    if(data&&!in){ volatile u8* b=(volatile u8*)(u64)d->dbuf; for(int i=0;i<len&&i<1024;i++)b[i]=data[i]; }
    int r=xh_ctrl(rt,req,val,idx,len,in,0);
    d->e0_enq=e0_enq; d->e0_cyc=e0_cyc;
    cur_slot=s_slot; cur_ep0=s_ep0; cur_dbuf=s_db; e0_enq=s_enq; e0_cyc=s_cyc; return r; }
static int gpu_tx(struct gpu* d,const u8* p,int n){
    if(!d->ok||n<=0||n>GPU_TXSZ)return -1;
    if(!d->out_dci){ if(d->type<GP_DS4)return -1; return gpu_ctrl(d,0x21,9,(u16)(0x0200|p[0]),(u16)d->ifn,(u16)n,0,p)?n:-1; }
    if(d->txout>=GPU_NTX){ d->err++; return -1; }
    u8* b=(u8*)(u64)(d->txbase+(u32)(d->txi*GPU_TXSZ)); for(int i=0;i<n;i++)b[i]=p[i];
    d->txout++; gpu_trb(d,d->outring,&d->out_enq,&d->out_cyc,(u32)(u64)b,n,d->out_dci); d->txi=(d->txi+1)%GPU_NTX; return n; }
static void gpu_sw_next(struct gpu* d){
    u8 o[64]; int n=0;
    if(d->init<4)n=gp_sw_hs(d->init,o);
    else if(d->init==4){ u8 a=0x30; n=gp_sw_sub(o,d->swc++,0x03,&a,1); }
    else if(d->init==5){ u8 a=1; n=gp_sw_sub(o,d->swc++,0x48,&a,1); }
    else if(d->init==6){ u8 a=(u8)(1u<<((d->pad>=0?gpad_at(d->pad)?gpad_at(d->pad)->player-1:0:0)&3)); n=gp_sw_sub(o,d->swc++,0x30,&a,1); }
    if(n){ gpu_tx(d,o,n); d->init++; if(d->init==4)gpu_sw_next(d); } }
static int gpu_out(int idx,int ref,int strong,int weak,u8 r,u8 g,u8 b){
    (void)idx; if(ref<0||ref>=GPU_MAX)return -1; struct gpu* d=&g_gpu[ref]; if(!d->ok)return -1;
    u8 o[80]; int n=0, pl=gpad_at(d->pad)?gpad_at(d->pad)->player:1;
    if(d->type==GP_X360){ n=gp_x360_rumble(o,strong,weak); }
    else if(d->type==GP_XONE)n=gp_xone_rumble(o,d->seq++,strong,weak);
    else if(d->type==GP_DS4)n=gp_ds4_out(o,0,strong,weak,r,g,b);
    else if(d->type==GP_DS5)n=gp_ds5_out(o,0,0,strong,weak,r,g,b,pl);
    else if(d->type==GP_SWPRO)n=gp_sw_rumble(o,d->swc++,strong,weak);
    if(!n)return -1;
    int lk=!gpu_inrx; if(lk&&!gpu_lock())return -1;
    int prev=g_xcur; xhc_select(d->ctl); int rv=gpu_tx(d,o,n); xhc_select(prev); if(lk)__sync_lock_release(&xh_poll_busy); return rv; }
static void gpu_rx(struct gpu* d,const u8* p,int n){
    d->rxp++; int ok=0; struct gpst s=d->st;
    if(d->type==GP_X360)ok=gp_x360_parse(p,n,&s);
    else if(d->type==GP_XONE){ u8 a[16]; int an=gp_xone_ack(p,n,a); if(an)gpu_tx(d,a,an); ok=gp_xone_parse(p,n,&s); }
    else if(d->type==GP_DS4)ok=gp_ds4_parse(p,n,&s);
    else if(d->type==GP_DS5)ok=gp_ds5_parse(p,n,&s);
    else if(d->type==GP_SWPRO){ ok=gp_sw_parse(p,n,&s); if(d->init<7&&(p[0]==0x81||p[0]==0x21||(d->init>=4&&p[0]==0x30)))gpu_sw_next(d); }
    else if(d->type==GP_HID)ok=gp_hid_parse(&d->hid,p,n,&s);
    if(ok){ d->st=s; gpad_report(d->pad,&s); }
    gpad_tick(); }
static int gpu_xfer(u8 sl,int ep,u8 cc,u32 res,u32 ptr){
    struct gpu* d=0; for(int k=0;k<GPU_MAX;k++) if(g_gpu[k].ok&&g_gpu[k].ctl==g_xcur&&sl==(u8)g_gpu[k].slot){ d=&g_gpu[k]; break; }
    if(!d)return 0;
    if(d->out_dci&&ep==d->out_dci){ if(d->txout>0)d->txout--; if(cc!=1&&cc!=13){ d->err++; d->halt_out=1; } return 1; }
    if(ep!=d->in_dci)return 0;
    u32 buf=0; if(ptr>=d->inring&&ptr<d->inring+255*16)buf=((volatile u32*)(u64)ptr)[0];
    if(buf<d->rxbase||buf>=d->rxbase+(u32)(GPU_NRX*GPU_RXSZ)||((buf-d->rxbase)%GPU_RXSZ)){ d->err++; return 1; }
    int len=d->in_mps>GPU_RXSZ?GPU_RXSZ:d->in_mps;
    if(cc==1||cc==13){ int got=len-(int)(res&0xFFFFFF); if(got<0)got=0; if(got>len)got=len; u8 t[GPU_RXSZ]; const volatile u8* q=(const volatile u8*)(u64)buf; for(int i=0;i<got;i++)t[i]=q[i]; if(!d->halt_in)gpu_arm(d,buf); if(got){ gpu_inrx++; gpu_rx(d,t,got); gpu_inrx--; } }
    else { d->err++; d->halt_in=1; }
    return 1; }
static void gpu_unhalt(struct gpu* d,int dci,int epaddr,u32 ring,u32* enq,u32* cyc){
    u8 cc=0,sl=0;
    xh_cmd_s(0,0,0,14u|((u32)dci<<6),(u32)d->slot,&cc,&sl);
    xh_cmd_s((ring+(*enq)*16)|(*cyc),0,0,16u|((u32)dci<<6),(u32)d->slot,&cc,&sl);
    gpu_ctrl(d,0x02,1,0,(u16)epaddr,0,0,0); }
static void gpu_gone(struct gpu* d){ if(!d->ok)return; d->ok=0; if(d->slot){ xh_disable_slot(d->slot); d->slot=0; } gpad_remove(d->pad); d->pad=-1; klog("gamepad: controller removed"); }
static int gpu_any(void){ for(int k=0;k<GPU_MAX;k++) if(g_gpu[k].ok)return 1; return 0; }
void gpusb_poll(void){
    if(!gpu_any())return;
    if(xh_wdepth>0||__sync_lock_test_and_set(&xh_poll_busy,1))return;
    xh_poll_t0=g_ticks;
    int prev=g_xcur, done=0;
    for(int k=0;k<GPU_MAX;k++){ struct gpu* d=&g_gpu[k]; if(!d->ok)continue;
        if(!((done>>d->ctl)&1)){ done|=1<<d->ctl; xhc_select(d->ctl); xhci_poll_one(); }
        if(d->ok&&(d->halt_in||d->halt_out)&&hr_ns()-d->rec_t>=50000000ULL){ xhc_select(d->ctl); d->rec_t=hr_ns();
            if(d->L.depth==0){ u32 pv=XMR(xhci_op+0x400+(u32)d->L.root*0x10); if(!(pv&1)){ gpu_gone(d); continue; } }
            if(d->halt_in){ d->halt_in=0; gpu_unhalt(d,d->in_dci,d->in_ep,d->inring,&d->in_enq,&d->in_cyc); for(int i=0;i<GPU_NRX;i++)gpu_arm(d,d->rxbase+(u32)(i*GPU_RXSZ)); }
            if(d->halt_out){ d->halt_out=0; gpu_unhalt(d,d->out_dci,d->out_ep,d->outring,&d->out_enq,&d->out_cyc); d->txout=0; } } }
    xhc_select(prev);
    __sync_lock_release(&xh_poll_busy);
    gpad_tick(); }
static int gpu_mem(struct gpu* d){
    if(d->mem)return 1;
    u8* p=(u8*)dma_alloc_pages(12); if(!p){ klog("gamepad: no memory"); return 0; }
    d->mem=(u32)(u64)p; d->inctx=d->mem; d->devctx=d->mem+0x1000u; d->ep0=d->mem+0x2000u; d->inring=d->mem+0x3000u; d->outring=d->mem+0x4000u; d->dbuf=d->mem+0x5000u; d->txbase=d->mem+0x6000u; d->rxbase=d->mem+0x8000u;
    return 1; }
struct gpu_cfg { int type, ifn, ie, oe, im, om, ii, oi, rdl; };
static int gpu_cfg_done(const struct gpu_cfg* f,int ty){ return ty&&f->ie&&(ty>=GP_DS4||f->oe); }
static int gpu_parse_cfg(const u8* c,int n,u16 vid,u16 pid,struct gpu_cfg* f){
    for(int i=0;i<(int)sizeof *f;i++)((u8*)f)[i]=0;
    int ty=0;
    for(int i=0;i+1<n;){ int bl=c[i]; if(bl<2||i+bl>n)break;
        if(c[i+1]==4&&bl>=9){ if(gpu_cfg_done(f,ty))break; int k=c[i+5], s=c[i+6], p=c[i+7]; ty=0; f->ie=f->oe=f->rdl=0;
            if(c[i+3]==0){ int t=gp_ident(vid,pid,k,s,p); if(t==GP_X360||t==GP_XONE)ty=t; else if(k==3&&p!=1&&p!=2)ty=t?t:GP_HID; }
            f->ifn=c[i+2]; }
        else if(c[i+1]==0x21&&bl>=9&&ty)f->rdl=c[i+7]|(c[i+8]<<8);
        else if(c[i+1]==5&&bl>=7&&ty&&(c[i+3]&3)==3){ int a=c[i+2], m=(c[i+4]|(c[i+5]<<8))&0x7FF;
            if((a&0x80)&&!f->ie){ f->ie=a; f->im=m; f->ii=c[i+6]; } else if(!(a&0x80)&&!f->oe){ f->oe=a; f->om=m; f->oi=c[i+6]; } }
        i+=bl; }
    if(!gpu_cfg_done(f,ty))return 0;
    f->type=ty; return 1; }
static struct gpu* gpu_slot_free(void){ for(int k=0;k<GPU_MAX;k++) if(!g_gpu[k].ok)return &g_gpu[k]; return 0; }
#define GFAIL(m) do{ xh_set_fail(m); d->ok=0; return 0; }while(0)
static int gpu_enum_loc(const usbloc_t* L){
    struct gpu* d=gpu_slot_free(); if(!d||!gpu_mem(d))return 0;
    xhci_failstep[0]=0; xhci_failcc=0; d->slot=0; d->pad=-1;
    int speed=L->speed; int mps0=(speed==3)?64:(speed>=4)?512:8;
    cur_ep0=d->ep0; cur_dbuf=d->dbuf; e0_enq=0; e0_cyc=1; ring_init(d->ep0);
    u8 cc=0,sl=0; if(!xh_cmd_s(0,0,0,9,0,&cc,&sl)||!sl){ xhci_failcc=cc; GFAIL("Enable Slot"); }
    cur_slot=sl; d->slot=sl;
    ctx_zero(d->devctx,(int)ctx_sz()*32/4); *(volatile u32*)(u64)(XHCI_DCBAA+sl*8)=d->devctx; *(volatile u32*)(u64)(XHCI_DCBAA+sl*8+4)=0;
    ctx_zero(d->inctx,(int)ctx_sz()*33/4); volatile u32* ic=(volatile u32*)(u64)d->inctx; ic[1]=(1u<<0)|(1u<<1);
    volatile u32* sc=(volatile u32*)(u64)(d->inctx+ctx_sz()); xh_slot_ctx(sc,L,1);
    volatile u32* e0=(volatile u32*)(u64)(d->inctx+ctx_sz()*2); e0[1]=(4u<<3)|(3u<<1)|((u32)mps0<<16); e0[2]=d->ep0|1u; e0[3]=0; e0[4]=8;
    if(!xh_cmd_s(d->inctx,0,0,11,(u32)sl,&cc,&sl)){ xhci_failcc=cc; GFAIL("Address Device"); }
    xh_delay_ms(10);
    if(!xh_fix_mps0(d->inctx,L,d->ep0))GFAIL("GET device descriptor (8)");
    if(!xh_ctrl(0x80,6,0x0100,0,18,1,0))GFAIL("GET device descriptor");
    u8 dd[18]; volatile u8* db=(volatile u8*)(u64)d->dbuf; for(int i=0;i<18;i++)dd[i]=db[i];
    if(dd[0]<18||dd[1]!=1)GFAIL("bad device descriptor");
    d->vid=(u16)(dd[8]|(dd[9]<<8)); d->pid=(u16)(dd[10]|(dd[11]<<8)); int ip=dd[15];
    if(!xh_ctrl(0x80,6,0x0200,0,9,1,0))GFAIL("GET config(9)");
    static u8 full[1024]; int wt=db[2]|(db[3]<<8), cv=db[5]; if(wt<9||db[1]!=2)GFAIL("bad config"); if(wt>1024)wt=1024;
    if(!xh_ctrl(0x80,6,0x0200,0,(u16)wt,1,0))GFAIL("GET config(full)");
    for(int i=0;i<wt;i++)full[i]=db[i];
    struct gpu_cfg cf; if(!gpu_parse_cfg(full,wt,d->vid,d->pid,&cf))GFAIL("no game controller interface");
    d->name[0]=0;
    if(ip&&xh_ctrl(0x80,6,(u16)(0x0300|ip),0x0409,64,1,0)){ int ln=db[0], o=0; if(ln>64)ln=64; for(int q=2;q+1<ln&&o<39;q+=2){ u8 ch=db[q]; if(db[q+1]||ch<32||ch>126)ch='?'; d->name[o++]=(char)ch; } d->name[o]=0; }
    if(!xh_ctrl(0x00,9,(u16)cv,0,0,0,0))GFAIL("SET_CONFIGURATION");
    d->type=cf.type; d->ifn=cf.ifn;
    if(d->type>=GP_DS4){ int rl=cf.rdl>0&&cf.rdl<=1024?cf.rdl:512;
        xh_ctrl(0x21,0x0A,0,(u16)cf.ifn,0,0,0);
        if(d->type==GP_HID){ if(!xh_ctrl(0x81,6,0x2200,(u16)cf.ifn,(u16)rl,1,0))GFAIL("GET report descriptor"); u8 rd[1024]; for(int i=0;i<rl;i++)rd[i]=db[i]; if(!gp_hid_desc(rd,rl,&d->hid))GFAIL("not a gamepad"); }
        if(d->type==GP_DS4||d->type==GP_DS5)xh_ctrl(0xA1,1,(u16)(0x0300|(d->type==GP_DS4?0x02:0x05)),(u16)cf.ifn,64,1,0); }
    d->in_ep=cf.ie; d->out_ep=cf.oe; d->in_mps=cf.im?cf.im:64; d->out_mps=cf.om?cf.om:64;
    d->in_dci=(cf.ie&0x0F)*2+1; d->out_dci=cf.oe?(cf.oe&0x0F)*2:0; int maxdci=d->in_dci>d->out_dci?d->in_dci:d->out_dci;
    ctx_zero(d->inctx,(int)ctx_sz()*33/4);
    ic=(volatile u32*)(u64)d->inctx; ic[1]=(1u<<0)|(1u<<d->in_dci)|(d->out_dci?(1u<<d->out_dci):0);
    sc=(volatile u32*)(u64)(d->inctx+ctx_sz()); xh_slot_ctx(sc,L,maxdci);
    ring_init(d->inring); ring_init(d->outring); d->in_enq=0; d->in_cyc=1; d->out_enq=0; d->out_cyc=1;
    { volatile u32* ei=(volatile u32*)(u64)(d->inctx+ctx_sz()*(u32)(d->in_dci+1)); ei[0]=xh_interval(speed,cf.ii)<<16; ei[1]=(7u<<3)|(3u<<1)|((u32)d->in_mps<<16); ei[2]=d->inring|1u; ei[3]=0; ei[4]=(u32)d->in_mps|((u32)d->in_mps<<16); }
    if(d->out_dci){ volatile u32* eo=(volatile u32*)(u64)(d->inctx+ctx_sz()*(u32)(d->out_dci+1)); eo[0]=xh_interval(speed,cf.oi)<<16; eo[1]=(3u<<3)|(3u<<1)|((u32)d->out_mps<<16); eo[2]=d->outring|1u; eo[3]=0; eo[4]=(u32)d->out_mps|((u32)d->out_mps<<16); }
    if(!xh_cmd_s(d->inctx,0,0,12,(u32)d->slot,&cc,&sl)){ xhci_failcc=cc; GFAIL("Configure Endpoint"); }
    d->e0_enq=e0_enq; d->e0_cyc=e0_cyc;
    d->ctl=g_xcur; d->L=*L; d->route=L->route; d->depth=L->depth; d->txout=0; d->txi=0; d->halt_in=d->halt_out=0; d->err=d->rxp=0; d->seq=0; d->swc=0; d->init=0;
    for(int i=0;i<(int)sizeof d->st;i++)((u8*)&d->st)[i]=0;
    int caps=GPC_RUMBLE|(d->type==GP_DS4||d->type==GP_DS5?GPC_LED|GPC_TOUCH|GPC_BAT:0)|(d->type==GP_SWPRO?GPC_BAT:0); if(d->type==GP_HID)caps=0;
    d->ok=1;
    d->pad=gpad_add(d->type,3,d->vid,d->pid,d->name[0]?d->name:gpad_typename(d->type),caps,0,(int)(d-g_gpu));
    if(d->pad<0){ d->ok=0; GFAIL("too many game controllers"); }
    for(int i=0;i<GPU_NRX;i++)gpu_arm(d,d->rxbase+(u32)(i*GPU_RXSZ));
    if(d->type==GP_XONE){ u8 o[16]; int n=gp_xone_init(o,d->seq++); gpu_tx(d,o,n); n=gp_xone_led(o,d->seq++); gpu_tx(d,o,n); }
    else if(d->type==GP_X360){ u8 o[4]; int n=gp_x360_led(o,gpad_at(d->pad)->player); gpu_tx(d,o,n); }
    else if(d->type==GP_SWPRO)gpu_sw_next(d);
    gpad_at(d->pad)->out=gpu_out; gpu_inrx++; if(d->type!=GP_SWPRO&&d->type!=GP_HID)gpad_set_out(d->pad,0,0,-1,-1,-1); gpu_inrx--;
    usbdev_note(L->root,d->vid,d->pid,full,wt,d->name[0]?d->name:"Game controller");
    { char b[112],*p=b; p=scopy(p,"gamepad: "); p=scopy(p,gpad_typename(d->type)); *p++=' '; p=hx(p,d->vid,4); *p++=':'; p=hx(p,d->pid,4); p=scopy(p," attached as /dev/input/js"); p=udec(p,(u32)d->pad); *p=0; klog(b); }
    return 1; }
#undef GFAIL
static int gpu_enum_root(int port){
    int sp=xh_root_reset(port); if(!sp)return 0;
    usbloc_t L; L.root=port; L.depth=0; L.hub=-1; L.hport=0; L.speed=sp; L.route=0; L.tt_slot=0; L.tt_port=0;
    return gpu_enum_loc(&L); }
static int gpu_owns(int c,int p){ for(int k=0;k<GPU_MAX;k++) if(g_gpu[k].ok&&g_gpu[k].ctl==c&&g_gpu[k].L.root==p)return 1; return 0; }
static int gpu_claimable(int c,int p){
    if(msd_ok&&msd_ctl==c&&msd_port==p)return 0;
    if(cam.ok&&cam.ctl==c&&cam.root==p)return 0;
    if(aud.ok&&aud.ctl==c&&aud.root==p)return 0;
    if(usn.ok&&usn.ctl==c&&usn.L.root==p)return 0;
    if(umidi_owns(c,p)||btu_owns(c,p)||gpu_owns(c,p))return 0;
    for(int k=0;k<USBDEV_MAX;k++) if(g_usbdev[k].used&&g_usbdev[k].ctl==c&&g_usbdev[k].port==p&&(g_usbdev[k].hid||g_usbdev[k].cam||g_usbdev[k].prn))return 0;
    for(int h=0;h<XHUB_MAX;h++) if(xhub[h].used&&xhub[h].ctl==c&&xhub[h].root==p)return 0;
    return 1; }
static void gpu_noslot(void){ for(int k=0;k<GPU_MAX;k++){ struct gpu* d=&g_gpu[k]; if(!d->ok&&d->slot){ xh_disable_slot(d->slot); d->slot=0; } } }
static int gpu_try_port(int p){
    if(!xh_cfg_gp||!gpu_slot_free()||!gpu_claimable(g_xcur,p))return 0;
    xh_cfg_gp=0;
    if(!fg_begin(&gpu_fg))return 0;
    if(gpu_enum_root(p)){ fg_ok(&gpu_fg); return 1; }
    gpu_noslot(); fg_ok(&gpu_fg); return 0; }
static int gpu_hub_try(int hi,int p){
    if(!xh_cfg_gp||!gpu_slot_free())return 0;
    xh_cfg_gp=0;
    if(!fg_begin(&gpu_fg))return 0;
    int sp=xhub_port_reset(hi,p); if(!sp){ fg_ok(&gpu_fg); return 0; }
    usbloc_t C; xhub_child_loc(hi,p,sp,&C);
    if(gpu_enum_loc(&C)){ fg_ok(&gpu_fg); klog("usb: game controller behind a hub"); return 1; }
    gpu_noslot(); fg_ok(&gpu_fg); return 0; }
int gpusb_scan(void){
    if(!g_xhc_n||!fg_begin(&gpu_fg))return gpu_any();
    if(xh_wdepth>0||__sync_lock_test_and_set(&xh_poll_busy,1)){ fg_ok(&gpu_fg); return gpu_any(); }
    int prev=g_xcur;
    for(int c=0;c<g_xhc_n&&gpu_slot_free();c++){ xhc_select(c); if(!xhci_present||!xhci_init_ok)continue; xhci_port_refresh();
        for(int p=0;p<xhci_ports&&gpu_slot_free();p++){ if(!xhci_port_conn[p]||!gpu_claimable(c,p))continue;
            int cand=0; for(int k=0;k<USBDEV_MAX;k++) if(g_usbdev[k].used&&g_usbdev[k].ctl==c&&g_usbdev[k].port==p&&(g_usbdev[k].cls==3||g_usbdev[k].cls==0xFF))cand=1;
            if(!cand)continue;
            if(!gpu_enum_root(p))gpu_noslot(); else { usb_seen[p]=1; xhci_port_conn[p]=1; } } }
    xhc_select(prev);
    __sync_lock_release(&xh_poll_busy);
    fg_ok(&gpu_fg);
    return gpu_any(); }
int gpusb_enabled(void){ fg_boot(&gpu_fg); return gpu_fg.on&&!gpu_fg.dead; }
const char* gpusb_state(void){ fg_boot(&gpu_fg); return !gpu_fg.on?"off":gpu_fg.dead?"off after repeated failures":"on"; }
void gpusb_enable(int on){ fg_enable(&gpu_fg,on); }
static int gpu_active(void){ return gpu_any(); }
static void gpu_remove_below(int root,u32 R,int dR){ for(int k=0;k<GPU_MAX;k++){ struct gpu* d=&g_gpu[k]; if(d->ok&&d->ctl==g_xcur&&d->L.root==root&&xh_under(d->route,d->depth,R,dR))gpu_gone(d); } }
static void gpu_unplug(int p){ for(int k=0;k<GPU_MAX;k++){ struct gpu* d=&g_gpu[k]; if(d->ok&&d->ctl==g_xcur&&d->L.root==p)gpu_gone(d); } }
void gpu_reset_all(void){ for(int k=0;k<GPU_MAX;k++) if(g_gpu[k].ok){ g_gpu[k].ok=0; g_gpu[k].slot=0; gpad_remove(g_gpu[k].pad); g_gpu[k].pad=-1; } }
void gpad_pointer(int dx,int dy,int click){ if(dx||dy)in_rel(dx,dy); if(click==1)in_btn_raw(1,0,0); else if(click==2)in_btn_raw(0,0,0); }
