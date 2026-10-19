#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/drivers/storage/nvme.h"
#include "include/nvx/core/power.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/fs/fat/vfat.h"

 
struct nvx_g_acpi_t g_acpi;
static u16 acpi_r16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
u32 acpi_r32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
u64 acpi_r64(const u8* p){ return (u64)acpi_r32(p)|((u64)acpi_r32(p+4)<<32); }
static u8 acpi_sum(const u8* p,u32 n){ u8 s=0; for(u32 i=0;i<n;i++)s=(u8)(s+p[i]); return s; }
static int acpi_rsdp_ok(u64 a){ if(!a||a>=0x100000000ULL)return 0; const u8* p=(const u8*)a; if(p[0]!='R'||p[1]!='S'||p[2]!='D'||p[3]!=' '||p[4]!='P'||p[5]!='T'||p[6]!='R'||p[7]!=' ')return 0; return acpi_sum(p,20)==0; }
u64 acpi_find_rsdp(void){
    if(*(volatile u32*)(0x6000+128)==0x50445352u){ u64 a=*(volatile u64*)(0x6000+120); if(acpi_rsdp_ok(a))return a; }
    u16 ebseg; ebseg=cpu_bios_ebda_seg(); u64 ebda=(u64)ebseg<<4; if(ebda>=0x80000&&ebda<0xA0000) for(u64 a=ebda;a<ebda+1024;a+=16) if(acpi_rsdp_ok(a))return a;
    for(u64 a=0xE0000;a<0x100000;a+=16) if(acpi_rsdp_ok(a))return a;
    return 0;
}
const u8* acpi_tbl_ok(u64 a){ if(!a||a>=0x100000000ULL)return 0; const u8* t=(const u8*)a; u32 len=acpi_r32(t+4); if(len<36||len>0x400000)return 0; if(acpi_sum(t,len))return 0; return t; }
static int acpi_pkg_int(const u8** pp,const u8* end,u32* out){ const u8* p=*pp; if(p>=end)return -1; u8 op=*p++;
    if(op==0x00)*out=0; else if(op==0x01)*out=1; else if(op==0xFF)*out=0xFF;
    else if(op==0x0A){ if(p>=end)return -1; *out=*p++; } else if(op==0x0B){ if(p+2>end)return -1; *out=(u32)p[0]|((u32)p[1]<<8); p+=2; }
    else if(op==0x0C){ if(p+4>end)return -1; *out=(u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); p+=4; } else return -1;
    *pp=p; return 0; }
