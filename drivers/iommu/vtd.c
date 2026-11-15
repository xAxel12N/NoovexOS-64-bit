#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/drivers/iommu/vtd.h"
vtd_unit g_vtd[VTD_MAX];
int g_vtd_n=0, g_vtd_present=0, g_vtd_changed=0, g_vtd_failed=0;
u8 g_vtd_dmar_flags=0;
static u32 vtd_r32(u64 b,u32 o){ return *(volatile u32*)(b+o); }
static u64 vtd_r64(u64 b,u32 o){ return *(volatile u64*)(b+o); }
static void vtd_w32(u64 b,u32 o,u32 v){ *(volatile u32*)(b+o)=v; }
static int vtd_wait(u64 b,u32 o,u32 m,u32 want){ for(int i=0;i<10000;i++){ if((vtd_r32(b,o)&m)==want)return 1; hr_delay_us(10); } return 0; }
static u16 vtd_g16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
static u64 vtd_g64(const u8* p){ u64 v=0; for(int i=7;i>=0;i--)v=(v<<8)|p[i]; return v; }
static int vtd_gfx_only(const u8* sc,int n){
    int any=0;
    for(int o=0;o+6<=n;){ u8 ty=sc[o], ln=sc[o+1]; if(ln<6||o+ln>n)return 0;
        if(ty!=1||sc[o+5]!=0||ln!=8||sc[o+6]!=2||sc[o+7]!=0)return 0; any=1; o+=ln; }
    return any; }
static void vtd_log(int i,const vtd_unit* u){
    char b[200],*p=b; p=scopy(p,"iommu: unit "); p=udec(p,(u32)i); p=scopy(p," at 0x"); p=hx(p,(u32)(u->base>>32),8); p=hx(p,(u32)u->base,8);
    p=scopy(p,", translation "); p=scopy(p,(u->gsts0&(1u<<31))?"was ON":"off"); p=scopy(p,", protected memory "); p=scopy(p,(u->pmen0&(1u<<31))?"was ON":"off");
    p=scopy(p,", interrupt remapping "); p=scopy(p,(u->gsts0&(1u<<25))?"on":"off");
    if(u->done)p=scopy(p,u->done==1?" - turned off now":" - could NOT turn off");
    *p=0; klog(b); }
int vtd_init(void){
    if(g_vtd_present)return g_vtd_changed;
    const u8* t=acpi_find_table("DMAR",0); if(!t){ g_vtd_present=-1; klog("iommu: no DMAR table (no VT-d, nothing to turn off)"); return 0; }
    g_vtd_present=1;
    u32 len=(u32)(t[4]|(t[5]<<8)|(t[6]<<16)|((u32)t[7]<<24)); if(len>65536)len=65536;
    g_vtd_dmar_flags=t[37];
    u32 o=48;
    while(o+4<=len&&g_vtd_n<VTD_MAX){
        u16 ty=vtd_g16(t+o), ln=vtd_g16(t+o+2); if(ln<4||o+ln>len)break;
        if(ty==0&&ln>=16){
            vtd_unit* u=&g_vtd[g_vtd_n++]; u->flags=t[o+4]; u->base=vtd_g64(t+o+8); u->done=0; u->gsts0=u->gsts1=u->pmen0=u->pmen1=0; u->cap=0;
            if(!u->base||u->base>=0x100000000ULL){ u->done=3; g_vtd_failed++; char b[120],*p=b; p=scopy(p,"iommu: unit "); p=udec(p,(u32)(g_vtd_n-1)); p=scopy(p," registers at 0x"); p=hx(p,(u32)(u->base>>32),8); p=hx(p,(u32)u->base,8); p=scopy(p," are above 4 GB - not checked"); *p=0; klog(b); o+=ln; continue; }
            u->cap=vtd_r64(u->base,0x08);
            if(u->cap==~0ULL){ u->done=3; g_vtd_failed++; klog("iommu: a unit does not answer - not checked"); o+=ln; continue; }
            if(!(u->flags&1u)&&vtd_gfx_only(t+o+16,ln-16)){ klog("iommu: graphics-only unit left as it is (Linux does the same on newer Intel GPUs)"); o+=ln; continue; }
            u->gsts0=vtd_r32(u->base,0x1C); u->pmen0=((u->cap>>5)&3u)?vtd_r32(u->base,0x64):0;
            int bad=0, chg=0;
            if(u->gsts0&(1u<<31)){
                u32 cmd=(u->gsts0&0x96FFFFFFu)&~(1u<<31); vtd_w32(u->base,0x18,cmd);
                if(vtd_wait(u->base,0x1C,1u<<31,0))chg=1; else bad=1; }
            if(u->pmen0&(1u<<31)){
                vtd_w32(u->base,0x64,u->pmen0&~(1u<<31));
                if(vtd_wait(u->base,0x64,1u,0))chg=1; else bad=1; }
            u->gsts1=vtd_r32(u->base,0x1C); u->pmen1=((u->cap>>5)&3u)?vtd_r32(u->base,0x64):0;
            if(bad){ u->done=2; g_vtd_failed++; } else if(chg){ u->done=1; g_vtd_changed++; }
            vtd_log(g_vtd_n-1,u);
        }
        o+=ln;
    }
    if(!g_vtd_n)klog("iommu: DMAR table has no remapping units");
    return g_vtd_changed;
}
