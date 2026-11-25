#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/bluetooth/bt.h"
#include "net/bluetooth/bt_host.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "net/bluetooth/hci_parse.h"
#pragma GCC diagnostic pop
#include "net/bluetooth/bt_mgr.h"
#include "include/nvx/core/sysfeat.h"
extern volatile int g_busy_esc;
const btlist_t* bt_scan_list(void);
#include "net/bluetooth/bt_sec.h"
#include "net/bluetooth/bt_hid.h"
__attribute__((weak)) int hid_host_connect(btconn_t* c,int ms);
__attribute__((weak)) int hogp_connect(btconn_t* c,int ms);
__attribute__((weak)) int a2dp_connect(btconn_t* c,int ms);
__attribute__((weak)) int hfp_connect(btconn_t* c,int ms);
__attribute__((weak)) int bt_keys_count(void);
__attribute__((weak)) btkey_t* bt_keys_at(int i);
__attribute__((weak)) btkey_t* bt_keys_find(const u8* addr,int le);
__attribute__((weak)) int bt_keys_forget(const u8* addr,int le);
__attribute__((weak)) btkey_t* bt_keys_find_le(const u8* addr,int atype);
__attribute__((weak)) void bt_sec_pairing(const u8* addr,int on);
__attribute__((weak)) int bt_sec_auth_start(btconn_t* c);
__attribute__((weak)) int bt_sec_auth_poll(btconn_t* c);
__attribute__((weak)) void bt_sec_auth_abort(btconn_t* c);
__attribute__((weak)) int bt_smp_start(btconn_t* c);
__attribute__((weak)) int bt_smp_poll(btconn_t* c);
__attribute__((weak)) void bt_smp_abort(btconn_t* c);
__attribute__((weak)) void bt_sec_tick(void);
static int bt_keys_have(const u8* a,int le){ return bt_keys_find&&bt_keys_find(a,le)?1:0; }
static btmdev_t btm_d[BTM_MAX];
static int btm_n, btm_started;
static char btm_msg[96];
static void btm_say(const char* a,const char* b){ char* p=btm_msg; char* e=btm_msg+sizeof btm_msg-1; while(*a&&p<e)*p++=*a++; if(b)while(*b&&p<e)*p++=*b++; *p=0; klog2("bluetooth: ",btm_msg); }
const char* btm_last(void){ return btm_msg; }
static int btm_eq(const u8* a,const u8* b){ for(int i=0;i<6;i++) if(a[i]!=b[i])return 0; return 1; }
static btmdev_t* btm_find(const u8* a,int le){ for(int i=0;i<btm_n;i++) if(btm_eq(btm_d[i].addr,a)&&btm_d[i].le==(u8)le)return &btm_d[i]; return 0; }
static btmdev_t* btm_add(const u8* a,int le,int atype){ btmdev_t* d=btm_find(a,le); if(d)return d;
    if(btm_n>=BTM_MAX){ int k=-1; for(int i=0;i<btm_n;i++) if(!btm_d[i].bonded&&!bt_conn_by_addr(btm_d[i].addr,btm_d[i].le)){ k=i; break; } if(k<0)return 0; btm_d[k]=btm_d[--btm_n]; }
    d=&btm_d[btm_n++]; u8* z=(u8*)d; for(int i=0;i<(int)sizeof *d;i++)z[i]=0; for(int i=0;i<6;i++)d->addr[i]=a[i]; d->le=(u8)le; d->atype=(u8)atype; return d; }
