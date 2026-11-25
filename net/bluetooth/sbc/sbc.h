#ifndef NVX_NET_BLUETOOTH_SBC_H
#define NVX_NET_BLUETOOTH_SBC_H
#include "include/nvx/base.h"
#define SBC_X_BUFFER_SIZE 328
enum { SBC_MODE_MONO=0, SBC_MODE_DUAL=1, SBC_MODE_STEREO=2, SBC_MODE_JOINT=3 };
enum { SBC_AM_LOUDNESS=0, SBC_AM_SNR=1 };
typedef struct sbcenc { u8 freq, blocks, mode, alloc, subbands, bitpool, channels, msbc, odd; u16 codesize; int framelen, position;
    int16_t X[2][SBC_X_BUFFER_SIZE] __attribute__((aligned(16))); int32_t sbf[16][2][8]; u32 sf[2][8]; } sbcenc_t;
int sbc_enc_init(sbcenc_t* e,int rate,int channels,int mode,int subbands,int blocks,int alloc,int bitpool);
int sbc_enc_frame(sbcenc_t* e,const u8* pcm,u8* out,int cap);
int sbc_frame_len(int subbands,int blocks,int channels,int mode,int bitpool);
u8 sbc_crc8(const u8* d,int bits);
#define MSBC_FRAME 57
#define MSBC_PCM 240
int sbc_enc_init_msbc(sbcenc_t* e);
void sbc_bits(const sbcenc_t* f,int bits[2][8]);
typedef struct { int32_t V[170]; int off[16]; sbcenc_t f; int32_t sb[15][8]; } msbcdec_t;
void msbc_dec_init(msbcdec_t* d);
int msbc_dec_frame(msbcdec_t* d,const u8* in,int n,int16_t* pcm);
#endif
