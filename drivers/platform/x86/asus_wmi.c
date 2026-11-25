#include "include/nvx/base.h"
#include "include/nvx/drivers/platform/hkact.h"

extern void toast(const char* m);
u32 g_asusk_n=0, g_asusk_last=0;

int asus_wmi_hk_id(u32 c){
    if((c>=0x10&&c<=0x1F)||c==0x2F)return HKA_BRI_UP;
    if((c>=0x20&&c<=0x29)||c==0x2E)return HKA_BRI_DN;
    if((c>=0x61&&c<=0x67)||(c>=0x8C&&c<=0x93)||(c>=0xA0&&c<=0xA7))return HKA_DISPLAY;
    switch(c){
    case 0x2a: return HKA_SCREENSHOT; case 0x2d: return HKA_DISPLAY; case 0x30: return HKA_VOL_UP; case 0x31: return HKA_VOL_DN; case 0x32: return HKA_MUTE;
    case 0x33: case 0x35: return HKA_LOCK; case 0x40: return HKA_PREV; case 0x41: return HKA_NEXT; case 0x43: return HKA_STOP; case 0x45: return HKA_PLAY;
    case 0x51: return HKA_BROWSER; case 0x55: case 0xB5: return HKA_CALC; case 0x5D: case 0x88: return HKA_AIRPLANE; case 0x6B: return HKA_TOUCHPAD;
    case 0x7c: return HKA_MICMUTE; case 0x82: case 0x85: return HKA_CAMERA; case 0xC4: return HKA_KBD_UP; case 0xC5: return HKA_KBD_DN; case 0xC7: return HKA_KBD_CYCLE; }
    return 0; }
int asus_wmi_key(u32 c){
    g_asusk_n++; g_asusk_last=c;
    int a=asus_wmi_hk_id(c); if(a){ hkact(a); return 1; }
    switch(c){
    case 0x7D: case 0x7E: hkact_bt(c==0x7D); return 1;
    case 0x2b: case 0x2c: case 0x5E: case 0x5F: case 0x57: case 0x58: case 0x60: case 0x6E: case 0x79: case 0x7B: case 0xC0: case 0xC6: case 0xCF: return 1;
    case 0xFA: case 0xBD: toast("Lid flipped"); return 1; }
    return 0; }
