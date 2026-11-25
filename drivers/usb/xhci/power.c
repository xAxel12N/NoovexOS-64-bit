#ifndef XPM_R
#define XPM_R(a) XMR(a)
#define XPM_W(a,v) XMW(a,v)
#endif
#define XPM_NI 4
#define XPM_RWMASK 0x0E00C200u
static struct featguard xpm_fg={"USB sleep state save","feat-usbpm.cfg",1,0,3,0,0};
static struct featguard xpm_wfg={"USB keyboard wake","feat-usbwake.cfg",1,0,3,0,0};
static struct featguard xpm_ifg={"USB idle port suspend","feat-usbidle.cfg",1,0,3,0,0};
static struct featguard xpm_lfg={"USB link power management","feat-usblpm.cfg",0,0,3,0,0};
typedef struct { int saved, lost, stopped; u32 cmd, dnctrl, dcbl, dcbh, cfg, ni; u32 iman[XPM_NI], imod[XPM_NI], erstsz[XPM_NI], erstba[XPM_NI], erstbah[XPM_NI], erdp[XPM_NI], erdph[XPM_NI]; u32 u3, idle, lpm, wake; u64 idle_t[32]; } xpm_t;
static xpm_t g_xpm[XHC_MAX];
void xhci_pm_idle(int now);
u32 g_xpm_saves, g_xpm_restores, g_xpm_reinits, g_xpm_u3, g_xpm_idle_u3, g_xpm_lpm;
char g_xpm_why[96];
static void xpm_say(const char* m){ int i=0; while(m[i]&&i<95){ g_xpm_why[i]=m[i]; i++; } g_xpm_why[i]=0; klog(m); }
static int xpm_on(struct featguard* g){ fg_boot(g); return g->on&&!g->dead; }
int xhci_pm_feature(int f,int on){ struct featguard* g=f==0?&xpm_fg:f==1?&xpm_wfg:f==2?&xpm_ifg:f==3?&xpm_lfg:0; if(!g)return -1; if(on==0||on==1)fg_enable(g,on); return xpm_on(g); }
static int xpm_wait(u64 a,u32 m,u32 want,int us){ for(int t=0;t<us;t+=10){ u32 v=XPM_R(a); if(v==0xFFFFFFFFu)return 0; if((v&m)==want)return 1; hr_delay_us(10); } return (XPM_R(a)&m)==want; }
static u64 xpm_port(int p){ return xhci_op+0x400+(u32)p*0x10; }
static int xpm_pls(int p){ return (int)((XPM_R(xpm_port(p))>>5)&0xF); }
static int xpm_set_pls(int p,int pls){ u64 a=xpm_port(p); u32 v=XPM_R(a); XPM_W(a,(v&XPM_RWMASK)|(1u<<16)|((u32)pls<<5)); return 0; }
static void xpm_clr_plc(int p){ u64 a=xpm_port(p); u32 v=XPM_R(a); if(v&(1u<<22))XPM_W(a,(v&XPM_RWMASK)|(1u<<22)); }
static int xpm_port_used(int p){
    volatile u32* dc=(volatile u32*)(u64)XHCI_DCBAA;
    for(int s=1;s<=xhci_slots&&s<256;s++){ u32 c=dc[s*2]; if(!c)continue; u32 rp=(*(volatile u32*)(u64)(c+4)>>16)&0xFF; if(rp==(u32)(p+1))return 1; }
    return 0; }
int xhci_port_suspend(int p){
    if(p<0||p>=xhci_ports||!xhci_op)return -1; u32 v=XPM_R(xpm_port(p));
    if(v==0xFFFFFFFFu||!(v&1)||!(v&2))return -1; int pls=(int)((v>>5)&0xF); if(pls==3)return 0; if(pls!=0)return -1;
    xpm_set_pls(p,3);
    for(int t=0;t<20000;t+=50){ if(xpm_pls(p)==3)break; hr_delay_us(50); }
    xpm_clr_plc(p); if(xpm_pls(p)!=3)return -1; g_xpm_u3++; return 0; }
