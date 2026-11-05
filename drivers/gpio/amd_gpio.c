#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/gpio/amd_gpio.h"
#include "include/nvx/drivers/acpi/aml_scan.h"

void term_puts(const char*);
u64 acpi_hi_map(u64 pa);

int g_amdg_ok, g_amdg_state, g_amdg_pin=-1, g_amdg_pol;
u64 g_amdg_base; static u32 g_amdg_len; static char g_amdg_hid[12];
static u32 g_amdg_samples, g_amdg_rep, g_amdg_norep, g_amdg_agree_lo, g_amdg_agree_hi, g_amdg_skips; static int g_amdg_cur;

static int ag_is_amd(void){ u32 a,b,c,d; cpu_cpuid(0,0,&a,&b,&c,&d); return b==0x68747541u&&d==0x69746E65u&&c==0x444D4163u; }
static int ag_hid(const char* h){ return (h[0]=='A'&&h[1]=='M'&&h[2]=='D'&&h[3]=='I'&&h[4]=='0'&&h[5]=='0'&&h[6]=='3'&&(h[7]=='0'||h[7]=='1')&&!h[8])||(h[0]=='A'&&h[1]=='M'&&h[2]=='D'&&h[3]=='0'&&h[4]=='0'&&h[5]=='3'&&h[6]=='0'&&!h[7]); }
static int ag_map(u64 base,u32 len){
    if(!base||len<0x100)return 0; if(base>=0x100000000ULL&&!acpi_hi_map(base))return 0;
    int good=0; for(int p=0;p<8;p++){ u32 v=*(volatile u32*)(base+(u64)p*4u); if(v!=0xFFFFFFFFu&&v!=0)good++; }
    return good>0; }
void amdg_init(void){
    { extern void (*g_tp_amd_sample)(void); extern void (*g_tp_amd_learn)(int); extern int (*g_tp_amd_ready)(void); extern int (*g_tp_amd_asserted)(void); extern int (*g_tp_amd_setup)(u64,int,int);
      g_tp_amd_sample=amdg_sample; g_tp_amd_learn=amdg_learn; g_tp_amd_ready=amdg_ready; g_tp_amd_asserted=amdg_asserted; g_tp_amd_setup=amdg_tp_setup; }
    g_amdg_ok=0; if(!ag_is_amd())return;
    for(int i=0;i<g_amld_n;i++) if(ag_hid(g_amld[i].hid)&&g_amld[i].has_mem){ u64 b=g_amld[i].mem_base; u32 l=g_amld[i].mem_len?g_amld[i].mem_len:0x400u;
            if(ag_map(b,l)){ g_amdg_base=b; g_amdg_len=l; int k=0; for(;g_amld[i].hid[k]&&k<11;k++)g_amdg_hid[k]=g_amld[i].hid[k]; g_amdg_hid[k]=0; g_amdg_ok=1; break; } }
    if(g_amdg_ok){ char b[120],*p=b; p=scopy(p,"gpio: AMD GPIO controller "); p=scopy(p,g_amdg_ok==1?g_amdg_hid:"(standard FCH address)"); p=scopy(p," at 0x"); p=hx(p,(u32)g_amdg_base,8); p=scopy(p,", "); p=udec(p,g_amdg_len/4u); p=scopy(p," pins"); *p=0; klog(b); } }
int amdg_present(void){ return g_amdg_ok!=0; }
u32 amdg_reg(int pin){ if(!g_amdg_ok||pin<0||(u32)pin*4u+4u>g_amdg_len)return 0xFFFFFFFFu; return *(volatile u32*)(g_amdg_base+(u64)pin*4u); }
int amdg_tp_setup(u64 base,int pin,int active_low){
    g_amdg_state=0; g_amdg_pin=-1;
    if(!g_amdg_ok)amdg_init();
    if(base&&base!=g_amdg_base&&ag_map(base,0x400u)){ g_amdg_base=base; if(!g_amdg_len)g_amdg_len=0x400u; g_amdg_ok=1; }
    if(!g_amdg_ok){ klog("touchpad: AMD GPIO controller not usable - polling the touchpad"); return -1; }
    u32 v=amdg_reg(pin);
    if(v==0xFFFFFFFFu){ klog("touchpad: AMD GPIO interrupt pin reads 0xFFFFFFFF - polling the touchpad"); return -1; }
    if(v&(1u<<23)){ klog("touchpad: AMD GPIO interrupt pin is set as an output - polling the touchpad"); return -1; }
    g_amdg_pin=pin; g_amdg_pol=active_low?0:1; g_amdg_samples=g_amdg_rep=g_amdg_norep=g_amdg_agree_lo=g_amdg_agree_hi=0; g_amdg_state=1;
    klog("touchpad: AMD GPIO interrupt pin found - checking it against real reports before using it");
    return 0; }
