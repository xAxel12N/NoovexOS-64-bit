#include "include/nvx/base.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/video/nvidia/nvgpu.h"
#define NV_PMC_BOOT_0        0x000000
#define NV_PMC_BOOT_42       0x000A00
#define NV_PDISP_VGA_CR_ROM  0x619F04
#define NV_PROM_DATA         0x300000
#define NV_PROM_SIZE         0x020000
#define NV_PBUS_PCI_ROM      0x001800

static int   g_nv_found=0;
int   g_nv_bus,g_nv_slot,g_nv_fn;
u16   g_nv_dev;
u64   g_nv_bar0=0, g_nv_bar1=0, g_nv_bar3=0;
u32   g_nv_bar0_sz=0;
u32   g_nv_boot0=0;
int   g_nv_arch=0, g_nv_impl=0, g_nv_rev=0;
u8    g_nv_vbios[65536]; int g_nv_vbios_len=0;
u32   g_nv_bit_off=0, g_nv_dcb_off=0;

int   g_nv_ncon=0; u8 g_nv_con_type[16], g_nv_con_or[16], g_nv_con_heads[16];

u32 nvr32(u32 off){ if(!g_nv_bar0)return 0xFFFFFFFFu; return *(volatile u32*)(u64)(g_nv_bar0+off); }

static u32 nv_bar_size(int bus,int slot,int fn,int bi){
    int off=0x10+bi*4; u32 orig=pci_cfg(bus,slot,fn,off);
    pci_cfgw(bus,slot,fn,off,0xFFFFFFFFu); u32 sz=pci_cfg(bus,slot,fn,off);
    pci_cfgw(bus,slot,fn,off,orig);
    sz&=0xFFFFFFF0u; if(!sz)return 0; return (~sz)+1;
}

int nv_probe(void){
    if(g_nv_found)return 1;
    for(int bus=0;bus<256;bus++) for(int slot=0;slot<32;slot++){
        u32 id0=pci_cfg(bus,slot,0,0); if((id0&0xFFFF)==0xFFFF)continue;
        int nf=(pci_cfg(bus,slot,0,0x0C)&0x800000)?8:1;
        for(int fn=0;fn<nf;fn++){ u32 id=pci_cfg(bus,slot,fn,0); if((id&0xFFFF)!=0x10DE)continue;
            u32 cl=pci_cfg(bus,slot,fn,0x08); int c=(cl>>24)&0xFF;
            if(c!=0x03)continue;                         /* display controller */
            g_nv_bus=bus; g_nv_slot=slot; g_nv_fn=fn; g_nv_dev=(u16)(id>>16);
            u32 b0=pci_cfg(bus,slot,fn,0x10);
            g_nv_bar0=(u64)(b0&0xFFFFFFF0u);
            g_nv_bar0_sz=nv_bar_size(bus,slot,fn,0);
            u32 b1=pci_cfg(bus,slot,fn,0x14);
            if((b1&0x6)==0x4){ u32 b1hi=pci_cfg(bus,slot,fn,0x18); g_nv_bar1=((u64)b1hi<<32)|(b1&0xFFFFFFF0u); g_nv_bar3=(u64)(pci_cfg(bus,slot,fn,0x1C)&0xFFFFFFF0u); }
            else { g_nv_bar1=(u64)(b1&0xFFFFFFF0u); g_nv_bar3=(u64)(pci_cfg(bus,slot,fn,0x18)&0xFFFFFFF0u); }
            u32 cmd=pci_cfg(bus,slot,fn,0x04); pci_cfgw(bus,slot,fn,0x04,cmd|0x06);   /* mem + bus master */
            g_nv_found=1; return 1;
        }
    }
    return 0;
}

static const char* nv_arch_name(int a){
    switch(a){
    case 0x04: return "Fermi";  case 0x05: return "Fermi";
    case 0x0E: return "Kepler"; case 0x0F: return "Kepler"; case 0x11: return "Kepler";
    case 0x12: return "Maxwell";case 0x13: return "Maxwell";
    case 0x14: return "Pascal"; case 0x16: return "Pascal";
    case 0x17: return "Volta";
    case 0x15: return "Turing"; case 0x16+0x02: return "Turing";
    case 0x17+0x02: return "Ampere";
    default:   return "NVIDIA";
    }
}
const char* nv_arch_name2(u32 boot0){
    u32 a=(boot0>>20)&0x1FF;
    if(a>=0x160&&a<0x170)return "Turing (TU11x)";
    if(a>=0x162&&a<0x166)return "Turing";
    switch(a>>4){
    case 0x0C: return "Fermi"; case 0x0E: return "Kepler"; case 0x0F: return "Kepler";
    case 0x11: return "Kepler"; case 0x12: return "Maxwell"; case 0x13: return "Maxwell";
    case 0x14: return "Pascal"; case 0x16: return "Turing"; case 0x17: return "Ampere"; case 0x19: return "Ada";
    }
    return "NVIDIA GPU";
}

