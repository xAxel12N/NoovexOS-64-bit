int ufat_poke(const char* name,const char* data,int len){
    if(!ufat_ok||!msd_ok||len<=0||len>512)return -1;
    for(int i=0;i<ufs_n;i++){ const char* a=ufs[i].name; int k=0; while(a[k]&&name[k]&&((a[k]>='a'&&a[k]<='z')?a[k]-32:a[k])==((name[k]>='a'&&name[k]<='z')?name[k]-32:name[k]))k++;
        if(a[k]||name[k])continue;
        if(ufs[i].size!=(u32)len||ufs[i].clus<2)break;
        u8 s[512]; for(int q=0;q<512;q++)s[q]=0; for(int q=0;q<len;q++)s[q]=(u8)data[q];
        return umsd_write(ufat_dataclus+(ufs[i].clus-2)*ufat_spc,1,s)<0?-1:0; }
    return ufat_write_file(name,data,len); }
int ufat_delete_file(int idx){
    if(idx<0||idx>=ufs_n||!ufat_ok)return -1; char n83[11]; ufat_make83(ufs[idx].name,n83); u32 lba; int off; u8 sec[512];
    if(!ufat_dirent_scan(n83,0,&lba,&off))return -1; if(umsd_read(lba,1,sec)<0)return -1; u8* d=sec+off;
    u32 oc=(d[0x1A]|(d[0x1B]<<8))|((u32)(d[0x14]|(d[0x15]<<8))<<16); if(oc>=2)ufat_free_chain(oc);
    d[0]=0xE5; if(umsd_write(lba,1,sec)<0)return -1; ufat_flush(); ufat_mount(); return 0; }
/* ---- USB hotplug: auto-mount + auto-open the file manager when a stick is inserted ---- */
int g_files_usb=0, g_usb_arrived=0, msd_port=-1;static u8 usb_seen[32]={0}; static u64 usb_poll_t=0; static int usb_seen_init=0;
static u8* xhub_db(xhub_t* h){ return (u8*)(u64)XHID_DATABUF(h->reg); }
static int xhub_ctrl(xhub_t* h,u8 rt,u8 req,u16 val,u16 idx,u16 len,int in){
    int s_slot=cur_slot; u32 s_ep0=cur_ep0, s_db=cur_dbuf, s_enq=e0_enq, s_cyc=e0_cyc;
    cur_slot=h->slot; cur_ep0=XHID_EP0RING(h->reg); cur_dbuf=XHID_DATABUF(h->reg); e0_enq=h->e0_enq; e0_cyc=h->e0_cyc;
    int r=xh_ctrl(rt,req,val,idx,len,in,0);
    h->e0_enq=e0_enq; h->e0_cyc=e0_cyc;
    cur_slot=s_slot; cur_ep0=s_ep0; cur_dbuf=s_db; e0_enq=s_enq; e0_cyc=s_cyc; return r; }
static int xhub_status(xhub_t* h,int p,u16* st,u16* ch){ if(!xhub_ctrl(h,0xA3,0,0,(u16)p,4,1))return 0; u8* b=xhub_db(h); *st=(u16)(b[0]|(b[1]<<8)); *ch=(u16)(b[2]|(b[3]<<8)); return 1; }
static void xh_delay_ms(int ms){ for(int i=0;i<ms;i++) for(volatile int k=0;k<1000;k++) io_wait(); }
static int xh_under(u32 route,int depth,u32 R,int dR){ if(depth<dR)return 0; u32 m=dR>=5?0xFFFFFu:((1u<<(4*dR))-1u); return (route&m)==(R&m); }
static void xhub_remove_below(int root,u32 R,int dR){
    for(int i=0;i<xhid_n;i++) if(xhid_used[i]&&xhid_ctl[i]==g_xcur&&xhid_port[i]==root&&xh_under(xhid_route[i],xhid_depth[i],R,dR)){ int sl=xhid_slot[i];
        for(int k=0;k<xhid_n;k++) if(xhid_used[k]&&xhid_ctl[k]==g_xcur&&xhid_slot[k]==sl){ xhid_used[k]=0; if(usbk_rep_dev==k){ usbk_rep_code=0; usbk_rep_dev=-1; } for(int z=0;z<8;z++)xhid_kprev[k][z]=0; }
        xh_disable_slot(sl); }
    for(int h=0;h<XHUB_MAX;h++) if(xhub[h].used&&xhub[h].ctl==g_xcur&&xhub[h].root==root&&xh_under(xhub[h].route,xhub[h].depth,R,dR)){ xh_disable_slot(xhub[h].slot); xreg_hub[xhub[h].reg]=0; xhub[h].used=0; }
    if(msd_ok&&msd_ctl==g_xcur&&msd_port==root&&xh_under(msd_route,msd_depth,R,dR)){ xh_disable_slot(msd_slot); msd_ok=0; ufat_ok=0; ufs_n=0; msd_port=-1; g_files_usb=0; }
    int any=0; for(int i=0;i<xhid_n;i++) if(xhid_used[i])any=1; if(!any)xhci_mouse_ok=0; }
static int xhub_port_reset(int hi,int p){
    xhub_t* h=&xhub[hi]; u16 st=0,ch=0;
    if(!xhub_ctrl(h,0x23,3,4,(u16)p,0,0))return 0;
    for(int t=0;t<100;t++){ xh_delay_ms(5); if(!xhub_status(h,p,&st,&ch))return 0; if((ch&(1u<<4))||(!(st&(1u<<4))&&(st&2u)))break; }
    xhub_ctrl(h,0x23,1,20,(u16)p,0,0); if(ch&1u)xhub_ctrl(h,0x23,1,16,(u16)p,0,0);
    xh_delay_ms(10);
    if(!xhub_status(h,p,&st,&ch))return 0; if(!(st&1u)||!(st&2u))return 0;
    if(h->ss)return 4; if(st&(1u<<9))return 2; if(st&(1u<<10))return 3; return 1; }
