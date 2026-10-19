#ifndef NVX_DRIVERS_BUS_PCI_H
#define NVX_DRIVERS_BUS_PCI_H

#include "include/nvx/base.h"

#define PCI_MAX 24

extern u16 pci_ven[PCI_MAX], pci_dev[PCI_MAX];
extern u8 pci_cls[PCI_MAX], pci_sub[PCI_MAX], pci_bus[PCI_MAX], pci_slot[PCI_MAX];
extern int pci_n;
extern int g_pci_quiesced;

u32 pci_cfg(int bus,int slot,int fn,int off);
void pci_cfgw(int bus,int slot,int fn,int off,u32 v);
int pci_find(int cls,int sub,int prog,int* obus,int* oslot,int* ofn);
void pci_scan(void);
void pci_quiesce(void);

#endif
