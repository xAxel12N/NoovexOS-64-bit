#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/sysfeat.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/drivers/acpi/aml.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "include/nvx/drivers/input/touchpad/i2c_hid.h"
#include "include/nvx/drivers/video/nvidia/nvmon.h"
#include "include/nvx/drivers/usb/ucsi.h"
#include "include/nvx/shell/terminal.h"

extern u64 g_aml_hard_deadline;
extern volatile u64 g_ticks;
static struct featguard uca_fg={"USB-C port status (UCSI)","feat-ucsi.cfg",1,0,3,0,0};
static const u8 UCA_UUID[16]={0xC2,0x98,0x83,0x6F,0xA4,0x7C,0xE4,0x11,0xAD,0x36,0x63,0x10,0x42,0xB5,0x00,0x8F};
static ucsi_t g_uca; static anode* g_uca_dev; static anode* g_uca_dsm; static u64 g_uca_pa, g_uca_va, g_uca_at; static u32 g_uca_len; static int g_uca_probed;
static char g_uca_path[48];

static void uca_zero(aobj* o){ o->type=AT_NONE; o->p=0; o->e=0; o->own=0; o->len=0; o->i=0; o->cont=0; o->node=0; }
static int uca_eval(anode* m,aobj* a,int n,aobj* out){
    if(!m||!g_aml_ok)return -1; u64 dl=g_aml_hard_deadline; g_aml_hard_deadline=0; int r=aml_eval(m,a,n,out); g_aml_hard_deadline=dl;
    if(!r&&out->type==AT_REF){ aobj d; uca_zero(&d); aml_deref(out,&d); aobj_free(out); *out=d; }
    return r; }
static int uca_call(int fn,u64* res){
    aobj a[4]; for(int k=0;k<4;k++)uca_zero(&a[k]);
    u8* ub=(u8*)kmalloc(16); if(!ub)return -1; for(int k=0;k<16;k++)ub[k]=UCA_UUID[k];
    a[0].type=AT_BUF; a[0].p=ub; a[0].len=16; aobj_int(&a[1],1); aobj_int(&a[2],(u64)fn); a[3].type=AT_PKG; a[3].len=0; a[3].e=0;
    aobj o; uca_zero(&o); int r=uca_eval(g_uca_dsm,a,4,&o); for(int k=0;k<4;k++)aobj_free(&a[k]);
    if(!r&&res)*res=(o.type==AT_INT||o.type==AT_BUF)?aobj_to_int(&o):0;
    aobj_free(&o); return r; }
static int uca_rd(void* c,u32 off,void* b,u32 n){ (void)c; if(!g_uca_va||off+n>g_uca_len)return -1; volatile u8* s=(volatile u8*)(g_uca_va+off); u8* d=(u8*)b; for(u32 i=0;i<n;i++)d[i]=s[i]; return 0; }
static int uca_wr(void* c,u32 off,const void* b,u32 n){ (void)c; if(!g_uca_va||off+n>g_uca_len)return -1; volatile u8* d=(volatile u8*)(g_uca_va+off); const u8* s=(const u8*)b; for(u32 i=0;i<n;i++)d[i]=s[i]; return 0; }
static int uca_dsm(void* c,int fn){ (void)c; return uca_call(fn,0)?-1:0; }
static void uca_delay(void* c,u32 ms){ (void)c; ih_delay_ms((int)ms); }
static const ucsi_ops uca_ops={uca_rd,uca_wr,uca_dsm,uca_delay,0,1000};

static int uca_crs(anode* d){
    anode* c=aml_child(d,"_CRS"); if(!c)return -1; aobj o; uca_zero(&o);
    if(uca_eval(c,0,0,&o)||o.type!=AT_BUF||!o.p){ aobj_free(&o); return -1; }
    const u8* b=o.p; u32 n=o.len, i=0;
    while(i<n&&!g_uca_pa){ u8 t=b[i]; if(t==0x79)break;
        if(t&0x80){ if(i+3>n)break; u32 L=(u32)b[i+1]|((u32)b[i+2]<<8); const u8* e=b+i; if(i+3+L>n)break;
            if(t==0x86&&L>=9){ g_uca_pa=(u64)e[4]|((u64)e[5]<<8)|((u64)e[6]<<16)|((u64)e[7]<<24); g_uca_len=(u32)e[8]|((u32)e[9]<<8)|((u32)e[10]<<16)|((u32)e[11]<<24); }
            else if(t==0x87&&L>=23&&e[3]==0){ g_uca_pa=(u64)e[10]|((u64)e[11]<<8)|((u64)e[12]<<16)|((u64)e[13]<<24); g_uca_len=(u32)e[22]|((u32)e[23]<<8)|((u32)e[24]<<16)|((u32)e[25]<<24); }
            else if(t==0x8A&&L>=43&&e[3]==0){ u64 a=0, l=0; for(int k=7;k>=0;k--){ a=(a<<8)|e[14+k]; l=(l<<8)|e[38+k]; } g_uca_pa=a; g_uca_len=l>0x10000?0x10000:(u32)l; }
            i+=3+L; }
        else i+=1+(t&7); }
    aobj_free(&o); return g_uca_pa?0:-1; }
