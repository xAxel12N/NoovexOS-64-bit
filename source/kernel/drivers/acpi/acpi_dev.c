#include "include/nvx/base.h"
#include "include/nvx/mm/heap.h"
#include "include/nvx/mm/pmm_vmm.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/lib/js.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/net/http.h"
#include "include/nvx/gui/wm.h"
#include "include/nvx/core/power.h"
#include "include/nvx/drivers/acpi/aml.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "include/nvx/arch/x86_64/hrtimer.h"
#include "include/nvx/arch/x86_64/smp_sched.h"
struct nvx_g_aec_t g_aec;

static u64 acpi_now_ns(void){ return hr_precise()?hr_ns():g_ticks*10000000ULL; }
static volatile u32* g_glock; static u32 g_gl_fail, g_gl_waits, g_gl_held;
static int acpi_gl_hook(int acq){
    if(!g_glock)return 0;
    if(!acq){ u32 o,n; do{ o=*g_glock; n=o&~3u; }while(!__sync_bool_compare_and_swap(g_glock,o,n)); g_gl_held=0;
        if((o&1)&&g_acpi.pm1a_cnt&&!g_acpi.hwred)outw((u16)g_acpi.pm1a_cnt,(u16)(inw((u16)g_acpi.pm1a_cnt)|4)); return 0; }
    u64 end=acpi_now_ns()+50000000ULL;
    for(int round=0;round<1000;round++){
        u32 o,n; do{ o=*g_glock; n=(o&~1u)|2u; if(o&2)n|=1u; }while(!__sync_bool_compare_and_swap(g_glock,o,n));
        if(!(n&1)){ g_gl_held=1; return 0; }
        g_gl_waits++;
        for(int i=0;i<200000;i++){ if(!(*g_glock&2))break; if(acpi_now_ns()>end)break; io_wait(); }
        if(acpi_now_ns()>end){ do{ o=*g_glock; n=o; if(o&2)break; n=o&~1u; }while(!__sync_bool_compare_and_swap(g_glock,o,n)); g_gl_fail++; return -1; } }
    g_gl_fail++; return -1; }
static void aec_lock(int on){ if(g_aec.glk){ if(on)aml_gl(1); else aml_gl(0); } }
struct nvx_g_acs_t g_acs;
void (*g_acpi_pwrbtn_cb)(void);
static int aec_wait(int want_obf){
    for(int t=0;t<20000;t++){ u8 s=inb(g_aec.cmd); if(want_obf?(s&1):!(s&2))return 0; io_wait(); }
    g_aec.timeouts++; if(++g_aec.fails>=8){ g_aec.fails=0; g_aec.trips++; g_aec.dead_until=acpi_now_ns()+5000000000ULL; } return -1; }
static int aec_dead(void){ if(!g_aec.dead_until)return 0; if(acpi_now_ns()<g_aec.dead_until)return 1; g_aec.dead_until=0; return 0; }
static void aec_flush(void){ for(int k=0;k<4&&(inb(g_aec.cmd)&1);k++)(void)inb(g_aec.data); }
static int aec_read_raw(u8 a,u8* v){
    aec_flush(); if(aec_wait(0))return -1; outb(g_aec.cmd,0x80); if(aec_wait(0))return -1; outb(g_aec.data,a); if(aec_wait(1))return -1; *v=inb(g_aec.data); g_aec.reads++; g_aec.fails=0; return 0; }
static int aec_write_raw(u8 a,u8 v){
    if(aec_wait(0))return -1; outb(g_aec.cmd,0x81); if(aec_wait(0))return -1; outb(g_aec.data,a); if(aec_wait(0))return -1; outb(g_aec.data,v); if(aec_wait(0))return -1; g_aec.writes++; g_aec.fails=0; return 0; }
int aec_read(u8 a,u8* v){ if(!g_aec.ok||aec_dead())return -1; aec_lock(1); int r=aec_read_raw(a,v); aec_lock(0); return r; }
static int aec_write(u8 a,u8 v){ if(!g_aec.ok||aec_dead())return -1; aec_lock(1); int r=aec_write_raw(a,v); aec_lock(0); return r; }
static int aec_query(u8* q){
    if(!g_aec.ok||aec_dead()||!(inb(g_aec.cmd)&0x20))return 0;
    aec_lock(1); int r=0; if(!aec_wait(0)){ outb(g_aec.cmd,0x84); if(!aec_wait(1)){ *q=inb(g_aec.data); g_aec.queries++; g_aec.fails=0; r=*q?1:0; } } aec_lock(0); return r; }
