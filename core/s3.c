#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/power.h"
#include "include/nvx/core/sysfeat.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "include/nvx/drivers/acpi/aml.h"
#include "include/nvx/drivers/acpi/acpi_wake.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/arch/x86_64/cpu/cpu_init.h"
#include "include/nvx/arch/x86_64/smp/smp_sched.h"
#include "include/nvx/arch/x86_64/interrupts/interrupts.h"
#include "include/nvx/drivers/storage/nvme.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/drivers/rtc/cmos.h"
#include "include/nvx/drivers/net/ethernet/nic.h"
#include "include/nvx/core/sound.h"
#include "include/nvx/net/wifi/wifi_mgr.h"

#define S3_TB 0x9000ULL
#define S3_NPCI 96
extern const unsigned char s3_wake_start[], s3_wake_end[];
int s3_save(void);
extern int g_acpi_have_s3; extern u32 g_acpi_s3a, g_acpi_s3b;
extern volatile int g_cores_online;

static struct featguard s3_fg={"S3 suspend to RAM","feat-s3.cfg",1,0,3,0,0};
int s3_enable(int on){ if(on<0){ fg_boot(&s3_fg); return s3_fg.on&&!s3_fg.dead; } fg_enable(&s3_fg,on); return on; }
static struct { u8 b,s,f,hdr; u32 c[16]; } s3_pci[S3_NPCI];
static int s3_npci;
static u16 s3_vbe[10];
static u8 s3_fpu[4096] __attribute__((aligned(64)));
u32 g_s3_count, g_s3_fail;
char g_s3_why[96];

static u64 s3_facs(void){
    const u8* f=acpi_find_table("FACP",0); if(!f)return 0; u32 fl=acpi_r32(f+4); u64 a=0;
    if(fl>=140)a=acpi_r64(f+132); if(!a&&fl>=40)a=acpi_r32(f+36);
    if(!a||a>=0x100000000ULL)return 0; const u8* t=(const u8*)a;
    if(t[0]!='F'||t[1]!='A'||t[2]!='C'||t[3]!='S'||acpi_r32(t+4)<64)return 0; return a; }
static int s3_vbe_ok(void){ outw(0x1CE,0); u16 id=inw(0x1CF); return id>=0xB0C0&&id<=0xB0CF; }
static void s3_vbe_save(void){ for(int i=1;i<10;i++){ outw(0x1CE,(u16)i); s3_vbe[i]=inw(0x1CF); } }
static void s3_vbe_load(void){ outw(0x1CE,4); outw(0x1CF,0); for(int i=1;i<10;i++){ if(i==4)continue; outw(0x1CE,(u16)i); outw(0x1CF,s3_vbe[i]); } outw(0x1CE,4); outw(0x1CF,s3_vbe[4]); }
int bt_hci_up(void);
extern int g_snd_usb;
int wifi_hw_generic(void);
static const char* s3_gpu_c; static int s3_gpu_done;
static const char* s3_gpu_scan(void){
    int vga=0;
    for(int b=0;b<256;b++) for(int s=0;s<32;s++){ u32 id=pci_cfg(b,s,0,0); if((id&0xFFFF)==0xFFFF)continue;
        int nf=(pci_cfg(b,s,0,0x0C)&0x800000)?8:1;
        for(int f=0;f<nf;f++){ id=pci_cfg(b,s,f,0); if((id&0xFFFF)==0xFFFF)continue; if((pci_cfg(b,s,f,8)>>24)!=0x03)continue;
            u16 v=(u16)id;
            if(v==0x10DE)return "NVIDIA graphics: restarting GSP firmware after S3 is not safe yet";
            if(v==0x1002)return "AMD graphics: the driver has no mode set to bring the screen back after S3";
            if(v==0x8086)return "Intel graphics: the driver has no mode set to bring the screen back after S3";
            if(v!=0x1234&&v!=0x1AF4&&v!=0x1B36)return "the screen cannot be restored after S3 on this graphics hardware";
            vga=1; } }
    return vga&&s3_vbe_ok()?0:"the screen cannot be restored after S3 on this graphics hardware"; }
