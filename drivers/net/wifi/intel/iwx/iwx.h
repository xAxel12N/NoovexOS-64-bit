/* Derived from OpenBSD sys/dev/pci/if_iwx.c, if_iwxreg.h (rev 1.230 / 1.75). SPDX-License-Identifier: ISC AND BSD-3-Clause */
/*
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
 */

/*-
 * Based on BSD-licensed source modules in the Linux iwlwifi driver,
 * which were used as the reference documentation for this implementation.
 *
 ******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************
 */

/*-
 * Copyright (c) 2007-2010 Damien Bergamini <damien.bergamini@free.fr>
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
 */
#ifndef NVX_IWX_H
#define NVX_IWX_H
#include "include/nvx/base.h"
#include "drivers/net/wifi/intel/iwx/iwx_cmd.h"
#define IWX_FAM_22000 0
#define IWX_FAM_AX210 1
#define IWX_FAM_BZ 2
#define IWX_CSR_HW_IF_CONFIG_REG 0x000
#define IWX_CSR_INT_COALESCING 0x004
#define IWX_CSR_INT 0x008
#define IWX_CSR_INT_MASK 0x00c
#define IWX_CSR_FH_INT_STATUS 0x010
#define IWX_CSR_RESET 0x020
#define IWX_CSR_GP_CNTRL 0x024
#define IWX_CSR_HW_REV 0x028
#define IWX_CSR_FUNC_SCRATCH 0x02c
#define IWX_CSR_GIO_REG 0x03C
#define IWX_CSR_UCODE_DRV_GP1_CLR 0x05c
#define IWX_CSR_MBOX_SET_REG 0x088
#define IWX_CSR_HW_RF_ID 0x09c
#define IWX_CSR_MAC_SHADOW_REG_CTRL 0x0A8
#define IWX_CSR_GIO_CHICKEN_BITS 0x100
#define IWX_CSR_CTXT_INFO_BA 0x40
#define IWX_CSR_CTXT_INFO_ADDR 0x118
#define IWX_CSR_IML_DATA_ADDR 0x120
#define IWX_CSR_IML_SIZE_ADDR 0x128
#define IWX_CSR_DBG_HPET_MEM_REG 0x240
#define IWX_CSR_DBG_LINK_PWR_MGMT_REG 0x250
#define IWX_CSR_MSIX_FH_INT_CAUSES_AD 0x2800
#define IWX_CSR_MSIX_FH_INT_MASK_AD 0x2804
#define IWX_CSR_MSIX_HW_INT_CAUSES_AD 0x2808
#define IWX_CSR_MSIX_HW_INT_MASK_AD 0x280C
#define IWX_HBUS_TARG_MEM_RADDR 0x40c
#define IWX_HBUS_TARG_MEM_RDAT 0x41c
#define IWX_HBUS_TARG_PRPH_WADDR 0x444
#define IWX_HBUS_TARG_PRPH_RADDR 0x448
#define IWX_HBUS_TARG_PRPH_WDAT 0x44c
#define IWX_HBUS_TARG_PRPH_RDAT 0x450
#define IWX_HBUS_TARG_WRPTR 0x460
#define IWX_RFH_Q0_FRBDCB_WIDX_TRG 0x1C80
#define IWX_UREG_CHICK 0xa05c00
#define IWX_UREG_CHICK_MSI_ENABLE (1u<<24)
#define IWX_UREG_CPU_INIT_RUN 0xa05c44
#define IWX_HPM_DEBUG 0xa03440
#define IWX_PREG_PRPH_WPROT_22000 0xa04d00
#define IWX_GP_MAC_CLOCK_READY 0x00000001u
#define IWX_GP_INIT_DONE 0x00000004u
#define IWX_GP_MAC_ACCESS_REQ 0x00000008u
#define IWX_GP_GOING_TO_SLEEP 0x00000010u
#define IWX_GP_MAC_INIT (1u<<6)
#define IWX_GP_ROM_START (1u<<7)
#define IWX_GP_MAC_STATUS (1u<<20)
#define IWX_GP_BZ_MAC_ACCESS_REQ (1u<<21)
#define IWX_GP_HW_RF_KILL_SW 0x08000000u
#define IWX_GP_SW_RESET (1u<<31)
#define IWX_INT_ALIVE (1u<<0)
#define IWX_INT_SW_ERR (1u<<25)
#define IWX_INT_HW_ERR (1u<<29)
#define IWX_INT_FH_RX (1u<<31)
#define IWX_MAC_QU 0x33
#define IWX_MAC_CC 0x34
#define IWX_MAC_QUZ 0x35
#define IWX_MAC_SO 0x37
#define IWX_MAC_TY 0x42
#define IWX_MAC_SOF 0x43
#define IWX_MAC_MA 0x44
#define IWX_MAC_BZ 0x46
#define IWX_RF_JF2 0x105
#define IWX_RF_JF1 0x108
#define IWX_RF_HR2 0x10A
#define IWX_RF_HR1 0x10C
#define IWX_RF_GF 0x10D
#define IWX_ALIVE_STATUS_OK 0xCAFE
#define IWX_TLV_UCODE_MAGIC 0x0a4c5749u
#define IWX_UCODE_TLV_SEC_RT 19
#define IWX_UCODE_TLV_API_CHANGES_SET 29
#define IWX_UCODE_TLV_ENABLED_CAPABILITIES 30
#define IWX_UCODE_TLV_N_SCAN_CHANNELS 31
#define IWX_UCODE_TLV_FW_VERSION 36
#define IWX_UCODE_TLV_CMD_VERSIONS 48
#define IWX_UCODE_TLV_IML 52
#define IWX_UCODE_TLV_PHY_SKU 23
#define IWX_UCODE_TLV_PNVM_DATA 74
#define IWX_CPU1_CPU2_SEPARATOR_SECTION 0xFFFFCCCCu
#define IWX_PAGING_SEPARATOR_SECTION 0xAAAABBBBu
#define IWX_UCODE_SECT_MAX 69
#define IWX_MAX_DRAM_ENTRY 64
#define IWX_RX_RING 512
#define IWX_NRXBUF 64
#define IWX_RBUF_SIZE 4096
#define IWX_TX_RING 256
#define IWX_NCMDBUF 32
#define IWX_CMDBUF_SIZE 4096
#define IWX_FIRST_TB_SIZE 20
#define IWX_TFH_NUM_TBS 25
#define IWX_NTXQ 3
#define IWX_CMD_QID 0
#define IWX_DATA_QID 1
#define IWX_ST_NONE 0
#define IWX_ST_FOUND 1
#define IWX_ST_NOFW 2
#define IWX_ST_HWFAIL 3
#define IWX_ST_ALIVE 4
#define IWX_ST_READY 5
struct __attribute__((packed)) iwx_ctxt_dram { u64 umac_img[IWX_MAX_DRAM_ENTRY],lmac_img[IWX_MAX_DRAM_ENTRY],virtual_img[IWX_MAX_DRAM_ENTRY]; };
struct __attribute__((packed)) iwx_ctxt_info {
    u16 mac_id,version,size,rsv0; u32 control_flags,rsv1; u64 reserved0;
    u64 free_rbd_addr,used_rbd_addr,status_wr_ptr;
    u64 cmd_queue_addr; u8 cmd_queue_size,rsv2[7]; u32 reserved1[4];
    u64 core_dump_addr; u32 core_dump_size,rsv3; u64 early_debug_addr; u32 early_debug_size,rsv4; u64 platform_nvm_addr; u32 platform_nvm_size,rsv5;
    u32 reserved2[16]; struct iwx_ctxt_dram dram; u32 reserved3[16];
};
struct __attribute__((packed)) iwx_prph_scratch {
    u16 mac_id,version,size,rsv0; u32 control_flags,rsv1;
    u64 pnvm_base_addr; u32 pnvm_size,rsv2; u64 hwm_base_addr; u32 hwm_size,debug_token_config;
    u64 free_rbd_addr; u32 rsv3; u64 rp_base_addr; u32 rp_size,rsv4;
    u32 reserved[12]; struct iwx_ctxt_dram dram;
};
struct __attribute__((packed)) iwx_ctxt_info_gen3 {
    u16 version,size; u32 config; u64 prph_info_base_addr,cr_head_idx_arr_base_addr,tr_tail_idx_arr_base_addr,cr_tail_idx_arr_base_addr,tr_head_idx_arr_base_addr;
    u16 cr_idx_arr_size,tr_idx_arr_size; u64 mtr_base_addr,mcr_base_addr; u16 mtr_size,mcr_size,mtr_doorbell_vec,mcr_doorbell_vec,mtr_msi_vec,mcr_msi_vec;
    u8 mtr_opt_header_size,mtr_opt_footer_size,mcr_opt_header_size,mcr_opt_footer_size; u16 msg_rings_ctrl_flags,prph_info_msi_vec;
    u64 prph_scratch_base_addr; u32 prph_scratch_size,reserved;
};
struct __attribute__((packed)) iwx_rx_transfer_desc { u16 rbid,reserved[3]; u64 addr; };
struct __attribute__((packed)) iwx_tfh_tb { u16 tb_len; u64 addr; };
struct __attribute__((packed)) iwx_tfh_tfd { u16 num_tbs; struct iwx_tfh_tb tbs[IWX_TFH_NUM_TBS]; u32 pad; };
struct __attribute__((packed)) iwx_cmd_header_wide { u8 opcode,group_id,idx,qid; u16 length; u8 reserved,version; };
_Static_assert(sizeof(struct iwx_ctxt_info)==1792,"iwx_ctxt_info");
_Static_assert(sizeof(struct iwx_prph_scratch)==1660,"iwx_prph_scratch");
_Static_assert(sizeof(struct iwx_ctxt_info_gen3)==104,"iwx_ctxt_info_gen3");
_Static_assert(sizeof(struct iwx_rx_transfer_desc)==16&&sizeof(struct iwx_tfh_tfd)==256&&sizeof(struct iwx_cmd_header_wide)==8,"iwx ring structs");
_Static_assert(sizeof(struct iwx_rx_packet)==8&&sizeof(struct iwx_rx_mpdu_desc)==64,"iwx rx structs");
struct iwx_dma { u8* va; u64 pa; u32 sz; };
struct iwx_fw_sect { u32 devoff; const u8* data; u32 len; };
struct iwx_cmd_ver { u8 cmd,group,cmd_ver,notif_ver; };
struct iwx_txq { struct iwx_dma desc,bc,buf; int cur,nbuf; u32 bufsz; };
struct iwx_softc {
    int bus,slot,fn,state,family,integrated,nic_locks,fwapi,starts;
    u16 vendor,device,subdev; u32 hw_step,mmio,hw_rev,hw_rf_id,umac_off,rfkill,last_int,alive_status,uc_ok;
    const char* product; const char* fwbase; const char* pnvmbase; const char* err;
    char fwfile[80],pnvmfile[80],fwver[40],fwb[32],pnvmb[32];
    u8* fwraw; u32 fwsize; struct iwx_fw_sect sect[IWX_UCODE_SECT_MAX]; int nsect;
    const u8* iml; u32 iml_len; u32 ucode_api[4],capa[4],phy_config,n_scan_channels; const u8* pnvm_tlv; u32 pnvm_tlv_len;
    struct iwx_cmd_ver cmd_versions[128]; int n_cmd_versions;
    struct iwx_dma ctxt,scratch,prph_info,imld,rx_free,rx_used,rx_stat,rxbuf[IWX_NRXBUF],fwd[IWX_UCODE_SECT_MAX];
    int nfwd,rx_cur,rx_widx,dram_done; u32 rx_pkts;
    struct iwx_txq txq[IWX_NTXQ];
};
extern struct iwx_softc g_iwx;
const char* iwx_match(u16 vendor,u16 device);
int iwx_probe(void);
int iwx_start(void);
void iwx_status(char* out,int cap);
void iwx_up(char* out,int cap);
void iwx_wifi_hw_probe(void);
u32 iwx_r32(struct iwx_softc* sc,u32 o);
void iwx_w32(struct iwx_softc* sc,u32 o,u32 v);
int iwx_nic_lock(struct iwx_softc* sc);
void iwx_nic_unlock(struct iwx_softc* sc);
u32 iwx_read_prph(struct iwx_softc* sc,u32 a);
void iwx_write_prph(struct iwx_softc* sc,u32 a,u32 v);
int iwx_fw_select(struct iwx_softc* sc);
int iwx_fw_parse(struct iwx_softc* sc,const u8* d,u32 n);
int iwx_fw_load_file(struct iwx_softc* sc);
int iwx_cmd_version(struct iwx_softc* sc,u8 grp,u8 cmd);
int iwx_notif_version(struct iwx_softc* sc,u8 grp,u8 cmd);
#endif