static void xhub_child_loc(int hi,int p,int sp,usbloc_t* C){ xhub_t* h=&xhub[hi]; C->root=h->root; C->depth=h->depth+1; C->hub=hi; C->hport=p; C->speed=sp;
    C->route=h->route|((u32)(p>15?15:p)<<(4*h->depth));
    if(h->speed==3&&(sp==1||sp==2)){ C->tt_slot=h->slot; C->tt_port=p; } else { C->tt_slot=h->tt_slot; C->tt_port=h->tt_port; } }
static int xhub_enum_port(int hi,int p){
    if(xhub[hi].depth+1>5)return 0;
    int sp=xhub_port_reset(hi,p); if(!sp)return 0;
    usbloc_t C; xhub_child_loc(hi,p,sp,&C);
    if(!msd_ok){ if(xhci_msd_enum_loc(&C)){ msd_ctl=g_xcur; msd_port=C.root; ufat_mount(); if(ufat_ok)g_usb_arrived=1; klog("usb: USB stick behind a hub"); return 0; }
        xh_disable_slot(msd_slot); msd_slot=0; sp=xhub_port_reset(hi,p); if(!sp)return 0; xhub_child_loc(hi,p,sp,&C); }
    int r=xhci_enum_loc(&C); aud_sync_e0(); if(r>0)klog("usb: keyboard/mouse behind a hub set up"); return r>0?r:0; }
static int xhub_attach(const usbloc_t* L,int d,const u8* full,int wtot,u8 cfgval){
    int hi=-1; for(int h=0;h<XHUB_MAX;h++) if(!xhub[h].used){ hi=h; break; }
    if(hi<0||L->depth>=5){ xh_set_fail("too many USB hubs"); return -1; }
    int ep=0,mps=1,intv=12,inhub=0;
    for(int i=0;i+2<wtot;){ int bl=full[i]; if(bl<2)break; if(full[i+1]==4&&i+5<wtot)inhub=(full[i+5]==9); else if(full[i+1]==5&&inhub&&i+6<wtot&&(full[i+2]&0x80)&&(full[i+3]&3)==3&&!ep){ ep=full[i+2]; mps=full[i+4]|(full[i+5]<<8); intv=full[i+6]; } i+=bl; }
    if(!xh_ctrl(0x00,9,cfgval,0,0,0,0)){ xh_set_fail("hub SET_CONFIGURATION"); return -1; }
    int ss=L->speed>=4;
    if(!xh_ctrl(0xA0,6,ss?0x2A00:0x2900,0,ss?12:9,1,0)){ xh_set_fail("hub descriptor"); return -1; }
    volatile u8* db=(volatile u8*)(u64)cur_dbuf; int np=db[2]; if(np>15)np=15; u16 chr=(u16)(db[3]|(db[4]<<8)); int p2g=db[5]*2;
    if(np<1){ xh_set_fail("hub reports no ports"); return -1; }
    xhub_t* h=&xhub[hi]; for(u32 i=0;i<sizeof(*h);i++)((u8*)h)[i]=0;
    h->ctl=g_xcur; h->reg=d; h->slot=cur_slot; h->root=L->root; h->depth=L->depth; h->speed=L->speed; h->route=L->route; h->tt_slot=L->tt_slot; h->tt_port=L->tt_port; h->parent=L->hub; h->pport=L->hport; h->nports=np; h->ttt=(chr>>5)&3; h->ss=ss;
    int dci=ep?(ep&0x0F)*2+1:1;
    ctx_zero(XHID_INPUTCTX(d),(int)ctx_sz()*(dci+2)/4);
    volatile u32* ic=(volatile u32*)(u64)XHID_INPUTCTX(d); ic[1]=1u|(ep?(1u<<dci):0u);
    volatile u32* sc=(volatile u32*)(u64)(XHID_INPUTCTX(d)+ctx_sz()); xh_slot_ctx(sc,L,dci); sc[0]|=1u<<26; sc[1]|=(u32)np<<24; if(L->speed==3)sc[2]|=(u32)h->ttt<<16;
    if(ep){ volatile u32* er=(volatile u32*)(u64)XHID_EPINRING(d); for(int i=0;i<256*4;i++)er[i]=0; int li=255*4; er[li]=XHID_EPINRING(d); er[li+3]=(6u<<10)|(1u<<1)|1u;
        volatile u32* e=(volatile u32*)(u64)(XHID_INPUTCTX(d)+ctx_sz()*(u32)(dci+1)); e[0]=xh_interval(L->speed,intv)<<16; e[1]=(7u<<3)|(3u<<1)|((u32)mps<<16); e[2]=XHID_EPINRING(d)|1u; e[4]=(u32)mps|((u32)mps<<16); }
    u8 cc=0,sl=0; if(!xh_cmd_s(XHID_INPUTCTX(d),0,0,12,(u32)cur_slot,&cc,&sl)){ xhci_failcc=cc; xh_set_fail("hub Configure Endpoint"); return -1; }
    if(ss)xh_ctrl(0x20,12,(u16)L->depth,0,0,0,0);
    h->e0_enq=e0_enq; h->e0_cyc=e0_cyc; h->used=1; xreg_hub[d]=1;
    for(int p=1;p<=np;p++)xhub_ctrl(h,0x23,3,8,(u16)p,0,0);
    xh_delay_ms(p2g<100?100:p2g);
    klog("usb: hub set up");
    int got=0;
    for(int p=1;p<=np;p++){ u16 st=0,ch=0; if(!xhub_status(&xhub[hi],p,&st,&ch))continue; if(ch&1u)xhub_ctrl(&xhub[hi],0x23,1,16,(u16)p,0,0);
        if(st&1u){ xhub[hi].conn|=1u<<p; got+=xhub_enum_port(hi,p); } }
    return got; }