static const char* s3_gpu_why(void){ if(!s3_gpu_done){ s3_gpu_c=s3_gpu_scan(); s3_gpu_done=1; } return s3_gpu_c; }
static u64 s3_days(int y,int m,int d){ y-=m<=2; i64 era=(y>=0?y:y-399)/400; u32 yoe=(u32)(y-era*400); u32 doy=(u32)((153*(m+(m>2?-3:9))+2)/5+d-1); u32 doe=yoe*365+yoe/4-yoe/100+doy; return (u64)(era*146097+(i64)doe-719468); }
static u64 s3_rtc(void){
    int a[6], b[6];
    for(int t=0;t<5;t++){ read_clock(&a[0],&a[1],&a[2],&a[3],&a[4],&a[5]); read_clock(&b[0],&b[1],&b[2],&b[3],&b[4],&b[5]); int same=1; for(int i=0;i<6;i++)if(a[i]!=b[i])same=0; if(same)break; }
    if(b[4]<1||b[4]>12||b[5]<1||b[5]>31||b[0]>23||b[1]>59||b[2]>60||b[3]<2000)return 0;
    return s3_days(b[3],b[4],b[5])*86400ULL+(u64)b[0]*3600+(u64)b[1]*60+(u64)b[2]; }
u32 g_s3_slept;
void k_realtime_slept(u64 ms);
void lx_clock_slept(u64 ns);
static void s3_clock_back(u64 e0){
    u64 e1=s3_rtc(); g_s3_slept=0;
    if(!e0||!e1||e1<e0||e1-e0>10ULL*366*86400){ klog("s3: the motherboard clock could not be read after resume - the time may be behind"); return; }
    g_s3_slept=(u32)(e1-e0); k_realtime_slept((u64)g_s3_slept*1000ULL); lx_clock_slept((u64)g_s3_slept*1000000000ULL);
    char m[80],*q=m; q=scopy(q,"s3: clock resynced from the motherboard clock, slept "); q=udec(q,g_s3_slept); q=scopy(q," s"); *q=0; klog(m); }
static int s3_vbe_check(void){ for(int i=1;i<10;i++){ if(i==4)continue; outw(0x1CE,(u16)i); if(inw(0x1CF)!=s3_vbe[i])return -1; } outw(0x1CE,4); return (inw(0x1CF)&1)==(s3_vbe[4]&1)?0:-1; }
const char* s3_why_not(void){
    if(!g_acpi.ok)return "ACPI is not available";
    if(!g_acpi_have_s3)return "the firmware does not offer S3";
    if(g_acpi.hwred)return "the firmware uses hardware-reduced ACPI";
    if(!g_acpi.pm1a_cnt||!g_acpi.pm1a_evt)return "the firmware has no sleep control register";
    if(!s3_facs())return "the firmware has no wake vector table (FACS)";
    if((u32)(s3_wake_end-s3_wake_start)>0xF00)return "the wake code does not fit";
    if(g_dktype!=DK_NONE&&g_dktype!=DK_IDE&&g_dktype!=DK_NVME&&g_dktype!=DK_AHCI)return "the disk controller driver cannot be restarted after S3 yet";
    if(xhci_init_ok&&msd_ok)return "a USB storage device is in use and cannot be restarted after S3 yet";
    if(nic_init&&net_ok)return "the wired network card cannot be restarted after S3 yet";
    if(g_wm_up&&wifi_hw_generic())return "this Wi-Fi card cannot be restarted after S3 yet";
    if(bt_hci_up())return "Bluetooth cannot be restarted after S3 yet";
    if(g_snd_usb&&g_snd_state==1)return "USB audio cannot be restarted after S3 yet";
    const char* g=s3_gpu_why(); if(g)return g;
    return acpi_wake_kbd_ok(3)?0:"no keyboard can wake the computer from S3"; }
int s3_supported(void){ fg_boot(&s3_fg); return s3_fg.on&&!s3_fg.dead&&!s3_why_not(); }
const char* s3_status(void){ fg_boot(&s3_fg); if(s3_fg.dead)return "S3 was turned off because the computer did not wake up from it"; if(!s3_fg.on)return "S3 is turned off - type s3 on to try it"; return s3_why_not(); }

