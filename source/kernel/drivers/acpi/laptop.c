#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/mm/heap.h"
#include "include/nvx/core/sound.h"
#include "include/nvx/drivers/acpi/aml.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"

extern u64 g_aml_hard_deadline;
extern void toast(const char* m);
int g_lap_kind=0, g_lap_kbd=-1, g_lap_kbdmax=0, g_lap_chg=0, g_lap_chg_ok=0, g_lap_tp_off=0; u32 g_lap_events=0, g_lap_last=0;
const char* g_lap_name="none";
static anode* g_lap_dev;

static void lz(aobj* o){ o->type=AT_NONE; o->p=0; o->e=0; o->own=0; o->len=0; o->i=0; o->cont=0; o->node=0; }
static int lap_call(anode* m,aobj* a,int n,u64* ret){
    if(!m||!g_aml_ok)return -1; u64 dl=g_aml_hard_deadline; g_aml_hard_deadline=0; aobj o; lz(&o); int r=aml_eval(m,a,n,&o); g_aml_hard_deadline=dl;
    if(!r&&o.type==AT_REF){ aobj d; lz(&d); aml_deref(&o,&d); aobj_free(&o); o=d; }
    if(!r&&ret)*ret=aobj_to_int(&o); aobj_free(&o); return r; }
static int lap_m0(const char* nm,u64* ret){ return lap_call(aml_child(g_lap_dev,nm),0,0,ret); }
static int lap_m1(const char* nm,u64 a,u64* ret){ aobj x; lz(&x); aobj_int(&x,a); int r=lap_call(aml_child(g_lap_dev,nm),&x,1,ret); aobj_free(&x); return r; }
static int lap_m2(const char* nm,u64 a,u64 b,u64* ret){ aobj x[2]; lz(&x[0]); lz(&x[1]); aobj_int(&x[0],a); aobj_int(&x[1],b); int r=lap_call(aml_child(g_lap_dev,nm),x,2,ret); aobj_free(&x[0]); aobj_free(&x[1]); return r; }
static int asus_wmnb(u32 method,u32 a0,u32 a1,u64* ret){
    anode* m=aml_child(g_lap_dev,"WMNB"); if(!m)return -1; aobj x[3]; for(int k=0;k<3;k++)lz(&x[k]);
    aobj_int(&x[0],0); aobj_int(&x[1],method); u8* b=(u8*)kmalloc(8); if(!b)return -1;
    for(int k=0;k<4;k++){ b[k]=(u8)(a0>>(8*k)); b[4+k]=(u8)(a1>>(8*k)); } x[2].type=AT_BUF; x[2].p=b; x[2].len=8;
    int r=lap_call(m,x,3,ret); for(int k=0;k<3;k++)aobj_free(&x[k]); return r; }
#define ASUS_DSTS 0x53545344u
#define ASUS_DEVS 0x53564544u
#define ASUS_INIT 0x54494E49u
static int lap_uid_is(anode* d,const char* s){ anode* u=aml_child(d,"_UID"); if(!u)return 0; aobj o; lz(&o); u64 dl=g_aml_hard_deadline; g_aml_hard_deadline=0; int r=aml_eval(u,0,0,&o); g_aml_hard_deadline=dl;
    int ok=0; if(!r&&o.type==AT_STR&&o.p){ ok=1; for(int k=0;s[k];k++) if((u32)k>=o.len||o.p[k]!=(u8)s[k])ok=0; } aobj_free(&o); return ok; }
static void vol_step(int d){ int v=g_snd_vol+d*6; if(d==-10)v=g_snd_vol?0:60; if(v<0)v=0; if(v>100)v=100; g_snd_vol=v; extern void snd_apply_volume(int); snd_apply_volume(v); extern void osd_show(int,int,const char*); osd_show(1,v,v?"Volume":"Muted"); }
static void bl(int d){ bl_step(d); extern int bl_cur_pct(void); extern void osd_show(int,int,const char*); int b=bl_cur_pct(); osd_show(2,b<0?0:b,"Brightness"); }
void laptop_kbd_set(int lvl);
static void kbd_step(int d){ if(g_lap_kbdmax<=0)return; int l=g_lap_kbd<0?0:g_lap_kbd; l+=d; if(l<0)l=0; if(l>g_lap_kbdmax)l=d>0?0:g_lap_kbdmax; laptop_kbd_set(l); toast(l==0?"Keyboard light off":"Keyboard light on"); }
static void tp_toggle(void){ extern void hk_do(int); hk_do(19); }

