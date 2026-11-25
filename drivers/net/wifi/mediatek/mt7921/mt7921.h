/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Derived from Linux drivers/net/wireless/mediatek/mt76 (mt76_connac_mcu.h, mt76_connac2_mac.h, mt792x.h, mt7921/).
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2020 MediaTek Inc.
 * Licence text: drivers/net/wifi/mediatek/mt7921/LICENCE.BSD-3-Clause-Clear
 */
#ifndef NVX_MT7921_H
#define NVX_MT7921_H
#include "include/nvx/base.h"
#define MT_ST_NONE 0
#define MT_ST_FOUND 1
#define MT_ST_NOFW 2
#define MT_ST_HWFAIL 3
#define MT_ST_FWOK 4
#define MT_ST_READY 5
#define MT_CHIP_7961 0x7961
#define MT_CHIP_7920 0x7920
#define MT_CHIP_7922 0x7922
#define MT_CHIP_7925 0x7925
#define MT_TX_RING 128
#define MT_MCU_RING 128
#define MT_FWDL_RING 128
#define MT_RX_RING 128
#define MT_RX_MCU_RING 128
#define MT_RXBUF_SIZE 2048
#define MT_TXBUF_SIZE 2048
#define MT_TXP_SIZE 32
#define MT_FWDL_CHUNK 0x1000
#define MT_Q_TX 0
#define MT_Q_MCU 1
#define MT_Q_FWDL 2
#define MT_Q_RX_DATA 3
#define MT_Q_RX_MCU 4
#define MT_NQ 5
#define MT_TXD_SIZE 32
#define MT_SDIO_HDR 0
#define MCU_PKT_ID 0xa0
#define MCU_PQ_ID(p,q) (((p)<<15)|((q)<<10))
#define MCU_S2D_H2N 0
#define MCU_FIELD_QUERY (1u<<16)
#define MCU_FIELD_UNI (1u<<17)
#define MCU_FIELD_CE (1u<<18)
#define MCU_CMD(t) ((u32)(t)&0xffu)
#define MCU_UNI_CMD(t) (MCU_FIELD_UNI|((u32)(t)&0xffu))
#define MCU_CE_CMD(t) (MCU_FIELD_CE|((u32)(t)&0xffu))
#define MCU_CE_QUERY(t) (MCU_CE_CMD(t)|MCU_FIELD_QUERY)
#define MCU_CMD_TARGET_ADDRESS_LEN_REQ 0x01
#define MCU_CMD_FW_START_REQ 0x02
#define MCU_CMD_INIT_ACCESS_REG 0x03
#define MCU_CMD_NIC_POWER_CTRL 0x04
#define MCU_CMD_PATCH_START_REQ 0x05
#define MCU_CMD_PATCH_FINISH_REQ 0x07
#define MCU_CMD_PATCH_SEM_CONTROL 0x10
#define MCU_CMD_EXT_CID 0xed
#define MCU_CMD_FW_SCATTER 0xee
#define MCU_CMD_RESTART_DL_REQ 0xef
#define MCU_CE_CMD_START_HW_SCAN 0x03
#define MCU_CE_CMD_SET_PS_PROFILE 0x05
#define MCU_CE_CMD_SET_RX_FILTER 0x0a
#define MCU_CE_CMD_SET_CHAN_DOMAIN 0x0f
#define MCU_CE_CMD_SET_BSS_ABORT 0x17
#define MCU_CE_CMD_CANCEL_HW_SCAN 0x1b
#define MCU_CE_CMD_SET_CLC 0x5c
#define MCU_CE_CMD_GET_NIC_CAPAB 0x8a
#define MCU_CE_CMD_REG_WRITE 0xc0
#define MCU_CE_CMD_FWLOG_2_HOST 0xc5
#define MCU_CE_CMD_CHIP_CONFIG 0xca
#define MCU_UNI_CMD_DEV_INFO_UPDATE 0x01
#define MCU_UNI_CMD_BSS_INFO_UPDATE 0x02
#define MCU_UNI_CMD_STA_REC_UPDATE 0x03
#define MCU_UNI_CMD_SUSPEND 0x05
#define MCU_UNI_CMD_OFFLOAD 0x06
#define MCU_UNI_CMD_HIF_CTRL 0x07
#define MCU_EVENT_GENERIC 0x01
#define MCU_EVENT_MT_PATCH_SEM 0x04
#define MCU_EVENT_LP_INFO 0x07
#define MCU_EVENT_SCAN_DONE 0x0d
#define MCU_EVENT_TX_DONE 0x0f
#define MCU_EVENT_BSS_BEACON_LOSS 0x13
#define MCU_EVENT_SCHED_SCAN_DONE 0x23
#define MCU_EVENT_DBG_MSG 0x27
#define MCU_EVENT_EXT 0xed
#define MCU_EVENT_COREDUMP 0xf0
#define PATCH_SEM_GET 1
#define PATCH_SEM_REL 0
enum { PATCH_NOT_DL_SEM_FAIL, PATCH_IS_DL, PATCH_NOT_DL_SEM_SUCCESS, PATCH_REL_SEM_SUCCESS };
#define PATCH_SEC_TYPE_MASK 0xffffu
#define PATCH_SEC_TYPE_INFO 0x2
#define FW_FEATURE_SET_ENCRYPT (1u<<0)
#define FW_FEATURE_SET_KEY_IDX (3u<<1)
#define FW_FEATURE_ENCRY_MODE (1u<<4)
#define FW_FEATURE_OVERRIDE_ADDR (1u<<5)
#define FW_FEATURE_NON_DL (1u<<6)
#define DL_MODE_ENCRYPT (1u<<0)
#define DL_MODE_KEY_IDX (3u<<1)
#define DL_MODE_RESET_SEC_IV (1u<<3)
#define DL_MODE_NEED_RSP (1u<<31)
#define FW_START_OVERRIDE (1u<<0)
struct __attribute__((packed)) mt_mcu_txd { u32 txd[8]; u16 len,pq_id; u8 cid,pkt_type,set_query,seq,uc_d2b0_rev,ext_cid,s2d_index,ext_cid_ack; u32 rsv[5]; };
struct __attribute__((packed)) mt_mcu_uni_txd { u32 txd[8]; u16 len,cid; u8 rsv,pkt_type,frag_n,seq; u16 checksum; u8 s2d_index,option; u8 rsv1[4]; };
struct __attribute__((packed)) mt_mcu_rxd { u32 rxd[6]; u16 len,pkt_type_id; u8 eid,seq,option,rsv,ext_eid,rsv1[2],s2d_index; };
struct __attribute__((packed)) mt_patch_hdr { char build_date[16]; char platform[4]; u32 hw_sw_ver,patch_ver; u16 checksum,rsv; struct __attribute__((packed)) { u32 patch_ver,subsys,feature,n_region,crc,rsv[11]; } desc; };
struct __attribute__((packed)) mt_patch_sec { u32 type,offs,size; union { u32 spec[13]; struct __attribute__((packed)) { u32 addr,len,sec_key_idx,align_len,rsv[9]; } info; }; };
struct __attribute__((packed)) mt_fw_trailer { u8 chip_id,eco_code,n_region,format_ver,format_flag,rsv[2]; char fw_ver[10]; char build_date[15]; u32 crc; };
struct __attribute__((packed)) mt_fw_region { u32 decomp_crc,decomp_len,decomp_blk_sz; u8 rsv[4]; u32 addr,len; u8 feature_set,type,rsv1[14]; };
struct __attribute__((packed)) mt_wfdma_desc { u32 buf0,ctrl,buf1,info; };
_Static_assert(sizeof(struct mt_mcu_txd)==64&&sizeof(struct mt_mcu_uni_txd)==48&&sizeof(struct mt_mcu_rxd)==36,"mt mcu descs");
_Static_assert(sizeof(struct mt_patch_hdr)==96&&sizeof(struct mt_patch_sec)==64,"mt patch");
_Static_assert(sizeof(struct mt_fw_trailer)==36&&sizeof(struct mt_fw_region)==40&&sizeof(struct mt_wfdma_desc)==16,"mt fw");
static inline u32 mt_be32(u32 v){ return (v>>24)|((v>>8)&0xff00u)|((v<<8)&0xff0000u)|(v<<24); }
struct mt_dma { u8* va; u64 pa; u32 sz; };
struct mt_ring { struct mt_dma desc,buf; int n,head,tail,hw_idx; u32 bufsz,regbase; };
struct mt_pci_id { u16 vendor,device,chip; const char* name; const char* patch; const char* ram; };
struct mt7921_softc;
struct mt_bus_ops { u32 (*rr)(struct mt7921_softc* sc,u32 o); void (*wr)(struct mt7921_softc* sc,u32 o,u32 v); int (*tx)(struct mt7921_softc* sc,int qid,const u8* buf,u32 len);
    void (*rx_poll)(struct mt7921_softc* sc); int (*tx_idle)(struct mt7921_softc* sc,int qid,u32 ms); int (*fw_pre)(struct mt7921_softc* sc); void (*fw_post)(struct mt7921_softc* sc); };
