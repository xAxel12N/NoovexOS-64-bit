#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/drivers/acpi/aml.h"
#include "include/nvx/drivers/platform/x86/wmi.h"

#define HPW_EVENT "95F24279-4D7B-4334-9387-ACCDC67EF61C"
#define HPW_BIOS "5FB7F034-2C63-45E9-BE91-3D44E2C707E4"
enum { HPQ_WIRELESS=0x05, HPQ_HOTKEY=0x0C };
enum { HPE_BEZEL=0x04, HPE_WIRELESS=0x05, HPE_KBD_LIGHT=0x0D, HPE_CAMERA=0x1A, HPE_OMEN=0x1D };
int g_hpw_ok=0, g_hpw_wifi=-1, g_hpw_bt=-1; u32 g_hpw_events=0, g_hpw_last=0, g_hpw_lastkey=0;
static anode* g_hpw_rf;

static void osd(int k,int v,const char* t){ extern void osd_show(int,int,const char*); osd_show(k,v,t); }
static void push(int id){ extern void hk_push(int); hk_push(id); }
static int hp_bios(u32 cmd,u32 q,const u8* in,u32 insz,u8* out,u32 outsz){
    u32 mid=outsz==0?1:outsz<=4?2:outsz<=128?3:outsz<=1024?4:5; u32 n=16+insz; u8* b=(u8*)kmalloc(n); if(!b)return -1;
    u32 h[4]={0x55434553u,cmd,q,insz}; for(int k=0;k<16;k++)b[k]=(u8)(h[k>>2]>>(8*(k&3))); for(u32 k=0;k<insz;k++)b[16+k]=in[k];
    aobj o; int r=wmi_method(HPW_BIOS,0,mid,b,n,&o); kfree(b);
    if(r||o.type!=AT_BUF||!o.p||o.len<8){ aobj_free(&o); return -1; }
    u32 rc=o.p[4]|(u32)o.p[5]<<8|(u32)o.p[6]<<16|(u32)o.p[7]<<24;
    if(!rc)for(u32 k=0;k<outsz;k++)out[k]=8+k<o.len?o.p[8+k]:0; aobj_free(&o); return rc?-(int)rc:0; }
static int hp_read_int(u32 q,u32* v){ u8 in[4]={0},o[4]; if(hp_bios(1,q,in,4,o,4))return -1; *v=o[0]|(u32)o[1]<<8|(u32)o[2]<<16|(u32)o[3]<<24; return 0; }
static void hp_radio_state(int show){
    u32 w; if(hp_read_int(HPQ_WIRELESS,&w))return; int wf=!(w&0x200)||!(w&0x800), bt=(w&0x10000)?(!(w&0x20000)||!(w&0x80000)):1;
    int ch=g_hpw_wifi>=0&&wf!=g_hpw_wifi; g_hpw_wifi=wf; g_hpw_bt=bt;
    if(show||ch){ extern int g_airplane; g_airplane=wf&&bt; osd(4,-1,wf?"Wireless off":"Wireless on"); } }
static void hp_key(u32 kc){
    g_hpw_lastkey=kc; extern void klog_key(u32); klog_key(0x70000u|(kc&0xFFFF));
    switch(kc){ case 0x02: push(13); break; case 0x03: push(14); break; case 0x270: push(17); break;
        case 0x21A9: case 0x121A9: push(19); break; case 0x2169: osd(6,-1,"Rotate display"); break; default: break; } }
static void hp_ev(const wmi_blk* b,u32 nid,aobj* d){
    (void)b; (void)nid; if(d->type!=AT_BUF||!d->p||(d->len!=8&&d->len!=16))return; const u8* p=d->p;
    u32 id=p[0]|(u32)p[1]<<8|(u32)p[2]<<16|(u32)p[3]<<24; int o=d->len==16?8:4; u32 ed=p[o]|(u32)p[o+1]<<8|(u32)p[o+2]<<16|(u32)p[o+3]<<24;
    g_hpw_events++; g_hpw_last=id;
    if(id==HPE_BEZEL){ u32 kc=0; if(!hp_read_int(HPQ_HOTKEY,&kc))hp_key(kc); }
    else if(id==HPE_WIRELESS)hp_radio_state(1);
    else if(id==HPE_KBD_LIGHT)osd(8,-1,"Keyboard light");
    else if(id==HPE_CAMERA){ if(ed==0xFF)osd(5,-1,"Camera shutter closed"); else if(ed==0xFE)osd(5,-1,"Camera shutter open"); }
    else if(id==HPE_OMEN)osd(8,-1,"Omen key"); }
int hp_wmi_notify(anode* n,u32 v){
    if(!g_hpw_rf||n!=g_hpw_rf)return 0; if(v==0x80){ g_hpw_events++; push(18); } return 1; }
void hp_wmi_init(void){
    anode* rf[2]; if(g_aroot&&aml_find_devices(g_aroot,"HPQ6001",rf,2,0,0)>0&&(aml_sta(rf[0])&1)){ g_hpw_rf=rf[0]; klog("hp: wireless button found"); }
    if(!wmi_has(HPW_EVENT))return; g_hpw_ok=1; wmi_on_event(HPW_EVENT,hp_ev); wmi_set_events(HPW_EVENT,1);
    if(wmi_has(HPW_BIOS))hp_radio_state(0); klog("hp: WMI hotkeys enabled"); }
