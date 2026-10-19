#ifndef NVX_DRIVERS_STORAGE_AHCI_H
#define NVX_DRIVERS_STORAGE_AHCI_H

#include "include/nvx/base.h"

int ahci_identify(volatile u32* abar,int port,char* model,u64* sectors);
int nic_up_raw(void);
void nic_send(const u8* data,int len);
int nic_recv(u8* out,int max);
void nic_flush_rx(void);
void nic_kick(void);
int eth_hdr(u8* f,const u8* dst,u16 type);
void arp_request(u32 tip);
int net_poll(u8* rx,int max,int ticks);

#endif
