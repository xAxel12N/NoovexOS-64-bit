#include <stdio.h>
#include <string.h>
#include "include/nvx/base.h"
#include "net/bluetooth/bt_mgr.h"
#include "net/bluetooth/bt_sec.h"
#define W 1280u
#define H 800u
#define ST_FG 1u
#define ST_DIM 2u
#define ACC 3u
#define P_WIFIADV 7
static int checks, fails;
#define CHECK(c,msg) do{ checks++; if(c)printf("  %-66s ok\n",msg); else { fails++; printf("  %-66s FAIL (line %d)\n",msg,__LINE__); } }while(0)
char* scopy(char* p,const char* s){ while(*s)*p++=*s++; *p=0; return p; }
char* udec(char* p,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*p++=t[--n]; *p=0; return p; }
int mx,my; volatile int mouse_btn; volatile u64 g_ticks=1000;
int st_x0=200, st_w0=900;
static struct { int x,y,w,id,kind; char t[48]; } bt_b[64]; static int bt_nb;
static struct { int id,on; } bt_tg[16]; static int bt_ntg;
static char bt_txt[64][160]; static int bt_nt;
static char last_toast[128]; static int presents;
static void rec_txt(const char* s){ if(bt_nt<64&&s){ strncpy(bt_txt[bt_nt],s,159); bt_txt[bt_nt][159]=0; bt_nt++; } }
static int st_card(int y,int h,const char* t){ (void)h; rec_txt(t); return y+40; }
static void st_rowtext(int y,const char* t,const char* d){ (void)y; rec_txt(t); rec_txt(d); }
static void st_btn(int x,int y,int w,const char* t,int id,int kind){ if(bt_nb<64){ bt_b[bt_nb].x=x; bt_b[bt_nb].y=y; bt_b[bt_nb].w=w; bt_b[bt_nb].id=id; bt_b[bt_nb].kind=kind; strncpy(bt_b[bt_nb].t,t,47); bt_b[bt_nb].t[47]=0; bt_nb++; } }
static void st_sep(int y){ (void)y; }
static void st_toggle(int x,int y,int on,int id){ (void)x; (void)y; if(bt_ntg<16){ bt_tg[bt_ntg].id=id; bt_tg[bt_ntg].on=on; bt_ntg++; } }
static void st_bold(int x,int y,const char* s,u32 c){ (void)x; (void)y; (void)c; rec_txt(s); }
static void pdstr(int x,int y,const char* s,u32 c,int sc){ (void)x; (void)y; (void)c; (void)sc; rec_txt(s); }
static void pdstr_ctr(int x,int y,const char* s,u32 c,int sc){ (void)x; (void)y; (void)c; (void)sc; rec_txt(s); }
static int pw_str(const char* s,int sc){ (void)sc; return (int)strlen(s)*8; }
static u32 rgb(int r,int g,int b){ return (u32)(r<<16|g<<8|b); }
static void fill_round(int x,int y,int w,int h,int r,int m,u32 c){ (void)x; (void)y; (void)w; (void)h; (void)r; (void)m; (void)c; }
static void toast(const char* s){ strncpy(last_toast,s,127); }
void present_rect(int x,int y,int w,int h){ (void)x; (void)y; (void)w; (void)h; presents++; }
int btusb_ok(void){ return 1; }
int btusb_listed(u16* v,u16* p){ *v=1; *p=2; return 1; }
int bt_status_text(char* o,int cap){ (void)cap; scopy(o,"adapter: USB 8087:0026 (claimed)\naddress: 00:11:22:33:44:55\n"); return 1; }
char* rtw_coex_status(char* o){ return scopy(o,"coex: not needed"); }
int wifi_set_get(int f){ (void)f; return 1; }
int bt_hci_up(void){ return 1; }
int bt_up(void){ return 1; }
int bt_scan(int s){ (void)s; return 0; }
void btm_sync(void){}
int btm_count(void){ return 0; }
const btmdev_t* btm_get(int i){ (void)i; return 0; }
const char* btm_kind(const btmdev_t* d){ (void)d; return ""; }
const char* btm_last(void){ return ""; }
int btm_pair(const char* s){ (void)s; return 0; }
int btm_connect(const char* s){ (void)s; return 0; }
int btm_disconnect(const char* s){ (void)s; return 0; }
int btm_forget(const char* s){ (void)s; return 0; }
int btm_autoconnect(void){ return 0; }
static int boot_on=1; int btm_boot_enabled(void){ return boot_on; } void btm_boot_enable(int on){ boot_on=on; } const char* btm_boot_why(void){ return ""; }
static btprompt_t pr; static int answered=-1;
const btprompt_t* bt_sec_prompt(void){ return &pr; }
void bt_sec_answer(int yes){ answered=yes; pr.on=0; }
static int a_en=1, a_act, a_rt, route_calls;
static int s_en=1; int bt_srv_enabled(void){ return s_en; } void bt_srv_set_enabled(int on){ s_en=on; }
static int h_en=1; int hfp_enabled(void){ return h_en; } void hfp_set_enabled(int on){ h_en=on; } int hfp_text(char* p,char* e){ const char* t=h_en?"Headset microphone: no Hands-Free headset connected\n":"Headset microphone: off\n"; char* s=p; while(*t&&p<e)*p++=*t++; return (int)(p-s); }
int a2dp_enabled(void){ return a_en; }
void a2dp_set_enabled(int on){ a_en=on; if(!on)a_act=a_rt=0; }
const char* a2dp_reason(void){ return a_en?"":"turned off after repeated failures"; }
int a2dp_text(char* p,char* e){ const char* t=!a_en?"Bluetooth audio: off\n":a_act?"Bluetooth audio: streaming (SBC 48 kHz)\n":"Bluetooth audio: no headphones connected\n"; char* s=p; while(*t&&p<e)*p++=*t++; return (int)(p-s); }
int a2dp_active(void){ return a_act; }
int a2dp_routed(void){ return a_act&&a_rt; }
int a2dp_route(int bt){ route_calls++; if(!a_act)return 0; a_rt=bt; return a_rt; }
#include "apps/system/settings/bluetooth.c"
static void page(void){ bt_nb=bt_ntg=bt_nt=0; st_bt_page(100); }
static int btn(int id,const char* t){ for(int i=0;i<bt_nb;i++) if(bt_b[i].id==id&&(!t||!strcmp(bt_b[i].t,t)))return i+1; return 0; }
static int txt(const char* s){ for(int i=0;i<bt_nt;i++) if(strstr(bt_txt[i],s))return 1; return 0; }
static int tg(int id){ for(int i=0;i<bt_ntg;i++) if(bt_tg[i].id==id)return bt_tg[i].on; return -1; }
int main(void){
    page(); CHECK(!btn(2802,0)&&!btn(2803,0)&&!txt("Pairing request"),"no pairing card while no prompt is open");
    pr.on=1; pr.kind=BTP_CONFIRM; pr.value=123456; page();
    CHECK(txt("Pairing request")&&txt("123 456")&&btn(2802,"Yes")&&btn(2803,"No"),"confirm prompt: card shows 123 456 with Yes (2802) and No (2803)");
    st_bt_heavy(2802); CHECK(answered==1&&!pr.on,"Settings Yes answers the prompt with yes");
    pr.on=1; pr.kind=BTP_ENTRY; pr.nd=3; memcpy(pr.dig,"123",4); answered=-1; page();
    CHECK(txt("1 2 3 _ _ _")&&btn(2802,"Pair")&&btn(2803,"Cancel"),"passkey prompt: typed digits and blanks, Pair / Cancel");
    st_bt_heavy(2803); CHECK(answered==0,"Settings Cancel answers the prompt with no");
    pr.on=1; pr.kind=BTP_CONFIRM; pr.value=42; pr.t0=7; bt_nt=0; presents=0;
    int w=440,h=190,x=((int)W-w)/2,y=((int)H-h)/2, bw=120, by=y+h-50, ox=x+w-20-bw, nx=ox-12-bw;
    mouse_btn=0; int r0=bt_prompt_ui(&pr);
    CHECK(r0==0&&presents==1&&txt("Bluetooth pairing")&&txt("000 042"),"dialog draws once with the zero-padded code, no answer without a click");
    bt_prompt_ui(&pr); CHECK(presents==1,"dialog is not redrawn when nothing changed");
    mx=ox+5; my=by+5; mouse_btn=1; int r1=bt_prompt_ui(&pr); int r1b=bt_prompt_ui(&pr); mouse_btn=0; bt_prompt_ui(&pr);
    CHECK(r1==1&&r1b==0,"click on Yes returns 1 once (edge, not level)");
    mx=nx+5; mouse_btn=1; int r2=bt_prompt_ui(&pr); mouse_btn=0; bt_prompt_ui(&pr);
    CHECK(r2==2,"click on No returns 2");
    mx=x+5; my=y+5; mouse_btn=1; int r3=bt_prompt_ui(&pr); mouse_btn=0;
    CHECK(r3==0,"click outside the buttons does nothing");
    pr.on=0; a_en=1; a_act=0; page();
    CHECK(tg(2805)==1&&!btn(2806,0)&&txt("Bluetooth headphones")&&txt("Switches to headphones when they connect"),"audio card: toggle on, no output button without headphones");
    a_act=1; a_rt=1; page();
    CHECK(btn(2806,"Use speakers")&&txt("sound plays here"),"headphones streaming: output shown as headphones, button Use speakers");
    st_bt_heavy(2806); page();
    CHECK(route_calls==1&&!a_rt&&btn(2806,"Use headphones")&&txt("Speakers (headphones stay connected)"),"Use speakers routes sound back to the speakers");
    st_bt_heavy(2806); CHECK(a_rt&&strstr(last_toast,"Bluetooth headphones"),"Use headphones routes it to the headphones again");
    st_bt_heavy(2805); page(); CHECK(!a_en&&tg(2805)==0&&txt("off"),"toggle 2805 turns Bluetooth audio off");
    st_bt_heavy(2805); CHECK(a_en,"toggle 2805 turns it back on");
    page(); CHECK(tg(2807)==1&&txt("Headset microphone (Hands-Free)")&&txt("no Hands-Free headset connected"),"audio card: headset microphone row with toggle 2807 on");
    st_bt_heavy(2807); page(); CHECK(!h_en&&tg(2807)==0,"toggle 2807 turns the headset microphone off"); st_bt_heavy(2807); CHECK(h_en,"toggle 2807 turns it back on");
    page(); CHECK(tg(2809)==1&&txt("Let paired headsets connect to this PC"),"audio card: incoming headset connections row with toggle 2809 on"); st_bt_heavy(2809); page(); CHECK(!s_en&&tg(2809)==0&&txt("Only this PC starts headset connections"),"toggle 2809 turns incoming connections off"); st_bt_heavy(2809); CHECK(s_en,"toggle 2809 turns them back on");
    printf("%d of %d checks passed\n",checks-fails,checks);
    if(!fails)printf("BT UI: ALL PASS\n");
    return fails?1:0;
}
