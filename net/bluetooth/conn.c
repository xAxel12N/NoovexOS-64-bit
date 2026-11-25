#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/drivers/bluetooth/bt.h"
#include "net/bluetooth/bt_host.h"
void ui_pump(void);
extern volatile u32 g_ml_beat;
__attribute__((weak)) int bt_sec_event(const u8* e,int n){ (void)e; (void)n; return 0; }
__attribute__((weak)) int bt_sec_auth(btconn_t* c,int ms){ (void)c; (void)ms; return -1; }
__attribute__((weak)) int bt_smp_pair(btconn_t* c,int ms){ (void)c; (void)ms; return -1; }
__attribute__((weak)) int bt_sco_conn_req(const u8* p,int pl){ (void)p; (void)pl; return 0; }
__attribute__((weak)) void bt_sco_event(const u8* e,int n){ (void)e; (void)n; }
__attribute__((weak)) int bt_mgr_accept(const u8* a,u32 cod,int le){ (void)a; (void)cod; (void)le; return 0; }
static btconn_t bc[BT_MAX_CONN];
static int bc_infl[BT_MAX_CONN];
static struct { u16 mtu, lmtu, n, ln; int fr, lfr, shared; u64 pump_t; } fc;
static struct { bt_conn_cb up; bt_disc_cb down; } bl[8];
static int bl_n;
static u16 be16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
static int addr_eq(const u8* a,const u8* b){ for(int i=0;i<6;i++)if(a[i]!=b[i])return 0; return 1; }
static void bc_log(const char* m,btconn_t* c,u32 v){
    static const char h[]="0123456789ABCDEF"; char b[128],*p=b; p=scopy(p,"bluetooth: "); p=scopy(p,m); *p++=' ';
    for(int i=5;i>=0;i--){ *p++=h[c->addr[i]>>4]; *p++=h[c->addr[i]&15]; if(i)*p++=':'; }
    p=scopy(p," 0x"); p=hx(p,v,2); *p=0; klog(b);
}
__attribute__((weak)) void bt_sec_ui(void);
static void bc_idle(void){ u64 t=hr_ns(); if(bt_sec_ui)bt_sec_ui(); if(t-fc.pump_t>20000000ULL){ fc.pump_t=t; g_ml_beat++; ui_pump(); } }
static int bc_idx(btconn_t* c){ return (int)(c-bc); }
static int* bc_pool(btconn_t* c){ return (c->le&&!fc.shared)?&fc.lfr:&fc.fr; }
static int bc_mtu(btconn_t* c){ int m=(c->le&&!fc.shared)?fc.lmtu:fc.mtu; if(m>1020)m=1020; if(m<27)m=27; return m; }
static void bc_free(btconn_t* c,u8 reason){
    int up=c->state==BTC_UP||c->state==BTC_CLOSING; int i=bc_idx(c);
    if(bc_infl[i]){ int* pl=bc_pool(c); *pl+=bc_infl[i]; bc_infl[i]=0; }
    c->reason=reason;
    if(up){ l2cap_conn_down(c); for(int k=0;k<bl_n;k++)if(bl[k].down)bl[k].down(c,reason); }
    c->state=BTC_FREE; c->handle=0xFFFF; c->rxn=0; c->sec=0; c->mgr=0; c->att=0;
}
static btconn_t* bc_alloc(const u8* a,int le,int atype){
    for(int i=0;i<BT_MAX_CONN;i++) if(bc[i].state==BTC_FREE){ btconn_t* c=&bc[i]; u8* z=(u8*)c; for(int k=0;k<(int)sizeof *c;k++)z[k]=0;
        for(int k=0;k<6;k++){ c->addr[k]=a[k]; } c->le=(u8)le; c->atype=(u8)atype; c->handle=0xFFFF; c->le_mtu=23; c->state=BTC_CONNECTING; bc_infl[i]=0; return c; }
    return 0;
}
static u8 bc_smtu;
int bt_hci_sco_mtu(void){ return bc_smtu; }
void bt_conn_init(void){
    for(int i=0;i<BT_MAX_CONN;i++) if(bc[i].state!=BTC_FREE)bc_free(&bc[i],0x16);
    fc.mtu=27; fc.n=1; fc.lmtu=0; fc.ln=0;
    u8 rp[16]; int n=bt_hci_cmd(0x1005,0,0,rp,(int)sizeof rp,2000);
    if(n>=8&&!rp[0]){ fc.mtu=be16(rp+1); fc.n=be16(rp+4); bc_smtu=rp[3]; }
    if(bt_hci_le()){ n=bt_hci_cmd(0x2002,0,0,rp,(int)sizeof rp,2000); if(n>=4&&!rp[0]){ fc.lmtu=be16(rp+1); fc.ln=rp[3]; } }
    if(!fc.n)fc.n=1;
    fc.shared=!fc.lmtu||!fc.ln; fc.fr=fc.n; fc.lfr=fc.ln;
    char b[96],*p=b; p=scopy(p,"bluetooth: ACL buffers "); p=udec(p,fc.n); p=scopy(p," x "); p=udec(p,fc.mtu);
    if(!fc.shared){ p=scopy(p,", LE "); p=udec(p,fc.ln); p=scopy(p," x "); p=udec(p,fc.lmtu); } *p=0; klog(b);
}
btconn_t* bt_conn_by_handle(u16 h){ for(int i=0;i<BT_MAX_CONN;i++) if(bc[i].state!=BTC_FREE&&bc[i].handle==h)return &bc[i]; return 0; }
btconn_t* bt_conn_by_addr(const u8* a,int le){ for(int i=0;i<BT_MAX_CONN;i++) if(bc[i].state!=BTC_FREE&&bc[i].le==(u8)le&&addr_eq(bc[i].addr,a))return &bc[i]; return 0; }
btconn_t* bt_conn_at(int i){ return (i>=0&&i<BT_MAX_CONN&&bc[i].state!=BTC_FREE)?&bc[i]:0; }
int bt_conn_listen(bt_conn_cb up,bt_disc_cb down){ for(int i=0;i<bl_n;i++)if(bl[i].up==up&&bl[i].down==down)return 1; if(bl_n>=8)return 0; bl[bl_n].up=up; bl[bl_n].down=down; bl_n++; return 1; }
static void bc_up(btconn_t* c,u16 h){
    c->handle=h; c->state=BTC_UP; c->t_up=hr_ns(); c->rxn=0;
    bc_log(c->le?"LE connected":"connected",c,h);
    for(int k=0;k<bl_n;k++)if(bl[k].up)bl[k].up(c);
}
static void ev_conn(const u8* p,int pl){
    if(pl<11||p[9]!=1)return;
    btconn_t* c=0; for(int i=0;i<BT_MAX_CONN;i++) if(bc[i].state==BTC_CONNECTING&&!bc[i].le&&addr_eq(bc[i].addr,p+3)){ c=&bc[i]; break; }
    if(!c){ if(p[0])return; c=bc_alloc(p+3,0,0); if(!c){ u8 d[3]={p[1],p[2],0x14}; bt_hci_send(0x0406,d,3); return; } }
    if(p[0]){ bc_log("connection failed",c,p[0]); bc_free(c,p[0]); return; }
    c->enc=p[10]; bc_up(c,(u16)(be16(p+1)&0xFFF));
}
static void ev_conn_req(const u8* p,int pl){
    if(pl<10)return;
    u32 cod=(u32)p[6]|((u32)p[7]<<8)|((u32)p[8]<<16);
    u8 d[7]; for(int i=0;i<6;i++)d[i]=p[i];
    if(p[9]!=1&&bt_sco_conn_req(p,pl))return;
    if(p[9]!=1||bt_conn_by_addr(p,0)||!bt_mgr_accept(p,cod,0)){ d[6]=0x0F; bt_hci_send(0x040A,d,7); return; }
    btconn_t* c=bc_alloc(p,0,0); if(!c){ d[6]=0x0D; bt_hci_send(0x040A,d,7); return; }
    c->cod=cod; d[6]=0x01; bt_hci_send(0x0409,d,7);
}
static void ev_le_conn(const u8* p,int pl,int enh){
    int need=enh?31:19; if(pl<need)return;
    int o=enh?12:0;
    btconn_t* c=0; for(int i=0;i<BT_MAX_CONN;i++) if(bc[i].state==BTC_CONNECTING&&bc[i].le){ c=&bc[i]; break; }
    if(p[1]){ if(c){ bc_log("LE connection failed",c,p[1]); bc_free(c,p[1]); } return; }
    if(!c||!addr_eq(c->addr,p+6)){ btconn_t* o2=bt_conn_by_addr(p+6,1); if(!o2||o2->state!=BTC_CONNECTING){ c=bc_alloc(p+6,1,p[5]); if(!c){ u8 d[3]={p[2],p[3],0x14}; bt_hci_send(0x0406,d,3); return; } } else c=o2; }
    c->role=p[4]; c->atype=p[5]; c->interval=be16(p+12+o); c->latency=be16(p+14+o); c->timeout=be16(p+16+o);
    bc_up(c,(u16)(be16(p+2)&0xFFF));
}
static void ev_nocp(const u8* p,int pl){
    if(pl<1){ return; } int num=p[0]; if(1+num*4>pl)return;
    for(int i=0;i<num;i++){ u16 h=be16(p+1+i*4)&0xFFF; int cnt=be16(p+3+i*4); btconn_t* c=bt_conn_by_handle(h); if(!c)continue;
        int k=bc_idx(c); if(cnt>bc_infl[k])cnt=bc_infl[k]; bc_infl[k]-=cnt; *bc_pool(c)+=cnt; }
    if(fc.fr>fc.n){ fc.fr=fc.n; } if(fc.lfr>fc.ln)fc.lfr=fc.ln;
}
void bt_host_event(const u8* e,int n){
    if(n<2){ return; } int code=e[0], pl=e[1]; if(pl+2>n)return; const u8* p=e+2; btconn_t* c;
    switch(code){
    case 0x03: ev_conn(p,pl); break;
    case 0x04: ev_conn_req(p,pl); break;
    case 0x05: if(pl>=4&&!p[0]&&(c=bt_conn_by_handle(be16(p+1)&0xFFF))){ bc_log("disconnected",c,p[3]); bc_free(c,p[3]); } break;
    case 0x06: if(pl>=3&&!p[0]&&(c=bt_conn_by_handle(be16(p+1)&0xFFF)))c->auth=1; break;
    case 0x08: if(pl>=4&&(c=bt_conn_by_handle(be16(p+1)&0xFFF))){ if(!p[0])c->enc=p[3]; } break;
    case 0x13: ev_nocp(p,pl); break;
    case 0x1A: klog("bluetooth: controller ACL buffer overflow"); break;
    case 0x3E: if(pl<1)break;
        if(p[0]==0x01)ev_le_conn(p,pl,0);
        else if(p[0]==0x0A)ev_le_conn(p,pl,1);
        else if(p[0]==0x03&&pl>=10&&!p[1]&&(c=bt_conn_by_handle(be16(p+2)&0xFFF))){ c->interval=be16(p+4); c->latency=be16(p+6); c->timeout=be16(p+8); }
        break;
    }
    bt_sco_event(e,n);
    bt_sec_event(e,n);
}
int bt_conn_wait(btconn_t* c,volatile u8* flag,u8 want,int ms){
    u64 t0=hr_ns(), lim=(u64)ms*1000000ULL;
    while(*flag!=want&&*flag!=0&&c->state!=BTC_FREE&&hr_ns()-t0<lim&&bt_hci_up()){ bt_poll(); if(*flag==want)break; bc_idle(); hr_delay_us(300); }
    return *flag==want;
}
static int bc_dead_wait(btconn_t* c,int ms){ u64 t0=hr_ns(), lim=(u64)ms*1000000ULL; while(c->state!=BTC_FREE&&hr_ns()-t0<lim&&bt_hci_up()){ bt_poll(); bc_idle(); hr_delay_us(300); } return c->state==BTC_FREE; }
static int bc_page(btconn_t* c,int ms){
    u8 rp[4], *a=c->addr; int r, le=c->le, atype=c->atype;
    if(le){ u8 p[25]={0x60,0x00,0x30,0x00,0,(u8)atype, a[0],a[1],a[2],a[3],a[4],a[5], 0, 0x18,0x00,0x28,0x00,0x00,0x00,0xF4,0x01,0,0,0,0};
        r=bt_hci_cmd(0x200D,p,25,rp,(int)sizeof rp,ms); }
    else { u8 p[13]={a[0],a[1],a[2],a[3],a[4],a[5],0x18,0xCC,0x01,0,0,0,0x01}; r=bt_hci_cmd(0x0405,p,13,rp,(int)sizeof rp,ms); }
    if(r<1||rp[0]){ bc_log("connect command refused",c,r<1?0xFF:rp[0]); c->state=BTC_FREE; return 0; }
    return 1;
}
static int bc_ok(const u8* a,int le){ return bt_hci_up()&&(le?bt_hci_le():bt_hci_bredr())&&a; }
static int bc_le_busy(int le){ for(int i=0;i<BT_MAX_CONN;i++) if(bc[i].state==BTC_CONNECTING&&bc[i].le&&le)return 1; return 0; }
btconn_t* bt_connect_start(const u8* a,int le,int atype){
    if(!bc_ok(a,le))return 0;
    btconn_t* c=bt_conn_by_addr(a,le); if(c)return c;
    if(bc_le_busy(le))return 0;
    c=bc_alloc(a,le,atype); if(!c){ klog("bluetooth: too many connections"); return 0; }
    return bc_page(c,500)?c:0;
}
void bt_connect_abort(btconn_t* c){
    if(!c)return;
    if(c->state==BTC_CONNECTING){ if(c->le)bt_hci_send(0x200E,0,0); else bt_hci_send(0x0408,c->addr,6); }
    else if(c->state==BTC_UP){ u8 p[3]={(u8)c->handle,(u8)(c->handle>>8),0x13}; c->state=BTC_CLOSING; bt_hci_send(0x0406,p,3); }
}
void bt_conn_drop(btconn_t* c,u8 reason){ if(c&&c->state!=BTC_FREE){ bc_log("connection dropped",c,reason); bc_free(c,reason); } }
btconn_t* bt_connect(const u8* a,int le,int atype,int ms){
    if(!bc_ok(a,le))return 0;
    if(ms<1000){ ms=1000; } if(ms>30000)ms=30000;
    btconn_t* c=bt_conn_by_addr(a,le);
    if(c&&c->state==BTC_UP)return c;
    if(c)return bt_conn_wait(c,&c->state,BTC_UP,ms)?c:0;
    if(bc_le_busy(le))return 0;
    c=bc_alloc(a,le,atype); if(!c){ klog("bluetooth: too many connections"); return 0; }
    u8 rp[4];
    if(!bc_page(c,2000))return 0;
    if(bt_conn_wait(c,&c->state,BTC_UP,ms))return c;
    if(c->state==BTC_CONNECTING){
        if(le)bt_hci_cmd(0x200E,0,0,rp,(int)sizeof rp,2000); else bt_hci_cmd(0x0408,a,6,rp,(int)sizeof rp,2000);
        bt_conn_wait(c,&c->state,BTC_UP,3000);
        if(c->state==BTC_UP){ bt_disconnect(c,0x13,2000); return 0; }
        if(c->state==BTC_CONNECTING){ bc_log("connect timed out",c,0); c->state=BTC_FREE; }
    }
    return 0;
}
int bt_disconnect(btconn_t* c,u8 reason,int ms){
    if(!c||c->state==BTC_FREE)return 1;
    if(c->state==BTC_CONNECTING){ u8 rp[4]; if(c->le)bt_hci_cmd(0x200E,0,0,rp,(int)sizeof rp,2000); else bt_hci_cmd(0x0408,c->addr,6,rp,(int)sizeof rp,2000); if(!bc_dead_wait(c,ms))c->state=BTC_FREE; return 1; }
    c->state=BTC_CLOSING;
    u8 p[3]={(u8)c->handle,(u8)(c->handle>>8),reason?reason:0x13}; u8 rp[4];
    int r=bt_hci_cmd(0x0406,p,3,rp,(int)sizeof rp,2000);
    if(r>=1&&!rp[0]&&bc_dead_wait(c,ms))return 1;
    if(c->state!=BTC_FREE)bc_free(c,0x16);
    return r>=1&&!rp[0];
}
int bt_page_scan(int on){ u8 v=on?0x02:0x00; u8 rp[4]; int r=bt_hci_cmd(0x0C1A,&v,1,rp,(int)sizeof rp,2000); return r>=1&&!rp[0]; }
int bt_acl_pending(void){ int s=0; for(int i=0;i<BT_MAX_CONN;i++)s+=bc_infl[i]; return s; }
static int bc_credit(btconn_t* c){
    int* pl=bc_pool(c); u64 t0=hr_ns();
    while(*pl<=0){ if(c->state!=BTC_UP&&c->state!=BTC_CLOSING)return 0; if(hr_ns()-t0>2000000000ULL||!bt_hci_up())return 0;
        u8 ev[264]; bt_hci_wait_evt(0x13,-1,ev,(int)sizeof ev,50); }
    return 1;
}
int bt_acl_send(btconn_t* c,u16 cid,const u8* p,int n){
    if(!c||(c->state!=BTC_UP&&c->state!=BTC_CLOSING)||n<0||n>0xFFFF-4)return -1;
    int mtu=bc_mtu(c), tot=n+4, off=0; u8 pk[1024+4];
    while(off<tot){
        int seg=tot-off; if(seg>mtu)seg=mtu;
        if(!bc_credit(c)){ bc_log("ACL send stalled, no controller credits",c,cid); return -1; }
        u16 h=(u16)(c->handle|((off?0x1:(c->le?0x0:0x2))<<12));
        pk[0]=(u8)h; pk[1]=(u8)(h>>8); pk[2]=(u8)seg; pk[3]=(u8)(seg>>8);
        for(int i=0;i<seg;i++){ int k=off+i; pk[4+i]=k==0?(u8)n:k==1?(u8)(n>>8):k==2?(u8)cid:k==3?(u8)(cid>>8):p[k-4]; }
        if(btusb_bulk_out(pk,seg+4,500)<0){ bc_log("ACL send failed",c,cid); return -1; }
        (*bc_pool(c))--; bc_infl[bc_idx(c)]++; off+=seg;
    }
    return n;
}
static void acl_pkt(const u8* d,int len,u16 hf){
    btconn_t* c=bt_conn_by_handle(hf&0xFFF); if(!c)return;
    int pb=(hf>>12)&3;
    if(pb!=1){ c->rxn=0; c->rxwant=0; }
    else if(!c->rxn)return;
    if(c->rxn+len>BT_ACL_MAX){ c->rxn=0; return; }
    for(int i=0;i<len;i++)c->rx[c->rxn+i]=d[i];
    c->rxn+=len;
    if(c->rxn<4)return;
    int l2=be16(c->rx); c->rxwant=4+l2;
    if(c->rxwant>BT_ACL_MAX){ c->rxn=0; return; }
    if(c->rxn<c->rxwant)return;
    int n=c->rxwant; c->rxn=0; c->rxwant=0;
    l2cap_rx(c,be16(c->rx+2),c->rx+4,n-4);
}
static u8 ast[2304]; static int astn;
static u8 aq[4][1100]; static int aqn[4], aqh, aqt, adepth;
static void acl_feed(const u8* p,int n){
    if(astn+n>(int)sizeof ast){ astn=0; if(n>(int)sizeof ast)return; }
    for(int i=0;i<n;i++)ast[astn+i]=p[i];
    astn+=n;
    int o=0;
    while(astn-o>=4){ int len=be16(ast+o+2); if(len>BT_ACL_MAX){ o=astn; break; } if(astn-o<4+len)break;
        acl_pkt(ast+o+4,len,be16(ast+o)); o+=4+len; }
    for(int i=o;i<astn;i++)ast[i-o]=ast[i];
    astn-=o;
}
void bt_acl_rx(const u8* p,int n){
    if(n<=0)return;
    if(adepth){ int nt=(aqt+1)&3; if(nt==aqh||n>1100)return; for(int i=0;i<n;i++)aq[aqt][i]=p[i]; aqn[aqt]=n; aqt=nt; return; }
    adepth=1;
    static u8 w[1100]; int m=n>1100?1100:n; for(int i=0;i<m;i++)w[i]=p[i];
    acl_feed(w,m);
    while(aqh!=aqt){ int k=aqh; aqh=(aqh+1)&3; for(int i=0;i<aqn[k];i++)w[i]=aq[k][i]; acl_feed(w,aqn[k]); }
    adepth=0;
}
