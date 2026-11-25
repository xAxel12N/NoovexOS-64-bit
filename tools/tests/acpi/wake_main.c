#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#define XHC_MAX 4
#define XHID_MAX 10
enum { AT_NONE=0, AT_INT, AT_STR, AT_BUF, AT_PKG, AT_REF, AT_NAMESTR };
enum { RK_NODE=0, RK_PKG, RK_BUF, RK_SLOT };
enum { K_SCOPE=1, K_DEVICE, K_NAME, K_METHOD, K_REGION, K_FIELD, K_INDEXFIELD, K_BANKFIELD, K_BUFFIELD, K_MUTEX, K_EVENT, K_PROCESSOR, K_POWER };
typedef struct aobj { u8 type, rk, own; u32 len; u64 i; u8* p; struct aobj* e; struct anode* node; } aobj;
typedef struct anode { char nm[5]; struct anode *parent, *child, *next; u8 kind; int gpe, sx, bus, slot, fn, kbd, pkgform, pwr; } anode;
struct aw_blk { u16 b0, b1; u8 l0, l1, base1; };
struct featguard { const char* name; const char* flag; int on, fails, maxfail, dead, trying; };
static void fg_boot(struct featguard* g){ if(!g->trying)g->trying=-1; }
static int fg_begin(struct featguard* g){ fg_boot(g); if(!g->on||g->dead)return 0; g->trying=1; return 1; }
static void fg_ok(struct featguard* g){ if(g->trying==1)g->trying=-1; }
static char lastlog[200]; static void klog(const char* m){ snprintf(lastlog,sizeof lastlog,"%s",m); }
static char* scopy(char* q,const char* s){ while(*s)*q++=*s++; return q; }
static char* udec(char* q,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*q++=t[--n]; return q; }
static char* hx(char* q,u32 v,int d){ for(int i=d-1;i>=0;i--)*q++="0123456789ABCDEF"[(v>>(i*4))&15]; return q; }
static u32 acpi_r32(const u8* p){ u32 v; memcpy(&v,p,4); return v; }
static u64 acpi_r64(const u8* p){ u64 v; memcpy(&v,p,8); return v; }
static u8 g_tbl[3][512];
static const u8* acpi_tbl_ok(u64 a){ return (const u8*)(uintptr_t)a; }
static struct { int ok; u64 rsdp; } g_acpi;
static int g_aml_ok=1; static anode* g_aroot;
static u8 io[0x10000]; static u8 io_w1c[0x10000]; static int kbc_present=1;
static u8 inb(u16 p){ if(p==0x64)return kbc_present?0x1C:0xFF; return io[p]; }
static void outb(u16 p,u8 v){ if(io_w1c[p])io[p]&=(u8)~v; else io[p]=v; }
static int g_xcur, g_xhc_n=1, xhci_init_ok=1, xhci_pci_bus, xhci_pci_slot=0x14, xhci_pci_fn;
static void xhc_select(int i){ g_xcur=i; }
static int xhid_n, xhid_used[XHID_MAX], xhid_iskbd[XHID_MAX], xhid_ctl[XHID_MAX];
static int n_dsw, last_dsw_on=-1, last_dsw_sx=-1, n_psw, n_lxx, n_on; static char last_lxx[5];
static anode* nn(anode* par,const char* nm,u8 k){ anode* n=calloc(1,sizeof *n); memcpy(n->nm,nm,4); n->kind=k; n->parent=par; if(par){ n->next=par->child; par->child=n; } n->gpe=-1; return n; }
static anode* aml_child(anode* n,const char* s4){ char s[4]; int k=0; for(;k<4&&s4[k];k++)s[k]=s4[k]; for(;k<4;k++)s[k]='_'; for(anode* c=n?n->child:0;c;c=c->next) if(!memcmp(c->nm,s,4))return c; return 0; }
static void aobj_free(aobj* o){ if(o->type==AT_PKG&&o->e)free(o->e); o->type=AT_NONE; o->e=0; o->len=0; }
static void aobj_int(aobj* o,u64 v){ aobj_free(o); o->type=AT_INT; o->i=v; }
static int aml_deref(aobj* r,aobj* out){ (void)r; out->type=AT_NONE; return -1; }
static anode* g_pwr;
static int aml_eval(anode* n,aobj* a,int na,aobj* out){
    out->type=AT_NONE; out->e=0; out->len=0; anode* d=n->parent;
    if(!memcmp(n->nm,"_PRW",4)){ if(d->gpe<0)return -1; out->type=AT_PKG; out->len=d->pwr?3:2; out->e=calloc(3,sizeof(aobj));
        if(d->pkgform){ out->e[0].type=AT_PKG; } else { out->e[0].type=AT_INT; out->e[0].i=(u64)d->gpe; }
        out->e[1].type=AT_INT; out->e[1].i=(u64)d->sx; if(d->pwr){ out->e[2].type=AT_REF; out->e[2].rk=RK_NODE; out->e[2].node=g_pwr; } return 0; }
    if(!memcmp(n->nm,"_DSW",4)){ n_dsw++; last_dsw_on=(int)a[0].i; last_dsw_sx=na>1?(int)a[1].i:-1; return 0; }
    if(!memcmp(n->nm,"_PSW",4)){ n_psw++; last_dsw_on=(int)a[0].i; return 0; }
    if(!memcmp(n->nm,"_ON_",4)){ n_on++; return 0; }
    if(n->nm[0]=='_'&&(n->nm[1]=='L'||n->nm[1]=='E')){ n_lxx++; memcpy(last_lxx,n->nm,4); return 0; }
    return -1; }
