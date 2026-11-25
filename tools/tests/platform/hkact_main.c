#include <stdio.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;
#define NVX_BASE_H
int g_airplane, g_lap_kind, g_lap_kbd=-1, g_lap_kbdmax, g_wm_state=3, g_wm_up=1;
static int osd_k, osd_v, pushes[32], npush, bl=-1, amd=-1, amd_ok, wifi_pw=-1, bt_up, scan=3, scan_wr=-1, kset=-1, fails, checks;
static char osd_t[64], tst[64];
void osd_show(int k,int v,const char* t){ osd_k=k; osd_v=v; strcpy(osd_t,t); }
void toast(const char* m){ strcpy(tst,m); }
void hk_push(int id){ pushes[npush++]=id; }
int bl_cur_pct(void){ return bl; }
void bl_step(int d){ bl+=d*10; if(bl>100)bl=100; if(bl<0)bl=0; }
void wifi_user_power(int on){ wifi_pw=on; g_wm_state=on?1:0; }
void laptop_kbd_set(int l){ if(g_lap_kbdmax<=0)return; if(!g_lap_kind){ extern int hkact_kbd_set_cb(int); hkact_kbd_set_cb(l); } g_lap_kbd=l; }
int amd_backlight_set(int p){ if(!amd_ok)return -1; amd=p; return 0; }
int amd_backlight_get(void){ return amd_ok?amd:-1; }
int bt_hci_up(void){ return bt_up; }
int bt_hci_cmd(u16 op,const u8* p,int n,u8* rp,int rc,int ms){ (void)n; (void)rc; (void)ms; if(op==0x0C19){ rp[0]=0; rp[1]=(u8)scan; return 2; } if(op==0x0C1A){ scan_wr=p[0]; scan=p[0]; rp[0]=0; return 1; } return -1; }
#include "../../../drivers/platform/x86/hkact.c"
#define CK(c,m) do{ checks++; if(c)printf("%s ok\n",m); else { fails++; printf("FAIL %s\n",m); } }while(0)
static void kcb(int l){ kset=l; }
int main(void){
    hkact(HKA_BRI_UP); CK(osd_k==0&&g_hka_events==1,"no backlight: no OSD, counted");
    amd_ok=1; amd=50; hkact(HKA_BRI_UP); CK(amd==60&&osd_k==2&&osd_v==60,"AMD fallback up");
    for(int i=0;i<10;i++)hkact(HKA_BRI_DN); CK(amd==5,"AMD clamps at 5%");
    bl=40; hkact(HKA_BRI_UP); CK(bl==50&&amd==5&&osd_v==50,"ACPI/Intel path wins over AMD");
    npush=0; hkact(HKA_VOL_UP); hkact(HKA_VOL_DN); hkact(HKA_MUTE); hkact(HKA_MICMUTE); hkact(HKA_TOUCHPAD);
    CK(npush==5&&pushes[0]==1&&pushes[1]==2&&pushes[2]==3&&pushes[3]==17&&pushes[4]==19,"volume/mute/mic/touchpad queued to hk_do ids");
    hkact(0); hkact(HKA_MAX); hkact(-3); CK(npush==5,"out of range ignored");
    bt_up=1; scan=3; hkact(HKA_AIRPLANE); CK(g_airplane==1&&g_hka_bt_off==1&&scan==0&&osd_k==4&&!strcmp(osd_t,"Airplane mode on"),"airplane on: BT scan off");
    hkact(HKA_WIFI); CK(wifi_pw==-1,"Wi-Fi stays off in airplane mode");
    hkact(HKA_BT); CK(g_hka_bt_off==1,"BT key ignored in airplane mode");
    hkact(HKA_AIRPLANE); CK(g_airplane==0&&g_hka_bt_off==0&&scan==3,"airplane off: BT scan restored");
    hkact_airplane(0); CK(g_airplane==0,"idempotent");
    scan_wr=-1; scan=0; hkact_airplane(1); hkact_airplane(0); CK(scan_wr==-1,"BT scan never written when it was already off");
    hkact(HKA_BT); CK(g_hka_bt_off==1&&!strcmp(osd_t,"Bluetooth off"),"BT key off"); hkact(HKA_BT); CK(g_hka_bt_off==0,"BT key on");
    g_wm_state=3; hkact(HKA_WIFI); CK(wifi_pw==0&&!strcmp(osd_t,"Wi-Fi off"),"Wi-Fi key off"); hkact(HKA_WIFI); CK(wifi_pw==1,"Wi-Fi key on");
    bt_up=0; hkact_airplane(1); CK(g_hka_bt_off==1,"no BT: state only"); hkact_airplane(0);
    hkact(HKA_KBD_UP); CK(kset==-1&&g_lap_kbdmax==0,"no kbd light: nothing");
    g_lap_kind=1; hkact_kbd_register(3,1,kcb); CK(g_lap_kbdmax==0,"register refused when laptop.c owns kbd"); g_lap_kind=0;
    hkact_kbd_register(3,1,kcb); CK(g_lap_kbdmax==3&&g_lap_kbd==1,"register");
    hkact(HKA_KBD_UP); hkact(HKA_KBD_UP); CK(kset==3&&g_lap_kbd==3,"kbd up"); hkact(HKA_KBD_UP); CK(kset==0&&!strcmp(tst,"Keyboard light off"),"kbd up wraps to off");
    hkact(HKA_KBD_DN); CK(kset==0,"kbd down floors at 0"); hkact(HKA_KBD_CYCLE); CK(kset==1,"kbd cycle");
    hkact_kbd_report(9); CK(g_lap_kbd==3,"firmware-reported level clamped");
    g_hka_osd=0; osd_k=0; hkact(HKA_AIRPLANE); CK(osd_k==0&&g_airplane==1,"OSD can be silenced");
    printf("%d checks, %d failed\n%s\n",checks,fails,fails?"FAILED":"ALL PASS"); return fails?1:0; }
