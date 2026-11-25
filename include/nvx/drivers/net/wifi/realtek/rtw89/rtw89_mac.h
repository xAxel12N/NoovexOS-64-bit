/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * rtw89 MAC init (DLE/HFC/DMAC/CMAC), BAR sizing helpers and scan offload glue (rtw89c side of the rtw89 port).
 * Derived from Linux drivers/net/wireless/realtek/rtw89 (mac.c, mac.h, reg.h, fw.c, fw.h, rtw8852b.c, rtw8852b_common.c;
 * GPL-2.0 OR BSD-3-Clause, used under BSD-3-Clause).
 * Copyright(c) 2019-2022  Realtek Corporation
 * Licence text: drivers/net/wifi/realtek/rtw89/LICENCE.BSD-3-Clause
 */
#ifndef NVX_DRIVERS_NET_WIFI_REALTEK_RTW89_MAC_H
#define NVX_DRIVERS_NET_WIFI_REALTEK_RTW89_MAC_H
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_pci.h"

#define R_AX_SYS_FUNC_EN 0x0002
#define R_AX_SPS_DIG_ON_CTRL0 0x0200
#define R_AX_WLRF_CTRL 0x02F0
#define R_AX_PHYREG_SET 0x8040
#define R_AX_DMAC_CLK_EN 0x8404
#define R_AX_HCI_FC_CTRL 0x8A00
#define R_AX_CH_PAGE_CTRL 0x8A04
#define R_AX_ACH0_PAGE_CTRL 0x8A10
#define R_AX_PUB_PAGE_CTRL1 0x8A90
#define R_AX_PUB_PAGE_CTRL2 0x8A94
#define R_AX_WP_PAGE_CTRL1 0x8AA0
#define R_AX_WP_PAGE_CTRL2 0x8AA4
#define R_AX_WDE_PKTBUF_CFG 0x8C08
#define R_AX_WDE_QTA0_CFG 0x8C40
#define R_AX_WDE_INI_STATUS 0x8D00
#define R_AX_PLE_PKTBUF_CFG 0x9008
#define R_AX_PLE_QTA0_CFG 0x9040
#define R_AX_PLE_INI_STATUS 0x9100
#define R_AX_MPDU_PROC 0x9C00
#define R_AX_ACTION_FWD0 0x9C04
#define R_AX_TF_FWD 0x9C14
#define R_AX_CUT_AMSDU_CTRL 0x9C40
#define R_AX_SEC_ENG_CTRL 0x9D00
#define R_AX_SEC_MPDU_PROC 0x9D04
#define R_AX_SS_CTRL 0x9E10
#define R_AX_CK_EN 0xC004
#define R_AX_CCA_CONTROL 0xC390
#define R_AX_RCR 0xCE00
#define R_AX_PLCP_HDR_FLTR 0xCE04
#define R_AX_RX_FLTR_OPT 0xCE20
#define R_AX_CTRL_FLTR 0xCE24
#define R_AX_MGNT_FLTR 0xCE28
#define R_AX_DATA_FLTR 0xCE2C
#define R_AX_ADDR_CAM_CTRL 0xCE34

