/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Derived from Linux drivers/net/wireless/mediatek/mt76 (mt7925/mcu.h, mt7925/mcu.c, mt76_connac_mcu.h, mt792x.h).
 * Copyright (C) 2023 MediaTek Inc.
 * Licence text: drivers/net/wifi/mediatek/mt7921/LICENCE.BSD-3-Clause-Clear
 */
#ifndef NVX_MT7925_H
#define NVX_MT7925_H
#include "drivers/net/wifi/mediatek/mt7921/mt7921.h"
#define MT7925_RXD_SIZE 44
#define MT7925_RXD_EXTRA 8
#define MCU7925_CMD_ACK 1
#define MCU7925_UNI_QUERY_ACK 3
#define MCU7925_UNI_EXT_ACK 7
#define MCU_UNI_CMD_CHIP_CONFIG 0x0e
#define MCU_UNI_CMD_SCAN_REQ 0x16
#define MCU_UNI_EVENT_RESULT 0x01
#define MCU_UNI_EVENT_SCAN_DONE 0x0e
#define UNI_CHIP_CONFIG_NIC_CAPA 3
#define MT7925_NIC_CAP_MAC_ADDR 7
#define MT7925_NIC_CAP_6G 0x18
#define UNI_SCAN_REQ 1
#define UNI_SCAN_CANCEL 2
#define UNI_SCAN_SSID 10
#define UNI_SCAN_BSSID 11
#define UNI_SCAN_CHANNEL 12
#define UNI_SCAN_MISC 14
#define MT7925_SCAN_MAX_CH 64
#define MT7925_BAND_2G 1
#define MT7925_BAND_5G 2
#define MT7925_BAND_6G 3
#define MT7925_VCH6 178
#define MT7925_SCAN_REQ_LEN (4+20+368+16+136+12)
#define MT7925_RXD_BASE 32
#define MT7925_TX_NORMAL 0
#define MT7925_TX_ADDBA 2
#define MT7925_TXD1_FIXED_RATE (1u<<31)
#define MT7925_TXD3_BA_DISABLE (1u<<28)
#define MT7925_TXD6_DAS (1u<<2)
#define MT7925_TXD6_DIS_MAT (1u<<3)
#define MT7925_BASIC_RATES_TBL 14
#define UNI_BSS_INFO_RATE 11
#define UNI_BSS_INFO_QBSS 15
#define UNI_BSS_INFO_SEC 16
#define UNI_BSS_INFO_IFS_TIME 23
#define UNI_BSS_INFO_MLD 26
#define STA_REC_PHY 0x15
#define STA_REC_EHT 0x22
#define STA_REC_MLD_OFF 0x23
#define STA_REC_REMOVE 0x25
#define STA_REC_KEY_V3 0x27
#define STA_REC_HDR_TRANS 0x2b
#define MT7925_STA_STATE_NONE 0
#define MT7925_STA_STATE_ASSOC 2
#define MT7925_PHY_MODE_A 1
#define MT7925_PHY_MODE_B 2
#define MT7925_PHY_MODE_G 4
#define MT7925_PHY_MODE_GN 8
#define MT7925_PHY_MODE_AN 16
#define MT7925_PHY_MODE_AC 32
#define MT7925_PHY_MODE_AX_6G 1
#define MT7925_PHY_TYPE_ERP_INDEX 1
#define MT7925_PHY_TYPE_OFDM_INDEX 3
#define MT7925_MODE_CCK 1
#define MT7925_MODE_OFDM 2
#define MT7925_MODE_EHT 32
#define MT7925_MODE_WPA2_PSK 7
#define MT7925_CIPHER_AES_CCMP 4
#define MT7925_CIPHER_BIP_CMAC_128 6
#define MCU_UNI_CMD_SET_POWER_LIMIT 0x2c
#define MCU_UNI_CMD_EFUSE_CTRL 0x2d
#define MT7925_EE_HW_TYPE 0xa71u
#define MT7925_FW_TYPE_CLC 2
#define MT7925_CLC_NREC 2
#define MT7925_CLC_MAXIDX 4
#define MT7925_CLC_REQ 84u
#define MT7925_ENV_ANY 0
#define MT7925_ENV_INDOOR 1
#define MT7925_RAM_PATH "/lib/firmware/mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin"
struct mt7925_eht { u8 ok,nmcs; u16 mac_cap; u8 phy[9]; u8 mcs[9]; };
extern struct mt7925_eht mt7925_peer_eht;
extern int mt7925_eht_on, mt7925_eht_fail;
void mt7925_assoc_ies(const u8* ies,int n);
extern int mt7925_eht_adv;
int mt7925_eht_ap(const u8* bssid);
void mt7925_eht_note(const u8* f,u32 len);
int mt7925_assoc_eht(u8* o,int cap);
u32 mt7925_txd_build(struct mt7921_softc* sc,const u8* f,u32 len,u8* out);
int mt7925_rxd_parse(const u8* buf,u32 len,u32* off,u32* flen,int* rssi,u8* ch);
u32 mt7925_bss_build(struct mt7921_softc* sc,const u8* bssid,u8 ch,int en,u8* out);
u32 mt7925_sta_build(struct mt7921_softc* sc,u8 wcid,const u8* peer,u8 state,u16 aid,u8* out);
u32 mt7925_key_build(struct mt7921_softc* sc,u8 wcid,int idx,int type,const u8* key,int len,u8* out);
int mt7925_eht_parse(const u8* ies,int n,struct mt7925_eht* e);
u32 mt7925_sta_eht_tlv(const struct mt7925_eht* e,u8* out);
void mt7925_mac_attach(struct mt7921_softc* sc);
extern struct mt7921_softc g_mt7925;
extern const struct mt_pci_id mt7925_ids[];
extern int mt7925_has_6g;
const struct mt_pci_id* mt7925_match(u16 vendor,u16 device);
u32 mt7925_mcu_frame(struct mt7921_softc* sc,u32 cmd,const void* data,u32 len,u8* out,u32 cap,u8* seqo);
int mt7925_mcu_send(struct mt7921_softc* sc,u32 cmd,const void* data,u32 len,int wait);
const u8* mt7925_rx_fix(struct mt7921_softc* sc,const u8* buf,u32* len);
int mt7925_nic_capab_parse(struct mt7921_softc* sc,const u8* p,int n);
int mt7925_mcu_init(struct mt7921_softc* sc);
u32 mt7925_scan_req_build(struct mt7921_softc* sc,u8 seq,const u8* ch,const u8* band,int n,u8* out,u32 cap);
u32 mt7925_scan_cancel_build(struct mt7921_softc* sc,u8 seq,u8* out);
int mt7925_hw_scan(struct mt7921_softc* sc,const u8* ch,const u8* band,int n);
int mt7925_scan_busy(struct mt7921_softc* sc);
int mt7925_connect(struct mt7921_softc* sc,const u8* bssid,u8 ch,u8 band,const char* ssid,u8 akm,u8 ht);
int mt7925_probe(void);
int mt7925_start(void);
void mt7925_status(char* out,int cap);
int mt7925_present(void);
int mt7925_scan_list(const u8* ch,int n,u8* oc,u8* ob,int cap,int six);
extern int mt7925_6g_on;
int mt7925_clc_find(const u8* fw,u32 n,u32* off,u32* len);
int mt7925_clc_split(const u8* b,u32 len,u8 encap,const u8** rec,u32* rl);
int mt7925_clc_countries(const u8* rec,u32 rl,char* out,int cap);
u32 mt7925_clc_req(const u8* rec,u32 rl,u8 idx,const char* cc,u8 env,int nth,u8* out,u32 cap,u8* flag);
int mt7925_clc_apply(struct mt7921_softc* sc,const char* cc);
int mt7925_clc_load(struct mt7921_softc* sc);
int mt7925_clc_list(char* out,int cap);
void mt7925_clc_sync(struct mt7921_softc* sc,const char* cc);
void mt7925_clc_reset(void);
extern int mt7925_clc_on, mt7925_clc_fail, mt7925_clc_sent; extern u8 mt7925_clc_chan_conf; extern char mt7925_clc_cc[3]; extern const char* mt7925_clc_err;
void mt7925_wifi_hw_probe(void);
extern void (*mt7925_fw_lost)(struct mt7921_softc* sc);
extern u32 mt7925_fw_restarts;
#endif