static int btm_hex(char c){ return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; }
int btm_parse_addr(const char* s,u8* a){ for(int i=5;i>=0;i--){ int h=btm_hex(s[0]), l=h<0?-1:btm_hex(s[1]); if(l<0)return 0; a[i]=(u8)(h<<4|l); s+=2; if(i){ if(*s!=':'&&*s!='-')return 0; s++; } } return *s==0||*s==' '; }
void btm_sync(void){
    const btlist_t* L=bt_scan_list();
    for(int i=0;L&&i<L->n;i++){ const btdev_t* s=&L->d[i]; btmdev_t* d=btm_add(s->addr,s->le,s->atype); if(!d)continue;
        d->seen=1; d->rssi=s->rssi; if(s->cod)d->cod=s->cod; if(s->appear)d->appear=s->appear; if(s->name[0]){ int k=0; for(;s->name[k]&&k<BTD_NAME-1;k++)d->name[k]=s->name[k]; d->name[k]=0; } }
    if(bt_keys_count&&bt_keys_at){ int n=bt_keys_count(); for(int i=0;i<n;i++){ btkey_t* k=bt_keys_at(i); if(!k)continue; btmdev_t* d=btm_add(k->addr,k->le,k->atype); if(d)d->bonded=1; } }
    for(int i=0;i<btm_n;i++){ btmdev_t* d=&btm_d[i]; btconn_t* c=bt_conn_by_addr(d->addr,d->le); d->conn=c&&c->state==BTC_UP; if(bt_keys_find)d->bonded=bt_keys_have(d->addr,d->le); }
}
const char* btm_kind(const btmdev_t* d){
    if(d->le){ u16 a=d->appear; if(a==0x03C1)return "keyboard"; if(a==0x03C2)return "mouse"; if(a==0x03C3)return "joystick"; if(a==0x03C4)return "gamepad"; if((a>>6)==0x0F)return "input"; if((a>>6)==0x25)return "headphones"; if((a>>6)==0x01)return "phone"; if((a>>6)==0x02)return "computer"; if((a>>6)==0x03)return "watch"; return ""; }
    if(((d->cod>>8)&0x1F)==5){ u32 m=(d->cod>>6)&3; return m==1?"keyboard":m==2?"mouse":m==3?"keyboard+mouse":"input"; }
    if(((d->cod>>8)&0x1F)==4)return "audio";
    return bth_major(d->cod);
}
int btm_count(void){ return btm_n; }
const btmdev_t* btm_get(int i){ return i>=0&&i<btm_n?&btm_d[i]:0; }
static int btm_up(void){
    if(!bt_up()||!bt_hci_up()){ btm_say("adapter not ready - see: bt status",0); return 0; }
    if(!btm_started){ btm_started=1; bt_page_scan(1); btm_sync(); }
    return 1;
}
static btmdev_t* btm_pick(const char* s){
    btm_sync();
    if(s[0]>='0'&&s[0]<='9'&&(!s[1]||s[1]==' '||(s[1]>='0'&&s[1]<='9'&&(!s[2]||s[2]==' ')))){ int i=s[0]-'0'; if(s[1]>='0'&&s[1]<='9')i=i*10+s[1]-'0'; if(i>=1&&i<=btm_n)return &btm_d[i-1]; btm_say("no device with that number - see: bt devices",0); return 0; }
    u8 a[6]; if(!btm_parse_addr(s,a)){ btm_say("address must look like 12:34:56:78:9A:BC (or a number from bt devices)",0); return 0; }
    int le=0; const char* t=s+17; while(*t==' ')t++; if((t[0]=='l'||t[0]=='L')&&(t[1]=='e'||t[1]=='E'))le=1;
    btmdev_t* d=btm_find(a,le); if(!d&&!le&&!btm_find(a,0))d=btm_find(a,1);
    if(!d)d=btm_add(a,le,0);
    if(!d)btm_say("device list full",0);
    return d;
}
static int btm_pms=15000;
static int btm_profile(btmdev_t* d,btconn_t* c){
    int r=-1, ms=btm_pms;
    if(!d->le&&((d->cod>>8)&0x1F)==4&&a2dp_connect){ r=a2dp_connect(c,ms<12000?ms:12000); if(r>0){ btm_say("audio connected: ",d->name[0]?d->name:"headphones"); if(hfp_connect&&hfp_connect(c,3000)>0)btm_say("headset controls connected: ",d->name[0]?d->name:"headphones"); return 1; } if(!r)return 0; }
    if(d->le){ if(hogp_connect)r=hogp_connect(c,ms); }
    else if(hid_host_connect)r=hid_host_connect(c,ms);
    if(r>0){ btm_say("connected: ",d->name[0]?d->name:"device"); return 1; }
    if(r<0&&!(d->le?hogp_connect!=0:hid_host_connect!=0)){ btm_say("connected (no input profile in this build)",0); return 1; }
    btm_say("connected, but the device did not offer keyboard/mouse input",0); return 0;
}
static int btm_link(btmdev_t* d,int fresh){
    if(fresh&&d->bonded&&bt_keys_forget){ bt_keys_forget(d->addr,d->le); d->bonded=0; }
    btconn_t* c=bt_conn_by_addr(d->addr,d->le);
    if(!c||c->state!=BTC_UP){ c=bt_connect(d->addr,d->le,d->atype,d->le?8000:12000);
        if(!c||c->state!=BTC_UP){ btm_say(d->le?"the device did not answer (is it advertising / in pairing mode?)":"the device did not answer (is it on and in pairing mode?)",0); return 0; } }
    if(d->cod)c->cod=d->cod;
    int s=d->le?bt_smp_pair(c,30000):bt_sec_auth(c,30000);
    if(s<=0){ btm_say(fresh?"pairing failed or was cancelled":"could not secure the link - try: bt pair",0); bt_disconnect(c,0x05,3000); return 0; }
    if(d->le&&d->appear==0x03C1&&bt_keys_find_le){ btkey_t* k=bt_keys_find_le(d->addr,d->atype); if(!k||!(k->flags&BTKF_AUTH)){ btm_say("keyboard paired without a passkey - refused",0); if(k&&bt_keys_forget)bt_keys_forget(d->addr,1); bt_disconnect(c,0x05,3000); return 0; } }
    d->conn=1; d->bonded=bt_keys_find?bt_keys_have(d->addr,d->le):1;
    return btm_profile(d,c)?1:0;
}
int btm_pair(const char* s){ if(!btm_up())return -1; btmdev_t* d=btm_pick(s); if(!d)return -1; btm_say("pairing...",0); if(bt_sec_pairing)bt_sec_pairing(d->addr,1); int r=btm_link(d,1); if(bt_sec_pairing)bt_sec_pairing(0,0); btm_sync(); return r>0?0:-1; }
int btm_connect(const char* s){ if(!btm_up())return -1; btmdev_t* d=btm_pick(s); if(!d)return -1; btm_say("connecting...",0); int r=btm_link(d,0); btm_sync(); return r>0?0:-1; }
int btm_disconnect(const char* s){
    int n=0;
    if(!s||!s[0]){ for(int i=0;i<BT_MAX_CONN;i++){ btconn_t* c=bt_conn_at(i); if(c&&c->state==BTC_UP){ bt_disconnect(c,0x13,3000); n++; } } }
    else { btmdev_t* d=btm_pick(s); if(!d)return -1; btconn_t* c=bt_conn_by_addr(d->addr,d->le); if(c&&c->state!=BTC_FREE){ bt_disconnect(c,0x13,3000); n=1; } }
    btm_sync(); btm_say(n?"disconnected":"nothing was connected",0); return n;
}
int btm_forget(const char* s){
    btmdev_t* d=btm_pick(s); if(!d)return -1;
    btconn_t* c=bt_conn_by_addr(d->addr,d->le); if(c&&c->state!=BTC_FREE)bt_disconnect(c,0x13,3000);
    int r=bt_keys_forget?bt_keys_forget(d->addr,d->le):0; d->bonded=0; d->conn=0;
    btm_say(r>=0?"forgotten: ":"could not remove the saved key for ",d->name[0]?d->name:"device"); return r>=0?0:-1;
}
int bt_mgr_accept(const u8* addr,u32 cod,int le){
    btmdev_t* d=btm_find(addr,le); if(!d)d=btm_add(addr,le,0);
    if(d&&cod)d->cod=cod;
    int ok=bt_keys_have(addr,le)||(d&&d->bonded);
    return ok;
}
int btm_autoconnect(void){
    if(!btm_up())return 0;
    btm_sync();
    int tried=0, ok=0; g_busy_esc=0;
    for(int i=0;i<btm_n&&tried<4&&!g_busy_esc;i++){ btmdev_t* d=&btm_d[i]; if(!d->bonded||d->conn)continue; tried++;
        btconn_t* c=bt_connect(d->addr,d->le,d->atype,5000); if(!c||c->state!=BTC_UP)continue;
        int s=d->le?bt_smp_pair(c,10000):bt_sec_auth(c,10000); if(s<=0){ bt_disconnect(c,0x05,2000); continue; }
        d->conn=1; if(btm_profile(d,c))ok++; }
    if(tried){ char b[64],*p=b; p=udec(p,(u32)ok); p=scopy(p," of "); p=udec(p,(u32)tried); p=scopy(p," saved devices reconnected"); *p=0; btm_say(b,0); }
    return ok;
}
static struct featguard btm_fg={"Bluetooth reconnect at boot","feat-btreconn.cfg",1,0,3,0,0};
static char btm_bwhy[64];
int btm_boot_enabled(void){ fg_boot(&btm_fg); return btm_fg.on&&!btm_fg.dead; }
void btm_boot_enable(int on){ fg_enable(&btm_fg,on); btm_bwhy[0]=0; }
const char* btm_boot_why(void){ fg_boot(&btm_fg); if(!btm_fg.on)return "Turned off"; if(btm_fg.dead)return btm_bwhy[0]?btm_bwhy:"Turned itself off after the last boot did not finish"; return btm_bwhy; }
enum { RB_WAIT, RB_NEXT, RB_PAGE, RB_SEC, RB_DROP, RB_DONE };
static struct { u8 st, i, tried, ok, le; u8 addr[6]; btconn_t* c; u64 t0; } rb;
static u64 btm_now(void){ extern u64 hr_ns(void); return hr_ns(); }
static int rb_alive(void){ return rb.c&&rb.c->state!=BTC_FREE&&bt_conn_by_addr(rb.addr,rb.le)==rb.c; }
static void rb_drop(void){ if(rb_alive()){ bt_connect_abort(rb.c); rb.st=RB_DROP; rb.t0=btm_now(); } else rb.st=RB_NEXT; }
static int rb_finish(void){
    rb.st=RB_DONE; fg_ok(&btm_fg);
    { char* q=btm_bwhy; q=udec(q,(u32)rb.ok); q=scopy(q," of "); q=udec(q,(u32)rb.tried); scopy(q," saved devices reconnected at startup"); }
    if(rb.tried)btm_say(btm_bwhy,0);
    return rb.ok; }
