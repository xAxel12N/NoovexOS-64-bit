#include "include/nvx/drivers/usb/mtp.h"
static struct { int ok, ctl, slot, in_dci, out_dci, in_ep, out_ep, iface, depth, tries; u32 mem, inctx, devctx, ep0, dbuf, inring, outring, buf, in_enq, in_cyc, out_enq, out_cyc, e0_enq, e0_cyc, route; u64 lastpoll; usbloc_t L; } mtx_;
static int mx_mem(void){ if(mtx_.mem)return 1; u8* p=(u8*)dma_alloc_pages(24); if(!p){ klog("mtp: no memory"); return 0; }
    mtx_.mem=(u32)(u64)p; mtx_.inctx=mtx_.mem; mtx_.devctx=mtx_.mem+0x1000u; mtx_.ep0=mtx_.mem+0x2000u; mtx_.inring=mtx_.mem+0x3000u; mtx_.outring=mtx_.mem+0x4000u; mtx_.dbuf=mtx_.mem+0x5000u; mtx_.buf=(mtx_.mem+0x6000u+0xFFFFu)&~0xFFFFu; if(mtx_.buf+0x10000u>mtx_.mem+24u*4096u)mtx_.buf=mtx_.mem+0x7000u; return 1; }
static int mx_ctrl(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in){
    int s_slot=cur_slot; u32 s_ep0=cur_ep0, s_db=cur_dbuf, s_enq=e0_enq, s_cyc=e0_cyc;
    cur_slot=mtx_.slot; cur_ep0=mtx_.ep0; cur_dbuf=mtx_.dbuf; e0_enq=mtx_.e0_enq; e0_cyc=mtx_.e0_cyc;
    int r=xh_ctrl(rt,req,val,idx,len,in,0); mtx_.e0_enq=e0_enq; mtx_.e0_cyc=e0_cyc;
    cur_slot=s_slot; cur_ep0=s_ep0; cur_dbuf=s_db; e0_enq=s_enq; e0_cyc=s_cyc; return r; }
static void mx_unhalt(int dci,int epaddr,u32 ring,u32* enq,u32* cyc,int stalled){ u8 cc=0,sl=0;
    xh_cmd_s(0,0,0,(stalled?14u:15u)|((u32)dci<<6),(u32)mtx_.slot,&cc,&sl);
    xh_cmd_s((ring+(*enq)*16)|(*cyc),0,0,16u|((u32)dci<<6),(u32)mtx_.slot,&cc,&sl); if(stalled)mx_ctrl(0x02,1,0,(u16)epaddr,0,0); }
static int mx_td(int out,int n,int ms){
    if(!mtx_.ok||n<0||n>0x10000)return MTP_E_ARG; xhc_select(mtx_.ctl);
    u32 ring=out?mtx_.outring:mtx_.inring; u32* enq=out?&mtx_.out_enq:&mtx_.in_enq; u32* cyc=out?&mtx_.out_cyc:&mtx_.in_cyc; int dci=out?mtx_.out_dci:mtx_.in_dci;
    volatile u32* tr=(volatile u32*)(u64)(ring+(*enq)*16); tr[0]=mtx_.buf; tr[1]=0; tr[2]=(u32)n; __sync_synchronize(); tr[3]=(1u<<10)|(1u<<5)|(1u<<2)|(*cyc);
    (*enq)++; if(*enq>=255){ volatile u32* lk=(volatile u32*)(u64)(ring+255*16); lk[3]=(lk[3]&~1u)|(*cyc); *enq=0; *cyc^=1; }
    __sync_synchronize(); xh_db(mtx_.slot,(u32)dci);
    u8 cc=0; int ok=0; u64 t0=g_ticks; u32 lim=(u32)(ms/10+1);
    xh_want_slot=mtx_.slot; xh_want_ep=dci; xh_want_ptr[0]=(u32)(u64)tr; xh_want_pn=1;
    while(!ok&&g_ticks-t0<=lim){ ok=xh_event_w(32,0,&cc,0,0,lim,XH_IT_BULK); if(!ok&&g_ticks==t0)break; }
    xh_want_slot=-1; xh_want_ep=-1; xh_want_pn=0;
    if(!ok){ mx_unhalt(dci,out?mtx_.out_ep:mtx_.in_ep,ring,enq,cyc,0); return MTP_E_TIMEOUT; }
    if(cc==6){ mx_unhalt(dci,out?mtx_.out_ep:mtx_.in_ep,ring,enq,cyc,1); return MTP_E_IO; }
    if(cc!=1&&cc!=13)return MTP_E_IO; return n; }
