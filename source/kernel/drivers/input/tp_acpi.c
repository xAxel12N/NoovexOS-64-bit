#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/mm/heap.h"
#include "include/nvx/drivers/acpi/aml.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "include/nvx/drivers/input/tp_acpi.h"

extern u64 g_aml_hard_deadline;
int g_tpa_crs_ok, g_tpa_dsm_reg=-1, g_tpa_ps0, g_tpa_pr0, g_tpa_timing, g_tpa_lpss;
u16 g_tpa_ss[3], g_tpa_fs[3];

static void tpa_zero(aobj* o){ o->type=AT_NONE; o->p=0; o->e=0; o->own=0; o->len=0; o->i=0; o->cont=0; o->node=0; }
static int tpa_eval(anode* m,aobj* a,int n,aobj* out){
    if(!m||!g_aml_ok)return -1; u64 dl=g_aml_hard_deadline; g_aml_hard_deadline=0; int r=aml_eval(m,a,n,out); g_aml_hard_deadline=dl;
    if(!r&&out->type==AT_REF){ aobj d; tpa_zero(&d); aml_deref(out,&d); aobj_free(out); *out=d; }
    return r; }
static anode* tpa_node(const char* path){ if(!g_aml_ok||!path||!path[0])return 0; return aml_find(path); }

int tpa_crs(const char* dev,tpa_res* r){
    r->ok=0; r->addr=0; r->speed=0; r->ctrl[0]=0; r->has_gpio=0; r->pin=0; r->active_low=0; r->gctrl[0]=0;
    anode* d=tpa_node(dev); if(!d)return -1; anode* c=aml_child(d,"_CRS"); if(!c)return -1;
    aobj o; tpa_zero(&o); if(tpa_eval(c,0,0,&o)||o.type!=AT_BUF||!o.p){ aobj_free(&o); return -1; }
    const u8* b=o.p; u32 n=o.len, i=0;
    while(i<n){ u8 t=b[i]; if(t==0x79)break;
        if(t&0x80){ if(i+3>n)break; u32 L=(u32)b[i+1]|((u32)b[i+2]<<8); const u8* e=b+i; u32 tot=3+L; if(i+tot>n)break;
            if(t==0x8E&&L>=15&&e[5]==1&&!r->ok){ u16 tl=(u16)(e[10]|(e[11]<<8)); r->speed=(u32)e[12]|((u32)e[13]<<8)|((u32)e[14]<<16)|((u32)e[15]<<24); r->addr=(u16)(e[16]|(e[17]<<8));
                u32 so=12+tl; int k=0; while(so<tot&&e[so]&&k<71)r->ctrl[k++]=(char)e[so++]; r->ctrl[k]=0; r->ok=1; }
            else if(t==0x8C&&L>=20&&e[4]==0&&!r->has_gpio){ u16 fl=(u16)(e[7]|(e[8]<<8)); u16 pto=(u16)(e[14]|(e[15]<<8)); u16 rso=(u16)(e[17]|(e[18]<<8));
                if(pto+1<tot){ r->pin=(u16)(e[pto]|(e[pto+1]<<8)); r->active_low=((fl>>1)&3)==1; int k=0; u32 so=rso; while(so<tot&&e[so]&&k<71)r->gctrl[k++]=(char)e[so++]; r->gctrl[k]=0; r->has_gpio=1; } }
            i+=tot; }
        else i+=1+(t&7); }
    aobj_free(&o); g_tpa_crs_ok=r->ok; return r->ok?0:-1; }

int tpa_dsm_hid(const char* dev){
    static const u8 uuid[16]={0xF7,0xF6,0xDF,0x3C,0x67,0x42,0x55,0x45,0xAD,0x05,0xB3,0x0A,0x3D,0x89,0x38,0xDE};
    anode* d=tpa_node(dev); if(!d)return -1; anode* m=aml_child(d,"_DSM"); if(!m)return -1;
    aobj a[4]; for(int k=0;k<4;k++)tpa_zero(&a[k]);
    u8* ub=(u8*)kmalloc(16); if(!ub)return -1; for(int k=0;k<16;k++)ub[k]=uuid[k];
    a[0].type=AT_BUF; a[0].p=ub; a[0].len=16; aobj_int(&a[1],1); aobj_int(&a[2],1); a[3].type=AT_PKG; a[3].len=0; a[3].e=0;
    aobj o; tpa_zero(&o); int r=tpa_eval(m,a,4,&o); for(int k=0;k<4;k++)aobj_free(&a[k]);
    int v=-1; if(!r&&(o.type==AT_INT||o.type==AT_BUF)){ u64 x=aobj_to_int(&o); if(x<0x10000)v=(int)x; }
    aobj_free(&o); g_tpa_dsm_reg=v; return v; }