static int acpi_find_sx(const u8* t,char d,u32* a,u32* b){
    u32 len=acpi_r32(t+4); const u8* aml=t+36; const u8* end=t+len;
    for(const u8* q=aml;q+5<end;q++){
        if(q[0]!='_'||q[1]!='S'||q[2]!=(u8)d||q[3]!='_')continue;
        if(!((q-aml>=1&&q[-1]==0x08)||(q-aml>=2&&q[-2]==0x08&&q[-1]=='\\')))continue;
        const u8* p=q+4; if(*p!=0x12)continue; p++; int nb=(*p>>6)&3; p+=1+nb; if(p>=end)continue; p++;
        if(acpi_pkg_int(&p,end,a))continue; if(acpi_pkg_int(&p,end,b))*b=0; return 0; }
    return -1;
}
void power_init(void){
    g_acpi.rsdp=acpi_find_rsdp(); if(!g_acpi.rsdp){ klog("acpi: no RSDP found - power off will use fallbacks"); return; }
    const u8* r=(const u8*)g_acpi.rsdp; g_acpi.rev=r[15];
    u64 sdt=0; int w=4; if(r[15]>=2&&acpi_r32(r+20)>=36&&acpi_r64(r+24)){ sdt=acpi_r64(r+24); w=8; } else sdt=acpi_r32(r+16);
    const u8* x=acpi_tbl_ok(sdt); if(!x){ klog("acpi: root table unreadable"); return; }
    u32 n=(acpi_r32(x+4)-36)/(u32)w; const u8* fadt=0; const u8* ssdt[8]; int ns=0;
    for(u32 i=0;i<n;i++){ u64 e=w==8?acpi_r64(x+36+i*8):acpi_r32(x+36+i*4); const u8* t=acpi_tbl_ok(e); if(!t)continue;
        if(t[0]=='F'&&t[1]=='A'&&t[2]=='C'&&t[3]=='P')fadt=t; else if(t[0]=='S'&&t[1]=='S'&&t[2]=='D'&&t[3]=='T'&&ns<8)ssdt[ns++]=t; }
    if(!fadt){ klog("acpi: no FADT"); return; }
    u32 fl=acpi_r32(fadt+4);
    g_acpi.smi_cmd=acpi_r32(fadt+48); g_acpi.acpi_en=fadt[52];
    g_acpi.pm1a_evt=acpi_r32(fadt+56); g_acpi.pm1b_evt=acpi_r32(fadt+60); g_acpi.pm1a_cnt=acpi_r32(fadt+64); g_acpi.pm1b_cnt=acpi_r32(fadt+68); g_acpi.pm1_evt_len=fadt[88];
    g_acpi.flags=fl>=116?acpi_r32(fadt+112):0;
    if(fl>=172&&!g_acpi.pm1a_cnt&&fadt[172]==1)g_acpi.pm1a_cnt=(u32)acpi_r64(fadt+176);
    if(fl>=184&&!g_acpi.pm1b_cnt&&fadt[184]==1)g_acpi.pm1b_cnt=(u32)acpi_r64(fadt+188);
    if(fl>=148&&!g_acpi.pm1a_evt&&fadt[148]==1)g_acpi.pm1a_evt=(u32)acpi_r64(fadt+152);
    if(fl>=129&&(g_acpi.flags&(1u<<10))){ g_acpi.rst_space=fadt[116]; g_acpi.rst_addr=acpi_r64(fadt+120); g_acpi.rst_val=fadt[128]; }
    if(fl>=256&&(g_acpi.flags&(1u<<20))){ g_acpi.hwred=1; g_acpi.slp_space=fadt[244]; g_acpi.slp_addr=acpi_r64(fadt+248); }
    u64 dsa=acpi_r32(fadt+40); if(fl>=148&&acpi_r64(fadt+140))dsa=acpi_r64(fadt+140);
    const u8* dsdt=acpi_tbl_ok(dsa);
    if(dsdt&&!acpi_find_sx(dsdt,'5',&g_acpi.s5a,&g_acpi.s5b))g_acpi.have_s5=1;
    if(dsdt&&!acpi_find_sx(dsdt,'1',&g_acpi.s1a,&g_acpi.s1b))g_acpi.have_s1=1;
    for(int i=0;i<ns;i++){ if(!g_acpi.have_s5&&!acpi_find_sx(ssdt[i],'5',&g_acpi.s5a,&g_acpi.s5b))g_acpi.have_s5=1; if(!g_acpi.have_s1&&!acpi_find_sx(ssdt[i],'1',&g_acpi.s1a,&g_acpi.s1b))g_acpi.have_s1=1; }
    g_acpi.ok=1;
    klog(g_acpi.have_s5?"acpi: ready - S5 power off available":"acpi: tables found but no _S5 object");
    if(g_acpi.have_s1)klog("acpi: S1 sleep available");
}
static void power_prepare(void){ klog("power: flushing filesystems"); if(nvxntfs_mounted())nvxntfs_umount(); if(vf_mounted())vf_umount(); }
void acpi_enable_mode(void){
    if(!g_acpi.pm1a_cnt||!g_acpi.smi_cmd||!g_acpi.acpi_en)return;
    if(inw((u16)g_acpi.pm1a_cnt)&1)return;
    outb((u16)g_acpi.smi_cmd,g_acpi.acpi_en);
    u64 t0=hr_precise()?hr_ns():0; for(int i=0;i<3000000&&!(inw((u16)g_acpi.pm1a_cnt)&1);i++){ io_wait(); if(t0&&hr_ns()-t0>1000000000ULL){ klog("acpi: firmware did not switch to ACPI mode"); break; } }
}
static void acpi_gas_write8(u8 space,u64 addr,u8 v){ if(space==1)outb((u16)addr,v); else if(space==0&&addr&&addr<0x100000000ULL)*(volatile u8*)addr=v; }
static void power_safe_screen(const char* line){
    fill_rect(0,0,(int)W,(int)H,rgb(0,0,0));
    int bw=(int)slen(line)*8+80, bh=60, bx=((int)W-bw)/2, by=((int)H-bh)/2;
    fill_rect(bx,by,bw,bh,rgb(248,249,251)); fill_rect(bx+3,by+3,bw-6,2,rgb(20,24,34)); fill_rect(bx+3,by+bh-5,bw-6,2,rgb(20,24,34)); fill_rect(bx+3,by+3,2,bh-6,rgb(20,24,34)); fill_rect(bx+bw-5,by+3,2,bh-6,rgb(20,24,34));
    dstr(bx+40,by+23,line,rgb(20,24,34),1); blit_all();
}
 
