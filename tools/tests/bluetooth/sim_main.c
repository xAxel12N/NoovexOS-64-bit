#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "tools/tests/bluetooth/sim_shim.h"
#include "net/bluetooth/bt_host.h"
#include "net/bluetooth/bt_mgr.h"
#include "include/nvx/core/sysfeat.h"
int bt_up(void); int bt_scan(int);
volatile u32 g_ml_beat; volatile int g_busy_esc; volatile unsigned char kdown[128]; volatile u64 g_ticks; volatile u32 g_kbd_last_tick;
int kcaps, g_krep_delay=50, g_krep_rate=5, g_krep_flag, g_rtw_coex_done, g_wm_state;
void ui_pump(void){}
u32 rtw_find_mmio(void){ return 0; }
int rtw_coex_bt_share(u32 m,int on,int conn){ (void)m; (void)on; (void)conn; return 1; }
int rtw_coex_active(void){ return 0; }
void rtw_coex_bt_scan(int on){ (void)on; }
int rtlbt_setup(int (*cmd)(u16,const u8*,int,u8*,int),char* m,int cap){ (void)cmd; (void)cap; m[0]=0; return 0; }
int intelbt_match(u16 v,u16 p){ (void)v; (void)p; return 0; } int intelbt_setup(char* m,int cap){ (void)m; (void)cap; return 0; }
int bcmbt_match(u16 v,u16 m){ (void)v; (void)m; return 0; } int bcmbt_setup(int (*cmd)(u16,const u8*,int,u8*,int),u16 v,u16 p,char* m,int cap){ (void)cmd; (void)v; (void)p; (void)m; (void)cap; return 0; }
static int verbose;
void klog(const char* s){ if(verbose)printf("    klog: %s\n",s); }
void klog2(const char* a,const char* b){ if(verbose)printf("    klog: %s%s\n",a,b); }
char* scopy(char* p,const char* s){ while(*s)*p++=*s++; *p=0; return p; }
char* udec(char* p,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*p++=t[--n]; *p=0; return p; }
char* hx(char* p,u32 v,int dg){ static const char h[]="0123456789abcdef"; for(int i=dg-1;i>=0;i--)*p++=h[(v>>(i*4))&15]; *p=0; return p; }
int slen(const char* s){ int n=0; while(s[n])n++; return n; }
static u64 vt;
u64 hr_ns(void){ return vt+=20000; }
void hr_delay_us(u32 us){ vt+=(u64)us*1000; }
static char keys[64]; static int nkeys, nrel, nbtn;
void kpush(char c){ if(nkeys<63)keys[nkeys++]=c; }
void hk_push(int id){ (void)id; }
int usb_hid_key(u8 u,int shift){ if(u>=4&&u<=29)return (shift?'A':'a')+u-4; return 0; }
u8 usb_hid_sc(u8 u){ return u; }
void in_rel(int dx,int dy){ (void)dx; (void)dy; nrel++; }
void in_btn_raw(int l,int r,int m){ (void)l; (void)r; (void)m; nbtn++; }
void in_xbtn(int a,int b){ (void)a; (void)b; }
void in_scroll(int v,int h){ (void)v; (void)h; }
static u8 evq[64][260]; static int evn[64], evh, evt_;
static u8 aclq[64][1100]; static int acln[64], aclh, aclt;
static void ev(int code,const u8* p,int n){ u8* e=evq[evt_%64]; e[0]=(u8)code; e[1]=(u8)n; memcpy(e+2,p,(size_t)n); evn[evt_%64]=n+2; evt_++; }
static void cc(u16 op,const u8* r,int n){ u8 b[260]; b[0]=1; b[1]=(u8)op; b[2]=(u8)(op>>8); memcpy(b+3,r,(size_t)n); ev(0x0E,b,n+3); }
static void cs(u16 op,u8 st){ u8 b[4]={st,1,(u8)op,(u8)(op>>8)}; ev(0x0F,b,4); }
static const u8 RADDR[6]={0x01,0x02,0x03,0x04,0x05,0x06};
static int pages, cancels; static int connected, auth_done, enc_on, linkkey, ssp_seen, l2open[2], hs_sent, jw, cnfneg, ioneg, rejn, pkshown, nc, pe, ourio=-1, pkneg, badio; static u32 pkgot; static u16 rcid[2], scid_ctl, scid_int;
#define H 0x0040
static void acl(u16 cid,const u8* p,int n){ u8* a=aclq[aclt%64]; a[0]=(u8)H; a[1]=(u8)((H>>8)|0x20); a[2]=(u8)(n+4); a[3]=(u8)((n+4)>>8); a[4]=(u8)n; a[5]=(u8)(n>>8); a[6]=(u8)cid; a[7]=(u8)(cid>>8); memcpy(a+8,p,(size_t)n); acln[aclt%64]=n+8; aclt++; }
static void sig(u8 code,u8 id,const u8* d,int n){ u8 b[64]; b[0]=code; b[1]=id; b[2]=(u8)n; b[3]=0; memcpy(b+4,d,(size_t)n); acl(1,b,n+4); }
static u8 rid=0x40;
int btusb_listed(u16* v,u16* p){ *v=0x1234; *p=0x5678; return 1; }
int btusb_probe(void){ return 1; }
int btusb_ok(void){ return 1; }
void btusb_ids(u16* v,u16* p){ *v=0x1234; *p=0x5678; }
void btusb_stats(u32* a,u32* b,u32* c,u32* d){ *a=*b=*c=*d=0; }
int btusb_vctrl(u8 rt,u8 req,u16 val,u16 idx,u8* buf,u16 len){ (void)rt; (void)req; (void)val; (void)idx; (void)buf; (void)len; return -1; }
int btusb_cmd(const u8* pk,int n){
    u16 op=(u16)(pk[0]|pk[1]<<8); const u8* p=pk+3; (void)n; u8 r[32]={0};
    switch(op){
    case 0x1001: { u8 v[9]={0,9,0x0b,0,9,0x02,0,0x34,0x12}; cc(op,v,9); } break;
    case 0x1408: { u8 v[4]={0,(u8)H,0,16}; cc(op,v,4); } break;
    case 0x1009: { u8 v[7]={0,0xA0,0xA1,0xA2,0xA3,0xA4,0xA5}; cc(op,v,7); } break;
    case 0x1003: { u8 v[9]={0,0xff,0xff,0x8f,0xfe,0x1b,0xff,0x59,0x83}; cc(op,v,9); } break;
    case 0x0405: if(p[0]==0xA1||p[0]==0xA2){ cs(op,0); pages++; break; }
        if(memcmp(p,RADDR,6)){ cs(op,0); u8 c[11]={0x04,0,0}; memcpy(c+3,p,6); c[9]=1; ev(0x03,c,11); break; }
        cs(op,0); { u8 c[11]={0,(u8)H,0}; memcpy(c+3,RADDR,6); c[9]=1; c[10]=0; ev(0x03,c,11); connected=1; } break;
    case 0x0408: cs(op,0); if(p[0]==0xA1){ u8 c[11]={0x02,0,0}; memcpy(c+3,p,6); c[9]=1; ev(0x03,c,11); cancels++; } break;
    case 0x0406: cs(op,0); { u8 d[4]={0,p[0],p[1],0x16}; ev(0x05,d,4); connected=0; } break;
    case 0x0411: cs(op,0); if(linkkey){ u8 d[6]; memcpy(d,RADDR,6); ev(0x17,d,6); } else { u8 d[6]; memcpy(d,RADDR,6); ev(0x31,d,6); } break;
    case 0x040B: { u8 v[7]={0}; memcpy(v+1,RADDR,6); cc(op,v,7); u8 a[3]={0,(u8)H,0}; ev(0x06,a,3); auth_done=1; } break;
    case 0x042B: if(nc||pe){ ourio=p[6]; u8 v[7]={0}; memcpy(v+1,RADDR,6); cc(op,v,7); u8 io[9]; memcpy(io,RADDR,6); io[6]=nc?(badio?0x07:0x01):0x00; io[7]=0; io[8]=0x05; ev(0x32,io,9); ssp_seen=1;
            u8 uc[10]; memcpy(uc,RADDR,6); uc[6]=0x3F; uc[7]=0x42; uc[8]=0x0F; uc[9]=0; ev(nc?0x33:0x34,uc,nc?10:6); break; }
        { u8 v[7]={0}; memcpy(v+1,RADDR,6); cc(op,v,7); u8 io[9]; memcpy(io,RADDR,6); io[6]=jw?0x03:0x02; io[7]=0; io[8]=jw?0x04:0x05; ev(0x32,io,9); ssp_seen=1;
        if(jw){ u8 uc[10]; memcpy(uc,RADDR,6); uc[6]=0x40; uc[7]=0xE2; uc[8]=0x01; uc[9]=0; ev(0x33,uc,10); break; }
        u8 pk[10]; memcpy(pk,RADDR,6); pk[6]=0x40; pk[7]=0xE2; pk[8]=0x01; pk[9]=0; ev(0x3B,pk,10); pkshown=1;
        u8 sp[7]={0}; memcpy(sp+1,RADDR,6); ev(0x36,sp,7); u8 lk[23]; memcpy(lk,RADDR,6); for(int i=0;i<16;i++)lk[6+i]=(u8)(0x10+i); lk[22]=5; ev(0x18,lk,23); linkkey=1; u8 a[3]={0,(u8)H,0}; ev(0x06,a,3); auth_done=1; } break;
    case 0x042D: { u8 v[7]={0}; memcpy(v+1,RADDR,6); cc(op,v,7); cnfneg=1; u8 sp[7]={0x05}; memcpy(sp+1,RADDR,6); ev(0x36,sp,7); u8 a[3]={0x05,(u8)H,0}; ev(0x06,a,3); } break;
    case 0x042E: { u8 v[7]={0}; memcpy(v+1,RADDR,6); cc(op,v,7); pkgot=(u32)p[6]|(u32)p[7]<<8|(u32)p[8]<<16|(u32)p[9]<<24; int ok=pkgot==987654; u8 sp[7]={(u8)(ok?0:0x05)}; memcpy(sp+1,RADDR,6); ev(0x36,sp,7);
        if(ok){ u8 lk[23]; memcpy(lk,RADDR,6); for(int i=0;i<16;i++)lk[6+i]=(u8)(0x30+i); lk[22]=5; ev(0x18,lk,23); linkkey=1; } u8 a[3]={(u8)(ok?0:0x05),(u8)H,0}; ev(0x06,a,3); auth_done=ok; } break;
    case 0x042F: { u8 v[7]={0}; memcpy(v+1,RADDR,6); cc(op,v,7); pkneg=1; u8 sp[7]={0x05}; memcpy(sp+1,RADDR,6); ev(0x36,sp,7); u8 a[3]={0x05,(u8)H,0}; ev(0x06,a,3); } break;
    case 0x0434: { u8 v[7]={0}; memcpy(v+1,RADDR,6); cc(op,v,7); ioneg=1; } break;
    case 0x042C: { u8 v[7]={0}; memcpy(v+1,RADDR,6); cc(op,v,7); u8 sp[7]={0}; memcpy(sp+1,RADDR,6); ev(0x36,sp,7); u8 lk[23]; memcpy(lk,RADDR,6); for(int i=0;i<16;i++)lk[6+i]=(u8)(0x10+i); lk[22]=(u8)(nc?5:4); ev(0x18,lk,23); linkkey=1; u8 a[3]={0,(u8)H,0}; ev(0x06,a,3); auth_done=1; } break;
    case 0x0413: cs(op,0); { u8 e[4]={0,(u8)H,0,1}; ev(0x08,e,4); enc_on=1; } break;
    default: if((op>>10)==1){ if(op==0x040A)rejn++; cs(op,0); break; } r[0]=0; cc(op,r,(op==0x1005)?8:1); if(op==0x1005){ evq[(evt_-1)%64][6]=0x00; evq[(evt_-1)%64][7]=0x04; evq[(evt_-1)%64][8]=0x40; evq[(evt_-1)%64][9]=0x08; evq[(evt_-1)%64][10]=0x00; evq[(evt_-1)%64][11]=0x00; } break; }
    return 0;
}
int btusb_evt(u8* out,int cap){ if(evh==evt_)return 0; int n=evn[evh%64]; if(n>cap)n=cap; memcpy(out,evq[evh%64],(size_t)n); evh++; return n; }
int btusb_bulk_in(u8* out,int cap,int ms){ (void)ms; if(aclh==aclt)return 0; int n=acln[aclh%64]; if(n>cap)n=cap; memcpy(out,aclq[aclh%64],(size_t)n); aclh++; return n; }
static void remote_l2(u16 cid,const u8* p,int n){
    if(cid==1){ for(int o=0;o+4<=n;){ u8 code=p[o], id=p[o+1]; int l=p[o+2]|p[o+3]<<8; const u8* d=p+o+4;
            if(code==0x02){ u16 psm=(u16)(d[0]|d[1]<<8), sc=(u16)(d[2]|d[3]<<8); int k=psm==0x13; if(k)scid_int=sc; else scid_ctl=sc; rcid[k]=(u16)(0x0070+k);
                u8 rs[8]={(u8)rcid[k],0,(u8)sc,(u8)(sc>>8),0,0,0,0}; sig(0x03,id,rs,8); u8 cr[4]={(u8)sc,(u8)(sc>>8),0,0}; sig(0x04,rid++,cr,4); }
            else if(code==0x04){ u16 dc=(u16)(d[0]|d[1]<<8); int k=dc==rcid[1]; u16 sc=k?scid_int:scid_ctl; u8 rs[6]={(u8)sc,(u8)(sc>>8),0,0,0,0}; sig(0x05,id,rs,6); l2open[k]=1; }
            else if(code==0x06){ u8 rs[4]={d[0],d[1],d[2],d[3]}; sig(0x07,id,rs,4); }
            else if(code==0x0A){ u8 rs[4]={d[0],d[1],1,0}; sig(0x0B,id,rs,4); }
            o+=4+l; } return; }
    if(cid==rcid[0]&&n>=1&&(p[0]&0xF0)==0x70){ u8 h=0x00; acl(scid_ctl,&h,1); hs_sent=1; }
}
int btusb_bulk_out(const u8* p,int n,int ms){ (void)ms; if(n<8)return -1; int l=p[4]|p[5]<<8; u16 cid=(u16)(p[6]|p[7]<<8); if(l+8>n)return -1;
    { u8 nc[5]={1,(u8)H,0,1,0}; ev(0x13,nc,5); } remote_l2(cid,p+8,l); return n; }
