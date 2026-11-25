/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Derived from Linux drivers/net/wireless/mediatek/mt76 (mt792x_regs.h, mt7921/regs.h, dma.h).
 * Copyright (C) 2020 MediaTek Inc.
 * Licence text: drivers/net/wifi/mediatek/mt7921/LICENCE.BSD-3-Clause-Clear
 */
#ifndef NVX_MT7921_REGS_H
#define NVX_MT7921_REGS_H
#define MT_BAR0_SIZE 0x100000u
#define MT_OWN_RETRY 10
#define MT_HIF_REMAP_L1 0xfe24cu
#define MT_HIF_REMAP_BASE_L1 0x40000u
#define MT_HW_BOUND 0x70010020u
#define MT_HW_CHIPID 0x70010200u
#define MT_HW_REV 0x70010204u
#define MT_CONN_ON_LPCTL 0x7c060010u
#define PCIE_LPCR_HOST_SET_OWN (1u<<0)
#define PCIE_LPCR_HOST_CLR_OWN (1u<<1)
#define PCIE_LPCR_HOST_OWN_SYNC (1u<<2)
#define MT_CONN_ON_MISC 0x7c0600f0u
#define MT_TOP_MISC_FW_STATE 7u
#define MT_TOP_MISC2_FW_PWR_ON 1u
#define MT_TOP_MISC2_FW_N9_RDY 3u
#define MT_TOP_LPCR_HOST_BAND0 0x18060010u
#define MT_TOP_LPCR_HOST_FW_OWN (1u<<0)
#define MT_TOP_LPCR_HOST_DRV_OWN (1u<<1)
#define MT_WFSYS_SW_RST_B 0x18000140u
#define WFSYS_SW_RST_B (1u<<0)
#define WFSYS_SW_INIT_DONE (1u<<4)
#define MT_PCIE_MAC_INT_ENABLE 0x10188u
#define MT_PCIE_MAC_PM 0x10194u
#define MT_PCIE_MAC_PM_L0S_DIS (1u<<8)
#define MT_WFDMA0(o) (0xd4000u+(o))
#define MT_WFDMA0_RST MT_WFDMA0(0x100)
#define MT_WFDMA0_RST_LOGIC_RST (1u<<4)
#define MT_WFDMA0_RST_DMASHDL_ALL_RST (1u<<5)
#define MT_WFDMA0_HOST_INT_STA MT_WFDMA0(0x200)
#define MT_WFDMA0_HOST_INT_ENA MT_WFDMA0(0x204)
#define MT_WFDMA0_GLO_CFG MT_WFDMA0(0x208)
#define MT_WFDMA0_RST_DTX_PTR MT_WFDMA0(0x20c)
#define MT_WFDMA0_GLO_CFG_EXT0 MT_WFDMA0(0x2b0)
#define MT_WFDMA0_PRI_DLY_INT_CFG0 MT_WFDMA0(0x2f0)
#define MT_WFDMA0_TX_EXT(n) MT_WFDMA0(0x600+((n)<<2))
#define MT_WFDMA0_RX_EXT(n) MT_WFDMA0(0x680+((n)<<2))
#define MT_TX_RING_BASE MT_WFDMA0(0x300)
#define MT_RX_EVENT_RING_BASE MT_WFDMA0(0x500)
#define MT_RING_STRIDE 0x10u
#define MT_GLO_TX_DMA_EN (1u<<0)
#define MT_GLO_TX_DMA_BUSY (1u<<1)
#define MT_GLO_RX_DMA_EN (1u<<2)
#define MT_GLO_RX_DMA_BUSY (1u<<3)
#define MT_GLO_DMA_SIZE(x) ((u32)(x)<<4)
#define MT_GLO_TX_WB_DDONE (1u<<6)
#define MT_GLO_FIFO_DIS_CHECK (1u<<11)
#define MT_GLO_FIFO_LITTLE_ENDIAN (1u<<12)
#define MT_GLO_RX_WB_DDONE (1u<<13)
#define MT_GLO_DISP_BASE_PTR_CHAIN_EN (1u<<15)
#define MT_GLO_OMIT_RX_INFO_PFET2 (1u<<21)
#define MT_GLO_OMIT_RX_INFO (1u<<27)
#define MT_GLO_OMIT_TX_INFO (1u<<28)
#define MT_GLO_CLK_GAT_DIS (1u<<30)
#define MT_GLO_EXT0_TX_DMASHDL_EN (1u<<6)
#define MT_DMASHDL_SW_CONTROL 0x7c026004u
#define MT_DMASHDL_BYPASS (1u<<28)
#define MT_WFDMA_DUMMY_CR 0x54000120u
#define MT_WFDMA_NEED_REINIT (1u<<1)
#define MT_DMA_CTL_SD_LEN0(l) (((u32)(l)&0x3fffu)<<16)
#define MT_DMA_CTL_LEN0_GET(c) (((c)>>16)&0x3fffu)
#define MT_DMA_CTL_LAST_SEC0 (1u<<30)
#define MT_DMA_CTL_DMA_DONE (1u<<31)
#define MT_PREFETCH(b,d) (((u32)(b)<<16)|(d))
#define MT_TXQ_HW_DATA 0
#define MT_TXQ_HW_FWDL 16
#define MT_TXQ_HW_MCU 17
#define MT_RXQ_HW_MCU 0
#define MT_RXQ_HW_DATA 2
#define MT_RXQ_HW_MCU_WA 4
u32 mt7921_reg_addr(struct mt7921_softc* sc,u32 addr);
u32 mt7921_l1_r32(struct mt7921_softc* sc,u32 addr);
int mt7921_fw_own(struct mt7921_softc* sc);
int mt7921_drv_own(struct mt7921_softc* sc);
int mt7921_wfsys_reset(struct mt7921_softc* sc);
#endif