void power_off(void){ if(g_gdisk_probed)gdisk_flush();
    power_prepare(); klog("power: shutting down");
    if(g_acpi.ok&&g_acpi.have_s5){
        if(g_acpi.hwred&&g_acpi.slp_addr){ acpi_gas_write8(g_acpi.slp_space,g_acpi.slp_addr,(u8)(((g_acpi.s5a&7)<<2)|0x20)); }
        else if(g_acpi.pm1a_cnt){ acpi_enable_mode(); cpu_irq_off();
            u16 c=(u16)(inw((u16)g_acpi.pm1a_cnt)&0xE3FF); outw((u16)g_acpi.pm1a_cnt,(u16)(c|((g_acpi.s5a&7)<<10)|(1<<13)));
            if(g_acpi.pm1b_cnt){ u16 d=(u16)(inw((u16)g_acpi.pm1b_cnt)&0xE3FF); outw((u16)g_acpi.pm1b_cnt,(u16)(d|((g_acpi.s5b&7)<<10)|(1<<13))); } }
        for(int i=0;i<2000000;i++)io_wait();
    }
    outw(0x604,0x2000); outw(0xB004,0x2000); outw(0x4004,0x3400);
    klog("power: power off did not take effect");
    power_safe_screen("It is now safe to turn off your computer.");
    for(;;)cpu_stop();
}
void power_reboot(void){ if(g_gdisk_probed)gdisk_flush();
    power_prepare(); klog("power: restarting"); cpu_irq_off();
    if(g_acpi.ok&&g_acpi.rst_addr&&(g_acpi.rst_space==0||g_acpi.rst_space==1)){ acpi_gas_write8(g_acpi.rst_space,g_acpi.rst_addr,g_acpi.rst_val); for(int i=0;i<500000;i++)io_wait(); }
    outb(0xCF9,0x02); for(int i=0;i<1000;i++)io_wait(); outb(0xCF9,0x06); for(int i=0;i<500000;i++)io_wait();
    for(int i=0;i<100000&&(inb(0x64)&2);i++)io_wait(); outb(0x64,0xFE); for(int i=0;i<500000;i++)io_wait();
    cpu_triple_fault();
    for(;;)cpu_halt();
}

int power_sleep(void){
    if(g_acpi.ok&&g_acpi.have_s1&&g_acpi.pm1a_cnt&&g_acpi.pm1a_evt&&!g_acpi.hwred){
        klog("power: entering S1 sleep"); acpi_enable_mode();
        u16 st=(u16)g_acpi.pm1a_evt, en=(u16)(g_acpi.pm1a_evt+(g_acpi.pm1_evt_len?g_acpi.pm1_evt_len/2:2));
        outw(st,0x8000); outw(en,(u16)(inw(en)|0x0100));
        u16 c=(u16)(inw((u16)g_acpi.pm1a_cnt)&0xE3FF); outw((u16)g_acpi.pm1a_cnt,(u16)(c|((g_acpi.s1a&7)<<10)|(1<<13)));
        for(int i=0;i<200000000&&!(inw(st)&0x8000);i++)cpu_pause();
        outw(st,0x8000); klog("power: woke from S1"); return 1;
    }
    klog("power: S1 not available - screen off until a key is pressed");
    fill_rect(0,0,(int)W,(int)H,rgb(0,0,0)); blit_all();
    while(kpop()>=0){}
    for(;;){ cpu_idle(); if(kpop()>=0)break; }
    return 0;
}