#define B_AX_MAC_FUNC_EN (1u<<30)
#define B_AX_DMAC_FUNC_EN (1u<<29)
#define B_AX_DMAC_CRPRT (1u<<31)
#define B_AX_MPDU_PROC_EN (1u<<28)
#define B_AX_WD_RLS_EN (1u<<27)
#define B_AX_DLE_WDE_EN (1u<<26)
#define B_AX_TXPKT_CTRL_EN (1u<<25)
#define B_AX_STA_SCH_EN (1u<<24)
#define B_AX_DLE_PLE_EN (1u<<23)
#define B_AX_PKT_BUF_EN (1u<<22)
#define B_AX_DMAC_TBL_EN (1u<<21)
#define B_AX_PKT_IN_EN (1u<<20)
#define B_AX_DLE_CPUIO_EN (1u<<19)
#define B_AX_DISPATCHER_EN (1u<<18)
#define B_AX_MAC_SEC_EN (1u<<16)
#define B_AX_BBRPT_CLK_EN (1u<<17)
#define RTW89_DMAC_CLK_ALL ((1u<<16)|(1u<<17)|(1u<<18)|(1u<<19)|(1u<<20)|(1u<<24)|(1u<<25)|(1u<<27))
#define RTW89_DMAC_FUNC_RUN (B_AX_MAC_FUNC_EN|B_AX_DMAC_FUNC_EN|B_AX_MAC_SEC_EN|B_AX_DISPATCHER_EN|B_AX_DLE_CPUIO_EN|B_AX_PKT_IN_EN|B_AX_DMAC_TBL_EN|B_AX_PKT_BUF_EN|B_AX_STA_SCH_EN|B_AX_TXPKT_CTRL_EN|B_AX_WD_RLS_EN|B_AX_MPDU_PROC_EN|B_AX_DMAC_CRPRT)
#define RTW89_DMAC_FUNC_PRE (B_AX_MAC_FUNC_EN|B_AX_DMAC_FUNC_EN|B_AX_DISPATCHER_EN|B_AX_PKT_BUF_EN)
#define RTW89_DLE_EN (B_AX_DLE_WDE_EN|B_AX_DLE_PLE_EN)
#define RTW89_CMAC_FUNC_RUN ((1u<<31)|(1u<<30)|(1u<<29)|(1u<<28)|0x3Fu)
#define RTW89_CMAC_CK_RUN ((1u<<30)|0x3Fu)
#define B_AX_OCP_L1_MASK 0xE000u
#define B_AX_REG_ZCDC_H_MASK 0x60000u
#define B_AX_FEN_BBRSTB 1u
#define B_AX_FEN_BB_GLB_RSTN 2u
#define B_AX_AFC_AFEDIG (1u<<17)
#define XTAL_SI_WL_RFC_S0 0x80
#define XTAL_SI_WL_RFC_S1 0x81
#define PHYREG_SET_XYN_CYCLE 0xE
#define B_AX_HCI_FC_EN 1u
#define B_AX_HCI_FC_CH12_EN 8u
#define B_AX_HCI_FC_MODE_MASK 0x6u
#define B_AX_HCI_FC_WD_FULL_COND_MASK 0x30u
#define B_AX_HCI_FC_WP_CH07_FULL_COND_MASK 0xC0u
#define B_AX_HCI_FC_WP_CH811_FULL_COND_MASK 0x300u
#define B_AX_HCI_FC_CH12_FULL_COND_MASK 0xC00u
#define B_AX_SS_EN 1u
#define B_AX_SS_INIT_DONE_1 (1u<<31)
#define B_AX_SS_WARM_INIT_FLG (1u<<29)
#define B_AX_SS_NONEMPTY_SS2FINFO_EN (1u<<28)
#define TRXCFG_MPDU_PROC_ACT_FRWD 0x02A95A95u
#define TRXCFG_MPDU_PROC_TF_FRWD 0x0000AA55u
#define TRXCFG_MPDU_PROC_CUT_CTRL 0x010E05F0u
#define B_AX_ADDR_CAM_CLR (1u<<8)
#define B_AX_ADDR_CAM_EN 1u
#define RX_FLTR_FRAME_TO_HOST 0x55555555u
#define B_AX_RX_MPDU_MAX_LEN_MASK 0x3F0000u
#define B_AX_A_BCN_CHK_EN (1u<<7)
#define B_AX_A_BC (1u<<2)
#define B_AX_A_A1_MATCH (1u<<1)
#define RTW89_DEFAULT_RX_FLTR ((1u<<1)|(1u<<2)|(1u<<3)|(1u<<4)|(1u<<5)|(1u<<10)|(1u<<14)|(3u<<24)|(1u<<7))
#define RTW89_PLCP_FLTR 0x7Fu
#define RTW89_CCA_SET 0x712100FFu
#define RTW89_CCA_CLR 0x8E1E0100u

#define RTW89_C2H_CL_OFLD 1
#define RTW89_C2H_FUNC_SCANOFLD_RSP 9
enum { RTW89_SCAN_DWELL_NOTIFY=0, RTW89_SCAN_PRE_TX_NOTIFY, RTW89_SCAN_POST_TX_NOTIFY, RTW89_SCAN_ENTER_CH_NOTIFY, RTW89_SCAN_LEAVE_CH_NOTIFY, RTW89_SCAN_END_SCAN_NOTIFY };
enum { RTW89_QTA_SCC=0, RTW89_QTA_DLFW=1 };

#define RTW89_BAR_MIN 0x10000u
#define RTW89_BAR_MAX 0x200000u
#define RTW89_PHY_FILE "rtw89/rtw8852b_phy.bin"
#define RTW89_PHY_FILE_C "rtw89/rtw8852c_phy.bin"

struct rtw89_scan_state { int active,ended; u8 cur_ch,last_rsn,nch; u32 enter,leave,events,polls,start_err; u8 ch[RTW89_SCAN_MAX_CH]; };
extern struct rtw89_scan_state g_r89scan;
extern int (*rtw89_scan_pre_hook)(struct rtw89_softc* sc);

u32 rtw89_bar_size(u32 probe);
int rtw89_mac_dle_init(struct rtw89_softc* sc,int mode);
int rtw89_mac_hfc_init(struct rtw89_softc* sc,int reset,int en,int h2c_en);
int rtw89_mac_dmac_pre_init(struct rtw89_softc* sc);
int rtw89_mac_enable_bb_rf(struct rtw89_softc* sc);
int rtw89_mac_sys_init(struct rtw89_softc* sc);
int rtw89_mac_trx_init(struct rtw89_softc* sc);
int rtw89_mac_init_post(struct rtw89_softc* sc);
void rtw89_mac_set_rx_fltr(struct rtw89_softc* sc,u32 f);
void rtw89_scan_c2h(struct rtw89_softc* sc,u8 cat,u8 cls,u8 func,const u8* pl,u32 len);
int rtw89_scan_begin(struct rtw89_softc* sc,const u8* ch,int n);
int rtw89_scan_finished(struct rtw89_softc* sc);
void rtw89_scan_poll(struct rtw89_softc* sc,u32 budget);
#endif
