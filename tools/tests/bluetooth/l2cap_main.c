#include <stdio.h>
#include <string.h>
#include "net/bluetooth/conn.c"
#include "net/bluetooth/l2cap.c"
static int fails, checks;
#define CHECK(c,m) do{ checks++; if(c)printf("  %s ok\n",m); else { printf("  FAIL %s\n",m); fails++; } }while(0)
volatile u32 g_ml_beat; static u64 now;
void ui_pump(void){}
u64 hr_ns(void){ return now+=100000; }
void hr_delay_us(u32 us){ now+=(u64)us*1000; }
void klog(const char* s){ printf("    log: %s\n",s); }
char* scopy(char* p,const char* s){ while(*s)*p++=*s++; *p=0; return p; }
char* udec(char* p,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*p++=t[--n]; *p=0; return p; }
char* hx(char* p,u32 v,int d){ for(int i=d-1;i>=0;i--)*p++="0123456789ABCDEF"[(v>>(i*4))&15]; *p=0; return p; }
static u8 cmds[64][40]; static int ncmd; static u8 out[64][1100]; static int outn[64], nout;
static u8 evq[16][64]; static int evn[16], evh, evt;
static void ev(const u8* e,int n){ memcpy(evq[evt],e,n); evn[evt]=n; evt=(evt+1)&15; }
int bt_hci_up(void){ return 1; } int bt_hci_le(void){ return 1; } int bt_hci_bredr(void){ return 1; }
int bt_hci_send(u16 op,const u8* p,int n){ if(n>0)memcpy(cmds[ncmd]+2,p,n>38?38:n); cmds[ncmd][0]=(u8)op; cmds[ncmd][1]=(u8)(op>>8); ncmd++; return 0; }
int bt_hci_cmd(u16 op,const u8* p,int n,u8* rp,int rcap,int ms){ (void)rcap; (void)ms; bt_hci_send(op,p,n);
    if(op==0x1005){ u8 r[8]={0,27,0,0,3,0,0,0}; memcpy(rp,r,8); return 8; }
    if(op==0x2002){ u8 r[4]={0,0,0,0}; memcpy(rp,r,4); return 4; }
    if(op==0x0405){ rp[0]=0; u8 e[13]={0x03,11,0,0x42,0x00,1,2,3,4,5,6,1,0}; ev(e,13); return 1; }
    if(op==0x0406){ rp[0]=0; u8 e[6]={0x05,4,0,(u8)p[0],(u8)p[1],0x16}; ev(e,6); return 1; }
    rp[0]=0; return 1; }
