#include "include/nvx/base.h"
#include "include/nvx/drivers/acpi/aml.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "drivers/video/amd/amd.h"

int amd_dmub_backlight(int pct);

typedef struct { int init, mode, dcn, cur; anode* atif; u16 atif_ver; u32 atif_fn, atif_nmask; int qbtc, ac, dc, minsig, maxsig; u32 r_cntl, r_period, r_lock, period, cntl0; u32 sets, fails; char why[48]; } amd_bl_t;
static amd_bl_t g_abl;

static void abl_lz(aobj* o){ o->type=AT_NONE; o->rk=0; o->p=0; o->e=0; o->own=0; o->len=0; o->i=0; o->cont=0; o->node=0; o->idx=0; }
static void abl_why(const char* s){ int i=0; while(s[i]&&i<47){ g_abl.why[i]=s[i]; i++; } g_abl.why[i]=0; }
static anode* abl_find_atif(anode* n,int depth){
    if(!n||depth>10)return 0;
    for(anode* c=n->child;c;c=c->next){ if(c->kind!=K_DEVICE&&c->kind!=K_SCOPE)continue; anode* m=aml_child(c,"ATIF"); if(m&&m->kind==K_METHOD)return m; anode* r=abl_find_atif(c,depth+1); if(r)return r; }
    return 0; }
static int abl_atif(u32 fn,const u8* in,u32 inlen,u8* out,u32 cap){
    if(!g_abl.atif||!g_aml_ok)return -1;
    aobj a[2], o; abl_lz(&a[0]); abl_lz(&a[1]); abl_lz(&o); aobj_int(&a[0],fn);
    u8 tmp[8]; if(in&&inlen){ for(u32 i=0;i<inlen&&i<8;i++)tmp[i]=in[i]; a[1].type=AT_BUF; a[1].p=tmp; a[1].len=inlen>8?8:inlen; } else aobj_int(&a[1],0);
    u64 dl=g_aml_hard_deadline; g_aml_hard_deadline=0; int r=aml_eval(g_abl.atif,a,2,&o); g_aml_hard_deadline=dl;
    if(!r&&o.type==AT_REF){ aobj d; abl_lz(&d); aml_deref(&o,&d); aobj_free(&o); o=d; }
    int n=-1; if(!r&&o.type==AT_BUF&&o.p&&o.len>=2){ u32 sz=o.p[0]|((u32)o.p[1]<<8); if(sz>o.len)sz=o.len; if(sz>cap)sz=cap; for(u32 i=0;i<sz;i++)out[i]=o.p[i]; n=(int)sz; }
    if(in&&inlen&&a[1].p==tmp){ a[1].type=AT_NONE; a[1].p=0; a[1].len=0; }
    aobj_free(&o); aobj_free(&a[0]); aobj_free(&a[1]); return n; }