static int aml_hid_is(anode* d,const char* id){ return d->kbd&&(!strcmp(id,"PNP0303")); }
static int acpi_pci_loc(anode* n,int* b,int* s,int* f,int depth){ (void)depth; if(n->bus<0)return -1; *b=n->bus; *s=n->slot; *f=n->fn; return 0; }
#define AW_HOST
#include "drivers/acpi/acpi_wake.c"

static int fails, oks;
#define CK(c,m) do{ if(c){ oks++; printf("%s ok\n",m); } else { fails++; printf("FAIL %s\n",m); } }while(0)

static void mk_fadt(u8* f,u32 len,u32 g0,u8 l0,u32 g1,u8 l1,u8 base1,int xg){
    memset(f,0,512); memcpy(f,"FACP",4); memcpy(f+4,&len,4); memcpy(f+10,"BOCHS ",6);
    memcpy(f+80,&g0,4); memcpy(f+84,&g1,4); f[92]=l0; f[93]=l1; f[94]=base1;
    if(xg){ f[220]=1; u64 x=xg==2?0:g0; memcpy(f+224,&x,8); } }
static void reset_aw(void){ g_aw_scanned=0; g_awn=0; aw_fg.on=1; aw_fg.dead=0; aw_fg.trying=0; aw_fg.fails=0; }
static void setup_tables(u8 oem_qemu){
    u8* r=g_tbl[0]; u8* x=g_tbl[1]; u8* f=g_tbl[2];
    memset(r,0,64); r[15]=2; u32 rl=36; memcpy(r+20,&rl,4); u64 xa=(u64)(uintptr_t)x; memcpy(r+24,&xa,8);
    memset(x,0,64); memcpy(x,"XSDT",4); u32 l=44; memcpy(x+4,&l,4); u64 fa=(u64)(uintptr_t)f; memcpy(x+36,&fa,8);
    mk_fadt(f,244,0x1800,0x20,0,0,0,0); if(!oem_qemu)memcpy(f+10,"LENOVO",6);
    g_acpi.ok=1; g_acpi.rsdp=(u64)(uintptr_t)r;
    for(int i=0;i<32;i++)io_w1c[0x1800+i]=i<16; }

