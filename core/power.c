#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/drivers/storage/nvme.h"
#include "include/nvx/core/power.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/fs/fat/vfat.h"
#include "include/nvx/drivers/acpi/aml.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/graphics/theme/theme.h"

 
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
static void power_prepare(void){ { extern int nvgsp_shutdown(void); nvgsp_shutdown(); } klog("power: flushing filesystems"); if(nvxntfs_mounted())nvxntfs_umount(); if(vf_mounted())vf_umount(); }
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
 
void power_off(void){ { extern void nwk_webcache_tick(int) __attribute__((weak)); if(nwk_webcache_tick)nwk_webcache_tick(1); } if(g_gdisk_probed)gdisk_flush();
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
void power_reboot(void){ { extern void nwk_webcache_tick(int) __attribute__((weak)); if(nwk_webcache_tick)nwk_webcache_tick(1); } if(g_gdisk_probed)gdisk_flush();
    power_prepare(); klog("power: restarting"); cpu_irq_off();
    if(g_acpi.ok&&g_acpi.rst_addr&&(g_acpi.rst_space==0||g_acpi.rst_space==1)){ acpi_gas_write8(g_acpi.rst_space,g_acpi.rst_addr,g_acpi.rst_val); for(int i=0;i<500000;i++)io_wait(); }
    outb(0xCF9,0x02); for(int i=0;i<1000;i++)io_wait(); outb(0xCF9,0x06); for(int i=0;i<500000;i++)io_wait();
    for(int i=0;i<100000&&(inb(0x64)&2);i++)io_wait(); outb(0x64,0xFE); for(int i=0;i<500000;i++)io_wait();
    cpu_triple_fault();
    for(;;)cpu_halt();
}

int g_sleeping; volatile int g_sleep_wake, g_sleep_req; u64 g_wake_t; u32 g_sleep_count, g_sleep_secs; int g_lps0_state=-1; u32 g_lps0_mask_i, g_lps0_mask_m;
static anode* g_lps0_dsm;
static const u8 LPS0_I[16]={0xA0,0x40,0xEB,0xC4,0xD2,0x6C,0xE2,0x11,0xBC,0xFD,0x08,0x00,0x20,0x0C,0x9A,0x66};
static const u8 LPS0_M[16]={0x56,0x0D,0xE0,0x11,0x64,0xCE,0xCE,0x47,0x83,0x7B,0x1F,0x89,0x8F,0x9A,0xA4,0x61};
static void lps0_zero(aobj* o){ o->type=AT_NONE; o->p=0; o->e=0; o->own=0; o->len=0; o->i=0; o->cont=0; o->node=0; }
static int g_lps0_rev_i;
static int lps0_call(const u8* uuid,int rev,int fn,u64* out){
    if(!g_lps0_dsm)return -1; aobj a[4]; for(int k=0;k<4;k++)lps0_zero(&a[k]);
    extern void* kmalloc(unsigned long); u8* ub=(u8*)kmalloc(16); if(!ub)return -1; for(int k=0;k<16;k++)ub[k]=uuid[k];
    a[0].type=AT_BUF; a[0].p=ub; a[0].len=16; aobj_int(&a[1],(u64)rev); aobj_int(&a[2],(u64)fn); a[3].type=AT_PKG; a[3].len=0; a[3].e=0;
    aobj o; lps0_zero(&o); u64 dl=g_aml_hard_deadline; g_aml_hard_deadline=0; int r=aml_eval(g_lps0_dsm,a,4,&o); g_aml_hard_deadline=dl;
    if(!r&&o.type==AT_REF){ aobj d; lps0_zero(&d); aml_deref(&o,&d); aobj_free(&o); o=d; }
    if(!r&&out)*out=(o.type==AT_INT||o.type==AT_BUF)?aobj_to_int(&o):0;
    aobj_free(&o); for(int k=0;k<4;k++)aobj_free(&a[k]); return r; }
static void lps0_init(void){
    if(g_lps0_state>=0)return; g_lps0_state=0; if(!g_aml_ok||!g_aroot)return;
    anode* d[2]; int n=aml_find_devices(g_aroot,"PNP0D80",d,2,0,0); if(n<1)return;
    g_lps0_dsm=aml_child(d[0],"_DSM"); if(!g_lps0_dsm)return;
    u64 v=0; if(!lps0_call(LPS0_I,1,0,&v)&&(v&1)){ g_lps0_mask_i=(u32)v; g_lps0_rev_i=1; } else { v=0; if(!lps0_call(LPS0_I,0,0,&v)&&(v&1))g_lps0_mask_i=(u32)v; }
    v=0; if(!lps0_call(LPS0_M,0,0,&v)&&(v&1))g_lps0_mask_m=(u32)v;
    if(g_lps0_mask_i||g_lps0_mask_m){ g_lps0_state=1; char b[96],*q=b; q=scopy(q,"power: low-power idle device found (Intel functions 0x"); q=hx(q,g_lps0_mask_i,8); q=scopy(q,", Microsoft 0x"); q=hx(q,g_lps0_mask_m,8); *q++=')'; *q=0; klog(b); } }
static void lps0(int fi,int fm){
    if(fi&&((g_lps0_mask_i>>fi)&1))lps0_call(LPS0_I,g_lps0_rev_i,fi,0);
    if(fm&&((g_lps0_mask_m>>fm)&1))lps0_call(LPS0_M,0,fm,0); }
void power_wake_req(void){ if(g_sleeping)g_sleep_wake=1; }
extern int bkl_held(void); extern void bkl_lock(void); extern void bkl_unlock(void); extern void bl_screen(int);
int power_s2idle(void){
    if(g_sleeping)return 0;
    klog("power: light sleep - open the lid, press the power button or a key to wake");
    lps0_init();
    { extern volatile int g_fs_wb_pending; extern void nvx3_sync_all(void); if(g_fs_wb_pending)nvx3_sync_all(); }
    if(g_gdisk_probed)gdisk_flush();
    int took=0; if(!bkl_held()){ bkl_lock(); took=1; }
    fill_rect(0,0,(int)W,(int)H,rgb(0,0,0)); blit_all(); bl_screen(0);
    lps0(3,3); lps0(0,7); lps0(5,5);
    extern int g_battsave; int bs=g_battsave; g_battsave=1; { extern void cpupower_saver(int); cpupower_saver(1); }
    g_sleep_wake=0; g_sleeping=1; g_sleep_count++; while(kpop()>=0){}
    extern volatile int g_frz_hold, g_jc_frozen; int ofh=g_frz_hold; g_frz_hold=1; g_jc_frozen=1;
    { extern int g_state, g_logged_in; extern void acct_ui_lock(void); if(g_state==ST_DESKTOP&&g_logged_in)acct_ui_lock(); }
    extern int g_ticks_live; extern int bkl_drop(void); extern void bkl_take(int); extern int kpending(void);
    u64 t0=g_ticks, ap=0; u32 spins=0;
    for(;;){
        if(g_ticks-ap>=5||(!g_ticks_live&&(++spins&0xFFFFF)==0)){ ap=g_ticks; acpi_poll(); }
        if(xhid_n)xhci_mouse_poll();
        { extern volatile int g_hk_w, g_hk_r; if(g_hk_r!=g_hk_w){ g_hk_r=g_hk_w; g_sleep_wake=1; } }
        if(kpop()>=0)g_sleep_wake=1;
        if(g_sleep_wake)break;
        if(g_acb_pct>=0&&g_acb_pct<=3&&!g_acb_charging&&g_ac_online!=1){ g_sleeping=0; g_jc_frozen=0; g_frz_hold=ofh; klog("power: battery empty during sleep - shutting down"); power_off(); }
        { extern void cpupower_tick(void); cpupower_tick(); }
        if(!g_ticks_live){ cpu_pause(); continue; }
        { int bd=bkl_drop(); cpu_irq_off(); if(!g_sleep_wake&&!kpending())cpu_idle(); else cpu_irq_on(); bkl_take(bd); } }
    g_jc_frozen=0; g_frz_hold=ofh;
    g_sleeping=0; g_sleep_secs=(u32)((g_ticks-t0)/100);
    lps0(6,6); lps0(0,8); lps0(4,4);
    g_battsave=bs; { extern void cpupower_saver(int); cpupower_saver(bs); }
    while(kpop()>=0){}
    bl_screen(1);
    { char b[64],*q=b; q=scopy(q,"power: woke from light sleep after "); q=udec(q,g_sleep_secs); q=scopy(q," s"); *q=0; klog(b); }
    { extern void fb_shadow_invalidate(void); extern void gfx_invalidate_all(void); extern void xover_invalidate(void) __attribute__((weak)); fb_shadow_invalidate(); gfx_invalidate_all(); if(xover_invalidate)xover_invalidate(); { extern void live_invalidate(void) __attribute__((weak)); extern void live_present(void) __attribute__((weak)); extern int g_state; if(g_state==ST_LIVE&&live_invalidate){ live_invalidate(); if(live_present)live_present(); } } }
    if(took)bkl_unlock();
    g_sleep_req=0; g_wake_t=g_ticks; { extern void jc_kick_waiters(void); jc_kick_waiters(); }
    return 2; }
int power_sleep(void){
    if(g_acpi.ok&&g_acpi.have_s1&&g_acpi.pm1a_cnt&&g_acpi.pm1a_evt&&!g_acpi.hwred){
        klog("power: entering S1 sleep"); acpi_enable_mode();
        u16 st=(u16)g_acpi.pm1a_evt, en=(u16)(g_acpi.pm1a_evt+(g_acpi.pm1_evt_len?g_acpi.pm1_evt_len/2:2));
        outw(st,0x8000); outw(en,(u16)(inw(en)|0x0100));
        u16 c=(u16)(inw((u16)g_acpi.pm1a_cnt)&0xE3FF); outw((u16)g_acpi.pm1a_cnt,(u16)(c|((g_acpi.s1a&7)<<10)|(1<<13)));
        for(int i=0;i<200000000&&!(inw(st)&0x8000);i++)cpu_pause();
        outw(st,0x8000); klog("power: woke from S1"); return 1;
    }
    return power_s2idle();
}