static void xhub_poll_ports(void){
    for(int hi=0;hi<XHUB_MAX;hi++){ if(!xhub[hi].used||xhub[hi].ctl!=g_xcur)continue;
        for(int p=1;p<=xhub[hi].nports;p++){ xhub_t* h=&xhub[hi]; u16 st=0,ch=0; if(!xhub_status(h,p,&st,&ch))break;
            if(ch&1u)xhub_ctrl(h,0x23,1,16,(u16)p,0,0);
            int c=st&1u, was=(int)((h->conn>>p)&1u);
            u32 R=h->route|((u32)(p>15?15:p)<<(4*h->depth));
            if(was&&(!c||(ch&1u))){ h->conn&=~(1u<<p); xhub_remove_below(h->root,R,h->depth+1); }
            if(c&&!((xhub[hi].conn>>p)&1u)){ xhub[hi].conn|=1u<<p; xhub_enum_port(hi,p); }
            if(!xhub[hi].used)break; } } }
usbdev_t g_usbdev[USBDEV_MAX]; int g_usbdev_n=0; u32 g_usbdev_gen=0;
static const char* usb_cls_name(int c,int proto){ switch(c){ case 1: return "Audio device"; case 2: return "Network adapter"; case 3: return proto==1?"Keyboard":proto==2?"Mouse":"Input device"; case 6: return "Camera (still image)"; case 7: return "Printer"; case 8: return "USB drive"; case 9: return "USB hub"; case 0x0B: return "Card reader"; case 0x0E: return "Camera"; case 0xE0: return "Bluetooth adapter"; case 0xEF: return "Device"; case 0xFF: return "Vendor device"; } return "USB device"; }
void usbdev_note(int port,u16 vid,u16 pid,const u8* cfg,int n,const char* name){
    int kind=-1, kbd=0, mouse=0, cam=0, prn=0;
    for(int i=0;i+8<n;){ int bl=cfg[i]; if(bl<2)break; if(cfg[i+1]==4){ int c=cfg[i+5], pr=cfg[i+7]; if(c==3&&pr==1)kbd=1; else if(c==3&&pr==2)mouse=1; else if(c==0x0E)cam=1; else if(c==7)prn=1; if(kind<0)kind=c; } i+=bl; }
    int slot=-1; for(int k=0;k<USBDEV_MAX;k++) if(g_usbdev[k].used&&g_usbdev[k].ctl==g_xcur&&g_usbdev[k].port==port&&g_usbdev[k].vid==vid&&g_usbdev[k].pid==pid){ slot=k; break; }
    if(slot<0) for(int k=0;k<USBDEV_MAX;k++) if(!g_usbdev[k].used){ slot=k; break; } if(slot<0)return;
    usbdev_t* d=&g_usbdev[slot]; int fresh=!d->used; d->used=1; d->ctl=g_xcur; d->port=port; d->vid=vid; d->pid=pid;
    const char* kn=cam?"Camera":prn?"Printer":(kbd&&mouse)?"Keyboard and mouse":kbd?"Keyboard":mouse?"Mouse":usb_cls_name(kind,0); int q=0; for(;kn[q]&&q<31;q++)d->kind[q]=kn[q]; d->kind[q]=0;
    q=0; for(;name&&name[q]&&q<39;q++)d->name[q]=name[q]; d->name[q]=0; d->cam=cam; d->prn=prn; d->hid=(kbd||mouse); d->t=g_ticks;
    if(slot>=g_usbdev_n)g_usbdev_n=slot+1; g_usbdev_gen++;
    if(fresh&&usb_seen_init){ extern void notify(const char*,const char*); char b[80],*p=b; p=scopy(p,d->kind); p=scopy(p," connected"); *p=0; notify(b,d->name[0]?d->name:"USB"); } }
void usbdev_gone(int port){ for(int k=0;k<USBDEV_MAX;k++) if(g_usbdev[k].used&&g_usbdev[k].ctl==g_xcur&&g_usbdev[k].port==port){ g_usbdev[k].used=0; g_usbdev_gen++; } }
int msd_autoinit_safe(void){ if(msd_ok)return 1; if(!g_xhc_n)return 0;
    for(int c=0;c<g_xhc_n;c++){ xhc_select(c); if(!xhci_present||!xhci_init_ok)continue; xhci_port_refresh();
        for(int p=0;p<xhci_ports;p++){ if(!xhci_port_conn[p])continue; int claimed=0; for(int k=0;k<USBDEV_MAX;k++) if(g_usbdev[k].used&&g_usbdev[k].ctl==c&&g_usbdev[k].port==p&&(g_usbdev[k].hid||g_usbdev[k].cam||g_usbdev[k].prn))claimed=1; if(claimed)continue;
            if(xhci_msd_enum(p)){ msd_ctl=c; msd_port=p; ufat_mount(); return 1; } xh_disable_slot(msd_slot); msd_slot=0; } }
    return 0; }
static void usb_hotplug_poll_one(void){
    if(!xhci_present||!xhci_init_ok)return;
    if(!usb_seen_init){ for(int p=0;p<xhci_ports;p++){ u32 v=XMR(xhci_op+0x400+p*0x10); usb_seen[p]=(u8)((v&1)&&xhci_port_conn[p]); } usb_seen_init=1; return; }
    for(int p=0;p<xhci_ports;p++){ u32 v=XMR(xhci_op+0x400+p*0x10); int cc=v&1;
        if(cc && !usb_seen[p]){ usb_seen[p]=1; xhci_port_conn[p]=1; int done=0;
            if(!msd_ok){ if(xhci_msd_enum(p)){ msd_ctl=g_xcur; ufat_mount(); if(ufat_ok){ g_usb_arrived=1; msd_port=p; } done=1; } else { xh_disable_slot(msd_slot); msd_slot=0; } }
            if(!done&&xhid_free_region(-1,-1)>=0){ if(xhci_hid_enum(p))klog("usb: keyboard/mouse connected"); } }
        else if(!cc && usb_seen[p]){ usb_seen[p]=0; xhci_port_conn[p]=0;
            if(msd_ok && msd_ctl==g_xcur && msd_port==p){ if(msd_slot){ xh_disable_slot(msd_slot); msd_slot=0; } msd_ok=0; ufat_ok=0; ufs_n=0; msd_port=-1; g_files_usb=0; }
            xhid_remove_port(p); usbdev_gone(p); uvc_unplug(p); aud_unplug(p); }
    }
    xhub_poll_ports();
}
typedef struct { int present, init_ok, slots, ports, ports_hw, pci_bus, pci_slot, pci_fn, hi4g, csz, seen_init; u16 vendor; u64 base, op; u32 dboff, rtoff, mem, cmd_enq, cmd_cyc, ev_deq, ev_cyc; u8 conn[32], speed[32], seen[32]; } xhc_t;
static xhc_t g_xhc[XHC_MAX];
static void xhc_save(void){ xhc_t* c=&g_xhc[g_xcur]; c->present=xhci_present; c->init_ok=xhci_init_ok; c->slots=xhci_slots; c->ports=xhci_ports; c->ports_hw=xhci_ports_hw; c->pci_bus=xhci_pci_bus; c->pci_slot=xhci_pci_slot; c->pci_fn=xhci_pci_fn; c->hi4g=xhci_hi4g; c->csz=xhci_csz; c->seen_init=usb_seen_init;
    c->vendor=xhci_vendor; c->base=xhci_base; c->op=xhci_op; c->dboff=xhci_dboff; c->rtoff=xhci_rtoff; c->mem=g_xmem; c->cmd_enq=cmd_enq; c->cmd_cyc=cmd_cyc; c->ev_deq=ev_deq; c->ev_cyc=ev_cyc;
    for(int i=0;i<32;i++){ c->conn[i]=xhci_port_conn[i]; c->speed[i]=xhci_port_speed[i]; c->seen[i]=usb_seen[i]; } }
