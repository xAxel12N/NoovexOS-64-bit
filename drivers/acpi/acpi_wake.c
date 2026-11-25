#ifndef AW_HOST
#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/power.h"
#include "include/nvx/core/sysfeat.h"
#include "include/nvx/drivers/acpi/aml.h"
#include "include/nvx/drivers/acpi/acpi_wake.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/lib/base/fmt.h"
int acpi_pci_loc(anode* n,int* bus,int* dev,int* fn,int depth);
#endif

#define AW_MAX 32
struct aw_dev { anode* dev; u32 gpe; u8 sx, kind, armed, was_en, bit; u16 sts, en; int bus, slot, fn; };
static struct aw_dev g_aw[AW_MAX];
static int g_awn, g_aw_scanned, g_aw_blk_ok, g_aw_qemu;
static struct aw_blk g_awb;
static struct featguard aw_fg={"Keyboard wake through ACPI","feat-acpiwake.cfg",1,0,3,0,0};
u32 g_aw_arms, g_aw_wakes; int g_aw_last=-1, g_aw_narmed;
char g_aw_why[96];

int aw_fadt_blk(const u8* f,u32 len,struct aw_blk* b){
    b->b0=b->b1=0; b->l0=b->l1=b->base1=0;
    if(len<96)return -1;
    u32 g0=acpi_r32(f+80), g1=acpi_r32(f+84); b->l0=f[92]; b->l1=f[93]; b->base1=f[94];
    if(len>=232&&f[220]==1){ u64 x=acpi_r64(f+224); if(x)g0=(u32)x; } else if(len>=232&&f[220]==0&&acpi_r64(f+224)&&!g0)return -2;
    if(len>=244&&f[232]==1){ u64 x=acpi_r64(f+236); if(x)g1=(u32)x; }
    if(g0>0xFFFF||g1>0xFFFF)return -2;
    b->b0=(u16)(b->l0>=2?g0:0); b->b1=(u16)(b->l1>=2?g1:0);
    return b->b0||b->b1?0:-1; }

int aw_gpe_reg(const struct aw_blk* b,u32 g,u16* sts,u16* en,u8* bit){
    u32 n0=(u32)(b->l0/2)*8, n1=(u32)(b->l1/2)*8;
    if(b->b0&&g<n0){ *sts=(u16)(b->b0+g/8); *en=(u16)(b->b0+b->l0/2+g/8); *bit=(u8)(1u<<(g&7)); return 0; }
    if(b->b1&&g>=b->base1&&g<b->base1+n1){ u32 k=g-b->base1; *sts=(u16)(b->b1+k/8); *en=(u16)(b->b1+b->l1/2+k/8); *bit=(u8)(1u<<(k&7)); return 0; }
    return -1; }

static int aw_int(aobj* e,u64* v){
    if(e->type==AT_INT){ *v=e->i; return 0; }
    if(e->type!=AT_REF)return -1;
    aobj d; d.type=AT_NONE; d.p=0; d.e=0; d.own=0; d.len=0;
    int r=aml_deref(e,&d)||d.type!=AT_INT; if(!r)*v=d.i; aobj_free(&d); return r?-1:0; }

int aw_prw_decode(aobj* o,u32* gpe,u32* sx){
    if(o->type!=AT_PKG||o->len<2||!o->e)return -1;
    if(o->e[0].type==AT_PKG)return -2;
    u64 g=0,s=0; if(aw_int(&o->e[0],&g)||aw_int(&o->e[1],&s))return -1;
    if(g>255||s>5)return -1;
    *gpe=(u32)g; *sx=(u32)s; return 0; }

static int aw_prw(anode* d,u32* gpe,u32* sx){
    anode* p=aml_child(d,"_PRW"); if(!p)return -1;
    aobj o; o.type=AT_NONE; o.p=0; o.e=0; o.own=0; o.len=0;
    if(aml_eval(p,0,0,&o)){ aobj_free(&o); return -1; }
    if(o.type==AT_REF){ aobj t; t.type=AT_NONE; t.p=0; t.e=0; t.own=0; t.len=0; aml_deref(&o,&t); aobj_free(&o); o=t; }
    int r=aw_prw_decode(&o,gpe,sx);
    if(!r) for(u32 k=2;k<o.len;k++){ aobj* e=&o.e[k]; if(e->type==AT_REF&&e->rk==RK_NODE&&e->node&&e->node->kind==K_POWER){ anode* on=aml_child(e->node,"_ON"); if(on){ aobj z; aml_eval(on,0,0,&z); aobj_free(&z); } } }
    aobj_free(&o); return r; }

