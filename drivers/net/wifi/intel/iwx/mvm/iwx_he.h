/* SPDX-License-Identifier: GPL-3.0-or-later. Firmware API layouts from Linux iwlwifi fw/api (GPL-2.0 OR BSD-3-Clause, Copyright (C) 2012-2014, 2018-2026 Intel Corporation), used under BSD-3-Clause; see NOTICE. */
#ifndef NVX_IWX_HE_H
#define NVX_IWX_HE_H
#include "drivers/net/wifi/intel/iwx/mvm/iwx_tlc.h"
#define IWX_STA_HE_CTXT_CMD 0x07
#define IWX_TLC_MODE_HE 3
#define IWX_TLC_CFG_LDPC (1u<<1)
#define IWX_MAC_FILTER_IN_11AX (1u<<14)
#define IWX_NVM_SKU_11AX (1u<<4)
#define IWX_NVM_CH_160 (1u<<11)
#define IWX_HE_PKT_EXT_NONE 7
#define IWX_STA_CTXT_HE_BSS_COLOR_DIS (1u<<5)
#define IWX_STA_CTXT_HE_NIC_NOT_ACK_ENABLED (1u<<13)
#define IWX_STA_MODIFY_ADD_BA_TID (1u<<3)
#define IWX_STA_MODIFY_REMOVE_BA_TID (1u<<4)
struct iwx_hepeer { u8 he,ldpc,c160,color,cdis; u16 mcs80,mcs160,rts; };
void iwx_he_parse(const u8* ie,u32 len,struct iwx_hepeer* h);
void iwx_he_scan_note(const u8* bssid,const u8* ie,u32 len);
int iwx_he_scan_peer(const u8* bssid,struct iwx_hepeer* h);
void iwx_he_assoc_note(const u8* ie,u32 len);
void iwx_he_nvm_note(u32 sku,int ch,u32 flags);
int iwx_he_ch160(int ch);
u32 iwx_he_cap_ie(u8* f,int ch,int mimo,int c160);
u32 iwx_he_insert(u8* body,u32 n,u32 ht_len,u32 cap,int ch,int mimo,int c160);
int iwx_he_link160(const struct iwx_hepeer* h,const struct iwx_peer* p,int ch,struct iwx_link* l);
u16 iwx_he_rates(u16 apmap,int nss);
u32 iwx_he_tlc_build(const struct iwx_peer* p,const struct iwx_hepeer* h,const struct iwx_link* l,u8 txant,int ver,void* out);
u32 iwx_he_ctxt_build(const struct iwx_hepeer* h,int ver,u8* out);
int iwx_he_allowed(struct iwx_softc* sc);
int iwx_he_prepare(struct iwx_softc* sc,const u8* bssid,int ch,int htok,struct iwx_hepeer* h);
int iwx_he_assoc_done(struct iwx_softc* sc,struct iwx_hepeer* h,int sent,int ch,const struct iwx_peer* ap,struct iwx_link* l);
int iwx_he_after_assoc(struct iwx_softc* sc,const struct iwx_peer* ap,const struct iwx_link* l);
void iwx_he_reset(void);
int iwx_ba_rx(int tid,u16 ssn,u16 win,int start);
#define IWX_BATX_WIN 16
#define IWX_BATX_INFLIGHT 28
int iwx_ba_tx(int tid,u16 ssn,u16 win,int start);
int iwx_batx_route(struct iwx_softc* sc,const u8* f,u32 len);
void iwx_batx_notif(const u8* d,u32 len);
void iwx_batx_teardown(struct iwx_softc* sc);
extern volatile int iwx_batx_on,iwx_batx_q;
extern volatile u32 iwx_batx_full,iwx_batx_frames,iwx_batx_notifs;
extern volatile int iwx_he_on,iwx_he_fail,iwx_he_up,iwx_ba_on;
extern struct iwx_hepeer iwx_he_cur;
#endif
