/*
 * Goodix Moc driver for libfprint
 * Copyright (C) 2019 Shenzhen Goodix Technology Co., Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * NoovexOS port of libfprint drivers/goodixmoc (goodix.c, goodix_proto.c, goodix_proto.h):
 * used under GPL-3.0-or-later as permitted by LGPL-2.1 section 3.
 */
#include "include/nvx/base.h"
#include "include/nvx/drivers/input/fingerprint/fprint.h"
#define GX_OUT 0x01
#define GX_IN 0x83
#define GX_ACK 0xAA
#define GX_TID 32
static const u8 gx_cfg0[26]={0x00,0x00,0x64,0x50,0x0f,0x41,0x08,0x0a,0x18,0x00,0x00,0x23,0x00,0x00,0x01,0x01,0x00,0x01,0x01,0x01,0x01,0x00,0x01,0x01,0x05,0x05};
static struct { fp_xport_t x; int ok; u8 seq; u8 rx[2048]; int rlen, res; const u8* rb; u8 tid[GX_TID]; int stages; } gx;
u8 gx_crc8(const u8* d,u32 n){ u32 c=0; for(u32 j=0;j<n;j++){ c^=(u32)d[j]<<8; for(int i=0;i<8;i++){ if(c&0x8000)c^=0x1070u<<3; c<<=1; } } c>>=8; return (u8)~c; }
u32 gx_crc32(const u8* d,u32 n){ u32 c=0xFFFFFFFFu; for(u32 i=0;i<n;i++){ c^=d[i]; for(int k=0;k<8;k++)c=(c>>1)^(0xEDB88320u&(0u-(c&1u))); } return ~c; }
int gx_build(u8* o,int cap,u8 c0,u8 c1,u8 seq,const u8* pl,int n){
    if(n<0||n+12>cap)return -1;
    u32 l=(u32)n+4; o[0]=c0; o[1]=c1; o[2]=0; o[3]=seq; o[4]=(u8)l; o[5]=(u8)(l>>8); o[6]=gx_crc8(o,6); o[7]=(u8)~o[6];
    for(int i=0;i<n;i++)o[8+i]=pl[i];
    u32 c=gx_crc32(o,(u32)(8+n)); for(int i=0;i<4;i++)o[8+n+i]=(u8)(c>>(8*i)); return n+12; }
int gx_parse(const u8* b,int g,u8* c0,const u8** body,int* blen){
    if(g<12)return -1;
    u32 l=(u32)(b[4]|b[5]<<8); if(l<4||8+l>(u32)g)return -1;
    u32 c=gx_crc32(b,8+l-4), w=(u32)b[8+l-4]|(u32)b[8+l-3]<<8|(u32)b[8+l-2]<<16|(u32)b[8+l-1]<<24; if(c!=w)return -1;
    *c0=b[0]; *body=b+8; *blen=(int)l-4; return *blen<1?-1:0; }
static int gx_send(u8 c0,u8 c1,const u8* pl,int n){
    static u8 tx[512]; if(!gx.ok)return -1; int len=gx_build(tx,(int)sizeof tx,c0,c1,gx.seq++,pl,n); if(len<0)return -1;
    if(gx.x.out(gx.x.ctx,GX_OUT,tx,len,1000)!=len)return -1;
    u8 h; const u8* b; int bl;
    int g=gx.x.in(gx.x.ctx,GX_IN,gx.rx,(int)sizeof gx.rx,2000); if(gx_parse(gx.rx,g,&h,&b,&bl)||h!=GX_ACK||bl<2||b[1]!=c0)return -1;
    return 0; }
static int gx_data(u8 c0,int ms){
    u8 h; const u8* b; int bl; int g=gx.x.in(gx.x.ctx,GX_IN,gx.rx,(int)sizeof gx.rx,ms); if(g<=0)return 0;
    if(gx_parse(gx.rx,g,&h,&b,&bl)||h!=c0)return -1;
    gx.res=b[0]; gx.rb=b+1; gx.rlen=bl-1; return 1; }
static int gx_cmd(u8 c0,u8 c1,const u8* pl,int n,int data_ms){ if(gx_send(c0,c1,pl,n))return -1; return gx_data(c0,data_ms)==1?0:-1; }
int goodixfp_open(const fp_xport_t* x){ gx.x=*x; gx.ok=1; gx.seq=0; gx.stages=8; return 1; }
void goodixfp_set_stages(int n){ gx.stages=n<1?8:n>20?20:n; }
int goodixfp_init(void){
    u8 z=0; if(gx_cmd(0xD0,0x00,&z,1,1000))return 0;
    u8 cfg[128]; for(int i=0;i<128;i++)cfg[i]=0; for(int i=0;i<26;i++)cfg[i]=gx_cfg0[i]; cfg[6]=(u8)gx.stages; cfg[26]=1;
    u32 c=gx_crc32(cfg,124); for(int i=0;i<4;i++)cfg[124+i]=(u8)(c>>(8*i));
    if(gx_cmd(0xC0,0x01,cfg,128,1000))return 0;
    return 1; }
