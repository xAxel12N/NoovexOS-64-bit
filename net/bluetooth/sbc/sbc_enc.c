/*
 * Bluetooth low-complexity, subband codec (SBC)
 *
 * Copyright (C) 2017  Aurelien Jacobs <aurel@gnuage.org>
 * Copyright (C) 2012-2013  Intel Corporation
 * Copyright (C) 2008-2010  Nokia Corporation
 * Copyright (C) 2004-2010  Marcel Holtmann <marcel@holtmann.org>
 * Copyright (C) 2004-2005  Henryk Ploetz <henryk@ploetzli.ch>
 * Copyright (C) 2005-2008  Brad Midgley <bmidgley@xmission.com>
 *
 * Ported to NoovexOS from FFmpeg libavcodec/sbcenc.c, sbcdsp.c and sbc.c
 * (generic C paths, libavcodec glue replaced by a small frame API).
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include "include/nvx/base.h"
#include "net/bluetooth/sbc/sbc.h"
#include "net/bluetooth/sbc/sbc_tables.h"
#define SCALE_OUT_BITS 15
static const int sbc_offset4[4][4]={ {-1,0,0,0},{-2,0,0,1},{-2,0,0,1},{-2,0,0,1} };
static const int sbc_offset8[4][8]={ {-2,0,0,0,0,0,0,1},{-3,0,0,0,0,0,1,2},{-4,0,0,0,0,0,1,2},{-4,0,0,0,0,0,1,2} };
static int sclz(u32 x){ return __builtin_clz(x); }
static int32_t sabs(int32_t v){ return v<0?-v:v; }
static void cp16(int16_t* d,const int16_t* s,int n){ for(int i=0;i<n;i++)d[i]=s[i]; }
static int16_t rn16(const u8* p){ return (int16_t)(p[0]|(p[1]<<8)); }
u8 sbc_crc8(const u8* d,int len){
    u8 crc=0x0F; int nb=len>>3, bl=len&7;
    for(int i=0;i<nb;i++){ crc^=d[i]; for(int k=0;k<8;k++)crc=(u8)(crc&0x80?(crc<<1)^0x1D:crc<<1); }
    if(bl){ u8 bits=d[nb]; while(bl--){ int8_t m=(int8_t)(bits^crc); crc=(u8)((crc<<1)^((m>>7)&0x1D)); bits<<=1; } }
    return crc;
}
static void bitneed_of(const sbcenc_t* f,int ch,int bn[8],int* maxb){
    int sb_n=f->subbands;
    for(int sb=0;sb<sb_n;sb++){
        if(f->alloc==SBC_AM_SNR)bn[sb]=(int)f->sf[ch][sb];
        else if(f->sf[ch][sb]==0)bn[sb]=-5;
        else { int l=(int)f->sf[ch][sb]-(sb_n==4?sbc_offset4[f->freq][sb]:sbc_offset8[f->freq][sb]); bn[sb]=l>0?l/2:l; }
        if(bn[sb]>*maxb)*maxb=bn[sb]; }
}
static void calc_bits(const sbcenc_t* f,int bits[2][8]){
    int sbn=f->subbands, bp=f->bitpool;
    if(f->mode==SBC_MODE_MONO||f->mode==SBC_MODE_DUAL){
        for(int ch=0;ch<f->channels;ch++){
            int bn[8], mx=0, bc=0, sc=0, bs; bitneed_of(f,ch,bn,&mx); bs=mx+1;
            do{ bs--; bc+=sc; sc=0; for(int sb=0;sb<sbn;sb++){ if(bn[sb]>bs+1&&bn[sb]<bs+16)sc++; else if(bn[sb]==bs+1)sc+=2; } }while(bc+sc<bp);
            if(bc+sc==bp){ bc+=sc; bs--; }
            for(int sb=0;sb<sbn;sb++){ if(bn[sb]<bs+2)bits[ch][sb]=0; else { bits[ch][sb]=bn[sb]-bs; if(bits[ch][sb]>16)bits[ch][sb]=16; } }
            for(int sb=0;bc<bp&&sb<sbn;sb++){ if(bits[ch][sb]>=2&&bits[ch][sb]<16){ bits[ch][sb]++; bc++; } else if(bn[sb]==bs+1&&bp>bc+1){ bits[ch][sb]=2; bc+=2; } }
            for(int sb=0;bc<bp&&sb<sbn;sb++) if(bits[ch][sb]<16){ bits[ch][sb]++; bc++; }
        }
        return;
    }
    int bn[2][8], mx=0, bc=0, sc=0, bs, ch, sb;
    bitneed_of(f,0,bn[0],&mx); bitneed_of(f,1,bn[1],&mx); bs=mx+1;
    do{ bs--; bc+=sc; sc=0; for(ch=0;ch<2;ch++)for(sb=0;sb<sbn;sb++){ if(bn[ch][sb]>bs+1&&bn[ch][sb]<bs+16)sc++; else if(bn[ch][sb]==bs+1)sc+=2; } }while(bc+sc<bp);
    if(bc+sc==bp){ bc+=sc; bs--; }
    for(ch=0;ch<2;ch++)for(sb=0;sb<sbn;sb++){ if(bn[ch][sb]<bs+2)bits[ch][sb]=0; else { bits[ch][sb]=bn[ch][sb]-bs; if(bits[ch][sb]>16)bits[ch][sb]=16; } }
    ch=0; sb=0;
    while(bc<bp){ if(bits[ch][sb]>=2&&bits[ch][sb]<16){ bits[ch][sb]++; bc++; } else if(bn[ch][sb]==bs+1&&bp>bc+1){ bits[ch][sb]=2; bc+=2; }
        if(ch==1){ ch=0; sb++; if(sb>=sbn)break; } else ch=1; }
    ch=0; sb=0;
    while(bc<bp){ if(bits[ch][sb]<16){ bits[ch][sb]++; bc++; }
        if(ch==1){ ch=0; sb++; if(sb>=sbn)break; } else ch=1; }
}
static void analyze(const int16_t* in,int32_t* out,const int16_t* consts,unsigned sbn){
    int32_t t1[8]; int16_t t2[8];
    for(unsigned i=0;i<sbn;i++)t1[i]=1<<(SBC_PROTO_FIXED_SCALE-1);
    for(unsigned hop=0;hop<10*sbn;hop+=2*sbn) for(unsigned i=0;i<2*sbn;i++)t1[i>>1]+=in[hop+i]*consts[hop+i];
    for(unsigned i=0;i<sbn;i++)t2[i]=(int16_t)(t1[i]>>SBC_PROTO_FIXED_SCALE);
    for(int i=0;i<8;i++)t1[i]=0;
    for(unsigned i=0;i<sbn/2;i++) for(unsigned j=0;j<2*sbn;j++)t1[j>>1]+=t2[i*2+(j&1)]*consts[10*sbn+i*2*sbn+j];
    for(unsigned i=0;i<sbn;i++)out[i]=t1[i]>>(SBC_COS_TABLE_FIXED_SCALE-SCALE_OUT_BITS);
}
static void analyze_4b_4s(int16_t* x,int32_t* out,int st){
    analyze(x+12,out,sbcdsp_analysis_consts_fixed4_simd_odd,4); out+=st;
    analyze(x+8,out,sbcdsp_analysis_consts_fixed4_simd_even,4); out+=st;
    analyze(x+4,out,sbcdsp_analysis_consts_fixed4_simd_odd,4); out+=st;
    analyze(x,out,sbcdsp_analysis_consts_fixed4_simd_even,4);
}
static void analyze_4b_8s(int16_t* x,int32_t* out,int st){
    analyze(x+24,out,sbcdsp_analysis_consts_fixed8_simd_odd,8); out+=st;
    analyze(x+16,out,sbcdsp_analysis_consts_fixed8_simd_even,8); out+=st;
    analyze(x+8,out,sbcdsp_analysis_consts_fixed8_simd_odd,8); out+=st;
    analyze(x,out,sbcdsp_analysis_consts_fixed8_simd_even,8);
}
static int input_4s(int pos,const u8* pcm,int16_t X[2][SBC_X_BUFFER_SIZE],int ns,int nch){
    if(pos<ns){ for(int c=0;c<nch;c++)cp16(&X[c][SBC_X_BUFFER_SIZE-40],&X[c][pos],36); pos=SBC_X_BUFFER_SIZE-40; }
    for(;ns>=8;ns-=8,pcm+=16*nch){ pos-=8;
        for(int c=0;c<nch;c++){ int16_t* x=&X[c][pos];
            x[0]=rn16(pcm+14*nch+2*c); x[1]=rn16(pcm+6*nch+2*c); x[2]=rn16(pcm+12*nch+2*c); x[3]=rn16(pcm+8*nch+2*c);
            x[4]=rn16(pcm+0*nch+2*c); x[5]=rn16(pcm+4*nch+2*c); x[6]=rn16(pcm+2*nch+2*c); x[7]=rn16(pcm+10*nch+2*c); } }
    return pos;
}
static int input_8s(int pos,const u8* pcm,int16_t X[2][SBC_X_BUFFER_SIZE],int ns,int nch){
    if(pos<ns){ for(int c=0;c<nch;c++)cp16(&X[c][SBC_X_BUFFER_SIZE-72],&X[c][pos],72); pos=SBC_X_BUFFER_SIZE-72; }
    if(pos%16==8){ pos-=8; ns-=8;
        for(int c=0;c<nch;c++){ int16_t* x=&X[c][pos];
            x[0]=rn16(pcm+14*nch+2*c); x[2]=rn16(pcm+12*nch+2*c); x[3]=rn16(pcm+0*nch+2*c); x[4]=rn16(pcm+10*nch+2*c);
            x[5]=rn16(pcm+2*nch+2*c); x[6]=rn16(pcm+8*nch+2*c); x[7]=rn16(pcm+4*nch+2*c); x[8]=rn16(pcm+6*nch+2*c); }
        pcm+=16*nch; }
    for(;ns>=16;ns-=16,pcm+=32*nch){ pos-=16;
        for(int c=0;c<nch;c++){ int16_t* x=&X[c][pos];
            x[0]=rn16(pcm+30*nch+2*c); x[1]=rn16(pcm+14*nch+2*c); x[2]=rn16(pcm+28*nch+2*c); x[3]=rn16(pcm+16*nch+2*c);
            x[4]=rn16(pcm+26*nch+2*c); x[5]=rn16(pcm+18*nch+2*c); x[6]=rn16(pcm+24*nch+2*c); x[7]=rn16(pcm+20*nch+2*c);
            x[8]=rn16(pcm+22*nch+2*c); x[9]=rn16(pcm+6*nch+2*c); x[10]=rn16(pcm+12*nch+2*c); x[11]=rn16(pcm+0*nch+2*c);
            x[12]=rn16(pcm+10*nch+2*c); x[13]=rn16(pcm+2*nch+2*c); x[14]=rn16(pcm+8*nch+2*c); x[15]=rn16(pcm+4*nch+2*c); } }
    if(ns==8){ pos-=8;
        for(int c=0;c<nch;c++){ int16_t* x=&X[c][pos];
            x[-7]=rn16(pcm+14*nch+2*c); x[1]=rn16(pcm+6*nch+2*c); x[2]=rn16(pcm+12*nch+2*c); x[3]=rn16(pcm+0*nch+2*c);
            x[4]=rn16(pcm+10*nch+2*c); x[5]=rn16(pcm+2*nch+2*c); x[6]=rn16(pcm+8*nch+2*c); x[7]=rn16(pcm+4*nch+2*c); } }
    return pos;
}
static u32 sfof(u32 x){ return (u32)((31-SCALE_OUT_BITS)-sclz(x)); }
static void calc_sf(sbcenc_t* f){
    for(int ch=0;ch<f->channels;ch++) for(int sb=0;sb<f->subbands;sb++){
        u32 x=1u<<SCALE_OUT_BITS; for(int b=0;b<f->blocks;b++){ int32_t t=sabs(f->sbf[b][ch][sb]); if(t)x|=(u32)(t-1); }
        f->sf[ch][sb]=sfof(x); }
}
static int calc_sf_j(sbcenc_t* f){
    int joint=0, sb=f->subbands-1, blocks=f->blocks; u32 x=1u<<SCALE_OUT_BITS, y=1u<<SCALE_OUT_BITS;
    for(int b=0;b<blocks;b++){ int32_t a=sabs(f->sbf[b][0][sb]), c=sabs(f->sbf[b][1][sb]); if(a)x|=(u32)(a-1); if(c)y|=(u32)(c-1); }
    f->sf[0][sb]=sfof(x); f->sf[1][sb]=sfof(y);
    while(--sb>=0){
        int32_t j[16][2]; x=1u<<SCALE_OUT_BITS; y=1u<<SCALE_OUT_BITS;
        for(int b=0;b<blocks;b++){ int32_t a=f->sbf[b][0][sb], c=f->sbf[b][1][sb]; j[b][0]=(a>>1)+(c>>1); j[b][1]=(a>>1)-(c>>1); a=sabs(a); c=sabs(c); if(a)x|=(u32)(a-1); if(c)y|=(u32)(c-1); }
        f->sf[0][sb]=sfof(x); f->sf[1][sb]=sfof(y);
        x=1u<<SCALE_OUT_BITS; y=1u<<SCALE_OUT_BITS;
        for(int b=0;b<blocks;b++){ int32_t a=sabs(j[b][0]), c=sabs(j[b][1]); if(a)x|=(u32)(a-1); if(c)y|=(u32)(c-1); }
        x=sfof(x); y=sfof(y);
        if(f->sf[0][sb]+f->sf[1][sb]>x+y){ joint|=1<<(f->subbands-1-sb); f->sf[0][sb]=x; f->sf[1][sb]=y; for(int b=0;b<blocks;b++){ f->sbf[b][0][sb]=j[b][0]; f->sbf[b][1][sb]=j[b][1]; } }
    }
    return joint;
}
typedef struct { u8* p; int cap, n; u32 acc; int an; } pbits_t;
static void pb_put(pbits_t* b,int n,u32 v){ for(int i=n-1;i>=0;i--){ b->acc=(b->acc<<1)|((v>>i)&1); if(++b->an==8){ if(b->n<b->cap)b->p[b->n]=(u8)b->acc; b->n++; b->an=0; b->acc=0; } } }
static void pb_flush(pbits_t* b){ if(b->an){ if(b->n<b->cap)b->p[b->n]=(u8)(b->acc<<(8-b->an)); b->n++; b->an=0; b->acc=0; } }
static int pack(sbcenc_t* f,u8* out,int cap,int joint){
    u8 crch[11]={0}; int crcp=16, bits[2][8]; u32 lev[2][8], dl[2][8];
    out[0]=0x9C; out[1]=(u8)(((f->freq&3)<<6)|((((f->blocks>>2)-1)&3)<<4)|((f->mode&3)<<2)|((f->alloc&1)<<1)|(f->subbands==8)); out[2]=f->bitpool;
    if(f->msbc){ out[0]=0xAD; out[1]=0; out[2]=0; }
    if(f->bitpool>f->subbands<<(4+(f->mode==SBC_MODE_STEREO||f->mode==SBC_MODE_JOINT)))return -1;
    crch[0]=out[1]; crch[1]=out[2];
    pbits_t pb={out+4,cap-4,0,0,0};
    if(f->mode==SBC_MODE_JOINT){ pb_put(&pb,f->subbands,(u32)joint); crch[crcp>>3]=(u8)joint; crcp+=f->subbands; }
    for(int ch=0;ch<f->channels;ch++) for(int sb=0;sb<f->subbands;sb++){ pb_put(&pb,4,f->sf[ch][sb]&15); crch[crcp>>3]=(u8)((crch[crcp>>3]<<4)|(f->sf[ch][sb]&15)); crcp+=4; }
    if(crcp%8)crch[crcp>>3]=(u8)(crch[crcp>>3]<<(8-crcp%8));
    out[3]=sbc_crc8(crch,crcp);
    calc_bits(f,bits);
    for(int ch=0;ch<f->channels;ch++) for(int sb=0;sb<f->subbands;sb++){ lev[ch][sb]=((1u<<bits[ch][sb])-1)<<(32-(f->sf[ch][sb]+SCALE_OUT_BITS+2)); dl[ch][sb]=1u<<(f->sf[ch][sb]+SCALE_OUT_BITS+1); }
    for(int b=0;b<f->blocks;b++) for(int ch=0;ch<f->channels;ch++) for(int sb=0;sb<f->subbands;sb++){
        if(!bits[ch][sb])continue;
        u32 s=(u32)(((u64)lev[ch][sb]*(u32)(dl[ch][sb]+(u32)f->sbf[b][ch][sb]))>>32);
        pb_put(&pb,bits[ch][sb],s); }
    pb_flush(&pb);
    return pb.n+4>cap?-1:pb.n+4;
}
int sbc_frame_len(int sbn,int blocks,int ch,int mode,int bp){
    int dual=mode==SBC_MODE_DUAL, joint=mode==SBC_MODE_JOINT;
    if(mode==SBC_MODE_MONO||mode==SBC_MODE_DUAL)return 4+(4*sbn*ch)/8+(blocks*ch*bp+7)/8;
    return 4+(4*sbn*ch)/8+((blocks*bp*(1+dual)+joint*sbn)+7)/8;
}
int sbc_enc_init(sbcenc_t* e,int rate,int ch,int mode,int sbn,int blocks,int alloc,int bp){
    u8* z=(u8*)e; for(unsigned i=0;i<sizeof *e;i++)z[i]=0;
    int fi=rate==16000?0:rate==32000?1:rate==44100?2:rate==48000?3:-1;
    if(fi<0||(ch!=1&&ch!=2)||(sbn!=4&&sbn!=8)||(blocks!=4&&blocks!=8&&blocks!=12&&blocks!=16)||bp<2||bp>250)return -1;
    if((ch==1)!=(mode==SBC_MODE_MONO)||mode<0||mode>3)return -1;
    if(bp>sbn<<(4+(mode==SBC_MODE_STEREO||mode==SBC_MODE_JOINT)))return -1;
    e->freq=(u8)fi; e->channels=(u8)ch; e->mode=(u8)mode; e->subbands=(u8)sbn; e->blocks=(u8)blocks; e->alloc=(u8)(alloc&1); e->bitpool=(u8)bp;
    e->codesize=(u16)(sbn*blocks*ch*2); e->framelen=sbc_frame_len(sbn,blocks,ch,mode,bp);
    e->position=(SBC_X_BUFFER_SIZE-sbn*9)&~7;
    return e->framelen;
}
void sbc_bits(const sbcenc_t* f,int bits[2][8]){ calc_bits(f,bits); }
int sbc_enc_init_msbc(sbcenc_t* e){
    if(sbc_enc_init(e,16000,1,SBC_MODE_MONO,8,16,SBC_AM_LOUDNESS,26)<0)return -1;
    e->msbc=1; e->odd=1; e->blocks=15; e->codesize=MSBC_PCM; e->framelen=MSBC_FRAME; return MSBC_FRAME;
}
int sbc_enc_frame(sbcenc_t* f,const u8* pcm,u8* out,int cap){
    if(!f||!f->codesize||cap<f->framelen)return -1;
    int ns=f->subbands*f->blocks;
    if(f->msbc){ f->position=input_8s(f->position,pcm,f->X,ns,1); int16_t* x=&f->X[0][f->position-8+f->blocks*8];
        for(int b=0;b<f->blocks;b++){ analyze(x,f->sbf[b][0],f->odd?sbcdsp_analysis_consts_fixed8_simd_odd:sbcdsp_analysis_consts_fixed8_simd_even,8); f->odd^=1; x-=8; } }
    else if(f->subbands==8){ f->position=input_8s(f->position,pcm,f->X,ns,f->channels);
        for(int ch=0;ch<f->channels;ch++){ int16_t* x=&f->X[ch][f->position-32+f->blocks*8]; for(int b=0;b<f->blocks;b+=4){ analyze_4b_8s(x,f->sbf[b][ch],16); x-=32; } } }
    else { f->position=input_4s(f->position,pcm,f->X,ns,f->channels);
        for(int ch=0;ch<f->channels;ch++){ int16_t* x=&f->X[ch][f->position-16+f->blocks*4]; for(int b=0;b<f->blocks;b+=4){ analyze_4b_4s(x,f->sbf[b][ch],16); x-=16; } } }
    int j=0; if(f->mode==SBC_MODE_JOINT)j=calc_sf_j(f); else calc_sf(f);
    return pack(f,out,cap,j);
}
