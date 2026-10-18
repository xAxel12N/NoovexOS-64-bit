#ifndef NVX_DRIVERS_NET_NIC_H
#define NVX_DRIVERS_NET_NIC_H

#include "include/nvx/base.h"
#include "include/nvx/mm/dmawin.h"

#define RXR 8

extern char NETLOG[18][52];
extern int netlog_n;
#define nic_initb ((u8*)(u64)DMW(0x1D0000u))
#define nic_rxd   ((u8*)(u64)DMW(0x1D0100u))
#define nic_txd   ((u8*)(u64)DMW(0x1D0200u))
#define nic_rxb   ((u8*)(u64)DMW(0x1D1000u))
#define nic_txb   ((u8*)(u64)DMW(0x1D5000u))
extern u16 nic_io;
extern int nic_bus ,nic_dev ,nic_present ,nic_init ,net_ok;
extern int nic_kind;
extern struct nvx_nic g_mnic;
extern int nic_rx_idx ,nic_tx_idx;
extern u8 my_mac[6];
extern u32 my_ip , gw_ip , dns_ip , my_mask;
extern int g_dhcp_done , g_dhcp_ok;
extern u8 gw_mac[6];
extern int gw_known;
extern u16 ping_ms;

u16 pit_read(void);
void netlog(const char*s);
void be16(u8*b,u16 v);
void be32(u8*b,u32 v);
u16 rd16(const u8*b);
u32 rd32(const u8*b);
u16 cksum(const u8*d,int n);
u32 net_rand(void);
void pci_wcfg(int bus,int slot,int fn,int off,u32 v);

static inline u32 A32(void*p){ return (u32)(u64)p; }

#endif