static void xhc_load(int k){ xhc_t* c=&g_xhc[k]; xhci_present=c->present; xhci_init_ok=c->init_ok; xhci_slots=c->slots; xhci_ports=c->ports; xhci_ports_hw=c->ports_hw; xhci_pci_bus=c->pci_bus; xhci_pci_slot=c->pci_slot; xhci_pci_fn=c->pci_fn; xhci_hi4g=c->hi4g; xhci_csz=c->csz; usb_seen_init=c->seen_init;
    xhci_vendor=c->vendor; xhci_base=c->base; xhci_op=c->op; xhci_dboff=c->dboff; xhci_rtoff=c->rtoff; g_xmem=c->mem; cmd_enq=c->cmd_enq; cmd_cyc=c->cmd_cyc; ev_deq=c->ev_deq; ev_cyc=c->ev_cyc;
    for(int i=0;i<32;i++){ xhci_port_conn[i]=c->conn[i]; xhci_port_speed[i]=c->speed[i]; usb_seen[i]=c->seen[i]; } }
void xhc_select(int i){ if(i<0||i>=XHC_MAX||i==g_xcur)return; xhc_save(); g_xcur=i; xhc_load(i); }
static int xhci_scan_ctrls(int* bus,int* slot,int* fn,int max){
    int n=0;
    for(int b=0;b<256&&n<max;b++) for(int d=0;d<32&&n<max;d++){
        u32 id=pci_cfg(b,d,0,0); if((id&0xFFFF)==0xFFFF)continue;
        int multi=(pci_cfg(b,d,0,0x0C)>>16)&0x80;
        for(int f=0;f<(multi?8:1)&&n<max;f++){
            if(f){ u32 i2=pci_cfg(b,d,f,0); if((i2&0xFFFF)==0xFFFF)continue; }
            u32 cl=pci_cfg(b,d,f,0x08); if((cl>>8)==0x0C0330u){ bus[n]=b; slot[n]=d; fn[n]=f; n++; } } }
    return n;
}
static void xhci_init(void){
    int bus[XHC_MAX], slot[XHC_MAX], fn[XHC_MAX]; int n=xhci_scan_ctrls(bus,slot,fn,XHC_MAX);
    if(g_xcur!=0){ xhc_save(); g_xcur=0; xhc_load(0); }
    for(int k=0;k<XHC_MAX;k++){ g_xhc[k].present=0; g_xhc[k].init_ok=0; g_xhc[k].seen_init=0; }
    for(int k=0;k<n;k++){ xhc_select(k); g_xmem=k==0?DMW(0x10000u):xhc_mem_for(k-1); usb_seen_init=0; if(!g_xmem){ klog("usb: no DMA memory left for another xHCI controller"); continue; }
        xhci_init_one(bus[k],slot[k],fn[k]); cmd_enq=0; cmd_cyc=1; ev_deq=0; ev_cyc=1;
        if(xhci_init_ok){ xhci_csz=(XMR(xhci_base+0x10)>>2)&1; xhci_dboff=XMR(xhci_base+0x14)&~3u; xhci_rtoff=XMR(xhci_base+0x18)&~0x1Fu; } }
    g_xhc_n=n; xhc_select(0); if(!n){ xhci_present=0; xhci_init_ok=0; }
    klog(n>1?"usb: several xHCI controllers found":n?"usb: one xHCI controller found":"usb: no xHCI controller found");
}
void usb_hotplug_poll(void){
    if(!g_xhc_n)return;
    if(g_ticks-usb_poll_t<80)return; usb_poll_t=g_ticks;
    for(int c=0;c<g_xhc_n;c++){ xhc_select(c); usb_hotplug_poll_one(); }
}
typedef struct { u8 alt, ep, intv, burst, mult; u16 mps; u32 esit; } uvc_alt_t;
typedef struct { u8 kind, fi, ri; u16 w, h; u32 defint; } uvc_fr_t;
static struct { int ok, ctl, reg, slot, speed, root, vc_if, vs_if, bcd, plen, bulk, nalt, nfr, cur_alt, dci, maxp, burst, mult, esit, ss, ntd, bufsz, epon, isbulk, bep, bmps, bburst, nvs, inpay, paylen, pinfo; int vs[4]; u8* desc; int descn; u32 e0_enq, e0_cyc; uvc_alt_t alt[24]; uvc_fr_t fr[64]; } cam;
static void uvc_aud_share(int from_cam);
int uvc_state=0, uvc_w=0, uvc_h=0, uvc_kind=0, uvc_fi=0, uvc_ri=0, uvc_altsel=0, uvc_lastcc=0, uvc_cfgcc=0, uvc_maxintr=0;
u32 uvc_maxpay=0, uvc_maxfs=0, uvc_interval=0, uvc_events=0, uvc_tds=0, uvc_tderr=0, uvc_frames=0, uvc_badframes=0, uvc_short=0, uvc_bytes=0, uvc_seq=0, uvc_fps10=0, uvc_jlen=0;
u16 uvc_vid=0, uvc_pid=0; char uvc_name[40]="", uvc_msg[96]="";
u32* uvc_rgb=0; u8* uvc_jpg=0; volatile int uvc_jnew=0;
static u8 *uvc_fb=0, *uvc_bufs=0, *uvc_ermem=0; static volatile u32 *uvc_ring=0, *uvc_er=0, *uvc_erst=0;
static u32 uvc_enq=0, uvc_deq=0, uvc_cyc=1, uvc_out=0, uvc_ev1=0, uvc_ev1c=1, uvc_fill=0, uvc_fcap=0; static int uvc_fbad=0, uvc_lfid=-1; static u64 uvc_ft0=0; static u32 uvc_fc0=0;
static void uvc_m(const char* s){ int i=0; while(s[i]&&i<95){ uvc_msg[i]=s[i]; i++; } uvc_msg[i]=0; }
static u32 uvc_r32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static int uvc_req(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in,u8* data){
    if(!cam.ok)return 0; int sv=g_xcur; xhc_select(cam.ctl);
    u32 s0=cur_ep0,s1=cur_dbuf,s2=e0_enq,s3=e0_cyc; int s4=cur_slot;
    cur_ep0=XHID_EP0RING(cam.reg); cur_dbuf=XHID_DATABUF(cam.reg); e0_enq=cam.e0_enq; e0_cyc=cam.e0_cyc; cur_slot=cam.slot;
    if(!in&&len&&data){ volatile u8* db=(volatile u8*)(u64)cur_dbuf; for(int i=0;i<len;i++)db[i]=data[i]; }
    int r=xh_ctrl(rt,req,val,idx,len,in,in?data:0);
    cam.e0_enq=e0_enq; cam.e0_cyc=e0_cyc; uvc_aud_share(1); cur_ep0=s0; cur_dbuf=s1; e0_enq=s2; e0_cyc=s3; cur_slot=s4; xhc_select(sv); return r; }
