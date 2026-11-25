#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "net/bluetooth/bt_hid.h"
#include "net/bluetooth/bt_sec.h"
typedef struct { btconn_t* c; l2chan_t *ctl, *in; hidr_t r; volatile u8 hs; u8 hsv, want_boot, authq; u32 reps; } hidh_t;
static hidh_t hh[BT_MAX_CONN];
static int hh_inited;
static hidh_t* hh_of(btconn_t* c,int mk){
    for(int i=0;i<BT_MAX_CONN;i++) if(hh[i].c==c)return &hh[i];
    if(!mk)return 0;
    for(int i=0;i<BT_MAX_CONN;i++) if(!hh[i].c||hh[i].c->state==BTC_FREE){ u8* z=(u8*)&hh[i]; for(int k=0;k<(int)sizeof hh[i];k++)z[k]=0; hh[i].c=c; hidr_boot(&hh[i].r,1,1); return &hh[i]; }
    return 0;
}
static void hh_drop(hidh_t* h){ if(!h->c)return; hidr_release(&h->r); h->c=0; h->ctl=h->in=0; h->want_boot=0; }
static void hh_ctl_rx(l2chan_t* ch,const u8* p,int n){
    hidh_t* h=(hidh_t*)ch->priv; if(!h||n<1)return;
    int t=p[0]>>4, a=p[0]&15;
    if(t==0){ h->hsv=(u8)a; h->hs=1; }
    else if(t==1&&a==5){ klog("bluetooth: HID device unplugged (virtual cable)"); if(h->in)l2cap_close(h->in,500); if(h->ctl)l2cap_close(h->ctl,500); hh_drop(h); }
}
static void hh_in_rx(l2chan_t* ch,const u8* p,int n){
    hidh_t* h=(hidh_t*)ch->priv; if(!h||n<2||p[0]!=0xA1||!ch->c||!ch->c->enc||ch->c->keysz<BT_BREDR_KEYSZ||!bt_keys_find(ch->c->addr,0))return;
    hidr_input(&h->r,p[1],p+2,n-2); h->reps++;
}
static void hh_closed(l2chan_t* ch){
    hidh_t* h=(hidh_t*)ch->priv; if(!h)return;
    if(h->ctl==ch)h->ctl=0;
    if(h->in==ch)h->in=0;
    if(!h->ctl&&!h->in)hh_drop(h);
}
static void hh_bind(hidh_t* h,l2chan_t* ch,int intr){ ch->priv=h; ch->closed=hh_closed; ch->rx=intr?hh_in_rx:hh_ctl_rx; if(intr)h->in=ch; else h->ctl=ch; }
static int hh_accept(l2chan_t* ch){
    if(!ch->c||ch->c->le||(ch->psm!=BT_PSM_HID_CTRL&&ch->psm!=BT_PSM_HID_INTR))return 0;
    if(!bt_keys_find(ch->c->addr,0))return 0;
    hidh_t* h=hh_of(ch->c,1); if(!h)return 0;
    int intr=ch->psm==BT_PSM_HID_INTR; hh_bind(h,ch,intr); if(intr)h->want_boot=1;
    return 1;
}
void hidh_init(void){ if(hh_inited)return; if(l2cap_server(BT_PSM_HID_CTRL,hh_accept)>=0&&l2cap_server(BT_PSM_HID_INTR,hh_accept)>=0)hh_inited=1; }
int hidh_active(btconn_t* c){ hidh_t* h=hh_of(c,0); return h&&h->in; }
void hidh_conn_down(btconn_t* c){ hidh_t* h=hh_of(c,0); if(h)hh_drop(h); }
static int hh_set_boot(hidh_t* h,int ms){
    if(!h->ctl)return 0;
    u8 sp=0x70; h->hs=0;
    if(l2cap_send(h->ctl,&sp,1)<0)return 0;
    if(!bt_conn_wait(h->c,&h->hs,1,ms))return 0;
    return h->hsv==0;
}
int hid_host_connect(btconn_t* c,int ms){
    if(!c||c->state!=BTC_UP||c->le)return -1;
    hidh_init();
    hidh_t* h=hh_of(c,1); if(!h)return -1;
    int to=ms/2<1000?1000:ms/2>8000?8000:ms/2;
    if(!h->ctl){ l2chan_t* ch=l2cap_connect(c,BT_PSM_HID_CTRL,hh_ctl_rx,to); if(!ch){ klog("bluetooth: HID control channel refused"); hh_drop(h); return 0; } hh_bind(h,ch,0); }
    if(!h->in){ l2chan_t* ch=l2cap_connect(c,BT_PSM_HID_INTR,hh_in_rx,to); if(!ch){ klog("bluetooth: HID interrupt channel refused"); l2cap_close(h->ctl,500); hh_drop(h); return 0; } hh_bind(h,ch,1); }
    h->want_boot=0;
    klog(hh_set_boot(h,2000)?"bluetooth: HID input on (boot protocol)":"bluetooth: HID input on (device did not confirm boot protocol)");
    return 1;
}
void bt_hidh_tick(void){
    for(int i=0;i<BT_MAX_CONN;i++){ hidh_t* h=&hh[i]; if(!h->c)continue;
        if(h->c->state!=BTC_UP){ hh_drop(h); continue; }
        if(h->in&&!h->c->enc&&!h->authq){ btconn_t* c=h->c; h->authq=1; if(bt_sec_auth(c,10000)<=0){ bt_disconnect(c,0x05,2000); continue; } }
        if(h->want_boot&&h->ctl&&h->in&&h->in->state==L2_OPEN&&h->ctl->state==L2_OPEN){ u8 sp=0x70; h->want_boot=0; l2cap_send(h->ctl,&sp,1); }
        hidr_tick(&h->r); }
}
int bt_hidh_text(char* p,char* e){
    char* s=p;
    for(int i=0;i<BT_MAX_CONN;i++){ hidh_t* h=&hh[i]; if(!h->c||!h->in)continue;
        const char* t="BR/EDR HID: input on\n"; while(*t&&p<e)*p++=*t++; }
    return (int)(p-s);
}
void bt_hogp_tick(void);
int bt_hogp_text(char* p,char* e);
void bt_hid_poll(void){ bt_hidh_tick(); bt_hogp_tick(); }
int bt_hid_status_text(char* out,int cap){ if(cap<1)return 0; char* p=out; char* e=out+cap-1; p+=bt_hidh_text(p,e); p+=bt_hogp_text(p,e); *p=0; return (int)(p-out); }
