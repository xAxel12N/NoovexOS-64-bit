/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Derived from Linux drivers/net/wireless/mediatek/mt76 (mt76_connac_mcu.h, mt76_connac2_mac.h, mt76_connac.h).
 * Copyright (C) 2020 MediaTek Inc.
 * Licence text: drivers/net/wifi/mediatek/mt7921/LICENCE.BSD-3-Clause-Clear
 */
#ifndef NVX_MT7921_MCU_H
#define NVX_MT7921_MCU_H
#include "drivers/net/wifi/mediatek/mt7921/mt7921.h"
#define MT_MCU_TIMEOUT_MS 3000
#define MT_TX_TYPE_CT 0
#define MT_TX_TYPE_SF 1
#define MT_TX_TYPE_CMD 2
#define MT_HDR_FORMAT_CMD 1
#define MT_HDR_FORMAT_802_11 2
#define MT_TX_MCU_PORT_RX_Q0 0x20
#define MT_TX_MCU_PORT_RX_FWDL 0x3e
#define MT_TX_PORT_IDX_MCU 1
#define MT_TXD1_LONG_FORMAT (1u<<31)
#define MT_LMAC_AC00 0
#define MT_LMAC_ALTX0 0x10
#define MCU_Q_QUERY 0
#define MCU_Q_SET 1
#define MCU_Q_NA 3
#define MCU_CMD_UNI_EXT_ACK 7
#define MCU_EXT_QUERY(t) (MCU_CMD(MCU_CMD_EXT_CID)|(((u32)(t)&0xffu)<<8)|MCU_FIELD_QUERY)
#define MCU_EXT_CMD_EFUSE_ACCESS 0x01
#define MCU_EVENT_ROC 0x10
#define MCU_UNI_UNSOLICITED 4
#define MCU_UNI_EVENT_ROC 0x27
#define MCU_UNI_CMD_ROC 0x27
#define UNI_ROC_ACQUIRE 0
#define UNI_ROC_ABORT 1
#define UNI_EVENT_ROC_GRANT 0
#define MT_ROC_REQ_JOIN 0
#define MT_ROC_REQ_ROC 1
#define MT_CHAN_NO_IR (1u<<1)
#define MT_CHAN_RADAR (1u<<3)
#define PKT_TYPE_TXS 0
#define PKT_TYPE_NORMAL 2
#define PKT_TYPE_TXRX_NOTIFY 6
#define PKT_TYPE_RX_EVENT 7
#define MT_NIC_CAP_MAC_ADDR 7
#define MT_NIC_CAP_6G 0x18
#define MT_EE_MAC_ADDR 0x004
#define MT_WCID_AP 1
#define MT_WCID_BMC 0
#define DEV_INFO_ACTIVE 0
#define UNI_BSS_INFO_BASIC 0
#define UNI_BSS_INFO_RLM 2
#define STA_REC_BASIC 0
#define STA_REC_STATE 7
#define STA_REC_KEY_V2 0x11
#define CONNECTION_INFRA_STA 0x10001u
#define CONNECTION_INFRA_AP 0x10002u
#define CONN_STATE_DISCONNECT 0
#define CONN_STATE_CONNECT 1
#define CONN_STATE_PORT_SECURE 2
#define EXTRA_INFO_VER 1
#define EXTRA_INFO_NEW 2
#define MCU_CIPHER_AES_CCMP 5
#define MCU_CIPHER_BIP_CMAC_128 10
#define SCAN_FUNC_SPLIT_SCAN (1u<<5)
#define MT_SCAN_REQ_LEN 1186
#define MT_REQ_MAX 192
#define MT_STA_REMOVE 0xff
u64 mt_ms(void);
void mt_put16(u8* p,u16 v);
void mt_put32(u8* p,u32 v);
u16 mt_get16(const u8* p);
u32 mt_get32(const u8* p);
void mt_zero(void* d,u32 n);
void mt_copy(void* d,const void* s,u32 n);
u32 mt_mcu_frame(struct mt7921_softc* sc,u32 cmd,const void* data,u32 len,u8* out,u32 cap,u8* seqo);
int mt_mcu_status(struct mt7921_softc* sc,u32 cmd);
int mt_mcu_query(struct mt7921_softc* sc,u32 cmd,const void* data,u32 len,const u8** out,int* olen);
int mt_mcu_nic_capab(struct mt7921_softc* sc);
int mt_mcu_efuse_read(struct mt7921_softc* sc,u32 off,u8* val);
int mt_mcu_set_chan_domain(struct mt7921_softc* sc);
int mt_mcu_chan_domain_sync(struct mt7921_softc* sc);
u32 mt_chan_domain_build(u8* b,u32 cap);
u32 mt_scan_req_build(struct mt7921_softc* sc,const u8* ch,int n,u8* out,u32 cap);
void mt_scan_done_event(struct mt7921_softc* sc,const u8* p,u32 n);
void mt_beacon_loss(struct mt7921_softc* sc);
int mt_dev_add(struct mt7921_softc* sc,int en);
u32 mt_bss_req_build(struct mt7921_softc* sc,const u8* bssid,u8 ch,int en,u8* out);
u32 mt_sta_req_build(struct mt7921_softc* sc,u8 wcid,const u8* peer,u8 state,u16 aid,u8* out);
u32 mt_key_req_build(struct mt7921_softc* sc,u8 wcid,int idx,int type,const u8* key,int len,u8* out);
u32 mt_txd_build(struct mt7921_softc* sc,const u8* f,u32 len,u8* out);
int mt_rxd_parse(const u8* buf,u32 len,u32* off,u32* flen,int* rssi,u8* ch);
#endif
