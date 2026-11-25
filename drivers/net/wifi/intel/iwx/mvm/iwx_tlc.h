/*
 * Derived from OpenBSD sys/dev/pci/if_iwx.c, if_iwxreg.h (rev 1.230 / 1.75).
 * Copyright (c) 2014, 2016 genua gmbh <info@genua.de>
 *   Author: Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2014 Fixup Software Ltd.
 * Copyright (c) 2017, 2019, 2020 Stefan Sperling <stsp@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Portions: Copyright(c) 2017 Intel Deutschland GmbH, Copyright(c) 2018 - 2019
 * Intel Corporation, used under the BSD-3-Clause branch of the dual BSD/GPLv2
 * licence: Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. Neither the name Intel Corporation nor the names of
 * its contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission. THIS SOFTWARE IS PROVIDED
 * BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DAMAGES ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef NVX_IWX_TLC_H
#define NVX_IWX_TLC_H
#include "drivers/net/wifi/intel/iwx/iwx_cmd.h"
#define IWX_TLC_MNG_CONFIG_CMD 0x0f
#define IWX_TLC_MNG_UPDATE_NOTIF 0xf7
#define IWX_TLC_MODE_NON_HT 0
#define IWX_TLC_MODE_HT 1
#define IWX_TLC_MODE_VHT 2
#define IWX_TLC_CFG_STBC (1u<<0)
#define IWX_TLC_NOTIF_FLAG_RATE (1u<<0)
#define IWX_MAC_QOS_FLG_TGN (1u<<1)
#define IWX_MAC_PROT_FLG_HT_PROT (1u<<23)
#define IWX_MAC_PROT_FLG_FAT_PROT (1u<<24)
#define IWX_TX_FLAGS_ENCRYPT_DIS (1u<<1)
struct iwx_peer { u16 rates,htcap,vht_rxmcs; u8 ht,vht,he,wmm,hop,vop,ampdu,htop0,htop1,vw,vc0,vc1; u8 mcs[4]; u32 vhtcap; };
struct iwx_link { u8 ht,vht,mimo,width,ctrl_pos,chains; u32 sta_flags,mac_prot,mac_qos; };
struct __attribute__((packed)) iwx_tlc_config_cmd_v3 { u8 sta_id,reserved1[3]; u8 max_ch_width,mode,chains,amsdu; u16 flags,non_ht_rates; u16 ht_rates[2][2]; u16 max_mpdu_len; u8 sgi_ch_width_supp,reserved2; u32 max_tx_op; };
struct __attribute__((packed)) iwx_tlc_config_cmd_v4 { u8 sta_id,reserved1[3]; u8 max_ch_width,mode,chains,sgi_ch_width_supp; u16 flags,non_ht_rates; u16 ht_rates[2][3]; u16 max_mpdu_len,max_tx_op; };
struct __attribute__((packed)) iwx_tlc_update_notif { u8 sta_id,reserved[3]; u32 flags,rate,amsdu_size,amsdu_enabled; };
void iwx_peer_parse(const u8* ie,u32 len,struct iwx_peer* p);
void iwx_peer_merge(struct iwx_peer* a,const struct iwx_peer* scan,int ht_sent,int ch);
int iwx_mimo_ok(u8 txant,u8 rxant);
u32 iwx_assoc_ht_ies(u8* out,const struct iwx_peer* p,int ch,u8 txant,u8 rxant);
void iwx_link_pick(const struct iwx_peer* p,int ch,u8 txant,u8 rxant,struct iwx_link* l);
u32 iwx_tlc_build(const struct iwx_peer* p,const struct iwx_link* l,u8 txant,int ver,void* out);
void iwx_tlc_notif(const u8* d,u32 len);
int iwx_scan_peer(const u8* bssid,struct iwx_peer* out);
int iwx_tlc_send(struct iwx_softc* sc,const struct iwx_peer* p,const struct iwx_link* l);
extern volatile int iwx_tlc_on;
extern volatile u32 iwx_tlc_rate;
extern struct iwx_peer iwx_apeer;
extern volatile int iwx_apeer_ok;
#endif