static int acpi_crs_io(anode* dev,u16* ports,int max){
    anode* c=aml_child(dev,"_CRS"); if(!c)return 0; aobj o; if(aml_eval(c,0,0,&o)||o.type!=AT_BUF){ aobj_free(&o); return 0; }
    int n=0; u32 i=0;
    while(i<o.len&&n<max){ u8 t=o.p[i];
        if(t&0x80){ if(i+3>o.len)break; u32 l=(u32)o.p[i+1]|((u32)o.p[i+2]<<8); i+=3+l; continue; }
        u32 l=t&7; u8 nm=(t>>3)&0x0F; if(nm==0x0F)break;
        if(nm==0x08&&l>=7&&i+1+l<=o.len)ports[n++]=(u16)(o.p[i+2]|(o.p[i+3]<<8));
        else if(nm==0x09&&l>=3&&i+1+l<=o.len)ports[n++]=(u16)((o.p[i+1]|(o.p[i+2]<<8))&0x3FF);
        i+=1+l; }
    aobj_free(&o); return n; }
const u8* acpi_find_table(const char* sig,int nth){
    u64 rsdp=acpi_find_rsdp(); if(!rsdp)return 0; const u8* r=(const u8*)rsdp; u64 sdt; int w=4;
    if(r[15]>=2&&acpi_r32(r+20)>=36&&acpi_r64(r+24)){ sdt=acpi_r64(r+24); w=8; } else sdt=acpi_r32(r+16);
    const u8* x=acpi_tbl_ok(sdt); if(!x)return 0; u32 n=(acpi_r32(x+4)-36)/(u32)w;
    for(u32 i=0;i<n;i++){ u64 e=w==8?acpi_r64(x+36+i*8):acpi_r32(x+36+i*4); const u8* t=acpi_tbl_ok(e); if(!t)continue;
        if(t[0]==sig[0]&&t[1]==sig[1]&&t[2]==sig[2]&&t[3]==sig[3]){ if(nth--==0)return t; } }
    return 0; }
static void aec_setup(void){
    g_aec.ok=0; g_aec.dev=0;
    const u8* e=acpi_find_table("ECDT",0);
    if(e&&acpi_r32(e+4)>=65&&e[36]==1&&e[48]==1){ u64 c=acpi_r64(e+40), d=acpi_r64(e+52); if(c&&d&&c<0x10000&&d<0x10000){ g_aec.cmd=(u16)c; g_aec.data=(u16)d; g_aec.gpe=e[64]; g_aec.from_ecdt=1;
        u32 l=acpi_r32(e+4); if(l>65){ char path[64]; u32 k=0; while(k<63&&65+k<l&&e[65+k]){ path[k]=(char)e[65+k]; k++; } path[k]=0; g_aec.dev=aml_find(path); } } }
    if(!g_aec.dev){ anode* d[4]; int n=aml_find_devices(g_aroot,"PNP0C09",d,4,0,0); for(int i=0;i<n;i++){ if(!(aml_sta(d[i])&1))continue; g_aec.dev=d[i]; break; } }
    if(g_aec.dev&&!g_aec.from_ecdt){ u16 pt[4]; int n=acpi_crs_io(g_aec.dev,pt,4); if(n>=2){ g_aec.data=pt[0]; g_aec.cmd=pt[1]; } else { g_aec.data=0x62; g_aec.cmd=0x66; }
        anode* gp=aml_child(g_aec.dev,"_GPE"); u64 v=0; if(gp&&!aml_eval_int(gp,&v))g_aec.gpe=(u32)v; }
    if(g_aec.dev){ anode* gl=aml_child(g_aec.dev,"_GLK"); u64 v=0; if(gl&&!aml_eval_int(gl,&v)&&v)g_aec.glk=1; }
    if(!g_aec.dev&&!g_aec.from_ecdt)return;
    if(inb(g_aec.cmd)==0xFF){ klog("acpi: EC ports read 0xFF, not using the EC"); return; }
    g_aec.ok=1; aec_flush(); if(aec_wait(0)){ g_aec.ok=0; klog("acpi: EC stuck busy"); return; }
    klog(g_aec.from_ecdt?"acpi: embedded controller found (ECDT)":"acpi: embedded controller found (namespace)"); }
