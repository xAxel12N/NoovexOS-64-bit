#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/arch/x86_64/cpu/cputemp.h"
#include "include/nvx/drivers/bus/pci.h"

void term_puts(const char*);
void toast(const char*);
void pci_wcfg(int bus,int slot,int fn,int off,u32 v);
extern volatile u64 g_ticks;

int g_ct_ok, g_ct_temp=-1000, g_ct_pkg=-1000, g_ct_tjmax=100, g_ct_hwp, g_ct_mhz;
static int g_ct_vendor, g_ct_dts, g_ct_ptm, g_ct_aperf, g_ct_epp, g_ct_base_mhz;
static u32 g_ct_caps; static u64 g_ct_a0, g_ct_m0, g_ct_t; static u64 g_ct_hot_t;

static int ct_hv(void){ u32 a,b,c,d; cpu_cpuid(1,0,&a,&b,&c,&d); return (int)((c>>31)&1u); }
static u32 ct_smn(u32 reg){ pci_wcfg(0,0,0,0x60,reg); return pci_cfg(0,0,0,0x64); }
static void ct_read(void){
    if(g_ct_vendor==1){
        if(g_ct_dts){ u64 v=0; g_ct_temp=(!cpu_rdmsr_safe(0x19C,&v)&&(v&(1ULL<<31)))?g_ct_tjmax-(int)((v>>16)&0x7F):-1000; }
        if(g_ct_ptm){ u64 v=0; g_ct_pkg=!cpu_rdmsr_safe(0x1B1,&v)?g_ct_tjmax-(int)((v>>16)&0x7F):-1000; } }
    else if(g_ct_vendor==2){ u32 v=ct_smn(0x00059800u); if(v==0xFFFFFFFFu||!v){ g_ct_temp=-1000; return; } int t=(int)((v>>21)&0x7FFu)*125; if(v&(1u<<19))t-=49000; g_ct_temp=t/1000; g_ct_pkg=g_ct_temp; } }
void ctemp_init(void){
    u32 a,b,c,d,mx; cpu_cpuid(0,0,&mx,&b,&c,&d);
    int intel=(b==0x756E6547u&&d==0x49656E69u&&c==0x6C65746Eu), amd=(b==0x68747541u&&d==0x69746E65u&&c==0x444D4163u);
    u32 fam=0,mod=0; cpu_cpuid(1,0,&a,&b,&c,&d); fam=(a>>8)&0xFu; mod=(a>>4)&0xFu; if(fam==0xFu)fam+=(a>>20)&0xFFu; if(fam>=6)mod|=((a>>16)&0xFu)<<4;
    if(ct_hv()){ klog("cpu: running in a virtual machine - temperature and speed control are left to the host"); return; }
    if(mx>=0x16){ cpu_cpuid(0x16,0,&a,&b,&c,&d); g_ct_base_mhz=(int)(a&0xFFFFu); }
    if(intel&&mx>=6){
        cpu_cpuid(6,0,&a,&b,&c,&d); g_ct_dts=(int)(a&1u); g_ct_ptm=(int)((a>>6)&1u); g_ct_aperf=(int)(c&1u); g_ct_epp=(int)((a>>10)&1u);
        static const u8 atom[]={0x1C,0x26,0x27,0x35,0x36,0x37,0x4A,0x4C,0x4D,0x5A,0x5C,0x5D,0x5F,0x7A,0x86,0x96,0x9C,0xBE}; int isatom=0; for(u32 k=0;k<sizeof atom;k++) if(mod==atom[k])isatom=1;
        if(g_ct_dts){ g_ct_vendor=1; if(fam==6&&mod>=0x1A&&!isatom){ u64 tt=0; if(!cpu_rdmsr_safe(0x1A2,&tt)){ int tj=(int)((tt>>16)&0xFF); if(tj>=70&&tj<=130)g_ct_tjmax=tj; } } }
        if((a>>7)&1u){ u64 en=0,caps=0; if(!cpu_rdmsr_safe(0x770,&en)&&((en&1ULL)||!cpu_wrmsr_safe(0x770,1ULL))&&!cpu_rdmsr_safe(0x771,&caps)){ g_ct_caps=(u32)caps; u32 hi=g_ct_caps&0xFFu, lo=(g_ct_caps>>24)&0xFFu;
            if(hi&&lo<=hi){ u64 req=(u64)lo|((u64)hi<<8); if(g_ct_epp)req|=(u64)0x80<<24; if(!cpu_wrmsr_safe(0x774,req))g_ct_hwp=1; } } } }
    else if(amd&&fam>=0x17){ u32 vid=pci_cfg(0,0,0,0); if((vid&0xFFFFu)==0x1022u)g_ct_vendor=2;
        if(mx>=6){ cpu_cpuid(6,0,&a,&b,&c,&d); g_ct_aperf=(int)(c&1u); } }
    if(g_ct_aperf){ if(cpu_rdmsr_safe(0xE8,&g_ct_a0)||cpu_rdmsr_safe(0xE7,&g_ct_m0))g_ct_aperf=0; g_ct_t=g_ticks; }
    ct_read(); g_ct_ok=g_ct_vendor&&g_ct_temp>-100&&g_ct_temp<150;
    char t[140],*p=t; p=scopy(p,"cpu: ");
    if(g_ct_ok){ p=scopy(p,"temperature "); p=udec(p,(u32)(g_ct_temp<0?0:g_ct_temp)); p=scopy(p," C"); if(g_ct_vendor==1){ p=scopy(p," (TjMax "); p=udec(p,(u32)g_ct_tjmax); p=scopy(p," C)"); } }
    else p=scopy(p,"no temperature sensor this driver knows");
    p=scopy(p,g_ct_hwp?", hardware speed control (HWP) on":", no HWP"); *p=0; klog(t); }