int goodixfp_result(void){ return gx.res; }
static int gx_finger_up(void){ for(int t=0;t<20;t++){ u8 z=0; if(gx_cmd(0xB0,0x02,&z,1,10000))return 0; if(gx.res==0)return 1; } return 0; }
static int gx_tpl(const u8* p,int n,u8* uid,int cap){
    if(n<69||p[0]!=67)return -1;
    int sz=p[68]; if(sz>56||69+sz>n)return -1;
    int k=sz<cap?sz:cap; for(int i=0;i<k;i++)uid[i]=p[69+i]; return k; }
int goodixfp_enroll(const char* uid,void (*prog)(int pct)){
    u8 z=0, d[3]; int ul=0; while(uid[ul]&&ul<56)ul++;
    if(gx_cmd(0xA6,0x00,&z,1,2000))return 0;
    if(gx_cmd(0xA1,0x00,&z,1,2000)||gx.res!=0||gx.rlen<GX_TID)return 0;
    for(int i=0;i<GX_TID;i++)gx.tid[i]=gx.rb[i];
    int st=0;
    for(int tries=0;st<gx.stages&&tries<gx.stages*4;tries++){
        d[0]=0; d[1]=gx_cfg0[4]; d[2]=gx_cfg0[5]; if(gx_cmd(0xA2,0x00,d,3,15000))return 0;
        if(gx.res>=0x80||gx.rlen<2||gx.rb[0]<gx_cfg0[4]||gx.rb[1]<gx_cfg0[5])continue;
        d[0]=1; d[1]=gx_cfg0[2]; d[2]=gx_cfg0[3]; if(gx_cmd(0xA0,0x00,d,3,2000))return 0;
        if(gx.rlen>=2&&gx.rb[1]<=gx_cfg0[3]&&gx.res<0x80&&gx.res!=0x73){ st++; if(prog)prog(st*100/gx.stages); }
        if(!gx_finger_up())return 0; }
    if(st<gx.stages)return 0;
    d[0]=0; d[1]=gx_cfg0[2]; d[2]=gx_cfg0[3]; if(gx_cmd(0xA3,0x00,d,3,2000))return 0;
    if(gx.res!=0)return -3;
    u8 f[2+70+56]; int fl=70+ul, o=0; f[o++]=(u8)fl; f[o++]=(u8)(fl>>8); f[o++]=67; f[o++]=1; f[o++]=1; f[o++]=0;
    for(int i=0;i<32;i++){ f[o++]=0; }
    for(int i=0;i<GX_TID;i++){ f[o++]=gx.tid[i]; }
    f[o++]=(u8)ul; for(int i=0;i<ul;i++)f[o++]=(u8)uid[i]; f[o++]=0;
    if(gx_cmd(0xA4,0x00,f,o,5000))return 0;
    return gx.res==0?1:gx.res==0x8F?-2:0; }
static int gx_commit(const char* uid){
    u8 d[3]; int ul=0; while(uid[ul]&&ul<56)ul++;
    d[0]=0; d[1]=gx_cfg0[2]; d[2]=gx_cfg0[3]; if(gx_cmd(0xA3,0x00,d,3,2000))return -1;
    if(gx.res!=0)return -3;
    u8 f[2+70+56]; int fl=70+ul, o=0; f[o++]=(u8)fl; f[o++]=(u8)(fl>>8); f[o++]=67; f[o++]=1; f[o++]=1; f[o++]=0;
    for(int i=0;i<32;i++){ f[o++]=0; }
    for(int i=0;i<GX_TID;i++){ f[o++]=gx.tid[i]; }
    f[o++]=(u8)ul; for(int i=0;i<ul;i++)f[o++]=(u8)uid[i]; f[o++]=0;
    if(gx_cmd(0xA4,0x00,f,o,5000))return -1;
    return gx.res==0?1:gx.res==0x8F?-2:-1; }