static int acpi_pci_loc(anode* n,int* bus,int* dev,int* fn,int depth){
    if(!n||n==g_aroot||depth>8)return -1;
    if(aml_hid_is(n,"PNP0A08")||aml_hid_is(n,"PNP0A03")){ anode* b=aml_child(n,"_BBN"); u64 v=0; if(b&&aml_eval_int(b,&v))v=0; *bus=(int)(v&0xFF); *dev=-1; *fn=-1; return 0; }
    anode* a=aml_child(n,"_ADR"); if(!a)return acpi_pci_loc(n->parent,bus,dev,fn,depth+1);
    u64 adr=0; if(aml_eval_int(a,&adr))return -1;
    int pb=0,pd=-1,pf=-1; if(acpi_pci_loc(n->parent,&pb,&pd,&pf,depth+1))return -1;
    if(pd>=0){ outl(0xCF8,(1u<<31)|((u32)pb<<16)|((u32)pd<<11)|((u32)pf<<8)|0x18); pb=(int)((inl(0xCFC)>>8)&0xFF); }
    *bus=pb; *dev=(int)((adr>>16)&0x1F); *fn=(int)(adr&7); return 0; }
u32 g_acpi_memblock;
static u64 acpi_mem_rd(u64 a,int w){ if(w==1)return *(volatile u8*)a; if(w==2)return *(volatile u16*)a; if(w==4)return *(volatile u32*)a; return *(volatile u64*)a; }
static int acpi_mem_wr(u64 a,int w,u64 v){
    if(mm_known()){ u64 bs,be; u32 bt; if(!mm_first_bad(a,a+(u64)w,&bs,&be,&bt)||bs!=a||be<a+(u64)w){ g_acpi_memblock++; return -1; } }
    else if(a<0x10000000ULL||(a+(u64)w>heap_base&&a<heap_top)){ g_acpi_memblock++; return -1; }
    if(w==1)*(volatile u8*)a=(u8)v; else if(w==2)*(volatile u16*)a=(u16)v; else if(w==4)*(volatile u32*)a=(u32)v; else *(volatile u64*)a=v; return 0; }
static int acpi_region_pci(anode* rg,int* b,int* d,int* f){
    if(!rg)return -1; anode* n=rg->parent;
    if(n&&(aml_hid_is(n,"PNP0A08")||aml_hid_is(n,"PNP0A03"))){ anode* bb=aml_child(n,"_BBN"); u64 v=0; if(bb&&aml_eval_int(bb,&v))v=0; anode* ad=aml_child(n,"_ADR"); u64 adr=0; if(ad&&aml_eval_int(ad,&adr))adr=0;
        *b=(int)(v&0xFF); *d=(int)((adr>>16)&0x1F); *f=(int)(adr&7); return 0; }
    if(acpi_pci_loc(n,b,d,f,0)||*d<0)return -1; return 0; }
static int acpi_rd(u8 sp,u64 a,int w,u64* v,anode* rg){
    *v=0;
    if(sp==0){ if(a+(u64)w>0x100000000ULL||a<0x1000)return -1;
        *v=acpi_mem_rd(a,w); return 0; }
    if(sp==1){ if(a>0xFFFF)return -1; u16 p=(u16)a; if(w==1)*v=inb(p); else if(w==2)*v=inw(p); else if(w==4)*v=inl(p); else *v=(u64)inl(p)|((u64)inl((u16)(p+4))<<32); return 0; }
    if(sp==2){ int b,d,f; if(acpi_region_pci(rg,&b,&d,&f))return -1; u32 off=(u32)a;
        for(int k=0;k<w;k++){ u32 o=off+(u32)k; if(o>0xFF)return -1; outl(0xCF8,(1u<<31)|((u32)b<<16)|((u32)d<<11)|((u32)f<<8)|(o&0xFC)); *v|=(u64)inb((u16)(0xCFC+(o&3)))<<(8*k); } return 0; }
    if(sp==3){ u8 x; if(a>0xFF||aec_read((u8)a,&x))return -1; *v=x; return 0; }
    if(sp==5){ if(a>0xFF)return -1; if(a<0x80){ outb(0x70,(u8)a); *v=inb(0x71); } else { outb(0x72,(u8)a); *v=inb(0x73); } return 0; }
    return -1; }
