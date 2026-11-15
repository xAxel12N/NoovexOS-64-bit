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
#ifndef NVX_IWX_CMD_H
#define NVX_IWX_CMD_H
#include "include/nvx/base.h"
#define IWX_WIDE_ID(g,o) ((((u32)(g))<<8)|(u32)(o))
#define IWX_LONG_GROUP 1
#define IWX_SYSTEM_GROUP 2
#define IWX_MAC_CONF_GROUP 3
#define IWX_DATA_PATH_GROUP 5
#define IWX_REGULATORY_AND_NVM_GROUP 0xc
#define IWX_ALIVE 0x1
#define IWX_REPLY_ERROR 0x2
#define IWX_SCAN_CFG_CMD 0xc
#define IWX_SCAN_REQ_UMAC 0xd
#define IWX_SCAN_ABORT_UMAC 0xe
#define IWX_SCAN_COMPLETE_UMAC 0xf
#define IWX_SCAN_ITERATION_COMPLETE_UMAC 0xb5
#define IWX_TX_CMD 0x1c
#define IWX_POWER_TABLE_CMD 0x77
#define IWX_TX_ANT_CONFIGURATION_CMD 0x98
#define IWX_BT_CONFIG 0x9b
#define IWX_REPLY_RX_PHY_CMD 0xc0
#define IWX_REPLY_RX_MPDU_CMD 0xc1
#define IWX_MCC_UPDATE_CMD 0xc8
#define IWX_REPLY_BEACON_FILTERING_CMD 0xd2
#define IWX_SOC_CONFIGURATION_CMD 0x01
#define IWX_DQA_ENABLE_CMD 0x00
#define IWX_NVM_GET_INFO 0x02
#define IWX_RX_NO_DATA_NOTIF 0xf5
#define IWX_CMD_FAILED_MSK 0x40
#define IWX_NUM_CHANNELS 110
#define IWX_NVM_MAC_SKU_FLAGS_BAND_2_4_ENABLED (1u<<0)
#define IWX_NVM_MAC_SKU_FLAGS_BAND_5_2_ENABLED (1u<<1)
#define IWX_PHY_BAND_5 0
#define IWX_PHY_BAND_24 1
#define IWX_RX_MPDU_RES_STATUS_CRC_OK (1u<<0)
#define IWX_RX_MPDU_RES_STATUS_OVERRUN_OK (1u<<1)
#define IWX_RX_MPDU_MFLG2_PAD (1u<<5)
#define IWX_UCODE_TLV_API_SCAN_EXT_CHAN_VER 58
#define IWX_UCODE_TLV_API_REGULATORY_NVM_INFO 48
#define IWX_UCODE_TLV_CAPA_LAR_SUPPORT 1
#define IWX_UCODE_TLV_CAPA_DQA_SUPPORT 12
#define IWX_DEFAULT_QUEUE_CMD 0
struct iwx_softc;
struct __attribute__((packed)) iwx_cmd_header { u8 code,flags,idx,qid; };
struct __attribute__((packed)) iwx_rx_packet { u32 len_n_flags; struct iwx_cmd_header hdr; u8 data[]; };
struct __attribute__((packed)) iwx_nvm_get_info_rsp {
    u32 gen_flags; u16 nvm_version; u8 board_type,n_hw_addrs;
    u32 mac_sku_flags; u32 tx_chains,rx_chains;
    u32 lar_enabled,n_channels; u32 channel_profile[IWX_NUM_CHANNELS];
};
struct __attribute__((packed)) iwx_nvm_get_info_rsp_v3 {
    u32 gen_flags; u16 nvm_version; u8 board_type,n_hw_addrs;
    u32 mac_sku_flags; u32 tx_chains,rx_chains;
    u32 lar_enabled,n_channels; u16 channel_profile[51];
};
struct __attribute__((packed)) iwx_mcc_update_cmd { u16 mcc; u8 source_id,reserved; u32 key; u32 reserved2[5]; };
struct __attribute__((packed)) iwx_scan_config { u8 enable_cam_mode,enable_promiscuous_mode,bcast_sta_id,reserved; u32 tx_chains,rx_chains; };
struct __attribute__((packed)) iwx_beacon_filter_cmd { u32 v[11]; u32 lo[2],hi[2]; };
struct __attribute__((packed)) iwx_scan_general_params_v10 {
    u16 flags; u8 reserved,scan_start_mac_id; u8 active_dwell[2]; u8 adwell_default_2g,adwell_default_5g,adwell_default_social_chn,reserved1;
    u16 adwell_max_budget; u32 max_out_of_time[2]; u32 suspend_time[2]; u32 scan_priority; u8 passive_dwell[2]; u8 num_of_fragments[2];
};
struct __attribute__((packed)) iwx_scan_channel_cfg_umac { u32 flags; u8 channel_num,b1,b2,b3; };
struct __attribute__((packed)) iwx_scan_channel_params_v6 { u8 flags,count,n_aps_override[2]; struct iwx_scan_channel_cfg_umac channel_config[67]; };
struct __attribute__((packed)) iwx_scan_umac_schedule { u16 interval; u8 iter_count,reserved; };
struct __attribute__((packed)) iwx_scan_periodic_parms_v1 { struct iwx_scan_umac_schedule schedule[2]; u16 delay,reserved; };
struct __attribute__((packed)) iwx_scan_probe_segment { u16 offset,len; };
struct __attribute__((packed)) iwx_scan_probe_req { struct iwx_scan_probe_segment mac_header,band_data[3],common_data; u8 buf[512]; };
struct __attribute__((packed)) iwx_ssid_ie { u8 id,len; u8 ssid[32]; };
struct __attribute__((packed)) iwx_scan_probe_params_v4 {
    struct iwx_scan_probe_req preq; u8 short_ssid_num,bssid_num; u16 reserved;
    struct iwx_ssid_ie direct_scan[20]; u32 short_ssid[8]; u8 bssid_array[16][6];
};
struct __attribute__((packed)) iwx_scan_req_umac_v14 {
    u32 uid,ooc_priority;
    struct iwx_scan_general_params_v10 general_params; struct iwx_scan_channel_params_v6 channel_params;
    struct iwx_scan_periodic_parms_v1 periodic_params; struct iwx_scan_probe_params_v4 probe_params;
};
struct __attribute__((packed)) iwx_umac_scan_complete { u32 uid; u8 last_schedule,last_iter,status,ebs_status; u32 time_from_last_iter,reserved; };
struct __attribute__((packed)) iwx_rx_mpdu_desc_v1 { u32 rss_hash,filter_match,rate_n_flags; u8 energy_a,energy_b,channel,mac_context; u32 gp2,tsf0,tsf1; };
struct __attribute__((packed)) iwx_rx_mpdu_desc_v3 { u32 filter_match,rss_hash,partial_hash; u16 raw_xsum,reserved_xsum; u32 rate_n_flags; u8 energy_a,energy_b,channel,mac_context; u32 gp2,tsf0,tsf1,reserved[2]; };
struct __attribute__((packed)) iwx_rx_mpdu_desc {
    u16 mpdu_len; u8 mac_flags1,mac_flags2,amsdu_info; u16 phy_info; u8 mac_phy_idx; u16 raw_csum; u16 l3l4_flags; u32 status,reorder_data;
    union { struct iwx_rx_mpdu_desc_v1 v1; struct iwx_rx_mpdu_desc_v3 v3; } __attribute__((packed));
};
#define IWX_RX_DESC_SIZE_V1 (sizeof(struct iwx_rx_mpdu_desc)-sizeof(struct iwx_rx_mpdu_desc_v3)+sizeof(struct iwx_rx_mpdu_desc_v1))
struct __attribute__((packed)) iwx_dram_sec_info { u32 pn_low; u16 pn_high,aux_info; };
struct __attribute__((packed)) iwx_tx_cmd_gen2 { u16 len,offload_assist; u32 flags; struct iwx_dram_sec_info dram_info; u32 rate_n_flags; };
struct __attribute__((packed)) iwx_tx_cmd_gen3 { u16 len,flags; u32 offload_assist; struct iwx_dram_sec_info dram_info; u32 rate_n_flags; u8 reserved[8]; };
#define IWX_TX_FLAGS_CMD_RATE (1u<<0)
#define IWX_RATE_MCS_ANT_A_MSK (1u<<14)
#define IWX_RATE_MCS_CCK_MSK_V1 (1u<<9)
#define IWX_RATE_1M_PLCP 10
#define IWX_RATE_6M_PLCP 13
#define IWX_MAX_SCAN_BSS 48
struct iwx_bss { u8 bssid[6]; u8 ssid[33]; u8 ssid_len; u8 channel; int rssi; u16 capab; };
struct iwx_nvm { u8 hw_addr[6]; u16 nvm_version; u8 valid_tx_ant,valid_rx_ant,band24,band5,lar_enabled,n_channels; u8 chan[IWX_NUM_CHANNELS]; };
extern struct iwx_softc* iwx_get_sc(void);
extern int iwx_tx_hcmd(struct iwx_softc* sc,u32 id,const void* d,u32 len);
extern int iwx_tx_data(struct iwx_softc* sc,int qid,const void* txcmd,u32 tclen,const void* frame,u32 flen);
extern void iwx_rx_poll(struct iwx_softc* sc);
extern int iwx_is_ax210(struct iwx_softc* sc);
extern int iwx_fw_api(struct iwx_softc* sc,int bit);
extern int iwx_fw_capa(struct iwx_softc* sc,int bit);
extern void iwx_hw_mac(struct iwx_softc* sc,u8* mac);
extern u64 iwx_now_ms(void);
void iwx_rx_pkt(struct iwx_softc* sc,const u8* pkt,u32 len);
int iwx_send_cmd_sync(struct iwx_softc* sc,u32 id,const void* d,u32 len,void* resp,u32 resp_max,u32* resp_len,u32 timeout_ms);
int iwx_send_cmd_status(struct iwx_softc* sc,u32 id,const void* d,u32 len,u32* status);
int iwx_nvm_init(struct iwx_softc* sc);
int iwx_init_hw_cmds(struct iwx_softc* sc);
const struct iwx_nvm* iwx_nvm_info(void);
int iwx_scan_start(struct iwx_softc* sc,const u8* ssid,int ssid_len);
int iwx_scan_busy(void);
int iwx_scan_results(struct iwx_bss* out,int max);
void iwx_scan_rx_mgmt(const u8* f,u32 len,int rssi,u8 chan);
void iwx_scan_done_notif(const u8* d,u32 len);
void iwx_rx_mpdu(struct iwx_softc* sc,const u8* d,u32 len);
int iwx_tx_frame(struct iwx_softc* sc,const u8* frame,u32 len);
int iwx_join(struct iwx_softc* sc,const u8* bssid,int ch);
int iwx_join_ssid(struct iwx_softc* sc,const u8* bssid,int ch,const char* ssid);
void iwx_leave(struct iwx_softc* sc);
int iwx_set_key(struct iwx_softc* sc,int idx,int type,const u8* key,int len,int pairwise);
int iwx_glue_register(void);
extern void (*iwx_rx_deliver)(const u8* frame,u32 len,int rssi);
#endif