static int uvc_has_video(const u8* c,int n){ for(int i=0;i+7<n;){ int bl=c[i]; if(bl<2)break; if(c[i+1]==4&&c[i+5]==0x0E)return 1; if(c[i+1]==0x0B&&c[i+4]==0x0E)return 1; i+=bl; } return 0; }
static int uvc_fourcc(const u8* g){
    if(g[0]=='Y'&&g[1]=='U'&&g[2]=='Y'&&g[3]=='2')return 0; if(g[0]=='N'&&g[1]=='V'&&g[2]=='1'&&g[3]=='2')return 2;
    if(g[0]=='U'&&g[1]=='Y'&&g[2]=='V'&&g[3]=='Y')return 3; if((g[0]=='Y'&&g[1]=='8'&&g[2]=='0'&&g[3]=='0')||(g[0]=='G'&&g[1]=='R'&&g[2]=='E'&&g[3]=='Y')||(g[0]=='Y'&&g[1]=='8'&&g[2]==' ')||(g[0]=='L'&&g[1]=='8'&&g[2]==' '))return 4;
    if((g[0]=='I'&&g[1]=='4'&&g[2]=='2'&&g[3]=='0')||(g[0]=='I'&&g[1]=='Y'&&g[2]=='U'&&g[3]=='V'))return 5; if(g[0]=='Y'&&g[1]=='V'&&g[2]=='1'&&g[3]=='2')return 6;
    return -1; }
const char* uvc_kind_name(int k){ return k==1?"MJPEG":k==2?"NV12":k==3?"UYVY":k==4?"Grey (infrared)":k==5?"I420":k==6?"YV12":"YUY2"; }
static int uvc_has_color(void){ for(int k=0;k<cam.nfr;k++) if(cam.fr[k].kind!=4)return 1; return 0; }
static void uvc_parse(const u8* c,int n,int target);
static void uvc_choose(const u8* c,int n,int want){
    uvc_parse(c,n,-1); if(want>=0&&want<cam.nvs){ uvc_parse(c,n,cam.vs[want]); return; }
    if(cam.nvs<=1)return;
    for(int k=0;k<cam.nvs;k++){ uvc_parse(c,n,cam.vs[k]); if(cam.nfr&&uvc_has_color())return; }
    uvc_parse(c,n,cam.vs[0]); }
int uvc_streams(void){ return cam.ok?cam.nvs:0; }
int uvc_stream_cur(void){ for(int k=0;k<cam.nvs;k++) if(cam.vs[k]==cam.vs_if)return k; return 0; }
int uvc_select_stream(int k){
    if(!cam.ok||!cam.desc||k<0||k>=cam.nvs)return -1; int was=uvc_state==2; extern void uvc_stop(void); if(was)uvc_stop();
    uvc_choose(cam.desc,cam.descn,k); char b[96],*p=b; p=scopy(p,"camera: stream "); p=udec(p,(u32)k+1); p=scopy(p," of "); p=udec(p,(u32)cam.nvs); p=scopy(p,", "); p=udec(p,(u32)cam.nfr); p=scopy(p," sizes"); *p=0; klog(b);
    return was; }