static int acpi_wr(u8 sp,u64 a,int w,u64 v,anode* rg){
    if(sp==0){ if(a+(u64)w>0x100000000ULL||a<0x1000)return -1;
        return acpi_mem_wr(a,w,v); }
    if(sp==1){ if(a>0xFFFF)return -1; u16 p=(u16)a; if(w==1)outb(p,(u8)v); else if(w==2)outw(p,(u16)v); else if(w==4)outl(p,(u32)v); else { outl(p,(u32)v); outl((u16)(p+4),(u32)(v>>32)); } return 0; }
    if(sp==2){ int b,d,f; if(acpi_region_pci(rg,&b,&d,&f))return -1; u32 off=(u32)a;
        for(int k=0;k<w;k++){ u32 o=off+(u32)k; if(o>0xFF)return -1; outl(0xCF8,(1u<<31)|((u32)b<<16)|((u32)d<<11)|((u32)f<<8)|(o&0xFC)); outb((u16)(0xCFC+(o&3)),(u8)(v>>(8*k))); } return 0; }
    if(sp==3){ if(a>0xFF)return -1; return aec_write((u8)a,(u8)v); }
    if(sp==5){ if(a>0xFF)return -1; if(a<0x80){ outb(0x70,(u8)a); outb(0x71,(u8)v); } else { outb(0x72,(u8)a); outb(0x73,(u8)v); } return 0; }
    return -1; }
static void acpi_sleep_ms(u32 ms){ if(hr_precise()){ hr_delay_us(ms*1000u); return; } for(u32 i=0;i<ms*1000u;i++)io_wait(); }
static u64 acpi_timer100ns(void){ return hr_precise()?hr_ns()/100ULL:(u64)g_ticks*100000ULL; }
static void acpi_stall_us(u32 us){ if(hr_precise())hr_delay_us(us); else for(u32 i=0;i<us;i++)io_wait(); }

acbat g_acb[2]; int g_acb_n=0; anode* g_acad=0; int g_ac_online=-1; int g_acb_pct=-1, g_acb_charging=0, g_acb_mins=-1; static u64 g_acb_t=0;
static u32 acpi_pkg_u32(const aobj* o,u32 i){ if(o->type!=AT_PKG||i>=o->len)return 0xFFFFFFFFu; const aobj* e=&o->e[i]; if(e->type==AT_INT)return (u32)e->i; if(e->type==AT_BUF||e->type==AT_STR)return (u32)aobj_to_int(e); return 0xFFFFFFFFu; }
static void acpi_pkg_str(const aobj* o,u32 i,char* out,int cap){ out[0]=0; if(o->type!=AT_PKG||i>=o->len)return; const aobj* e=&o->e[i]; int n=0;
    if(e->type==AT_STR||e->type==AT_BUF){ for(u32 k=0;k<e->len&&n<cap-1;k++){ u8 c=e->p[k]; if(!c)break; out[n++]=(c>=0x20&&c<0x7F)?(char)c:'?'; } }
    while(n>0&&out[n-1]==' ')n--; out[n]=0; }
static void acbat_info(acbat* b){
    aobj o; anode* m=aml_child(b->dev,"_BIX"); int bix=1; if(!m){ m=aml_child(b->dev,"_BIF"); bix=0; } if(!m)return;
    if(aml_eval(m,0,0,&o)||o.type!=AT_PKG){ aobj_free(&o); return; }
    int s=bix?1:0; if(bix&&o.len<20){ aobj_free(&o); return; } if(!bix&&o.len<13){ aobj_free(&o); return; }
    b->mwh=acpi_pkg_u32(&o,(u32)s)==0; b->design=acpi_pkg_u32(&o,(u32)s+1); b->full=acpi_pkg_u32(&o,(u32)s+2); b->tech=acpi_pkg_u32(&o,(u32)s+3); b->volt=acpi_pkg_u32(&o,(u32)s+4);
    b->warn=acpi_pkg_u32(&o,(u32)s+5); b->low=acpi_pkg_u32(&o,(u32)s+6); b->cycles=bix?acpi_pkg_u32(&o,8):0xFFFFFFFFu;
    int ms=bix?16:9; acpi_pkg_str(&o,(u32)ms,b->model,sizeof b->model); acpi_pkg_str(&o,(u32)ms+1,b->serial,sizeof b->serial); acpi_pkg_str(&o,(u32)ms+2,b->type,sizeof b->type); acpi_pkg_str(&o,(u32)ms+3,b->oem,sizeof b->oem);
    b->have_info=1; aobj_free(&o); }
