/*
 * Bluetooth low-complexity, subband codec (SBC): mSBC decoder (mono, 8 subbands, 15 blocks)
 *
 * Derived from the SBC library (sbc.c, sbc_tables.h):
 * Copyright (C) 2004-2010  Marcel Holtmann <marcel@holtmann.org>
 * Copyright (C) 2004-2005  Henryk Ploetz <henryk@ploetzli.ch>
 * Copyright (C) 2005-2008  Brad Midgley <bmidgley@xmission.com>
 * Copyright (C) 2008-2010  Nokia Corporation
 * Copyright (C) 2012-2013  Intel Corporation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "include/nvx/base.h"
#include "net/bluetooth/sbc/sbc.h"
static const int32_t pm0[40]={0,-1484,-17826,17825,1483,-42,-2105,-21754,13942,916,-90,-2742,-25579,10243,432,-146,-3342,-29150,6844,46,-216,-3842,-32314,3837,-237,-299,-4170,-34935,1288,-424,-388,-4253,-36898,-767,-523,-468,-4016,-38114,-2322,-552};
static const int32_t pm1[40]={-528,-3392,-38524,-3392,-528,-552,-2322,-38114,-4016,-468,-523,-767,-36898,-4253,-388,-424,1288,-34935,-4170,-299,-237,3837,-32314,-3842,-216,46,6844,-29150,-3342,-146,432,10243,-25579,-2742,-90,916,13942,-21754,-2105,-42};
static const int32_t syn[128]={5792,-5793,-5793,5792,5792,-5793,-5793,5792,4551,-8035,1598,6811,-6812,-1599,8034,-4552,3134,-7569,7568,-3135,-3135,7568,-7569,3134,1598,-4552,6811,-8035,8034,-6812,4551,-1599,0,0,0,0,0,0,0,0,-1599,4551,-6812,8034,-8035,6811,-4552,1598,-3135,7568,-7569,3134,3134,-7569,7568,-3135,-4552,8034,-1599,-6812,6811,1598,-8035,4551,-5793,5792,5792,-5793,-5793,5792,5792,-5793,-6812,1598,8034,4551,-4552,-8035,-1599,6811,-7569,-3135,3134,7568,7568,3134,-3135,-7569,-8035,-6812,-4552,-1599,1598,4551,6811,8034,-8192,-8192,-8192,-8192,-8192,-8192,-8192,-8192,-8035,-6812,-4552,-1599,1598,4551,6811,8034,-7569,-3135,3134,7568,7568,3134,-3135,-7569,-6812,1598,8034,4551,-4552,-8035,-1599,6811};
static int16_t clip16(int32_t s){ return (int16_t)(s>32767?32767:s<-32768?-32768:s); }
void msbc_dec_init(msbcdec_t* d){
    u8* z=(u8*)d; for(unsigned i=0;i<sizeof *d;i++)z[i]=0;
    for(int i=0;i<16;i++)d->off[i]=10*i+10;
    d->f.subbands=8; d->f.blocks=15; d->f.channels=1; d->f.mode=SBC_MODE_MONO; d->f.alloc=SBC_AM_LOUDNESS; d->f.bitpool=26; d->f.freq=0; d->f.msbc=1;
}
int msbc_dec_frame(msbcdec_t* d,const u8* p,int n,int16_t* pcm){
    if(n<MSBC_FRAME||p[0]!=0xAD||p[1]||p[2])return -1;
    u8 crch[11]={0}; int cp=16, bits[2][8];
    for(int sb=0;sb<8;sb++){ u32 v=(p[4+(sb>>1)]>>(sb&1?0:4))&15; d->f.sf[0][sb]=v; crch[cp>>3]|=(u8)(v<<(4-(cp&7))); cp+=4; }
    if(p[3]!=sbc_crc8(crch,cp))return -2;
    sbc_bits(&d->f,bits);
    int c=64;
    for(int b=0;b<15;b++) for(int sb=0;sb<8;sb++){
        int nb=bits[0][sb]; if(!nb){ d->sb[b][sb]=0; continue; }
        u32 lev=(1u<<nb)-1, sh=d->f.sf[0][sb]+1+2, a=0;
        for(int k=0;k<nb;k++){ if(c>=n*8)return -1; if((p[c>>3]>>(7-(c&7)))&1)a|=1u<<(nb-k-1); c++; }
        d->sb[b][sb]=(int32_t)(((((u64)a<<1)|1)<<sh)/lev)-(int32_t)(1u<<sh); }
    for(int b=0;b<15;b++){
        for(int i=0;i<16;i++){ if(--d->off[i]<0){ d->off[i]=159; for(int j=0;j<9;j++)d->V[j+160]=d->V[j]; }
            int32_t s=0; for(int k=0;k<8;k++)s+=syn[i*8+k]*d->sb[b][k]; d->V[d->off[i]]=s>>15; }
        for(int i=0,ix=0;i<8;i++,ix+=5){ int k=(i+8)&15; int32_t s=0;
            for(int j=0;j<5;j++)s+=d->V[d->off[i]+2*j]*pm0[ix+j]+d->V[d->off[k]+2*j+1]*pm1[ix+j];
            pcm[b*8+i]=clip16(s>>15); } }
    return 120;
}
