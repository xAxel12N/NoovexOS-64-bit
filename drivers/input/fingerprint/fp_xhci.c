#include "include/nvx/drivers/input/fingerprint/fprint.h"
static struct { int ok, ctl, slot, port, kind, ndci, odci[16], mps[16], typ[16], itv[16]; u32 mem, inctx, devctx, ep0, dbuf, data, ring[16], enq[16], cyc[16], e0_enq, e0_cyc; u16 vid, pid; } fpu;
static u32 fpu_ring(int dci){ return fpu.mem+0x3000u+(u32)(dci==2?0:dci==3?1:dci==5?2:3)*0x1000u; }
static int fpu_mem(void){
    if(fpu.mem)return 1;
    u8* p=(u8*)dma_alloc_pages(9); if(!p)return 0;
    fpu.mem=(u32)(u64)p; fpu.inctx=fpu.mem; fpu.devctx=fpu.mem+0x1000u; fpu.ep0=fpu.mem+0x2000u; fpu.dbuf=fpu.mem+0x7000u; fpu.data=fpu.mem+0x8000u; return 1;
}
static void fpu_release(void){
    if(fpu.slot){ int prev=g_xcur; xhc_select(fpu.ctl); xh_disable_slot(fpu.slot); xhc_select(prev); }
    fpu.slot=0; fpu.ok=0;
}
#define FFAIL(m) do{ xh_set_fail(m); return 0; }while(0)
static int fpu_enum(const usbloc_t* L){
    if(!fpu_mem())return 0;
    xhci_failstep[0]=0; xhci_failcc=0; fpu.slot=0;
    int speed=L->speed; int mps0=(speed==3)?64:(speed>=4)?512:8;
    cur_ep0=fpu.ep0; cur_dbuf=fpu.dbuf; e0_enq=0; e0_cyc=1; ring_init(fpu.ep0);
    u8 cc=0,sl=0; if(!xh_cmd_s(0,0,0,9,0,&cc,&sl)||!sl){ xhci_failcc=cc; FFAIL("Enable Slot"); }
    cur_slot=sl; fpu.slot=sl;
    ctx_zero(fpu.devctx,(int)ctx_sz()*32/4); *(volatile u32*)(u64)(XHCI_DCBAA+sl*8)=fpu.devctx; *(volatile u32*)(u64)(XHCI_DCBAA+sl*8+4)=0;
    ctx_zero(fpu.inctx,(int)ctx_sz()*33/4); volatile u32* ic=(volatile u32*)(u64)fpu.inctx; ic[1]=3u;
    volatile u32* sc=(volatile u32*)(u64)(fpu.inctx+ctx_sz()); xh_slot_ctx(sc,L,1);
    volatile u32* e0=(volatile u32*)(u64)(fpu.inctx+ctx_sz()*2); e0[1]=(4u<<3)|(3u<<1)|((u32)mps0<<16); e0[2]=fpu.ep0|1u; e0[3]=0; e0[4]=8;
    if(!xh_cmd_s(fpu.inctx,0,0,11,(u32)sl,&cc,&sl)){ xhci_failcc=cc; FFAIL("Address Device"); }
    xh_delay_ms(10);
    if(!xh_fix_mps0(fpu.inctx,L,fpu.ep0))FFAIL("GET device descriptor (8)");
    if(!xh_ctrl(0x80,6,0x0100,0,18,1,0))FFAIL("GET device descriptor");
    volatile u8* db=(volatile u8*)(u64)fpu.dbuf;
    fpu.vid=(u16)(db[8]|(db[9]<<8)); fpu.pid=(u16)(db[10]|(db[11]<<8));
    const fp_id_t* id=fprint_lookup(fpu.vid,fpu.pid); if(!id||!fprint_supported(id->kind))FFAIL("not a supported fingerprint reader"); fpu.kind=id->kind;
    if(!xh_ctrl(0x80,6,0x0200,0,9,1,0))FFAIL("GET config(9)");
    int wt=db[2]|(db[3]<<8), cv=db[5]; if(wt<9)FFAIL("bad config"); if(wt>1024)wt=1024;
    if(!xh_ctrl(0x80,6,0x0200,0,(u16)wt,1,0))FFAIL("GET config(full)");
    static u8 full[1024]; for(int i=0;i<wt;i++)full[i]=db[i];
    for(int d=0;d<16;d++){ fpu.mps[d]=0; fpu.typ[d]=0; fpu.itv[d]=0; }
    int ifn=-1, alt=0;
    for(int i=0;i+2<=wt;){ int bl=full[i]; if(bl<2||i+bl>wt)break;
        if(full[i+1]==4&&bl>=9){ ifn=full[i+2]; alt=full[i+3]; }
        else if(full[i+1]==5&&bl>=7&&ifn==0&&alt==0&&((full[i+3]&3)==2||(full[i+3]&3)==3)){ int a=full[i+2], dci=(a&0x0F)*2+((a&0x80)?1:0); if((dci==2||dci==3||dci==5||dci==7)&&!fpu.mps[dci]){ fpu.mps[dci]=(full[i+4]|(full[i+5]<<8))&0x7FF; fpu.typ[dci]=full[i+3]&3; fpu.itv[dci]=full[i+6]; } }
        i+=bl; }
    if(fpu.kind==FPK_ELAN&&(!fpu.mps[2]||!fpu.mps[5]||!fpu.mps[7]||fpu.typ[2]!=2||fpu.typ[5]!=2||fpu.typ[7]!=2))FFAIL("Elan endpoints 0x01/0x82/0x83 missing");
    if(fpu.kind==FPK_SYNA_MOC&&(!fpu.mps[2]||!fpu.mps[3]||!fpu.mps[7]))FFAIL("Synaptics endpoints 0x01/0x81/0x83 missing");
    if(fpu.kind==FPK_GOODIX_MOC&&(!fpu.mps[2]||!fpu.mps[7]||fpu.typ[7]!=2))FFAIL("Goodix endpoints 0x01/0x83 missing");
    if(!xh_ctrl(0x00,9,(u16)cv,0,0,0,0))FFAIL("SET_CONFIGURATION");
    ctx_zero(fpu.inctx,(int)ctx_sz()*33/4);
    ic=(volatile u32*)(u64)fpu.inctx; ic[1]=1u; int maxd=1; for(int d=2;d<=7;d++) if(fpu.mps[d]){ ic[1]|=1u<<d; maxd=d; }
    sc=(volatile u32*)(u64)(fpu.inctx+ctx_sz()); xh_slot_ctx(sc,L,maxd);
    for(int d=2;d<=7;d++){ if(!fpu.mps[d])continue; u32 r=fpu_ring(d); ring_init(r); fpu.enq[d]=0; fpu.cyc[d]=1; int in=d&1, it=fpu.typ[d]==3;
        volatile u32* ec=(volatile u32*)(u64)(fpu.inctx+ctx_sz()*(u32)(d+1)); ec[0]=it?xh_interval(L->speed,fpu.itv[d])<<16:0; ec[1]=(u32)(it?(in?7:3):(in?6:2))<<3|(3u<<1)|((u32)fpu.mps[d]<<16); ec[2]=r|1u; ec[3]=0; ec[4]=it?(u32)fpu.mps[d]|((u32)fpu.mps[d]<<16):1024; }
    if(!xh_cmd_s(fpu.inctx,0,0,12,(u32)fpu.slot,&cc,&sl)){ xhci_failcc=cc; FFAIL("Configure Endpoint"); }
    fpu.e0_enq=e0_enq; fpu.e0_cyc=e0_cyc; fpu.ctl=g_xcur; fpu.port=L->root; fpu.ok=1;
    return 1;
}
#undef FFAIL
static void fpu_ep_reset(int dci,int stalled){
    u8 cc=0,sl=0; if(!stalled)xh_cmd_s(0,0,0,15u|((u32)dci<<6),(u32)fpu.slot,&cc,&sl);
    xh_cmd_s(0,0,0,14u|((u32)dci<<6),(u32)fpu.slot,&cc,&sl);
    xh_cmd_s((fpu_ring(dci)+fpu.enq[dci]*16)|fpu.cyc[dci],0,0,16u|((u32)dci<<6),(u32)fpu.slot,&cc,&sl);
    int s_slot=cur_slot; u32 s_ep0=cur_ep0, s_db=cur_dbuf, s_enq=e0_enq, s_cyc=e0_cyc;
    cur_slot=fpu.slot; cur_ep0=fpu.ep0; cur_dbuf=fpu.dbuf; e0_enq=fpu.e0_enq; e0_cyc=fpu.e0_cyc;
    xh_ctrl(0x02,1,0,(u16)((dci>>1)|((dci&1)?0x80:0)),0,0,0);
    fpu.e0_enq=e0_enq; fpu.e0_cyc=e0_cyc; cur_slot=s_slot; cur_ep0=s_ep0; cur_dbuf=s_db; e0_enq=s_enq; e0_cyc=s_cyc;
}
static int fpu_td(int dci,int len,int ms){
    u32 ring=fpu_ring(dci);
    volatile u32* tr=(volatile u32*)(u64)(ring+fpu.enq[dci]*16);
    tr[0]=fpu.data; tr[1]=0; tr[2]=(u32)len; __sync_synchronize(); tr[3]=(1u<<10)|(1u<<5)|((dci&1)?(1u<<2):0)|fpu.cyc[dci];
    fpu.enq[dci]++; if(fpu.enq[dci]>=255){ volatile u32* lk=(volatile u32*)(u64)(ring+255*16); lk[3]=(lk[3]&~1u)|fpu.cyc[dci]; fpu.enq[dci]=0; fpu.cyc[dci]^=1; }
    __sync_synchronize(); xh_db(fpu.slot,(u32)dci);
    u8 cc=0; int ok=0; u64 t0=hr_ns(), lim=(u64)(ms<50?50:ms)*1000000ULL;
    xh_want_slot=fpu.slot; xh_want_ep=dci; xh_want_ptr[0]=(u32)(u64)tr; xh_want_pn=1;
    while(!ok&&hr_ns()-t0<lim)ok=xh_event_w(32,0,&cc,0,0,2,20000);
    xh_want_slot=-1; xh_want_ep=-1; xh_want_pn=0;
    if(!ok){ fpu_ep_reset(dci,0); return -1; }
    if(cc!=1&&cc!=13){ fpu_ep_reset(dci,cc==6); return -1; }
    u32 res=((volatile u32*)(u64)(XHCI_EVTRING+((ev_deq+255)&255)*16))[2]&0xFFFFFF;
    int got=len-(int)res; return got<0?0:got;
}
static int fpu_out(void* c,int ep,const u8* p,int n,int ms){
    (void)c; if(!fpu.ok||n<1||n>4096||(ep&0x80))return -1;
    volatile u8* d=(volatile u8*)(u64)fpu.data; for(int i=0;i<n;i++)d[i]=p[i];
    return fpu_td((ep&0x0F)*2,n,ms);
}
static int fpu_in(void* c,int ep,u8* p,int cap,int ms){
    (void)c; if(!fpu.ok||!(ep&0x80))return -1;
    int dci=(ep&0x0F)*2+1, tot=0;
    while(tot<cap){ int ch=cap-tot; if(ch>4096)ch=4096; int g=fpu_td(dci,ch,ms); if(g<0)return tot?tot:-1;
        volatile u8* d=(volatile u8*)(u64)fpu.data; for(int i=0;i<g;i++)p[tot+i]=d[i]; tot+=g; if(g<ch)break; }
    return tot;
}
static void fpu_sleep(int ms){ hr_delay_ms((u32)ms); }
static int fpu_open(void){
    if(fpu.ok)return 1;
    if(!g_xhc_n)return 0;
    u16 v=0,d=0; int k=fprint_find(&v,&d); if(!fprint_supported(k))return 0;
    int prev=g_xcur; int s_slot=cur_slot; u32 s_ep0=cur_ep0, s_db=cur_dbuf, s_enq=e0_enq, s_cyc=e0_cyc;
    for(int c=0;c<g_xhc_n&&!fpu.ok;c++){ xhc_select(c); if(!xhci_present||!xhci_init_ok)continue;
        for(int u=0;u<USBDEV_MAX&&!fpu.ok;u++){ usbdev_t* ud=&g_usbdev[u]; if(!ud->used||ud->ctl!=c||ud->hid||ud->cam||ud->prn||ud->vid!=v||ud->pid!=d)continue;
            int p=ud->port; if(p<0||p>=xhci_ports||!xhci_port_conn[p]||btu_owns(c,p))continue;
            int hub=0; for(int h=0;h<XHUB_MAX;h++) if(xhub[h].used&&xhub[h].ctl==c&&xhub[h].root==p)hub=1; if(hub)continue;
            int sp=xh_root_reset(p); usbloc_t L; L.root=p; L.depth=0; L.hub=-1; L.hport=0; L.speed=sp; L.route=0; L.tt_slot=0; L.tt_port=0;
            if(!sp||!fpu_enum(&L)){ if(fpu.slot){ xh_disable_slot(fpu.slot); fpu.slot=0; } char b[96],*q=b; q=scopy(q,"fingerprint: reader setup failed: "); q=scopy(q,xhci_failstep); *q=0; klog(b); }
            else usb_seen[p]=1; } }
    xhc_select(prev); cur_slot=s_slot; cur_ep0=s_ep0; cur_dbuf=s_db; e0_enq=s_enq; e0_cyc=s_cyc;
    return fpu.ok;
}
static int fpu_sess;
static void fpu_sess_drop(void){ if(fpu_sess){ int prev=g_xcur; if(fpu_sess==1){ xhc_select(fpu.ctl); elanfp_stop(); xhc_select(prev); } fpu_release(); fpu_sess=0; } }
int fprint_usb_capture(fp_image_t* img,int finger_ms,char* msg,int cap){
    const char* m="no supported fingerprint reader"; int r=0;
    if(!btu_lock()){ m="USB busy"; goto out; }
    fpu_sess_drop();
    if(!fpu_open())goto unl;
    if(fpu.kind!=FPK_ELAN){ fpu_release(); m="this reader matches on the chip, no image"; goto unl; }
    int prev=g_xcur; xhc_select(fpu.ctl);
    fp_xport_t x; x.ctx=0; x.out=fpu_out; x.in=fpu_in; x.sleep_ms=fpu_sleep;
    elanfp_open(&x,fpu.pid);
    if(!elanfp_activate())m="reader did not answer (activate)";
    else if(!elanfp_calibrate())m="reader calibration failed";
    else { r=elanfp_capture(img,finger_ms); m=r>0?"image captured":r<0?"finger was on the reader during calibration, lift it and retry":"no finger detected"; }
    xhc_select(prev); fpu_release();
unl: btu_unlock();
out: if(msg&&cap>0){ int i=0; for(;m[i]&&i<cap-1;i++)msg[i]=m[i]; msg[i]=0; }
    return r;
}
static void fpm_copy(char* msg,int cap,const char* m){ if(msg&&cap>0){ int i=0; for(;m[i]&&i<cap-1;i++)msg[i]=m[i]; msg[i]=0; } }
int fprint_usb_moc(int op,const char* uid,u8* tid,void (*prog)(int pct),char* msg,int cap){
    const char* m="no supported fingerprint reader"; int r=-1;
    if(!btu_lock()){ fpm_copy(msg,cap,"USB busy"); return -1; }
    fpu_sess_drop();
    if(!fpu_open()||!fprint_is_moc(fpu.kind)){ if(fpu.ok)fpu_release(); btu_unlock(); fpm_copy(msg,cap,m); return -1; }
    int prev=g_xcur; xhc_select(fpu.ctl);
    fp_xport_t x; x.ctx=0; x.out=fpu_out; x.in=fpu_in; x.sleep_ms=fpu_sleep;
    if(fpu.kind==FPK_SYNA_MOC){ synafp_open(&x);
        if(!synafp_init())m="reader did not answer (init)";
        else if(op==FPM_ENROLL){ r=synafp_enroll(uid,1,prog); m=r==1?"enrolled on the reader":r==-2?"the reader's fingerprint storage is full":"enrollment failed"; }
        else if(op==FPM_VERIFY){ r=synafp_verify(uid); m=r==1?"match":r==0?"no match":r==-2?"not enrolled on this reader":r==-3?"bad sample, try again":"verify failed"; }
        else { r=synafp_delete(uid,1); m=r?"deleted from the reader":"delete failed"; } }
    else { goodixfp_open(&x); goodixfp_set_stages(fprint_gx_stages(fpu.pid));
        if(!goodixfp_init())m="reader did not answer (init)";
        else if(op==FPM_ENROLL){ r=goodixfp_enroll(uid,prog); if(r==1&&tid){ const u8* t=goodixfp_tid(); for(int i=0;i<32;i++)tid[i]=t[i]; } m=r==1?"enrolled on the reader":r==-2?"the reader's fingerprint storage is full":r==-3?"this finger is already enrolled":"enrollment failed"; }
        else if(op==FPM_VERIFY){ r=goodixfp_verify(uid); m=r==1?"match":r==0?"no match":r==-3?"bad sample, try again":"verify failed"; }
        else { r=tid?goodixfp_delete(uid,tid):0; m=r?"deleted from the reader":"delete failed"; } }
    xhc_select(prev); fpu_release(); btu_unlock(); fpm_copy(msg,cap,m); return r;
}
int fprint_usb_slice(fp_image_t* img,int finger_ms,char* msg,int cap){
    const char* m="USB busy"; int r=0;
    if(!btu_lock())goto out;
    m="no supported fingerprint reader";
    if(fpu_sess!=1){ fpu_sess_drop(); if(!fpu_open())goto unl; }
    if(fpu.kind!=FPK_ELAN){ fpu_release(); m="this reader matches on the chip, no image"; goto unl; }
    int prev=g_xcur; xhc_select(fpu.ctl);
    if(fpu_sess!=1){ fp_xport_t x; x.ctx=0; x.out=fpu_out; x.in=fpu_in; x.sleep_ms=fpu_sleep; elanfp_open(&x,fpu.pid);
        if(!elanfp_activate())m="reader did not answer (activate)"; else if(!elanfp_calibrate())m="reader calibration failed"; else fpu_sess=1;
        if(fpu_sess!=1){ xhc_select(prev); fpu_release(); goto unl; } }
    r=elanfp_capture(img,finger_ms);
    if(r<0){ fpu_sess=0; elanfp_stop(); fpu_release(); m="finger was on the reader during calibration, lift it and retry"; }
    else m=r>0?"image captured":"no finger detected";
    xhc_select(prev);
unl: btu_unlock();
out: fpm_copy(msg,cap,m); return r<0?0:r;
}
void fprint_usb_end(void){ if(fpu_sess&&btu_lock()){ fpu_sess_drop(); btu_unlock(); } }
int fprint_moc_enroll_begin(const char* uid,char* msg,int cap){
    const char* m="no supported fingerprint reader"; int r=-1;
    if(!btu_lock()){ fpm_copy(msg,cap,"USB busy"); return -1; }
    fpu_sess_drop();
    if(!fpu_open()||!fprint_is_moc(fpu.kind)){ if(fpu.ok)fpu_release(); btu_unlock(); fpm_copy(msg,cap,m); return -1; }
    int prev=g_xcur; xhc_select(fpu.ctl);
    fp_xport_t x; x.ctx=0; x.out=fpu_out; x.in=fpu_in; x.sleep_ms=fpu_sleep;
    if(fpu.kind==FPK_SYNA_MOC){ synafp_open(&x); if(!synafp_init())m="reader did not answer (init)"; else if(synafp_enroll_begin(uid,1))m="the reader refused to start enrolment"; else r=0; }
    else { goodixfp_open(&x); goodixfp_set_stages(fprint_gx_stages(fpu.pid)); if(!goodixfp_init())m="reader did not answer (init)"; else if(goodixfp_enroll_begin(uid))m="the reader refused to start enrolment"; else r=0; }
    xhc_select(prev); if(r)fpu_release(); else { fpu_sess=2; m="touch the sensor"; }
    btu_unlock(); fpm_copy(msg,cap,m); return r;
}
int fprint_moc_enroll_step(u8* tid,int* pct,char* msg,int cap){
    if(fpu_sess!=2||!fpu.ok){ fpm_copy(msg,cap,"enrolment is not running"); return -1; }
    if(!btu_lock())return 0;
    int prev=g_xcur; xhc_select(fpu.ctl);
    int syn=fpu.kind==FPK_SYNA_MOC, r=syn?synafp_enroll_step(pct):goodixfp_enroll_step(pct);
    if(r==1&&tid&&!syn){ const u8* t=goodixfp_tid(); for(int i=0;i<32;i++)tid[i]=t[i]; }
    xhc_select(prev);
    if(r){ fpu_release(); fpu_sess=0; fpm_copy(msg,cap,r==1?"enrolled on the reader":r==-2?"the reader's fingerprint storage is full":r==-3?"this finger is already enrolled":"enrollment failed"); }
    btu_unlock(); return r;
}