static void acbat_status(acbat* b){
    aobj o; anode* m=aml_child(b->dev,"_BST"); if(!m)return; if(aml_eval(m,0,0,&o)||o.type!=AT_PKG||o.len<4){ aobj_free(&o); return; }
    b->state=acpi_pkg_u32(&o,0); b->rate=acpi_pkg_u32(&o,1); b->remain=acpi_pkg_u32(&o,2); b->pvolt=acpi_pkg_u32(&o,3); aobj_free(&o); }
void acpi_battery_update(int full){
    if(!g_acs.up)return;
    u64 tot_rem=0, tot_full=0, rate=0; int any=0, chg=0, dis=0;
    for(int i=0;i<g_acb_n;i++){ acbat* b=&g_acb[i]; int st=aml_sta(b->dev); int pr=(st&0x10)!=0;
        if(pr&&(!b->present||full||!b->have_info))acbat_info(b); b->present=pr; if(!pr)continue;
        acbat_status(b); if(b->remain==0xFFFFFFFFu)continue;
        u32 cap=(b->full&&b->full!=0xFFFFFFFFu)?b->full:b->design; if(!cap||cap==0xFFFFFFFFu)continue;
        u32 rem=b->remain>cap?cap:b->remain; tot_rem+=rem; tot_full+=cap; any=1;
        if(b->state&2)chg=1; if(b->state&1)dis=1; if(b->rate!=0xFFFFFFFFu&&b->rate<0x7FFFFFFFu)rate+=b->rate; }
    if(g_acad){ anode* p=aml_child(g_acad,"_PSR"); u64 v=0; if(p&&!aml_eval_int(p,&v))g_ac_online=v?1:0; }
    if(!any){ g_acb_pct=-1; g_acb_mins=-1; return; }
    g_acb_pct=(int)((tot_rem*100+tot_full/2)/tot_full); if(g_acb_pct>100)g_acb_pct=100;
    g_acb_charging=chg||(g_ac_online==1&&!dis);
    g_acb_mins=-1; if(rate>0){ if(dis&&!chg)g_acb_mins=(int)(tot_rem*60/rate); else if(chg&&tot_full>tot_rem)g_acb_mins=(int)((tot_full-tot_rem)*60/rate); }
    if(g_acb_mins>99*60)g_acb_mins=-1; }
struct nvx_g_bl_t g_bl;

u64 acpi_hi_map(u64 pa){
    if(pa<0x100000000ULL)return pa; if(pa>=0x800000000000ULL)return 0;
    int i4=(int)(pa>>39); if(i4==8)return 0;
    u64 cr3; cr3=cpu_get_pt(); u64* pml4=(u64*)(cr3&~0xFFFULL);
    if(!(pml4[i4]&1)){ u64* np=(u64*)dma_alloc_pages(1); if(!np)return 0; for(int k=0;k<512;k++)np[k]=0; pml4[i4]=(u64)np|3ULL; }
    u64* pdpt=(u64*)(pml4[i4]&0x000FFFFFFFFFF000ULL); int i3=(int)((pa>>30)&511);
    if(!(pdpt[i3]&1)){ u64* pd=(u64*)dma_alloc_pages(1); if(!pd)return 0; u64 gb=pa&~0x3FFFFFFFULL; for(int k=0;k<512;k++)pd[k]=(gb+((u64)k<<21))|0x9BULL; pdpt[i3]=(u64)pd|3ULL; cpu_set_pt(cr3); smp_tlb_shootdown(); }
    return pa; }
static u32 bl_rd(u32 o){ return *(volatile u32*)(g_bl.mmio+o); }
static void bl_wr(u32 o,u32 v){ *(volatile u32*)(g_bl.mmio+o)=v; }
static void bl_intel_probe(void){
    u32 id=pci_cfg(0,2,0,0); if((id&0xFFFF)!=0x8086)return; if((pci_cfg(0,2,0,8)>>24)!=0x03)return;
    u32 lo=pci_cfg(0,2,0,0x10); u64 bar=lo&0xFFFFFFF0u; if(((lo>>1)&3)==2)bar|=(u64)pci_cfg(0,2,0,0x14)<<32; if(!bar||!(pci_cfg(0,2,0,4)&2))return;
    u64 m=acpi_hi_map(bar); if(!m)return; g_bl.mmio=m;
    u32 c1=bl_rd(0xC8250), r54=bl_rd(0xC8254), r58=bl_rd(0xC8258), cpu=bl_rd(0x48250), r48=bl_rd(0x48254);
    if(c1==0xFFFFFFFFu||r54==0xFFFFFFFFu||r58==0xFFFFFFFFu||cpu==0xFFFFFFFFu||r48==0xFFFFFFFFu){ g_bl.mmio=0; return; }
    if((cpu>>31)&&(r54>>16)&&(r48&0xFFFF)&&(r48&0xFFFF)<=(r54>>16)&&!(r58&&r58<=r54)){ g_bl.pwm_kind=3; g_bl.pwm_max=r54>>16; }
    else if((c1>>31)&&r58&&r58<=r54){ g_bl.pwm_kind=1; g_bl.pwm_max=r54; }
    else if((c1>>31)&&(r54>>16)&&(r54&0xFFFF)<=(r54>>16)){ g_bl.pwm_kind=2; g_bl.pwm_max=r54>>16; }
    else g_bl.mmio=0; }