int main(void){
    struct aw_blk b; u8 f[512]; u16 s,e; u8 bit;
    mk_fadt(f,244,0x1800,0x20,0,0,0,0); CK(!aw_fadt_blk(f,244,&b)&&b.b0==0x1800&&b.l0==0x20&&!b.b1,"FADT GPE0 block read");
    mk_fadt(f,244,0x1800,0x20,0,0,0,1); CK(!aw_fadt_blk(f,244,&b)&&b.b0==0x1800,"FADT X_GPE0 (I/O) preferred");
    mk_fadt(f,244,0,0x20,0,0,0,0); f[220]=0; { u64 m=0xFED00000ULL; memcpy(f+224,&m,8); } CK(aw_fadt_blk(f,244,&b)==-2,"memory-mapped GPE block refused");
    mk_fadt(f,244,0,0,0,0,0,0); CK(aw_fadt_blk(f,244,&b)==-1,"no GPE block found");
    CK(aw_fadt_blk(f,60,&b)==-1,"short FADT refused");
    mk_fadt(f,244,0x400,8,0x500,4,32,0); CK(!aw_fadt_blk(f,244,&b)&&b.b1==0x500&&b.base1==32,"FADT GPE1 block read");
    CK(!aw_gpe_reg(&b,0x0D,&s,&e,&bit)&&s==0x401&&e==0x405&&bit==0x20,"GPE 0x0D maps to GPE0 status/enable byte 1 bit 5");
    CK(!aw_gpe_reg(&b,31,&s,&e,&bit)&&s==0x403&&e==0x407&&bit==0x80,"last GPE0 bit");
    CK(aw_gpe_reg(&b,32+16,&s,&e,&bit)==-1,"GPE past GPE1 refused");
    CK(!aw_gpe_reg(&b,33,&s,&e,&bit)&&s==0x500&&e==0x502&&bit==0x02,"GPE 33 maps into GPE1 block");
    mk_fadt(f,244,0x1800,0x20,0,0,0,0); aw_fadt_blk(f,244,&b);
    CK(!aw_gpe_reg(&b,0x6D,&s,&e,&bit)&&s==0x180D&&e==0x181D&&bit==0x20,"GPE 0x6D on a 32-byte block (Intel PME)");
    aobj o={0}; aobj el[2]={{0}}; o.type=AT_PKG; o.len=2; o.e=el; el[0].type=AT_INT; el[0].i=0x6D; el[1].type=AT_INT; el[1].i=3; u32 g=0,sx=0;
    CK(!aw_prw_decode(&o,&g,&sx)&&g==0x6D&&sx==3,"_PRW {0x6D, 3} decoded");
    el[0].type=AT_PKG; CK(aw_prw_decode(&o,&g,&sx)==-2,"_PRW with a GPE block device refused");
    el[0].type=AT_INT; el[1].i=9; CK(aw_prw_decode(&o,&g,&sx)==-1,"_PRW with bad sleep state refused");
    o.len=1; CK(aw_prw_decode(&o,&g,&sx)==-1,"short _PRW refused");
    o.type=AT_INT; CK(aw_prw_decode(&o,&g,&sx)==-1,"_PRW that is not a package refused");

    setup_tables(0);
    g_aroot=nn(0,"\\___",K_SCOPE); anode* sb=nn(g_aroot,"_SB_",K_DEVICE); anode* gp=nn(g_aroot,"_GPE",K_SCOPE); nn(gp,"_L6D",K_METHOD);
    anode* pci=nn(sb,"PCI0",K_DEVICE); pci->bus=0;
    anode* xh=nn(pci,"XHC_",K_DEVICE); xh->gpe=0x6D; xh->sx=3; xh->bus=0; xh->slot=0x14; xh->fn=0; nn(xh,"_ADR",K_NAME); nn(xh,"_PRW",K_METHOD); nn(xh,"_DSW",K_METHOD); xh->pwr=1;
    g_pwr=nn(g_aroot,"PUBS",K_POWER); nn(g_pwr,"_ON_",K_METHOD);
    anode* hd=nn(pci,"HDAS",K_DEVICE); hd->gpe=0x6D; hd->sx=4; hd->bus=0; hd->slot=0x1F; hd->fn=3; nn(hd,"_ADR",K_NAME); nn(hd,"_PRW",K_METHOD); nn(hd,"_PSW",K_METHOD);
    anode* lan=nn(pci,"GLAN",K_DEVICE); lan->gpe=0x0D; lan->sx=0; lan->bus=0; lan->slot=0x1F; lan->fn=6; nn(lan,"_ADR",K_NAME); nn(lan,"_PRW",K_METHOD);
    reset_aw(); aw_scan();
    CK(g_awn==3&&g_aw_blk_ok,"scan finds 3 wake devices with _PRW and a PCI address");
    CK(n_on>=1,"_PRW power resources turned on");
    CK(aw_is_xhci(&g_aw[0])==0||aw_is_xhci(&g_aw[1])==0||aw_is_xhci(&g_aw[2])==0,"xHCI matched by bus/device/function");
    CK(!acpi_wake_kbd_ok(3),"no USB keyboard and no PS/2 _PRW: no key can wake from S3 on real hardware");
    xhid_n=1; xhid_used[0]=1; xhid_iskbd[0]=1; xhid_ctl[0]=0;
    CK(acpi_wake_kbd_ok(3),"USB keyboard on an xHCI whose _PRW allows S3: a key can wake");
    xh->sx=1; reset_aw(); CK(!acpi_wake_kbd_ok(3),"xHCI _PRW only to S1: S3 not allowed");
    xh->sx=3; reset_aw(); aw_scan();
    io[0x180D]=0x20; io[0x181D]=0x01; n_dsw=n_psw=0;
    int na=acpi_wake_arm(3);
    CK(na==1,"S3 arm: only the xHCI is armed (HDA not ours, LAN only S0)");
    CK(io[0x181D]==0x21,"GPE 0x6D enable bit set, other enables kept");
    CK(io[0x180D]==0,"stale GPE status cleared when arming");
    CK(n_dsw==1&&last_dsw_on==1&&last_dsw_sx==3,"_DSW(1, 3, 3) called");
    CK(aw_fg.trying==1,"featguard running across sleep");
    io[0x180D]=0x20; acpi_wake_clear(); CK(io[0x180D]==0,"clear before SLP_EN drops the status bit");
    CK(!acpi_wake_poll(),"no wake pending");
    io[0x180D]=0x20; CK(acpi_wake_poll(),"poll sees the wake event");
    n_lxx=0; int hit=acpi_wake_disarm();
    CK(hit==0x6D&&g_aw_wakes==1,"disarm reports GPE 0x6D as the wake source");
    CK(n_lxx==1&&!memcmp(last_lxx,"_L6D",4),"\\_GPE._L6D run once on wake");
    CK(io[0x180D]==0,"wake status cleared");
    CK(io[0x181D]==0x01,"enable byte restored to firmware value");
    CK(last_dsw_on==0,"_DSW(0) on wake");
    CK(aw_fg.trying==-1,"featguard ok after a good wake");
    CK(acpi_wake_disarm()==-1,"second disarm does nothing");
    io[0x181D]=0x20; acpi_wake_arm(0); acpi_wake_disarm(); CK(io[0x181D]==0x20,"enable left on when firmware had it on");
    n_psw=0; xh->slot=0x15; reset_aw(); na=acpi_wake_arm(0); CK(na==0,"xHCI not at that address: nothing armed");
    xh->slot=0x14; aw_fg.dead=1; aw_fg.on=1; g_aw_scanned=1; CK(acpi_wake_arm(3)==0,"featguard dead: nothing armed");
    reset_aw(); xh->pkgform=1; aw_scan(); CK(g_awn==2,"GPE block device form skipped");
    xh->pkgform=0; reset_aw();
    xhid_n=0; anode* kb=nn(sb,"PS2K",K_DEVICE); kb->kbd=1; kb->gpe=0x18; kb->sx=3; kb->bus=-1; nn(kb,"_PRW",K_METHOD);
    reset_aw(); CK(acpi_wake_kbd_ok(3),"PS/2 keyboard with _PRW to S3: a key can wake");
    kbc_present=0; reset_aw(); CK(!acpi_wake_kbd_ok(3),"PS/2 _PRW but no controller: no");
    kbc_present=1; kb->gpe=-1; setup_tables(1); reset_aw(); CK(acpi_wake_kbd_ok(3),"QEMU (BOCHS tables) with PS/2: key wake allowed");
    kbc_present=0; reset_aw(); CK(!acpi_wake_kbd_ok(3),"QEMU without PS/2 and no USB keyboard: no");
    kbc_present=1; setup_tables(0); for(int i=0;i<32;i++)io_w1c[0x1800+i]=0; g_tbl[2][92]=0; reset_aw(); aw_scan(); CK(!g_aw_blk_ok&&g_awn==0,"no GPE block: nothing to arm");
    CK(strstr(acpi_wake_status(),"not available")!=0,"status explains why");
    g_acpi.ok=0; reset_aw(); CK(acpi_wake_arm(3)==0,"no ACPI: nothing armed");
    printf(fails?"ACPI WAKE: %d FAILURES\n":"ACPI WAKE: ALL PASS (%d)\n",fails?fails:oks);
    return fails?1:0; }