int xhci_port_resume(int p){
    if(p<0||p>=xhci_ports||!xhci_op)return -1; u32 v=XPM_R(xpm_port(p));
    if(v==0xFFFFFFFFu||!(v&1))return -1; int pls=(int)((v>>5)&0xF), ss=(int)((v>>10)&0xF)>=4;
    if(pls==0)return 0;
    if(pls==3&&!ss){ xpm_set_pls(p,15); hr_delay_us(20000); xpm_set_pls(p,0); }
    else if(pls==15&&!ss){ hr_delay_us(20000); xpm_set_pls(p,0); }
    else if(pls==3)xpm_set_pls(p,0);
    for(int t=0;t<100000;t+=100){ if(xpm_pls(p)==0)break; hr_delay_us(100); }
    xpm_clr_plc(p); v=XPM_R(xpm_port(p)); return ((v>>5)&0xF)==0&&(v&3)==3?0:-1; }
static int xpm_ctrl_slot(int e,u8 rt,u8 req,u16 val,u16 idx,u16 len,int in,u8* data){
    int d=xhid_own[e]; if(d<0||d>=XHID_MAX)return 0;
    int s_slot=cur_slot; u32 s_ep0=cur_ep0, s_db=cur_dbuf, s_enq=e0_enq, s_cyc=e0_cyc;
    cur_slot=xhid_slot[e]; cur_ep0=XHID_EP0RING(d); cur_dbuf=XHID_DATABUF(d); e0_enq=xhid_e0enq[d]; e0_cyc=xhid_e0cyc[d];
    int ok=xh_ctrl(rt,req,val,idx,len,in,data);
    xhid_e0enq[d]=e0_enq; xhid_e0cyc[d]=e0_cyc; cur_slot=s_slot; cur_ep0=s_ep0; cur_dbuf=s_db; e0_enq=s_enq; e0_cyc=s_cyc; return ok; }
static int xpm_first_e(int c,int p){ for(int e=0;e<xhid_n;e++) if(xhid_used[e]&&xhid_ctl[e]==c&&xhid_port[e]==p&&xhid_depth[e]==0)return e; return -1; }
static void xpm_remote_wake(int c,int on){
    xpm_t* S=&g_xpm[c];
    for(int e=0;e<xhid_n;e++){ if(!xhid_used[e]||xhid_ctl[e]!=c||!xhid_iskbd[e]||xhid_depth[e]!=0)continue; int p=xhid_port[e]; if(p<0||p>=32)continue;
        if(on){ if(xpm_ctrl_slot(e,0x00,3,1,0,0,0,0))S->wake|=1u<<p; }
        else if(S->wake&(1u<<p)){ xpm_ctrl_slot(e,0x00,1,1,0,0,0,0); S->wake&=~(1u<<p); } } }
static void xpm_pme(int on){
    if(xhci_pci_bus<0)return; int pm=pci_find_cap(xhci_pci_bus,xhci_pci_slot,xhci_pci_fn,1); if(!pm)return;
    u32 v=pci_cfg(xhci_pci_bus,xhci_pci_slot,xhci_pci_fn,pm+4); v=(v&~0x8103u)|0x8000u|(on?0x100u:0u); pci_cfgw(xhci_pci_bus,xhci_pci_slot,xhci_pci_fn,pm+4,v); }
static int xpm_save_one(int c){
    xpm_t* S=&g_xpm[c]; S->saved=0; S->lost=0; S->stopped=0; S->u3=0;
    if(!xhci_present||!xhci_init_ok||!xhci_op)return -1;
    u64 op=xhci_op, rt=xhci_base+xhci_rtoff+0x20; if(XPM_R(op+4)==0xFFFFFFFFu)return -1;
    if(xpm_on(&xpm_wfg))xpm_remote_wake(c,1);
    for(int p=0;p<xhci_ports;p++){ u32 v=XPM_R(xpm_port(p)); if((v&3)!=3)continue; int pls=(int)((v>>5)&0xF); if(pls==3&&(S->idle&(1u<<p)))continue;
        if(xhci_port_suspend(p)==0)S->u3|=1u<<p; }
    S->cmd=XPM_R(op); XPM_W(op,S->cmd&~1u);
    if(!xpm_wait(op+4,1u,1u,20000)){ xpm_say("usb: controller did not stop for sleep"); return -1; }
    S->stopped=1;
    S->dnctrl=XPM_R(op+0x14); S->dcbl=XPM_R(op+0x30); S->dcbh=XPM_R(op+0x34); S->cfg=XPM_R(op+0x38);
    u32 ni=(XPM_R(xhci_base+4)>>8)&0x7FF; if(ni>XPM_NI)ni=XPM_NI; if(!ni)ni=1; S->ni=ni;
    for(u32 i=0;i<ni;i++){ u64 r=rt+i*0x20; S->iman[i]=XPM_R(r); S->imod[i]=XPM_R(r+4); S->erstsz[i]=XPM_R(r+8); S->erstba[i]=XPM_R(r+0x10); S->erstbah[i]=XPM_R(r+0x14); S->erdp[i]=XPM_R(r+0x18); S->erdph[i]=XPM_R(r+0x1C); }
    XPM_W(op,(S->cmd&~1u)|(1u<<8));
    if(!xpm_wait(op+4,1u<<8,0,100000)){ xpm_say("usb: controller did not finish saving its state"); return -1; }
    if(XPM_R(op+4)&(1u<<10)){ XPM_W(op+4,1u<<10); xpm_say("usb: controller reported a save error"); return -1; }
    if(xpm_on(&xpm_wfg))xpm_pme(1);
    S->saved=1; g_xpm_saves++; return 0; }
