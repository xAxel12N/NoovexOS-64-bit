#include "include/nvx/drivers/usb/usbnet_frame.h"
static unf_ctx_t ueth_fc;
static int ueth_chip, ueth_lk, ueth_msr;
static u64 ueth_lt;
static const u16 ueth_ids[][3]={
    {0x0BDA,0x8152,UNF_URE},{0x0BDA,0x8153,UNF_URE},{0x0BDA,0x8050,UNF_URE},{0x0BDA,0x8053,UNF_URE},
    {0x17EF,0x7205,UNF_URE},{0x17EF,0x720A,UNF_URE},{0x17EF,0x720C,UNF_URE},{0x17EF,0x3054,UNF_URE},{0x17EF,0x3062,UNF_URE},{0x17EF,0x3069,UNF_URE},{0x17EF,0x3082,UNF_URE},{0x17EF,0xA387,UNF_URE},{0x17EF,0xA359,UNF_URE},
    {0x045E,0x07AB,UNF_URE},{0x045E,0x07C6,UNF_URE},{0x045E,0x0C5E,UNF_URE},{0x0955,0x09FF,UNF_URE},{0x04E8,0xA101,UNF_URE},{0x2357,0x0601,UNF_URE},{0x13B1,0x0041,UNF_URE},{0x2001,0xB301,UNF_URE},{0x056E,0x4017,UNF_URE},
    {0x0B95,0x1790,UNF_AXEN},{0x0B95,0x178A,UNF_AXEN},{0x050D,0x0128,UNF_AXEN},{0x2001,0x4A00,UNF_AXEN},{0x17EF,0x304B,UNF_AXEN},{0x0DF6,0x0072,UNF_AXEN},
};
static int ueth_match(u16 vid,u16 pid){ for(unsigned i=0;i<sizeof ueth_ids/sizeof ueth_ids[0];i++) if(ueth_ids[i][0]==vid&&ueth_ids[i][1]==pid)return ueth_ids[i][2]; return 0; }
static int ueth_parse(const u8* c,int n,int cfgval,int kind,usn_cand_t* best){
    int curif=-1, alt=0, cls=0, in=0, out=0, inm=0, outm=0;
    for(int i=0;i+2<=n;){ int bl=c[i]; if(bl<2||i+bl>n)break; int ty=c[i+1];
        if(ty==4&&bl>=9){ if(in&&out)break; curif=c[i+2]; alt=c[i+3]; cls=c[i+5]; in=out=0; }
        else if(ty==5&&bl>=7&&cls==0xFF&&(c[i+3]&3)==2){ int a=c[i+2], m=c[i+4]|(c[i+5]<<8); if(a&0x80){ if(!in){ in=a; inm=m; } } else if(!out){ out=a; outm=m; } }
        i+=bl; }
    if(!in||!out||curif<0)return 0;
    best->kind=kind; best->cfgval=cfgval; best->ctlif=curif; best->dataif=curif; best->alt=alt; best->in_ep=in; best->in_mps=inm&0x7FF; best->out_ep=out; best->out_mps=outm&0x7FF; best->imac=0;
    return 1;
}
static int ure_rd(u16 reg,u16 ty,u8* b,int n){ if(!usn_ctrl(0xC0,0x05,reg,ty,(u16)n,1))return 0; volatile u8* d=(volatile u8*)(u64)usn.dbuf; for(int i=0;i<n;i++)b[i]=d[i]; return 1; }
static int ure_wr(u16 reg,u16 ty,const u8* b,int n){ volatile u8* d=(volatile u8*)(u64)usn.dbuf; for(int i=0;i<n;i++)d[i]=b[i]; return usn_ctrl(0x40,0x05,reg,ty,(u16)n,0); }
static u32 ure_r4(u16 reg,u16 ty){ u8 b[4]={0,0,0,0}; ure_rd((u16)(reg&~3),ty,b,4); return (u32)b[0]|((u32)b[1]<<8)|((u32)b[2]<<16)|((u32)b[3]<<24); }
static u16 ure_r2(u16 reg,u16 ty){ return (u16)(ure_r4(reg,ty)>>((reg&2)<<3)); }
static u8 ure_r1(u16 reg,u16 ty){ return (u8)(ure_r4(reg,ty)>>((reg&3)<<3)); }
static int ure_wn(u16 reg,u16 ty,u32 v,u16 be){ u8 b[4]; v<<=(reg&3)<<3; be=(u16)(be<<(reg&3)); b[0]=(u8)v; b[1]=(u8)(v>>8); b[2]=(u8)(v>>16); b[3]=(u8)(v>>24); return ure_wr((u16)(reg&~3),(u16)(ty|be),b,4); }
static int ure_w1(u16 reg,u16 ty,u32 v){ return ure_wn(reg,ty,v&0xFF,0x11); }
static int ure_w2(u16 reg,u16 ty,u32 v){ return ure_wn(reg,ty,v&0xFFFF,0x33); }
static int ure_w4(u16 reg,u16 ty,u32 v){ return ure_wn(reg,ty,v,0xFF); }
static int axg_rd(u8 cmd,u16 idx,u16 reg,u8* b,int n){ if(!usn_ctrl(0xC0,cmd,reg,idx,(u16)n,1))return 0; volatile u8* d=(volatile u8*)(u64)usn.dbuf; for(int i=0;i<n;i++)b[i]=d[i]; return 1; }
static int axg_wr(u8 cmd,u16 idx,u16 reg,const u8* b,int n){ volatile u8* d=(volatile u8*)(u64)usn.dbuf; for(int i=0;i<n;i++)d[i]=b[i]; return usn_ctrl(0x40,cmd,reg,idx,(u16)n,0); }
static int axg_w1(u16 reg,u8 v){ return axg_wr(1,1,reg,&v,1); }
static int axg_w2(u16 reg,u16 v){ u8 b[2]={(u8)v,(u8)(v>>8)}; return axg_wr(1,2,reg,b,2); }
static int axg_phy(u16 reg){ u8 b[2]={0,0}; if(!axg_rd(2,3,reg,b,2))return -1; return b[0]|(b[1]<<8); }
static int ueth_init(int kind,u8* mac,u16 bcd){
    int ok=0; ueth_chip=kind; ueth_lk=0; ueth_lt=0; ueth_msr=0;
    unf_init(&ueth_fc,kind,usn.out_mps);
    if(kind==UNF_URE){
        ure_w1(0xE813,0x100,0x10);
        for(int i=0;i<50&&(ure_r1(0xE813,0x100)&0x10);i++)xh_delay_ms(10);
        u8 m[8]; if(ure_rd(0xC000,0x100,m,8)&&unf_mac_ok(m))ok=1; else if(ure_rd(0xD000,0x100,m,8)&&unf_mac_ok(m))ok=1;
        if(ok)for(int i=0;i<6;i++)mac[i]=m[i];
        ure_w2(0xC0B4,0x100,ure_r2(0xC0B4,0x100)&~1u); ure_w2(0xC0B4,0x100,ure_r2(0xC0B4,0x100)|1u);
        ure_w1(0xE813,0x100,ure_r1(0xE813,0x100)|0x08|0x04);
        ure_w2(0xE85A,0x100,ure_r2(0xE85A,0x100)&~0x0008u);
        ure_w4(0xC010,0x100,(ure_r4(0xC010,0x100)&~0x1u)|0x2|0x4|0x8);
    } else if(kind==UNF_AXEN){
        axg_w2(0x26,0); axg_w2(0x26,0x20); xh_delay_ms(250);
        axg_w1(0x33,0x03); xh_delay_ms(100);
        if(bcd>=0x0200){ u8 z=0; axg_wr(0x08,1,0,&z,1); }
        u8 m[6]; if(axg_rd(1,6,0x10,m,6)&&unf_mac_ok(m)){ ok=1; for(int i=0;i<6;i++)mac[i]=m[i]; }
        static const u8 q[5]={7,0xCC,0x4C,0x18,0x08}; axg_wr(1,5,0x2E,q,5);
        axg_w1(0x54,0x34); axg_w1(0x55,0x52); axg_w1(0x34,0); axg_w1(0x35,0);
        axg_w2(0x0B,0x0100|0x0080|0x0008|0x0010|0x0002);
        axg_w1(0x24,0);
        ueth_msr=0x0001|0x0002|0x0010|0x0020|0x0100; axg_w2(0x22,(u16)ueth_msr);
    }
    if(!ok){ unf_mac_local(mac,hr_ns()^((u64)usn.vid<<32)^usn.pid); klog("usb-eth: adapter has no valid MAC address, using a random local one"); }
    { char b[96],*p=b; p=scopy(p,"usb-eth: "); p=scopy(p,unf_kind_name(kind)); p=scopy(p," initialised, MAC "); for(int i=0;i<6;i++){ p=hx(p,mac[i],2); if(i<5)*p++=':'; } *p=0; klog(b); }
    return 1;
}
static void ueth_sink(void* a,const u8* f,int n){ (void)a; usn_q_put(f,n); }
static int ueth_rx(const u8* b,int n){ return unf_rx(&ueth_fc,b,n,ueth_sink,0); }
static int ueth_tx(u8* out,int cap,const u8* f,int len){ return unf_tx(&ueth_fc,out,cap,f,len); }
static int ueth_link(void){
    if(!usn.ok)return 0;
    if(ueth_lt&&hr_ns()-ueth_lt<1000000000ULL)return ueth_lk;
    ueth_lt=hr_ns(); int was=ueth_lk;
    int prev=g_xcur; xhc_select(usn.ctl);
    if(ueth_chip==UNF_URE)ueth_lk=(ure_r2(0xE908,0x100)&0x0002)?1:0;
    else if(ueth_chip==UNF_AXEN){ axg_phy(1); int s=axg_phy(1); ueth_lk=s>=0&&(s&4);
        if(ueth_lk&&!was){ int ps=axg_phy(0x11); int msr=0x0100|0x0010|0x0020; if(ps>=0){ int sp=(ps>>14)&3; if(sp==2)msr|=0x0001|0x0008; else if(sp==1)msr|=0x0200; if(ps&0x2000)msr|=0x0002; } if(msr!=ueth_msr){ ueth_msr=msr; axg_w2(0x22,(u16)msr); } } }
    xhc_select(prev);
    if(ueth_lk!=was)klog(ueth_lk?"usb-eth: link up":"usb-eth: link down");
    return ueth_lk;
}