static void s3_pci_save(void){
    s3_npci=0;
    for(int b=0;b<256;b++) for(int s=0;s<32;s++){
        if((pci_cfg(b,s,0,0)&0xFFFF)==0xFFFF)continue;
        int nf=(pci_cfg(b,s,0,0x0C)&0x800000)?8:1;
        for(int f=0;f<nf&&s3_npci<S3_NPCI;f++){ if((pci_cfg(b,s,f,0)&0xFFFF)==0xFFFF)continue;
            for(int i=0;i<16;i++)s3_pci[s3_npci].c[i]=pci_cfg(b,s,f,i*4);
            s3_pci[s3_npci].b=(u8)b; s3_pci[s3_npci].s=(u8)s; s3_pci[s3_npci].f=(u8)f; s3_pci[s3_npci].hdr=(u8)((s3_pci[s3_npci].c[3]>>16)&0x7F); s3_npci++; } } }
static int s3_pci_load(void){
    int lost=0;
    for(int k=0;k<s3_npci;k++){ int b=s3_pci[k].b, s=s3_pci[k].s, f=s3_pci[k].f; const u32* c=s3_pci[k].c;
        if(pci_cfg(b,s,f,0)!=c[0]){ lost++; continue; }
        int pm=pci_find_cap(b,s,f,1); if(pm){ u32 v=pci_cfg(b,s,f,pm+4); if(v&3){ pci_cfgw(b,s,f,pm+4,v&~3u); hr_delay_us(10000); } }
        for(int i=15;i>=4;i--)pci_cfgw(b,s,f,i*4,c[i]); pci_cfgw(b,s,f,0x0C,c[3]&0xFFFFu);
        pci_cfgw(b,s,f,0x04,c[1]&0xFFFF); }
    return lost; }

static void s3_aml(const char* nm,u64 arg){
    if(!g_aml_ok||!g_aroot)return; anode* n=aml_child(g_aroot,nm); if(!n)return;
    aobj a, o; cpu_fill(&a,0,sizeof a); cpu_fill(&o,0,sizeof o); aobj_int(&a,arg);
    u64 dl=g_aml_hard_deadline; g_aml_hard_deadline=0; aml_eval(n,&a,1,&o); g_aml_hard_deadline=dl;
    aobj_free(&o); aobj_free(&a); }

static u8 s3_pic[2]; static int s3_kbc=-1;
int g_s3_woke;
static int kbc_w(void){ for(int t=0;t<100000;t++){ if(!(inb(0x64)&2))return 1; io_wait(); } return 0; }
static int kbc_r(void){ for(int t=0;t<100000;t++){ if(inb(0x64)&1)return 1; io_wait(); } return 0; }
static void kbc_drain(void){ for(int i=0;i<64&&(inb(0x64)&1);i++)inb(0x60); }
static int kbc_cmd(u8 c){ if(!kbc_w())return -1; outb(0x64,c); return 0; }
static void s3_kbc_save(void){ s3_kbc=-1; if(inb(0x64)==0xFF)return; kbc_drain(); if(kbc_cmd(0x20)<0||!kbc_r())return; s3_kbc=inb(0x60); }
static int s3_kbc_load(void){
    if(s3_kbc<0)return 0; kbc_drain();
    if(kbc_cmd(0xAD)<0)return -1; if(kbc_cmd(0xA7)<0)return -1; kbc_drain();
    if(kbc_cmd(0x60)<0||!kbc_w())return -1; outb(0x60,(u8)s3_kbc);
    if(!(s3_kbc&0x20)&&kbc_cmd(0xA8)<0)return -1; if(!(s3_kbc&0x10)&&kbc_cmd(0xAE)<0)return -1;
    if(kbc_w()){ outb(0x60,0xF4); if(kbc_r())inb(0x60); }
    if(!(s3_kbc&0x20)){ extern void mouse_init(void); mouse_init(); }
    kbc_drain(); if(kbc_cmd(0x20)<0||!kbc_r())return -1; return inb(0x60)==(u8)s3_kbc?0:-1; }
static void s3_cpu_back(u64 tsc){
    arch_fpu_init(); cpu_fpu_restore(s3_fpu);
    if(cpu_tsc()<tsc)cpu_wrmsr_safe(0x10,tsc);
    if(apic_s3_restore())klog("s3: interrupt controller did not take its saved state"); pic_remap(); pit_init(100); outb(0x21,s3_pic[0]); outb(0xA1,s3_pic[1]);
    if(g_hr.hpet_ok&&g_hr.hpet_va){ volatile u64* c=(volatile u64*)(g_hr.hpet_va+0x10); *c=(*c&~2ULL)|1; }
    hr_wake_cancel(); }