static u32 abl_u32(const u8* p){ return p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static void abl_atif_probe(void){
    g_abl.minsig=12; g_abl.maxsig=255; g_abl.ac=-1; g_abl.dc=-1;
    if(!g_aml_ok||!g_aroot)return; g_abl.atif=abl_find_atif(g_aroot,0); if(!g_abl.atif)return;
    u8 b[256]; int n=abl_atif(0,0,0,b,sizeof b); if(n<12){ g_abl.atif=0; return; }
    g_abl.atif_ver=(u16)(b[2]|(b[3]<<8)); g_abl.atif_nmask=abl_u32(b+4); g_abl.atif_fn=abl_u32(b+8);
    if(!(g_abl.atif_fn&(1u<<15)))return;
    u8 q[3]={3,0,0}; n=abl_atif(0x10,q,3,b,sizeof b); if(n<10||b[4])return;
    if(b[7]<b[8]&&b[8]){ g_abl.minsig=b[7]; g_abl.maxsig=b[8]; g_abl.ac=b[5]; g_abl.dc=b[6]; g_abl.qbtc=1; } }
static int abl_dcn_regs(void){
    u32 v=amd_ipver(AMD_IP_DCN), base=g_amd.disc_ok?g_amd.ip[AMD_IP_DCN].base[2]:0; u16 d=g_amd.did; int kind=0;
    if(v==AMD_IPV(2,1,0))kind=1; else if(v==AMD_IPV(3,1,2)||v==AMD_IPV(3,1,3))kind=2;
    else if(!g_amd.disc_ok){ if(d==0x1636||d==0x1638||d==0x164C)kind=1; else if(d==0x1681)kind=2; }
    if(!kind){ abl_why("DCN version without known PWM offsets"); return 0; }
    if(!base)base=0x34C0;
    if(kind==1){ g_abl.r_cntl=base+0x2888; g_abl.r_period=base+0x288A; g_abl.r_lock=base+0x288B; g_abl.dcn=21; }
    else { g_abl.r_cntl=base+0x2F19; g_abl.r_period=base+0x2F1B; g_abl.r_lock=base+0x2F1C; g_abl.dcn=31; }
    return 1; }
static void abl_pwm_probe(void){
    if(!g_amd.found||!g_amd.mmio){ abl_why("no AMD GPU MMIO"); return; }
    if(!abl_dcn_regs())return;
    u32 c=amd_rreg(g_abl.r_cntl), p=amd_rreg(g_abl.r_period), l=amd_rreg(g_abl.r_lock);
    if(c==0xFFFFFFFFu||p==0xFFFFFFFFu||l==0xFFFFFFFFu){ abl_why("PWM registers unreadable"); return; }
    if(!(c>>31)){ abl_why("BL_PWM not enabled by firmware"); return; }
    g_abl.cntl0=c; g_abl.period=p&0xFFFF; if(!g_abl.period)g_abl.period=0xFFFF; g_abl.mode=2; }
static void abl_init(void){
    if(g_abl.init)return; g_abl.init=1; g_abl.cur=-1; abl_why("none");
    abl_atif_probe();
    if(g_bl.ok&&g_bl.out){ g_abl.mode=1; return; }
    abl_pwm_probe(); }
static int abl_pwm_get(void){
    u32 c=amd_rreg(g_abl.r_cntl); if(c==0xFFFFFFFFu)return -1; u32 duty=c&0xFFFF; u32 sig=(u32)(((u64)duty*255+g_abl.period/2)/g_abl.period); if(sig>255)sig=255;
    int lo=g_abl.minsig, hi=g_abl.maxsig; if((int)sig<=lo)return 0; if((int)sig>=hi)return 100; return (int)(((sig-(u32)lo)*100+(u32)(hi-lo)/2)/(u32)(hi-lo)); }
static int abl_pwm_set(int pct){
    u32 sig=(u32)g_abl.minsig+(u32)((g_abl.maxsig-g_abl.minsig)*pct+50)/100; u32 duty=(u32)(((u64)g_abl.period*sig+127)/255); if(duty>0xFFFF)duty=0xFFFF; if(!duty)duty=1;
    u32 l=amd_rreg(g_abl.r_lock); if(l==0xFFFFFFFFu)return -1;
    amd_wreg(g_abl.r_lock,l|1u); u32 c=amd_rreg(g_abl.r_cntl); if(c==0xFFFFFFFFu||!(c>>31)){ amd_wreg(g_abl.r_lock,l&~1u); return -1; }
    amd_wreg(g_abl.r_cntl,(c&0xFFFF0000u)|duty); amd_wreg(g_abl.r_lock,l&~1u);
    if(amd_wait(g_abl.r_lock,0x100u,0,50000)==-2)return -1;
    return (amd_rreg(g_abl.r_cntl)&0xFFFFu)==duty?0:-2; }
int amd_backlight_set(int pct){
    abl_init(); if(pct<0)pct=0; if(pct>100)pct=100;
    if(g_abl.mode==1){ bl_set(pct); g_abl.cur=g_bl.cur; g_abl.sets++; return 0; }
    if(pct<1)pct=1;
    int r=g_abl.mode==2?abl_pwm_set(pct):-2;
    if(g_abl.mode==2&&!r){ g_abl.cur=pct; g_abl.sets++; return 0; }
    if(r!=-2||amd_dmub_backlight(pct)){ g_abl.fails++; return -1; }
    g_abl.mode=3;
    g_abl.cur=pct; g_abl.sets++; return 0; }
int amd_backlight_get(void){
    abl_init();
    if(g_abl.mode==1)return g_bl.ok?g_bl.cur:-1;
    if(g_abl.mode==2){ int v=abl_pwm_get(); if(v>=0)g_abl.cur=v; return v; }
    return g_abl.mode==3?g_abl.cur:-1; }
int amd_backlight_step(int dir){ int c=amd_backlight_get(); if(c<0)return -1; int n=c+(dir>0?10:-10); if(n>100)n=100; if(n<1)n=1; if(amd_backlight_set(n))return -1; return amd_backlight_get(); }
static char* abl_s(char* p,char* e,const char* s){ while(*s&&p<e)*p++=*s++; return p; }
static char* abl_d(char* p,char* e,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v&&n<11); while(n&&p<e)*p++=t[--n]; return p; }
static char* abl_x(char* p,char* e,u32 v,int w){ for(int i=w-1;i>=0&&p<e;i--)*p++="0123456789ABCDEF"[(v>>(i*4))&15]; return p; }
int amd_bl_describe(char* out,int cap){
    if(!out||cap<2)return 0; abl_init(); char* p=out; char* e=out+cap-1;
    p=abl_s(p,e,"backlight: "); p=abl_s(p,e,g_abl.mode==1?"ACPI _BCM (firmware)":g_abl.mode==2?"DCN BL_PWM":g_abl.mode==3?"DMCUB ABM (firmware owns the PWM)":"not controllable");
    int c=amd_backlight_get(); if(c>=0){ p=abl_s(p,e,", "); p=abl_d(p,e,(u32)c); p=abl_s(p,e,"%"); }
    if(!g_abl.mode){ p=abl_s(p,e," ("); p=abl_s(p,e,g_abl.why); p=abl_s(p,e,")"); }
    p=abl_s(p,e,"\n  ATIF: ");
    if(!g_abl.atif)p=abl_s(p,e,"not present");
    else { char path[64]; aml_path(g_abl.atif,path,64); p=abl_s(p,e,path); p=abl_s(p,e," v"); p=abl_d(p,e,g_abl.atif_ver); p=abl_s(p,e," functions 0x"); p=abl_x(p,e,g_abl.atif_fn,8); p=abl_s(p,e," notify 0x"); p=abl_x(p,e,g_abl.atif_nmask,8); }
    p=abl_s(p,e,"\n  input signal "); p=abl_d(p,e,(u32)g_abl.minsig); p=abl_s(p,e,"-"); p=abl_d(p,e,(u32)g_abl.maxsig); p=abl_s(p,e,g_abl.qbtc?" (from ATIF)":" (default)");
    if(g_abl.qbtc){ p=abl_s(p,e,", default AC "); p=abl_d(p,e,(u32)g_abl.ac); p=abl_s(p,e," DC "); p=abl_d(p,e,(u32)g_abl.dc); }
    if(g_abl.r_cntl){ p=abl_s(p,e,"\n  DCN "); p=abl_s(p,e,g_abl.dcn==21?"2.1":"3.1"); p=abl_s(p,e," BL_PWM_CNTL 0x"); p=abl_x(p,e,amd_rreg(g_abl.r_cntl),8); p=abl_s(p,e," PERIOD 0x"); p=abl_x(p,e,amd_rreg(g_abl.r_period),8); p=abl_s(p,e," LOCK 0x"); p=abl_x(p,e,amd_rreg(g_abl.r_lock),8); }
    if(g_abl.sets||g_abl.fails){ p=abl_s(p,e,"\n  sets "); p=abl_d(p,e,g_abl.sets); p=abl_s(p,e," failures "); p=abl_d(p,e,g_abl.fails); }
    p=abl_s(p,e,"\n"); *p=0; return (int)(p-out); }