void laptop_init(void){
    if(!g_aml_ok||!g_aroot)return; anode* out[8]; int n;
    if((n=aml_find_devices(g_aroot,"LEN0068",out,8,0,0))>0||(n=aml_find_devices(g_aroot,"IBM0068",out,8,0,0))>0||(n=aml_find_devices(g_aroot,"LEN0268",out,8,0,0))>0){
        g_lap_dev=out[0]; g_lap_kind=1; g_lap_name="Lenovo ThinkPad";
        for(int b=0;b<32;b++)lap_m2("MHKM",(u64)(b+1),1,0); lap_m1("MHKC",1,0);
        u64 v=0; if(aml_child(g_lap_dev,"MLCG")&&!lap_m1("MLCG",0,&v)){ g_lap_kbdmax=2; g_lap_kbd=(int)(v&3); }
        if(aml_child(g_lap_dev,"BCSS"))g_lap_chg_ok=1; }
    else if((n=aml_find_devices(g_aroot,"VPC2004",out,8,0,0))>0){
        g_lap_dev=out[0]; g_lap_kind=2; g_lap_name="Lenovo IdeaPad";
        if(aml_child(g_lap_dev,"SBMC"))g_lap_chg_ok=1; if(aml_child(g_lap_dev,"SALS"))g_lap_kbdmax=1; }
    else { n=aml_find_devices(g_aroot,"PNP0C14",out,8,0,0); for(int i=0;i<n;i++) if(aml_child(out[i],"WMNB")&&lap_uid_is(out[i],"ATK")){ g_lap_dev=out[i]; break; }
        if(g_lap_dev){ g_lap_kind=3; g_lap_name="ASUS"; asus_wmnb(ASUS_INIT,0,0,0);
            u64 v=0; if(!asus_wmnb(ASUS_DSTS,0x00050021u,0,&v)&&(v&0x10000)){ g_lap_kbdmax=3; g_lap_kbd=(int)(v&3); }
            v=0; if(!asus_wmnb(ASUS_DSTS,0x00120057u,0,&v)&&(v&0x10000))g_lap_chg_ok=1; }
        else if((n=aml_find_devices(g_aroot,"ATK0100",out,8,0,0))>0||(n=aml_find_devices(g_aroot,"ATK0101",out,8,0,0))>0){ g_lap_dev=out[0]; g_lap_kind=4; g_lap_name="ASUS (ATK)"; lap_m1("INIT",0,0); } }
    if(g_lap_kind){ char b[80],*p=b; p=scopy(p,"laptop: "); p=scopy(p,g_lap_name); p=scopy(p," extras found"); *p=0; klog(b); } }

void laptop_kbd_set(int lvl){
    if(g_lap_kbdmax<=0)return; if(lvl<0)lvl=0; if(lvl>g_lap_kbdmax)lvl=g_lap_kbdmax;
    if(g_lap_kind==1)lap_m1("MLCS",(u64)lvl,0);
    else if(g_lap_kind==2)lap_m0(lvl?"SALS":"HALS",0);
    else if(g_lap_kind==3)asus_wmnb(ASUS_DEVS,0x00050021u,0x80u|(u32)lvl,0);
    g_lap_kbd=lvl; }
void laptop_charge_set(int on){
    if(!g_lap_chg_ok)return;
    if(g_lap_kind==1)lap_m1("BCSS",on?80:0,0);
    else if(g_lap_kind==2)lap_m1("SBMC",on?3:5,0);
    else if(g_lap_kind==3)asus_wmnb(ASUS_DEVS,0x00120057u,on?80u:100u,0);
    g_lap_chg=on; toast(on?"Battery stops charging at 80%":"Battery charges to 100%"); }

static int vpc_rd(u32 cmd,u64* out){
    if(lap_m2("VPCW",1,cmd,0))return -1;
    for(int t=0;t<200;t++){ u64 v=1; if(lap_m1("VPCR",1,&v))return -1; if(v==0)return lap_m1("VPCR",0,out); for(volatile int d=0;d<20000;d++); }
    return -1; }
static void lap_key(u32 c){
    g_lap_events++; g_lap_last=c;
    if(g_lap_kind==1){ switch(c){ case 0x1010: bl(1); break; case 0x1011: bl(-1); break; case 0x1015: vol_step(1); break; case 0x1016: vol_step(-1); break; case 0x1017: vol_step(-10); break; case 0x1012: kbd_step(1); break; case 0x1008: case 0x1009: tp_toggle(); break; } return; }
    if(c>=0x10&&c<=0x1F){ bl(1); return; } if(c>=0x20&&c<=0x2E){ bl(-1); return; }
    switch(c){ case 0x30: vol_step(1); break; case 0x31: vol_step(-1); break; case 0x32: vol_step(-10); break; case 0xC4: kbd_step(1); break; case 0xC5: kbd_step(-1); break; case 0xC7: kbd_step(1); break; case 0x6B: tp_toggle(); break; } }
int laptop_notify(anode* n,u32 v){
    if(!g_lap_kind||n!=g_lap_dev)return 0;
    if(g_lap_kind==1){ if(v!=0x80)return 1; for(int k=0;k<16;k++){ u64 c=0; if(lap_m0("MHKP",&c)||!c)break; lap_key((u32)c); } return 1; }
    if(g_lap_kind==3){ u64 c=v; if(v==0xFF){ if(lap_m1("_WED",0xFF,&c))return 1; } lap_key((u32)c&0xFF); return 1; }
    if(g_lap_kind==4){ lap_key(v); return 1; }
    if(g_lap_kind==2){ if(v!=0x80)return 1; u64 v1=0,v2=0; if(vpc_rd(0x10,&v1))return 1; vpc_rd(0x1A,&v2); u32 bits=(u32)((v2<<8)|(v1&0xFF)); extern void klog_key(u32); klog_key(0x50000u|(bits&0xFFFF));
        extern void hk_push(int);
        for(int bit=0;bit<16;bit++){ if(!(bits&(1u<<bit)))continue;
            if(bit==6)hk_push(15); else if(bit==7)hk_push(16); else if(bit==8)hk_push(17); else if(bit==13)hk_push(18); else if(bit==9)hk_push(18);
            else if(bit==5){ u64 t=0; if(!vpc_rd(0x1B,&t)){ g_lap_tp_off=t?0:1; extern void osd_show(int,int,const char*); osd_show(7,g_lap_tp_off?0:-1,g_lap_tp_off?"Touchpad off":"Touchpad on"); } else hk_push(19); }
            else if(bit==4){ u64 c=0,m=0; if(!vpc_rd(0x12,&c)&&!vpc_rd(0x11,&m)&&m){ extern void osd_show(int,int,const char*); osd_show(2,(int)(c*100/m),"Brightness"); } }
            else if(bit==3){ toast("Novo button"); } }
        return 1; }
    return 1; }
