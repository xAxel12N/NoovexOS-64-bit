static u32 ureg_tried[USBREG_MAX];
static void ureg_str(u8 idx,char* out,int cap){
    out[0]=0; if(!idx||xh_ctrl_to==1)return;
    if(!xh_ctrl(0x80,6,(u16)(0x0300|idx),0x0409,64,1,0))return;
    volatile u8* sb=(volatile u8*)(u64)cur_dbuf; int n=sb[0], o=0; if(n>64)n=64; if(sb[1]!=3)return;
    for(int q=2;q+1<n&&o<cap-1;q+=2){ u8 c=sb[q]; if(sb[q+1]||c<32||c>126)c='?'; out[o++]=(char)c; }
    while(o>0&&out[o-1]==' ')o--; out[o]=0; }
static void usbreg_probe(const usbloc_t* L,const u8* full,int wtot){
    if(!L||!xh_ctrl(0x80,6,0x0100,0,18,1,0))return;
    u8 dd[18]; volatile u8* db=(volatile u8*)(u64)cur_dbuf; for(int i=0;i<18;i++)dd[i]=db[i];
    usbreg_t* r=usbreg_add(g_xcur,L->root,L->route,L->depth,L->speed,dd,full,wtot); if(!r)return;
    int i=(int)(r-g_usbreg); u32 key=((u32)r->vid<<16)|r->pid;
    if(ureg_tried[i]==key&&(r->prod[0]||r->mfr[0]))return;
    ureg_tried[i]=key; char m[32], p[40], s[32];
    ureg_str(dd[14],m,32); ureg_str(dd[15],p,40); ureg_str(dd[16],s,32); usbreg_strings(r,m,p,s); }
static int ureg_at(int c,int ctl,int rt,int root,u32 a,u32 b,int da,int db){ u32 m=db<=0?0u:db>=5?0xFFFFFu:((1u<<(4*db))-1u); return c==ctl&&rt==root&&da==db&&(a&m)==(b&m); }
const char* usbreg_drv_at(int ctl,int root,u32 route,int depth){
    for(int h=0;h<XHUB_MAX;h++) if(xhub[h].used&&ureg_at(xhub[h].ctl,ctl,xhub[h].root,root,xhub[h].route,route,xhub[h].depth,depth))return "hub";
    for(int i=0;i<xhid_n&&i<XHID_MAX;i++) if(xhid_used[i]&&ureg_at(xhid_ctl[i],ctl,xhid_port[i],root,xhid_route[i],route,xhid_depth[i],depth))return "usbhid";
    if(msd_ok&&ureg_at(msd_ctl,ctl,msd_port,root,msd_route,route,msd_depth,depth))return "usb-storage";
    if(cam.ok&&ureg_at(cam.ctl,ctl,cam.root,root,cam.route,route,cam.depth,depth))return "uvcvideo";
    if(aud.ok&&ureg_at(aud.ctl,ctl,aud.root,root,aud.route,route,aud.depth,depth))return "snd-usb-audio";
    if(usn.ok&&ureg_at(usn.ctl,ctl,usn.L.root,root,usn.route,route,usn.depth,depth))return usn.kind==3?"cdc_ncm":"usbnet";
    for(int k=0;k<UM_MAX;k++) if(g_um[k].ok&&ureg_at(g_um[k].ctl,ctl,g_um[k].L.root,root,g_um[k].route,route,g_um[k].depth,depth))return "snd-usb-audio";
    if(btu.ok&&ureg_at(btu.ctl,ctl,btu.L.root,root,btu.route,route,btu.L.depth,depth))return "btusb";
    if(fpu.ok&&fpu.ctl==ctl&&fpu.port==root&&!depth)return "fp-usb";
    return 0; }
int usbreg_hc_count(void){ return g_xhc_n; }
int usbreg_hc_ports(int c){ if(c<0||c>=g_xhc_n||c>=XHC_MAX)return 0; return c==g_xcur?xhci_ports:g_xhc[c].ports; }
