#ifndef NVX_USB_IPHETH_H
#define NVX_USB_IPHETH_H
enum { IPH_KIND=6, IPH_CLASS=0xFF, IPH_SUB=0xFD, IPH_PROTO=1, IPH_RT_IN=0xC0, IPH_CMD_MAC=0x00, IPH_CMD_CARRIER=0x45, IPH_CARRIER_ON=0x04, IPH_CTRL_LEN=0x40, IPH_PAD=2, IPH_TXLEN=1514, IPH_RXLEN=1516 };
typedef struct { int ifnum, alt, in_ep, in_mps, out_ep, out_mps; } iph_cand_t;
static int iph_find(const u8* c,int n,iph_cand_t* r){
    int match=0, curif=-1, curalt=0, in=0, out=0, inm=0, outm=0, found=0;
    for(int i=0;i+2<=n;){ int bl=c[i]; if(bl<2||i+bl>n)break; int ty=c[i+1];
        if(ty==4&&bl>=9){
            if(match&&in&&out&&!found){ r->ifnum=curif; r->alt=curalt; r->in_ep=in; r->in_mps=inm&0x7FF; r->out_ep=out; r->out_mps=outm&0x7FF; found=1; }
            curif=c[i+2]; curalt=c[i+3]; match=c[i+5]==IPH_CLASS&&c[i+6]==IPH_SUB&&c[i+7]==IPH_PROTO; in=out=inm=outm=0;
        }
        else if(ty==5&&bl>=7&&match&&(c[i+3]&3)==2){ int a=c[i+2], m=c[i+4]|(c[i+5]<<8); if(a&0x80){ if(!in){ in=a; inm=m; } } else if(!out){ out=a; outm=m; } }
        i+=bl; }
    if(match&&in&&out&&!found){ r->ifnum=curif; r->alt=curalt; r->in_ep=in; r->in_mps=inm&0x7FF; r->out_ep=out; r->out_mps=outm&0x7FF; found=1; }
    return found;
}
static int iph_rx(const u8* b,int n,const u8** f){
    if(n>IPH_RXLEN||n-IPH_PAD<14)return -1;
    *f=b+IPH_PAD; return n-IPH_PAD;
}
static int iph_tx(u8* out,int cap,const u8* f,int len){
    if(len<14||len>IPH_TXLEN||cap<IPH_TXLEN)return -1;
    for(int i=0;i<len;i++)out[i]=f[i];
    for(int i=len;i<IPH_TXLEN;i++)out[i]=0;
    return IPH_TXLEN;
}
static int iph_carrier(const u8* b,int n){ return n>=1&&b[0]==IPH_CARRIER_ON; }
static int iph_mac_ok(const u8* m){ int any=0; for(int i=0;i<6;i++)if(m[i])any=1; return any&&!(m[0]&1); }
static int usbnet_phone_vid(u16 v){
    static const u16 t[]={0x05AC,0x18D1,0x04E8,0x22B8,0x0FCE,0x0BB4,0x1004,0x12D1,0x2717,0x2A70,0x22D9,0x19D2,0x2E04,0x2D95,0x0E8D,0x1BBB,0x2A45,0x2AE5};
    for(int i=0;i<(int)(sizeof t/sizeof t[0]);i++)if(t[i]==v)return 1;
    return 0;
}
static const char* usbnet_phone_name(int kind,u16 vid){
    if(kind==IPH_KIND)return "Phone (USB)";
    if((kind==1||kind==2||kind==3)&&usbnet_phone_vid(vid))return "Phone (USB)";
    return 0;
}
#endif