void nv_read_id(void){
    if(!g_nv_bar0)return;
    g_nv_boot0=nvr32(NV_PMC_BOOT_0);
    g_nv_arch=(g_nv_boot0>>20)&0xFF;
    g_nv_impl=(g_nv_boot0>>16)&0x0F;
    g_nv_rev=g_nv_boot0&0xFF;
}

/* ---------- VBIOS ---------- */
static int nv_vbios_from_prom(void){
    if(!g_nv_bar0)return 0;
    u32 shadow=nvr32(0x088050); *(volatile u32*)(u64)(g_nv_bar0+0x088050)=shadow&~1u;
    int ok=0; u32 w=nvr32(NV_PROM_DATA);
    if((w&0xFFFF)==0xAA55){ int len=(int)sizeof(g_nv_vbios); if(len>NV_PROM_SIZE)len=NV_PROM_SIZE;
        for(int i=0;i+3<len;i+=4){ u32 x=nvr32(NV_PROM_DATA+(u32)i); g_nv_vbios[i]=(u8)x; g_nv_vbios[i+1]=(u8)(x>>8); g_nv_vbios[i+2]=(u8)(x>>16); g_nv_vbios[i+3]=(u8)(x>>24); }
        g_nv_vbios_len=len; ok=1; }
    *(volatile u32*)(u64)(g_nv_bar0+0x088050)=shadow;
    return ok;
}
int nv_load_vbios(void){
    if(g_nv_vbios_len)return 1;
    if(nv_vbios_from_prom())return 1;
    return 0;
}
static u16 nv_le16(int off){ return (u16)(g_nv_vbios[off]|(g_nv_vbios[off+1]<<8)); }

/* BIT structure: signature 0xB8 0xFF 'B' 'I' 'T' 0x00 */
int nv_find_bit(void){
    for(int i=0;i+6<g_nv_vbios_len;i++){
        if(g_nv_vbios[i]==0xB8&&g_nv_vbios[i+1]==0xFF&&g_nv_vbios[i+2]=='B'&&g_nv_vbios[i+3]=='I'&&g_nv_vbios[i+4]=='T'&&g_nv_vbios[i+5]==0){ g_nv_bit_off=i; return 1; }
    }
    /* older cards: 'BMP' signature 0xFF 0x7F 'NV' */
    return 0;
}
/* Walk BIT tokens to find the 'D' (DCB) pointer table, or scan for DCB 0x4.x signature. */
int nv_find_dcb(void){
    /* DCB header: version at [0], header size, entry count, entry size, then signature 0x4EDCBDCB at +6 */
    for(int i=6;i+4<=g_nv_vbios_len;i++){
        u32 sig=g_nv_vbios[i]|(g_nv_vbios[i+1]<<8)|(g_nv_vbios[i+2]<<16)|((u32)g_nv_vbios[i+3]<<24);
        if(sig==0x4EDCBDCBu){ g_nv_dcb_off=i-6; return 1; }   /* signature sits at header+6 in v4.x */
    }
    return 0;
}
void nv_parse_dcb(void){
    g_nv_ncon=0; if(!g_nv_dcb_off)return;
    u8 ver=g_nv_vbios[g_nv_dcb_off];
    if(ver<0x30)return;
    u8 hlen=g_nv_vbios[g_nv_dcb_off+1];
    u8 ecnt=g_nv_vbios[g_nv_dcb_off+2];
    u8 esz =g_nv_vbios[g_nv_dcb_off+3];
    for(int e=0;e<ecnt&&g_nv_ncon<16;e++){
        int o=g_nv_dcb_off+hlen+e*esz;
        if(o+4>g_nv_vbios_len)break;
        u32 ent=g_nv_vbios[o]|(g_nv_vbios[o+1]<<8)|(g_nv_vbios[o+2]<<16)|((u32)g_nv_vbios[o+3]<<24);
        int type=ent&0xF;
        if(type==0xF)continue;                 /* skip */
        if(ent==0x00000000u||ent==0xFFFFFFFFu)break;
        int heads=(ent>>0)&0;                   /* placeholder */
        g_nv_con_type[g_nv_ncon]=(u8)type;
        g_nv_con_or[g_nv_ncon]=(u8)((ent>>24)&0xF);
        g_nv_con_heads[g_nv_ncon]=(u8)((ent>>8)&0xF);
        (void)heads; g_nv_ncon++;
    }
}
const char* nv_con_name(u8 t){
    switch(t){ case 0: return "VGA/CRT"; case 1: return "TV"; case 2: return "TMDS/DVI"; case 3: return "LVDS (panel)";
    case 4: return "Reserved"; case 5: return "SDI"; case 6: return "DisplayPort"; case 8: return "eDP (panel)"; default: return "unknown"; }
}

static int nv_init(void){
    if(!nv_probe())return 0;
    nv_read_id();
    if(nv_load_vbios()){ (void)nv_find_bit(); if(nv_find_dcb())nv_parse_dcb(); }
    return 1;
}