static int mx_out(const u8* b,int n,int ms){ if(n>0x10000)return MTP_E_ARG; volatile u8* d=(volatile u8*)(u64)mtx_.buf; for(int i=0;i<n;i++)d[i]=b[i]; int r=mx_td(1,n,ms); if(r<0)return r; if(n&&!(n&511))mx_td(1,0,ms); return n; }
static int mx_in(u8* b,int n,int ms){ if(n>0x10000)return MTP_E_ARG; volatile u8* d=(volatile u8*)(u64)mtx_.buf; for(int i=0;i<12&&i<n;i++)d[i]=0; int r=mx_td(0,n,ms); if(r<0)return r;
    u32 cl=(u32)d[0]|((u32)d[1]<<8)|((u32)d[2]<<16)|((u32)d[3]<<24); int got=n; if(n>=12&&(d[4]==2||d[4]==3)&&d[5]==0&&cl>=12&&cl<(u32)n)got=(int)cl; else if(n>=12&&cl==0&&d[4]==0)got=0;
    for(int i=0;i<got;i++)b[i]=d[i]; return got; }
static int mx_reset(void){ if(!mtx_.ok)return -1; mx_ctrl(0x21,0x66,0,(u16)mtx_.iface,0,0); mx_unhalt(mtx_.in_dci,mtx_.in_ep,mtx_.inring,&mtx_.in_enq,&mtx_.in_cyc,1); mx_unhalt(mtx_.out_dci,mtx_.out_ep,mtx_.outring,&mtx_.out_enq,&mtx_.out_cyc,1); return 0; }
static const mtp_tr_t mx_tr={mx_out,mx_in,mx_reset,0x10000};
#define MXFAIL(m) do{ xh_set_fail(m); if(mtx_.slot){ xh_disable_slot(mtx_.slot); mtx_.slot=0; } return 0; }while(0)
static int mtp_enum_loc(const usbloc_t* L){
    if(mtx_.ok||mtx_.tries>=3||!mx_mem())return -1; mtx_.slot=0;
    int speed=L->speed; int mps0=(speed==3)?64:(speed>=4)?512:8;
    cur_ep0=mtx_.ep0; cur_dbuf=mtx_.dbuf; e0_enq=0; e0_cyc=1; ring_init(mtx_.ep0);
    u8 cc=0,sl=0; if(!xh_cmd_s(0,0,0,9,0,&cc,&sl)||!sl)MXFAIL("Enable Slot"); cur_slot=sl; mtx_.slot=sl;
    ctx_zero(mtx_.devctx,(int)ctx_sz()*32/4); *(volatile u32*)(u64)(XHCI_DCBAA+sl*8)=mtx_.devctx; *(volatile u32*)(u64)(XHCI_DCBAA+sl*8+4)=0;
    ctx_zero(mtx_.inctx,(int)ctx_sz()*33/4); volatile u32* ic=(volatile u32*)(u64)mtx_.inctx; ic[1]=3u;
    volatile u32* sc=(volatile u32*)(u64)(mtx_.inctx+ctx_sz()); xh_slot_ctx(sc,L,1);
    volatile u32* e0=(volatile u32*)(u64)(mtx_.inctx+ctx_sz()*2); e0[1]=(4u<<3)|(3u<<1)|((u32)mps0<<16); e0[2]=mtx_.ep0|1u; e0[3]=0; e0[4]=8;
    if(!xh_cmd_s(mtx_.inctx,0,0,11,(u32)sl,&cc,&sl))MXFAIL("Address Device");
    xh_delay_ms(10);
    if(!xh_fix_mps0(mtx_.inctx,L,mtx_.ep0))MXFAIL("GET device descriptor (8)");
    if(!xh_ctrl(0x80,6,0x0100,0,18,1,0))MXFAIL("GET device descriptor");
    volatile u8* db=(volatile u8*)(u64)mtx_.dbuf; u16 vid=(u16)(db[8]|(db[9]<<8)), pid=(u16)(db[10]|(db[11]<<8));
    if(!xh_ctrl(0x80,6,0x0200,0,9,1,0))MXFAIL("GET config");
    int wt=db[2]|(db[3]<<8), cv=db[5]; if(wt<9)MXFAIL("config"); if(wt>1024)wt=1024;
    if(!xh_ctrl(0x80,6,0x0200,0,(u16)wt,1,0))MXFAIL("GET config (full)");
    static u8 full[1024]; for(int i=0;i<wt;i++)full[i]=db[i];
    mtp_if_t mi; int m=mtp_match(full,wt,&mi);
    if(m==1){ int okstr=0; if(mi.istr&&xh_ctrl(0x80,6,(u16)(0x0300|mi.istr),0x0409,255,1,0)){ u8 s[255]; for(int i=0;i<255;i++)s[i]=db[i]; okstr=mtp_str_is_mtp(s,255); } if(!okstr)m=0; }
    if(!m){ if(mtp_android_vid(vid)){ extern void notify(const char*,const char*); usbdev_note(L->root,vid,pid,full,wt,"Android phone"); notify("Phone connected for charging only","To see its files, unlock it and choose File transfer in the USB notification"); } MXFAIL("not MTP"); }
    if(!xh_ctrl(0x00,9,(u16)cv,0,0,0,0))MXFAIL("SET_CONFIGURATION");
    mtx_.in_ep=mi.in_ep; mtx_.out_ep=mi.out_ep; mtx_.iface=mi.iface; mtx_.in_dci=(mi.in_ep&15)*2+1; mtx_.out_dci=(mi.out_ep&15)*2; int maxdci=mtx_.in_dci>mtx_.out_dci?mtx_.in_dci:mtx_.out_dci;
    ctx_zero(mtx_.inctx,(int)ctx_sz()*33/4); ic=(volatile u32*)(u64)mtx_.inctx; ic[1]=1u|(1u<<mtx_.in_dci)|(1u<<mtx_.out_dci);
    sc=(volatile u32*)(u64)(mtx_.inctx+ctx_sz()); xh_slot_ctx(sc,L,maxdci);
    ring_init(mtx_.inring); ring_init(mtx_.outring); mtx_.in_enq=0; mtx_.in_cyc=1; mtx_.out_enq=0; mtx_.out_cyc=1;
    volatile u32* ei=(volatile u32*)(u64)(mtx_.inctx+ctx_sz()*(u32)(mtx_.in_dci+1)); ei[1]=(6u<<3)|(3u<<1)|((u32)(mi.in_mps?mi.in_mps:512)<<16); ei[2]=mtx_.inring|1u; ei[3]=0; ei[4]=1024;
    volatile u32* eo=(volatile u32*)(u64)(mtx_.inctx+ctx_sz()*(u32)(mtx_.out_dci+1)); eo[1]=(2u<<3)|(3u<<1)|((u32)(mi.out_mps?mi.out_mps:512)<<16); eo[2]=mtx_.outring|1u; eo[3]=0; eo[4]=1024;
    if(!xh_cmd_s(mtx_.inctx,0,0,12,(u32)mtx_.slot,&cc,&sl))MXFAIL("Configure Endpoint");
    mtx_.e0_enq=e0_enq; mtx_.e0_cyc=e0_cyc; mtx_.ctl=g_xcur; mtx_.L=*L; mtx_.route=L->route; mtx_.depth=L->depth; mtx_.ok=1;
    if(mtp_open(&mx_tr,vid,pid)<0){ mtx_.ok=0; mtx_.tries++; MXFAIL("MTP session"); }
    mtx_.tries=0; usbdev_note(L->root,vid,pid,full,wt,g_mtp.model[0]?g_mtp.model:mtp_kind_name());
    if(g_mtp.locked){ extern void notify(const char*,const char*); notify("Unlock your phone","Choose File transfer in its USB notification to see its files"); }
    return 1; }
