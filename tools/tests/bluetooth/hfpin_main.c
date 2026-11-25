#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "net/bluetooth/conn.c"
#include "net/bluetooth/l2cap.c"
#include "net/bluetooth/profiles/rfcomm.c"
#include "net/bluetooth/profiles/hfp.c"
#include "net/bluetooth/profiles/sdp_srv.c"
static int fails, checks;
#define CHECK(c,m) do{ checks++; if(c)printf("  %s ok\n",m); else { printf("  FAIL %s\n",m); fails++; } }while(0)
struct nvx_snd g_snd; int g_snd_state=1, g_snd_vol=80;
volatile u32 g_ml_beat; static u64 now; static int bonded=1;
btkey_t* bt_keys_find(const u8* a,int le){ static btkey_t k; (void)a; return bonded&&!le?&k:0; }
int snd_init(void){ return 1; }
void ui_pump(void){}
u64 hr_ns(void){ return now+=100000; }
void hr_delay_us(u32 us){ now+=(u64)us*1000; }
void klog(const char* s){ printf("    log: %s\n",s); }
void klog2(const char* a,const char* b){ printf("    log: %s%s\n",a,b); }
static int fg_fails, fg_oks;
void fg_boot(struct featguard* g){ (void)g; } int fg_begin(struct featguard* g){ return g->on&&!g->dead; } void fg_ok(struct featguard* g){ (void)g; fg_oks++; } void fg_fail(struct featguard* g,const char* w){ (void)g; (void)w; fg_fails++; } void fg_enable(struct featguard* g,int on){ g->on=on; }
char* scopy(char* p,const char* s){ while(*s)*p++=*s++; *p=0; return p; }
char* udec(char* p,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*p++=t[--n]; *p=0; return p; }
char* hx(char* p,u32 v,int d){ for(int i=d-1;i>=0;i--)*p++="0123456789ABCDEF"[(v>>(i*4))&15]; *p=0; return p; }
static u8 evq[16][64]; static int evn[16], evh, evt;
static void ev(const u8* e,int n){ memcpy(evq[evt],e,n); evn[evt]=n; evt=(evt+1)&15; }
int bt_hci_up(void){ return 1; } int bt_hci_le(void){ return 1; } int bt_hci_bredr(void){ return 1; }
static u16 last_op; static u8 last_cmd[32]; static int last_n;
int bt_hci_send(u16 op,const u8* p,int n){ last_op=op; last_n=n<32?n:32; memcpy(last_cmd,p,last_n); return 0; }
int bt_hci_cmd(u16 op,const u8* p,int n,u8* rp,int rcap,int ms){ (void)p; (void)n; (void)rcap; (void)ms;
    if(op==0x1005){ u8 r[8]={0,0xFD,0x03,0,8,0,0,0}; memcpy(rp,r,8); return 8; } rp[0]=0; return 1; }