static int xpm_restore_one(int c){
    xpm_t* S=&g_xpm[c];
    if(!S->saved||!xhci_op)return -1;
    u64 op=xhci_op, rt=xhci_base+xhci_rtoff+0x20; xpm_pme(0);
    if(XPM_R(op+4)==0xFFFFFFFFu){ xpm_say("usb: controller is not answering after sleep"); return -1; }
    if(!xpm_wait(op+4,1u<<11,0,200000)){ xpm_say("usb: controller stayed not-ready after sleep"); return -1; }
    if(!(XPM_R(op+4)&1u)){ xpm_say("usb: controller was running after sleep, restarting it"); return -1; }
    XPM_W(op+0x14,S->dnctrl); XPM_W(op+0x30,S->dcbl); XPM_W(op+0x34,S->dcbh); XPM_W(op+0x38,S->cfg);
    for(u32 i=0;i<S->ni;i++){ u64 r=rt+i*0x20; XPM_W(r+8,S->erstsz[i]); XPM_W(r+0x14,S->erstbah[i]); XPM_W(r+0x10,S->erstba[i]); XPM_W(r+0x1C,S->erdph[i]); XPM_W(r+0x18,S->erdp[i]); XPM_W(r+4,S->imod[i]); XPM_W(r,S->iman[i]); }
    XPM_W(op,(S->cmd&~1u)|(1u<<9));
    if(!xpm_wait(op+4,1u<<9,0,100000)){ xpm_say("usb: controller did not finish restoring its state"); return -1; }
    if(XPM_R(op+4)&(1u<<10)){ XPM_W(op+4,1u<<10); xpm_say("usb: controller lost its saved state during sleep"); return -1; }
    if(XPM_R(op+0x30)!=S->dcbl){ xpm_say("usb: controller did not keep its device table"); return -1; }
    XPM_W(op+0x18,(XHCI_CMDRING+cmd_enq*16)|(cmd_cyc&1u)); XPM_W(op+0x1C,0);
    XPM_W(op,S->cmd|1u);
    if(!xpm_wait(op+4,1u,0,20000)){ xpm_say("usb: controller did not start again after sleep"); return -1; }
    S->stopped=0; S->saved=0; int bad=0;
    for(int p=0;p<xhci_ports;p++){ if(!(S->u3&(1u<<p)))continue; if(xhci_port_resume(p)<0&&xpm_port_used(p))bad++; }
    S->u3=0;
    if(bad){ xpm_say("usb: a device did not come back after sleep, finding devices again"); return -1; }
    for(int e=0;e<xhid_n;e++) if(xhid_used[e]&&xhid_ctl[e]==c)xh_db(xhid_slot[e],(u32)xhid_dci[e]);
    if(xpm_on(&xpm_wfg))xpm_remote_wake(c,0);
    g_xpm_restores++; return 0; }
int xhci_pm_suspend(int deep){
    if(!g_xhc_n)return 0; int sv=g_xcur, bad=0;
    if(!deep){ for(int c=0;c<g_xhc_n;c++){ xhc_select(c); if(xhci_init_ok)xhci_pm_idle(1); } xhc_select(sv); return 0; }
    if(!fg_begin(&xpm_fg)){ for(int c=0;c<XHC_MAX;c++)g_xpm[c].saved=0; return -1; }
    for(int c=0;c<g_xhc_n;c++){ xhc_select(c); if(xhci_init_ok&&xpm_save_one(c)<0)bad++; }
    xhc_select(sv); if(bad)for(int c=0;c<XHC_MAX;c++)g_xpm[c].lost=g_xpm[c].lost||!g_xpm[c].saved;
    return bad?-1:0; }
