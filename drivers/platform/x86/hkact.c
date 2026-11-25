#include "include/nvx/base.h"
#include "include/nvx/drivers/platform/hkact.h"

extern int g_airplane, g_lap_kind, g_lap_kbd, g_lap_kbdmax;
extern void laptop_kbd_set(int);
extern int g_wm_state, g_wm_up;
extern void osd_show(int kind,int val,const char* txt);
extern void toast(const char* m);
extern void hk_push(int id);
extern int bl_cur_pct(void);
extern void bl_step(int dir);
extern void wifi_user_power(int on);
int amd_backlight_set(int pct);
int amd_backlight_get(void);
__attribute__((weak)) int bt_hci_up(void);
__attribute__((weak)) int bt_hci_cmd(u16 op,const u8* p,int n,u8* rp,int rcap,int ms);
int g_hka_osd=1, g_hka_events=0, g_hka_last=0, g_hka_bt_off=0;
static int hka_scan=-1;
static void (*hka_kbd_set)(int);

static void hka_osd(int k,int v,const char* t){ if(g_hka_osd)osd_show(k,v,t); }
int hkact_bri_get(void){ int b=bl_cur_pct(); if(b>=0)return b; { b=amd_backlight_get(); if(b>=0&&b<=100)return b; } return -1; }
int hkact_bri_step(int dir){
    if(bl_cur_pct()>=0){ bl_step(dir); return bl_cur_pct(); }
    { int b=amd_backlight_get(); if(b<0||b>100)return -1; b+=dir*10; if(b<5)b=5; if(b>100)b=100; if(amd_backlight_set(b))return -1; return b; }
    return -1; }
static int bt_scan_rd(void){ u8 rp[4]={0}; if(!bt_hci_cmd||bt_hci_cmd(0x0C19,0,0,rp,(int)sizeof rp,500)<2||rp[0])return -1; return rp[1]; }
static int bt_scan_wr(int v){ u8 b=(u8)v, rp[4]={0}; return bt_hci_cmd&&bt_hci_cmd(0x0C1A,&b,1,rp,(int)sizeof rp,500)>=1&&!rp[0]; }
void hkact_bt(int on){
    if(!bt_hci_up||!bt_hci_up()){ g_hka_bt_off=!on; return; }
    if(!on&&!g_hka_bt_off){ hka_scan=bt_scan_rd(); if(hka_scan>0)bt_scan_wr(0); g_hka_bt_off=1; }
    else if(on&&g_hka_bt_off){ if(hka_scan>0)bt_scan_wr(hka_scan); hka_scan=-1; g_hka_bt_off=0; } }
void hkact_wifi(int on){ if(!g_airplane&&g_wm_up)wifi_user_power(on); }
void hkact_airplane(int on){ on=on?1:0; if(on==g_airplane)return; g_airplane=on; hkact_bt(!on); hka_osd(4,-1,on?"Airplane mode on":"Airplane mode off"); }
void hkact_kbd_register(int max,int cur,void (*set)(int)){ if(g_lap_kind||max<=0||!set)return; hka_kbd_set=set; g_lap_kbdmax=max; g_lap_kbd=cur<0?0:cur>max?max:cur; }
void hkact_kbd_report(int lvl){ if(g_lap_kbdmax<=0)return; g_lap_kbd=lvl<0?0:lvl>g_lap_kbdmax?g_lap_kbdmax:lvl; toast(g_lap_kbd?"Keyboard light on":"Keyboard light off"); }
int hkact_kbd_set_cb(int lvl){ if(g_lap_kind||!hka_kbd_set)return 0; hka_kbd_set(lvl); g_lap_kbd=lvl; return 1; }
void hkact_kbd_step(int dir){
    if(g_lap_kbdmax<=0)return;
    int l=(g_lap_kbd<0?0:g_lap_kbd)+(dir?dir:1);
    if(l>g_lap_kbdmax)l=dir<0?g_lap_kbdmax:0;
    if(l<0)l=0;
    laptop_kbd_set(l); toast(l?"Keyboard light on":"Keyboard light off"); }
void hkact(int a){
    if(a<=HKA_NONE||a>=HKA_MAX)return;
    g_hka_events++; g_hka_last=a;
    switch(a){
    case HKA_BRI_UP: case HKA_BRI_DN: { int b=hkact_bri_step(a==HKA_BRI_UP?1:-1); if(b>=0)hka_osd(2,b,"Brightness"); } break;
    case HKA_VOL_UP: hk_push(1); break; case HKA_VOL_DN: hk_push(2); break; case HKA_MUTE: hk_push(3); break;
    case HKA_PLAY: hk_push(4); break; case HKA_NEXT: hk_push(5); break; case HKA_PREV: hk_push(6); break; case HKA_STOP: hk_push(7); break;
    case HKA_CALC: hk_push(8); break; case HKA_BROWSER: hk_push(9); break; case HKA_FILES: hk_push(11); break; case HKA_SLEEP: hk_push(12); break;
    case HKA_DISPLAY: hk_push(15); break; case HKA_CAMERA: hk_push(16); break; case HKA_MICMUTE: hk_push(17); break;
    case HKA_AIRPLANE: hkact_airplane(!g_airplane); break;
    case HKA_WIFI: if(g_wm_up&&!g_airplane){ int on=g_wm_state==0; hkact_wifi(on); hka_osd(4,-1,on?"Wi-Fi on":"Wi-Fi off"); } break;
    case HKA_BT: if(!g_airplane){ hkact_bt(g_hka_bt_off); hka_osd(4,-1,g_hka_bt_off?"Bluetooth off":"Bluetooth on"); } break;
    case HKA_KBD_UP: hkact_kbd_step(1); break; case HKA_KBD_DN: hkact_kbd_step(-1); break; case HKA_KBD_CYCLE: hkact_kbd_step(0); break;
    case HKA_TOUCHPAD: hk_push(19); break; case HKA_LOCK: hk_push(20); break; case HKA_SCREENSHOT: hk_push(28); break; } }