static void uvc_parse(const u8* c,int n,int target){
    int cif=-1,calt=0,ccls=0,csub=0,fk=-1,fidx=0,lastalt=-1,lastbulk=0;
    cam.vc_if=-1; cam.vs_if=target; cam.bcd=0x0100; cam.nalt=0; cam.nfr=0; cam.bulk=0; cam.bep=0; cam.bmps=0; cam.bburst=0; if(target<0)cam.nvs=0;
    for(int i=0;i+2<=n;){ int bl=c[i]; if(bl<2||i+bl>n)break; int t=c[i+1];
        if(t==4&&bl>=9){ cif=c[i+2]; calt=c[i+3]; ccls=c[i+5]; csub=c[i+6]; lastalt=-1;
            if(ccls==14&&csub==1&&cam.vc_if<0)cam.vc_if=cif;
            if(ccls==14&&csub==2&&calt==0&&target<0&&cam.nvs<4){ int dup=0; for(int k=0;k<cam.nvs;k++) if(cam.vs[k]==cif)dup=1; if(!dup)cam.vs[cam.nvs++]=cif; }
            if(ccls==14&&csub==2&&cam.vs_if<0)cam.vs_if=cif; lastbulk=0; }
        else if(t==0x24&&ccls==14&&cif==cam.vc_if&&cam.vc_if>=0&&bl>=5&&c[i+2]==1) cam.bcd=c[i+3]|(c[i+4]<<8);
        else if(t==0x24&&ccls==14&&cif==cam.vs_if&&cam.vs_if>=0&&calt==0){ int st=c[i+2];
            if(st==4&&bl>=21){ fidx=c[i+3]; const u8* g=c+i+5; fk=uvc_fourcc(g); }
            else if(st==6&&bl>=11){ fidx=c[i+3]; fk=1; }
            else if((st==5||st==7)&&bl>=26&&fk>=0&&cam.nfr<64){ uvc_fr_t* f=&cam.fr[cam.nfr++]; f->kind=(u8)fk; f->fi=(u8)fidx; f->ri=c[i+3]; f->w=(u16)(c[i+5]|(c[i+6]<<8)); f->h=(u16)(c[i+7]|(c[i+8]<<8)); f->defint=uvc_r32(c+i+21); }
            else if(st==4||st==6||st==0x10||st==0x0C) fk=-1; }
        else if(t==5&&bl>=7&&ccls==14&&cif==cam.vs_if&&cam.vs_if>=0){ int ep=c[i+2], at=c[i+3]&3; u16 mps=(u16)(c[i+4]|(c[i+5]<<8));
            if((ep&0x80)&&at==1&&calt>0&&cam.nalt<24){ uvc_alt_t* a=&cam.alt[cam.nalt]; a->alt=(u8)calt; a->ep=(u8)ep; a->intv=c[i+6]; a->mps=mps; a->burst=(u8)((mps>>11)&3); a->mult=0; a->esit=(u32)(mps&0x7FF)*(u32)(((mps>>11)&3)+1); lastalt=cam.nalt; cam.nalt++; }
            else if((ep&0x80)&&at==2&&calt==0){ cam.bulk=1; cam.bep=ep; cam.bmps=mps; lastbulk=1; } }
        else if(t==0x30&&bl>=6&&lastbulk&&cam.speed>=4){ cam.bburst=c[i+2]; lastbulk=0; }
        else if(t==0x30&&bl>=6&&lastalt>=0&&cam.speed>=4){ uvc_alt_t* a=&cam.alt[lastalt]; a->burst=c[i+2]; a->mult=(u8)(c[i+3]&3); u32 b=(u32)(c[i+4]|(c[i+5]<<8)); a->esit=b?b:(u32)(a->mps&0x7FF)*(a->burst+1u)*(a->mult+1u); }
        i+=bl; }
    cam.plen=cam.bcd>=0x0150?48:cam.bcd>=0x0110?34:26; }
static int uvc_attach(const usbloc_t* L,int d,const u8* full,int wtot,u8 cfgval,u16 vid,u16 pid,const char* nm){
    cam.ok=0; cam.ctl=g_xcur; cam.reg=d; cam.slot=cur_slot; cam.speed=L->speed; cam.root=L->root; cam.e0_enq=e0_enq; cam.e0_cyc=e0_cyc; cam.cur_alt=0; cam.dci=0;
    uvc_vid=vid; uvc_pid=pid; int q=0; for(;nm&&nm[q]&&q<39;q++)uvc_name[q]=nm[q]; uvc_name[q]=0; if(!uvc_name[0]){ const char* g="USB camera"; for(q=0;g[q];q++)uvc_name[q]=g[q]; uvc_name[q]=0; }
    if(cam.desc){ kfree(cam.desc); cam.desc=0; } cam.desc=(u8*)kmalloc((unsigned long)wtot+16); if(cam.desc){ for(int k=0;k<wtot;k++)cam.desc[k]=full[k]; cam.descn=wtot; }
    cam.epon=0; cam.isbulk=0; cam.inpay=0;
    uvc_choose(full,wtot,-1);
    if(!xh_ctrl(0x00,9,cfgval,0,0,0,0)){ uvc_m("camera: SET_CONFIGURATION failed"); return 0; }
    cam.e0_enq=e0_enq; cam.e0_cyc=e0_cyc; cam.ok=1; uvc_state=1;
    if(cam.vs_if>=0) uvc_req(0x01,0x0B,0,(u16)cam.vs_if,0,0,0);
    char b[96],*p=b; p=scopy(p,"camera: "); p=scopy(p,uvc_name); p=scopy(p,", "); p=udec(p,(u32)cam.nfr); p=scopy(p," sizes, "); p=udec(p,(u32)cam.nalt); p=scopy(p," stream settings"); if(cam.bulk&&!cam.nalt)p=scopy(p,", bulk"); if(cam.nvs>1){ p=scopy(p,", "); p=udec(p,(u32)cam.nvs); p=scopy(p," streams"); } *p=0; klog(b); uvc_m("Camera found");
    return 1; }
