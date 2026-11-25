#include "include/nvx/drivers/bluetooth/bt.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "net/bluetooth/hci_parse.h"
#pragma GCC diagnostic pop
#include "net/bluetooth/bt_mgr.h"
#include "net/bluetooth/bt_sec.h"
#include "include/nvx/net/wifi/wifi_settings.h"
int bt_hci_up(void);
int a2dp_enabled(void); void a2dp_set_enabled(int on); const char* a2dp_reason(void); int a2dp_text(char* p,char* e); int a2dp_active(void); int a2dp_routed(void); int a2dp_route(int bt);
int hfp_enabled(void); void hfp_set_enabled(int on); int hfp_text(char* p,char* e);
int bt_srv_enabled(void); void bt_srv_set_enabled(int on);
static u64 st_bt_at=0;
static char* st_coex_plain(char* o){ extern char* rtw_coex_status(char*); static char cx[640]; rtw_coex_status(cx)[0]=0; const char* s=cx; while(*s==' ')s++; if(s[0]=='c'&&s[4]==':')s+=6;
    int act=s[0]=='a'&&s[1]=='c', lnk=0; for(int i=0;cx[i];i++) if(cx[i]=='c'&&cx[i+1]=='o'&&cx[i+2]=='n'&&cx[i+3]=='n'&&cx[i+4]=='e'&&cx[i+5]=='c'&&cx[i+6]=='t'&&cx[i+7]=='e'&&cx[i+8]=='d'){ lnk=1; break; }
    if(wifi_set_get(WSET_COEX)==0)return scopy(o,"Turned off - Wi-Fi does not make room for Bluetooth");
    if(s[0]=='n'&&s[1]=='o'&&s[2]=='t')return scopy(o,"Not needed with this Wi-Fi card");
    if(s[0]=='o'&&s[1]=='f')return scopy(o,"Turned itself off after an error - Wi-Fi uses the whole antenna");
    if(s[0]=='B')return scopy(o,"Bluetooth is off - Wi-Fi uses the whole antenna");
    if(s[0]=='W')return scopy(o,"Wi-Fi is not on");
    if(act)return scopy(o,lnk?"Wi-Fi and Bluetooth take turns - a Bluetooth device is connected":"Wi-Fi and Bluetooth take turns on the antenna");
    return scopy(o,"No status yet"); }
static char* btp_code(char* q,const btprompt_t* p){
    if(p->kind==BTP_CONFIRM){ u32 v=p->value; char d[8]; for(int i=5;i>=0;i--){ d[i]=(char)('0'+v%10); v/=10; } d[6]=0; d[7]=0; for(int i=0;i<6;i++){ *q++=d[i]; if(i==2)*q++=' '; } *q=0; return q; }
    for(int i=0;i<6;i++){ *q++=i<p->nd?p->dig[i]:'_'; if(i<5)*q++=' '; } *q=0; return q; }
void present_rect(int x,int y,int w,int h);
int bt_prompt_ui(const btprompt_t* p){
    extern int mx,my; extern volatile int mouse_btn; static int pb, sig; static u64 t0;
    int w=440,h=190,x=((int)W-w)/2,y=((int)H-h)/2, bw=120, by=y+h-50, ox=x+w-20-bw, nx=ox-12-bw;
    int s2=(int)(p->value^((u32)p->nd<<24)^((u32)p->kind<<28)); if(t0!=p->t0)pb=1; if(t0!=p->t0||s2!=sig){ t0=p->t0; sig=s2;
        char b[96],*q;
        fill_round(x-2,y-2,w+4,h+4,14,15,rgb(200,204,214)); fill_round(x,y,w,h,12,15,rgb(255,255,255));
        st_bold(x+20,y+18,"Bluetooth pairing",ST_FG);
        q=b; q=scopy(q,p->kind==BTP_CONFIRM?"Does the device show this code?  ":"Type the passkey shown on "); q=bth_addr(q,p->addr); *q=0; pdstr(x+20,y+48,b,ST_DIM,1);
        q=b; btp_code(q,p); pdstr_ctr(x+w/2,y+76,b,ST_FG,3);
        pdstr(x+20,by+6,p->kind==BTP_CONFIRM?"Y / N":"Enter / Esc",ST_DIM,1);
        fill_round(nx,by,bw,32,8,15,rgb(234,236,240)); pdstr_ctr(nx+bw/2,by+8,p->kind==BTP_CONFIRM?"No":"Cancel",rgb(40,44,52),1);
        fill_round(ox,by,bw,32,8,15,(p->kind==BTP_ENTRY&&p->nd!=6)?rgb(160,180,214):ACC); pdstr_ctr(ox+bw/2,by+8,p->kind==BTP_CONFIRM?"Yes":"Pair",rgb(255,255,255),1);
        present_rect(x-2,y-2,w+4,h+4); }
    int b=mouse_btn&1, r=0;
    if(b&&!pb){ if(my>=by&&my<by+32){ if(mx>=ox&&mx<ox+bw)r=1; else if(mx>=nx&&mx<nx+bw)r=2; } }
    pb=b; return r; }