static int rb_sec_poll(void){ btconn_t* c=rb.c; if(rb.le)return bt_smp_poll?bt_smp_poll(c):1; return bt_sec_auth_poll?bt_sec_auth_poll(c):1; }
static void rb_sec_abort(void){ if(rb.le){ if(bt_smp_abort)bt_smp_abort(rb.c); } else if(bt_sec_auth_abort)bt_sec_auth_abort(rb.c); }
static void rb_secure(void){
    btconn_t* c=rb.c; btmdev_t* d=btm_find(rb.addr,rb.le); if(d&&d->cod)c->cod=d->cod;
    int r=rb.le?(bt_smp_start?bt_smp_start(c):1):(bt_sec_auth_start?bt_sec_auth_start(c):1);
    if(r<0){ rb_drop(); return; }
    rb.st=RB_SEC; rb.t0=btm_now(); }
static void rb_profile(void){
    btmdev_t* d=btm_find(rb.addr,rb.le); if(!d){ rb.st=RB_NEXT; return; }
    d->conn=1; btm_pms=BTM_RB_PROFILE_MS; if(btm_profile(d,rb.c))rb.ok++; btm_pms=15000; rb.st=RB_NEXT; }
int btm_boot_busy(void){ return rb.st>RB_WAIT&&rb.st<RB_DONE; }
int btm_boot_tick(void){
    extern volatile u64 g_ticks; u16 v=0,p=0; u64 now;
    if(bt_sec_tick)bt_sec_tick();
    switch(rb.st){
    case RB_WAIT:
        if(g_ticks<BTM_BOOT_TICKS)return 0;
        rb.st=RB_DONE;
        if(!bt_keys_count||!bt_keys_count()||!btusb_listed(&v,&p)||!fg_begin(&btm_fg))return 0;
        if(!bt_up()){ scopy(btm_bwhy,"The adapter did not start"); fg_fail(&btm_fg,btm_bwhy); return -1; }
        if(!btm_up()){ scopy(btm_bwhy,"The adapter did not start"); fg_fail(&btm_fg,btm_bwhy); return -1; }
        btm_sync(); rb.i=0; rb.tried=0; rb.ok=0; rb.c=0; rb.st=RB_NEXT; return 0;
    case RB_NEXT:
        for(;rb.i<btm_n&&rb.tried<BTM_RB_MAX;rb.i++){ btmdev_t* d=&btm_d[rb.i]; btconn_t* o=bt_conn_by_addr(d->addr,d->le);
            if(!d->bonded||d->conn||(o&&o->state==BTC_UP))continue;
            if(d->le?!bt_hci_le():!bt_hci_bredr())continue;
            rb.tried++; rb.i++; for(int k=0;k<6;k++)rb.addr[k]=d->addr[k]; rb.le=d->le;
            rb.c=bt_connect_start(d->addr,d->le,d->atype); if(!rb.c)return 0;
            if(rb.c->state==BTC_UP)rb_secure(); else { rb.st=RB_PAGE; rb.t0=btm_now(); }
            return 0; }
        return rb_finish();
    case RB_PAGE:
        if(!rb_alive()){ rb.st=RB_NEXT; return 0; }
        if(rb.c->state==BTC_UP){ rb_secure(); return 0; }
        if(btm_now()-rb.t0>(u64)(rb.le?BTM_RB_LE_MS:BTM_RB_PAGE_MS)*1000000ULL){ bt_connect_abort(rb.c); rb.st=RB_DROP; rb.t0=btm_now(); }
        return 0;
    case RB_SEC: {
        if(!rb_alive()){ rb.st=RB_NEXT; return 0; }
        int r=rb_sec_poll();
        if(r>0){ rb_profile(); return 0; }
        if(r<0||btm_now()-rb.t0>(u64)BTM_RB_SEC_MS*1000000ULL){ if(!r)rb_sec_abort(); rb_drop(); }
        return 0; }
    case RB_DROP:
        now=btm_now();
        if(!rb_alive()){ rb.st=RB_NEXT; return 0; }
        if(rb.c->state==BTC_UP)bt_connect_abort(rb.c);
        if(now-rb.t0>(u64)BTM_RB_DROP_MS*1000000ULL){ bt_conn_drop(rb.c,0x16); rb.st=RB_NEXT; }
        return 0;
    }
    return 0; }
static char* btm_put(char* p,char* e,const char* s){ while(*s&&p<e)*p++=*s++; return p; }
int btm_devices_text(char* out,int cap){
    char* p=out; char* e=out+cap-1; char t[160],*q;
    btm_sync();
    if(!btm_n)p=btm_put(p,e,"no devices yet - try: bt scan\n");
    for(int i=0;i<btm_n;i++){ btmdev_t* d=&btm_d[i];
        q=t; if(i<9)*q++=' '; q=udec(q,(u32)(i+1)); q=scopy(q,"  "); q=bth_addr(q,d->addr); q=scopy(q,d->le?"  LE     ":"  BR/EDR ");
        q=scopy(q,d->conn?"connected ":d->bonded?"paired    ":"          ");
        q=scopy(q,d->name[0]?d->name:"(no name)"); { const char* k=btm_kind(d); if(k[0]){ q=scopy(q,"  ["); q=scopy(q,k); *q++=']'; } }
        *q++='\n'; *q=0; p=btm_put(p,e,t); }
    *p=0; return btm_n;
}