static const char* uca_why(const char* s){ int i=0; while(s[i]&&i<47){ g_uca.why[i]=s[i]; i++; } g_uca.why[i]=0; return g_uca.why; }
int ucsi_probe(void){
    if(g_uca_probed)return g_uca.state==UCSI_ST_OK?0:-1;
    g_uca_probed=1; g_uca.state=UCSI_ST_NONE; g_uca.why[0]=0;
    fg_boot(&uca_fg); if(!uca_fg.on||uca_fg.dead){ g_uca.state=UCSI_ST_OFF; return -1; }
    if(!g_aml_ok||!g_aroot){ uca_why("no ACPI namespace"); return -1; }
    anode* d[2]; int n=aml_find_devices(g_aroot,"PNP0CA0",d,2,0,0); if(n<1)n=aml_find_devices(g_aroot,"USBC000",d,2,0,0);
    if(n<1){ uca_why("no UCSI device (PNP0CA0) in ACPI"); return -1; }
    g_uca_dev=d[0]; aml_path(d[0],g_uca_path,sizeof g_uca_path);
    if(!(aml_sta(d[0])&1)){ uca_why("UCSI device turned off by the firmware"); return -1; }
    g_uca_dsm=aml_child(d[0],"_DSM"); if(!g_uca_dsm){ uca_why("UCSI device has no _DSM"); return -1; }
    if(uca_crs(d[0])||g_uca_len<48||g_uca_pa<0x1000){ uca_why("UCSI mailbox address not found in _CRS"); return -1; }
    if(!fg_begin(&uca_fg)){ g_uca.state=UCSI_ST_OFF; return -1; }
    u64 m=0; if(uca_call(0,&m)||(m&6)!=6){ fg_fail(&uca_fg,"_DSM read/write functions missing"); uca_why("_DSM read/write functions missing"); g_uca.state=UCSI_ST_FAILED; return -1; }
    if(g_uca_len>0x1000)g_uca_len=0x1000; g_uca_va=acpi_hi_map(g_uca_pa); if(!g_uca_va){ fg_fail(&uca_fg,"mailbox not mappable"); uca_why("mailbox not mappable"); g_uca.state=UCSI_ST_FAILED; return -1; }
    if(ucsi_core_init(&g_uca,&uca_ops)){ char w[48]; scopy(w,g_uca.why)[0]=0; fg_fail(&uca_fg,w); g_uca.state=UCSI_ST_FAILED; klog2("usbc: UCSI failed: ",w); return -1; }
    fg_ok(&uca_fg); g_uca_at=g_ticks;
    { char b[120],*p=b; p=scopy(p,"usbc: UCSI "); p=udec(p,g_uca.ver>>8); *p++='.'; p=udec(p,(g_uca.ver>>4)&15); p=scopy(p," at "); p=scopy(p,g_uca_path); p=scopy(p,", mailbox 0x"); p=uhex(p,g_uca_pa,8); p=scopy(p,", "); p=udec(p,g_uca.nconn); p=scopy(p," USB-C ports"); *p=0; klog(b); }
    return 0; }
int ucsi_state(void){ return g_uca.state; }
int ucsi_count(void){ return g_uca.state==UCSI_ST_OK?g_uca.nconn:0; }
void ucsi_tick(int force){ if(ucsi_probe())return; if(!force&&g_ticks-g_uca_at<300)return; g_uca_at=g_ticks; u32 t0=g_uca.timeouts; ucsi_core_refresh(&g_uca,&uca_ops);
    if(g_uca.timeouts>t0+1){ fg_fail(&uca_fg,"port status requests timed out"); if(uca_fg.dead)g_uca.state=UCSI_ST_OFF; } }
