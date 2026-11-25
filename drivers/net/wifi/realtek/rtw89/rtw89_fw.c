/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Derived from Linux drivers/net/wireless/realtek/rtw89/fw.c (rtw89_mfw_recognize, rtw89_fw_hdr_parser_v0; GPL-2.0 OR BSD-3-Clause, used under BSD-3-Clause).
 * Copyright(c) 2019-2020  Realtek Corporation
 * Licence text: drivers/net/wifi/realtek/rtw89/LICENCE.BSD-3-Clause
 */
#include "drivers/net/wifi/realtek/rtw89/rtw89.h"

static u32 r89_le32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }

int rtw89_mfw_pick(const u8* d,u32 len,u8 cv,u8 type,u32* off,u32* size,u8* got_cv){
    if(!d||len<16)return -1;
    if(d[0]!=RTW89_MFW_SIG){ if(type!=RTW89_FW_NORMAL)return -2; *off=0; *size=len; if(got_cv)*got_cv=cv; return 0; }
    u32 n=d[1]; if(!n||16+16*n>len)return -1;
    int best=-1; u8 bcv=0;
    for(u32 i=0;i<n;i++){ const u8* e=d+16+16*i; if(e[1]!=type||e[2])continue; if(e[0]<=cv&&(best<0||bcv<e[0])){ best=(int)i; bcv=e[0]; } }
    if(best<0)return -2;
    const u8* e=d+16+16*(u32)best; u32 o=r89_le32(e+4), s=r89_le32(e+8);
    if(o<16+16*n||o>len||s>len-o||s<32)return -1;
    *off=o; *size=s; if(got_cv)*got_cv=bcv; return 0; }

int rtw89_fw_hdr_parse(const u8* d,u32 len,struct rtw89_fw_info* fi){
    if(!d||!fi||len<32)return -1;
    u32 w[8]; for(int i=0;i<8;i++)w[i]=r89_le32(d+4*i);
    fi->hdr_ver=(u8)(w[3]>>24); if(fi->hdr_ver>1)return -3;
    fi->major=(u8)w[1]; fi->minor=(u8)(w[1]>>8); fi->sub=(u8)(w[1]>>16); fi->idx=(u8)(w[1]>>24); fi->commit=w[2];
    fi->month=(u8)w[4]; fi->day=(u8)(w[4]>>8); fi->hour=(u8)(w[4]>>16); fi->min=(u8)(w[4]>>24); fi->year=(u16)(fi->hdr_ver?(w[5]&0xFFFF):w[5]);
    int n=(int)((w[6]>>8)&0xFF); if(!n||n>FWDL_SECTION_MAX_NUM)return -4;
    u32 base=32+16*(u32)n; if(base>len)return -1;
    fi->dyn=(u8)((w[7]>>16)&1);
    if(fi->hdr_ver)fi->hdr_len=(w[5]>>16)&0xFFFF; else fi->hdr_len=fi->dyn?((w[3]>>16)&0xFF):base;
    if(fi->hdr_len<base||fi->hdr_len>len)return -5;
    if(!fi->hdr_ver&&fi->dyn&&r89_le32(d+base)!=fi->hdr_len-base)return -5;
    u32 pos=fi->hdr_len; fi->nsec=n;
    for(int i=0;i<n;i++){ const u8* s=d+32+16*i; u32 w0=r89_le32(s), w1=r89_le32(s+4), w2=r89_le32(s+8);
        struct rtw89_fw_section* x=&fi->sec[i];
        x->type=(u8)((w1>>24)&0xF); x->len=w1&0xFFFFFF; if(w1&(1u<<28))x->len+=FWDL_SECTION_CHKSUM_LEN;
        x->redl=(u8)((w1>>29)&1); x->dladdr=w0&0x1FFFFFFFu; x->off=pos; x->mssc=0;
        u32 ml=0; if(x->type==FWDL_SECURITY_SECTION_TYPE){ x->mssc=fi->hdr_ver?(w2&0xFF):w2; if(x->mssc>64)return -6; ml=x->mssc*FWDL_SECURITY_SIGLEN; }
        if(x->len>len-pos||ml>len-pos-x->len)return -7;
        pos+=x->len+ml; }
    if(pos!=len)return -8;
    return 0; }

int rtw89_fw_parse(const u8* d,u32 len,u8 cv,struct rtw89_fw_info* fi){
    u32 o=0,s=0; u8 gcv=0; u8 t=RTW89_FW_NORMAL_CE;
    int r=rtw89_mfw_pick(d,len,cv,t,&o,&s,&gcv);
    if(r==-2){ t=RTW89_FW_NORMAL; r=rtw89_mfw_pick(d,len,cv,t,&o,&s,&gcv); }
    if(r<0)return r;
    r=rtw89_fw_hdr_parse(d+o,s,fi); if(r<0)return r;
    fi->off=o; fi->size=s; fi->cv=gcv; fi->type=t; return 0; }
