#include "include/nvx/base.h"
#include "include/nvx/drivers/bus/pci.h"
/* =========================================================================
   PCI enumeration (read-only config-space scan; safe, no MMIO/DMA)
   ========================================================================= */

u16 pci_ven[PCI_MAX], pci_dev[PCI_MAX]; u8 pci_cls[PCI_MAX], pci_sub[PCI_MAX], pci_bus[PCI_MAX], pci_slot[PCI_MAX]; int pci_n=0;
u32 pci_cfg(int bus,int slot,int fn,int off){ u32 a=(1u<<31)|((u32)bus<<16)|((u32)slot<<11)|((u32)fn<<8)|(off&0xFC); outl(0xCF8,a); return inl(0xCFC); }
void pci_cfgw(int bus,int slot,int fn,int off,u32 v){ u32 a=(1u<<31)|((u32)bus<<16)|((u32)slot<<11)|((u32)fn<<8)|(off&0xFC); outl(0xCF8,a); outl(0xCFC,v); }
/* find a device by class/subclass across ALL 256 buses + all functions (NVMe/AHCI often live on high buses) */
int pci_find(int cls,int sub,int prog,int* obus,int* oslot,int* ofn){
    for(int bus=0;bus<256;bus++) for(int slot=0;slot<32;slot++){
        u32 id0=pci_cfg(bus,slot,0,0); if((id0&0xFFFF)==0xFFFF)continue;
        int nf=(pci_cfg(bus,slot,0,0x0C)&0x800000)?8:1;   /* multifunction? */
        for(int fn=0;fn<nf;fn++){ u32 id=pci_cfg(bus,slot,fn,0); if((id&0xFFFF)==0xFFFF)continue;
            u32 cl=pci_cfg(bus,slot,fn,0x08); int c=(cl>>24)&0xFF, s=(cl>>16)&0xFF, pr=(cl>>8)&0xFF;
            if(c==cls && s==sub && (prog<0||pr==prog)){ if(obus)*obus=bus; if(oslot)*oslot=slot; if(ofn)*ofn=fn; return 1; } }
    }
    return 0;
}
void pci_scan(void){ pci_n=0;
    for(int bus=0;bus<4;bus++) for(int slot=0;slot<32;slot++){ u32 id=pci_cfg(bus,slot,0,0); u16 v=id&0xFFFF; if(v==0xFFFF)continue;
        u32 cl=pci_cfg(bus,slot,0,0x08);
        if(pci_n<PCI_MAX){ pci_ven[pci_n]=v; pci_dev[pci_n]=(u16)(id>>16); pci_cls[pci_n]=(cl>>24)&0xFF; pci_sub[pci_n]=(cl>>16)&0xFF; pci_bus[pci_n]=(u8)bus; pci_slot[pci_n]=(u8)slot; pci_n++; } }
}
int g_pci_quiesced=0;
void pci_quiesce(void){ g_pci_quiesced=0;
    for(int bus=0;bus<256;bus++) for(int slot=0;slot<32;slot++){
        u32 id0=pci_cfg(bus,slot,0,0); if((id0&0xFFFF)==0xFFFF)continue;
        int nf=(pci_cfg(bus,slot,0,0x0C)&0x800000)?8:1;
        for(int fn=0;fn<nf;fn++){ u32 id=pci_cfg(bus,slot,fn,0); if((id&0xFFFF)==0xFFFF)continue;
            u32 hdr=(pci_cfg(bus,slot,fn,0x0C)>>16)&0x7F; if(hdr!=0)continue;
            u32 cl=pci_cfg(bus,slot,fn,0x08); int c=(int)((cl>>24)&0xFF), sc=(int)((cl>>16)&0xFF), pi=(int)((cl>>8)&0xFF);
            int stop=(c==0x01||c==0x02||c==0x04||c==0x0D);
            if(c==0x0C&&sc!=0x05){ if(sc!=0x03||pi!=0x30)stop=1; else { u32 b0=pci_cfg(bus,slot,fn,0x10); if((b0&6)==4&&pci_cfg(bus,slot,fn,0x14))stop=1; } }
            if(!stop)continue;
            u32 cmd=pci_cfg(bus,slot,fn,0x04); if(!(cmd&0x04))continue;
            pci_cfgw(bus,slot,fn,0x04,(cmd&0xFFFFu)&~0x04u); g_pci_quiesced++; } } }
