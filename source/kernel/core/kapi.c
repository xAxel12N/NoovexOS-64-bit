#include "include/nvx/base.h"
#include "include/nvx/mm/pmm_vmm.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/arch/x86_64/hrtimer.h"
#include "include/nvx/core/kapi.h"
u32 kapi_pci_rd32(int bus,int dev,int fn,int off){ return pci_cfg(bus,dev,fn,off); }
void kapi_pci_wr32(int bus,int dev,int fn,int off,u32 v){ pci_cfgw(bus,dev,fn,off,v); }
int kapi_pci_find(u16 vendor,const u16* devices,int nth,int* obus,int* odev,int* ofn){
    for(int bus=0;bus<256;bus++) for(int dev=0;dev<32;dev++){ u32 h=pci_cfg(bus,dev,0,0x0C); int fns=((h>>16)&0x80)?8:1;
        for(int fn=0;fn<fns;fn++){ u32 id=pci_cfg(bus,dev,fn,0); if((id&0xFFFF)==0xFFFF){ if(fn==0)break; continue; }
            if((id&0xFFFF)!=vendor)continue;
            for(int i=0;devices[i];i++) if((u16)(id>>16)==devices[i]){ if(nth--==0){ *obus=bus; *odev=dev; *ofn=fn; return 1; } } } }
    return 0;
}
int kapi_pci_find_class(int cls,int sub,int nth,int* obus,int* odev,int* ofn){
    for(int bus=0;bus<256;bus++) for(int dev=0;dev<32;dev++){ u32 h=pci_cfg(bus,dev,0,0x0C); int fns=((h>>16)&0x80)?8:1;
        for(int fn=0;fn<fns;fn++){ u32 id=pci_cfg(bus,dev,fn,0); if((id&0xFFFF)==0xFFFF){ if(fn==0)break; continue; }
            u32 c=pci_cfg(bus,dev,fn,0x08); if((int)(c>>24)==cls&&(int)((c>>16)&0xFF)==sub){ if(nth--==0){ *obus=bus; *odev=dev; *ofn=fn; return 1; } } } }
    return 0;
}
void kapi_pci_enable(int bus,int dev,int fn){ u32 c=pci_cfg(bus,dev,fn,0x04); pci_cfgw(bus,dev,fn,0x04,(c&0xFFFFu)|0x407u); }
u8 kapi_inb(u16 p){ return inb(p); }
u16 kapi_inw(u16 p){ return inw(p); }
u32 kapi_inl(u16 p){ return inl(p); }
void kapi_outb(u16 p,u8 v){ outb(p,v); }
void kapi_outw(u16 p,u16 v){ outw(p,v); }
void kapi_outl(u16 p,u32 v){ outl(p,v); }
void* kapi_dma_pages(unsigned long pages){ return dma_alloc_pages(pages); }
u64 kapi_phys(const void* p){ return (u64)p; }
void kapi_delay_us(u32 us){ hr_delay_us(us); }
u64 kapi_ns(void){ return hr_ns(); }
void kapi_log(const char* s){ klog(s); }
void kapi_mb(void){ cpu_wmb(); }
