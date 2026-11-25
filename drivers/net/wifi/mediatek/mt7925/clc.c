/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Derived from Linux drivers/net/wireless/mediatek/mt76 (mt7925/mcu.c mt7925_load_clc, __mt7925_mcu_set_clc,
 * mt7925_mcu_set_clc, mt7925_mcu_read_eeprom; mt7925/mt7925.h CLC record structs).
 * Copyright (C) 2023 MediaTek Inc.
 * Licence text: drivers/net/wifi/mediatek/mt7921/LICENCE.BSD-3-Clause-Clear
 */
#include "drivers/net/wifi/mediatek/mt7925/mt7925.h"
#include "drivers/net/wifi/mediatek/mt7921/mcu/mcu.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/core/klog.h"
static u32 m5c_le32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static u8* m5c_rec[MT7925_CLC_NREC]; static u32 m5c_len[MT7925_CLC_NREC];
int mt7925_clc_on=1, mt7925_clc_fail, mt7925_clc_sent; u8 mt7925_clc_chan_conf=0xff; char mt7925_clc_cc[3]; const char* mt7925_clc_err;
int mt7925_clc_find(const u8* fw,u32 n,u32* off,u32* len){
    if(!fw||n<sizeof(struct mt_fw_trailer))return -1;
    const struct mt_fw_trailer* t=(const struct mt_fw_trailer*)(const void*)(fw+n-sizeof(*t)); u32 nr=t->n_region;
    if(!nr||(u64)nr*sizeof(struct mt_fw_region)+sizeof(*t)>n)return -1;
    const u8* r0=fw+n-sizeof(*t)-nr*sizeof(struct mt_fw_region); u64 o=0;
    for(u32 i=0;i<nr;i++){ const u8* r=r0+i*sizeof(struct mt_fw_region); u32 l=m5c_le32(r+20);
        if((r[24]&FW_FEATURE_NON_DL)&&r[25]==MT7925_FW_TYPE_CLC){ if(o+l>n-sizeof(*t)-nr*sizeof(struct mt_fw_region))return -1; *off=(u32)o; *len=l; return 0; }
        o+=l; }
    return -1; }
int mt7925_clc_split(const u8* b,u32 len,u8 encap,const u8** rec,u32* rl){
    int got=0; for(int i=0;i<MT7925_CLC_NREC;i++){ rec[i]=0; rl[i]=0; }
    for(u32 o=0;o<len;){ if(len-o<16)return -1; const u8* c=b+o; u32 cl=m5c_le32(c); if(cl<16||cl>len-o)return -1;
        u8 idx=c[4]; if(idx>=MT7925_CLC_MAXIDX)break;
        if(idx<MT7925_CLC_NREC&&!rec[idx]&&(c[7]&3)==encap){ rec[idx]=c; rl[idx]=cl; got++; }
        o+=cl; }
    return got; }
static int m5c_rules(const u8* rec,u32 rl,const u8** p,const u8** e){
    if(!rec||rl<24)return -1; const u8* d=rec+16; u32 ns=rec[8], end=m5c_le32(d+4);
    if(16u+16u*ns>rl||end>rl-16||end<16u*ns)return -1; *p=d+16u*ns; *e=d+end; return 0; }
int mt7925_clc_countries(const u8* rec,u32 rl,char* out,int cap){
    const u8 *p,*e; int n=0,k=0; if(cap<1)return 0; out[0]=0; if(m5c_rules(rec,rl,&p,&e)<0)return -1;
    for(;p+8<=e;p+=8){ int dup=0; for(int j=0;j+1<k;j+=3)if(out[j]==(char)p[0]&&out[j+1]==(char)p[1])dup=1; if(dup)continue; n++;
        if(k+3<cap){ if(k)out[k-1]=' '; out[k++]=(char)p[0]; out[k++]=(char)p[1]; out[k++]=0; } }
    return n; }
u32 mt7925_clc_req(const u8* rec,u32 rl,u8 idx,const char* cc,u8 env,int nth,u8* out,u32 cap,u8* flag){
    const u8 *p,*e; if(m5c_rules(rec,rl,&p,&e)<0)return 0; const u8* d=rec+16; u32 ns=rec[8];
    for(;p+8<=e;p+=8){ if(p[0]!=(u8)cc[0]||p[1]!=(u8)cc[1])continue; if(nth--)continue;
        if(!p[4]||p[4]>ns)return 0; const u8* s=d+16u*(p[4]-1u); u32 so=m5c_le32(s+4), sl=m5c_le32(s+8);
        if(so>rl-16||sl>rl-16-so||MT7925_CLC_REQ+sl>cap)return 0;
        mt_zero(out,MT7925_CLC_REQ); mt_put16(out+4,3); mt_put16(out+6,(u16)(MT7925_CLC_REQ+sl-4)); out[8]=rec[5]; mt_put16(out+10,(u16)sl); out[12]=idx; out[13]=env;
        out[16]=p[0]; out[17]=p[1]; out[18]=p[2]; out[19]=p[3]; mt_copy(out+MT7925_CLC_REQ,d+so,sl); if(flag)*flag=rec[5]==1?0xff:p[5]; return MT7925_CLC_REQ+sl; }
    return 0; }
