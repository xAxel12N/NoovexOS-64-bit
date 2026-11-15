#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/drivers/bluetooth/bt.h"
#include "drivers/bluetooth/hci_parse.h"
#include "drivers/bluetooth/rtlbt_fw.h"
#include "drivers/bluetooth/intelbt_fw.h"
#include "drivers/bluetooth/bcmbt_fw.h"
void ui_pump(void);
extern volatile u32 g_ml_beat;
extern volatile int g_busy_esc;
extern volatile unsigned char kdown[128];
static int bt_esc(void){ if(kdown[0x01])g_busy_esc=1; return g_busy_esc; }
#include "include/nvx/net/wifi/wifi_mgr.h"
#include "include/nvx/drivers/net/wifi/rtw8821ce.h"
u32 rtw_find_mmio(void);
static struct { int up, fw, le, bredr, coex, inq_done, hwerr; u8 hver, lmpver; u16 hrev, lmpsub, manuf; u8 addr[6]; u32 scans; char msg[96], fwmsg[96]; } bt;
static btlist_t bt_list;
static u8 bt_ev[264];
static u64 bt_pump_t;
static void bt_copy(char* d,const char* s,int cap){ int i=0; for(;s[i]&&i<cap-1;i++)d[i]=s[i]; d[i]=0; }
static void bt_idle(void){ u64 t=hr_ns(); if(t-bt_pump_t>20000000ULL){ bt_pump_t=t; g_ml_beat++; ui_pump(); } }
static void bt_event(const u8* e,int n){
    int c=hci_evt_parse(&bt_list,e,n);
    if(c==0x01)bt.inq_done=1;
    else if(c==0x10&&!bt.hwerr){ bt.hwerr=1; char b[64],*p=b; p=scopy(p,"bluetooth: controller hardware error 0x"); p=hx(p,n>2?e[2]:0,2); *p=0; klog(b); }
}
static int bt_send(u16 op,const u8* p,int n){
    u8 pk[260]; if(n<0||n>255)return -1;
    pk[0]=(u8)op; pk[1]=(u8)(op>>8); pk[2]=(u8)n; for(int i=0;i<n;i++)pk[3+i]=p[i];
    return btusb_cmd(pk,n+3);
}
static int bt_wait(u16 op,u8* rp,int rcap,int ms){
    u64 t0=hr_ns(), lim=(u64)ms*1000000ULL;
    while(hr_ns()-t0<lim){
        int n=btusb_evt(bt_ev,(int)sizeof bt_ev);
        if(n<=0){ if(!btusb_ok())return -1; bt_idle(); hr_delay_us(200); continue; }
        int m=hci_cc_match(bt_ev,n,op);
        if(m==1){ int rl=bt_ev[1]-3; if(rl>n-5)rl=n-5; if(rl<0)rl=0; if(rl>rcap)rl=rcap; for(int i=0;i<rl;i++)rp[i]=bt_ev[5+i]; return rl; }
        if(m==2){ if(rcap>0)rp[0]=bt_ev[2]; return 1; }
        bt_event(bt_ev,n);
    }
    return -1;
}
static int bt_cmd_t(u16 op,const u8* p,int n,u8* rp,int rcap,int ms){ if(bt_send(op,p,n)<0)return -1; return bt_wait(op,rp,rcap,ms); }
static int bt_cmd(u16 op,const u8* p,int n,u8* rp,int rcap){ return bt_cmd_t(op,p,n,rp,rcap,2000); }
static int bt_ok(u16 op,const u8* p,int n){ u8 rp[8]; int r=bt_cmd(op,p,n,rp,(int)sizeof rp); return r>=1&&rp[0]==0; }
static int bt_wait_code(int code,int ms){
    u64 t0=hr_ns(), lim=(u64)ms*1000000ULL;
    while(hr_ns()-t0<lim){
        int n=btusb_evt(bt_ev,(int)sizeof bt_ev);
        if(n<=0){ if(!btusb_ok())return 0; bt_idle(); hr_delay_us(200); continue; }
        bt_event(bt_ev,n); if(bt_ev[0]==code)return 1;
    }
    return 0;
}
static void bt_fail(const char* m){ bt_copy(bt.msg,m,(int)sizeof bt.msg); char b[128],*p=b; p=scopy(p,"bluetooth: "); p=scopy(p,m); *p=0; klog(b); }
static int bt_read_version(void){
    u8 rp[16]; int n=bt_cmd(0x1001,0,0,rp,(int)sizeof rp); if(n<9||rp[0])return 0;
    bt.hver=rp[1]; bt.hrev=bth_g16(rp+2); bt.lmpver=rp[4]; bt.manuf=bth_g16(rp+5); bt.lmpsub=bth_g16(rp+7); return 1;
}
int bt_up(void){
    if(bt.up&&btusb_ok())return 1;
    bt.up=0; bt.hwerr=0;
    if(!btusb_ok()){ bt.fw=0; if(!btusb_probe()){ u16 v,p; bt_fail(btusb_listed(&v,&p)?"adapter found but USB setup failed (see log)":"no Bluetooth adapter found"); return 0; } }
    for(int i=0;i<64&&btusb_evt(bt_ev,(int)sizeof bt_ev)>0;i++){}
    { u16 iv=0,ip=0; btusb_ids(&iv,&ip); if(!bt.fw&&intelbt_match(iv,ip)){ int r=intelbt_setup(bt.fwmsg,(int)sizeof bt.fwmsg); if(r<0){ bt_fail("Intel firmware load failed"); return 0; } bt.fw=r?r:3; } }
    if(!bt_read_version()){ bt_cmd_t(0x0C03,0,0,bt_ev,8,5000); if(!bt_read_version()){ bt_fail("controller does not answer HCI commands"); return 0; } }
    u16 vid=0,pid=0; btusb_ids(&vid,&pid);
    if(!bt.fw&&(bt.manuf==0x005D||vid==0x0BDA)){
        char m[96]; m[0]=0; int r=rtlbt_setup(bt_cmd,m,(int)sizeof m); bt_copy(bt.fwmsg,m,(int)sizeof bt.fwmsg);
        { char b[128],*p=b; p=scopy(p,"bluetooth: "); p=scopy(p,m); *p=0; klog(b); }
        if(r<0){ bt_fail("Realtek firmware load failed"); return 0; }
        bt.fw=r?r:3;
    }
    else if(!bt.fw&&bcmbt_match(vid,bt.manuf)){ char m[96]; int r=bcmbt_setup(bt_cmd,vid,pid,m,(int)sizeof m); bt_copy(bt.fwmsg,m,(int)sizeof bt.fwmsg); klog2("bluetooth: ",m); bt.fw=r?r:3; }
    { u8 rp[8]; int n=bt_cmd_t(0x0C03,0,0,rp,(int)sizeof rp,5000); if(n<1||rp[0]){ bt_fail("HCI Reset failed"); return 0; } }
    if(!bt_read_version()){ bt_fail("Read Local Version failed after reset"); return 0; }
    { u8 rp[16]; int n=bt_cmd(0x1009,0,0,rp,(int)sizeof rp); if(n>=7&&!rp[0]) for(int i=0;i<6;i++)bt.addr[i]=rp[1+i]; }
    bt.le=0; bt.bredr=1;
    { u8 rp[16]; int n=bt_cmd(0x1003,0,0,rp,(int)sizeof rp); if(n>=9&&!rp[0]){ bt.le=(rp[1+4]>>6)&1; bt.bredr=!((rp[1+4]>>5)&1); } }
    { static const u8 em[8]={0xFF,0xFF,0xFB,0xFF,0x02,0x40,0x00,0x60}; if(!bt_ok(0x0C01,em,8))klog("bluetooth: Set Event Mask failed"); }
    if(bt.bredr){ u8 m2=2, m1=1; if(!bt_ok(0x0C45,&m2,1))bt_ok(0x0C45,&m1,1); u8 pt[2]={0x00,0x10}; bt_ok(0x0C18,pt,2); }
    if(bt.le){ u8 hs[2]={1,0}; bt_ok(0x0C6D,hs,2); static const u8 lm[8]={0x1F,0,0,0,0,0,0,0}; bt_ok(0x2001,lm,8); }
    bt.up=1; bt_copy(bt.msg,"ready",(int)sizeof bt.msg);
    { char b[128],*p=b; p=scopy(p,"bluetooth: ready, address "); p=bth_addr(p,bt.addr); p=scopy(p,bt.le?", LE":""); p=scopy(p,bt.bredr?", BR/EDR":""); *p=0; klog(b); }
    return 1;
}
static u32 bt_coex_m;
static void bt_coex(int on){
    if(on){ bt_coex_m=0; bt.coex=0; if(!g_rtw_coex_done)return; u32 m=rtw_find_mmio(); if(!m)return;
        bt_coex_m=m; bt.coex=rtw_coex_bt_share(m,1,g_wm_state==WM_CONNECTED)?1:2; klog(bt.coex==1?"bluetooth: Wi-Fi antenna shared with Bluetooth for the scan":"bluetooth: Wi-Fi coex register did not answer"); }
    else if(bt_coex_m){ int ok=rtw_coex_bt_share(bt_coex_m,0,0); bt_coex_m=0; klog(ok?"bluetooth: antenna back to Wi-Fi only":"bluetooth: coex restore failed, Wi-Fi-only setup re-applied"); }
}
int bt_scan(int secs){
    if(secs<2)secs=2;
    if(secs>15)secs=15;
    if(!bt_up())return -1;
    bt_list.n=0; bt_list.drops=0;
    bt_coex(1);
    int inq=0, le=0;
    if(bt.bredr){ u8 p[5]={0x33,0x8B,0x9E,(u8)((secs*100+127)/128),0}; u8 rp[4]; int n=bt_cmd(0x0401,p,5,rp,(int)sizeof rp); inq=(n>=1&&rp[0]==0); if(!inq)klog("bluetooth: Inquiry was refused"); }
    if(bt.le){ u8 sp[7]={1,0x60,0x00,0x30,0x00,0,0}; u8 en[2]={1,0}; le=bt_ok(0x200B,sp,7)&&bt_ok(0x200C,en,2); if(!le)klog("bluetooth: LE scan was refused"); }
    if(!inq&&!le){ bt_coex(0); bt_fail("the controller refused both scan types"); return -1; }
    bt.inq_done=0; g_busy_esc=0;
    u64 t0=hr_ns(), want=(u64)secs*1000000000ULL, lim=want+4000000000ULL;
    while(btusb_ok()){
        u64 t=hr_ns()-t0; if(t>=lim||bt_esc())break; if(t>=want&&(!inq||bt.inq_done))break;
        int n=btusb_evt(bt_ev,(int)sizeof bt_ev);
        if(n>0)bt_event(bt_ev,n); else { bt_idle(); hr_delay_us(500); }
    }
    if(le){ u8 dis[2]={0,0}; bt_ok(0x200C,dis,2); }
    if(inq&&!bt.inq_done)bt_ok(0x0402,0,0);
    int asked=0;
    for(int i=0;i<bt_list.n&&asked<4&&btusb_ok()&&!bt_esc();i++){ btdev_t* d=&bt_list.d[i]; if(d->le||d->nameq)continue;
        u8 p[10]; for(int k=0;k<6;k++)p[k]=d->addr[k]; p[6]=d->psrm; p[7]=0; p[8]=(u8)d->clkoff; p[9]=(u8)((d->clkoff>>8)|0x80);
        u8 rp[4]; int n=bt_cmd(0x0419,p,10,rp,(int)sizeof rp); asked++;
        if(n>=1&&rp[0]==0)bt_wait_code(0x07,4000); }
    bt_coex(0);
    bt.scans++;
    { char b[80],*p=b; p=scopy(p,"bluetooth: scan found "); p=udec(p,(u32)bt_list.n); p=scopy(p," devices"); *p=0; klog(b); }
    return bt_list.n;
}
static char* bt_put(char* p,char* e,const char* s){ while(*s&&p<e)*p++=*s++; return p; }
int bt_status_text(char* out,int cap){
    char* p=out; char* e=out+cap-1; char t[64],*q; u16 vid=0,pid=0;
    if(btusb_ok()){ btusb_ids(&vid,&pid); q=t; q=scopy(q,"adapter: USB "); q=hx(q,vid,4); *q++=':'; q=hx(q,pid,4); q=scopy(q," (claimed)\n"); *q=0; p=bt_put(p,e,t); }
    else if(btusb_listed(&vid,&pid)){ q=t; q=scopy(q,"adapter: USB "); q=hx(q,vid,4); *q++=':'; q=hx(q,pid,4); q=scopy(q," (not started - try: bt up)\n"); *q=0; p=bt_put(p,e,t); }
    else p=bt_put(p,e,"adapter: none found\n");
    if(bt.up){
        q=t; q=scopy(q,"address: "); q=bth_addr(q,bt.addr); q=scopy(q,bt.le?"  LE":""); q=scopy(q,bt.bredr?"  BR/EDR":""); *q++='\n'; *q=0; p=bt_put(p,e,t);
        q=t; q=scopy(q,"HCI version "); q=udec(q,bt.hver); q=scopy(q,"  revision 0x"); q=hx(q,bt.hrev,4); q=scopy(q,"  LMP subversion 0x"); q=hx(q,bt.lmpsub,4); q=scopy(q,"  maker 0x"); q=hx(q,bt.manuf,4); *q++='\n'; *q=0; p=bt_put(p,e,t);
        if(bt.lmpsub==0x8821&&bt.hrev==0x000c)p=bt_put(p,e,"chip: RTL8821C, firmware NOT loaded\n");
        else if(bt.lmpsub==0x8822&&bt.hrev==0x000c)p=bt_put(p,e,"chip: RTL8822C, firmware NOT loaded\n");
    }
    if(bt.fwmsg[0]){ p=bt_put(p,e,bt.fwmsg); p=bt_put(p,e,"\n"); }
    if(bt.msg[0]){ p=bt_put(p,e,"state: "); p=bt_put(p,e,bt.msg); p=bt_put(p,e,"\n"); }
    if(btusb_ok()){ u32 a,b2,c,d; btusb_stats(&a,&b2,&c,&d); q=t; q=scopy(q,"commands "); q=udec(q,d); q=scopy(q,"  events "); q=udec(q,a); q=scopy(q,"  dropped "); q=udec(q,b2); q=scopy(q,"  errors "); q=udec(q,c); *q++='\n'; *q=0; p=bt_put(p,e,t); }
    if(g_rtw_coex_done)p=bt_put(p,e,bt.coex==2?"Wi-Fi antenna: coex register did not answer during the last scan\n":"Wi-Fi antenna: Wi-Fi only; bt scan shares it with Bluetooth while the scan runs\n");
    if(bt.scans){ q=t; q=scopy(q,"last scan: "); q=udec(q,(u32)bt_list.n); q=scopy(q," devices\n"); *q=0; p=bt_put(p,e,t); }
    *p=0; return (int)(p-out);
}
int bt_list_text(char* out,int cap){
    char* p=out; char* e=out+cap-1; char t[128],*q;
    int ord[BTD_MAX]; int n=bt_list.n;
    for(int i=0;i<n;i++)ord[i]=i;
    for(int i=1;i<n;i++){ int k=ord[i], j=i; while(j>0&&bt_list.d[ord[j-1]].rssi<bt_list.d[k].rssi){ ord[j]=ord[j-1]; j--; } ord[j]=k; }
    if(!n)p=bt_put(p,e,bt.scans?"no devices found (is the other device visible / in pairing mode?)\n":"no scan yet - try: bt scan\n");
    for(int i=0;i<n;i++){ btdev_t* d=&bt_list.d[ord[i]];
        q=t; q=scopy(q,"  "); q=bth_addr(q,d->addr); q=scopy(q,d->le?"  LE     ":"  BR/EDR ");
        if(d->rssi==127)q=scopy(q,"   ?   "); else { int r=d->rssi; if(r<0){ *q++='-'; r=-r; } else *q++=' '; if(r<100)*q++=' '; if(r<10)*q++=' '; q=udec(q,(u32)r); q=scopy(q," dBm"); }
        q=scopy(q,"  "); q=scopy(q,d->name[0]?d->name:"(no name)");
        if(!d->le&&bth_major(d->cod)[0]){ q=scopy(q,"  ["); q=scopy(q,bth_major(d->cod)); *q++=']'; }
        *q++='\n'; *q=0; p=bt_put(p,e,t); }
    if(bt_list.drops){ q=t; q=scopy(q,"  (+"); q=udec(q,bt_list.drops); q=scopy(q," more not shown)\n"); *q=0; p=bt_put(p,e,t); }
    *p=0; return n;
}