void tpa_power_on(const char* dev){
    anode* d=tpa_node(dev); if(!d)return;
    anode* pr=aml_child(d,"_PR0");
    if(pr){ aobj o; tpa_zero(&o); if(!tpa_eval(pr,0,0,&o)&&o.type==AT_PKG&&o.e){ for(u32 k=0;k<o.len&&k<8;k++){ aobj* e=&o.e[k]; anode* pn=0;
            if(e->type==AT_REF&&e->node)pn=e->node; else if((e->type==AT_NAMESTR||e->type==AT_STR)&&e->p){ char s[80]; u32 q=0; for(;q<e->len&&q<79&&e->p[q];q++)s[q]=(char)e->p[q]; s[q]=0; pn=aml_find(s); }
            if(pn&&pn->kind==K_POWER){ anode* on=aml_child(pn,"_ON"); if(on){ aobj z; tpa_zero(&z); if(!tpa_eval(on,0,0,&z))g_tpa_pr0++; aobj_free(&z); } } } }
        aobj_free(&o); }
    anode* ps0=aml_child(d,"_PS0"); if(ps0){ aobj z; tpa_zero(&z); if(!tpa_eval(ps0,0,0,&z))g_tpa_ps0=1; aobj_free(&z); }
    if(g_tpa_ps0||g_tpa_pr0)ih_delay_ms(30); }

static int tpa_pkg3(anode* c,const char* nm,u16 out[3]){
    anode* m=aml_child(c,nm); if(!m)return -1; aobj o; tpa_zero(&o);
    if(tpa_eval(m,0,0,&o)||o.type!=AT_PKG||o.len<2||!o.e){ aobj_free(&o); return -1; }
    for(int k=0;k<3;k++){ out[k]=0; if((u32)k<o.len){ aobj* e=&o.e[k]; aobj v; tpa_zero(&v); if(e->type==AT_REF){ aml_deref(e,&v); out[k]=(u16)aobj_to_int(&v); aobj_free(&v); } else out[k]=(u16)aobj_to_int(e); } }
    aobj_free(&o); return (out[0]&&out[1])?0:-1; }
int tpa_timing(const char* ctrl,u16 ss[3],u16 fs[3]){
    anode* c=tpa_node(ctrl); if(!c)return 0; int got=0;
    if(!tpa_pkg3(c,"SSCN",ss))got|=1; if(!tpa_pkg3(c,"FMCN",fs))got|=2;
    if(got&1){ g_tpa_ss[0]=ss[0]; g_tpa_ss[1]=ss[1]; g_tpa_ss[2]=ss[2]; } if(got&2){ g_tpa_fs[0]=fs[0]; g_tpa_fs[1]=fs[1]; g_tpa_fs[2]=fs[2]; }
    g_tpa_timing=got; return got; }

void tpa_lpss_init(u64 base){
    volatile u32* rs=(volatile u32*)(base+0x204);
    if((*rs&7u)!=7u){ *rs=0x7u; ih_delay_ms(2); }
    *(volatile u32*)(base+0x240)=(u32)base; *(volatile u32*)(base+0x244)=(u32)(base>>32);
    g_tpa_lpss++; }

u64 tpa_map(u64 base){ if(base<0x100000000ULL)return base; return acpi_hi_map(base)?base:0; }
int tpa_hid(const char* dev,char* out){
    anode* d=tpa_node(dev); if(!d)return -1; anode* h=aml_child(d,"_HID"); if(!h)return -1;
    aobj o; tpa_zero(&o); if(tpa_eval(h,0,0,&o)){ aobj_free(&o); return -1; }
    int r=-1;
    if((o.type==AT_STR||o.type==AT_BUF)&&o.p){ int k=0; for(;k<11&&(u32)k<o.len&&o.p[k];k++)out[k]=(char)o.p[k]; out[k]=0; r=k?0:-1; }
    else if(o.type==AT_INT){ u32 v=(u32)o.i; u32 sw=((v&0xFF)<<24)|((v&0xFF00)<<8)|((v>>8)&0xFF00)|(v>>24); static const char H[]="0123456789ABCDEF";
        out[0]=(char)('@'+((sw>>26)&0x1F)); out[1]=(char)('@'+((sw>>21)&0x1F)); out[2]=(char)('@'+((sw>>16)&0x1F)); for(int k=0;k<4;k++)out[3+k]=H[(sw>>(12-4*k))&0xF]; out[7]=0; r=0; }
    aobj_free(&o); return r; }