int uvc_found(void){ return cam.ok; }
int uvc_nfr(void){ return cam.nfr; }
int uvc_frame_info(int i,int* kind,int* w,int* h,u32* defint){ if(i<0||i>=cam.nfr)return 0; *kind=cam.fr[i].kind; *w=cam.fr[i].w; *h=cam.fr[i].h; *defint=cam.fr[i].defint; return 1; }
int uvc_speed(void){ return cam.speed; }
int uvc_nalt(void){ return cam.nalt; }
int uvc_is_bulk(void){ return cam.bulk&&!cam.nalt; }
int uvc_bulk_active(void){ return cam.isbulk; }
void uvc_gone(void){ cam.ok=0; uvc_state=0; uvc_m("Camera disconnected"); }
static u8* uvc_al(u32 n,u32 al){ u8* p=(u8*)kmalloc((unsigned long)n+(unsigned long)al); if(!p)return 0; u64 a=((u64)p+al-1)&~(u64)(al-1); if(a+n>0xFFFFFFFFULL){ kfree(p); return 0; } return (u8*)a; }
static int uvc_cfg_ep(int add,const uvc_alt_t* a){
    u32 ic0=XHID_INPUTCTX(cam.reg); ctx_zero(ic0,(int)ctx_sz()*33/4); volatile u32* ic=(volatile u32*)(u64)ic0;
    int dci=add?((a->ep&0x0F)*2+1):cam.dci; if(dci<2)return 1;
    ic[0]=cam.epon?(1u<<cam.dci):0u; ic[1]=1u|(add?(1u<<dci):0u);
    volatile u32* os=(volatile u32*)(u64)XHID_DEVCTX(cam.reg); volatile u32* sc=(volatile u32*)(u64)(ic0+ctx_sz());
    for(int k=0;k<4;k++)sc[k]=os[k]; u32 ent=(sc[0]>>27)&31u; if(add&&(u32)dci>ent)ent=(u32)dci; sc[0]=(sc[0]&~(31u<<27))|(ent<<27); sc[3]=0;
    if(add){ volatile u32* ep=(volatile u32*)(u64)(ic0+ctx_sz()*(u32)(dci+1));
        u32 iv; if(cam.speed>=3){ int v=a->intv-1; if(v<0)v=0; if(v>15)v=15; iv=(u32)v; } else { int v=a->intv+2; if(v<3)v=3; if(v>18)v=18; iv=(u32)v; }
        u32 maxp=(u32)(a->mps&0x7FF), es=a->esit;
        ep[0]=(iv<<16)|((es>>16)<<24)|(cam.speed>=4?((u32)a->mult<<8):0u);
        ep[1]=(5u<<3)|((u32)a->burst<<8)|(maxp<<16);
        ep[2]=(u32)(u64)uvc_ring|1u; ep[3]=0; ep[4]=(es&0xFFFFu)|((es&0xFFFFu)<<16);
        if(cam.isbulk){ ep[0]=0; ep[1]=(3u<<1)|(6u<<3)|((u32)a->burst<<8)|(maxp<<16); ep[4]=(u32)cam.bufsz&0xFFFFu; } }
    u8 cc=0,sl=0; int sv=g_xcur; xhc_select(cam.ctl); xh_cmd_s(ic0,0,0,12,(u32)cam.slot,&cc,&sl); xhc_select(sv);
    uvc_cfgcc=cc; if(cc!=1)return 0;
    if(add){ cam.cur_alt=a->alt; cam.dci=dci; cam.epon=1; } else { cam.cur_alt=0; cam.epon=0; }
    return 1; }
static void uvc_td(void){
    volatile u32* t=uvc_ring+uvc_enq*4; u32 b=(u32)(u64)(uvc_bufs+(u64)uvc_enq*cam.bufsz); u32 len=(u32)cam.esit; u32 mp=(u32)cam.maxp; if(!mp)mp=1;
    if(cam.isbulk){ t[0]=b; t[1]=0; t[2]=len; __sync_synchronize(); t[3]=uvc_cyc|(1u<<2)|(1u<<5)|(1u<<10);
        uvc_enq++; if(uvc_enq>=(u32)cam.ntd){ volatile u32* lk=uvc_ring+(u32)cam.ntd*4; lk[3]=(lk[3]&~1u)|uvc_cyc; uvc_enq=0; uvc_cyc^=1; }
        uvc_out++; return; }
    u32 tdpc=(len+mp-1)/mp; if(!tdpc)tdpc=1; u32 tbc=0, tl=tdpc-1;
    if(cam.ss){ u32 mb=(u32)cam.burst+1u; tbc=(tdpc+mb-1)/mb-1u; u32 r=tdpc%mb; tl=r?r-1u:mb-1u; }
    t[0]=b; t[1]=0; t[2]=len|(1u<<22); __sync_synchronize();
    t[3]=uvc_cyc|(1u<<2)|(1u<<5)|(5u<<10)|((tbc&3u)<<7)|((tl&15u)<<16)|(1u<<31);
    uvc_enq++; if(uvc_enq>=(u32)cam.ntd){ volatile u32* lk=uvc_ring+(u32)cam.ntd*4; lk[3]=(lk[3]&~1u)|uvc_cyc; uvc_enq=0; uvc_cyc^=1; }
    uvc_out++; }
