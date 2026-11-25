#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "net/bluetooth/bt_host.h"
static l2chan_t lc[BT_MAX_CHAN];
static struct { u16 psm; bt_accept_cb acc; } lsv[6];
static bt_fixed_cb lfx[8];
static u8 l_ident;
static u16 g16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
static u8* p16(u8* p,u16 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); return p+2; }
static u8 nid(void){ l_ident++; if(!l_ident)l_ident=1; return l_ident; }
static void l_log(const char* m,u32 v){ char b[96],*p=b; p=scopy(p,"bluetooth: L2CAP "); p=scopy(p,m); p=scopy(p," 0x"); p=hx(p,v,4); *p=0; klog(b); }
void l2cap_init(void){ for(int i=0;i<BT_MAX_CHAN;i++)lc[i].state=L2_CLOSED; }
static void sig(btconn_t* c,u8 code,u8 id,const u8* d,int n){
    u8 b[4+64]; if(n>64)n=64;
    b[0]=code; b[1]=id; p16(b+2,(u16)n); for(int i=0;i<n;i++)b[4+i]=d[i];
    bt_acl_send(c,c->le?BT_CID_LESIG:BT_CID_SIG,b,4+n);
}
static void rej(btconn_t* c,u8 id,u16 reason,u16 a,u16 b2,int extra){ u8 d[6]; p16(d,reason); p16(d+2,a); p16(d+4,b2); sig(c,0x01,id,d,2+extra); }
static l2chan_t* ch_alloc(btconn_t* c,u16 psm){
    for(int i=0;i<BT_MAX_CHAN;i++) if(lc[i].state==L2_CLOSED){
        u16 cid=0x0040; for(int again=1;again;){ again=0; for(int k=0;k<BT_MAX_CHAN;k++) if(lc[k].state!=L2_CLOSED&&lc[k].c==c&&lc[k].lcid==cid){ cid++; again=1; } }
        l2chan_t* h=&lc[i]; u8* z=(u8*)h; for(int k=0;k<(int)sizeof *h;k++)z[k]=0;
        h->c=c; h->psm=psm; h->lcid=cid; h->imtu=BT_L2_MTU; h->omtu=672; h->state=L2_WAIT_CONN; return h; }
    return 0;
}
static l2chan_t* ch_lcid(btconn_t* c,u16 cid){ for(int i=0;i<BT_MAX_CHAN;i++) if(lc[i].state!=L2_CLOSED&&lc[i].c==c&&lc[i].lcid==cid)return &lc[i]; return 0; }
static void ch_free(l2chan_t* h,int notify){ if(h->state==L2_CLOSED)return; int was=h->state==L2_OPEN; h->state=L2_CLOSED; if(notify&&was&&h->closed)h->closed(h); }
static void cfg_req(l2chan_t* h){ u8 d[8]; p16(d,h->rcid); p16(d+2,0); d[4]=0x01; d[5]=2; p16(d+6,h->imtu); h->ident=nid(); sig(h->c,0x04,h->ident,d,h->imtu?8:4); }
static void ch_check_open(l2chan_t* h){
    if(h->state==L2_CONFIG&&h->cfg_in&&h->cfg_out){ h->state=L2_OPEN; l_log(h->server?"channel accepted, PSM":"channel open, PSM",h->psm); }
}
static void on_conn_req(btconn_t* c,u8 id,const u8* d,int n){
    if(n<4)return;
    u16 psm=g16(d), scid=g16(d+2); u8 r[8]; p16(r,0); p16(r+2,scid); p16(r+6,0);
    bt_accept_cb acc=0; for(int i=0;i<6;i++)if(lsv[i].acc&&lsv[i].psm==psm)acc=lsv[i].acc;
    if(!acc){ p16(r+4,0x0002); sig(c,0x03,id,r,8); l_log("refused unknown PSM",psm); return; }
    if(scid<0x0040){ p16(r+4,0x0006); sig(c,0x03,id,r,8); return; }
    l2chan_t* h=ch_alloc(c,psm); if(!h){ p16(r+4,0x0004); sig(c,0x03,id,r,8); return; }
    h->rcid=scid; h->server=1; h->state=L2_CONFIG;
    if(!acc(h)){ h->state=L2_CLOSED; p16(r+4,0x0003); sig(c,0x03,id,r,8); return; }
    p16(r,h->lcid); p16(r+4,0); sig(c,0x03,id,r,8);
    cfg_req(h);
}
static void on_conn_rsp(btconn_t* c,const u8* d,int n){
    if(n<8)return;
    l2chan_t* h=ch_lcid(c,g16(d+2)); if(!h||h->state!=L2_WAIT_CONN)return;
    u16 res=g16(d+4);
    if(res==1)return;
    if(res){ l_log("connection refused, result",res); ch_free(h,0); return; }
    h->rcid=g16(d); h->state=L2_CONFIG; cfg_req(h);
}
static void on_cfg_req(btconn_t* c,u8 id,const u8* d,int n){
    if(n<4)return;
    l2chan_t* h=ch_lcid(c,g16(d)); u16 fl=g16(d+2);
    if(!h){ rej(c,id,0x0002,g16(d),0,4); return; }
    u8 r[64]; int rn=6; u16 res=0; u8 bad[16]; int bn=0;
    for(int o=4;o+2<=n;){ int t=d[o], l=d[o+1]; if(o+2+l>n)break; const u8* v=d+o+2;
        int ty=t&0x7F;
        if(ty==0x01&&l>=2){ u16 m=g16(v); h->omtu=m<48?48:m; }
        else if(ty==0x04&&l>=1&&v[0]!=0){ if(res!=0x0001&&rn+11<=(int)sizeof r){ res=0x0001; r[rn++]=0x04; r[rn++]=9; r[rn++]=0; for(int k=0;k<8;k++)r[rn++]=0; } }
        else if(ty!=0x01&&ty!=0x02&&ty!=0x03&&ty!=0x04&&ty!=0x05&&ty!=0x06&&ty!=0x07&&!(t&0x80)&&bn<(int)sizeof bad)bad[bn++]=(u8)t;
        o+=2+l; }
    if(bn){ res=0x0003; rn=6; for(int i=0;i<bn;i++)r[rn++]=bad[i]; }
    p16(r,h->rcid); p16(r+2,(u16)(fl&1)); p16(r+4,res);
    sig(c,0x05,id,r,rn);
    if(!res&&!(fl&1)){ h->cfg_in=1; ch_check_open(h); }
}
static void on_cfg_rsp(btconn_t* c,const u8* d,int n){
    if(n<6)return;
    l2chan_t* h=ch_lcid(c,g16(d)); if(!h||h->state!=L2_CONFIG)return;
    u16 fl=g16(d+2), res=g16(d+4);
    if(res==0){ if(!(fl&1)){ h->cfg_out=1; ch_check_open(h); } return; }
    if(res==4)return;
    if(res==1&&h->imtu){ h->imtu=0; cfg_req(h); return; }
    l_log("configuration failed, result",res);
    u8 e[4]; p16(e,h->rcid); p16(e+2,h->lcid); h->state=L2_WAIT_DISC; sig(c,0x06,nid(),e,4);
}
static void on_disc_req(btconn_t* c,u8 id,const u8* d,int n){
    if(n<4)return;
    l2chan_t* h=ch_lcid(c,g16(d));
    if(!h){ rej(c,id,0x0002,g16(d),g16(d+2),4); return; }
    u8 r[4]; p16(r,h->lcid); p16(r+2,h->rcid); sig(c,0x07,id,r,4);
    l_log("channel closed by peer, PSM",h->psm); ch_free(h,1);
}
static void on_info_req(btconn_t* c,u8 id,const u8* d,int n){
    if(n<2){ return; } u16 t=g16(d); u8 r[12]; p16(r,t);
    if(t==1){ p16(r+2,0); p16(r+4,BT_L2_MTU); sig(c,0x0B,id,r,6); }
    else if(t==2){ p16(r+2,0); for(int i=4;i<8;i++)r[i]=0; r[4]=0x80; sig(c,0x0B,id,r,8); }
    else if(t==3){ p16(r+2,0); for(int i=4;i<12;i++)r[i]=0; r[4]=0x02; sig(c,0x0B,id,r,12); }
    else { p16(r+2,1); sig(c,0x0B,id,r,4); }
}
static void bredr_sig(btconn_t* c,const u8* p,int n){
    for(int o=0;o+4<=n;){ u8 code=p[o], id=p[o+1]; int l=g16(p+o+2); if(o+4+l>n)break; const u8* d=p+o+4;
        switch(code){
        case 0x01: for(int i=0;i<BT_MAX_CHAN;i++) if(lc[i].state!=L2_CLOSED&&lc[i].c==c&&lc[i].ident==id&&lc[i].state!=L2_OPEN){ l_log("command rejected, PSM",lc[i].psm); ch_free(&lc[i],0); } break;
        case 0x02: on_conn_req(c,id,d,l); break;
        case 0x03: on_conn_rsp(c,d,l); break;
        case 0x04: on_cfg_req(c,id,d,l); break;
        case 0x05: on_cfg_rsp(c,d,l); break;
        case 0x06: on_disc_req(c,id,d,l); break;
        case 0x07: if(l>=4){ l2chan_t* h=ch_lcid(c,g16(d+2)); if(h&&h->state==L2_WAIT_DISC)ch_free(h,0); } break;
        case 0x08: sig(c,0x09,id,d,l); break;
        case 0x0A: on_info_req(c,id,d,l); break;
        case 0x0B: break;
        default: rej(c,id,0x0000,0,0,0); break;
        }
        o+=4+l; }
}
static void le_sig(btconn_t* c,const u8* p,int n){
    if(n<4){ return; } u8 code=p[0], id=p[1]; int l=g16(p+2); if(4+l>n)return; const u8* d=p+4;
    if(code==0x12){
        if(l<8){ return; } u16 mn=g16(d), mx=g16(d+2), lat=g16(d+4), to=g16(d+6);
        int ok=mn>=6&&mx<=3200&&mn<=mx&&lat<=499&&to>=10&&to<=3200&&(u32)to*4>(u32)(1+lat)*mx;
        u8 r[2]; p16(r,ok?0:1); sig(c,0x13,id,r,2);
        if(ok){ u8 h[14]; p16(h,c->handle); p16(h+2,mn); p16(h+4,mx); p16(h+6,lat); p16(h+8,to); p16(h+10,0); p16(h+12,0); bt_hci_send(0x2013,h,14); }
    }
    else if(code==0x14){ u8 r[10]; for(int i=0;i<10;i++)r[i]=0; p16(r+8,0x0002); sig(c,0x15,id,r,10); }
    else if(code==0x17){ u8 r[8]; for(int i=0;i<8;i++)r[i]=0; p16(r+6,0x0002); sig(c,0x18,id,r,8); }
    else if(code==0x01||code==0x13||code==0x16){}
    else rej(c,id,0x0000,0,0,0);
}
void l2cap_rx(btconn_t* c,u16 cid,const u8* p,int n){
    if(!c||n<0)return;
    if(cid==BT_CID_SIG&&!c->le){ bredr_sig(c,p,n); return; }
    if(cid==BT_CID_LESIG&&c->le){ le_sig(c,p,n); return; }
    if(cid<0x0040){
        if(cid<8&&lfx[cid]){ lfx[cid](c,p,n); return; }
        if(cid==BT_CID_SMP&&c->le&&n>=1&&p[0]!=0x05){ u8 r[2]={0x05,0x05}; bt_acl_send(c,cid,r,2); }
        else if(cid==BT_CID_ATT&&c->le&&n>=1&&!(p[0]&1)&&p[0]!=0x52&&p[0]!=0xD2&&p[0]!=0x1E){ u8 r[5]={0x01,p[0],0,0,0x06}; bt_acl_send(c,cid,r,5); }
        return;
    }
    l2chan_t* h=ch_lcid(c,cid);
    if(h&&(h->state==L2_OPEN||h->state==L2_CONFIG)&&h->rx)h->rx(h,p,n);
}
void l2cap_conn_down(btconn_t* c){ for(int i=0;i<BT_MAX_CHAN;i++) if(lc[i].state!=L2_CLOSED&&lc[i].c==c)ch_free(&lc[i],1); }
int l2cap_server(u16 psm,bt_accept_cb accept){
    for(int i=0;i<6;i++) if(lsv[i].acc&&lsv[i].psm==psm){ lsv[i].acc=accept; return 1; }
    for(int i=0;i<6;i++) if(!lsv[i].acc){ lsv[i].psm=psm; lsv[i].acc=accept; return 1; }
    return 0;
}
l2chan_t* l2cap_connect(btconn_t* c,u16 psm,void (*rx)(l2chan_t*,const u8*,int),int ms){
    if(!c||c->le||c->state!=BTC_UP)return 0;
    if(ms<500){ ms=500; } if(ms>20000)ms=20000;
    l2chan_t* h=ch_alloc(c,psm); if(!h){ l_log("no free channel for PSM",psm); return 0; }
    h->rx=rx;
    u8 d[4]; p16(d,psm); p16(d+2,h->lcid); h->ident=nid(); sig(c,0x02,h->ident,d,4);
    if(bt_conn_wait(c,&h->state,L2_OPEN,ms))return h;
    if(h->state!=L2_CLOSED){
        l_log("channel setup timed out, PSM",psm);
        if(h->rcid&&c->state==BTC_UP){ u8 e[4]; p16(e,h->rcid); p16(e+2,h->lcid); h->state=L2_WAIT_DISC; sig(c,0x06,nid(),e,4); bt_conn_wait(c,&h->state,0xFF,1000); }
        h->state=L2_CLOSED;
    }
    return 0;
}
int l2cap_send(l2chan_t* h,const u8* p,int n){
    if(!h||h->state!=L2_OPEN||!h->c||n>h->omtu)return -1;
    return bt_acl_send(h->c,h->rcid,p,n);
}
int l2cap_close(l2chan_t* h,int ms){
    if(!h||h->state==L2_CLOSED)return 1;
    btconn_t* c=h->c;
    if(c&&c->state==BTC_UP&&h->rcid&&h->state!=L2_WAIT_CONN){
        u8 e[4]; p16(e,h->rcid); p16(e+2,h->lcid); h->state=L2_WAIT_DISC; sig(c,0x06,nid(),e,4);
        bt_conn_wait(c,&h->state,0xFF,ms<200?200:ms>5000?5000:ms);
    }
    int ok=h->state==L2_CLOSED; h->state=L2_CLOSED; return ok;
}
int l2cap_fixed(u16 cid,bt_fixed_cb rx){ if(cid>=8||cid==BT_CID_SIG||cid==BT_CID_LESIG)return 0; lfx[cid]=rx; return 1; }
int l2cap_fixed_send(btconn_t* c,u16 cid,const u8* p,int n){ if(!c||!c->le)return -1; return bt_acl_send(c,cid,p,n); }
int l2cap_le_conn_update(btconn_t* c,u16 imin,u16 imax,u16 lat,u16 to){
    if(!c||!c->le||c->state!=BTC_UP)return 0;
    u8 h[14]; p16(h,c->handle); p16(h+2,imin); p16(h+4,imax); p16(h+6,lat); p16(h+8,to); p16(h+10,0); p16(h+12,0);
    u8 rp[4]; int r=bt_hci_cmd(0x2013,h,14,rp,(int)sizeof rp,2000); return r>=1&&!rp[0];
}