static void s3_aps_back(void){
    for(int i=1;i<NCPU;i++){ g_cpu[i].online=0; g_cpu[i].parked=0; }
    g_cores_online=1; g_ncpu=1;
    if(smp_enable()>1)klog("s3: other cores started again"); else klog("s3: other cores did not start after resume - running on one core"); }

static void s3_say(const char* m){ char* q=g_s3_why; for(int i=0;m[i]&&i<95;i++)*q++=m[i]; *q=0; klog(m); }

int power_s3(void){
    const char* why=s3_why_not(); if(why){ s3_say(why); return -1; }
    if(!fg_begin(&s3_fg)){ s3_say("s3: turned off - using light sleep"); return -1; }
    int smp=g_smp_on; if(smp&&smp_disable()<0){ fg_ok(&s3_fg); s3_say("s3: the other cores could not be parked - using light sleep"); return -1; }
    u8* facs=(u8*)s3_facs();
    u32 tl=(u32)(s3_wake_end-s3_wake_start); u8* d=(u8*)S3_TB; for(u32 i=0;i<tl;i++)d[i]=s3_wake_start[i];
    *(volatile u32*)(facs+12)=(u32)S3_TB; if(acpi_r32(facs+4)>=32&&facs[32]>=1)*(volatile u64*)(facs+24)=0;
    s3_pci_save(); s3_vbe_save(); g_s3_woke=0;
    acpi_wake_arm(3); s3_aml("_PTS",3);
    klog("s3: entering suspend to RAM");
    u64 fl=cpu_irq_save(); s3_pic[0]=inb(0x21); s3_pic[1]=inb(0xA1); cpu_fpu_save(s3_fpu); s3_kbc_save(); apic_s3_save(); u64 tsc=cpu_tsc(); u64 rtc0=s3_rtc();
    u16 st=(u16)g_acpi.pm1a_evt; int woke=0;
    if(!s3_save()){
        acpi_enable_mode(); outw(st,0x8000); if(g_acpi.pm1b_evt)outw((u16)g_acpi.pm1b_evt,0x8000); acpi_wake_clear();
        cpu_cache_flush();
        u16 c=(u16)((inw((u16)g_acpi.pm1a_cnt)&0xC3FF)|((g_acpi_s3a&7)<<10));
        u16 cb=g_acpi.pm1b_cnt?(u16)((inw((u16)g_acpi.pm1b_cnt)&0xC3FF)|((g_acpi_s3b&7)<<10)):0;
        outw((u16)g_acpi.pm1a_cnt,c); if(g_acpi.pm1b_cnt)outw((u16)g_acpi.pm1b_cnt,cb);
        outw((u16)g_acpi.pm1a_cnt,(u16)(c|(1<<13))); if(g_acpi.pm1b_cnt)outw((u16)g_acpi.pm1b_cnt,(u16)(cb|(1<<13)));
        for(int i=0;i<50000000&&!(inw(st)&0x8000);i++)cpu_pause();
        cpu_fpu_restore(s3_fpu);
    } else { woke=1; s3_cpu_back(tsc); }
    outw(st,woke?0x8100:0x8000); if(woke&&g_acpi.pm1b_evt)outw((u16)g_acpi.pm1b_evt,0x8100);
    if(woke){ acpi_enable_mode(); if(s3_kbc_load()<0)klog("s3: keyboard controller did not take its saved settings"); g_s3_woke=1; }
    s3_aml("_WAK",3); acpi_wake_disarm();
    if(woke&&xhci_pm_woke())klog("s3: woken by a USB device");
    int lost=woke?s3_pci_load():0;
    if(woke){ s3_vbe_load(); klog(s3_vbe_check()?"s3: the screen did not take its saved mode":"s3: screen mode restored"); s3_clock_back(rtc0); }
    *(volatile u32*)(facs+12)=0;
    cpu_irq_restore(fl);
    if(smp)s3_aps_back();
    if(!woke){ g_s3_fail++; fg_fail(&s3_fg,"the firmware did not enter S3"); s3_say("s3: the firmware did not enter S3 - using light sleep"); return -1; }
    g_s3_count++; fg_ok(&s3_fg);
    { char b[96],*q=b; q=scopy(q,"s3: resumed from suspend to RAM"); if(lost){ q=scopy(q,", "); q=udec(q,(u32)lost); q=scopy(q," PCI functions changed"); } *q=0; klog(b); }
    g_s3_why[0]=0; return 3; }