int xhci_pm_resume(void){
    if(!g_xhc_n)return 1; int sv=g_xcur, bad=0, any=0;
    for(int c=0;c<g_xhc_n;c++){ xhc_select(c); if(!xhci_init_ok)continue; xpm_t* S=&g_xpm[c];
        if(S->saved||S->stopped){ any=1; if(xpm_restore_one(c)<0)bad++; continue; }
        if(S->u3){ for(int p=0;p<xhci_ports;p++) if((S->u3&(1u<<p))&&xhci_port_resume(p)<0&&xpm_port_used(p))bad++; S->u3=0; xpm_remote_wake(c,0); } }
    xhc_select(sv);
    if(bad){ g_xpm_reinits++; if(any)fg_fail(&xpm_fg,g_xpm_why); for(int c=0;c<XHC_MAX;c++){ g_xpm[c].saved=0; g_xpm[c].stopped=0; } return -1; }
    fg_ok(&xpm_fg); if(!any)return 1;
    klog("usb: devices kept their state across sleep"); return 0; }
void xhci_pm_idle(int now){
    if(!xhci_init_ok||!xhci_op||!xpm_on(&xpm_ifg))return; xpm_t* S=&g_xpm[g_xcur];
    for(int p=0;p<xhci_ports;p++){ u32 v=XPM_R(xpm_port(p)), b=1u<<p;
        if(v==0xFFFFFFFFu)return;
        if(!(v&1)){ S->idle&=~b; S->idle_t[p]=0; continue; }
        if(S->idle&b){ if(((v>>5)&0xF)!=3||xpm_port_used(p))S->idle&=~b; continue; }
        if((v&3)!=3||((v>>5)&0xF)!=0||xpm_port_used(p)){ S->idle_t[p]=0; continue; }
        if(!S->idle_t[p]){ S->idle_t[p]=g_ticks; if(!now)continue; }
        if(!now&&g_ticks-S->idle_t[p]<1000)continue;
        if(xhci_port_suspend(p)==0){ S->idle|=b; g_xpm_idle_u3++; klog("usb: idle device with no driver put into suspend"); } } }
static int xpm_bos(int e,u8* out,int max){
    if(!xpm_ctrl_slot(e,0x80,6,0x0F00,0,5,1,out))return 0; int t=out[2]|(out[3]<<8); if(t<5)return 0; if(t>max)t=max;
    return xpm_ctrl_slot(e,0x80,6,0x0F00,0,(u16)t,1,out)?t:0; }
static int xpm_hlc(int p){
    u32 x=(XPM_R(xhci_base+0x10)>>16)<<2; int g=0;
    while(x&&g++<64){ u64 a=xhci_base+x; u32 v=XPM_R(a);
        if((v&0xFF)==2&&(v>>24)==2){ u32 w=XPM_R(a+8); int o=(int)(w&0xFF), n=(int)((w>>8)&0xFF); if(p+1>=o&&p+1<o+n)return (w>>19)&1; }
        u32 nx=(v>>8)&0xFF; if(!nx)break; x+=nx<<2; }
    return 0; }
static int xpm_lpm_one(int c,int e){
    int p=xhid_port[e]; if(p<0||p>=xhci_ports)return 0; u32 v=XPM_R(xpm_port(p)); int sp=(int)((v>>10)&0xF);
    u8 bos[128]; int n=xpm_bos(e,bos,(int)sizeof bos); if(!n)return 0; int u2=0, ss=0, u1l=0, u2l=0;
    for(int i=5;i+2<n;){ int bl=bos[i]; if(bl<3||i+bl>n)break;
        if(bos[i+1]==0x10&&bos[i+2]==2&&bl>=7&&((bos[i+3]>>1)&1))u2=1;
        if(bos[i+1]==0x10&&bos[i+2]==3&&bl>=10){ ss=1; u1l=bos[i+7]; u2l=bos[i+8]|(bos[i+9]<<8); }
        i+=bl; }
    u64 pm=xpm_port(p)+4;
    if(sp<4&&sp!=0&&u2&&xpm_hlc(p)){ XPM_W(xpm_port(p)+0xC,(4u<<0)|(10u<<2)); XPM_W(pm,(4u<<4)|((u32)xhid_slot[e]<<8)|(1u<<16)); g_xpm[c].lpm|=1u<<p; g_xpm_lpm++; return 1; }
    if(sp>=4&&ss&&u1l<=10&&u2l<=2047){
        volatile u8* db=(volatile u8*)(u64)XHID_DATABUF(xhid_own[e]); u16 u2s=(u16)(u2l+1); db[0]=(u8)(u1l+1); db[1]=(u8)u1l; db[2]=(u8)u2s; db[3]=(u8)(u2s>>8); db[4]=(u8)u2l; db[5]=(u8)(u2l>>8);
        if(!xpm_ctrl_slot(e,0x00,0x30,0,0,6,0,0))return 0;
        int a=xpm_ctrl_slot(e,0x00,3,48,0,0,0,0), b=xpm_ctrl_slot(e,0x00,3,49,0,0,0,0); if(!a&&!b)return 0;
        XPM_W(pm,(XPM_R(pm)&~0xFFFFu)|(a?0x7Fu:0u)|(b?(0x80u<<8):0u)); g_xpm[c].lpm|=1u<<p; g_xpm_lpm++; return 1; }
    return 0; }
