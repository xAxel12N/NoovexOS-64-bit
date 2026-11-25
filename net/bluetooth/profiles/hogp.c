#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "net/bluetooth/bt_hid.h"
#include "net/bluetooth/bt_sec.h"
typedef struct { btconn_t* c; hidr_t r; u16 rh[8]; u8 rid[8]; int nr; u16 bk, bm; int boot; u32 reps; } hogp_t;
static hogp_t hg[BT_MAX_CONN];
static u8 hg_map[512];
static hogp_t* hg_of(btconn_t* c,int mk){
    for(int i=0;i<BT_MAX_CONN;i++) if(hg[i].c==c)return &hg[i];
    if(!mk)return 0;
    for(int i=0;i<BT_MAX_CONN;i++) if(!hg[i].c||hg[i].c->state==BTC_FREE){ u8* z=(u8*)&hg[i]; for(int k=0;k<(int)sizeof hg[i];k++)z[k]=0; hg[i].c=c; return &hg[i]; }
    return 0;
}
static void hg_ntf(btconn_t* c,u16 h,const u8* p,int n){
    hogp_t* g=hg_of(c,0); if(!g||!c->enc||c->keysz<7||!bt_keys_find_le(c->addr,c->atype))return;
    if(g->boot){ if(h&&h==g->bk){ hidr_input(&g->r,1,p,n); g->reps++; } else if(h&&h==g->bm){ hidr_input(&g->r,2,p,n); g->reps++; } return; }
    for(int i=0;i<g->nr;i++) if(g->rh[i]==h){ hidr_input(&g->r,g->rid[i],p,n); g->reps++; return; }
}
static int hg_secure(int e){ return e==0x05||e==0x0F||e==0x08||e==0x0C; }
static int hg_cccd(btconn_t* c,gchr_t* ch,int ms){
    int d=gatt_find_desc(c,(u16)(ch->vh+1),ch->end,0x2902,ms); if(!d)return 0;
    u8 on[2]={1,0}; return gatt_write(c,(u16)d,on,2,ms)==0;
}
int hogp_active(btconn_t* c){ hogp_t* g=hg_of(c,0); return g&&(g->nr||g->boot); }
void hogp_conn_down(btconn_t* c){ hogp_t* g=hg_of(c,0); if(!g)return; hidr_release(&g->r); g->c=0; g->nr=0; g->boot=0; att_conn_down(c); }
int hogp_connect(btconn_t* c,int ms){
    if(!c||c->state!=BTC_UP||!c->le)return -1;
    int to=ms>3000?3000:ms<500?500:ms;
    att_init(); att_set_ntf(hg_ntf);
    hogp_t* g=hg_of(c,1); if(!g)return -1;
    g->nr=0; g->boot=0; g->bk=g->bm=0; g->r.nl=0;
    gatt_exchange_mtu(c,185,to);
    u16 s=0,e=0;
    if(!gatt_find_svc(c,0x1812,&s,&e,to)){ klog("bluetooth: LE device has no HID service"); g->c=0; return 0; }
    gchr_t ch[24]; int nc=gatt_chars(c,s,e,ch,24,to);
    int mi=-1; for(int i=0;i<nc;i++) if(ch[i].uuid==0x2A4B){ mi=i; break; }
    int ml=-1;
    if(mi>=0){ ml=gatt_read(c,ch[mi].vh,hg_map,(int)sizeof hg_map,to);
        if(ml<0&&hg_secure(gatt_err())&&bt_smp_pair(c,30000)>0)ml=gatt_read(c,ch[mi].vh,hg_map,(int)sizeof hg_map,to); }
    if(ml>0&&hidr_parse(&g->r,hg_map,ml)>0){
        for(int i=0;i<nc&&g->nr<8;i++){ if(ch[i].uuid!=0x2A4D||!(ch[i].props&0x10))continue;
            int rr=gatt_find_desc(c,(u16)(ch[i].vh+1),ch[i].end,0x2908,to); u8 ref[2]={0,1};
            if(rr&&gatt_read(c,(u16)rr,ref,2,to)<2)continue;
            if(ref[1]!=1)continue;
            if(hg_cccd(c,&ch[i],to)){ g->rh[g->nr]=ch[i].vh; g->rid[g->nr]=ref[0]; g->nr++; } } }
    if(!g->nr){
        int kb=-1, mo=-1, pm=-1;
        for(int i=0;i<nc;i++){ if(ch[i].uuid==0x2A22)kb=i; else if(ch[i].uuid==0x2A33)mo=i; else if(ch[i].uuid==0x2A4E)pm=i; }
        if(kb<0&&mo<0){ klog("bluetooth: HID service has no usable input reports"); g->c=0; return 0; }
        if(pm>=0){ u8 z=0; gatt_write_cmd(c,ch[pm].vh,&z,1); }
        if(kb>=0&&hg_cccd(c,&ch[kb],to))g->bk=ch[kb].vh;
        if(mo>=0&&hg_cccd(c,&ch[mo],to))g->bm=ch[mo].vh;
        if(!g->bk&&!g->bm){ klog("bluetooth: could not enable HID notifications"); g->c=0; return 0; }
        hidr_boot(&g->r,g->bk!=0,g->bm!=0); g->boot=1;
        klog("bluetooth: LE HID input on (boot protocol)");
        return 1; }
    klog("bluetooth: LE HID input on (report protocol)");
    return 1;
}
void bt_hogp_tick(void){ for(int i=0;i<BT_MAX_CONN;i++){ hogp_t* g=&hg[i]; if(!g->c)continue; if(g->c->state!=BTC_UP){ hogp_conn_down(g->c); continue; } hidr_tick(&g->r); } }
int bt_hogp_text(char* p,char* e){
    char* s=p;
    for(int i=0;i<BT_MAX_CONN;i++){ hogp_t* g=&hg[i]; if(!g->c||(!g->nr&&!g->boot))continue;
        const char* t=g->boot?"LE HID: boot protocol input on\n":"LE HID: report protocol input on\n"; while(*t&&p<e)*p++=*t++; }
    return (int)(p-s);
}
