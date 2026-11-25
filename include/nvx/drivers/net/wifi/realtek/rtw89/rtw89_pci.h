/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * rtw89 PCIe DMA rings, TX/RX descriptors, PHY tables, channel set and scan offload (rtw89b side of the rtw89 port).
 * Derived from Linux drivers/net/wireless/realtek/rtw89 (pci.h, txrx.h, core.h, phy.h, fw.h, rtw8852c.c; GPL-2.0 OR BSD-3-Clause, used under BSD-3-Clause).
 * Copyright(c) 2019-2022  Realtek Corporation
 * Licence text: drivers/net/wifi/realtek/rtw89/LICENCE.BSD-3-Clause
 */
#ifndef NVX_DRIVERS_NET_WIFI_REALTEK_RTW89_PCI_H
#define NVX_DRIVERS_NET_WIFI_REALTEK_RTW89_PCI_H
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_core.h"

#define R_AX_PCIE_INIT_CFG1 0x1000
#define B_AX_RXHCI_EN (1u<<13)
#define B_AX_TXHCI_EN (1u<<11)
#define B_AX_RST_BDRAM (1u<<3)
#define R_AX_PCIE_DMA_STOP1 0x1010
#define B_AX_STOP_PCIEIO (1u<<20)
#define B_AX_STOP_WPDMA (1u<<19)
#define B_AX_STOP_CH12 (1u<<18)
#define B_AX_TX_STOP1_MASK_V1 0x00070F00u
#define R_AX_TXBD_RWPTR_CLR1 0x1014
#define R_AX_RXBD_RWPTR_CLR 0x1018
#define R_AX_PCIE_DMA_BUSY1 0x101C
#define DMA_BUSY1_CHECK_V1 0x00070F00u
#define TXBD_HW_IDX_MASK 0x0FFF0000u
#define R_AX_HAXI_INIT_CFG1 0x1000
#define B_AX_TXHCI_EN_V1 (1u<<7)
#define B_AX_RXHCI_EN_V1 (1u<<15)
#define B_AX_STOP_AXI_MST (1u<<17)
#define R_AX_HAXI_DMA_STOP2 0x11C0
#define R_AX_TXBD_RWPTR_CLR2_V1 0x11C4
#define R_AX_HAXI_DMA_BUSY2 0x11C8
#define R_AX_RXBD_RWPTR_CLR_V1 0x1200
#define R_AX_HAXI_DMA_BUSY3 0x1208
#define B_AX_TX_STOP1_MASK_HAXI 0x0007FF00u
#define DMA_BUSY1_CHECK_HAXI 0x0007FF00u

#define RTW89_PCI_TXBD_OPT_LS (1u<<14)
#define RTW89_PCI_TXWP_VALID (1u<<15)
#define RTW89_PCI_ADDR_MSDU_LS (1u<<15)
#define RTW89_PCI_RXBD_FS (1u<<15)
#define RTW89_PCI_RXBD_LS (1u<<14)
#define RTW89_PCI_RX_BUF_SIZE (11454+40+4)

enum { R89_TX_ACH0=0, R89_TX_CH8=1, R89_TX_CH12=2, R89_TX_N=3 };
enum { R89_RX_RXQ=0, R89_RX_RPQ=1, R89_RX_N=2 };
enum { RTW89_RX_TYPE_WIFI=0, RTW89_RX_TYPE_TX_REL_HOST=7, RTW89_RX_TYPE_C2H=10, RTW89_RX_TYPE_H2C=13, RTW89_RX_TYPE_FWDL=14 };
#define RTW89_DMA_ACH0 0
#define RTW89_DMA_B0MG 8
#define RTW89_DMA_H2C 12
#define RTW89_QSEL_BE0 0
#define RTW89_QSEL_B0_MGMT 0x12
#define RTW89_HW_RATE_CCK1 0x0
#define RTW89_HW_RATE_OFDM6 0x4

struct r89p_regs { u16 num,idx,bdram,desa; };
struct r89p_ring { u8* va; u64 pa; u8* buf; u64 bpa; u32 bsz; u16 len,wp,rp; struct r89p_regs r; u8 sidx,max,min; };
struct r89p_state { int ready; struct r89p_ring tx[R89_TX_N]; struct r89p_ring rx[R89_RX_N]; u32 tx_seq,h2c_sent,rx_frames,rx_c2h,rx_drop; };
struct rtw89_txinfo { u8 ch_dma,qsel,macid,rate,use_rate,fw_dl,en_info,wd_page,hdr_len; u16 seq; u32 pkt_size; };
#define RTW89_RATE_AUTO 0xFF
struct rtw89_rxinfo { u32 pkt_size,offset; u8 type,shift,drv_info,long_rxd,mac_info,fs,ls; u16 tag,bd_len; u16 data_rate; u8 bw; };