__attribute__((weak)) int bt_sec_event(const u8* e,int n){ (void)e; (void)n; return 0; }
__attribute__((weak)) int bt_sec_auth(btconn_t* c,int ms){ (void)c; (void)ms; return 1; }
__attribute__((weak)) int bt_smp_pair(btconn_t* c,int ms){ (void)c; (void)ms; return 1; }
static int fails, checks;
int fg_n_begin, fg_n_ok, fg_n_fail;
void fg_boot(struct featguard* g){ (void)g; }
int fg_begin(struct featguard* g){ fg_n_begin++; return g->on&&!g->dead; }
void fg_ok(struct featguard* g){ (void)g; fg_n_ok++; }
void fg_fail(struct featguard* g,const char* why){ (void)g; (void)why; fg_n_fail++; }
void fg_enable(struct featguard* g,int on){ g->on=on; }
static int ui_script, ui_calls; static u32 ui_value; static int ui_kind;
#define CHECK(c,msg) do{ checks++; if(c)printf("  %-62s ok\n",msg); else { fails++; printf("  %-62s FAIL (line %d)\n",msg,__LINE__); } }while(0)
void hidh_init(void);
int hidh_active(btconn_t* c);
#ifdef SIM_SEC
#include "net/bluetooth/bt_sec.h"
int bt_prompt_ui(const btprompt_t* p){ ui_calls++; ui_value=p->value; ui_kind=p->kind;
    if(ui_script==3){ static const int k[7]={0x0A,0x09,0x08,0x07,0x06,0x02,0x03}; for(int i=0;i<7;i++)bt_sec_prompt_key(k[i]); bt_sec_prompt_key(0x0E); bt_sec_prompt_key(0x05); return 1; }
    if(ui_script==4){ if(!p->nd)bt_sec_prompt_key(0x02); return 1; }
    return ui_script; }
