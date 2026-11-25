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
#include "drivers/net/wifi/intel/iwx/iwx_cmd.h"
#include "drivers/net/wifi/intel/iwx/mvm/iwx_tlc.h"
#include "include/nvx/core/klog.h"
extern void iwx_he_scan_note(const u8* bssid,const u8* ie,u32 len) __attribute__((weak));
static struct iwx_bss iwx_bss_tab[IWX_MAX_SCAN_BSS];
static struct iwx_peer iwx_peer_tab[IWX_MAX_SCAN_BSS];
static int iwx_nbss;
static volatile int iwx_scanning;
static struct iwx_scan_req_umac_v14 iwx_scmd;
int iwx_scan_busy(void){ return iwx_scanning; }
void iwx_scan_done_notif(const u8* d,u32 len){
    (void)d;
    if(len>=sizeof(struct iwx_umac_scan_complete))iwx_scanning=0;
}
void iwx_scan_rx_mgmt(const u8* f,u32 len,int rssi,u8 chan){
    if(len<36)return;
    u8 st=f[0]&0xFC;
    if(st!=0x80&&st!=0x50)return;
    const u8* bssid=f+16;
    int i; for(i=0;i<iwx_nbss;i++){ int s=1; for(int k=0;k<6;k++)if(iwx_bss_tab[i].bssid[k]!=bssid[k])s=0; if(s)break; }
    if(i==iwx_nbss){ if(iwx_nbss>=IWX_MAX_SCAN_BSS)return; iwx_nbss++; }
    struct iwx_bss* b=&iwx_bss_tab[i];
    for(int k=0;k<6;k++)b->bssid[k]=bssid[k];
    b->rssi=rssi; b->capab=(u16)(f[34]|(f[35]<<8)); b->channel=chan;
    iwx_peer_parse(f+36,len-36,&iwx_peer_tab[i]); if(iwx_he_scan_note)iwx_he_scan_note(bssid,f+36,len-36);
    u32 o=36;
    while(o+2<=len){
        u8 id=f[o],l=f[o+1];
        if(o+2+l>len)break;
        if(id==0){ u8 n=l>32?32:l; for(u8 k=0;k<n;k++)b->ssid[k]=f[o+2+k]; b->ssid[n]=0; b->ssid_len=n; }
        else if(id==3&&l>=1)b->channel=f[o+2];
        o+=2u+l;
    }
}
int iwx_scan_results(struct iwx_bss* out,int max){
    int n=iwx_nbss<max?iwx_nbss:max;
    for(int i=0;i<n;i++)out[i]=iwx_bss_tab[i];
    return n;
}
int iwx_scan_start(struct iwx_softc* sc,const u8* ssid,int ssid_len){
    if(!sc)return -1;
    if(!iwx_fw_api(sc,IWX_UCODE_TLV_API_SCAN_EXT_CHAN_VER)){ static int s; if(!s){ s=1; klog("iwx: firmware lacks SCAN_EXT_CHAN_VER; scan v14 unsupported"); } return -1; }
    iwx6_mcc_update(sc,0);
    struct iwx_scan_req_umac_v14* c=&iwx_scmd;
    u8* z=(u8*)c; for(u32 i=0;i<sizeof *c;i++)z[i]=0;
    if(ssid_len<0||ssid_len>32)ssid_len=0;
    c->ooc_priority=6; c->uid=0;
    struct iwx_scan_general_params_v10* g=&c->general_params;
    u16 fl=(1u<<1)|(1u<<2)|(1u<<7);
    if(!ssid_len)fl|=(1u<<11);
    g->flags=fl;
    g->adwell_default_social_chn=10; g->adwell_default_2g=2; g->adwell_default_5g=8;
    g->adwell_max_budget=300; g->scan_priority=6;
    g->active_dwell[0]=g->active_dwell[1]=10; g->passive_dwell[0]=g->passive_dwell[1]=110;
    c->periodic_params.schedule[0].iter_count=1;
    struct iwx_scan_probe_req* p=&c->probe_params.preq;
    u8* w=p->buf; u8 mac[6]; iwx_hw_mac(sc,mac);
    w[0]=0x40; for(int k=0;k<6;k++){ w[4+k]=0xFF; w[10+k]=mac[k]; w[16+k]=0xFF; }
    p->mac_header.offset=0; p->mac_header.len=24;
    u8* q=w+24; *q++=0; *q++=0;
    p->common_data.offset=24; p->common_data.len=2;
    static const u8 r24[]={1,8,0x82,0x84,0x8b,0x96,0x0c,0x12,0x18,0x24};
    static const u8 r5[]={1,8,0x8c,0x12,0x98,0x24,0xb0,0x48,0x60,0x6c};
    p->band_data[0].offset=26; p->band_data[0].len=sizeof r24; for(u32 k=0;k<sizeof r24;k++)q[k]=r24[k]; q+=sizeof r24;
    p->band_data[1].offset=(u16)(26+sizeof r24); p->band_data[1].len=sizeof r5; for(u32 k=0;k<sizeof r5;k++)q[k]=r5[k];
    u32 bm=0;
    if(ssid_len){ c->probe_params.direct_scan[0].id=0; c->probe_params.direct_scan[0].len=(u8)ssid_len; for(int k=0;k<ssid_len;k++)c->probe_params.direct_scan[0].ssid[k]=ssid[k]; bm=1; }
    struct iwx_scan_channel_params_v6* cp=&c->channel_params;
    cp->flags=(1u<<5); cp->n_aps_override[0]=10; cp->n_aps_override[1]=2;
    const struct iwx_nvm* n=iwx_nvm_info();
    static const u8 defch[]={1,2,3,4,5,6,7,8,9,10,11,36,40,44,48,149,153,157,161,165};
    const u8* chl=n->n_channels?n->chan:defch; u32 nc=n->n_channels?n->n_channels:sizeof defch;
    u8 cnt=0;
    for(u32 i=0;i<nc&&cnt<67;i++){
        struct iwx_scan_channel_cfg_umac* e=&cp->channel_config[cnt++];
        e->flags=bm; e->channel_num=chl[i]; e->b1=chl[i]<=14?IWX_PHY_BAND_24:IWX_PHY_BAND_5; e->b2=1; e->b3=0;
    }
    if(iwx6_uhb(sc)){ int k=iwx6_scan_fill(&cp->channel_config[cnt],67-cnt); if(k>0){ cnt=(u8)(cnt+k); cp->flags|=(1u<<6); } }
    cp->count=cnt;
    iwx_nbss=0; iwx_scanning=1;
    if(iwx_send_cmd_sync(sc,IWX_WIDE_ID(IWX_LONG_GROUP,IWX_SCAN_REQ_UMAC),c,sizeof *c,0,0,0,2000)<0){ iwx_scanning=0; return -1; }
    return 0;
}
int iwx_scan_find(const u8* bssid,struct iwx_bss* out){
    for(int i=0;i<iwx_nbss;i++){ int s=1; for(int k=0;k<6;k++)if(iwx_bss_tab[i].bssid[k]!=bssid[k])s=0; if(s){ if(out)*out=iwx_bss_tab[i]; return 0; } }
    return -1;
}
int iwx_scan_peer(const u8* bssid,struct iwx_peer* out){
    for(int i=0;i<iwx_nbss;i++){ int s=1; for(int k=0;k<6;k++)if(iwx_bss_tab[i].bssid[k]!=bssid[k])s=0; if(s){ *out=iwx_peer_tab[i]; return 0; } }
    return -1;
}