extern struct r89p_state g_r89p;
extern const struct rtw89_hci_ops rtw89_pci_hci;
extern void* (*rtw89_dma_pages)(unsigned long pages);
extern void (*rtw89_rx_frame)(const u8* frame,int len,int rssi);
int rtw89_pci_attach(struct rtw89_softc* sc);
int rtw89_pci_rings_alloc(struct rtw89_softc* sc);
void rtw89_pci_rings_reset(struct rtw89_softc* sc);
int rtw89_pci_pre_fwdl(struct rtw89_softc* sc);
void rtw89_pci_post_init(struct rtw89_softc* sc);
void rtw89_pci_txdma_all(struct rtw89_softc* sc,int on);
u32 rtw89_fill_fwcmd_desc(u8 chip,u8* d,u32 pkt_len,int fw_dl);
u32 rtw89_fill_txwd(u8 chip,u8* d,const struct rtw89_txinfo* ti);
u32 rtw89_fill_txaddr(u8* d,u32 len,u64 dma);
int rtw89_parse_rxdesc(u8 chip,const u8* b,u32 len,struct rtw89_rxinfo* ri);
int rtw89_pci_h2c_tx(struct rtw89_softc* sc,const u8* pkt,u32 len,int kind);
int rtw89_pci_c2h_rx(struct rtw89_softc* sc,u8* buf,u32 cap);
int rtw89_pci_tx_frame(struct rtw89_softc* sc,const u8* frame,u32 len,int mgmt,u8 rate,u8 macid);
int rtw89_pci_rx_poll(struct rtw89_softc* sc,u8* c2h,u32 cap,u32 budget);

int rtw8852c_pwr_on(struct rtw89_softc* sc);

#define RTW89_PHY_ADDR_OFFSET 0x10000u
#define RTW89_BYPASS_CR_DATA 0xbabecafeu
#define R_SWSI_DATA_V1 0x0370
#define R_SWSI_BIT_MASK_V1 0x0374
#define R_SWSI_V1 0x174C
#define B_SWSI_W_BUSY_V1 (1u<<24)
#define RTW89_RF_ADDR_ADSEL (1u<<16)
enum { RTW89_TBL_BB=1, RTW89_TBL_BB_GAIN=2, RTW89_TBL_RADIOA=3, RTW89_TBL_RADIOB=4, RTW89_TBL_NCTL=5 };
struct rtw89_reg2 { u32 addr,data; };
struct rtw89_phy_tbl { const struct rtw89_reg2* regs; u32 n; u8 kind; };
struct rtw89_phy_tbls { struct rtw89_phy_tbl t[6]; u32 nt; };
int rtw89_phy_tables_parse(const u8* d,u32 len,struct rtw89_phy_tbls* out);
struct rtw89_bb_gain { signed char lna[4][2][7], tia[4][2][2], rpl20[4][2], rpl40[4][2][9], rpl80[4][2][13], lnab[4][2][7], op1[4][2][7], tiaop1[4][2][8]; u32 n; };
extern struct rtw89_bb_gain rtw89_gain;
void rtw89_bb_gain_cfg(struct rtw89_bb_gain* g,u32 addr,u32 data);
u8 rtw89_gain_band(u8 ch);
int rtw89_phy_sel_headline(const struct rtw89_phy_tbl* t,u8 rfe,u8 cv,u32* hsize,u32* hidx);
int rtw89_phy_init_reg(struct rtw89_softc* sc,const struct rtw89_phy_tbl* t,u8 rfe,u8 cv,int path);
int rtw89_phy_write_rf(struct rtw89_softc* sc,int path,u32 addr,u32 mask,u32 data);
int rtw89_phy_load_8852b(struct rtw89_softc* sc,const u8* blob,u32 len);
int rtw89_chan_valid(u8 ch);
u8 rtw89_chan_band(u8 ch);

#define RTW89_SCAN_CH_TIME 45
#define RTW89_SCAN_DWELL 20
#define RTW89_SCAN_DFS_TIME 105
#define RTW89_SCANOFLD_PKT_NONE 0xFF
#define H2C_CL_MAC_FW_OFLD 0x9
#define H2C_FUNC_ADD_SCANOFLD_CH 0x16
#define H2C_FUNC_SCANOFLD 0x17
#define H2C_FUNC_OFLD_CFG 0x14
#define RTW89_CHINFO_ELEM_SIZE 28
#define RTW89_SCANOFLD_SIZE 28
#define RTW89_SCAN_MAX_CH 64
#define RTW89_SCAN_ACK_US 100000u
u32 rtw89_build_chinfo(u8* out,u32 cap,const u8* ch,int n,u8 probe_id);
u32 rtw89_build_scanofld(u8* out,u8 macid,u8 port,u8 band,int enable);
int rtw89_hw_scan_start(struct rtw89_softc* sc,const u8* ch,int n);
int rtw89_hw_scan_stop(struct rtw89_softc* sc);
#endif