static int m5c_count(const u8* rec,u32 rl,const char* cc){ const u8 *p,*e; int n=0; if(m5c_rules(rec,rl,&p,&e)<0)return 0; for(;p+8<=e;p+=8)if(p[0]==(u8)cc[0]&&p[1]==(u8)cc[1])n++; return n; }
static int m5c_idx(struct mt7921_softc* sc,int i,const char* cc,u8 env){
    static u8 req[MT_TXBUF_SIZE]; int n=m5c_count(m5c_rec[i],m5c_len[i],cc);
    for(int k=0;k<n;k++){ u8 fl=0xff; u32 l=mt7925_clc_req(m5c_rec[i],m5c_len[i],(u8)i,cc,env,k,req,sizeof req-sizeof(struct mt_mcu_uni_txd),&fl);
        if(!l){ mt7925_clc_err="country power table entry is damaged or too large"; return -1; }
        if(mt7925_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_SET_POWER_LIMIT),req,l,1)<0){ mt7925_clc_err="firmware rejected the country power table"; return -1; }
        mt7925_clc_chan_conf=fl; }
    return n; }
int mt7925_clc_apply(struct mt7921_softc* sc,const char* cc){
    if(!sc||sc->state!=MT_ST_READY||!cc||!cc[0]||!cc[1])return -1;
    int r=0, own=0, bad=0;
    for(int i=0;i<MT7925_CLC_NREC;i++){ if(!m5c_rec[i])continue; int h=m5c_idx(sc,i,cc,MT7925_ENV_ANY); if(h<0)return -1; if(h){ own++; r++; continue; }
        mt7925_clc_chan_conf=0xff; h=m5c_idx(sc,i,"00",MT7925_ENV_INDOOR); if(h<0)return -1; if(h)r++; else bad++; }
    if(!r||bad){ mt7925_clc_err="no power table for this country in the firmware"; return -1; }
    if(!own)cc="00";
    mt7925_clc_cc[0]=cc[0]; mt7925_clc_cc[1]=cc[1]; mt7925_clc_cc[2]=0; mt7925_clc_sent++; mt7925_clc_err=0; return r; }
int mt7925_hw_encap(struct mt7921_softc* sc,u8* v){
    u8 req[28]; const u8* p; int n; mt_zero(req,sizeof req); mt_put16(req+4,1); mt_put16(req+6,24); mt_put32(req+8,MT7925_EE_HW_TYPE&~15u);
    if(mt_mcu_query(sc,MCU_UNI_CMD(MCU_UNI_CMD_EFUSE_CTRL)|MCU_FIELD_QUERY,req,sizeof req,&p,&n)<0||n<64)return -1;
    *v=p[48+(MT7925_EE_HW_TYPE&15u)]&3; return 0; }
int mt7925_clc_load(struct mt7921_softc* sc){
    u8 enc; u8* fw; u32 n,o,l; const u8* rec[MT7925_CLC_NREC]; u32 rl[MT7925_CLC_NREC];
    if(m5c_rec[0]||m5c_rec[1])return 0;
    if(mt7925_hw_encap(sc,&enc)<0){ mt7925_clc_err="hardware type not readable"; return -1; }
    if(!mt7921_firmware_read||mt7921_firmware_read(MT7925_RAM_PATH,&fw,&n)<0||!fw){ mt7925_clc_err="RAM code file missing"; return -1; }
    int r=-1;
    if(mt7925_clc_find(fw,n,&o,&l)<0)mt7925_clc_err="no country table in this firmware";
    else if(mt7925_clc_split(fw+o,l,enc,rec,rl)<=0)mt7925_clc_err="country table does not match this card";
    else { r=0; for(int i=0;i<MT7925_CLC_NREC;i++)if(rec[i]){ u8* c=(u8*)kmalloc(rl[i]); if(!c){ r=-1; break; } mt_copy(c,rec[i],rl[i]); m5c_rec[i]=c; m5c_len[i]=rl[i]; } }
    kfree(fw); return r; }
int mt7925_clc_list(char* out,int cap){ for(int i=0;i<MT7925_CLC_NREC;i++)if(m5c_rec[i])return mt7925_clc_countries(m5c_rec[i],m5c_len[i],out,cap); if(cap>0)out[0]=0; return 0; }
static char m5c_want[2];
void mt7925_clc_reset(void){ m5c_want[0]=m5c_want[1]=0; mt7925_clc_sent=0; mt7925_clc_cc[0]=0; }
void mt7925_clc_sync(struct mt7921_softc* sc,const char* cc){
    if(!mt7925_clc_on||mt7925_clc_fail>=3||!sc||sc->state!=MT_ST_READY||!cc||!cc[0]||!cc[1]||cc[0]=='-')return;
    char* want=m5c_want; if(want[0]==cc[0]&&want[1]==cc[1])return; want[0]=cc[0]; want[1]=cc[1];
    if(mt7925_clc_load(sc)<0||mt7925_clc_apply(sc,cc)<0){ { want[0]=0; if(++mt7925_clc_fail==3)klog2("mt7925: country power tables off after 3 failures: ",mt7925_clc_err?mt7925_clc_err:"error"); } } }
int mt7925_scan_list(const u8* ch,int n,u8* oc,u8* ob,int cap,int six){ int k=0; for(int i=0;i<n&&k<cap;i++){ oc[k]=ch[i]; ob[k]=ch[i]<=14?MT7925_BAND_2G:MT7925_BAND_5G; k++; }
    for(int c=5;six&&c<=229&&k<cap;c+=16){ oc[k]=(u8)c; ob[k]=MT7925_BAND_6G; k++; } return k; }