static int bl_pwm_get(void){ if(!g_bl.pwm_kind||!g_bl.pwm_max)return -1; u32 d=g_bl.pwm_kind==1?bl_rd(0xC8258):g_bl.pwm_kind==2?(bl_rd(0xC8254)&0xFFFF):(bl_rd(0x48254)&0xFFFF); return (int)((u64)d*100/g_bl.pwm_max); }
static void bl_pwm_set(int pct){ if(!g_bl.pwm_kind||!g_bl.pwm_max)return; u32 d=(u32)((u64)g_bl.pwm_max*(u32)pct/100); if(d<1)d=1;
    if(g_bl.pwm_kind==1)bl_wr(0xC8258,d); else if(g_bl.pwm_kind==2)bl_wr(0xC8254,(bl_rd(0xC8254)&0xFFFF0000u)|d); else bl_wr(0x48254,(bl_rd(0x48254)&0xFFFF0000u)|d); }
static int bl_find(anode* n,int depth){
    if(depth>12)return 0;
    for(anode* c=n->child;c;c=c->next){ if(c->kind==K_METHOD||c->kind==K_NAME)continue;
        if(c->kind==K_DEVICE&&aml_child(c,"_BCM")&&aml_child(c,"_BCL")){ aobj o; if(!aml_eval(aml_child(c,"_BCL"),0,0,&o)&&o.type==AT_PKG&&o.len>=3){
                int n2=0; u8 seen[101]={0}; for(u32 k=2;k<o.len;k++){ u32 v=acpi_pkg_u32(&o,k); if(v<=100&&!seen[v]){ seen[v]=1; } }
                for(int v=0;v<=100;v++) if(seen[v])g_bl.lv[n2++]=(u8)v; aobj_free(&o);
                if(n2>=2){ g_bl.nlv=n2; g_bl.out=c; g_bl.bus=c->parent; return 1; } } else aobj_free(&o); }
        if(c->child&&bl_find(c,depth+1))return 1; }
    return 0; }
static int bl_nearest(int pct){ int best=0, bd=1000; for(int i=0;i<g_bl.nlv;i++){ int d=g_bl.lv[i]-pct; if(d<0)d=-d; if(d<bd){ bd=d; best=i; } } return best; }
static int bl_get(void){ if(!g_bl.ok)return -1; return g_bl.cur; }
void bl_set(int pct){
    if(!g_bl.ok)return; if(pct>100)pct=100; if(pct<g_bl.minlv)pct=g_bl.minlv;
    if(g_bl.out){ int i=bl_nearest(pct); pct=g_bl.lv[i]; aobj a,r; a.type=AT_NONE; a.p=0; a.e=0; a.own=0; aobj_int(&a,(u64)pct); aml_eval(aml_child(g_bl.out,"_BCM"),&a,1,&r); aobj_free(&r); aobj_free(&a); }
    bl_pwm_set(pct); g_bl.cur=pct; g_bl.sets++; }
int bl_cur_pct(void){ return g_bl.ok?g_bl.cur:-1; }
void bl_step(int dir){
    if(!g_bl.ok)return;
    if(g_bl.out){ int i=bl_nearest(g_bl.cur)+dir; if(i>=g_bl.nlv)i=g_bl.nlv-1; if(i<0)i=0; while(i<g_bl.nlv-1&&g_bl.lv[i]<g_bl.minlv)i++; bl_set(g_bl.lv[i]); }
    else bl_set(g_bl.cur+dir*10); }
