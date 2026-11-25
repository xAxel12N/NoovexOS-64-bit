#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "net/bluetooth/bt_host.h"
#include "net/bluetooth/profiles/rfcomm.h"
static u8 rfc_crc(const u8* p,int n){ u8 c=0xFF; for(int i=0;i<n;i++){ c^=p[i]; for(int b=0;b<8;b++)c=(u8)(c&1?(c>>1)^0xE0:c>>1); } return c; }
u8 rfc_fcs(const u8* p,int n){ return (u8)(0xFF-rfc_crc(p,n)); }
int rfc_build(u8 dlci,int cr,u8 ctl,int credits,const u8* info,int n,u8* out,int cap){
    if(n<0||n>32767||cap<n+6)return -1;
    int k=0; out[k++]=(u8)((dlci<<2)|(cr?2:0)|1); out[k++]=(u8)(ctl|(credits>=0?RFC_PF:0));
    if(n<128)out[k++]=(u8)((n<<1)|1); else { out[k++]=(u8)(n<<1); out[k++]=(u8)(n>>7); }
    int hl=k; if(credits>=0)out[k++]=(u8)credits;
    for(int i=0;i<n;i++)out[k++]=info[i];
    out[k++]=rfc_fcs(out,(ctl&~RFC_PF)==RFC_UIH?2:hl); return k;
}
int rfc_parse(const u8* p,int n,int cfc,rfcframe_t* f){
    if(n<4||!(p[0]&1))return 0;
    f->dlci=p[0]>>2; f->cr=(p[0]>>1)&1; f->type=(u8)(p[1]&~RFC_PF); f->pf=(p[1]&RFC_PF)?1:0;
    int k=2, len; if(p[2]&1){ len=p[2]>>1; k=3; } else { if(n<5)return 0; len=(p[2]>>1)|(p[3]<<7); k=4; }
    int hl=k; f->hascr=0; f->credits=0;
    if(f->type==RFC_UIH&&f->pf&&cfc&&f->dlci){ if(k>=n)return 0; f->credits=p[k++]; f->hascr=1; }
    if(k+len+1!=n)return 0;
    if(f->type!=RFC_UIH&&f->type!=RFC_SABM&&f->type!=RFC_UA&&f->type!=RFC_DM&&f->type!=RFC_DISC)return 0;
    if(rfc_fcs(p,f->type==RFC_UIH?2:hl)!=p[n-1])return 0;
    f->info=p+k; f->len=len; return 1;
}
enum { RS_IDLE, RS_MUX, RS_PN, RS_DLC, RS_UP, RS_LMUX, RS_LDLC };
static struct { l2chan_t* l2; btconn_t* c; u8 dlci, cfc, msc_in, msc_out, ini, lch; volatile u8 st, ev; int tx_cr, rx_cr, mtu; rfc_rx_cb rx; } R;
static struct { u8 ch; int (*ok)(btconn_t*); rfc_rx_cb rx; void (*up)(btconn_t*); } RL;
static void rs_tx(u8 dlci,int cr,u8 ctl,int credits,const u8* info,int n){ u8 b[RFC_MTU+8]; if(!R.ini)cr=!cr; int k=rfc_build(dlci,cr,ctl,credits,info,n,b,(int)sizeof b); if(k>0)l2cap_send(R.l2,b,k); }
static void rs_mcc(u8 type,int cmd,const u8* d,int n){ u8 b[16]; if(n>12)return; b[0]=(u8)(type|(cmd?2:0)|1); b[1]=(u8)((n<<1)|1); for(int i=0;i<n;i++)b[2+i]=d[i]; rs_tx(0,1,RFC_UIH,-1,b,2+n); }
static void rs_msc(int cmd){ u8 d[2]={(u8)((R.dlci<<2)|3),0x8D}; rs_mcc(0xE0,cmd,d,2); }
static void rs_grant(void){ if(R.cfc&&R.rx_cr<4){ int g=7-R.rx_cr; R.rx_cr+=g; rs_tx(R.dlci,1,RFC_UIH,g,0,0); } }
static void rs_mux_rx(const u8* p,int n){
    if(n<2){ return; } u8 t=p[0]&0xFC; int cmd=(p[0]>>1)&1; int l=p[1]>>1; if(!(p[1]&1)||2+l>n)return; const u8* d=p+2;
    if(t==0x80&&cmd&&!R.ini&&l>=8){ u8 dl=d[0]&0x3F; int m=d[4]|(d[5]<<8);
        if(R.st!=RS_LDLC||(dl>>1)!=R.lch||!dl||m<16){ rs_tx(dl,0,RFC_DM|RFC_PF,-1,0,0); return; }
        R.dlci=dl; R.cfc=(d[1]>>4)==0xF; R.mtu=m<RFC_MTU?m:RFC_MTU; R.tx_cr=R.cfc?(d[7]&7):0; R.rx_cr=R.cfc?7:0;
        u8 r[8]={dl,(u8)(R.cfc?0xE0:0),d[2],0,(u8)R.mtu,(u8)(R.mtu>>8),0,(u8)(R.cfc?7:0)}; rs_mcc(0x80,0,r,8); }
    else if(t==0x80&&!cmd&&l>=8&&(d[0]&0x3F)==R.dlci&&R.st==RS_PN){ R.cfc=(d[1]>>4)==0xE; int m=d[4]|(d[5]<<8); if(m<16){ R.ev=0; return; } R.mtu=m<R.mtu?m:R.mtu; R.tx_cr=R.cfc?(d[7]&7):0; R.ev=1; }
    else if(t==0xE0&&cmd&&l>=2){ rs_mcc(0xE0,0,d,l); if((d[0]>>2)==R.dlci)R.msc_in=1; }
    else if(t==0xE0&&!cmd){ R.msc_out=1; }
    else if(t==0x20&&cmd){ rs_mcc(0x20,0,d,l); }
    else if(t==0x90&&cmd){ u8 r[8]={0,0x07,0,0,0,0,0,0}; int k=l>8?8:l; for(int i=0;i<k;i++)r[i]=d[i]; rs_mcc(0x90,0,r,l==1?8:k); }
    else if(t==0x50&&cmd){ rs_mcc(0x50,0,d,l); }
    else if(cmd){ u8 r=p[0]; rs_mcc(0x10,0,&r,1); }
}
static void rs_rx(l2chan_t* ch,const u8* p,int n){
    rfcframe_t f; (void)ch; if(!rfc_parse(p,n,R.cfc,&f))return;
    if(f.dlci==0){
        if(f.type==RFC_UA&&R.st==RS_MUX)R.ev=1;
        else if(f.type==RFC_DM&&R.st==RS_MUX)R.ev=0;
        else if(f.type==RFC_DISC){ rs_tx(0,0,RFC_UA,-1,0,0); R.st=RS_IDLE; R.ev=0; }
        else if(f.type==RFC_UIH)rs_mux_rx(f.info,f.len);
        else if(f.type==RFC_SABM&&!R.ini&&f.pf&&(R.st==RS_LMUX||R.st==RS_LDLC)){ rs_tx(0,0,RFC_UA|RFC_PF,-1,0,0); R.st=RS_LDLC; }
        else if(f.type==RFC_SABM)rs_tx(0,0,RFC_DM,-1,0,0);
        return; }
    if(!R.ini&&R.st==RS_LMUX)return;
    if(!R.ini&&f.type==RFC_SABM&&R.st==RS_LDLC){
        if((f.dlci>>1)!=R.lch||(R.dlci&&f.dlci!=R.dlci)){ rs_tx(f.dlci,0,RFC_DM|RFC_PF,-1,0,0); return; }
        if(!R.dlci){ R.dlci=f.dlci; R.cfc=0; R.tx_cr=R.rx_cr=0; }
        rs_tx(R.dlci,0,RFC_UA|RFC_PF,-1,0,0); R.st=RS_UP; R.rx=RL.rx; rs_msc(1); klog("bluetooth: RFCOMM channel opened by the device"); if(RL.up)RL.up(R.c); return; }
    if(f.dlci!=R.dlci){ if(f.type==RFC_SABM)rs_tx(f.dlci,0,RFC_DM,-1,0,0); return; }
    if(f.type==RFC_UA&&R.st==RS_DLC)R.ev=1;
    else if(f.type==RFC_DM&&(R.st==RS_DLC||R.st==RS_PN))R.ev=0;
    else if(f.type==RFC_DISC){ rs_tx(R.dlci,0,RFC_UA,-1,0,0); if(R.st==RS_UP)klog("bluetooth: RFCOMM channel closed by the device"); R.st=R.ini?RS_MUX:RS_LDLC; if(!R.ini)R.dlci=0; R.ev=0; }
    else if(f.type==RFC_UIH&&R.st==RS_UP){ if(f.hascr){ R.tx_cr+=f.credits; if(R.tx_cr>RFC_MAXCR)R.tx_cr=RFC_MAXCR; } if(f.len){ if(R.cfc){ if(R.rx_cr<=0)return; R.rx_cr--; } if(R.rx)R.rx(f.info,f.len); rs_grant(); } }
}
static void rs_closed(l2chan_t* ch){ (void)ch; R.l2=0; R.st=RS_IDLE; R.ev=0; }
static int rs_wait(int ms){ R.ev=3; u8 st=R.st; int ok=bt_conn_wait(R.c,&R.ev,1,ms); return ok&&R.st==st; }
int rfcomm_open(btconn_t* c,u8 ch,rfc_rx_cb rx,int ms){
    if(!c||c->le||c->state!=BTC_UP||ch<1||ch>30)return 0;
    rfcomm_close();
    R.c=c; R.ini=1; R.rx=0; R.cfc=0; R.mtu=RFC_MTU; R.dlci=(u8)(ch<<1); R.msc_in=R.msc_out=0; R.tx_cr=R.rx_cr=0;
    int to=ms<1000?1000:ms>10000?10000:ms;
    R.l2=l2cap_connect(c,RFC_PSM,rs_rx,to); if(!R.l2){ klog("bluetooth: RFCOMM L2CAP channel refused"); return 0; }
    R.l2->closed=rs_closed;
    R.st=RS_MUX; rs_tx(0,1,RFC_SABM|RFC_PF,-1,0,0);
    if(!rs_wait(to)){ klog("bluetooth: RFCOMM multiplexer refused"); goto bad; }
    R.st=RS_PN; { u8 d[8]={R.dlci,0xF0,0,0,(u8)RFC_MTU,0,0,7}; rs_mcc(0x80,1,d,8); }
    if(!rs_wait(to)){ klog("bluetooth: RFCOMM parameter negotiation failed"); goto bad; }
    R.rx_cr=R.cfc?7:0; R.st=RS_DLC; rs_tx(R.dlci,1,RFC_SABM|RFC_PF,-1,0,0);
    if(!rs_wait(to)){ klog("bluetooth: RFCOMM channel refused by the device"); goto bad; }
    R.st=RS_UP; R.rx=rx; rs_msc(1);
    for(int i=0;i<10&&!(R.msc_in&&R.msc_out)&&R.st==RS_UP;i++){ volatile u8 f=3; bt_conn_wait(c,&f,1,100); }
    klog(R.cfc?"bluetooth: RFCOMM channel open (credit flow control)":"bluetooth: RFCOMM channel open");
    return R.st==RS_UP;
bad:
    rfcomm_close(); return 0;
}
int rfcomm_send(const u8* p,int n){
    if(R.st!=RS_UP||!R.l2||n<0){ return -1; } int s=0;
    while(s<n){ int k=n-s; if(k>R.mtu)k=R.mtu; if(R.cfc){ if(R.tx_cr<=0)break; R.tx_cr--; }
        int g=-1; if(R.cfc&&R.rx_cr<4){ g=7-R.rx_cr; R.rx_cr+=g; }
        rs_tx(R.dlci,1,RFC_UIH,g,p+s,k); s+=k; }
    return s;
}
void rfcomm_close(void){
    if(R.l2&&R.l2->state==L2_OPEN){ if(R.st==RS_UP||R.st==RS_DLC)rs_tx(R.dlci,1,RFC_DISC|RFC_PF,-1,0,0); rs_tx(0,1,RFC_DISC|RFC_PF,-1,0,0); R.l2->closed=0; l2cap_close(R.l2,500); }
    R.l2=0; R.st=RS_IDLE; R.rx=0;
}
int rfcomm_up(void){ return R.st==RS_UP&&R.l2&&R.l2->state==L2_OPEN; }
btconn_t* rfcomm_conn(void){ return rfcomm_up()?R.c:0; }
static int rs_accept(l2chan_t* ch){
    if(!RL.ok||!ch->c||ch->c->le||ch->psm!=RFC_PSM)return 0;
    if(R.st!=RS_IDLE&&R.l2&&R.l2->state!=L2_CLOSED){ klog("bluetooth: RFCOMM session already in use, incoming channel refused"); return 0; }
    if(!RL.ok(ch->c)){ klog("bluetooth: incoming RFCOMM refused (not an encrypted, bonded headset)"); return 0; }
    R.l2=ch; R.c=ch->c; R.ini=0; R.lch=RL.ch; R.rx=0; R.cfc=0; R.mtu=RFC_MTU; R.dlci=0; R.msc_in=R.msc_out=0; R.tx_cr=R.rx_cr=0; R.ev=0; R.st=RS_LMUX;
    ch->rx=rs_rx; ch->closed=rs_closed; return 1;
}
int rfcomm_listen(u8 ch,int (*ok)(btconn_t*),rfc_rx_cb rx,void (*up)(btconn_t*)){
    if(ch<1||ch>30||!ok)return 0;
    RL.ch=ch; RL.ok=ok; RL.rx=rx; RL.up=up;
    return l2cap_server(RFC_PSM,rs_accept);
}
int rfcomm_incoming(void){ return rfcomm_up()&&!R.ini; }
