#include "include/nvx/base.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "drivers/video/amd/amd.h"
#include "drivers/video/amd/atom/atom.h"

static u8 g_amd_vbios_buf[AMD_VBIOS_MAX]; static int vb_isvram;

static int vb_take(const volatile u8* src,u32 max,int srcid,int vram){
    if(max>AMD_VBIOS_MAX)max=AMD_VBIOS_MAX;
    if(max<0x200||src[0]!=0x55||src[1]!=0xAA)return 0;
    u32 sz=(u32)src[2]*512u; if(vram)sz=max; else if(sz<0x200||sz>max)sz=max;
    for(u32 i=0;i<sz;i+=4){ u32 x=*(const volatile u32*)(src+i); g_amd_vbios_buf[i]=(u8)x; g_amd_vbios_buf[i+1]=(u8)(x>>8); g_amd_vbios_buf[i+2]=(u8)(x>>16); g_amd_vbios_buf[i+3]=(u8)(x>>24); }
    if(!amd_atom_check(g_amd_vbios_buf,sz))return 0;
    g_amd.vbios=g_amd_vbios_buf; g_amd.vbios_len=sz; g_amd.vbios_src=srcid; vb_isvram=vram; return 1;
}

static int vb_vfct(void){
    const u8* t=acpi_find_table("VFCT",0); if(!t)return 0;
    u32 tl=(u32)t[4]|((u32)t[5]<<8)|((u32)t[6]<<16)|((u32)t[7]<<24); if(tl<0x4C||tl>(16u<<20))return 0;
    u32 il=0; u32 o=amd_vfct_find(t,tl,g_amd.bus,g_amd.slot,g_amd.fn,0x1002,g_amd.did,&il); if(!o||il<0x200)return 0;
    if(il>AMD_VBIOS_MAX)il=AMD_VBIOS_MAX;
    for(u32 i=0;i<il;i++)g_amd_vbios_buf[i]=t[o+i];
    if(!amd_atom_check(g_amd_vbios_buf,il))return 0;
    g_amd.vbios=g_amd_vbios_buf; g_amd.vbios_len=il; g_amd.vbios_src=AMD_VBIOS_VFCT; return 1;
}

static u64 vb_bar0(void){
    if(g_amd.fb_pa)return g_amd.fb_pa;
    u32 lo=pci_cfg(g_amd.bus,g_amd.slot,g_amd.fn,0x10); if(lo&1)return 0;
    u64 pa=lo&~0xFULL; if(((lo>>1)&3)==2)pa|=(u64)pci_cfg(g_amd.bus,g_amd.slot,g_amd.fn,0x14)<<32;
    return pa;
}

static int vb_vram(void){
    if(!g_amd.apu)return 0;
    u32 cmd=pci_cfg(g_amd.bus,g_amd.slot,g_amd.fn,0x04); if(!(cmd&2))return 0;
    u64 pa=vb_bar0(); if(!pa)return 0;
    if(g_amd.fb_len&&g_amd.fb_len<AMD_VBIOS_MAX)return 0;
    u64 v=acpi_hi_map(pa); if(!v)return 0;
    return vb_take((const volatile u8*)v,AMD_VBIOS_MAX,AMD_VBIOS_LEGACY,1);
}

static int vb_rombar(void){
    int b=g_amd.bus,s=g_amd.slot,f=g_amd.fn;
    u32 cmd=pci_cfg(b,s,f,0x04); if(!(cmd&2))return 0;
    u32 rb=pci_cfg(b,s,f,0x30); u32 pa=rb&0xFFFFF800u; if(!pa||pa==0xFFFFF800u)return 0;
    if(!(rb&1))pci_cfgw(b,s,f,0x30,rb|1);
    int ok=vb_take((const volatile u8*)(u64)pa,128u*1024u,AMD_VBIOS_ROMBAR,0);
    if(!(rb&1))pci_cfgw(b,s,f,0x30,rb);
    return ok;
}

int amd_vbios_fetch(void){
    if(g_amd.vbios&&g_amd.vbios_len)return 1;
    if(!g_amd.found)return 0;
    g_amd.vbios_src=AMD_VBIOS_NONE;
    vb_isvram=0;
    if(!vb_vfct()&&!vb_vram()&&!vb_rombar()){ const char* w="no VBIOS (VFCT/VRAM/ROM BAR)"; int i=0; g_amd_atom.ok=0; while(w[i]&&i<47){ g_amd_atom.why[i]=w[i]; i++; } g_amd_atom.why[i]=0; return 0; }
    amd_atom_parse(g_amd.vbios,g_amd.vbios_len,&g_amd_atom); g_amd_atom.vram_copy=vb_isvram;
    return 1;
}

int amd_atom_info(char* buf,int n){
    if(n<64)return 0;
    static const char* const src[]={"none","ACPI VFCT","ROM BAR","VRAM copy"};
    const char* w=g_amd.vbios_src>=0&&g_amd.vbios_src<=3?src[g_amd.vbios_src]:"?"; char* p=buf;
    const char* a="source: "; while(*a)*p++=*a++; while(*w)*p++=*w++;
    if(g_amd.vbios_len){ a=", "; while(*a)*p++=*a++; u32 k=g_amd.vbios_len/1024; char t[12]; int m=0; do{ t[m++]=(char)('0'+k%10); k/=10; }while(k); while(m)*p++=t[--m]; a=" KiB"; while(*a)*p++=*a++; }
    *p++='\n';
    return (int)(p-buf)+amd_atom_describe(&g_amd_atom,p,n-(int)(p-buf));
}