static u32 uvc_px(int y,int u,int v){ int c=298*(y-16); u-=128; v-=128; int r=(c+409*v+128)>>8, g=(c-100*u-208*v+128)>>8, b=(c+516*u+128)>>8; if(r<0)r=0; if(r>255)r=255; if(g<0)g=0; if(g>255)g=255; if(b<0)b=0; if(b>255)b=255; return ((u32)r<<16)|((u32)g<<8)|(u32)b; }
static void uvc_frame_done(void){
    if(!uvc_fbad&&uvc_fill>0){
        if(uvc_kind==1){ if(uvc_jpg&&!uvc_jnew){ const u8* s=uvc_fb; u32 n=uvc_fill; int dht=0; for(u32 i=0;i+1<n;i++){ if(s[i]==0xFF&&s[i+1]==0xC4){dht=1;break;} if(s[i]==0xFF&&s[i+1]==0xDA)break; }
                extern const u8 uvc_std_dht[]; extern const int uvc_std_dht_len; u32 o=0;
                if(!dht&&n>2){ uvc_jpg[0]=0xFF; uvc_jpg[1]=0xD8; o=2; for(int k=0;k<uvc_std_dht_len;k++)uvc_jpg[o++]=uvc_std_dht[k]; for(u32 i=2;i<n;i++)uvc_jpg[o++]=s[i]; }
                else { for(u32 i=0;i<n;i++)uvc_jpg[o++]=s[i]; }
                uvc_jlen=o; uvc_jnew=1; } uvc_frames++; }
        else if(uvc_kind==0){ u32 need=(u32)uvc_w*(u32)uvc_h*2u; if(uvc_fill>=need){ const u8* s=uvc_fb; u32* d=uvc_rgb; u32 np=(u32)uvc_w*(u32)uvc_h;
                for(u32 i=0;i<np;i+=2){ int y0=s[0]-16,u=s[1]-128,y1=s[2]-16,v=s[3]-128; s+=4; int rv=409*v, gv=-100*u-208*v, bv=516*u;
                    for(int k=0;k<2;k++){ int c=298*(k?y1:y0); int r=(c+rv+128)>>8, g=(c+gv+128)>>8, bb=(c+bv+128)>>8; if(r<0)r=0; if(r>255)r=255; if(g<0)g=0; if(g>255)g=255; if(bb<0)bb=0; if(bb>255)bb=255; d[i+k]=((u32)r<<16)|((u32)g<<8)|(u32)bb; } }
                uvc_frames++; uvc_seq++; } else uvc_short++; }
        else if(uvc_kind==3){ u32 need=(u32)uvc_w*(u32)uvc_h*2u; if(uvc_fill>=need){ const u8* s=uvc_fb; u32* d=uvc_rgb; u32 np=(u32)uvc_w*(u32)uvc_h;
                for(u32 i=0;i<np;i+=2){ d[i]=uvc_px(s[1],s[0],s[2]); d[i+1]=uvc_px(s[3],s[0],s[2]); s+=4; } uvc_frames++; uvc_seq++; } else uvc_short++; }
        else if(uvc_kind==4){ u32 np=(u32)uvc_w*(u32)uvc_h; if(uvc_fill>=np){ const u8* s=uvc_fb; u32* d=uvc_rgb; for(u32 i=0;i<np;i++){ u32 v=s[i]; d[i]=(v<<16)|(v<<8)|v; } uvc_frames++; uvc_seq++; } else uvc_short++; }
        else if(uvc_kind==5||uvc_kind==6){ u32 np=(u32)uvc_w*(u32)uvc_h; if(uvc_fill>=np*3u/2u){ const u8* Y=uvc_fb; const u8* P1=uvc_fb+np; const u8* P2=P1+np/4u; const u8* U=uvc_kind==5?P1:P2; const u8* V=uvc_kind==5?P2:P1; u32* d=uvc_rgb; int cw=uvc_w/2;
                for(int y=0;y<uvc_h;y++) for(int x=0;x<uvc_w;x++){ int ci=(y>>1)*cw+(x>>1); d[y*uvc_w+x]=uvc_px(Y[y*uvc_w+x],U[ci],V[ci]); } uvc_frames++; uvc_seq++; } else uvc_short++; }
        else { u32 np=(u32)uvc_w*(u32)uvc_h; if(uvc_fill>=np*3u/2u){ const u8* Y=uvc_fb; const u8* C=uvc_fb+np; u32* d=uvc_rgb;
                for(int y=0;y<uvc_h;y++) for(int x=0;x<uvc_w;x++){ int yy=Y[y*uvc_w+x]-16; const u8* cc=C+(y>>1)*uvc_w+(x&~1); int u=cc[0]-128,v=cc[1]-128; int c=298*yy;
                    int r=(c+409*v+128)>>8, g=(c-100*u-208*v+128)>>8, bb=(c+516*u+128)>>8; if(r<0)r=0; if(r>255)r=255; if(g<0)g=0; if(g>255)g=255; if(bb<0)bb=0; if(bb>255)bb=255; d[y*uvc_w+x]=((u32)r<<16)|((u32)g<<8)|(u32)bb; }
                uvc_frames++; uvc_seq++; } else uvc_short++; } }
    else if(uvc_fill>0) uvc_badframes++;
    uvc_fill=0; uvc_fbad=0; }
static void uvc_append(const u8* s,int dn){ if(dn<=0)return; if(uvc_fill+(u32)dn<=uvc_fcap){ u8* d=uvc_fb+uvc_fill; for(int i=0;i<dn;i++)d[i]=s[i]; uvc_fill+=(u32)dn; uvc_bytes+=(u32)dn; } else uvc_fbad=1; }
static void uvc_payload(const u8* b,int n){
    if(!b||n<2)return; int hl=b[0]; if(hl<2||hl>n)return; int info=b[1]; int fid=info&1;
    if(uvc_lfid>=0&&fid!=uvc_lfid&&uvc_fill>0)uvc_frame_done(); uvc_lfid=fid;
    if(info&0x40)uvc_fbad=1;
    uvc_append(b+hl,n-hl);
    if(info&2)uvc_frame_done(); }
static void uvc_bulk_chunk(const u8* b,int got,int tl){
    if(!cam.inpay){ if(got<2)return; int hl=b[0]; if(hl<2||hl>got)return; int info=b[1]; cam.pinfo=info; int fid=info&1;
        if(uvc_lfid>=0&&fid!=uvc_lfid&&uvc_fill>0)uvc_frame_done(); uvc_lfid=fid; if(info&0x40)uvc_fbad=1;
        uvc_append(b+hl,got-hl); cam.paylen=got; cam.inpay=1; }
    else { uvc_append(b,got); cam.paylen+=got; }
    if(got<tl||(uvc_maxpay&&(u32)cam.paylen>=uvc_maxpay)){ cam.inpay=0; if(cam.pinfo&2)uvc_frame_done(); } }
static int uvc_pref(const uvc_fr_t* f,int hd){
    if(f->w>1920||f->h>1200||f->w<16||f->h<16)return 99; if(f->kind==4&&uvc_has_color())return 3; int raw=f->kind!=1;
    if(hd){ if(!raw&&f->w<=1280)return 0; if(raw&&f->w<=640)return 1; if(!raw)return 2; return 3; }
    if(raw&&f->w<=640)return 0; if(!raw&&f->w<=640)return 1; if(!raw)return 2; return 3; }
