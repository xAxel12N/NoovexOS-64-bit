/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Derived from Linux drivers/net/wireless/mediatek/mt76 (mt76.h, usb.c, mt792x_usb.c, mt792x_regs.h, mt7921/usb.c).
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 * Copyright (C) 2022-2023 MediaTek Inc.
 * Licence text: drivers/net/wifi/mediatek/mt7921/LICENCE.BSD-3-Clause-Clear
 */
#ifndef NVX_MT7921_USB_H
#define NVX_MT7921_USB_H
#include "drivers/net/wifi/mediatek/mt7921/mt7921.h"
#define MTU_VEND_DEV_MODE 0x01
#define MTU_VEND_WRITE 0x02
#define MTU_VEND_POWER_ON 0x04
#define MTU_VEND_READ_EXT 0x63
#define MTU_VEND_WRITE_EXT 0x66
#define MTU_RT_VENDOR 0x5f
#define MTU_RT_UHW 0x5e
#define MTU_EP_OUT_CMD 0
#define MTU_EP_OUT_BE 1
#define MTU_EP_IN_DATA 0
#define MTU_EP_IN_EVT 1
#define MTU_NOUT 6
#define MTU_NIN 2
#define MTU_HW_CHIPID 0x70010200u
#define MTU_HW_REV 0x70010204u
#define MTU_CBTOP_RGU_WF_RST 0x70002600u
#define MTU_UMAC(o) (0x74000000u+(o))
#define MTU_UDMA_TX_QSEL MTU_UMAC(0x008)
#define MTU_FW_DL_EN (1u<<3)
#define MTU_UDMA_WLCFG_1 MTU_UMAC(0x00c)
#define MTU_UDMA_WLCFG_0 MTU_UMAC(0x018)
#define MTU_WL_RX_AGG_TO 0xffu
#define MTU_WL_RX_AGG_LMT 0xff00u
#define MTU_WL_TX_TMOUT_FUNC_EN (1u<<16)
#define MTU_WL_RX_MPSZ_PAD0 (1u<<18)
#define MTU_WL_RX_FLUSH (1u<<19)
#define MTU_TICK_1US_EN (1u<<20)
#define MTU_WL_RX_EN (1u<<22)
#define MTU_WL_TX_EN (1u<<23)
#define MTU_WL_RX_BUSY (1u<<30)
#define MTU_WL_TX_BUSY (1u<<31)
#define MTU_WL_RX_AGG_PKT_LMT 0xffu
#define MTU_WL_TX_TMOUT_LMT (0xfffffu<<8)
#define MTU_CONN_INFRA_STATUS MTU_UMAC(0xa20)
#define MTU_WFSYS_INIT_DONE (1u<<22)
#define MTU_CONN_INFRA_STATUS_SEL MTU_UMAC(0xa24)
#define MTU_EPCTL_RST_OPT 0x74011890u
#define MTU_UWFDMA0_GLO_CFG 0x7c024208u
#define MTU_UWFDMA0_TX_EXT(n) (0x7c024600u+((u32)(n)<<2))
#define MTU_GLO_TX_DMA_EN (1u<<0)
#define MTU_GLO_RX_DMA_EN (1u<<2)
#define MTU_GLO_RX_DMA_BUSY (1u<<3)
#define MTU_GLO_FW_DWLD_BYPASS_DMASHDL (1u<<9)
#define MTU_GLO_OMIT_RX_INFO_PFET2 (1u<<21)
#define MTU_GLO_OMIT_RX_INFO (1u<<27)
#define MTU_GLO_OMIT_TX_INFO (1u<<28)
#define MTU_DMASHDL(o) (0x7c026000u+(o))
#define MTU_WFDMA_DUMMY_CR 0x54000120u
#define MTU_WFDMA_NEED_REINIT (1u<<1)
#define MTU_WFDMA_HOST_CONFIG 0x7c027030u
#define MTU_HOST_RXEVT_EP4_EN (1u<<6)
#define MTU_CONN_ON_MISC 0x7c0600f0u
#define MTU_FW_PWR_ON 1u
#define MTU_FW_N9_RDY 3u
#define MTU_HDR 4
#define MTU_TXD 64
#define MTU_BUF (MT_FWDL_CHUNK+64)
struct mt7921u_hc {
    void* ctx;
    int (*ctrl)(void* ctx,u8 rt,u8 req,u16 val,u16 idx,u8* buf,u16 len);
    int (*bulk_out)(void* ctx,u8 ep,const u8* buf,u32 len,u32 ms);
    int (*bulk_in)(void* ctx,u8 ep,u8* buf,u32 cap,u32 ms);
    u8 out_ep[MTU_NOUT],in_ep[MTU_NIN];
};
struct mt7921u_dev { const struct mt7921u_hc* hc; int hung; u32 ctrl_err,bulk_err,rx_bad; };
struct mt_usb_id { u16 vendor,device; const char* name; };
extern const struct mt_usb_id mt7921u_ids[];
extern struct mt7921_softc g_mt7921u;
extern int mt7921u_enable;
const struct mt_usb_id* mt7921u_match(u16 vid,u16 pid);
int mt7921u_parse_eps(const u8* cfg,int n,struct mt7921u_hc* hc,int* iface);
int mt7921u_attach(const struct mt7921u_hc* hc,u16 vid,u16 pid);
void mt7921u_detach(void);
int mt7921u_start(void);
u32 mt7921u_frame(int qid,const u8* buf,u32 len,u8* out,u32 cap);
void mt7921u_status(char* out,int cap);
#endif