#define GXE_PMS 50
static struct { int st, stg, tries, ups, left; char uid[57]; } gxe;
int goodixfp_enroll_begin(const char* uid){
    u8 z=0; int ul=0; while(uid[ul]&&ul<56){ gxe.uid[ul]=uid[ul]; ul++; } gxe.uid[ul]=0; gxe.st=0;
    if(gx_cmd(0xA6,0x00,&z,1,2000))return -1;
    if(gx_cmd(0xA1,0x00,&z,1,2000)||gx.res!=0||gx.rlen<GX_TID)return -1;
    for(int i=0;i<GX_TID;i++)gx.tid[i]=gx.rb[i];
    gxe.stg=0; gxe.tries=0; gxe.st=1; return 0; }
int goodixfp_enroll_step(int* pct){
    u8 d[3], z=0; int r;
    if(pct)*pct=gxe.stg*100/gx.stages;
    switch(gxe.st){
    case 1: if(gxe.stg>=gx.stages){ gxe.st=0; return gx_commit(gxe.uid); }
        if(gxe.tries>=gx.stages*4){ gxe.st=0; return -1; }
        d[0]=0; d[1]=gx_cfg0[4]; d[2]=gx_cfg0[5]; if(gx_send(0xA2,0x00,d,3)){ gxe.st=0; return -1; } gxe.left=15000/GXE_PMS; gxe.st=2; return 0;
    case 2: r=gx_data(0xA2,GXE_PMS); if(!r){ if(--gxe.left>0)return 0; gxe.st=0; return -1; } if(r<0){ gxe.st=0; return -1; }
        if(gx.res>=0x80||gx.rlen<2||gx.rb[0]<gx_cfg0[4]||gx.rb[1]<gx_cfg0[5]){ gxe.tries++; gxe.st=1; return 0; }
        d[0]=1; d[1]=gx_cfg0[2]; d[2]=gx_cfg0[3]; if(gx_cmd(0xA0,0x00,d,3,2000)){ gxe.st=0; return -1; }
        if(gx.rlen>=2&&gx.rb[1]<=gx_cfg0[3]&&gx.res<0x80&&gx.res!=0x73)gxe.stg++;
        if(pct){ *pct=gxe.stg*100/gx.stages; } gxe.ups=0; gxe.st=3; return 0;
    case 3: if(gxe.ups>=20||gx_send(0xB0,0x02,&z,1)){ gxe.st=0; return -1; } gxe.ups++; gxe.left=10000/GXE_PMS; gxe.st=4; return 0;
    case 4: r=gx_data(0xB0,GXE_PMS); if(!r){ if(--gxe.left>0)return 0; gxe.st=0; return -1; } if(r<0){ gxe.st=0; return -1; }
        if(gx.res!=0){ gxe.st=3; return 0; } gxe.tries++; gxe.st=1; return 0; }
    return -1; }
int goodixfp_verify(const char* uid){
    u8 d[3]={1,gx_cfg0[10],gx_cfg0[11]}, nonce[GX_TID]; for(int i=0;i<GX_TID;i++)nonce[i]=0;
    if(gx_cmd(0xA2,0x00,d,3,15000))return -1;
    if(gx.res>=0x80)return -3;
    if(gx_cmd(0xA5,0x00,nonce,GX_TID,5000))return -1;
    int m=0;
    if(gx.res==0&&gx.rlen>=9){ int fs=gx.rb[7]|gx.rb[8]<<8; if(9+fs<=gx.rlen){ u8 u[56]; int n=gx_tpl(gx.rb+9,fs,u,56), ul=0; while(uid[ul])ul++;
        if(n==ul){ m=1; for(int i=0;i<n;i++) if(u[i]!=(u8)uid[i])m=0; } } }
    gx_finger_up(); return m; }
int goodixfp_delete_all(void){ if(gx_cmd(0xA7,0x01,0,0,5000))return 0; return gx.res==0; }
const u8* goodixfp_tid(void){ return gx.tid; }
int goodixfp_delete(const char* uid,const u8* tid){
    u8 f[2+70+56]; int ul=0; while(uid[ul]&&ul<56)ul++; int fl=70+ul, o=0; f[o++]=(u8)fl; f[o++]=(u8)(fl>>8); f[o++]=67; f[o++]=1; f[o++]=1; f[o++]=0;
    for(int i=0;i<32;i++){ f[o++]=0; }
    for(int i=0;i<GX_TID;i++){ f[o++]=tid[i]; }
    f[o++]=(u8)ul; for(int i=0;i<ul;i++)f[o++]=(u8)uid[i]; f[o++]=0;
    if(gx_cmd(0xA7,0x04,f,o,5000))return 0;
    return gx.res==0||gx.res==0x9C; }
