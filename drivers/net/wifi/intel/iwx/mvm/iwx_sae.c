/*
 * Derived from OpenBSD sys/dev/pci/if_iwx.c, if_iwxreg.h (rev 1.230 / 1.75) and
 * Linux drivers/net/wireless/intel/iwlwifi/fw/api/sta.h (MGMT_MCAST_KEY, STA_KEY_MFP).
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
 * Intel Corporation; Copyright (C) 2012-2014, 2018-2021, 2023, 2025-2026 Intel
 * Corporation, Copyright (C) 2013-2014 Intel Mobile Communications GmbH,
 * Copyright (C) 2016-2017 Intel Deutschland GmbH (SPDX: GPL-2.0 OR BSD-3-Clause),
 * used under the BSD-3-Clause branch of the dual BSD/GPLv2
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
#include "drivers/net/wifi/intel/iwx/iwx_cmd.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/net/wifi/wpa_sm.h"
#include "include/nvx/net/wifi/wifi_hw.h"
#define IWX_MGMT_MCAST_KEY 0x1f
#define IWX_STA_KEY_FLG_CCM 2u
struct __attribute__((packed)) iwx_mgmt_mcast_key_cmd { u32 ctrl_flags; u8 igtk[32]; u32 key_id,sta_id; u64 receive_seq_cnt; };
_Static_assert(sizeof(struct iwx_mgmt_mcast_key_cmd)==52,"iwx igtk cmd");
extern int g_wpa_mfp;
#define IWX_SAE_Q 4
static struct { struct iwx_softc* sc; u8 bssid[6],own[6]; u16 seq; volatile int h,n; int len[IWX_SAE_Q]; u8 body[IWX_SAE_Q][400]; int fails; } iwx_sx;
static int iwx_sae_eq6(const u8* a,const u8* b){ for(int i=0;i<6;i++)if(a[i]!=b[i])return 0; return 1; }
void iwx_sae_rx(const u8* f,u32 len){
    if(!iwx_sx.sc||iwx_sx.n>=IWX_SAE_Q||len<30||(f[0]&0xFC)!=0xB0||!iwx_sae_eq6(f+10,iwx_sx.bssid)||!iwx_sae_eq6(f+4,iwx_sx.own)||f[24]!=3||f[25]!=0)return;
    u32 n=len-24; if(n>400)n=400; int s=(iwx_sx.h+iwx_sx.n)%IWX_SAE_Q;
    for(u32 i=0;i<n;i++)iwx_sx.body[s][i]=f[24+i]; iwx_sx.len[s]=(int)n; iwx_sx.n++; }
extern int iwx_sp_rearm(struct iwx_softc* sc) __attribute__((weak));
static int iwx_sae_tx(void* c,const u8* f,int n,int ph,int round){ (void)c;(void)round;
    if(ph>0)return 0; { int (*volatile ra)(struct iwx_softc*)=iwx_sp_rearm; if(ra)ra(iwx_sx.sc); } if(iwx_tx_frame(iwx_sx.sc,f,(u32)n)<0)return 0; return ph<0?1:3; }
static int iwx_sae_rxm(void* c,u8 want,u8* body,int* blen,int ms){ (void)c;
    if(want!=0xB0)return 0; u64 t0=iwx_now_ms();
    while(!iwx_sx.n&&iwx_now_ms()-t0<(u64)ms)iwx_rx_poll(iwx_sx.sc);
    if(!iwx_sx.n)return 0; int s=iwx_sx.h; for(int i=0;i<iwx_sx.len[s];i++)body[i]=iwx_sx.body[s][i]; *blen=iwx_sx.len[s];
    iwx_sx.h=(s+1)%IWX_SAE_Q; iwx_sx.n--; return 1; }
static const struct wpa_io iwx_sae_io={ 0,0,0,0,iwx_sae_tx,iwx_sae_rxm,0,&iwx_sx.seq,0,0 };
int iwx_sae_mfp(void){ return g_wpa_mfp; }
int iwx_sae_auth(struct iwx_softc* sc,const u8* bssid){
    if(!wifi_gen_sae_active())return -2;
    iwx_sx.sc=sc; for(int i=0;i<6;i++)iwx_sx.bssid[i]=bssid[i]; iwx_hw_mac(sc,iwx_sx.own); iwx_sx.h=iwx_sx.n=0; iwx_sx.seq=0x100;
    klog("iwx: WPA3 SAE authentication (host commit/confirm)");
    int r=wifi_gen_sae_auth(&iwx_sae_io); iwx_sx.sc=0;
    if(r==0){ iwx_sx.fails=0; klog("iwx: SAE done, PMK derived"); return 0; }
    if(++iwx_sx.fails>=3)klog("iwx: SAE failed 3 times in a row; check the password (wifi sae off turns host SAE off)");
    return r>0?r:1; }
int iwx_set_igtk(struct iwx_softc* sc,int idx,const u8* key,int len){
    if(!sc||!key||len!=16||idx<4||idx>5)return -1;
    struct iwx_mgmt_mcast_key_cmd c; u8* z=(u8*)&c; for(u32 i=0;i<sizeof c;i++)z[i]=0;
    c.ctrl_flags=IWX_STA_KEY_FLG_CCM; for(int i=0;i<16;i++)c.igtk[i]=key[i]; c.key_id=(u32)idx; c.sta_id=0;
    if(iwx_send_cmd_sync(sc,IWX_MGMT_MCAST_KEY,&c,sizeof c,0,0,0,1000)<0){ klog("iwx: firmware refused the IGTK (BIP)"); return -1; }
    klog("iwx: IGTK installed, protected management frames on"); return 0; }