static int aw_is_kbd(anode* d){ return aml_hid_is(d,"PNP0303")||aml_hid_is(d,"PNP030B")||aml_hid_is(d,"PNP0320"); }

static void aw_walk(anode* n,int depth){
    if(!n||depth>16)return;
    for(anode* c=n->child;c&&g_awn<AW_MAX;c=c->next){
        if(c->kind==K_DEVICE&&aml_child(c,"_PRW")){
            struct aw_dev* a=&g_aw[g_awn]; u32 g=0,s=0;
            if(!aw_prw(c,&g,&s)){
                a->dev=c; a->gpe=g; a->sx=(u8)s; a->armed=0; a->bus=a->slot=a->fn=-1; a->kind=0;
                if(aw_is_kbd(c))a->kind=2;
                else if(aml_child(c,"_ADR")&&!acpi_pci_loc(c,&a->bus,&a->slot,&a->fn,0)&&a->slot>=0)a->kind=1;
                if(a->kind&&!aw_gpe_reg(&g_awb,g,&a->sts,&a->en,&a->bit))g_awn++; } }
        if(c->child)aw_walk(c,depth+1); } }

static void aw_scan(void){
    if(g_aw_scanned||!g_acpi.ok)return;
    if(!g_aml_ok||!g_aroot)return;
    g_aw_scanned=1; g_awn=0;
    const u8* r=(const u8*)g_acpi.rsdp; if(!r)return;
    u64 sdt=0; int w=4; if(r[15]>=2&&acpi_r32(r+20)>=36&&acpi_r64(r+24)){ sdt=acpi_r64(r+24); w=8; } else sdt=acpi_r32(r+16);
    const u8* x=acpi_tbl_ok(sdt); if(!x)return;
    u32 n=(acpi_r32(x+4)-36)/(u32)w; const u8* f=0;
    for(u32 i=0;i<n&&!f;i++){ u64 e=w==8?acpi_r64(x+36+i*8):acpi_r32(x+36+i*4); const u8* t=acpi_tbl_ok(e); if(t&&t[0]=='F'&&t[1]=='A'&&t[2]=='C'&&t[3]=='P')f=t; }
    if(!f)return;
    g_aw_qemu=f[10]=='B'&&f[11]=='O'&&f[12]=='C'&&f[13]=='H'&&f[14]=='S';
    int k=aw_fadt_blk(f,acpi_r32(f+4),&g_awb); g_aw_blk_ok=!k;
    if(k){ char* q=scopy(g_aw_why,k==-2?"the wake event registers are not I/O ports":"the firmware has no wake event registers"); *q=0; return; }
    aw_walk(g_aroot,0);
    char b[96],*q=b; q=scopy(q,"acpi: "); q=udec(q,(u32)g_awn); q=scopy(q," devices can wake the computer"); *q=0; klog(b); }

static int aw_is_xhci(const struct aw_dev* a){
    if(a->kind!=1)return -1;
    for(int c=0;c<g_xhc_n&&c<XHC_MAX;c++){ int sv=g_xcur; xhc_select(c); int m=xhci_init_ok&&xhci_pci_bus==a->bus&&xhci_pci_slot==a->slot&&xhci_pci_fn==a->fn; xhc_select(sv); if(m)return c; }
    return -1; }

static int aw_ctl_kbd(int c){ for(int e=0;e<xhid_n&&e<XHID_MAX;e++) if(xhid_used[e]&&xhid_iskbd[e]&&xhid_ctl[e]==c)return 1; return 0; }

static void aw_dsw(anode* d,int on,int sx){
    anode* m=aml_child(d,"_DSW"); aobj a[3], o; for(int i=0;i<3;i++){ a[i].type=AT_NONE; a[i].p=0; a[i].e=0; a[i].own=0; a[i].len=0; }
    if(m){ aobj_int(&a[0],(u64)on); aobj_int(&a[1],(u64)sx); aobj_int(&a[2],3); aml_eval(m,a,3,&o); aobj_free(&o); for(int i=0;i<3;i++)aobj_free(&a[i]); return; }
    m=aml_child(d,"_PSW"); if(m){ aobj_int(&a[0],(u64)on); aml_eval(m,a,1,&o); aobj_free(&o); aobj_free(&a[0]); } }