static void bl_setup(void){
    g_bl.ok=0; g_bl.out=0; bl_find(g_aroot,0); bl_intel_probe();
    if(!g_bl.out&&!g_bl.pwm_kind)return;
    g_bl.minlv=5; if(g_bl.out){ int m=100; for(int i=0;i<g_bl.nlv;i++) if(g_bl.lv[i]>0&&g_bl.lv[i]<m)m=g_bl.lv[i]; g_bl.minlv=m; }
    if(g_bl.bus){ anode* d=aml_child(g_bl.bus,"_DOS"); if(d){ aobj a,r; a.type=AT_NONE; a.p=0; a.e=0; a.own=0; aobj_int(&a,4); aml_eval(d,&a,1,&r); aobj_free(&r); aobj_free(&a); } }
    g_bl.cur=-1; if(g_bl.out){ anode* q=aml_child(g_bl.out,"_BQC"); u64 v; if(q&&!aml_eval_int(q,&v)&&v<=100){ g_bl.bqc=1; g_bl.cur=g_bl.lv[bl_nearest((int)v)]; } }
    if(g_bl.cur<0){ int p=bl_pwm_get(); g_bl.cur=p>=0?p:100; }
    g_bl.mode=g_bl.out?(g_bl.pwm_kind?3:1):2; g_bl.ok=1; }
static int g_acb_dirty;
static void acpi_on_notify(anode* n,u32 v){
    g_acs.notifies++;
    for(int i=0;i<g_acb_n;i++) if(n==g_acb[i].dev){ g_acb_dirty|=v==0x81?3:1; return; }
    if(n==g_acad){ g_acb_dirty|=1; return; }
    if(g_bl.ok&&(n==g_bl.out||n==g_bl.bus)){ if(v==0x86||v==0x87){ bl_step(v==0x86?1:-1); { extern void osd_show(int,int,const char*); extern void klog_key(u32); klog_key(0x30000u|v); osd_show(2,g_bl.cur<0?0:g_bl.cur,"Brightness"); } return; } if(v==0x85){ bl_step(1); { extern void osd_show(int,int,const char*); osd_show(2,g_bl.cur<0?0:g_bl.cur,"Brightness"); } return; } if(v==0x80||v==0x82){ extern void hk_push(int); hk_push(15); return; } if(v==0x88||v==0x89||v==0x8A)return; }
    { extern int laptop_notify(anode*,u32); if(laptop_notify(n,v))return; }
    { extern void klog_key(u32); klog_key(0x40000u|(v&0xFFFF)); }
    g_acs.unhandled++; }
void acpi_drain_notify(void){ anode* n; u32 v; for(int k=0;k<32&&aml_notify_pop(&n,&v);k++)acpi_on_notify(n,v); if(g_acb_dirty){ int full=(g_acb_dirty&2)!=0; g_acb_dirty=0; acpi_battery_update(full); } }
static void acpi_ec_event(u8 q){
    static const char hx[]="0123456789ABCDEF"; char nm[5]={'_','Q',hx[q>>4],hx[q&15],0}; g_aec.lastq=q; g_aec.events++;
    if(g_aec.dev){ anode* m=aml_child(g_aec.dev,nm); if(m){ aobj r; aml_eval(m,0,0,&r); aobj_free(&r); } }
    acpi_drain_notify(); }
void acpi_poll(void){
    if(!g_acs.up)return; g_aml_sleep_pool=300; g_aml_time_ms=300; g_aml_hard_deadline=acpi_now_ns()+500000000ULL;
    for(int k=0;k<8;k++){ u8 q; if(!aec_query(&q))break; acpi_ec_event(q); }
    if(g_acpi.pm1a_evt&&g_acs.acpi_mode&&!g_acpi.hwred){ u16 st=inw((u16)g_acpi.pm1a_evt); if(st!=0xFFFF&&(st&0x100)){ outw((u16)g_acpi.pm1a_evt,0x100); if(g_ticks-g_acs.pwrbtn_t>100){ g_acs.pwrbtn_t=g_ticks; if(g_acpi_pwrbtn_cb)g_acpi_pwrbtn_cb(); } } }
    acpi_drain_notify();
    if(g_acb_n&&g_ticks-g_acb_t>=500){ g_acb_t=g_ticks; acpi_battery_update(0); } g_aml_hard_deadline=0; }
