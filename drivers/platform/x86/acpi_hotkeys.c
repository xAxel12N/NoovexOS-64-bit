#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/drivers/acpi/aml.h"

extern u64 g_aml_hard_deadline; extern volatile u64 g_ticks;
extern void hk_push(int id); extern void klog_key(u32 v);
enum { AH_IHID=1, AH_VBTN, AH_RADIO, AH_ASUSW, AH_DELLR };
typedef struct { anode* dev; u8 kind, sub; u64 t; } ahdev;
static ahdev g_ah[8]; static int g_ah_n;
int g_ah_ihid, g_ah_vbtn, g_ah_radio; u32 g_ah_events, g_ah_last;
static const u8 ihid_guid[16]={0xb3,0x56,0xec,0xee,0x42,0x44,0x8f,0x40,0xa7,0x92,0x4e,0xdd,0x4d,0x75,0x80,0x54};

static void az(aobj* o){ u8* b=(u8*)o; for(u32 k=0;k<sizeof *o;k++)b[k]=0; }
static int ah_call(anode* m,aobj* a,int n,u64* ret){
    if(!m||!g_aml_ok)return -1; u64 dl=g_aml_hard_deadline; g_aml_hard_deadline=0; aobj o; az(&o); int r=aml_eval(m,a,n,&o); g_aml_hard_deadline=dl;
    if(!r&&o.type==AT_REF){ aobj d; az(&d); aml_deref(&o,&d); aobj_free(&o); o=d; }
    if(!r&&ret)*ret=aobj_to_int(&o); aobj_free(&o); return r; }
static int ah_m(anode* d,const char* nm,int na,u64 a,u64* ret){ anode* m=aml_child(d,nm); if(!m)return -1; aobj x; az(&x); aobj_int(&x,a); int r=ah_call(m,&x,na,ret); aobj_free(&x); return r; }
static int ihid_dsm(anode* d,u32 fn,int has,u64 arg,u64* ret){
    anode* m=aml_child(d,"_DSM"); if(!m)return -1; aobj x[4]; for(int k=0;k<4;k++)az(&x[k]);
    u8* g=(u8*)kmalloc(16); if(!g)return -1; for(int k=0;k<16;k++)g[k]=ihid_guid[k]; x[0].type=AT_BUF; x[0].p=g; x[0].len=16; x[0].own=1;
    aobj_int(&x[1],1); aobj_int(&x[2],fn); x[3].type=AT_PKG; x[3].own=1;
    if(has){ x[3].e=(aobj*)kmalloc(sizeof(aobj)); if(!x[3].e){ aobj_free(&x[0]); return -1; } az(&x[3].e[0]); aobj_int(&x[3].e[0],arg); x[3].len=1; }
    int r=ah_call(m,x,4,ret); for(int k=0;k<4;k++)aobj_free(&x[k]); return r; }
static int ihid_fn(anode* d,const char* nm,u32 fn,int has,u64 arg,u64* ret){ if(aml_child(d,nm))return ah_m(d,nm,has,arg,ret); return ihid_dsm(d,fn,has,arg,ret); }
static void ah_radio(ahdev* h){ if(g_ticks-h->t<25)return; h->t=g_ticks; hk_push(18); }
static void ihid_key(u32 c){
    switch(c){ case 8: hk_push(18); break; case 11: hk_push(12); break; case 14: hk_push(7); break; case 15: hk_push(4); break; case 16: hk_push(3); break;
        case 17: hk_push(1); break; case 18: hk_push(2); break; case 19: hk_push(13); break; case 20: hk_push(14); break; } }