int bt_hci_wait_evt(int code,int sub,u8* o,int cap,int ms){ (void)code; (void)sub; (void)o; (void)cap; (void)ms; bt_poll(); return -1; }
static void l2rx(u16 h,u16 cid,const u8* d,int n){ u8 b[200]; b[0]=(u8)h; b[1]=(u8)((h>>8)|0x20); b[2]=(u8)(n+4); b[3]=0; b[4]=(u8)n; b[5]=0; b[6]=(u8)cid; b[7]=(u8)(cid>>8); memcpy(b+8,d,n); bt_acl_rx(b,n+8); }
static void (*peer)(const u8*,int); static int autocred=1;
void bt_poll(void){ while(evh!=evt){ int k=evh; evh=(evh+1)&15; bt_host_event(evq[k],evn[k]); } }
int btusb_bulk_out(const u8* p,int n,int ms){ (void)ms; memcpy(out[nout],p,n); outn[nout]=n; nout++; if(autocred){ u8 e[7]={0x13,5,1,p[0],(u8)(p[1]&15),1,0}; ev(e,7); } if(peer)peer(p,n); return n; }
static int psm_acc(l2chan_t* ch){ (void)ch; return 1; }
static int rxn; static u8 rxb[300];
static void rxcb(l2chan_t* ch,const u8* p,int n){ (void)ch; memcpy(rxb,p,n); rxn=n; }
static u8 plc[2];
static void auto_peer(const u8* p,int n){
    if(n<12){ return; } u16 cid=p[6]|(p[7]<<8); if(cid!=1){ return; } const u8* s=p+8;
    if(s[0]==0x02){ plc[0]=s[6]; plc[1]=s[7];  u8 r[12]={0x03,s[1],8,0,0x50,0x00,s[6],s[7],0,0,0,0}; l2rx(0x42,1,r,12); u8 c[8]={0x04,0x77,4,0,s[6],s[7],0,0}; l2rx(0x42,1,c,8); }
    else if(s[0]==0x04){ u8 r[10]={0x05,s[1],6,0,s[4],s[5],0,0,0,0}; r[4]=plc[0]; r[5]=plc[1]; l2rx(0x42,1,r,10); }
}
int main(void){
    setvbuf(stdout,0,_IONBF,0); bt_conn_init(); l2cap_init();
    CHECK(fc.n==3&&fc.mtu==27&&fc.shared,"Read Buffer Size parsed, LE shares BR/EDR buffers");
    u8 a[6]={1,2,3,4,5,6};
    u8 req[12]={0x04,10,1,2,3,4,5,6,0x40,0x25,0x00,1}; ncmd=0; bt_host_event(req,12);
    CHECK(ncmd==1&&cmds[0][0]==0x0A&&cmds[0][1]==0x04,"incoming connection from an unbonded keyboard refused (Reject Connection Request)");
    u8 cc[13]={0x03,11,0,0x42,0x00,1,2,3,4,5,6,1,0}; bt_host_event(cc,13);
    btconn_t* c=bt_conn_by_addr(a,0); CHECK(c&&c->state==BTC_UP&&c->handle==0x42,"Connection Complete brings link up with handle");
    l2cap_server(BT_PSM_HID_CTRL,psm_acc);
    u8 cr[8]={0x02,5,4,0,0x11,0x00,0x41,0x00}; nout=0; l2rx(0x42,1,cr,8);
    CHECK(nout==2&&out[0][8]==0x03&&out[0][16]==0&&out[1][8]==0x04,"incoming PSM 0x11 accepted, our config request sent");
    l2chan_t* h=ch_lcid(c,out[0][12]|(out[0][13]<<8)); CHECK(h&&h->rcid==0x41&&h->state==L2_CONFIG,"channel in config state");
    u8 cq[12]={0x04,6,8,0,(u8)h->lcid,0,0,0,0x01,2,0x30,0x00}; nout=0; l2rx(0x42,1,cq,12);
    CHECK(nout==1&&out[0][8]==0x05&&out[0][14]==0&&h->omtu==48,"peer config (MTU 48) accepted");
    u8 cp[10]={0x05,h->ident,6,0,(u8)h->lcid,0,0,0,0,0}; l2rx(0x42,1,cp,10);
    CHECK(h->state==L2_OPEN,"channel open after both configs");
    { u8 rb[200]; int k=0; rb[k++]=0x04; rb[k++]=12; k+=2; rb[k++]=(u8)h->lcid; rb[k++]=0; rb[k++]=0; rb[k++]=0; for(int i=0;i<20;i++){ rb[k++]=0x04; rb[k++]=1; rb[k++]=3; } rb[2]=(u8)(k-4); rb[3]=0;
      nout=0; l2rx(0x42,1,rb,k); CHECK(nout==1&&out[0][8]==0x05&&out[0][16]==1&&outn[0]==8+4+17,"config request with 20 RFC options answered once, no overflow"); }
    u8 big[48]; for(int i=0;i<48;i++)big[i]=(u8)i; h->rx=rxcb; bt_poll(); autocred=0; nout=0;
    CHECK(l2cap_send(h,big,48)==48&&nout==2&&outn[0]==4+27&&outn[1]==4+25&&(out[0][1]>>4)==2&&(out[1][1]>>4)==1,"52-byte frame split in 27+25 ACL fragments (start, continuation)");
    CHECK(fc.fr==1,"two controller credits used");
    u8 nc[7]={0x13,5,1,0x42,0,2,0}; bt_host_event(nc,7); CHECK(fc.fr==3,"Number of Completed Packets returns credits"); autocred=1;
    u8 f1[4+10]={0x42,0x20,10,0, 12,0,0x40,0, 9,8,7,6,5,4}; u8 f2[4+6]={0x42,0x10,6,0, 3,2,1,0,0xAA,0xBB}; rxn=0; bt_acl_rx(f1,14); bt_acl_rx(f2,10);
    CHECK(rxn==12&&rxb[0]==9&&rxb[11]==0xBB,"fragmented incoming frame reassembled");
    u8 two[28]={0x42,0x20,10,0, 6,0,0x40,0,1,2,3,4,5,6, 0x42,0x20,10,0, 6,0,0x40,0,7,8,9,10,11,12}; rxn=0; bt_acl_rx(two,10); bt_acl_rx(two+10,18);
    CHECK(rxn==6&&rxb[0]==7,"ACL stream split across USB transfers parsed");
    u8 ec[8]={0x08,4,0,0,0,0x61,0x62,0x63}; ec[2]=4; nout=0; l2rx(0x42,1,ec,8); CHECK(nout==1&&out[0][8]==0x09,"echo answered");
    u8 bad[8]={0x02,9,4,0,0x99,0x00,0x42,0x00}; nout=0; l2rx(0x42,1,bad,8); CHECK(nout==1&&out[0][16]==2,"unknown PSM refused");
    peer=auto_peer; l2chan_t* o=l2cap_connect(c,BT_PSM_HID_INTR,rxcb,2000); peer=0;
    CHECK(o&&o->state==L2_OPEN&&o->rcid==0x50,"outgoing PSM 0x13 connect + config completes");
    u8 dr[8]={0x06,11,4,0,(u8)h->lcid,0,0x41,0}; nout=0; l2rx(0x42,1,dr,8); CHECK(h->state==L2_CLOSED&&nout==1&&out[0][8]==0x07,"peer disconnect answered");
    u8 le[21]={0x3E,19,0x01,0,0x43,0,0,1, 9,9,9,9,9,9, 0x18,0,0,0,0xF4,1,0}; bt_host_event(le,21);
    u8 b9[6]={9,9,9,9,9,9}; btconn_t* l=bt_conn_by_addr(b9,1); CHECK(l&&l->state==BTC_UP&&l->le&&l->atype==1,"LE Connection Complete (incoming) tracked");
    u8 up[12]={0x12,3,8,0,0x18,0,0x28,0,0,0,0xF4,1}; nout=0; ncmd=0; l2rx(0x43,5,up,12);
    CHECK(nout==1&&out[0][8]==0x13&&out[0][12]==0&&ncmd==1&&cmds[0][0]==0x13&&cmds[0][1]==0x20,"LE parameter update accepted and LE Connection Update issued");
    u8 smp[7]={1,3,0,1,16,7,7}; nout=0; l2rx(0x43,6,smp,7); CHECK(nout==1&&out[0][8]==0x05&&out[0][9]==0x05,"SMP without handler answers Pairing Not Supported");
    static int fx; fx=0; void fxcb(btconn_t* cc2,const u8* p,int n){ (void)cc2; (void)p; fx=n; } l2cap_fixed(BT_CID_ATT,fxcb);
    u8 att[3]={0x1B,1,0}; l2rx(0x43,4,att,3); CHECK(fx==3,"ATT fixed channel delivered to handler");
    u8 dc[6]={0x05,4,0,0x42,0,0x13}; int downs=0; (void)downs; bt_host_event(dc,6); CHECK(!bt_conn_by_addr(a,0)&&o&&o->state==L2_CLOSED,"Disconnection Complete frees link and its channels");
    btconn_t* oc=bt_connect(a,0,0,2000); CHECK(oc&&oc->state==BTC_UP,"outgoing Create Connection completes");
    CHECK(bt_disconnect(oc,0x13,1000)&&!bt_conn_by_addr(a,0),"Disconnect completes");
    printf(fails?"L2CAP HOST TEST: %d of %d FAILED\n":"L2CAP HOST TEST: ALL PASS (%d checks)\n",fails?fails:checks,checks);
    return fails!=0;
}