#undef MXFAIL
static int mtp_enum_root(int port){ int sp=xh_root_reset(port); if(!sp)return 0; usbloc_t L; L.root=port; L.depth=0; L.hub=-1; L.hport=0; L.speed=sp; L.route=0; L.tt_slot=0; L.tt_port=0; return mtp_enum_loc(&L); }
static void mtp_drop(void){ if(!mtx_.ok)return; g_mtp.dead=1; mtp_close(); if(mtx_.slot)xh_disable_slot(mtx_.slot); mtx_.slot=0; mtx_.ok=0; g_usbdev_gen++; klog("mtp: phone or camera removed"); }
static void mtp_remove_below(int root,u32 R,int dR){ if(mtx_.ok&&mtx_.ctl==g_xcur&&mtx_.L.root==root&&xh_under(mtx_.route,mtx_.depth,R,dR))mtp_drop(); }
static void mtp_unplug(int port){ if(mtx_.ok&&mtx_.ctl==g_xcur&&mtx_.L.root==port)mtp_drop(); }
static void mtp_poll(void){ if(!mtx_.ok||mtx_.ctl!=g_xcur||!g_mtp.locked||g_mtp.dead||g_ticks-mtx_.lastpoll<200)return; mtx_.lastpoll=g_ticks; mtp_refresh(); if(!g_mtp.locked){ g_usbdev_gen++; klog("mtp: phone unlocked - storage visible"); } }
