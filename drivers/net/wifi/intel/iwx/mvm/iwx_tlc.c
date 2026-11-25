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
#include "drivers/net/wifi/intel/iwx/mvm/iwx_tlc.h"
volatile int iwx_tlc_on;
volatile u32 iwx_tlc_rate;
_Static_assert(sizeof(struct iwx_tlc_config_cmd_v3)==28&&sizeof(struct iwx_tlc_config_cmd_v4)==28&&sizeof(struct iwx_tlc_update_notif)==20,"tlc sizes");
static const u8 iwx_tlc_11g[12]={2,4,11,22,12,18,24,36,48,72,96,108};
static void iwx_tlc_z(void* d,u32 n){ u8* a=(u8*)d; while(n--)*a++=0; }
static int iwx_tlc_nant(u8 a){ return (a&1)+((a>>1)&1)+((a>>2)&1); }
int iwx_mimo_ok(u8 txant,u8 rxant){ return ((rxant&3)==3||(rxant&6)==6)&&iwx_tlc_nant(txant)>1; }
void iwx_peer_parse(const u8* ie,u32 len,struct iwx_peer* p){
    iwx_tlc_z(p,sizeof *p);
    for(u32 o=0;o+2<=len;){
        u8 id=ie[o],l=ie[o+1]; const u8* v=ie+o+2;
        if(o+2+l>len)break;
        if(id==1||id==50){ for(u8 i=0;i<l;i++)for(int k=0;k<12;k++)if((v[i]&0x7F)==iwx_tlc_11g[k])p->rates|=(u16)(1u<<k); }
        else if(id==45&&l>=26){ p->ht=1; p->htcap=(u16)(v[0]|(v[1]<<8)); p->ampdu=v[2]; for(int k=0;k<4;k++)p->mcs[k]=v[3+k]; }
        else if(id==61&&l>=22){ p->hop=1; p->htop0=v[1]; p->htop1=v[2]; }
        else if(id==191&&l>=12){ p->vht=1; p->vhtcap=(u32)v[0]|((u32)v[1]<<8)|((u32)v[2]<<16)|((u32)v[3]<<24); p->vht_rxmcs=(u16)(v[4]|(v[5]<<8)); }
        else if(id==192&&l>=3){ p->vop=1; p->vw=v[0]; p->vc0=v[1]; p->vc1=v[2]; }
        else if(id==221&&l>=5&&v[0]==0x00&&v[1]==0x50&&v[2]==0xF2&&v[3]==2&&v[4]<=1)p->wmm=1;
        else if(id==255&&l>=1&&v[0]==35)p->he=1;
        o+=2u+l;
    }
}
void iwx_peer_merge(struct iwx_peer* a,const struct iwx_peer* s,int ht_sent,int ch){
    if(!a->rates)a->rates=s->rates;
    if(!a->ht&&s->ht){ a->ht=1; a->htcap=s->htcap; a->ampdu=s->ampdu; for(int k=0;k<4;k++)a->mcs[k]=s->mcs[k]; }
    if(!a->hop&&s->hop){ a->hop=1; a->htop0=s->htop0; a->htop1=s->htop1; }
    if(!a->vht&&s->vht){ a->vht=1; a->vhtcap=s->vhtcap; a->vht_rxmcs=s->vht_rxmcs; }
    if(!a->vop&&s->vop){ a->vop=1; a->vw=s->vw; a->vc0=s->vc0; a->vc1=s->vc1; }
    a->wmm|=s->wmm; a->he=0;
    if(!ht_sent)a->ht=0;
    if(!a->ht||ch<=14)a->vht=0;
}
u32 iwx_assoc_ht_ies(u8* f,const struct iwx_peer* p,int ch,u8 txant,u8 rxant){
    u32 n=0; int mimo=iwx_mimo_ok(txant,rxant), ntx=iwx_tlc_nant(txant);
    if(!p->ht||!p->wmm)return 0;
    u16 cap=(u16)((1u<<5)|(1u<<6)|(3u<<2)|(1u<<8)); if(ch>14)cap|=(1u<<1); if(ntx>1)cap|=(1u<<7);
    f[n++]=45; f[n++]=26; f[n++]=(u8)cap; f[n++]=(u8)(cap>>8); f[n++]=0x17;
    for(int i=0;i<16;i++)f[n+i]=0;
    f[n]=0xFF; if(mimo)f[n+1]=0xFF; f[n+12]=1; n+=16;
    for(int i=0;i<7;i++)f[n++]=0;
    if(ch>14&&p->vht){
        u32 vc=(1u<<5)|(1u<<8)|(7u<<23); if(ntx>1)vc|=(1u<<7); else vc|=(1u<<29); if(!mimo)vc|=(1u<<28);
        u16 mm=mimo?0xFFFA:0xFFFE;
        f[n++]=191; f[n++]=12; for(int i=0;i<4;i++)f[n++]=(u8)(vc>>(8*i));
        f[n++]=(u8)mm; f[n++]=(u8)(mm>>8); f[n++]=0; f[n++]=0; f[n++]=(u8)mm; f[n++]=(u8)(mm>>8); f[n++]=0; f[n++]=0;
    }
    static const u8 wmm[9]={221,7,0x00,0x50,0xF2,0x02,0x00,0x01,0x00};
    for(int i=0;i<9;i++)f[n++]=wmm[i];
    return n;
}
static u8 iwx_tlc_pos(int d){
    static const signed char dv[8]={-2,-6,-10,-14,2,6,10,14};
    for(int i=0;i<8;i++)if(dv[i]==d)return (u8)i;
    return 0;
}
void iwx_link_pick(const struct iwx_peer* p,int ch,u8 txant,u8 rxant,struct iwx_link* l){
    iwx_tlc_z(l,sizeof *l); l->chains=1;
    if(!p->ht)return;
    l->ht=1; l->vht=p->vht&&ch>14; l->mimo=(u8)iwx_mimo_ok(txant,rxant);
    if(l->mimo&&(l->vht?((p->vht_rxmcs>>2)&3)!=3:p->mcs[1]!=0))l->chains=2;
    int sco=p->hop?(p->htop0&3):0, c40=ch>14&&(p->htcap&(1u<<1))&&(sco==1||sco==3);
    if(l->vht&&p->vop&&p->vw>=1&&p->vw<=3&&c40){
        int c=p->vc0; if(p->vw==2)c=ch<c?c-8:c+8;
        int d=ch-c; if(d==-6||d==-2||d==2||d==6){ l->width=2; l->ctrl_pos=iwx_tlc_pos(d); }
    }
    if(!l->width&&c40){ l->width=1; l->ctrl_pos=sco==1?0:4; }
    u32 f=0;
    if(l->chains==2)f|=(1u<<28);
    f|=(u32)l->width<<26;
    u32 agg=l->vht?((p->vhtcap>>23)&7):(p->ampdu&3); if(agg>7)agg=7;
    f|=agg<<19;
    u32 dens=(p->ampdu>>2)&7; if(dens>=4)f|=dens<<23;
    l->sta_flags=f;
    l->mac_qos=IWX_MAC_QOS_FLG_TGN;
    u8 prot=p->htop1&3;
    if(prot==1||prot==3||(prot==2&&l->width))l->mac_prot=IWX_MAC_PROT_FLG_HT_PROT|IWX_MAC_PROT_FLG_FAT_PROT;
}
static u16 iwx_tlc_vht(const struct iwx_peer* p,int nss){
    u32 m=(p->vht_rxmcs>>(2*(nss-1)))&3;
    if(m==0)return 0xFF;
    if(m==1)return 0x1FF;
    if(m==2)return (p->htcap&(1u<<1))?0x3FF:0x1FF;
    return 0;
}
u32 iwx_tlc_build(const struct iwx_peer* p,const struct iwx_link* l,u8 txant,int ver,void* out){
    u16 nht=p->rates?p->rates:0xFF0, r[2]={0,0}, fl=0, mpdu=2312; u8 mode=IWX_TLC_MODE_NON_HT, sgi=0, chains=1;
    if(l->vht){ mode=IWX_TLC_MODE_VHT; r[0]=iwx_tlc_vht(p,1); if(l->mimo)r[1]=iwx_tlc_vht(p,2); mpdu=3895; }
    else if(l->ht){ mode=IWX_TLC_MODE_HT; r[0]=p->mcs[0]; if(l->mimo)r[1]=p->mcs[1]; mpdu=3839; }
    if(l->ht&&l->mimo)chains=3;
    if(iwx_tlc_nant(txant)>1&&((l->vht&&(p->vhtcap&(7u<<8)))||(!l->vht&&l->ht&&(p->htcap&(3u<<8)))))fl|=IWX_TLC_CFG_STBC;
    if(l->ht){ if(p->htcap&(1u<<5))sgi|=1; if(p->htcap&(1u<<6))sgi|=2; }
    if(l->vht&&(p->vhtcap&(1u<<5)))sgi|=4;
    if(ver==4){
        struct iwx_tlc_config_cmd_v4* c=(struct iwx_tlc_config_cmd_v4*)out; iwx_tlc_z(c,sizeof *c);
        c->max_ch_width=l->width; c->mode=mode; c->chains=chains; c->sgi_ch_width_supp=sgi; c->flags=fl; c->non_ht_rates=nht;
        c->ht_rates[0][0]=r[0]; c->ht_rates[1][0]=r[1]; c->max_mpdu_len=mpdu;
        return sizeof *c;
    }
    struct iwx_tlc_config_cmd_v3* c=(struct iwx_tlc_config_cmd_v3*)out; iwx_tlc_z(c,sizeof *c);
    c->max_ch_width=l->width; c->mode=mode; c->chains=chains; c->sgi_ch_width_supp=sgi; c->flags=fl; c->non_ht_rates=nht;
    c->ht_rates[0][0]=r[0]; c->ht_rates[1][0]=r[1]; c->max_mpdu_len=mpdu;
    return sizeof *c;
}
void iwx_tlc_notif(const u8* d,u32 len){
    if(len<sizeof(struct iwx_tlc_update_notif))return;
    const struct iwx_tlc_update_notif* n=(const struct iwx_tlc_update_notif*)d;
    if(n->sta_id==0&&(n->flags&IWX_TLC_NOTIF_FLAG_RATE))iwx_tlc_rate=n->rate;
}