struct mt_scan_res { u8 bssid[6],ch,akm; int rssi; char ssid[33]; };
#define MT_MAX_SCAN 32
struct mt7921_softc {
    int bus,slot,fn,state;
    u16 vendor,device,chip; u32 mmio,hw_rev,fw_flags,last_int;
    const struct mt_pci_id* id; const char* err;
    char fwver[16],patchver[20],builddate[20];
    struct mt_ring q[MT_NQ];
    u8 mcu_seq; u8 mac[6]; u8 bssid[6]; u8 chan; int rssi,connected,scanning,scan_done,nscan;
    u8 resp[2048]; int resp_len,resp_seq,resp_ok;
    u32 tx_pkts,rx_pkts,mcu_cmds,mcu_events,mcu_timeouts;
    u8 omac_idx,bss_idx,wlan_idx,band_idx;
    u8 roc_token,roc_active,roc_granted,roc_fails; u32 roc_grants,roc_timeouts;
    u32 txfree_events,tok_freed,tok_forced,tok_stale,rx_reenter;
    u8 gen,band6,rx_ch; int (*mcu_send)(struct mt7921_softc* sc,u32 cmd,const void* data,u32 len,int wait); const u8* (*rx_fix)(struct mt7921_softc* sc,const u8* buf,u32* len);
    u32 (*txd_build)(struct mt7921_softc* sc,const u8* f,u32 len,u8* out); int (*rxd_parse)(const u8* buf,u32 len,u32* off,u32* flen,int* rssi,u8* ch);
    u32 (*bss_build)(struct mt7921_softc* sc,const u8* bssid,u8 ch,int en,u8* out); u32 (*sta_build)(struct mt7921_softc* sc,u8 wcid,const u8* peer,u8 state,u16 aid,u8* out);
    u32 (*key_build)(struct mt7921_softc* sc,u8 wcid,int idx,int type,const u8* key,int len,u8* out);
    const struct mt_bus_ops* hif; void* hif_priv;
};
extern struct mt7921_softc g_mt7921;
#define MT7921_VCH6 178
extern int mt7921_has_6g,mt7921_6g_on,mt_assoc_eht_res;
int mt7921_scan_list(const u8* ch,int n,u8* out,int cap,int six);
extern const struct mt_pci_id mt7921_ids[];
extern int (*mt7921_firmware_read)(const char* path,u8** out,u32* len);
const struct mt_pci_id* mt7921_match(u16 vendor,u16 device);
int mt7921_probe(void);
int mt7921_start(void);
void mt7921_status(char* out,int cap);
void mt7921_up(char* out,int cap);
void mt7921_wifi_hw_probe(void);
u32 mt7921_r32(struct mt7921_softc* sc,u32 o);
void mt7921_w32(struct mt7921_softc* sc,u32 o,u32 v);
u32 mt7921_rmw(struct mt7921_softc* sc,u32 o,u32 clr,u32 set);
int mt7921_poll32(struct mt7921_softc* sc,u32 o,u32 mask,u32 val,u32 ms);
int mt7921_pci_init(struct mt7921_softc* sc);
int mt7921_dma_init(struct mt7921_softc* sc);
void mt7921_dma_stop(struct mt7921_softc* sc);
int mt7921_dma_tx(struct mt7921_softc* sc,int qid,const u8* buf,u32 len);
void mt7921_rx_poll(struct mt7921_softc* sc);
int mt7921_fw_load(struct mt7921_softc* sc);
int mt7921_mcu_send(struct mt7921_softc* sc,u32 cmd,const void* data,u32 len,int wait);
int mt7921_mcu_send_fwdl(struct mt7921_softc* sc,const u8* data,u32 len);
void mt7921_mcu_rx_event(struct mt7921_softc* sc,const u8* buf,u32 len);
int mt7921_mcu_init(struct mt7921_softc* sc);
int mt7921_hw_scan(struct mt7921_softc* sc,const u8* ch,int n);
int mt7921_scan_busy(struct mt7921_softc* sc);
int mt7921_connect(struct mt7921_softc* sc,const u8* bssid,u8 ch,const char* ssid,u8 akm,u8 ht);
void mt7921_disconnect(struct mt7921_softc* sc);
int mt7921_set_key(struct mt7921_softc* sc,int idx,int type,const u8* key,int len,int pairwise);
int mt7921_tx_data(struct mt7921_softc* sc,const u8* frame,u32 len);
void mt7921_rx_data(struct mt7921_softc* sc,const u8* buf,u32 len);
void mt_ba_drain(struct mt7921_softc* sc);
extern void (*mt7921_rx_deliver)(const u8* frame,u32 len,int rssi);
extern int mt7921_roc_enable,mt7921_tok_enable;
int mt7921_roc_acquire(struct mt7921_softc* sc,u8 ch,u32 ms,u8 type);
void mt7921_roc_abort(struct mt7921_softc* sc);
void mt7921_roc_event(struct mt7921_softc* sc,const u8* p,u32 n);
void mt7921_tx_free_event(struct mt7921_softc* sc,const u8* buf,u32 len);
int mt7921_tx_token_release(struct mt7921_softc* sc,u32 id);
int mt7921_tokens_busy(struct mt7921_softc* sc);
#endif