int acpi_wake_kbd_ok(int sx){
    aw_scan();
    if(g_aw_qemu&&inb(0x64)!=0xFF)return 1;
    for(int i=0;i<g_awn;i++){ struct aw_dev* a=&g_aw[i]; if(a->sx<sx)continue;
        if(a->kind==2&&inb(0x64)!=0xFF)return 1;
        int c=aw_is_xhci(a); if(c>=0&&aw_ctl_kbd(c))return 1; }
    return 0; }

int acpi_wake_arm(int sx){
    g_aw_narmed=0; g_aw_last=-1;
    fg_boot(&aw_fg); if(!aw_fg.on||aw_fg.dead)return 0;
    aw_scan(); if(!g_awn)return 0;
    if(!fg_begin(&aw_fg))return 0;
    for(int i=0;i<g_awn;i++){ struct aw_dev* a=&g_aw[i]; a->armed=0; if(a->sx<sx)continue;
        if(a->kind==1&&aw_is_xhci(a)<0)continue;
        aw_dsw(a->dev,1,sx);
        u8 e=inb(a->en); if(e==0xFF&&inb(a->sts)==0xFF)continue;
        a->was_en=e&a->bit; outb(a->sts,a->bit); outb(a->en,(u8)(e|a->bit)); a->armed=1; g_aw_narmed++; }
    g_aw_arms+=(u32)g_aw_narmed;
    if(!g_aw_narmed){ fg_ok(&aw_fg); return 0; }
    char b[96],*q=b; q=scopy(q,"acpi: "); q=udec(q,(u32)g_aw_narmed); q=scopy(q," wake events armed for sleep"); *q=0; klog(b);
    return g_aw_narmed; }

void acpi_wake_clear(void){ for(int i=0;i<g_awn;i++) if(g_aw[i].armed)outb(g_aw[i].sts,g_aw[i].bit); }

int acpi_wake_poll(void){ for(int i=0;i<g_awn;i++) if(g_aw[i].armed&&(inb(g_aw[i].sts)&g_aw[i].bit))return 1; return 0; }

static void aw_gpe_method(u32 g){
    anode* s=aml_child(g_aroot,"_GPE"); if(!s)return;
    static const char hd[]="0123456789ABCDEF"; char nm[5]={'_','L',hd[(g>>4)&15],hd[g&15],0};
    anode* m=aml_child(s,nm); if(!m){ nm[1]='E'; m=aml_child(s,nm); } if(!m)return;
    aobj o; aml_eval(m,0,0,&o); aobj_free(&o); }

int acpi_wake_disarm(void){
    int hit=-1, any=0;
    for(int i=0;i<g_awn;i++){ struct aw_dev* a=&g_aw[i]; if(!a->armed)continue; any=1;
        if(inb(a->sts)&a->bit){ if(hit<0){ hit=(int)a->gpe; g_aw_wakes++; aw_gpe_method(a->gpe); } outb(a->sts,a->bit); }
        if(!a->was_en){ u8 e=inb(a->en); outb(a->en,(u8)(e&~a->bit)); }
        aw_dsw(a->dev,0,0); a->armed=0; }
    if(!any)return -1;
    g_aw_narmed=0; g_aw_last=hit; fg_ok(&aw_fg);
    if(hit>=0){ char b[64],*q=b; q=scopy(q,"acpi: woken by wake event 0x"); q=hx(q,(u32)hit,2); *q=0; klog(b); }
    return hit; }

const char* acpi_wake_status(void){
    static char b[160]; char* q=b; fg_boot(&aw_fg); aw_scan();
    if(aw_fg.dead)q=scopy(q,"off - turned off after failures");
    else if(!aw_fg.on)q=scopy(q,"off");
    else if(!g_aw_blk_ok){ q=scopy(q,"not available: "); q=scopy(q,g_aw_why[0]?g_aw_why:"no ACPI"); }
    else { q=udec(q,(u32)g_awn); q=scopy(q," wake devices, armed "); q=udec(q,g_aw_arms); q=scopy(q,", wakes "); q=udec(q,g_aw_wakes); q=scopy(q,acpi_wake_kbd_ok(3)?", a key can wake from S3":", no key can wake from S3"); }
    *q=0; return b; }
