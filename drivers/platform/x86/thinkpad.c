#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/drivers/platform/hkact.h"

extern void toast(const char* m);
u32 g_tp_hkey_n=0, g_tp_hkey_last=0;

static const u8 tp_orig[32]={0,HKA_LOCK,0,HKA_SLEEP,HKA_AIRPLANE,HKA_CAMERA,HKA_DISPLAY,HKA_TOUCHPAD,0,0,0,HKA_SLEEP,0,0,0,HKA_BRI_UP,HKA_BRI_DN,0,0,0,
    HKA_VOL_UP,HKA_VOL_DN,HKA_MUTE,0,0,0,HKA_MICMUTE,0,0,0,0,HKA_FILES};
static const u8 tp_adapt[20]={0,0,HKA_SCREENSHOT};
static const u8 tp_ext[26]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,HKA_SCREENSHOT,HKA_CALC,HKA_BT};

int thinkpad_hk_id(u32 c){
    if(c>=0x1001&&c<=0x1020)return tp_orig[c-0x1001];
    if(c>=0x1103&&c<0x1103+sizeof tp_adapt)return tp_adapt[c-0x1103];
    if(c>=0x1300&&c<0x1300+sizeof tp_ext)return tp_ext[c-0x1300];
    return 0; }
int thinkpad_hkey(u32 c){
    g_tp_hkey_n++; g_tp_hkey_last=c;
    if(c==0x1012)return 0;
    int a=thinkpad_hk_id(c); if(a){ hkact(a); return 1; }
    if(c>=0x1001&&c<=0x1020)return 1;
    if(c>=0x1103&&c<0x1103+sizeof tp_adapt)return 1;
    if(c>=0x1300&&c<0x1300+sizeof tp_ext)return 1;
    switch(c){
    case 0x131b: toast("Camera shutter changed"); return 1;
    case 0x6011: case 0x6021: toast("ThinkPad: running hot"); return 1;
    case 0x6012: case 0x6022: toast("ThinkPad: critically hot, save your work"); return 1;
    case 0x7000: toast("Wireless switch changed"); return 1;
    case 0x6060: case 0x6000: case 0x6005: case 0x5010: case 0x6030: case 0x6032: case 0x60f0: case 0x6040: case 0x60b0: case 0x60b1: case 0x5001: case 0x5002: return 1; }
    return 0; }
