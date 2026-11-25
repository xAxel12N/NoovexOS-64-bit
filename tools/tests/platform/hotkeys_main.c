#include <stdio.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;
#define NVX_BASE_H
static int acts[64], nact, bt=-1, wifi=-1, fails, checks; static char tst[64];
void hkact(int a){ acts[nact++]=a; }
void hkact_bt(int on){ bt=on; }
void hkact_wifi(int on){ wifi=on; }
void toast(const char* m){ strcpy(tst,m); }
#include "drivers/platform/x86/thinkpad.c"
#include "drivers/platform/x86/asus_wmi.c"
#define CK(c,m) do{ checks++; if(c)printf("  %s ok\n",m); else { fails++; printf("  FAIL %s\n",m); } }while(0)
static int one(int (*f)(u32),u32 c){ nact=0; int r=f(c); return r&&nact==1?acts[0]:r?0:-1; }
int main(void){
    CK(one(thinkpad_hkey,0x1010)==HKA_BRI_UP&&one(thinkpad_hkey,0x1011)==HKA_BRI_DN,"ThinkPad Fn+Home/End: brightness");
    CK(one(thinkpad_hkey,0x1015)==HKA_VOL_UP&&one(thinkpad_hkey,0x1016)==HKA_VOL_DN&&one(thinkpad_hkey,0x1017)==HKA_MUTE,"ThinkPad volume up/down/mute (0x1015-0x1017)");
    CK(one(thinkpad_hkey,0x101b)==HKA_MICMUTE,"ThinkPad mic mute 0x101b");
    CK(one(thinkpad_hkey,0x1005)==HKA_AIRPLANE&&one(thinkpad_hkey,0x1007)==HKA_DISPLAY&&one(thinkpad_hkey,0x1004)==HKA_SLEEP&&one(thinkpad_hkey,0x1002)==HKA_LOCK,"ThinkPad Fn+F2/F4/F5/F7");
    CK(one(thinkpad_hkey,0x1006)==HKA_CAMERA&&one(thinkpad_hkey,0x1008)==HKA_TOUCHPAD&&one(thinkpad_hkey,0x1020)==HKA_FILES,"ThinkPad camera, touchpad, file key");
    CK(one(thinkpad_hkey,0x1105)==HKA_SCREENSHOT,"ThinkPad adaptive keyboard clipping tool");
    CK(one(thinkpad_hkey,0x1312)==HKA_SCREENSHOT&&one(thinkpad_hkey,0x1313)==HKA_CALC&&one(thinkpad_hkey,0x1314)==HKA_BT,"ThinkPad 2017+ extended keys: snip, calculator, Bluetooth");
    nact=0; CK(thinkpad_hkey(0x1012)==0&&nact==0,"ThinkLight key left to laptop.c (firmware changes the level)");
    CK(one(thinkpad_hkey,0x1018)==0&&one(thinkpad_hkey,0x1103)==0&&one(thinkpad_hkey,0x1319)==0,"known ThinkPad codes without an action are claimed silently");
    CK(one(thinkpad_hkey,0x6011)==0&&!strcmp(tst,"ThinkPad: running hot"),"thermal event toasts");
    CK(one(thinkpad_hkey,0x1400)==-1&&one(thinkpad_hkey,0x1000)==-1&&one(thinkpad_hkey,0x131a)==-1,"unknown ThinkPad codes not claimed, no table overrun");
    CK(one(asus_wmi_key,0x10)==HKA_BRI_UP&&one(asus_wmi_key,0x1F)==HKA_BRI_UP&&one(asus_wmi_key,0x20)==HKA_BRI_DN&&one(asus_wmi_key,0x2E)==HKA_BRI_DN&&one(asus_wmi_key,0x2F)==HKA_BRI_UP&&one(asus_wmi_key,0x29)==HKA_BRI_DN,"ASUS brightness ranges (0x2a-0x2d are other keys)");
    CK(one(asus_wmi_key,0x30)==HKA_VOL_UP&&one(asus_wmi_key,0x31)==HKA_VOL_DN&&one(asus_wmi_key,0x32)==HKA_MUTE&&one(asus_wmi_key,0x7c)==HKA_MICMUTE,"ASUS volume, mute, mic mute");
    CK(one(asus_wmi_key,0xC4)==HKA_KBD_UP&&one(asus_wmi_key,0xC5)==HKA_KBD_DN&&one(asus_wmi_key,0xC7)==HKA_KBD_CYCLE,"ASUS keyboard light keys go to the action layer");
    CK(one(asus_wmi_key,0x6B)==HKA_TOUCHPAD&&one(asus_wmi_key,0x88)==HKA_AIRPLANE&&one(asus_wmi_key,0x5D)==HKA_AIRPLANE,"ASUS touchpad, radio, wireless console");
    CK(one(asus_wmi_key,0x61)==HKA_DISPLAY&&one(asus_wmi_key,0x93)==HKA_DISPLAY&&one(asus_wmi_key,0xA7)==HKA_DISPLAY&&one(asus_wmi_key,0x2d)==HKA_DISPLAY,"ASUS display switch codes");
    CK(one(asus_wmi_key,0x45)==HKA_PLAY&&one(asus_wmi_key,0x41)==HKA_NEXT&&one(asus_wmi_key,0x40)==HKA_PREV&&one(asus_wmi_key,0x43)==HKA_STOP,"ASUS media keys");
    CK(one(asus_wmi_key,0x2a)==HKA_SCREENSHOT&&one(asus_wmi_key,0x55)==HKA_CALC&&one(asus_wmi_key,0x82)==HKA_CAMERA&&one(asus_wmi_key,0x35)==HKA_LOCK,"ASUS screenshot, calculator, camera, lock");
    bt=-1; CK(one(asus_wmi_key,0x7D)==0&&bt==1&&one(asus_wmi_key,0x7E)==0&&bt==0,"ASUS Bluetooth enable/disable set the state");
    CK(one(asus_wmi_key,0x2b)==0&&one(asus_wmi_key,0xC0)==0&&one(asus_wmi_key,0x5E)==0&&wifi==-1,"ASUS notifications claimed without action");
    CK(one(asus_wmi_key,0x01)==-1&&one(asus_wmi_key,0xFF)==-1,"unknown ASUS codes not claimed");
    printf(fails?"%d of %d FAILED\n":"ALL PASS (%d checks)\n",fails?fails:checks,checks); return fails!=0; }
