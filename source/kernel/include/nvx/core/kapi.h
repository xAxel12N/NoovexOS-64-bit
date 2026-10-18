#ifndef NVX_CORE_KAPI_H
#define NVX_CORE_KAPI_H

#include "include/nvx/base.h"

u32 kapi_pci_rd32(int bus,int dev,int fn,int off);
void kapi_pci_wr32(int bus,int dev,int fn,int off,u32 v);
int kapi_pci_find(u16 vendor,const u16* devices,int nth,int* obus,int* odev,int* ofn);
int kapi_pci_find_class(int cls,int sub,int nth,int* obus,int* odev,int* ofn);
void kapi_pci_enable(int bus,int dev,int fn);
u8 kapi_inb(u16 p);
u16 kapi_inw(u16 p);
u32 kapi_inl(u16 p);
void kapi_outb(u16 p,u8 v);
void kapi_outw(u16 p,u16 v);
void kapi_outl(u16 p,u32 v);
void* kapi_dma_pages(unsigned long pages);
u64 kapi_phys(const void* p);
void kapi_delay_us(u32 us);
u64 kapi_ns(void);
void kapi_log(const char* s);
void kapi_mb(void);

#endif