int bt_hci_wait_evt(int code,int sub,u8* o,int cap,int ms){ (void)code; (void)sub; (void)o; (void)cap; (void)ms; bt_poll(); return -1; }
static u8 pend[64][300]; static int pendn[64], ph, pt;
static void l2rx(u16 cid,const u8* d,int n){ u8* b=pend[pt]; pendn[pt]=n+8; pt=(pt+1)&63; b[0]=0x42; b[1]=0x20; b[2]=(u8)(n+4); b[3]=(u8)((n+4)>>8); b[4]=(u8)n; b[5]=(u8)(n>>8); b[6]=(u8)cid; b[7]=(u8)(cid>>8); memcpy(b+8,d,n); }
void bt_poll(void){ while(evh!=evt){ int k=evh; evh=(evh+1)&15; bt_host_event(evq[k],evn[k]); } while(ph!=pt){ int k=ph; ph=(ph+1)&63; bt_acl_rx(pend[k],pendn[k]); } }
static int iso_ok, iso_alt, iso_stops;
int btusb_sco_start(int alt){ iso_alt=alt; return iso_ok; }
void btusb_sco_stop(void){ iso_stops++; }
int a2dp_listen(void){ return 1; }
static u16 hs_sc[2]={0x50,0x51}, our[2], hres[2]; static u8 hid=0x30;
static u8 sdpin[2048]; static int sdpinn, sdpgot;
static int h_cfc, ua0, ua0_cr, pn_ok, pn_cfc, pn_cr, dlc_ua, dlc_ua_cr, msc_cmd, msc_rsp, dm_dlci=-1, uih_cr=-1, h_step, h_bad, bcs_seen;
static char hgot[2048]; static int hgotn;
static const char* hs_at[]={"AT+BRSF=144\r","AT+BAC=1,2\r","AT+CIND=?\r","AT+CIND?\r","AT+CMER=3,0,0,1\r",0};
static void hs_l2(int i,const u8* d,int n){ l2rx(our[i],d,n); }
static void hs_conn(int i,u16 psm){ u8 r[8]={0x02,++hid,4,0,(u8)psm,(u8)(psm>>8),(u8)hs_sc[i],(u8)(hs_sc[i]>>8)}; our[i]=0; hres[i]=0xFFFF; l2rx(1,r,8); bt_poll(); }
static void hs_rfc(u8 dlci,int cr,u8 ctl,int credits,const u8* d,int n){ u8 b[300]; int k=rfc_build(dlci,cr,ctl,credits,d,n,b,(int)sizeof b); hs_l2(1,b,k); }
static void hs_next_at(void){ const char* s=hs_at[h_step]; if(!s)return; h_step++; hs_rfc(26,1,RFC_UIH,7,(const u8*)s,(int)strlen(s)); }
static void hs_rfc_rx(const u8* s,int n){
    rfcframe_t f; if(!rfc_parse(s,n,h_cfc,&f)){ h_bad++; return; }
    if(f.type==RFC_DM){ dm_dlci=f.dlci; return; }
    if(f.dlci==0&&f.type==RFC_UA){ ua0=1; ua0_cr=f.cr; return; }
    if(f.dlci==0&&f.type==RFC_UIH&&f.len>=2){ const u8* m=f.info; u8 t=m[0]&0xFC; int cmd=(m[0]>>1)&1; if(f.cr!=0)h_bad++;
        if(t==0x80&&!cmd&&f.len>=10){ pn_ok=1; pn_cfc=m[3]==0xE0; pn_cr=m[9]; h_cfc=pn_cfc; }
        else if(t==0xE0&&cmd){ msc_cmd=1; u8 r[4]={0xE1,0x05,m[2],m[3]}; hs_rfc(0,1,RFC_UIH,-1,r,4); }
        else if(t==0xE0&&!cmd)msc_rsp=1;
        return; }
    if(f.dlci==26&&f.type==RFC_UA){ dlc_ua=1; dlc_ua_cr=f.cr; u8 m[4]={0xE3,0x05,(u8)((26<<2)|3),0x8D}; hs_rfc(0,1,RFC_UIH,-1,m,4); hs_next_at(); return; }
    if(f.dlci==26&&f.type==RFC_UIH){ uih_cr=f.cr; int o0=hgotn; if(f.len&&hgotn+f.len<(int)sizeof hgot){ memcpy(hgot+hgotn,f.info,f.len); hgotn+=f.len; hgot[hgotn]=0; }
        if(f.len&&strstr(hgot+o0,"+BCS: 2")){ bcs_seen=1; hs_rfc(26,1,RFC_UIH,7,(const u8*)"AT+BCS=2\r",9); }
        else if(f.len&&(strstr(hgot+o0,"OK")||strstr(hgot+o0,"ERROR")))hs_next_at();
        return; }
}
static void peer_l2(u16 cid,const u8* s,int n){
    if(cid==1){
        if(s[0]==0x03){ u16 dc=s[4]|(s[5]<<8), sc=s[6]|(s[7]<<8), rs=s[8]|(s[9]<<8); for(int i=0;i<2;i++)if(hs_sc[i]==sc){ hres[i]=rs; if(!rs){ our[i]=dc; u8 c[8]={0x04,++hid,4,0,(u8)dc,(u8)(dc>>8),0,0}; l2rx(1,c,8); } } }
        else if(s[0]==0x04){ u16 d=s[4]|(s[5]<<8); for(int i=0;i<2;i++)if(hs_sc[i]==d){ u8 r[10]={0x05,s[1],6,0,(u8)our[i],(u8)(our[i]>>8),0,0,0,0}; l2rx(1,r,10); } }
        else if(s[0]==0x06){ u8 r[8]={0x07,s[1],4,0,s[4],s[5],s[6],s[7]}; l2rx(1,r,8); }
        return; }
    if(cid==hs_sc[0]){ if(n<=(int)sizeof sdpin){ memcpy(sdpin,s,n); sdpinn=n; sdpgot++; } return; }
    if(cid==hs_sc[1])hs_rfc_rx(s,n);
}
int btusb_bulk_out(const u8* p,int n,int ms){ (void)ms; u8 e[7]={0x13,5,1,p[0],(u8)(p[1]&15),1,0}; ev(e,7); if(n>=8){ u16 cid=p[6]|(p[7]<<8); peer_l2(cid,p+8,n-8); } return n; }
static int req(const u8* q,int n,u8* o,int cap){ int k=sdp_srv_req(q,n,o,cap); return k; }
static int ecode(const u8* o,int k){ return k==7&&o[0]==1?(o[5]<<8)|o[6]:-1; }
static int ssa_all(u16 uuid,int maxb,int cap,u8* full,int* steps){
    u8 o[700]; int fn=0, off=-1; *steps=0;
    for(;;){ u8 q[32]; int k=0; q[k++]=0x06; q[k++]=0; q[k++]=(u8)*steps; q[k++]=0; q[k++]=0; q[k++]=0x35; q[k++]=3; q[k++]=0x19; q[k++]=(u8)(uuid>>8); q[k++]=(u8)uuid; q[k++]=(u8)(maxb>>8); q[k++]=(u8)maxb; q[k++]=0x35; q[k++]=5; q[k++]=0x0A; q[k++]=0; q[k++]=0; q[k++]=0xFF; q[k++]=0xFF;
        if(off<0)q[k++]=0; else { q[k++]=2; q[k++]=(u8)(off>>8); q[k++]=(u8)off; } q[4]=(u8)(k-5);
        int r=req(q,k,o,cap); (*steps)++; if(r<10||o[0]!=0x07||r>cap)return -1; int bc=(o[5]<<8)|o[6]; if(bc>maxb||7+bc+1>r)return -1;
        memcpy(full+fn,o+7,bc); fn+=bc; int cl=o[7+bc]; if(!cl)return fn; if(cl!=2||r!=7+bc+3)return -1; off=(o[8+bc]<<8)|o[9+bc]; if(*steps>200)return -1; }
}
static int find_attr(const u8* p,int n,u16 id,const u8** v,int* vn){
    for(int o=0;o<n;){ int hl,ln,t=sdp_de(p+o,n-o,&hl,&ln); if(t<0)return 0; if(t==6&&find_attr(p+o+hl,ln,id,v,vn))return 1;
        if(t==1&&ln==2&&((p[o+1]<<8)|p[o+2])==id){ int o2=o+hl+ln, h2,l2; if(sdp_de(p+o2,n-o2,&h2,&l2)<0)return 0; *v=p+o2; *vn=h2+l2; return 1; }
        o+=hl+ln; }
    return 0;
}
int main(void){
    setvbuf(stdout,0,_IONBF,0); bt_conn_init(); l2cap_init(); srand(7);
    sdp_srv_build(HFP_AG_CH,0x0020);
    { int hl,ln; u8 a[]={0x35,3,0x19,0x11,0x1F}; CHECK(sdp_de(a,5,&hl,&ln)==6&&hl==2&&ln==3,"data element: sequence header parsed");
      CHECK(sdp_de(a,4,&hl,&ln)<0,"data element: sequence longer than the buffer rejected");
      u8 b[]={0x37,0xFF,0xFF,0xFF,0xF0,0}; CHECK(sdp_de(b,6,&hl,&ln)<0,"data element: 4-byte length past the end rejected");
      u8 c[]={0x1B,0,0}; CHECK(sdp_de(c,3,&hl,&ln)<0,"data element: UUID of size 8 rejected");
      u8 d[]={0x0F}; CHECK(sdp_de(d,1,&hl,&ln)<0&&sdp_de(d,0,&hl,&ln)<0,"data element: bad uint size and empty input rejected");
      u8 e[]={0x36,0}; CHECK(sdp_de(e,2,&hl,&ln)<0,"data element: truncated 2-byte length rejected"); }
    { int n; for(int i=0;i<SDP_NREC;i++){ const u8* r=sdp_srv_record(i,&n); int hl,ln; CHECK(r&&sdp_de(r,n,&hl,&ln)==6&&hl+ln==n,i==0?"HFP AG record is one well-formed sequence":i==1?"A2DP source record is one well-formed sequence":"AVRCP target record is one well-formed sequence"); } }
    u8 o[700]; int k;
    { u8 q[]={0x02,0,1,0,8, 0x35,3,0x19,0x11,0x0A, 0,5, 0}; k=req(q,sizeof q,o,672); CHECK(k==14&&o[0]==3&&o[6]==1&&o[8]==1&&o[9]==0&&o[10]==1&&o[11]==0&&o[12]==2&&o[13]==0,"ServiceSearch 0x110A: one record, handle 0x00010002"); }
    { u8 q[]={0x02,0,2,0,22, 0x35,17,0x1C,0,0,0x11,0x1F,0,0,0x10,0,0x80,0,0,0x80,0x5F,0x9B,0x34,0xFB, 0,5, 0}; k=req(q,sizeof q,o,672); CHECK(k==14&&o[12]==1&&o[1]==0&&o[2]==2,"ServiceSearch with 128-bit base UUID of 0x111F finds the HFP AG record"); }
    { u8 q[]={0x02,0,3,0,8, 0x35,3,0x19,0x01,0x00, 0,2, 0}; k=req(q,sizeof q,o,672); CHECK(k==5+4+8+1&&o[6]==2&&o[8]==2,"ServiceSearch L2CAP: 3 records match, capped at 2 by the request"); }
    { u8 q[]={0x02,0,4,0,8, 0x35,3,0x19,0x11,0x08, 0,5, 0}; k=req(q,sizeof q,o,672); CHECK(k==10&&o[8]==0,"ServiceSearch Headset AG 0x1112: no records"); }
    { u8 q[]={0x02,0,5,0,9, 0x35,3,0x19,0x11,0x0A, 0,5, 0}; k=req(q,sizeof q,o,672); CHECK(ecode(o,k)==4,"parameter length disagreeing with PDU size: error 0x0004"); }
    { u8 q[]={0x02,0}; k=req(q,2,o,672); CHECK(ecode(o,k)==4,"two-byte PDU: error 0x0004"); }
    { u8 q[]={0x09,0,6,0,0}; k=req(q,5,o,672); CHECK(ecode(o,k)==3&&o[1]==0&&o[2]==6,"unknown PDU: error 0x0003 with the same transaction id"); }
    { u8 q[]={0x04,0,7,0,14, 0,1,0,9, 0,64, 0x35,5,0x0A,0,0,0xFF,0xFF, 0}; k=req(q,sizeof q,o,672); CHECK(ecode(o,k)==2,"ServiceAttribute for an unknown handle: error 0x0002"); }
    { u8 q[]={0x04,0,8,0,14, 0,1,0,1, 0,6, 0x35,5,0x0A,0,0,0xFF,0xFF, 0}; k=req(q,sizeof q,o,672); CHECK(ecode(o,k)==3,"MaximumAttributeByteCount below 7: error 0x0003"); }
    { u8 q[]={0x04,0,9,0,14, 0,1,0,1, 0,64, 0x35,5,0x0A,0xFF,0xFF,0,0, 0}; k=req(q,sizeof q,o,672); CHECK(ecode(o,k)==3,"attribute range with low above high: error 0x0003"); }
    { u8 q[]={0x04,0,10,0,12, 0,1,0,1, 0,64, 0x35,3,0x09,0x03,0x11, 0}; k=req(q,sizeof q,o,672); CHECK(k==7+8+1&&o[0]==5&&o[6]==8&&o[7]==0x35&&o[8]==6&&o[10]==3&&o[11]==0x11&&o[12]==0x09&&o[13]==0&&o[14]==0x20,"ServiceAttribute 0x0311 on HFP AG: wide-band speech feature 0x0020"); }
    { u8 q[]={0x06,0,11,0,14, 0x35,3,0x19,0x11,0x1F, 0,64, 0x35,3,0x09,0,4, 1,0}; k=req(q,sizeof q,o,672); CHECK(ecode(o,k)==5,"continuation state of length 1: error 0x0005"); }
    { u8 q[]={0x06,0,12,0,15, 0x35,3,0x19,0x11,0x1F, 0,64, 0x35,3,0x09,0,4, 2,0xFF,0xFF}; k=req(q,sizeof q,o,672); CHECK(ecode(o,k)==5,"continuation offset past the response: error 0x0005"); }
    { u8 q[]={0x06,0,13,0,12, 0x35,3,0x19,0x11,0x1F, 0,64, 0x35,3,0x09,0,4}; k=req(q,sizeof q,o,672); CHECK(ecode(o,k)==3,"missing continuation byte: error 0x0003"); }
    { u8 q[64]; int n=0; q[n++]=0x02; q[n++]=0; q[n++]=14; q[n++]=0; q[n++]=0; q[n++]=0x35; q[n++]=39; for(int i=0;i<13;i++){ q[n++]=0x19; q[n++]=0x11; q[n++]=0x1F; } q[n++]=0; q[n++]=5; q[n++]=0; q[4]=(u8)(n-5); k=req(q,n,o,672); CHECK(ecode(o,k)==3,"search pattern with 13 UUIDs: error 0x0003"); }
    { u8 q[]={0x02,0,15,0,9, 0x35,4,0x19,0x11,0x0A,0x08, 0,5, 0}; q[4]=9; k=req(q,sizeof q,o,672); CHECK(ecode(o,k)==3,"non-UUID inside the search pattern: error 0x0003"); }
    { u8 q[]={0x02,0,16,0,8, 0x35,9,0x19,0x11,0x0A, 0,5, 0}; k=req(q,sizeof q,o,672); CHECK(ecode(o,k)==3,"search pattern length past the PDU: error 0x0003"); }
    { u8 q[]={0x06,0,17,0,13, 0x35,3,0x19,0x11,0x1F, 0,64, 0x35,3,0x08,0,4, 0}; k=req(q,sizeof q,o,672); CHECK(ecode(o,k)==3,"attribute id of the wrong size: error 0x0003"); }
    { u8 big[700], small[700]; int s1, s2; int a=ssa_all(0x111F,0xFFFF,672,big,&s1), b=ssa_all(0x111F,16,48,small,&s2);
      CHECK(a>0&&s1==1&&b==a&&s2>3&&!memcmp(big,small,a),"ServiceSearchAttribute: 16-byte chunks with continuation reassemble to the single-shot answer");
      const u8* v; int vn; CHECK(a>0&&hfp_sdp_channel(big,a)==HFP_AG_CH,"HFP AG record: RFCOMM server channel 13 in the protocol list");
      CHECK(find_attr(big,a,0x0009,&v,&vn)&&vn==10&&v[5]==0x11&&v[6]==0x1E&&v[8]==1&&v[9]==8,"HFP AG record: Hands-Free profile 0x111E version 1.08");
      a=ssa_all(0x110A,0xFFFF,672,big,&s1); CHECK(a>0&&find_attr(big,a,0x0004,&v,&vn)&&v[9]==0x19&&v[14]==0x19&&v[16]==1&&v[17]==3,"A2DP source record: L2CAP PSM 0x0019, AVDTP 1.3");
      a=ssa_all(0x110C,0xFFFF,672,big,&s1); CHECK(a>0&&find_attr(big,a,0x0001,&v,&vn)&&v[3]==0x11&&v[4]==0x0C&&find_attr(big,a,0x0004,&v,&vn)&&v[9]==0x17,"AVRCP target record: class 0x110C on PSM 0x0017");
      a=ssa_all(0x1101,0xFFFF,672,big,&s1); CHECK(a==3&&big[0]==0x36&&big[1]==0&&big[2]==0,"no matching record: empty attribute list sequence"); }
    { u8 base[]={0x06,0,1,0,19, 0x35,3,0x19,0x11,0x1F, 0,32, 0x35,5,0x0A,0,0,0xFF,0xFF, 2,0,0x10}; int bad=0;
      for(int it=0;it<200000;it++){ u8 q[64]; int n;
          if(it&1){ n=(int)sizeof base; memcpy(q,base,n); int m=1+rand()%4; for(int j=0;j<m;j++)q[rand()%n]=(u8)rand(); if(rand()%3==0)n=rand()%(n+1); }
          else { n=rand()%64; for(int j=0;j<n;j++)q[j]=(u8)rand(); if(n>5){ q[0]=(u8)(2+2*(rand()%3)); q[3]=0; q[4]=(u8)(n-5); } }
          int cap=48+rand()%200; k=req(q,n,o,cap); if(k<7||k>cap||(o[0]!=1&&o[0]!=3&&o[0]!=5&&o[0]!=7)||((o[3]<<8)|o[4])!=k-5)bad++; }
      CHECK(!bad,"200000 random and mutated requests: every answer well-formed, within the MTU (ASan/UBSan clean)"); }
    u8 cc[13]={0x03,11,0,0x42,0x00,1,2,3,4,5,6,0x01,0}; bt_host_event(cc,13);
    btconn_t* c=bt_conn_by_addr((u8[]){1,2,3,4,5,6},0); CHECK(c&&c->state==BTC_UP,"ACL link from the headset up");
    bt_srv_init(); CHECK(fg_oks==1,"SDP, RFCOMM and A2DP listeners registered, featguard ok");
    hs_conn(0,SDP_PSM); CHECK(hres[0]==3,"unencrypted link: SDP channel refused (0x0003)");
    hs_conn(1,RFC_PSM); CHECK(hres[1]==3&&R.st==RS_IDLE,"unencrypted link: RFCOMM channel refused");
    c->enc=1; c->keysz=16; bonded=0; hs_conn(1,RFC_PSM); CHECK(hres[1]==3,"encrypted but not bonded: RFCOMM refused"); bonded=1;
    c->keysz=7; hs_conn(0,SDP_PSM); CHECK(hres[0]==3,"bonded with a 7-byte key: SDP refused"); c->keysz=16;
    hs_conn(0,SDP_PSM); CHECK(hres[0]==0&&our[0],"bonded, encrypted headset: SDP channel accepted");
    { u8 q[]={0x06,0,0x42,0,15, 0x35,3,0x19,0x11,0x1F, 0x02,0x00, 0x35,5,0x0A,0,0,0xFF,0xFF, 0}; sdpgot=0; hs_l2(0,q,sizeof q); bt_poll();
      CHECK(sdpgot==1&&sdpin[0]==0x07&&sdpin[2]==0x42&&hfp_sdp_channel(sdpin+7,sdpinn-7)==HFP_AG_CH,"headset's SDP query over L2CAP answered: HFP AG on channel 13"); }
    hs_conn(1,RFC_PSM); CHECK(hres[1]==0&&our[1]&&R.st==RS_LMUX&&!R.ini,"RFCOMM L2CAP channel accepted, waiting for SABM on DLCI 0");
    hs_rfc(26,1,RFC_SABM|RFC_PF,-1,0,0); bt_poll(); CHECK(!dlc_ua&&R.st==RS_LMUX,"DLC SABM before the multiplexer is up ignored");
    hs_rfc(0,1,RFC_SABM|RFC_PF,-1,0,0); bt_poll(); CHECK(ua0&&ua0_cr==1&&R.st==RS_LDLC,"SABM DLCI 0 answered UA with C/R=1 (responder response)");
    { u8 m[10]={0x83,0x11,10,0xF0,0,0,0x7F,0,0,7}; hs_rfc(0,1,RFC_UIH,-1,m,10); bt_poll(); CHECK(dm_dlci==10&&!pn_ok,"PN for another server channel (5): DM"); dm_dlci=-1; }
    hs_rfc(10,1,RFC_SABM|RFC_PF,-1,0,0); bt_poll(); CHECK(dm_dlci==10&&R.st==RS_LDLC,"SABM on DLCI 10 (not our channel): DM"); dm_dlci=-1;
    { u8 m[10]={0x83,0x11,26,0xF0,0,0,10,0,0,7}; hs_rfc(0,1,RFC_UIH,-1,m,10); bt_poll(); CHECK(dm_dlci==26&&!pn_ok,"PN with MTU 10: refused with DM"); dm_dlci=-1; }
    { u8 m[10]={0x83,0x11,26,0xF0,0,0,0x9A,0x02,0,7}; hs_rfc(0,1,RFC_UIH,-1,m,10); bt_poll(); CHECK(pn_ok&&pn_cfc&&pn_cr==7&&R.cfc&&R.mtu==127&&R.tx_cr==7&&!h_bad,"PN on DLCI 26 (channel 13): credit flow accepted (0xE0), MTU capped to 127, 7 credits each way, C/R=0 on our UIH"); }
    hs_rfc(26,1,RFC_SABM|RFC_PF,-1,0,0); bt_poll();
    CHECK(dlc_ua&&dlc_ua_cr==1&&msc_cmd&&R.dlci==26,"SABM DLCI 26 answered UA (C/R=1), MSC sent");
    CHECK(H.st==HAG_SLC&&hfp_slc_up()&&rfcomm_incoming()&&h_c==c,"headset ran BRSF/BAC/CIND/CMER: service level connection up as Audio Gateway");
    CHECK(strstr(hgot,"+BRSF: 512")&&strstr(hgot,"+CIND: 1,0,0,0,5,0,5")&&uih_cr==0&&!h_bad,"headset got +BRSF: 512 and +CIND; our UIH frames carry C/R=0");
    CHECK(bcs_seen&&H.codec==2&&hfp_codec()==2,"codec negotiation from the incoming link: +BCS: 2 confirmed, mSBC");
    CHECK(fg_oks==2&&hfp_mic_selected(),"HFP featguard ok, headset microphone selected");
    CHECK(hfp_connect(c,3000)==1&&rfcomm_incoming(),"outgoing hfp_connect to the same headset reuses the incoming link");
    hs_conn(1,RFC_PSM); CHECK(hres[1]==3,"second RFCOMM session while one is up: refused");
    { u8 b[8]; int kk=rfc_build(26,1,RFC_DISC|RFC_PF,-1,0,0,b,8); l2rx(R.l2?R.l2->lcid:0,b,kk); bt_poll(); CHECK(!hfp_slc_up()&&R.st==RS_LDLC,"DISC on DLCI 26 from the headset: channel closed, session back to listening"); }
    hfp_disconnect(); CHECK(!R.l2&&R.st==RS_IDLE,"hfp_disconnect closes the responder session");
    bt_srv_set_enabled(0); hs_conn(1,RFC_PSM); int r1=hres[1]; hs_conn(0,SDP_PSM); CHECK(r1==3&&hres[0]==3,"incoming connections turned off in Settings: RFCOMM and SDP refused");
    bt_srv_set_enabled(1); hs_conn(1,RFC_PSM); CHECK(hres[1]==0&&R.st==RS_LMUX,"turned back on: RFCOMM accepted again");
    printf("%d of %d checks passed\n",checks-fails,checks);
    if(!fails)printf("HFP INCOMING HOST TEST: ALL PASS\n");
    return fails!=0;
}
