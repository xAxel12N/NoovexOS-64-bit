#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/mm/heap.h"
#include "include/nvx/drivers/acpi/aml.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"

#define IGP_MAX 640
extern u64 g_aml_hard_deadline;
static u64* g_igp_pad; static u16* g_igp_ag; static u16* g_igp_ah; static int g_igp_n;
int g_igp_state, g_igp_comms, g_igp_pick=-1, g_igp_pol; u32 g_igp_samples, g_igp_rep, g_igp_norep, g_igp_skips; u64 g_igp_addr;
static u8* g_igp_cur;

int g_igp_err, g_igp_direct; u32 g_igp_dw0; char g_igp_hid[12];
static u64 g_igp_cb[8]; static u32 g_igp_cpb[8], g_igp_cst[8]; static int g_igp_cn;
typedef struct { const char* hid; int n; u16 start[4], end[4]; } igp_map;
static const igp_map IGP_MAPS[]={ {"INT344B",3,{0,48,120,0},{47,119,151,0}}, {"INT3453",1,{0,0,0,0},{511,0,0,0}}, {"INT3452",1,{0,0,0,0},{511,0,0,0}} };
static int igp_streq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
static int igp_direct(int pin,int active_low){
    for(u32 m=0;m<sizeof(IGP_MAPS)/sizeof(IGP_MAPS[0]);m++){ const igp_map* M=&IGP_MAPS[m]; if(!igp_streq(g_igp_hid,M->hid))continue;
        if(g_igp_cn<M->n){ g_igp_err=5; return -1; }
        for(int k=0;k<M->n;k++) if(pin>=M->start[k]&&pin<=M->end[k]){
            u64 a=g_igp_cb[k]+g_igp_cpb[k]+(u64)(pin-M->start[k])*g_igp_cst[k]; u32 dw0=*(volatile u32*)a; g_igp_dw0=dw0;
            if(dw0==0xFFFFFFFFu){ g_igp_err=6; return -1; }
            if(((dw0>>10)&7u)!=0||(dw0&(1u<<9))){ g_igp_err=7; return -1; }
            g_igp_addr=a; g_igp_pol=active_low?0:1; g_igp_pick=pin; g_igp_state=2; g_igp_direct=1;
            klog("touchpad: Intel GPIO interrupt pin mapped from ACPI - reading the pin instead of polling"); return 0; }
        g_igp_err=8; return -1; }
    g_igp_err=4; return -1; }