int ucsi_row(int i,char* t,int tn,char* d,int dn){ if(i<0||i>=ucsi_count())return -1; char x[96]; ucsi_conn_text(&g_uca.c[i],x,sizeof x,d,dn); char* p=t; char* e=t+tn-1; const char* h="USB-C port "; while(*h&&p<e)*p++=*h++; if(p<e)*p++=(char)('1'+i); h=": "; while(*h&&p<e)*p++=*h++; h=x; while(*h&&p<e)*p++=*h++; *p=0; return 0; }
int ucsi_dp_active(void){ int n=0; for(int i=0;i<ucsi_count();i++) if(g_uca.c[i].connected&&g_uca.c[i].dp)n++; return n; }
int ucsi_gpu_text(char* b,int n){
    char* p=b; char* e=b+n-1; char tmp[320]; char* q=tmp;
    extern int gpu_list(void*,int); struct { u16 ven, dev; u8 bus, slot; const char* vendor; const char* name; const char* driver; int accel; } g[4]; int ng=gpu_list(g,4); int dpgpu=0;
    for(int i=0;i<ng;i++){ if(q>tmp)q=scopy(q,"; "); q=scopy(q,g[i].vendor?g[i].vendor:"GPU"); q=scopy(q,": ");
        if(g[i].ven==0x10DE){ char s[4][96]; int k=nvm_screens(s,4), dp=0; for(int j=0;j<k;j++){ const char* x=s[j]; for(;*x;x++) if(x[0]=='D'&&x[1]=='i'&&x[2]=='s'&&x[3]=='p'){ dp++; break; } }
            if(k<0)q=scopy(q,"display engine not read"); else { q=udec(q,(u32)dp); q=scopy(q," DisplayPort head(s) lit"); } dpgpu+=dp; }
        else if(g[i].ven==0x8086)q=scopy(q,"USB-C DisplayPort is routed by the firmware (Type-C subsystem); screen shows if the firmware lit it");
        else if(g[i].ven==0x1002)q=scopy(q,"USB-C DisplayPort goes through the DCN/DMUB firmware; screen shows if the firmware lit it");
        else q=scopy(q,"no USB-C display support"); if(q>tmp+100)break; }
    *q=0; const char* h=ucsi_dp_active()?(dpgpu?"DP alt mode active and a GPU drives DisplayPort. ":"DP alt mode active. "):"No DP alt mode active. ";
    while(*h&&p<e)*p++=*h++; h=tmp; while(*h&&p<e)*p++=*h++; *p=0; return ucsi_dp_active(); }
int ucsi_enabled(void){ fg_boot(&uca_fg); return uca_fg.on&&!uca_fg.dead; }
void ucsi_set_enabled(int on){ fg_enable(&uca_fg,on); if(!on){ g_uca.state=UCSI_ST_OFF; g_uca_va=0; } g_uca_probed=0; }
const char* ucsi_status(void){
    fg_boot(&uca_fg); if(!uca_fg.on)return "Off"; if(uca_fg.dead)return g_uca.why[0]?g_uca.why:"Off after repeated failures";
    if(g_uca.state==UCSI_ST_OK)return "On - reading port status from the firmware"; return g_uca.why[0]?g_uca.why:"Not started"; }
static void uca_term(const char* s){ term_puts(s); term_putc('\n'); }
void ucsi_shell(const char* arg,void (*out)(const char*)){
    char b[200],*p; if(!arg)arg=""; if(!out)out=uca_term;
    if(seq(arg,"off")){ ucsi_set_enabled(0); out("usbc: UCSI off"); return; }
    if(seq(arg,"on")){ ucsi_set_enabled(1); out("usbc: UCSI on"); }
    ucsi_tick(1);
    if(g_uca.state!=UCSI_ST_OK){ p=b; p=scopy(p,"usbc: "); p=scopy(p,ucsi_status()); *p=0; out(b); return; }
    p=b; p=scopy(p,"UCSI "); p=udec(p,g_uca.ver>>8); *p++='.'; p=udec(p,(g_uca.ver>>4)&15); p=scopy(p,"  "); p=scopy(p,g_uca_path); p=scopy(p,"  mailbox 0x"); p=uhex(p,g_uca_pa,8); p=scopy(p,"  PD "); p=hx(p,g_uca.pdver,4); p=scopy(p,"  Type-C "); p=hx(p,g_uca.tcver,4);
    p=scopy(p,"  cmds "); p=udec(p,g_uca.cmds); p=scopy(p," errs "); p=udec(p,g_uca.errs); p=scopy(p," timeouts "); p=udec(p,g_uca.timeouts); *p=0; out(b);
    for(int i=0;i<g_uca.nconn;i++){ ucsi_conn* c=&g_uca.c[i]; char t[96], d[128]; ucsi_row(i,t,sizeof t,d,sizeof d); out(t); p=b; p=scopy(p,"    "); p=scopy(p,d); *p=0; out(b);
        if(c->connected){ p=b; p=scopy(p,"    role "); p=scopy(p,c->source?"source":"sink"); p=scopy(p,"  partner "); p=scopy(p,ucsi_ptype_name(c->ptype)); p=scopy(p,"  RDO 0x"); p=hx(p,c->rdo,8); p=scopy(p,"  PDO 0x"); p=hx(p,c->pdo,8);
            if(c->nsvid){ p=scopy(p,"  modes"); for(int k=0;k<c->nsvid;k++){ *p++=' '; p=hx(p,c->svid[k],4); } }
            if(c->ncam){ p=scopy(p,"  active"); for(int k=0;k<c->ncam;k++){ *p++=' '; p=hx(p,c->cam[k],4); } }
            if(c->dp_vdo){ p=scopy(p,"  DP VDO 0x"); p=hx(p,c->dp_vdo,8); } *p=0; out(b); }
        if(seq(arg,"raw")){ p=b; p=scopy(p,"    status"); for(int k=0;k<20;k++){ *p++=' '; p=hx(p,c->raw[k],2); } *p=0; out(b); } }
    ucsi_gpu_text(b,sizeof b); out(b); }
