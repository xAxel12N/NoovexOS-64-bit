/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Derived from Linux drivers/net/wireless/mediatek/mt76 (mt76_connac_mcu.h).
 * Copyright (C) 2020 MediaTek Inc.
 * Licence text: drivers/net/wifi/mediatek/mt7921/LICENCE.BSD-3-Clause-Clear
 */
#ifndef NVX_MT7921_RATE_H
#define NVX_MT7921_RATE_H
#include "drivers/net/wifi/mediatek/mt7921/mt7921.h"
#define STA_REC_RA 0x01
#define STA_REC_BA 0x06
#define STA_REC_HT 0x09
#define STA_REC_VHT 0x0a
#define STA_REC_WTBL 0x0d
#define STA_REC_HE 0x0e
#define STA_REC_PHY 0x15
#define STA_REC_HE_V2 0x19
#define WTBL_SET 2
#define WTBL_BA 8
#define MT_BA_TYPE_ORIGINATOR 1
#define MT_BA_TYPE_RECIPIENT 2
#define RST_BA_MAC_TID_MATCH 0
#define MT_PHY_HR_DSSS 0x01
#define MT_PHY_ERP 0x02
#define MT_PHY_OFDM 0x08
#define MT_PHY_HT 0x10
#define MT_PHY_VHT 0x20
#define MT_PHY_HE 0x40
#define MT_STA_INFO_ASSOC 2
#define MT_STA_RATE_MAX 160
struct mt_peer_caps { u8 ok,ht,vht,he,bw,nss,ampdu; u16 legacy,basic,ht_cap; u8 mcs[10]; u32 vht_cap; u16 vht_rx,vht_tx; u8 he_mac[6],he_phy[11]; u16 he_mcs[3]; };
struct mt_ba_st { u8 tx_st,tx_fail,tok,rx_on,rx_tok; u16 tx_ssn,rx_ssn; u64 tx_t; };
extern int mt_ra_on,mt_caps_on,mt_he_on,mt_ba_on,mt_batx_on,mt_caps_fail,mt_ra_fail,mt_ba_fail,mt_ba_port;
extern int mt_ba_in_rx;
extern u32 mt_ba_deferred;
extern struct mt_peer_caps mt_peer;
extern struct mt_ba_st mt_ba[8];
extern u16 mt_tx_sn[8];
extern u32 mt_ba_rx_sess,mt_ba_tx_sess,mt_ba_tx_req;
int mt_peer_parse(const u8* ie,int n,u8 ch,struct mt_peer_caps* c);
u32 mt_sta_rate_build(struct mt7921_softc* sc,u8 wcid,const u8* peer,u16 aid,const struct mt_peer_caps* c,int rssi,u8* out,u32 cap);
int mt_sta_rate_send(struct mt7921_softc* sc,u16 aid);
int mt_assoc_caps(struct mt7921_softc* sc,u8 ch,u8* o);
u32 mt_ba_wtbl_build(struct mt7921_softc* sc,int tid,u16 ssn,u16 win,int en,int tx,u8* out);
u32 mt_ba_sta_build(struct mt7921_softc* sc,int tid,u16 ssn,u16 win,int en,int tx,int amsdu,u8* out);
int mt7921_ba(struct mt7921_softc* sc,int tid,u16 ssn,u16 win,int en,int tx,int amsdu);
int mt_ba_tx_start(struct mt7921_softc* sc,int tid);
void mt_ba_tx_stop(struct mt7921_softc* sc,int tid,int send);
void mt_ba_reset(struct mt7921_softc* sc,int send);
void mt_ba_rx_action(struct mt7921_softc* sc,const u8* f,u32 fl);
int mt_ba_tx_hook(struct mt7921_softc* sc,const u8* f,u32 len,u32* d3);
int mt_ba_pending(void);
void mt_rate_cmd(const char* arg,char* out,int cap);
#endif