void amdg_sample(void){ if(g_amdg_state!=1)return; g_amdg_cur=(int)((amdg_reg(g_amdg_pin)>>16)&1u); }
void amdg_learn(int rep){
    if(g_amdg_state!=1)return; g_amdg_samples++; if(rep)g_amdg_rep++; else g_amdg_norep++;
    int lo=g_amdg_cur==0; if(lo==(rep!=0))g_amdg_agree_lo++; if((!lo)==(rep!=0))g_amdg_agree_hi++;
    if(g_amdg_rep<24||g_amdg_norep<24){ if(g_amdg_samples>6000){ g_amdg_state=3; klog("touchpad: AMD GPIO pin could not be checked - staying with polling"); } return; }
    if(g_amdg_agree_lo==g_amdg_samples){ g_amdg_pol=0; g_amdg_state=2; klog("touchpad: AMD GPIO pin matches the reports (active low) - reading the pin instead of polling"); return; }
    if(g_amdg_agree_hi==g_amdg_samples){ g_amdg_pol=1; g_amdg_state=2; klog("touchpad: AMD GPIO pin matches the reports (active high) - reading the pin instead of polling"); return; }
    g_amdg_state=3; klog("touchpad: AMD GPIO pin does not match the reports - staying with polling"); }
int amdg_ready(void){ return g_amdg_state==2; }
int amdg_asserted(void){ u32 v=amdg_reg(g_amdg_pin); if(v==0xFFFFFFFFu)return 1; int lvl=(int)((v>>16)&1u); int a=g_amdg_pol?lvl:!lvl; if(!a)g_amdg_skips++; return a; }

static void ag_line(int pin){
    char b[200],*p=b; u32 v=amdg_reg(pin);
    p=scopy(p,"  pin "); p=udec(p,(u32)pin); p=scopy(p,": 0x"); p=hx(p,v,8);
    if(v==0xFFFFFFFFu){ p=scopy(p,"  (no register)\n"); *p=0; term_puts(b); return; }
    p=scopy(p,(v&(1u<<23))?"  output":"  input"); p=scopy(p,(v&(1u<<16))?" high":" low");
    if(v&(1u<<20))p=scopy(p," pull-up"); if(v&(1u<<21))p=scopy(p," pull-down");
    if(v&(1u<<11)){ p=scopy(p,"  interrupt on ("); p=scopy(p,(v&(1u<<8))?"level":"edge"); u32 al=(v>>9)&3u; p=scopy(p,al==0?", active high":al==1?", active low":", both edges"); p=scopy(p,(v&(1u<<12))?", unmasked)":", masked)"); }
    else p=scopy(p,"  interrupt off");
    if(v&(1u<<28))p=scopy(p,"  status set"); if((v>>5)&3u)p=scopy(p,"  debounce on");
    *p++='\n'; *p=0; term_puts(b); }
void amdg_cmd(const char* a){
    while(*a==' ')a++;
    if(!g_amdg_ok)amdg_init();
    if(!g_amdg_ok){ term_puts("No AMD GPIO controller (AMDI0030) found on this computer.\n"); return; }
    char b[160],*p=b; p=scopy(p,"AMD GPIO controller "); p=scopy(p,g_amdg_ok==1?g_amdg_hid:"(standard FCH address)"); p=scopy(p," at 0x"); p=hx(p,(u32)g_amdg_base,8); p=scopy(p,", "); p=udec(p,g_amdg_len/4u); p=scopy(p," pins\n"); *p=0; term_puts(b);
    if(*a>='0'&&*a<='9'){ int n=0; while(*a>='0'&&*a<='9'){ n=n*10+(*a-'0'); a++; } ag_line(n); return; }
    if(g_amdg_pin>=0){ p=b; p=scopy(p,"Touchpad interrupt pin: "); p=scopy(p,g_amdg_state==2?"in use":g_amdg_state==1?"being checked against reports":g_amdg_state==3?"not used (polling)":"not set up"); p=scopy(p,"  checks "); p=udec(p,g_amdg_samples); p=scopy(p,"  skipped reads "); p=udec(p,g_amdg_skips); *p++='\n'; *p=0; term_puts(b); ag_line(g_amdg_pin); }
    else term_puts("No touchpad uses an AMD GPIO pin.   amdgpio <pin>   shows one pin\n"); }