static volatile u8* g_pmc_base; static u32 g_pmc_msg, g_pmc_arg, g_pmc_rsp; static u8 g_pmc_hint; static int g_pmc_probed;
int g_pmc_last=-1;
static u32 pmc_smn(u32 a){ pci_cfgw(0,0,0,0x60,a); return pci_cfg(0,0,0,0x64); }
static int pmc_probe(void){
    if(g_pmc_probed)return g_pmc_base!=0; g_pmc_probed=1;
    u32 id=pci_cfg(0,0,0,0); if((id&0xFFFF)!=0x1022)return 0; u16 dv=(u16)(id>>16);
    g_pmc_msg=0x538; g_pmc_arg=0x9BC; g_pmc_rsp=0x980; g_pmc_hint=3;
    if(dv==0x15D0)g_pmc_hint=2;
    else if(dv==0x1630||dv==0x1645||dv==0x14B5||dv==0x14D8||dv==0x14E8){}
    else if(dv==0x1507||dv==0x1122||dv==0x1B20)g_pmc_msg=0x938;
    else if(dv==0x115B){ g_pmc_msg=0xA10; g_pmc_arg=0xA18; g_pmc_rsp=0xA14; }
    else return 0;
    u32 lo=pmc_smn(0x13B102E8)&0xFFF00000u, hi=pmc_smn(0x13B102EC)&0xFFFFu; u64 pa=(((u64)hi<<32)|lo)+0x10000ULL;
    if(!lo||lo==0xFFF00000u)return 0; u64 va=acpi_hi_map(pa); if(!va)return 0;
    g_pmc_base=(volatile u8*)va; klog("power: AMD power management controller found (deep sleep hint available)"); return 1; }
static u32 pmc_rd(u32 o){ return *(volatile u32*)(g_pmc_base+o); }
static void pmc_wr(u32 o,u32 v){ *(volatile u32*)(g_pmc_base+o)=v; }
static int pmc_wait(u32* v){ for(int i=0;i<20000;i++){ *v=pmc_rd(g_pmc_rsp); if(*v)return 0; hr_delay_us(50); } return -1; }
int amd_pmc_hint(int enter){
    if(!pmc_probe())return -1; u32 v;
    if(pmc_wait(&v)<0){ klog("power: AMD PMC did not answer"); return g_pmc_last=-2; }
    pmc_wr(g_pmc_rsp,0); pmc_wr(g_pmc_arg,enter?1u:0u); pmc_wr(g_pmc_msg,g_pmc_hint);
    if(pmc_wait(&v)<0){ klog("power: AMD PMC deep sleep hint timed out"); return g_pmc_last=-2; }
    if(v!=1){ char b[64],*q=b; q=scopy(q,"power: AMD PMC refused the sleep hint 0x"); q=hx(q,v,2); *q=0; klog(b); return g_pmc_last=-3; }
    return g_pmc_last=0; }

static int pci_pm_set(int b,int s,int f,int st){
    int pm=pci_find_cap(b,s,f,1); if(!pm)return -1; u32 v=pci_cfg(b,s,f,pm+4); u32 nv=(v&~3u)|(u32)(st&3);
    if((v&3)==(u32)st)return 0; pci_cfgw(b,s,f,pm+4,nv&~0x8000u); hr_delay_us(10000); return ((pci_cfg(b,s,f,pm+4)&3)==(u32)st)?0:-1; }
static u32 g_d3_sv[16]; static int g_d3_b=-1, g_d3_s, g_d3_f;
int pci_d3hot_class(int cls,int sub,int on){
    if(on){ int b,s,f; if(!pci_find(cls,sub,-1,&b,&s,&f))return -1; for(int i=0;i<16;i++)g_d3_sv[i]=pci_cfg(b,s,f,i*4);
        if(pci_pm_set(b,s,f,3)<0){ pci_pm_set(b,s,f,0); return -1; } g_d3_b=b; g_d3_s=s; g_d3_f=f; return 0; }
    if(g_d3_b<0)return 0; int b=g_d3_b, s=g_d3_s, f=g_d3_f; g_d3_b=-1;
    int r=pci_pm_set(b,s,f,0); for(int i=15;i>=4;i--)pci_cfgw(b,s,f,i*4,g_d3_sv[i]); pci_cfgw(b,s,f,0x0C,g_d3_sv[3]&0xFFFFu); pci_cfgw(b,s,f,4,g_d3_sv[1]&0xFFFF); return r; }
