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
 * Portions: Copyright (C) 2012-2014, 2018-2025 Intel Corporation, used under the
 * BSD-3-Clause branch of the dual "GPL-2.0 OR BSD-3-Clause" licence (fw/api/rx.h
 * RX band field): Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following conditions
 * are met: Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer. Redistributions
 * in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution. Neither the name Intel Corporation
 * nor the names of its contributors may be used to endorse or promote products
 * derived from this software without specific prior written permission. THIS
 * SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DAMAGES ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "drivers/net/wifi/intel/iwx/iwx_cmd.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/net/wifi/wifi_hw.h"
#define IWX6_VBASE 178
#define IWX6_N 59
#define IWX6_NLEG 51
#define IWX_UCODE_TLV_CAPA_ULTRA_HB 48
#define IWX_UCODE_TLV_CAPA_LAR_MULTI_MCC 29
#define IWX_UCODE_TLV_CAPA_MCC_11AX 89
int g_iwx_6g=1;
static u64 iwx6_map,iwx6_act;
static u16 iwx6_mcc;
static int iwx6_mcc_fail;
extern const char* regdb_country(void) __attribute__((weak));
extern int regdb_freq_flags(int mhz,int bw) __attribute__((weak));
extern int regdb_enabled(void) __attribute__((weak));
extern int iwx_notif_version(struct iwx_softc* sc,u8 grp,u8 cmd) __attribute__((weak));
extern u32 iwx_he_cap_ie(u8* f,int ch,int mimo,int c160) __attribute__((weak));
extern int g_wifi_scanning;
static const u8 iwx6_leg[IWX6_NLEG]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,36,40,44,48,52,56,60,64,68,72,76,80,84,88,92,96,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165,169,173,177,181};
int iwx6_is_vch(int v){ return v>=IWX6_VBASE&&v<IWX6_VBASE+IWX6_N; }
int iwx6_ch(int v){ return iwx6_is_vch(v)?(v-IWX6_VBASE)*4+1:0; }
int iwx6_vch(int c){ return c>=1&&c<=233&&(c&3)==1?IWX6_VBASE+(c-1)/4:0; }
int iwx6_psc(int c){ return c>=5&&c<=229&&(c-5)%16==0; }
int iwx6_mhz(int c){ return 5950+5*c; }
int iwx6_uhb(struct iwx_softc* sc){ return iwx_is_ax210(sc)&&iwx_fw_capa(sc,IWX_UCODE_TLV_CAPA_ULTRA_HB); }
static int iwx6_reg_ok(int c){ int (*volatile ff)(int,int)=regdb_freq_flags; int (*volatile en)(void)=regdb_enabled; if(!ff||!en||!en())return 1; return ff(iwx6_mhz(c),20)>=0; }
static int iwx6_ir_ok(int c){ int (*volatile ff)(int,int)=regdb_freq_flags; int (*volatile en)(void)=regdb_enabled; if(!ff||!en||!en())return 1; int f=ff(iwx6_mhz(c),20); return f>=0&&!(f&2); }
void iwx6_chmap(struct iwx_nvm* n,const void* prof,int w,u32 np,int uhb){
    u32 nc=uhb?IWX6_NLEG+IWX6_N:IWX6_NLEG; n->n_channels=0; iwx6_map=iwx6_act=0;
    for(u32 i=0;i<nc&&i<np&&i<IWX_NUM_CHANNELS;i++){
        const u8* q=(const u8*)prof+i*(u32)w; u32 f=w==4?(u32)q[0]|((u32)q[1]<<8)|((u32)q[2]<<16)|((u32)q[3]<<24):(u32)q[0]|((u32)q[1]<<8); if(!(f&1))continue;
        if(i>=IWX6_NLEG){ iwx6_map|=1ull<<(i-IWX6_NLEG); if(f&(1u<<3))iwx6_act|=1ull<<(i-IWX6_NLEG); continue; }
        u8 c=iwx6_leg[i];
        if(c<=14?!n->band24:(!n->band5||c>=169))continue;
        n->chan[n->n_channels++]=c;
    }
}
int iwx6_count(void){ int k=0; for(int i=0;i<IWX6_N;i++)if(iwx6_map>>i&1)k++; return k; }
int iwx6_scan_fill(struct iwx_scan_channel_cfg_umac* e,int room){
    if(!g_iwx_6g)return 0;
    int k=0;
    for(int i=0;i<IWX6_N&&k<room;i++){
        int c=i*4+1; if(!(iwx6_map>>i&1)||!iwx6_psc(c)||!iwx6_reg_ok(c))continue;
        e[k].flags=((iwx6_act>>i)&1)&&iwx6_ir_ok(c)?0:(1u<<26); e[k].channel_num=(u8)c; e[k].b1=2; e[k].b2=1; e[k].b3=0; k++;
    }
    return k;
}
static int iwx6_has_sae(const u8* f,u32 len){
    u32 o=36;
    while(o+2<=len){ u8 id=f[o],l=f[o+1]; if(o+2+l>len)break;
        if(id==48&&l>=8){ const u8* r=f+o+2; u32 p=6; if(p+2>l)return 0; u32 pc=r[p]|(r[p+1]<<8); p+=2+4*pc; if(p+2>l)return 0; u32 ac=r[p]|(r[p+1]<<8); p+=2;
            for(u32 k=0;k<ac&&p+4<=l;k++,p+=4){ if(r[p]==0&&r[p+1]==0x0F&&r[p+2]==0xAC&&(r[p+3]==8||r[p+3]==24))return 1; }
            return 0; }
        o+=2u+l; }
    return 0;
}
u8 iwx6_rx_chan(int ax,u8 mac_phy_idx,u8 ch){ return ax&&((mac_phy_idx>>6)&3)==2?(u8)iwx6_vch(ch):ch; }
void iwx6_rx_mgmt(const u8* f,u32 len,int rssi,u8 vch){
    u8 st=f[0]&0xFC; if(!g_iwx_6g||!g_wifi_scanning||len<36||(st!=0x80&&st!=0x50))return;
    if(!iwx6_has_sae(f,len)){ static int s; if(!s){ s=1; klog("iwx: ignoring a 6 GHz network without WPA3-SAE (6 GHz allows SAE/OWE only)"); } return; }
    wifi_scan_parse(f,(int)len,vch,rssi,0);
}
static const u8 iwx6_hecap[24]={255,22,35,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0xFC,0xFF,0xFC,0xFF};
u32 iwx6_assoc_ies(u8* out,u32 cap,int vch){
    u32 n=0;
    (void)vch;
    u32 (*volatile he)(u8*,int,int,int)=iwx_he_cap_ie;
    if(!he){ if(cap<sizeof iwx6_hecap)return 0; for(u32 i=0;i<sizeof iwx6_hecap;i++)out[i]=iwx6_hecap[i]; n=sizeof iwx6_hecap; }
    if(n+5>cap)return n;
    out[n++]=255; out[n++]=3; out[n++]=59; out[n++]=0x38; out[n++]=0x06;
    return n;
}
int iwx6_join_ok(int vch,u8 akm_sae){
    if(!iwx6_is_vch(vch))return 1;
    if(!g_iwx_6g){ klog("iwx: 6 GHz is off (wifi 6g on)"); return 0; }
    if(!akm_sae){ klog("iwx: 6 GHz networks need WPA3-SAE; not joining"); return 0; }
    return 1;
}
static u16 iwx6_cc(void){
    const char* (*volatile rc)(void)=regdb_country; const char* c=rc?rc():0;
    if(!c||c[0]<'A'||c[0]>'Z'||c[1]<'A'||c[1]>'Z')return ('Z'<<8)|'Z';
    return (u16)((c[0]<<8)|c[1]);
}
int iwx6_mcc_update(struct iwx_softc* sc,int force){
    const struct iwx_nvm* n=iwx_nvm_info(); if(!sc||!n->lar_enabled)return 0;
    u16 cc=iwx6_cc(); if(!force&&(cc==iwx6_mcc||iwx6_mcc_fail>=3))return 0;
    int (*volatile nvf)(struct iwx_softc*,u8,u8)=iwx_notif_version; int nv=nvf?nvf(sc,IWX_LONG_GROUP,IWX_MCC_UPDATE_CMD):0;
    if(nv>=8&&nv!=99){ iwx6_mcc_fail=3; return -1; }
    struct iwx_mcc_update_cmd m; u8* z=(u8*)&m; for(u32 i=0;i<sizeof m;i++)z[i]=0;
    m.mcc=cc; m.source_id=iwx_fw_capa(sc,IWX_UCODE_TLV_CAPA_LAR_MULTI_MCC)?0x10:0;
    static u32 rbuf[160]; u32 rl=0;
    iwx6_mcc=cc;
    if(iwx_send_cmd_sync(sc,IWX_MCC_UPDATE_CMD,&m,sizeof m,rbuf,sizeof rbuf,&rl,2000)<0){ iwx6_mcc_fail++; klog("iwx: country (MCC) update failed"); return -1; }
    u32 hdr=iwx_fw_capa(sc,IWX_UCODE_TLV_CAPA_MCC_11AX)?20:16; const u8* r=(const u8*)rbuf;
    if(rl<hdr){ iwx6_mcc_fail++; return -1; }
    u32 nch=(u32)r[hdr-4]|((u32)r[hdr-3]<<8)|((u32)r[hdr-2]<<16)|((u32)r[hdr-1]<<24);
    if(nch>IWX_NUM_CHANNELS||rl!=hdr+nch*4||hdr+nch*4>sizeof rbuf){ iwx6_mcc_fail++; klog("iwx: country (MCC) reply has an unexpected size; keeping the NVM channel list"); return -1; }
    iwx6_mcc_fail=0;
    iwx6_chmap((struct iwx_nvm*)n,r+hdr,4,nch,iwx6_uhb(sc));
    u16 got=(u16)(r[4]|(r[5]<<8));
    char b[96],*p=b; p=scopy(p,"iwx: country "); *p++=(char)(got>>8); *p++=(char)got; p=scopy(p,", "); p=udec(p,n->n_channels); p=scopy(p," channels at 2.4/5 GHz, "); p=udec(p,(u32)iwx6_count()); p=scopy(p," at 6 GHz"); *p=0; klog(b);
    return 0;
}
