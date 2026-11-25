#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "include/nvx/drivers/platform/x86/wmi.h"

#define DELL_EV_GUID "9DBB5994-A997-11DA-B012-B622A1EF5492"
extern void hk_push(int id);
extern void toast(const char* m);
int g_dell_wmi=0; u32 g_dell_ev=0, g_dell_last=0;

int dell_wmi_hk_id(u16 type,u16 c,int acpi_bl){
    if(type==0x0000){ switch(c){ case 0x0109: return 3; case 0x0150: return 17; case 0xe005: return acpi_bl?0:14; case 0xe006: return acpi_bl?0:13;
        case 0xe00b: return 15; case 0xe011: return 18; case 0xe025: case 0xe029: return 11; case 0xe027: return 15; } return 0; }
    if(type==0x0010){ switch(c){ case 0x57: return acpi_bl?0:14; case 0x58: return acpi_bl?0:13; case 0x109: return 3; case 0x150: return 17; } return 0; }
    return 0; }
static void dell_key(u16 type,u16 c){ g_dell_ev++; g_dell_last=((u32)type<<16)|c; int id=dell_wmi_hk_id(type,c,g_bl.ok&&g_bl.out); if(id)hk_push(id); else if(type==0x0000&&c==0xe007)toast("Battery health button"); }
int dell_wmi_decode(const u8* p,u32 len,int v0,void (*key)(u16,u16)){
    u32 n=len/2, end=n, i=0; int keys=0; if(!p||!n)return 0;
    #define W(k) ((u16)(p[2*(k)]|(p[2*(k)+1]<<8)))
    if(v0&&(u32)W(0)+1<end)end=(u32)W(0)+1;
    for(int guard=0;i<end&&guard<16;guard++){ u32 l=W(i); if(!l)break; l++; if(i+l>end)break; u16 t=W(i+1);
        if((t==0x0000||t==0x0012)&&l>2){ key(t==0x0012?0x0000:t,W(i+2)); keys++; }
        else if(t==0x0010||t==0x0011) for(u32 k=2;k<l;k++){ key(t,W(i+k)); keys++; }
        i+=l; }
    #undef W
    return keys; }
static void dell_cb(const wmi_blk* b,u32 nid,aobj* d){ (void)b; (void)nid; if(!d||d->type!=AT_BUF||!d->p)return; dell_wmi_decode(d->p,d->len,0,dell_key); }
void dell_wmi_init(void){
    if(!wmi_has(DELL_EV_GUID))return;
    if(wmi_on_event(DELL_EV_GUID,dell_cb))return;
    g_dell_wmi=1; klog("dell-wmi: hotkey events on"); }