#endif
__attribute__((weak)) void bt_hogp_tick(void){}
__attribute__((weak)) int bt_hogp_text(char* p,char* e){ (void)p; (void)e; return 0; }
int main(int argc,char** argv){ (void)argv;
    verbose=argc>1;
    CHECK(bt_up()==1,"controller comes up through hci.c");
    { u8 inq[16]={1,0x01,0x02,0x03,0x04,0x05,0x06,0x01,0x00,0x00,0x40,0x05,0x00,0x00,0x00}; inq[14]=0; ev(0x02,inq,15); bt_scan(2); }
    btm_sync(); CHECK(btm_count()>=1&&btm_get(0)->cod==0x000540,"scan result reaches the manager with class of device");
    CHECK(!strcmp(btm_kind(btm_get(0)),"keyboard"),"class 0x000540 shown as keyboard");
    { u8 a[6]; CHECK(btm_parse_addr("06:05:04:03:02:01",a)&&!memcmp(a,RADDR,6),"address text parses MSB first"); CHECK(!btm_parse_addr("06:05:04",a),"short address rejected"); }
    hidh_init();
    int r=btm_pair("06:05:04:03:02:01");
    printf("    manager says: %s\n",btm_last());
    CHECK(connected,"ACL link created (Create Connection -> Connection Complete)");
#ifdef SIM_SEC
    CHECK(auth_done&&ssp_seen,"SSP (IO capability, confirmation, link key) ran on the link");
#else
    printf("  SSP skipped: net/bluetooth/sec.c not in the tree yet\n");
#endif
    CHECK(l2open[0]&&l2open[1],"L2CAP HID control 0x11 and interrupt 0x13 configured");
    CHECK(r==0,"bt pair reports success");
    { u8 rep[10]={0xA1,0x01,0x00,0x00,0x04,0,0,0,0,0}; acl(scid_int,rep,10); u8 up[10]={0xA1,0x01,0,0,0,0,0,0,0,0}; acl(scid_int,up,10); }
    for(int i=0;i<50;i++){ bt_poll(); u8 x[4]; bt_hci_wait_evt(0xFE,-1,x,4,1); }
    CHECK(nkeys>=1&&keys[0]=='a',"HID key report on the interrupt channel types 'a'");
    { char b[512]; btm_devices_text(b,sizeof b); printf("%s",b); CHECK(strstr(b,"connected")!=0,"bt devices lists the keyboard as connected"); }
    CHECK(btm_disconnect("")==1&&!connected,"bt disconnect closes the link");
#ifdef SIM_SEC
    { CHECK(pkshown&&bt_sec_passkey()==123456,"keyboard pairs with passkey entry, passkey shown");
      btkey_t* k=bt_keys_find(RADDR,0); CHECK(k&&(k->flags&BTKF_AUTH),"passkey bond stored as authenticated");
      ioneg=0; { u8 d[6]; memcpy(d,RADDR,6); d[0]^=0x55; ev(0x31,d,6); } for(int i=0;i<5;i++)bt_poll(); CHECK(ioneg,"remote-initiated pairing outside bt pair refused");
      jw=1; linkkey=0; cnfneg=0; int r2=btm_pair("06:05:04:03:02:01"); jw=0;
      CHECK(r2!=0&&cnfneg&&!bt_keys_find(RADDR,0),"keyboard offering only Just Works is refused, no bond stored");
      int k0=nkeys; rejn=0; { u8 cr[10]; memcpy(cr,RADDR,6); cr[6]=0x40; cr[7]=0x05; cr[8]=0; cr[9]=1; ev(0x04,cr,10); } for(int i=0;i<5;i++)bt_poll();
      CHECK(rejn==1,"incoming connection from unbonded keyboard refused");
      { u8 c3[11]={0,(u8)H,0}; memcpy(c3+3,RADDR,6); c3[9]=1; c3[10]=0; ev(0x03,c3,11); } for(int i=0;i<5;i++)bt_poll();
      { u8 cr[4]={0x13,0x00,0x41,0x00}; sig(0x02,0x21,cr,4); u8 rep[10]={0xA1,0x01,0,0,0x05,0,0,0,0,0}; acl(0x0040,rep,10); acl(0x0041,rep,10); }
      for(int i=0;i<20;i++){ bt_poll(); } btconn_t* bc=bt_conn_by_addr(RADDR,0);
      CHECK(nkeys==k0&&!(bc&&hidh_active(bc)),"unbonded unencrypted peer opening PSM 0x13 gets no HID channel and types nothing");
      if(bc&&bc->state==BTC_UP)bt_disconnect(bc,0x13,2000);
      nc=1; badio=1; linkkey=0; cnfneg=0; ui_script=1; ui_calls=0; { u8 iq[15]={1,0x01,0x02,0x03,0x04,0x05,0x06,0x01,0,0,0x04,0x04,0x24,0,0}; ev(0x02,iq,15); bt_scan(1); } { int rb2=btm_pair("06:05:04:03:02:01"); CHECK(!ui_calls&&rb2!=0&&cnfneg&&!bt_keys_find(RADDR,0),"user confirmation after an invalid peer IO capability (0x07) is refused without a prompt or auto-accept, no bond"); } badio=0; { u8 iq[15]={1,0x01,0x02,0x03,0x04,0x05,0x06,0x01,0,0,0x40,0x05,0x00,0,0}; ev(0x02,iq,15); bt_scan(1); }
      nc=1; linkkey=0; cnfneg=0; ui_script=2; ui_calls=0; int r3=btm_pair("06:05:04:03:02:01");
      CHECK(ourio==0x01,"IO capability reply says DisplayYesNo");
      CHECK(ui_calls&&ui_kind==BTP_CONFIRM&&ui_value==999999&&r3!=0&&cnfneg&&!bt_keys_find(RADDR,0),"numeric comparison: code 999999 shown, No sends negative reply, no bond");
      CHECK(!bt_sec_prompt()->on,"prompt closed after the answer");
      linkkey=0; ui_script=1; ui_calls=0; cnfneg=0; int r4=btm_pair("06:05:04:03:02:01"); btkey_t* k4=bt_keys_find(RADDR,0);
      CHECK(ui_calls&&r4==0&&!cnfneg&&k4&&(k4->flags&BTKF_AUTH)&&k4->ktype==5,"numeric comparison: Yes confirms, authenticated bond stored");
      btm_disconnect(""); nc=0; pe=1; linkkey=0; ui_script=3; ui_calls=0; pkneg=0; int r5=btm_pair("06:05:04:03:02:01"); btkey_t* k5=bt_keys_find(RADDR,0);
      CHECK(ui_calls&&ui_kind==BTP_ENTRY&&pkgot==987654&&r5==0&&k5&&k5->key[0]==0x30,"passkey entry: typed 987654 (with a backspace) sent in Passkey Reply, bond stored");
      btm_disconnect(""); linkkey=0; ui_script=4; ui_calls=0; pkneg=0; pkgot=0; int r6=btm_pair("06:05:04:03:02:01");
      CHECK(ui_calls&&r6!=0&&!pkgot&&!bt_keys_find(RADDR,0),"passkey entry: Enter with fewer than 6 digits is ignored, prompt times out, no bond");
      CHECK(pkneg,"timed-out passkey prompt sends Passkey Negative Reply");
      pe=0; btm_disconnect(""); linkkey=0; ui_script=1; btm_pair("06:05:04:03:02:01"); btm_disconnect(""); ui_script=0;
      CHECK(bt_keys_find(RADDR,0)&&!connected,"bonded keyboard, link down before the boot check");
      g_ticks=10; CHECK(btm_boot_tick()==0&&!connected,"no reconnect before the boot delay");
      { static const u8 A1[6]={0xA1,1,2,3,4,5}, A2[6]={0xA2,1,2,3,4,5}, A3[6]={0xA3,1,2,3,4,5}; bt_keys_add(A1,0,0); bt_keys_add(A2,0,0); bt_keys_add(A3,0,0); }
      g_ticks=BTM_BOOT_TICKS+1; auth_done=0; enc_on=0; l2open[0]=l2open[1]=0; int rb=0, nt=0; u64 worst=0, t0=vt;
      for(;nt<10000;nt++){ bt_poll(); u64 a=vt; rb=btm_boot_tick(); if(vt-a>worst)worst=vt-a; vt+=10000000ULL; if(nt&&!btm_boot_busy())break; }
      printf("    manager says: %s; %d main-loop ticks, %llu ms total, longest tick %llu us\n",btm_last(),nt,(unsigned long long)((vt-t0)/1000000ULL),(unsigned long long)(worst/1000ULL));
      CHECK(rb==1&&connected&&auth_done&&enc_on&&l2open[1]&&fg_n_begin==1&&fg_n_ok==1,"boot reconnect: bonded keyboard back with the stored key (encrypted, HID up), featguard begin/ok");
      CHECK(strstr(btm_last(),"1 of 4")!=0,"boot reconnect tried the keyboard and 3 absent devices");
      CHECK(worst<60000000ULL,"no main-loop tick blocks longer than 60 ms while devices are absent");
      CHECK(pages==2&&cancels==1,"silent devices are paged, then cancelled after the page window");
      CHECK(vt-t0<(u64)(3*(BTM_RB_PAGE_MS+BTM_RB_DROP_MS)+2000)*1000000ULL,"whole reconnect pass ends within its bounds");
      { int used=0; for(int i=0;i<BT_MAX_CONN;i++) if(bt_conn_at(i))used++; CHECK(used==1,"no connection slot left behind (cancel ignored -> slot dropped)"); }
      CHECK(btm_boot_tick()==0&&fg_n_begin==1,"boot reconnect runs once per boot"); }
#endif
    printf("%d of %d checks passed\n",checks-fails,checks);
    if(!fails)printf("BT SIM: ALL PASS\n");
    return fails?1:0;
}