static char* st_bt_audio_text(char* o){ char t[96]; int n=a2dp_text(t,t+sizeof t-1); t[n]=0; while(n>0&&t[n-1]=='\n')t[--n]=0; const char* s=t; if(s[0]=='B'&&n>17)s+=17;
    const char* r=a2dp_reason(); o=scopy(o,s); if(r&&r[0]){ o=scopy(o," - "); o=scopy(o,r); } if(a2dp_active())o=scopy(o,a2dp_routed()?" - sound plays here":" - sound plays on the speakers"); return o; }
static void st_bt_audio_out(int X1,int y){ if(a2dp_active())st_btn(X1-170,y+16,170,a2dp_routed()?"Use speakers":"Use headphones",2806,a2dp_routed()?0:1); }
static void st_bt_page(int cy){
    extern volatile u64 g_ticks; int X1=st_x0+st_w0-24; char b[128],*q;
    if(g_ticks-st_bt_at>100||!st_bt_at){ st_bt_at=g_ticks; btm_sync(); }
    int on=bt_hci_up(); u16 v=0,p=0; int have=btusb_ok()||btusb_listed(&v,&p);
    int y=st_card(cy,64,"Bluetooth");
    if(!have){ st_rowtext(y,"No Bluetooth adapter found","Plug in a USB Bluetooth adapter or turn on the built-in one"); cy=y+64+24; }
    else if(!on){ st_rowtext(y,"Bluetooth is off","Start the adapter, then look for devices"); st_btn(X1-120,y+16,120,"Turn on",2801,1); cy=y+64+24; }
    else { st_rowtext(y,"Bluetooth is on","Put a keyboard or mouse in pairing mode, then look for it"); st_btn(X1-160,y+16,160,"Look for devices",2800,1); cy=y+64+24; }
    { const btprompt_t* bp=bt_sec_prompt(); if(bp->on){ y=st_card(cy,64,"Pairing request"); q=b; btp_code(q,bp);
        st_rowtext(y,b,bp->kind==BTP_CONFIRM?"Choose Yes if the device shows the same code":"Type the passkey shown on the device, then press Enter");
        st_btn(X1-100,y+16,100,bp->kind==BTP_CONFIRM?"Yes":"Pair",2802,1); st_btn(X1-100-110,y+16,100,bp->kind==BTP_CONFIRM?"No":"Cancel",2803,0); cy=y+64+24; } }
    int n=btm_count(); if(n>10)n=10;
    if(n){ y=st_card(cy,n*56,"Devices");
        for(int i=0;i<n;i++){ const btmdev_t* d=btm_get(i); int ry=y+i*56; if(i)st_sep(ry);
            q=b; q=bth_addr(q,d->addr); q=scopy(q,d->le?"  LE":"  BR/EDR"); { const char* k=btm_kind(d); if(k[0]){ q=scopy(q,"  "); q=scopy(q,k); } } q=scopy(q,d->conn?"  -  connected":d->bonded?"  -  paired":""); *q=0;
            pdstr(st_x0+20,ry+10,d->name[0]?d->name:"(no name)",ST_FG,1); pdstr(st_x0+20,ry+30,b,ST_DIM,1);
            if(!on)continue;
            if(d->conn)st_btn(X1-120,ry+11,120,"Disconnect",2810+i,0);
            else if(d->bonded){ st_btn(X1-110,ry+11,110,"Forget",2870+i,2); st_btn(X1-110-120,ry+11,110,"Connect",2810+i,1); }
            else st_btn(X1-100,ry+11,100,"Pair",2840+i,1); }
        cy=y+n*56+24; }
    if(have){ static char sb[512]; bt_status_text(sb,(int)sizeof sb); char l1[120]; int j=0; for(int k=0;sb[k]&&j<100;){ int ad=sb[k]=='a'&&sb[k+1]=='d'&&sb[k+2]=='a', as=sb[k]=='a'&&sb[k+1]=='d'&&sb[k+2]=='d'; if(ad||as){ if(as&&j){ l1[j++]=','; l1[j++]=' '; } while(sb[k]&&sb[k]!=':')k++; if(sb[k])k++; while(sb[k]==' ')k++; if(as){ const char* w="address "; while(*w)l1[j++]=*w++; } while(sb[k]&&sb[k]!='\n'&&j<110)l1[j++]=sb[k++]; } while(sb[k]&&sb[k]!='\n')k++; if(sb[k])k++; } l1[j]=0; if(l1[0]>='a'&&l1[0]<='z')l1[0]-=32; while(j>0&&pw_str(l1,1)>st_w0-60)l1[--j]=0;
        char c1[120]; st_coex_plain(c1)[0]=0; j=0; while(c1[j])j++; while(j>0&&pw_str(c1,1)>st_w0-260)c1[--j]=0;
        y=st_card(cy,192,"Status"); st_rowtext(y,"Adapter",l1[0]?l1:"No details yet"); st_sep(y+64);
        st_rowtext(y+64,"Sharing with Wi-Fi",c1); st_btn(X1-170,y+80,170,"Wi-Fi settings",1000+P_WIFIADV,0); st_sep(y+128);
        { const char* w=btm_boot_why(); st_rowtext(y+128,"Reconnect paired devices at startup",w[0]?w:"Keyboards and mice you paired connect again after a restart"); st_toggle(X1-44,y+148,btm_boot_enabled(),2804); }
        cy=y+192+24;
        { char ab[160]; st_bt_audio_text(ab)[0]=0; y=st_card(cy,256,"Audio"); st_rowtext(y,"Bluetooth headphones",ab); st_toggle(X1-44,y+20,a2dp_enabled(),2805); st_sep(y+64);
          st_rowtext(y+64,"Sound output",a2dp_active()?(a2dp_routed()?"Bluetooth headphones":"Speakers (headphones stay connected)"):"Switches to headphones when they connect, back when they leave"); st_bt_audio_out(X1,y+64); st_sep(y+128);
          { char hb[128]; int hn=hfp_text(hb,hb+sizeof hb-1); hb[hn]=0; while(hn>0&&hb[hn-1]=='\n')hb[--hn]=0; st_rowtext(y+128,"Headset microphone (Hands-Free)",hn>20?hb+20:hb); st_toggle(X1-44,y+148,hfp_enabled(),2807); } st_sep(y+192);
          st_rowtext(y+192,"Let paired headsets connect to this PC",bt_srv_enabled()?"Paired, encrypted headphones and headsets can start the connection themselves":"Only this PC starts headset connections"); st_toggle(X1-44,y+212,bt_srv_enabled(),2809);
          cy=y+256+24; } }
    const char* m=btm_last(); if(m[0]){ y=st_card(cy,44,0); pdstr(st_x0+20,y+14,m,ST_DIM,1); cy=y+44+24; }
}
static void st_bt_heavy(int id){
    char a[24]; a[0]=0;
    if(id>=2810&&id<2900){ const btmdev_t* d=btm_get((id-2810)%30); if(!d)return; char* q=bth_addr(a,d->addr); if(d->le)scopy(q," le"); }
    if(id==2807){ hfp_set_enabled(!hfp_enabled()); st_bt_at=0; return; }
    if(id==2808){ extern int hfp_mic_selected(void); extern void hfp_mic_select(int); hfp_mic_select(!hfp_mic_selected()); return; }
    if(id==2809){ bt_srv_set_enabled(!bt_srv_enabled()); st_bt_at=0; return; }
    if(id==2805){ a2dp_set_enabled(!a2dp_enabled()); st_bt_at=0; return; }
    if(id==2806){ int r=a2dp_route(!a2dp_routed()); toast(!a2dp_active()?"No Bluetooth headphones connected":r?"Sound now plays on the Bluetooth headphones":"Sound now plays on the speakers"); st_bt_at=0; return; }
    if(id==2804){ btm_boot_enable(!btm_boot_enabled()); st_bt_at=0; return; }
    if(id==2802||id==2803){ bt_sec_answer(id==2802); st_bt_at=0; return; }
    if(id==2800){ int r=bt_scan(8); btm_sync(); if(r<0)toast("Bluetooth scan did not start"); else { char t[48],*q=t; q=udec(q,(u32)r); q=scopy(q," devices found"); *q=0; toast(t); } }
    else if(id==2801){ if(bt_up())btm_autoconnect(); toast(bt_hci_up()?"Bluetooth is on":"Bluetooth did not start - see the log"); }
    else if(id>=2810&&id<2840){ const btmdev_t* d=btm_get(id-2810); if(d&&d->conn)btm_disconnect(a); else btm_connect(a); toast(btm_last()); }
    else if(id>=2840&&id<2870){ btm_pair(a); toast(btm_last()); }
    else if(id>=2870&&id<2900){ btm_forget(a); toast(btm_last()); }
    st_bt_at=0;
}
