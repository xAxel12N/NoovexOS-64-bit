#include "include/nvx/base.h"
#include "net/bluetooth/bt_hid.h"
typedef struct { btconn_t* c; volatile u8 got; u8 busy; u16 mtu; int rn; u8 rsp[BT_L2_MTU]; } atts_t;
static atts_t att_s[BT_MAX_CONN];
static att_ntf_cb att_ntf;
static int att_inited;
static atts_t* att_of(btconn_t* c,int mk){
    if(c->att){ atts_t* s=(atts_t*)c->att; if(s->c==c)return s; }
    if(!mk)return 0;
    for(int i=0;i<BT_MAX_CONN;i++){ atts_t* s=&att_s[i]; if(s->c&&s->c->att==s&&s->c->state!=BTC_FREE)continue;
        s->c=c; s->got=0; s->busy=0; s->mtu=23; s->rn=0; c->att=s; return s; }
    return 0;
}
static int att_is_rsp(u8 op){ return op==0x01||op==0x03||op==0x05||op==0x07||op==0x09||op==0x0B||op==0x0D||op==0x0F||op==0x11||op==0x13||op==0x17||op==0x19; }
static void att_rx(btconn_t* c,const u8* p,int n){
    if(n<1)return;
    atts_t* s=att_of(c,1); if(!s)return;
    u8 op=p[0];
    if(op==0x1B||op==0x1D){ if(n>=3&&att_ntf)att_ntf(c,(u16)(p[1]|p[2]<<8),p+3,n-3); if(op==0x1D){ u8 cf=0x1E; l2cap_fixed_send(c,BT_CID_ATT,&cf,1); } return; }
    if(att_is_rsp(op)){ if(s->busy&&!s->got){ int k=n>(int)sizeof s->rsp?(int)sizeof s->rsp:n; for(int i=0;i<k;i++)s->rsp[i]=p[i]; s->rn=k; s->got=1; } return; }
    if(op==0x02&&n>=3){ u16 m=(u16)(p[1]|p[2]<<8); u8 r[3]={0x03,185,0}; if(m<23)m=23; if(m<185)s->mtu=m; else s->mtu=185; l2cap_fixed_send(c,BT_CID_ATT,r,3); return; }
    if(op&0x40)return;
    u8 e[5]={0x01,op,n>=3?p[1]:0,n>=3?p[2]:0,(op==0x04||op==0x06||op==0x08||op==0x10)?0x0A:0x06};
    l2cap_fixed_send(c,BT_CID_ATT,e,5);
}
void att_set_ntf(att_ntf_cb cb){ att_ntf=cb; }
void att_init(void){ if(att_inited)return; if(l2cap_fixed(BT_CID_ATT,att_rx)>=0)att_inited=1; }
void att_conn_down(btconn_t* c){ atts_t* s=att_of(c,0); if(!s)return; s->busy=0; s->got=1; s->c=0; c->att=0; }
int att_mtu(btconn_t* c){ atts_t* s=att_of(c,0); return s?s->mtu:23; }
void att_set_mtu(btconn_t* c,u16 m){ atts_t* s=att_of(c,1); if(s)s->mtu=m<23?23:m>185?185:m; }
int att_cmd(btconn_t* c,const u8* p,int n){ att_init(); return l2cap_fixed_send(c,BT_CID_ATT,p,n); }
int att_req(btconn_t* c,const u8* req,int n,u8* rsp,int cap,int ms){
    att_init();
    if(!c||c->state!=BTC_UP)return -1;
    atts_t* s=att_of(c,1); if(!s||s->busy)return -1;
    s->got=0; s->busy=1; s->rn=0;
    if(l2cap_fixed_send(c,BT_CID_ATT,req,n)<0){ s->busy=0; return -1; }
    int ok=bt_conn_wait(c,&s->got,1,ms);
    s->busy=0;
    if(!ok||!s->got||s->c!=c||!s->rn)return -1;
    int k=s->rn<cap?s->rn:cap; for(int i=0;i<k;i++)rsp[i]=s->rsp[i];
    return k;
}