void ctemp_poll(void){
    if(!g_ct_vendor&&!g_ct_aperf)return; static u64 last; if(g_ticks-last<200)return; last=g_ticks;
    ct_read();
    if(g_ct_aperf&&g_ct_base_mhz){ u64 a=0, m=0; if(cpu_rdmsr_safe(0xE8,&a)||cpu_rdmsr_safe(0xE7,&m))return; u64 da=a-g_ct_a0, dm=m-g_ct_m0; g_ct_a0=a; g_ct_m0=m; if(dm>1000)g_ct_mhz=(int)((da*(u64)g_ct_base_mhz)/dm); }
    int t=g_ct_pkg>-100?g_ct_pkg:g_ct_temp;
    if(g_ct_ok&&t>=g_ct_tjmax-5&&(!g_ct_hot_t||g_ticks-g_ct_hot_t>30000)){ g_ct_hot_t=g_ticks; char b[80],*p=b; p=scopy(p,"The CPU is very hot ("); p=udec(p,(u32)t); p=scopy(p," C) - check the fans and airflow"); *p=0; klog(b); toast(b); } }
int ctemp_text(char* out,int cap,int what){
    if(cap<32)return 0; char* p=out;
    if(what==0){ if(!g_ct_ok)return 0; int t=g_ct_pkg>-100?g_ct_pkg:g_ct_temp; p=udec(p,(u32)(t<0?0:t)); p=scopy(p," C (limit "); p=udec(p,(u32)g_ct_tjmax); p=scopy(p," C)"); *p=0; return 1; }
    if(!g_ct_mhz&&!g_ct_hwp)return 0; if(g_ct_mhz){ p=udec(p,(u32)g_ct_mhz); p=scopy(p," MHz"); } else p=scopy(p,"-");
    if(g_ct_hwp)p=scopy(p,", HWP on"); *p=0; return 1; }
void ctemp_cmd(void){
    ctemp_poll(); char b[200],*p=b;
    if(!g_ct_vendor){ term_puts("cputemp: no temperature sensor this driver knows (Intel with a digital sensor or AMD Zen needed, not in a VM)\n"); }
    else { p=scopy(p,g_ct_vendor==1?"Intel digital thermal sensor: core ":"AMD Zen sensor (Tctl): "); p=udec(p,(u32)(g_ct_temp<0?0:g_ct_temp)); p=scopy(p," C");
        if(g_ct_ptm){ p=scopy(p,", package "); p=udec(p,(u32)(g_ct_pkg<0?0:g_ct_pkg)); p=scopy(p," C"); }
        if(g_ct_vendor==1){ p=scopy(p,", limit (TjMax) "); p=udec(p,(u32)g_ct_tjmax); p=scopy(p," C"); } *p++='\n'; *p=0; term_puts(b); }
    p=b; if(g_ct_hwp){ p=scopy(p,"Speed control: HWP on, performance levels "); p=udec(p,(g_ct_caps>>24)&0xFFu); p=scopy(p," to "); p=udec(p,g_ct_caps&0xFFu); p=scopy(p," (guaranteed "); p=udec(p,(g_ct_caps>>8)&0xFFu); p=scopy(p,")"); p=scopy(p,g_ct_epp?", balanced energy setting":""); }
    else p=scopy(p,"Speed control: left to the firmware (no HWP)");
    *p++='\n'; *p=0; term_puts(b);
    if(g_ct_mhz){ p=b; p=scopy(p,"Average speed of core 0 while busy: "); p=udec(p,(u32)g_ct_mhz); p=scopy(p," MHz (base "); p=udec(p,(u32)g_ct_base_mhz); p=scopy(p," MHz)\n"); *p=0; term_puts(b); } }