static int ah_add(anode* d,int kind,int sub){ if(g_ah_n>=8)return 0; for(int i=0;i<g_ah_n;i++) if(g_ah[i].dev==d)return 0; g_ah[g_ah_n].dev=d; g_ah[g_ah_n].kind=(u8)kind; g_ah[g_ah_n].sub=(u8)sub; g_ah[g_ah_n].t=0; g_ah_n++; return 1; }
static int ah_find(const char* id,int kind,int sub){ anode* o[4]; int n=aml_find_devices(g_aroot,id,o,4,0,0), a=0; for(int i=0;i<n;i++)a+=ah_add(o[i],kind,sub); return a; }
void acpi_hotkeys_init(void){
    if(!g_aml_ok||!g_aroot)return; g_ah_n=0;
    static const char* const ih[]={"INT33D5","INTC1051","INTC1054","INTC1070","INTC1076","INTC1077","INTC1078","INTC107B","INTC10CB","INTC10CC"};
    for(u32 i=0;i<sizeof ih/sizeof ih[0];i++)g_ah_ihid+=ah_find(ih[i],AH_IHID,0);
    g_ah_vbtn=ah_find("INT33D6",AH_VBTN,0);
    g_ah_radio=ah_find("WSTADEF",AH_RADIO,0)+ah_find("AMDI0051",AH_RADIO,0)+ah_find("ATK4001",AH_ASUSW,0)+ah_find("ATK4002",AH_ASUSW,0);
    for(int i=0;i<g_ah_n;i++){ ahdev* h=&g_ah[i]; anode* d=h->dev;
        if(h->kind==AH_IHID){ ihid_fn(d,"BTNL",1,0,0,0); ihid_fn(d,"HDSM",3,1,1,0); u64 c=0; if(!ihid_fn(d,"HEBC",7,0,0,&c)&&(c&0x20000))ihid_fn(d,"BTNE",6,1,0x1F,0); }
        else if(h->kind==AH_VBTN){ anode* m=aml_child(d,"VBDL"); if(m)ah_call(m,0,0,0); } }
    { anode* o[2]; int n=aml_find_devices(g_aroot,"DELLABCE",o,2,0,0); if(!n)n=aml_find_devices(g_aroot,"DELRBTN",o,2,0,0);
        for(int i=0;i<n;i++){ u64 t=9; if(ah_m(o[i],"CRBT",0,0,&t)||t>3)continue; if(ah_add(o[i],AH_DELLR,t>=2)){ ah_m(o[i],"ARBT",1,1,0); g_ah_radio++; } } }
    if(g_ah_ihid)klog("hotkeys: Intel HID buttons found");
    if(g_ah_vbtn)klog("hotkeys: Intel virtual buttons found");
    if(g_ah_radio)klog("hotkeys: airplane-mode button found"); }
int acpi_hotkeys_notify(anode* n,u32 v){
    ahdev* h=0; for(int i=0;i<g_ah_n;i++) if(g_ah[i].dev==n){ h=&g_ah[i]; break; } if(!h)return 0;
    g_ah_events++; g_ah_last=(h->kind<<16)|(v&0xFFFF); klog_key(0x60000u|(v&0xFFFF));
    if(h->kind==AH_IHID){
        if(v==0xC0){ for(int k=0;k<8;k++){ u64 c=0; if(ihid_fn(n,"HDEM",4,0,0,&c)||!c)break; g_ah_last=(1u<<16)|(u32)(c&0xFFFF); ihid_key((u32)c); } }
        else if(v==0xC4)hk_push(1); else if(v==0xC6)hk_push(2);
        return 1; }
    if(h->kind==AH_VBTN){ if(v==0xC4)hk_push(1); else if(v==0xC6)hk_push(2); return 1; }
    if(h->kind==AH_RADIO){ if(v==0x80)ah_radio(h); return 1; }
    if(h->kind==AH_ASUSW){ if(v==0x88)ah_radio(h); return 1; }
    if(h->kind==AH_DELLR){ if(v!=0x80)return 1;
        if(!h->sub){ ah_radio(h); return 1; }
        u64 s=1; if(ah_m(n,"GRBT",0,0,&s))return 1; extern int g_airplane; if((s?0:1)!=g_airplane)ah_radio(h); return 1; }
    return 1; }