static u8* g_acpi_copies[24]; static int g_acpi_ncopies;
static int acpi_load_one(const u8* t){
    u32 len=acpi_r32(t+4); if(len<=36)return 0; u8* c=(u8*)kmalloc(len); if(!c)return -1; for(u32 i=0;i<len;i++)c[i]=t[i]; if(g_acpi_ncopies<24)g_acpi_copies[g_acpi_ncopies++]=c;
    g_acs.tables++; int r=aml_load_table(c+36,len-36); if(r)g_acs.bad_tables++; return r; }
void acpi_aml_init(void){
    if(g_acs.up)return; u64 t0=g_ticks;
    aml_rd_hook=acpi_rd; aml_wr_hook=acpi_wr; aml_sleep_hook=acpi_sleep_ms; aml_timer_hook=acpi_timer100ns; aml_now_hook=acpi_now_ns; aml_stall_hook=acpi_stall_us; aml_gl_hook=acpi_gl_hook;
    g_aml_sleep_pool=5000; g_aml_time_ms=3000; u64 tns0=acpi_now_ns(); g_aml_hard_deadline=tns0+15000000000ULL;
    const u8* fadt=acpi_find_table("FACP",0); if(!fadt){ klog("acpi: no FADT, AML disabled"); return; }
    u32 fl=acpi_r32(fadt+4); u64 d=acpi_r32(fadt+40); if(fl>=148&&acpi_r64(fadt+140))d=acpi_r64(fadt+140);
    const u8* dsdt=acpi_tbl_ok(d); if(!dsdt||dsdt[0]!='D'||dsdt[1]!='S'||dsdt[2]!='D'||dsdt[3]!='T'){ klog("acpi: DSDT unreadable, AML disabled"); return; }
    g_aml_rev=dsdt[8]>=2?2:1;
    { u64 fc=acpi_r32(fadt+36); if(fl>=140&&acpi_r64(fadt+132))fc=acpi_r64(fadt+132); if(fc&&fc<0x100000000ULL){ const u8* fx=(const u8*)fc; if(fx[0]=='F'&&fx[1]=='A'&&fx[2]=='C'&&fx[3]=='S'&&acpi_r32(fx+4)>=64)g_glock=(volatile u32*)(fx+16); } }
    if(aml_init_ns()){ klog("acpi: out of memory for the namespace"); return; }
    acpi_load_one(dsdt);
    for(int i=0;i<16;i++){ const u8* s=acpi_find_table("SSDT",i); if(!s)break; acpi_load_one(s); }
    if(!g_acpi.hwred&&g_acpi.pm1a_cnt&&g_acpi.smi_cmd&&g_acpi.acpi_en){ acpi_enable_mode(); }
    g_acs.acpi_mode=g_acpi.hwred||(g_acpi.pm1a_cnt&&(inw((u16)g_acpi.pm1a_cnt)&1));
    if(g_acpi.pm1a_evt&&!g_acpi.hwred)outw((u16)g_acpi.pm1a_evt,0x100);
    aml_run_reg(g_aroot,0,(1u<<0)|(1u<<1)|(1u<<2));
    aec_setup(); if(g_aec.ok){ aml_run_reg(g_aroot,0,1u<<3); g_acs.ec_reg=1; }
    aml_run_ini(g_aroot,0);
    anode* d2[4]; int nb=aml_find_devices(g_aroot,"PNP0C0A",d2,2,0,0); g_acb_n=0; for(int i=0;i<nb;i++){ acbat* b=&g_acb[g_acb_n++]; for(u32 k=0;k<sizeof *b;k++)((u8*)b)[k]=0; b->dev=d2[i]; }
    anode* ad[2]; g_acad=aml_find_devices(g_aroot,"ACPI0003",ad,1,0,0)?ad[0]:0;
    g_acs.up=1; { extern int g_aml_ok; g_aml_ok=1; } acpi_battery_update(1); g_acb_t=g_ticks; bl_setup(); acpi_drain_notify();
    g_acs.ms=(u32)((acpi_now_ns()-tns0)/1000000ULL); (void)t0; g_aml_hard_deadline=0;
    int k=0; const char* e=g_aml_lasterr; while(e[k]&&k<63){ g_acs.errs[k]=e[k]; k++; } g_acs.errs[k]=0;
    klog(g_acb_n?"acpi: AML up, battery found":"acpi: AML up, no battery");
    if(g_bl.ok)klog(g_bl.mode==2?"acpi: backlight via Intel PWM":"acpi: backlight via ACPI _BCM"); }
void acpi_power_button(void){ klog("acpi: power button pressed, shutting down"); power_off(); }
