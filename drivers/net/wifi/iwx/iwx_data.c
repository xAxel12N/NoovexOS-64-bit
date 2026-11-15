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
#include "drivers/net/wifi/iwx/iwx_cmd.h"
void (*iwx_rx_deliver)(const u8* frame,u32 len,int rssi);
static u8 iwx_txbuf[2400];
static u8 iwx_rxcp[2400];
int iwx_rate_v2,iwx_hwkeys;
u8 iwx_jbss[6];
volatile int iwx_jwant,iwx_auth_st=-1,iwx_assoc_st=-1;
volatile u16 iwx_assoc_aid;
static int iwx_eq6(const u8* a,const u8* b){ for(int i=0;i<6;i++)if(a[i]!=b[i])return 0; return 1; }
static void iwx_join_rx(const u8* f,u32 len){
    if(!iwx_jwant||len<24||!iwx_eq6(f+16,iwx_jbss))return;
    u8 st=f[0]&0xFC;
    if(st==0xB0&&len>=30){ if((f[26]|(f[27]<<8))==2)iwx_auth_st=f[28]|(f[29]<<8); }
    else if((st==0x10||st==0x30)&&len>=30){ iwx_assoc_aid=(u16)((f[28]|(f[29]<<8))&0x3FFF); iwx_assoc_st=f[26]|(f[27]<<8); }
    else if(st==0xC0||st==0xA0){ int r=len>=26?(f[24]|(f[25]<<8)):1; r|=0x10000; if(iwx_auth_st<0)iwx_auth_st=r; if(iwx_assoc_st<0)iwx_assoc_st=r; }
}
static u32 iwx_hdrlen(const u8* f){ u32 h=24; if((f[1]&3)==3)h+=6; if((f[0]&0x0C)==0x08&&(f[0]&0x80))h+=2; return h; }
void iwx_rx_mpdu(struct iwx_softc* sc,const u8* d,u32 len){
    int ax=iwx_is_ax210(sc);
    u32 ds=ax?sizeof(struct iwx_rx_mpdu_desc):IWX_RX_DESC_SIZE_V1;
    if(len<ds)return;
    const struct iwx_rx_mpdu_desc* desc=(const struct iwx_rx_mpdu_desc*)d;
    if(!(desc->status&IWX_RX_MPDU_RES_STATUS_CRC_OK)||!(desc->status&IWX_RX_MPDU_RES_STATUS_OVERRUN_OK))return;
    u32 fl=desc->mpdu_len;
    u32 off=ds;
    if(desc->mac_flags2&IWX_RX_MPDU_MFLG2_PAD)off+=2;
    if(fl<24||off+fl>len)return;
    int ea=ax?desc->v3.energy_a:desc->v1.energy_a, eb=ax?desc->v3.energy_b:desc->v1.energy_b;
    u8 ch=ax?desc->v3.channel:desc->v1.channel;
    ea=ea?-ea:-256; eb=eb?-eb:-256;
    int rssi=ea>eb?ea:eb;
    const u8* f=d+off;
    if((f[0]&0x0C)==0){ iwx_scan_rx_mgmt(f,fl,rssi,ch); iwx_join_rx(f,fl); }
    if(iwx_hwkeys&&(f[1]&0x40)&&(f[0]&0x0C)!=0x04){
        u32 st=desc->status, h=iwx_hdrlen(f);
        if((st&(7u<<8))!=(2u<<8))return;
        if((st&((1u<<11)|(1u<<6)))!=((1u<<11)|(1u<<6)))return;
        if(fl<h+8||fl-8>sizeof iwx_rxcp)return;
        for(u32 i=0;i<h;i++)iwx_rxcp[i]=f[i];
        for(u32 i=h+8;i<fl;i++)iwx_rxcp[i-8]=f[i];
        iwx_rxcp[1]&=(u8)~0x40; f=iwx_rxcp; fl-=8;
    }
    if(iwx_rx_deliver)iwx_rx_deliver(f,fl,rssi);
}
int iwx_tx_frame(struct iwx_softc* sc,const u8* frame,u32 len){
    if(!sc||len<24||len>sizeof iwx_txbuf-64)return -1;
    u32 h=iwx_hdrlen(frame); if(h>len)return -1;
    u32 pad=(h&3)?4-(h&3):0;
    u32 oa=((h/2)&0x1Fu)<<8; if(pad)oa|=(1u<<13);
    u32 rate=IWX_RATE_MCS_ANT_A_MSK|(iwx_rate_v2?(1u<<8):IWX_RATE_6M_PLCP);
    u32 fl=(1u<<1)|IWX_TX_FLAGS_CMD_RATE;
    const u8* src=frame; u32 n=len;
    if(pad){ for(u32 i=0;i<h;i++)iwx_txbuf[i]=frame[i]; for(u32 i=0;i<pad;i++)iwx_txbuf[h+i]=0; for(u32 i=h;i<len;i++)iwx_txbuf[i+pad]=frame[i]; src=iwx_txbuf; n=len+pad; }
    if(iwx_is_ax210(sc)){
        struct iwx_tx_cmd_gen3 t; u8* z=(u8*)&t; for(u32 i=0;i<sizeof t;i++)z[i]=0;
        t.len=(u16)len; t.flags=(u16)fl; t.offload_assist=oa; t.rate_n_flags=rate;
        return iwx_tx_data(sc,1,&t,sizeof t,src,n);
    }
    struct iwx_tx_cmd_gen2 t; u8* z=(u8*)&t; for(u32 i=0;i<sizeof t;i++)z[i]=0;
    t.len=(u16)len; t.offload_assist=(u16)oa; t.flags=fl; t.rate_n_flags=rate;
    return iwx_tx_data(sc,1,&t,sizeof t,src,n);
}