int xhci_lpm_apply(void){
    if(!g_xhc_n||!xpm_on(&xpm_lfg))return 0; if(!fg_begin(&xpm_lfg))return 0; int sv=g_xcur, n=0;
    for(int c=0;c<g_xhc_n;c++){ xhc_select(c); if(!xhci_init_ok)continue; u32 done=0;
        for(int e=0;e<xhid_n;e++){ if(!xhid_used[e]||xhid_ctl[e]!=c||xhid_depth[e]!=0)continue; int p=xhid_port[e]; if(p<0||p>=32||(done&(1u<<p))||xpm_first_e(c,p)!=e)continue; done|=1u<<p; n+=xpm_lpm_one(c,e); } }
    xhc_select(sv); fg_ok(&xpm_lfg); return n; }
int xhci_lpm_off(void){
    int sv=g_xcur, n=0;
    for(int c=0;c<g_xhc_n;c++){ xhc_select(c); if(!xhci_init_ok||!g_xpm[c].lpm)continue;
        for(int p=0;p<xhci_ports;p++){ if(!(g_xpm[c].lpm&(1u<<p)))continue; u64 pm=xpm_port(p)+4; XPM_W(pm,XPM_R(pm)&~(0x1FFFFu)); n++; }
        for(int e=0;e<xhid_n;e++) if(xhid_used[e]&&xhid_ctl[e]==c&&xhid_depth[e]==0&&(g_xpm[c].lpm&(1u<<xhid_port[e]))){ xpm_ctrl_slot(e,0x00,1,48,0,0,0,0); xpm_ctrl_slot(e,0x00,1,49,0,0,0,0); }
        g_xpm[c].lpm=0; }
    if(g_xhc_n)xhc_select(sv); return n; }
const char* xhci_pm_status(void){
    static char b[200]; char* q=b; fg_boot(&xpm_fg); fg_boot(&xpm_lfg);
    q=scopy(q,xpm_fg.dead?"sleep state save: off after failures":xpm_fg.on?"sleep state save: on":"sleep state save: off");
    q=scopy(q,", saves "); q=udec(q,g_xpm_saves); q=scopy(q,", restores "); q=udec(q,g_xpm_restores); q=scopy(q,", re-finds "); q=udec(q,g_xpm_reinits);
    q=scopy(q,"; idle ports suspended "); q=udec(q,g_xpm_idle_u3); q=scopy(q,"; link power management "); q=scopy(q,xpm_on(&xpm_lfg)?"on":"off"); q=scopy(q," ("); q=udec(q,g_xpm_lpm); q=scopy(q," ports)");
    if(g_xpm_why[0]){ q=scopy(q,"; last: "); for(int i=0;g_xpm_why[i]&&q<b+198;i++)*q++=g_xpm_why[i]; }
    *q=0; return b; }
int xhci_pm_woke(void){
    int sv=g_xcur, w=0;
    for(int c=0;c<g_xhc_n;c++){ xhc_select(c); if(xhci_pci_bus<0)continue; int pm=pci_find_cap(xhci_pci_bus,xhci_pci_slot,xhci_pci_fn,1); if(!pm)continue;
        u32 v=pci_cfg(xhci_pci_bus,xhci_pci_slot,xhci_pci_fn,pm+4); if((v&0x8100u)==0x8100u)w=1; }
    if(g_xhc_n)xhc_select(sv); return w; }