int igp_setup(const char* gctrl,int pin,int active_low){
    g_igp_state=0; g_igp_n=0; g_igp_err=0; g_igp_direct=0; g_igp_cn=0; g_igp_hid[0]=0;
    if(!g_aml_ok||!gctrl||!gctrl[0]){ g_igp_err=1; return -1; }
    anode* d=aml_find(gctrl); if(!d){ g_igp_err=1; return -1; } anode* c=aml_child(d,"_CRS"); if(!c){ g_igp_err=2; return -1; }
    { extern int tpa_hid(const char*,char*); char hb[12]; if(tpa_hid(gctrl,hb)==0){ int k=0; for(;hb[k]&&k<11;k++)g_igp_hid[k]=hb[k]; g_igp_hid[k]=0; } }
    aobj o; o.type=AT_NONE; o.p=0; o.e=0; o.own=0; o.len=0; o.i=0; o.cont=0; o.node=0; u64 dl=g_aml_hard_deadline; g_aml_hard_deadline=0; int r=aml_eval(c,0,0,&o); g_aml_hard_deadline=dl;
    if(!r&&o.type==AT_REF){ aobj dd; dd.type=AT_NONE; dd.p=0; dd.e=0; dd.own=0; dd.len=0; dd.i=0; dd.cont=0; dd.node=0; aml_deref(&o,&dd); aobj_free(&o); o=dd; }
    if(r||o.type!=AT_BUF||!o.p){ aobj_free(&o); g_igp_err=3; return -1; }
    if(!g_igp_pad){ g_igp_pad=(u64*)kmalloc(IGP_MAX*8); g_igp_ag=(u16*)kmalloc(IGP_MAX*2); g_igp_ah=(u16*)kmalloc(IGP_MAX*2); g_igp_cur=(u8*)kmalloc(IGP_MAX); }
    if(!g_igp_pad||!g_igp_ag||!g_igp_ah||!g_igp_cur){ aobj_free(&o); g_igp_err=9; return -1; }
    const u8* b=o.p; u32 n=o.len, i=0; g_igp_comms=0;
    while(i<n){ u8 t=b[i]; if(t==0x79)break;
        if(t&0x80){ if(i+3>n)break; u32 L=(u32)b[i+1]|((u32)b[i+2]<<8); if(i+3+L>n)break;
            if(t==0x86&&L>=9){ u64 base=(u32)b[i+4]|((u32)b[i+5]<<8)|((u32)b[i+6]<<16)|((u32)b[i+7]<<24); u32 len=(u32)b[i+8]|((u32)b[i+9]<<8)|((u32)b[i+10]<<16)|((u32)b[i+11]<<24);
                if(base&&len>=0x1000&&acpi_hi_map(base)){ u32 rev=*(volatile u32*)base; u32 pb=*(volatile u32*)(base+0x0C);
                    if(rev!=0xFFFFFFFFu&&pb&&pb<len&&!(pb&3)){ u32 st=((rev>>16)>=0x94)?16u:8u; u32 np=(len-pb)/st; if(np>320)np=320;
                        if(g_igp_cn<8){ g_igp_cb[g_igp_cn]=base; g_igp_cpb[g_igp_cn]=pb; g_igp_cst[g_igp_cn]=st; g_igp_cn++; }
                        for(u32 k=0;k<np&&g_igp_n<IGP_MAX;k++)g_igp_pad[g_igp_n++]=base+pb+k*st; g_igp_comms++; } } }
            i+=3+L; }
        else i+=1+(t&7); }
    aobj_free(&o);
    for(int k=0;k<g_igp_n;k++){ g_igp_ag[k]=0; g_igp_ah[k]=0; }
    g_igp_samples=g_igp_rep=g_igp_norep=0; g_igp_pick=-1;
    if(!g_igp_n){ g_igp_err=g_igp_err?g_igp_err:10; return -1; }
    if(pin>=0&&igp_direct(pin,active_low)==0)return 0;
    g_igp_state=1; return 0; }
void igp_sample(void){ if(g_igp_state!=1)return; for(int k=0;k<g_igp_n;k++)g_igp_cur[k]=(u8)((*(volatile u32*)g_igp_pad[k]>>1)&1u); }
void igp_learn(int rep){
    if(g_igp_state!=1)return; g_igp_samples++; if(rep)g_igp_rep++; else g_igp_norep++;
    for(int k=0;k<g_igp_n;k++){ int rx=g_igp_cur[k]; if((rx==0)==(rep!=0))g_igp_ag[k]++; if((rx==1)==(rep!=0))g_igp_ah[k]++; }
    if(g_igp_rep<24||g_igp_norep<24)return;
    int best=-1, pol=0, cnt=0;
    for(int k=0;k<g_igp_n;k++){ if(g_igp_ag[k]==g_igp_samples){ cnt++; best=k; pol=0; } if(g_igp_ah[k]==g_igp_samples){ cnt++; best=k; pol=1; } }
    if(cnt==1){ g_igp_pick=best; g_igp_pol=pol; g_igp_addr=g_igp_pad[best]; g_igp_state=2; klog("touchpad: Intel GPIO interrupt pad found - reading the pin instead of polling"); return; }
    if(cnt==0||g_igp_samples>4000){ g_igp_state=cnt==0?3:g_igp_state; if(g_igp_samples>4000)g_igp_state=3; } }
int igp_ready(void){ return g_igp_state==2; }
int igp_asserted(void){ u32 rx=(*(volatile u32*)g_igp_addr>>1)&1u; int a=g_igp_pol?(int)rx:(int)!rx; if(!a)g_igp_skips++; return a; }
