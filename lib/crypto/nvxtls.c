#include "include/nvx/base.h"
#include "include/nvx/lib/crypto/nvxtls.h"
#include "include/nvx/lib/crypto/nvxx509.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/lib/crypto/nvxcrypto.h"
#include "include/nvx/net/http/http.h"
/* nvxtls.h - TLS 1.3 client for NoovexOS. Include AFTER nvxcrypto.h.
   AES-128-GCM + ChaCha20-Poly1305 + key schedule + record + handshake + tls_get().
   Verified vs RFC 8448 Sec.3 + McGrew GCM + RFC8439 ChaCha (25/25 vectors).
   KERNEL: place tls_io (~18KB) + scratch (inner/rec/rb, ~70KB) at fixed high RAM. */
/* ===================== AES-128 ===================== */
const u8 SB[256]={
0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};
u8 xtime(u8 x){ return (u8)((x<<1)^((x>>7)*0x1b)); }
void aes128_expand(const u8 key[16],u8 rk[176]){
    for(int i=0;i<16;i++)rk[i]=key[i];
    u8 rcon=1;
    for(int i=16;i<176;i+=4){
        u8 t[4]; for(int j=0;j<4;j++)t[j]=rk[i-4+j];
        if(i%16==0){ u8 tmp=t[0]; t[0]=SB[t[1]]^rcon; t[1]=SB[t[2]]; t[2]=SB[t[3]]; t[3]=SB[tmp]; rcon=xtime(rcon); }
        for(int j=0;j<4;j++)rk[i+j]=rk[i-16+j]^t[j];
    }
}
static const u32 TLS_TE0[256]={
0xc66363a5u,0xf87c7c84u,0xee777799u,0xf67b7b8du,0xfff2f20du,0xd66b6bbdu,0xde6f6fb1u,0x91c5c554u,
0x60303050u,0x02010103u,0xce6767a9u,0x562b2b7du,0xe7fefe19u,0xb5d7d762u,0x4dababe6u,0xec76769au,
0x8fcaca45u,0x1f82829du,0x89c9c940u,0xfa7d7d87u,0xeffafa15u,0xb25959ebu,0x8e4747c9u,0xfbf0f00bu,
0x41adadecu,0xb3d4d467u,0x5fa2a2fdu,0x45afafeau,0x239c9cbfu,0x53a4a4f7u,0xe4727296u,0x9bc0c05bu,
0x75b7b7c2u,0xe1fdfd1cu,0x3d9393aeu,0x4c26266au,0x6c36365au,0x7e3f3f41u,0xf5f7f702u,0x83cccc4fu,
0x6834345cu,0x51a5a5f4u,0xd1e5e534u,0xf9f1f108u,0xe2717193u,0xabd8d873u,0x62313153u,0x2a15153fu,
0x0804040cu,0x95c7c752u,0x46232365u,0x9dc3c35eu,0x30181828u,0x379696a1u,0x0a05050fu,0x2f9a9ab5u,
0x0e070709u,0x24121236u,0x1b80809bu,0xdfe2e23du,0xcdebeb26u,0x4e272769u,0x7fb2b2cdu,0xea75759fu,
0x1209091bu,0x1d83839eu,0x582c2c74u,0x341a1a2eu,0x361b1b2du,0xdc6e6eb2u,0xb45a5aeeu,0x5ba0a0fbu,
0xa45252f6u,0x763b3b4du,0xb7d6d661u,0x7db3b3ceu,0x5229297bu,0xdde3e33eu,0x5e2f2f71u,0x13848497u,
0xa65353f5u,0xb9d1d168u,0x00000000u,0xc1eded2cu,0x40202060u,0xe3fcfc1fu,0x79b1b1c8u,0xb65b5bedu,
0xd46a6abeu,0x8dcbcb46u,0x67bebed9u,0x7239394bu,0x944a4adeu,0x984c4cd4u,0xb05858e8u,0x85cfcf4au,
0xbbd0d06bu,0xc5efef2au,0x4faaaae5u,0xedfbfb16u,0x864343c5u,0x9a4d4dd7u,0x66333355u,0x11858594u,
0x8a4545cfu,0xe9f9f910u,0x04020206u,0xfe7f7f81u,0xa05050f0u,0x783c3c44u,0x259f9fbau,0x4ba8a8e3u,
0xa25151f3u,0x5da3a3feu,0x804040c0u,0x058f8f8au,0x3f9292adu,0x219d9dbcu,0x70383848u,0xf1f5f504u,
0x63bcbcdfu,0x77b6b6c1u,0xafdada75u,0x42212163u,0x20101030u,0xe5ffff1au,0xfdf3f30eu,0xbfd2d26du,
0x81cdcd4cu,0x180c0c14u,0x26131335u,0xc3ecec2fu,0xbe5f5fe1u,0x359797a2u,0x884444ccu,0x2e171739u,
0x93c4c457u,0x55a7a7f2u,0xfc7e7e82u,0x7a3d3d47u,0xc86464acu,0xba5d5de7u,0x3219192bu,0xe6737395u,
0xc06060a0u,0x19818198u,0x9e4f4fd1u,0xa3dcdc7fu,0x44222266u,0x542a2a7eu,0x3b9090abu,0x0b888883u,
0x8c4646cau,0xc7eeee29u,0x6bb8b8d3u,0x2814143cu,0xa7dede79u,0xbc5e5ee2u,0x160b0b1du,0xaddbdb76u,
0xdbe0e03bu,0x64323256u,0x743a3a4eu,0x140a0a1eu,0x924949dbu,0x0c06060au,0x4824246cu,0xb85c5ce4u,
0x9fc2c25du,0xbdd3d36eu,0x43acacefu,0xc46262a6u,0x399191a8u,0x319595a4u,0xd3e4e437u,0xf279798bu,
0xd5e7e732u,0x8bc8c843u,0x6e373759u,0xda6d6db7u,0x018d8d8cu,0xb1d5d564u,0x9c4e4ed2u,0x49a9a9e0u,
0xd86c6cb4u,0xac5656fau,0xf3f4f407u,0xcfeaea25u,0xca6565afu,0xf47a7a8eu,0x47aeaee9u,0x10080818u,
0x6fbabad5u,0xf0787888u,0x4a25256fu,0x5c2e2e72u,0x381c1c24u,0x57a6a6f1u,0x73b4b4c7u,0x97c6c651u,
0xcbe8e823u,0xa1dddd7cu,0xe874749cu,0x3e1f1f21u,0x964b4bddu,0x61bdbddcu,0x0d8b8b86u,0x0f8a8a85u,
0xe0707090u,0x7c3e3e42u,0x71b5b5c4u,0xcc6666aau,0x904848d8u,0x06030305u,0xf7f6f601u,0x1c0e0e12u,
0xc26161a3u,0x6a35355fu,0xae5757f9u,0x69b9b9d0u,0x17868691u,0x99c1c158u,0x3a1d1d27u,0x279e9eb9u,
0xd9e1e138u,0xebf8f813u,0x2b9898b3u,0x22111133u,0xd26969bbu,0xa9d9d970u,0x078e8e89u,0x339494a7u,
0x2d9b9bb6u,0x3c1e1e22u,0x15878792u,0xc9e9e920u,0x87cece49u,0xaa5555ffu,0x50282878u,0xa5dfdf7au,
0x038c8c8fu,0x59a1a1f8u,0x09898980u,0x1a0d0d17u,0x65bfbfdau,0xd7e6e631u,0x844242c6u,0xd06868b8u,
0x824141c3u,0x299999b0u,0x5a2d2d77u,0x1e0f0f11u,0x7bb0b0cbu,0xa85454fcu,0x6dbbbbd6u,0x2c16163au};
#define TLS_ROR32(x,n) (((x)>>(n))|((x)<<(32-(n))))
static inline u32 tls_ld32be(const u8* p){ return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|(u32)p[3]; }
static inline void tls_st32be(u8* p,u32 v){ p[0]=(u8)(v>>24); p[1]=(u8)(v>>16); p[2]=(u8)(v>>8); p[3]=(u8)v; }
void aes128_enc(const u8 rk[176],const u8 in[16],u8 out[16]){
    u32 s0=tls_ld32be(in)^tls_ld32be(rk), s1=tls_ld32be(in+4)^tls_ld32be(rk+4), s2=tls_ld32be(in+8)^tls_ld32be(rk+8), s3=tls_ld32be(in+12)^tls_ld32be(rk+12);
    for(int r=1;r<10;r++){
        const u8* k=rk+r*16;
        u32 t0=TLS_TE0[s0>>24]^TLS_ROR32(TLS_TE0[(s1>>16)&255],8)^TLS_ROR32(TLS_TE0[(s2>>8)&255],16)^TLS_ROR32(TLS_TE0[s3&255],24)^tls_ld32be(k);
        u32 t1=TLS_TE0[s1>>24]^TLS_ROR32(TLS_TE0[(s2>>16)&255],8)^TLS_ROR32(TLS_TE0[(s3>>8)&255],16)^TLS_ROR32(TLS_TE0[s0&255],24)^tls_ld32be(k+4);
        u32 t2=TLS_TE0[s2>>24]^TLS_ROR32(TLS_TE0[(s3>>16)&255],8)^TLS_ROR32(TLS_TE0[(s0>>8)&255],16)^TLS_ROR32(TLS_TE0[s1&255],24)^tls_ld32be(k+8);
        u32 t3=TLS_TE0[s3>>24]^TLS_ROR32(TLS_TE0[(s0>>16)&255],8)^TLS_ROR32(TLS_TE0[(s1>>8)&255],16)^TLS_ROR32(TLS_TE0[s2&255],24)^tls_ld32be(k+12);
        s0=t0; s1=t1; s2=t2; s3=t3;
    }
    const u8* k=rk+160;
    u32 o0=((u32)SB[s0>>24]<<24)^((u32)SB[(s1>>16)&255]<<16)^((u32)SB[(s2>>8)&255]<<8)^(u32)SB[s3&255]^tls_ld32be(k);
    u32 o1=((u32)SB[s1>>24]<<24)^((u32)SB[(s2>>16)&255]<<16)^((u32)SB[(s3>>8)&255]<<8)^(u32)SB[s0&255]^tls_ld32be(k+4);
    u32 o2=((u32)SB[s2>>24]<<24)^((u32)SB[(s3>>16)&255]<<16)^((u32)SB[(s0>>8)&255]<<8)^(u32)SB[s1&255]^tls_ld32be(k+8);
    u32 o3=((u32)SB[s3>>24]<<24)^((u32)SB[(s0>>16)&255]<<16)^((u32)SB[(s1>>8)&255]<<8)^(u32)SB[s2&255]^tls_ld32be(k+12);
    tls_st32be(out,o0); tls_st32be(out+4,o1); tls_st32be(out+8,o2); tls_st32be(out+12,o3);
}
typedef struct { u64 hh[16], hl[16]; } gcm_tab;
static const u16 GCM_LAST4[16]={0x0000,0x1c20,0x3840,0x2460,0x7080,0x6ca0,0x48c0,0x54e0,0xe100,0xfd20,0xd940,0xc560,0x9180,0x8da0,0xa9c0,0xb5e0};
static void gcm_tab_init(gcm_tab* t,const u8 H[16]){
    u64 vh=((u64)tls_ld32be(H)<<32)|tls_ld32be(H+4), vl=((u64)tls_ld32be(H+8)<<32)|tls_ld32be(H+12);
    t->hl[8]=vl; t->hh[8]=vh; t->hh[0]=0; t->hl[0]=0;
    for(int i=4;i>0;i>>=1){ u32 T=(u32)(vl&1)*0xe1000000u; vl=(vh<<63)|(vl>>1); vh=(vh>>1)^((u64)T<<32); t->hl[i]=vl; t->hh[i]=vh; }
    for(int i=2;i<=8;i*=2){ u64 h=t->hh[i], l=t->hl[i]; for(int j=1;j<i;j++){ t->hh[i+j]=h^t->hh[j]; t->hl[i+j]=l^t->hl[j]; } }
}
static void gcm_mul(const gcm_tab* t,u8 x[16]){
    int lo=x[15]&15; u64 zh=t->hh[lo], zl=t->hl[lo];
    for(int i=15;i>=0;i--){
        lo=x[i]&15; int hi=(x[i]>>4)&15; u8 rem;
        if(i!=15){ rem=(u8)(zl&15); zl=(zh<<60)|(zl>>4); zh=zh>>4; zh^=(u64)GCM_LAST4[rem]<<48; zh^=t->hh[lo]; zl^=t->hl[lo]; }
        rem=(u8)(zl&15); zl=(zh<<60)|(zl>>4); zh=zh>>4; zh^=(u64)GCM_LAST4[rem]<<48; zh^=t->hh[hi]; zl^=t->hl[hi];
    }
    tls_st32be(x,(u32)(zh>>32)); tls_st32be(x+4,(u32)zh); tls_st32be(x+8,(u32)(zl>>32)); tls_st32be(x+12,(u32)zl);
}
static void ghash_blocks(u8 S[16],const gcm_tab* t,const u8*data,int len){
    int full=len/16;
    for(int b=0;b<full;b++){ for(int i=0;i<16;i++)S[i]^=data[b*16+i]; gcm_mul(t,S); }
    int rem=len-full*16;
    if(rem){ for(int i=0;i<rem;i++)S[i]^=data[full*16+i]; gcm_mul(t,S); }
}
static void inc32(u8 c[16]){ for(int i=15;i>=12;i--){ if(++c[i])break; } }
static int aes_gcm(const u8 key[16],const u8 iv[12],const u8*aad,int aadlen,
                   const u8*in,int len,u8*out,u8 tag[16],int encrypt){
    u8 rk[176]; aes128_expand(key,rk);
    u8 H[16]; for(int i=0;i<16;i++)H[i]=0; aes128_enc(rk,H,H);
    gcm_tab gt; gcm_tab_init(&gt,H);
    u8 J0[16]; for(int i=0;i<12;i++)J0[i]=iv[i]; J0[12]=0;J0[13]=0;J0[14]=0;J0[15]=1;
    u8 S[16]; for(int i=0;i<16;i++)S[i]=0;
    ghash_blocks(S,&gt,aad,aadlen);
    if(!encrypt) ghash_blocks(S,&gt,in,len);
    u8 ctr[16]; for(int i=0;i<16;i++)ctr[i]=J0[i];
    int off=0;
    while(off<len){ inc32(ctr); u8 ks[16]; aes128_enc(rk,ctr,ks);
        int k=len-off; if(k>16)k=16; for(int i=0;i<k;i++)out[off+i]=in[off+i]^ks[i]; off+=k; }
    if(encrypt) ghash_blocks(S,&gt,out,len);
    u8 lb[16]; u64 ab=(u64)aadlen*8, cb=(u64)len*8;
    for(int i=0;i<8;i++)lb[i]=(u8)(ab>>(56-i*8));
    for(int i=0;i<8;i++)lb[8+i]=(u8)(cb>>(56-i*8));
    for(int i=0;i<16;i++)S[i]^=lb[i]; gcm_mul(&gt,S);
    u8 EJ0[16]; aes128_enc(rk,J0,EJ0);
    if(encrypt){ for(int i=0;i<16;i++)tag[i]=S[i]^EJ0[i]; return 1; }
    else { u8 t[16]; int ok=0; for(int i=0;i<16;i++){ t[i]=S[i]^EJ0[i]; ok|=t[i]^tag[i]; } return ok==0; }
}

/* ===================== TLS 1.3 record AEAD (AES-128-GCM suite) =====================
   nonce = iv XOR seq(8B big-endian, right aligned); aad = 5-byte record header. */
static void tls_nonce(const u8 iv[12],u64 seq,u8 nonce[12]){
    for(int i=0;i<12;i++)nonce[i]=iv[i];
    for(int i=0;i<8;i++)nonce[11-i]^=(u8)(seq>>(i*8));
}
/* seal a TLS1.3 inner plaintext (already has content-type byte appended) into a record body */
static void tls_seal_gcm(const u8 key[16],const u8 iv[12],u64 seq,
                         const u8*inner,int innerlen,u8*recbody /* ct||tag */){
    int total=innerlen+16;
    u8 hdr[5]={0x17,0x03,0x03,(u8)(total>>8),(u8)total};
    u8 nonce[12]; tls_nonce(iv,seq,nonce);
    aes_gcm(key,nonce,hdr,5,inner,innerlen,recbody,recbody+innerlen,1);
}

/* ===================== TLS 1.3 key schedule ===================== */
typedef struct { u8 hs[32], chs[32], shs[32], master[32];
                 u8 chs_key[32],chs_iv[12], shs_key[32],shs_iv[12]; int klen; } tls_ks;
static void tls_key_schedule(const u8 ecdhe[32],const u8 hash_ch_sh[32],u16 cipher,tls_ks*k){
    int kl = (cipher==0x1303) ? 32 : 16;   /* ChaCha20=32, AES-128=16 */
    k->klen = kl;
    u8 zero[32]; for(int i=0;i<32;i++)zero[i]=0;
    u8 ehash[32]; sha256_hash((const u8*)"",0,ehash);
    u8 early[32]; hkdf_extract(0,0,zero,32,early);
    u8 derived[32]; hkdf_label(early,"derived",ehash,32,derived,32);
    hkdf_extract(derived,32,ecdhe,32,k->hs);
    hkdf_label(k->hs,"c hs traffic",hash_ch_sh,32,k->chs,32);
    hkdf_label(k->hs,"s hs traffic",hash_ch_sh,32,k->shs,32);
    u8 mderiv[32]; hkdf_label(k->hs,"derived",ehash,32,mderiv,32);
    hkdf_extract(mderiv,32,zero,32,k->master);
    hkdf_label(k->chs,"key",0,0,k->chs_key,kl); hkdf_label(k->chs,"iv",0,0,k->chs_iv,12);
    hkdf_label(k->shs,"key",0,0,k->shs_key,kl); hkdf_label(k->shs,"iv",0,0,k->shs_iv,12);
}
/* unified AEAD over the negotiated cipher (16-byte tag, 12-byte nonce, identical TLS framing) */
static void tls_seal(u16 cipher,const u8*key,const u8 nonce[12],const u8*aad,int aadlen,
                     const u8*pt,int ptlen,u8*ct,u8 tag[16]){
    if(cipher==0x1303) aead_seal(key,nonce,aad,aadlen,pt,ptlen,ct,tag);
    else aes_gcm(key,nonce,aad,aadlen,pt,ptlen,ct,tag,1);
}
static int tls_open(u16 cipher,const u8*key,const u8 nonce[12],const u8*aad,int aadlen,
                    const u8*ct,int ctlen,const u8 tag[16],u8*pt){
    if(cipher==0x1303) return aead_open(key,nonce,aad,aadlen,ct,ctlen,tag,pt);
    return aes_gcm(key,nonce,aad,aadlen,ct,ctlen,pt,(u8*)tag,0);
}
/* HKDF-Expand-Label */

/* ===================== TLS 1.3 handshake messages ===================== */
static int put16(u8*o,int p,int v){ o[p]=(u8)(v>>8); o[p+1]=(u8)v; return p+2; }
/* parse ServerHello payload -> cipher suite + x25519 server public key. returns 1 on success */
static u16 tls_last_cipher=0;
static const u8 g_hrr_rnd[32]={0xCF,0x21,0xAD,0x74,0xE5,0x9A,0x61,0x11,0xBE,0x1D,0x8C,0x02,0x1E,0x65,0xB8,0x91,0xC2,0xA2,0x11,0x16,0x7A,0xBB,0x8C,0x5E,0x07,0x9E,0x09,0xE2,0xC8,0xA8,0x33,0x9C};
typedef struct { u16 cipher, group; int hrr, klen, cklen, v12, ems; u8 key[65]; u8 srnd[32]; u8 cookie[512]; } tls_sh;
static int parse_serverhello(const u8*p,int len,tls_sh*sh){
    if(len<6||p[0]!=0x02)return 0;
    int o=4; o+=2;
    if(o+32>len)return 0;
    int hrr=1; for(int i=0;i<32;i++) if(p[o+i]!=g_hrr_rnd[i]){ hrr=0; break; }
    for(int i=0;i<32;i++)sh->srnd[i]=p[o+i];
    int legacy=(p[4]<<8)|p[5];
    o+=32; sh->hrr=hrr; sh->klen=0; sh->cklen=0; sh->group=0; sh->v12=0; sh->ems=0;
    if(o>=len)return 0; int sid=p[o]; o+=1+sid;
    if(o+2>len)return 0; sh->cipher=(p[o]<<8)|p[o+1]; o+=2; tls_last_cipher=sh->cipher;
    o+=1;
    if(o+2>len)return 0; int extlen=(p[o]<<8)|p[o+1]; o+=2;
    int end=o+extlen; if(end>len)end=len; int ver=0;
    while(o+4<=end){
        int et=(p[o]<<8)|p[o+1], el=(p[o+2]<<8)|p[o+3]; o+=4;
        if(o+el>end)return 0;
        if(et==0x002b && el==2) ver=(p[o]<<8)|p[o+1];
        if(et==0x0017) sh->ems=1;
        if(et==0x0033 && el>=2){
            sh->group=(p[o]<<8)|p[o+1];
            if(!hrr && el>=4){ int kl=(p[o+2]<<8)|p[o+3]; if(kl<=65 && 4+kl<=el){ for(int i=0;i<kl;i++)sh->key[i]=p[o+4+i]; sh->klen=kl; } }
        }
        if(et==0x002c && hrr && el>=2){ int cl=(p[o]<<8)|p[o+1]; if(cl<=(int)sizeof sh->cookie && 2+cl<=el){ for(int i=0;i<cl;i++)sh->cookie[i]=p[o+2+i]; sh->cklen=cl; } }
        o+=el;
    }
    if(ver==0 && legacy==0x0303 && !hrr){ sh->v12=1; return sh->cipher==0xC02B||sh->cipher==0xC02F||sh->cipher==0xCCA8||sh->cipher==0xCCA9; }
    if(ver!=0x0304)return 0;
    if(hrr) return sh->group==0x001d||sh->group==0x0017;
    return (sh->group==0x001d&&sh->klen==32)||(sh->group==0x0017&&sh->klen==65);
}
/* build a ClientHello handshake message for TLS1.3 (x25519, AES-GCM + ChaCha suites, SNI). */
int g_tls_only12=0, g_tls_pin=0; u8 g_tls_leaf[32]; u8 g_tls12_ms[48], g_tls12_crnd[32], g_tls12_srnd[32];
static int build_clienthello(const char*host,int hl,u16 grp,const u8*pub,int publen,const u8 rnd[32],const u8 sid[32],const u8*cookie,int cklen,u8*o){
    int p=0;
    o[p++]=0x01; int lp=p; p+=3;                 /* handshake header (len backpatched) */
    o[p++]=0x03; o[p++]=0x03;                     /* legacy_version */
    for(int i=0;i<32;i++)o[p++]=rnd[i];           /* random */
    o[p++]=32; for(int i=0;i<32;i++)o[p++]=sid[i]; /* legacy_session_id (compat mode) */
    p=put16(o,p,12); p=put16(o,p,0x1301); p=put16(o,p,0x1303); p=put16(o,p,0xC02B); p=put16(o,p,0xC02F); p=put16(o,p,0xCCA9); p=put16(o,p,0xCCA8);
    o[p++]=0x01; o[p++]=0x00;                      /* compression: null */
    int elp=p; p+=2; int exts=p;                   /* extensions (len backpatched) */
    /* server_name */
    if(hl>0){ p=put16(o,p,0x0000); p=put16(o,p,2+1+2+hl); p=put16(o,p,1+2+hl);
        o[p++]=0x00; p=put16(o,p,hl); for(int i=0;i<hl;i++)o[p++]=host[i]; }
    /* supported_groups: x25519 */
    p=put16(o,p,0x000a); p=put16(o,p,6); p=put16(o,p,4); p=put16(o,p,0x001d); p=put16(o,p,0x0017);
    /* signature_algorithms */
    p=put16(o,p,0x000d); p=put16(o,p,18); p=put16(o,p,16);
        p=put16(o,p,0x0403); p=put16(o,p,0x0503); p=put16(o,p,0x0804); p=put16(o,p,0x0805); p=put16(o,p,0x0806); p=put16(o,p,0x0401); p=put16(o,p,0x0501); p=put16(o,p,0x0601);
    /* supported_versions: TLS 1.3 */
    if(g_tls_only12){ p=put16(o,p,0x002b); p=put16(o,p,3); o[p++]=0x02; p=put16(o,p,0x0303); }
    else { p=put16(o,p,0x002b); p=put16(o,p,5); o[p++]=0x04; p=put16(o,p,0x0304); p=put16(o,p,0x0303); }
    p=put16(o,p,0x0017); p=put16(o,p,0);
    p=put16(o,p,0xff01); p=put16(o,p,1); o[p++]=0;
    p=put16(o,p,0x000b); p=put16(o,p,2); o[p++]=1; o[p++]=0;
    /* key_share: x25519 */
    p=put16(o,p,0x0033); p=put16(o,p,2+2+2+publen); p=put16(o,p,2+2+publen);
        p=put16(o,p,grp); p=put16(o,p,publen); for(int i=0;i<publen;i++)o[p++]=pub[i];
    if(cklen>0){ p=put16(o,p,0x002c); p=put16(o,p,2+cklen); p=put16(o,p,cklen); for(int i=0;i<cklen;i++)o[p++]=cookie[i]; }
    /* ALPN: advertise http/1.1 (needed by some CDNs e.g. Cloudflare) */
    p=put16(o,p,0x0010); p=put16(o,p,11); p=put16(o,p,9); o[p++]=8;
        { const char* a="http/1.1"; for(int i=0;i<8;i++)o[p++]=(u8)a[i]; }
    put16(o,elp,p-exts);
    int total=p-(lp+3); o[lp]=(u8)(total>>16); o[lp+1]=(u8)(total>>8); o[lp+2]=(u8)total;
    return p;
}
/* Finished verify_data = HMAC(finished_key, transcript_hash), finished_key from a traffic secret */
static void tls_finished(const u8 traffic[32],const u8 transcript_hash[32],u8 out[32]){
    u8 fk[32]; hkdf_label(traffic,"finished",0,0,fk,32);
    hmac_sha256(fk,32,transcript_hash,32,out);
}

/* ===================== TLS 1.3 client orchestration =====================
   Socket abstraction: caller supplies wr()/rd() over an established TCP connection.
   The server certificate chain, host name, CertificateVerify and Finished are verified (see nvxx509.h). */

static int tls_fill(tls_io*io,int need){
    while(io->rlen - io->rpos < need){
        if(io->rpos>0){ for(int i=0;i<io->rlen-io->rpos;i++)io->rbuf[i]=io->rbuf[io->rpos+i]; io->rlen-=io->rpos; io->rpos=0; }
        if(io->rlen>=(int)sizeof(io->rbuf))return 0;
        int n=io->rd(io->ctx, io->rbuf+io->rlen, (int)sizeof(io->rbuf)-io->rlen);
        if(n<=0)return 0; io->rlen+=n;
    }
    return 1;
}
static int tls_read_record(tls_io*io,u8*type,u8*out,int max,int*blen){
    if(!tls_fill(io,5))return 0;
    u8*h=io->rbuf+io->rpos; *type=h[0]; int rl=(h[3]<<8)|h[4];
    if(rl>max||rl<0)return 0;
    if(!tls_fill(io,5+rl))return 0;
    h=io->rbuf+io->rpos; for(int i=0;i<rl;i++)out[i]=h[5+i]; *blen=rl; io->rpos+=5+rl; return 1;
}
static int tls_write_plain(tls_io*io,u8 type,const u8*body,int len){
    u8 h[5]={type,0x03,0x03,(u8)(len>>8),(u8)len};
    if(io->wr(io->ctx,h,5)<=0)return 0; return io->wr(io->ctx,body,len)>0;
}
struct tls_scratch { u8 inner[0x8000]; u8 rec[0x8000]; u8 rb[0x8000]; u8 mch[0x10000]; u8 hs[65536]; };
static struct tls_scratch g_tls_scr0 __attribute__((aligned(64)));
struct tls_scratch* g_tls_scr = &g_tls_scr0;
unsigned long tls_scratch_size(void) { return sizeof(struct tls_scratch); }
#define g_tls_inner (g_tls_scr->inner)
#define g_tls_rec (g_tls_scr->rec)
#define g_tls_rb (g_tls_scr->rb)
#define g_tls_mch (g_tls_scr->mch)
#define g_tls_hs (g_tls_scr->hs)
tls_io g_tls_io;
static int tls_write_enc(tls_io*io,u16 cipher,const u8*key,const u8 iv[12],u64 seq,u8 itype,const u8*data,int len){
    u8* inner=g_tls_inner; u8* rec=g_tls_rec;
    int off=0;
    do {
        int chunk=len-off; if(chunk>16000)chunk=16000;                 /* TLS records cap at 16 KiB */
        for(int i=0;i<chunk;i++)inner[i]=data[off+i]; inner[chunk]=itype;
        int il=chunk+1, total=il+16;
        u8 hdr[5]={0x17,0x03,0x03,(u8)(total>>8),(u8)total};
        u8 nonce[12]; tls_nonce(iv,seq,nonce);
        tls_seal(cipher,key,nonce,hdr,5,inner,il,rec,rec+il);
        if(io->wr(io->ctx,hdr,5)<=0)return 0; if(io->wr(io->ctx,rec,total)<=0)return 0;
        seq++; off+=chunk;
    } while(off<len);
    return 1;
}
static int tls_decrypt(u16 cipher,const u8*key,const u8 iv[12],u64 seq,const u8*body,int blen,u8*out,u8*itype){
    if(blen<17)return -1; int ctlen=blen-16;
    u8 hdr[5]={0x17,0x03,0x03,(u8)(blen>>8),(u8)blen};
    u8 nonce[12]; tls_nonce(iv,seq,nonce);
    if(!tls_open(cipher,key,nonce,hdr,5,body,ctlen,(u8*)body+ctlen,out))return -1;
    int n=ctlen; while(n>0 && out[n-1]==0)n--;
    if(n<=0)return -1; *itype=out[n-1]; return n-1;
}
static void tls_wipe(void*p,int n){ volatile u8*v=(volatile u8*)p; while(n-->0)*v++=0; }

int g_tls_insecure=0;
const char* g_tls_why="";
static int t12_chacha(u16 c){ return c==0xCCA8||c==0xCCA9; }
static void prf12(const u8*sec,int sl,const char*label,const u8*seed,int seedl,u8*out,int outl){
    u8 ls[160]; int ll=0; while(label[ll]){ ls[ll]=(u8)label[ll]; ll++; } for(int i=0;i<seedl;i++)ls[ll+i]=seed[i]; int lsl=ll+seedl;
    u8 a[32], buf[32+160], t[32]; hmac_sha256(sec,sl,ls,lsl,a);
    for(int o=0;o<outl;){
        for(int i=0;i<32;i++)buf[i]=a[i]; for(int i=0;i<lsl;i++)buf[32+i]=ls[i];
        hmac_sha256(sec,sl,buf,32+lsl,t); for(int i=0;i<32&&o<outl;i++)out[o++]=t[i];
        hmac_sha256(sec,sl,a,32,a);
    }
    tls_wipe(t,32); tls_wipe(a,32);
}
static void t12_aad(u8 aad[13],u64 seq,u8 type,int len){ for(int i=0;i<8;i++)aad[i]=(u8)(seq>>(56-8*i)); aad[8]=type; aad[9]=3; aad[10]=3; aad[11]=(u8)(len>>8); aad[12]=(u8)len; }
static int t12_write(tls_io*io,tls_sess*ss,u8 type,const u8*data,int len){
    u8* rec=g_tls_rec; int off=0;
    do {
        int chunk=len-off; if(chunk>16000)chunk=16000;
        u64 seq=ss->cseq++; u8 aad[13]; t12_aad(aad,seq,type,chunk); u8 nonce[12]; int total;
        if(t12_chacha(ss->cipher)){ tls_nonce(ss->civ,seq,nonce); aead_seal(ss->ckey,nonce,aad,13,data+off,chunk,rec,rec+chunk); total=chunk+16; }
        else { for(int i=0;i<4;i++)nonce[i]=ss->civ[i]; for(int i=0;i<8;i++){ nonce[4+i]=aad[i]; rec[i]=aad[i]; } aes_gcm(ss->ckey,nonce,aad,13,data+off,chunk,rec+8,rec+8+chunk,1); total=chunk+24; }
        u8 hdr[5]={type,0x03,0x03,(u8)(total>>8),(u8)total};
        if(io->wr(io->ctx,hdr,5)<=0)return 0; if(io->wr(io->ctx,rec,total)<=0)return 0;
        off+=chunk;
    } while(off<len);
    return 1;
}
static int t12_open(tls_sess*ss,u8 type,const u8*body,int blen,u8*out){
    u64 seq=ss->sseq++; u8 nonce[12], aad[13];
    if(t12_chacha(ss->cipher)){ if(blen<16)return -1; int cl=blen-16; t12_aad(aad,seq,type,cl); tls_nonce(ss->siv,seq,nonce); return aead_open(ss->skey,nonce,aad,13,body,cl,body+cl,out)?cl:-1; }
    if(blen<24)return -1; int cl=blen-24; t12_aad(aad,seq,type,cl);
    for(int i=0;i<4;i++)nonce[i]=ss->siv[i]; for(int i=0;i<8;i++)nonce[4+i]=body[i];
    return aes_gcm(ss->skey,nonce,aad,13,body+8,cl,out,(u8*)body+8+cl,0)?cl:-1;
}
static int t12_alert(tls_io*io,int code){ u8 al[2]={2,(u8)code}; return tls_write_plain(io,0x15,al,2); }
static int tls12_handshake(tls_io*io,const char*hname,const u8 crnd[32],const tls_sh*sh,sha256*tr,const u8 xpriv[32],const u8 seedr[32],const u8*rest,int restl,tls_sess*ss){
    u8* rb=g_tls_rb; u8 rtype; int rl; u8* hs=g_tls_hs; int hn=0,hp=0;
    for(int i=0;i<restl&&i<(int)sizeof g_tls_hs;i++)hs[hn++]=rest[i];
    const u8* certp[10]; int certl[10]; int ncert=0, cert_req=0, done=0, have_ske=0, grp=0, spl=0, guard=0;
    u8 spub[65];
    while(!done && guard++<256){
        while(hn-hp<4 || hn-hp<4+((hs[hp+1]<<16)|(hs[hp+2]<<8)|hs[hp+3])){
            if(!tls_read_record(io,&rtype,rb,17000,&rl))return -5;
            if(rtype==0x15){ g_tls_why="server sent an alert during the handshake"; return -5; }
            if(rtype!=0x16){ g_tls_why="unexpected record in the TLS 1.2 handshake"; return -5; }
            if(hn+rl>(int)sizeof g_tls_hs){ g_tls_why="handshake too large"; return -5; }
            for(int i=0;i<rl;i++)hs[hn+i]=rb[i]; hn+=rl;
        }
        int mt=hs[hp]; int ln=(hs[hp+1]<<16)|(hs[hp+2]<<8)|hs[hp+3]; const u8* m=hs+hp+4;
        if((mt==11&&(ncert||have_ske))||(mt==12&&have_ske)||(mt==13&&cert_req)){ g_tls_why="unexpected message in the TLS 1.2 handshake"; t12_alert(io,10); return -5; }
        if(mt==11){
            if(ln<3){ t12_alert(io,50); return -10; }
            int ll=(m[0]<<16)|(m[1]<<8)|m[2]; int o=3, end=3+ll; if(end>ln){ t12_alert(io,50); return -10; }
            while(o+3<=end&&ncert<10){ int cl=(m[o]<<16)|(m[o+1]<<8)|m[o+2]; o+=3; if(o+cl>end)break; certp[ncert]=m+o; certl[ncert]=cl; ncert++; o+=cl; }
            if(ncert)sha256_hash(certp[0],certl[0],g_tls_leaf);
            if(!g_tls_insecure){ int r=x509_verify_chain(certp,certl,ncert,hname,tls_clock_now()); if(r){ g_tls_why=x509_reason(); t12_alert(io,r==-11?48:r==-13?45:42); return r; } }
        } else if(mt==12){
            if(ln<4||m[0]!=3){ g_tls_why="server key exchange is not ECDHE"; t12_alert(io,40); return -3; }
            grp=(m[1]<<8)|m[2]; spl=m[3]; int pe=4+spl;
            if(!((grp==0x001d&&spl==32)||(grp==0x0017&&spl==65))||pe+4>ln){ g_tls_why="server picked a key group it was not offered"; t12_alert(io,47); return -3; }
            for(int i=0;i<spl;i++)spub[i]=m[4+i];
            int scheme=(m[pe]<<8)|m[pe+1], sl=(m[pe+2]<<8)|m[pe+3]; if(pe+4+sl>ln){ t12_alert(io,50); return -14; }
            if(!g_tls_insecure||g_tls_pin){
                if(!ncert){ g_tls_why="server sent no certificate"; t12_alert(io,40); return -16; }
                static u8 sd[64+4+65]; for(int i=0;i<32;i++){ sd[i]=crnd[i]; sd[32+i]=sh->srnd[i]; } for(int i=0;i<pe;i++)sd[64+i]=m[i];
                if(x509_leaf_sig12(certp[0],certl[0],scheme,sd,64+pe,m+pe+4,sl)){ g_tls_why="server signature (ServerKeyExchange) is invalid"; t12_alert(io,51); return -14; }
            }
            have_ske=1;
        } else if(mt==13){ cert_req=1; }
        else if(mt==14){ done=1; }
        else if(mt!=22){ g_tls_why="unexpected message in the TLS 1.2 handshake"; t12_alert(io,10); return -5; }
        sha256_update(tr,hs+hp,4+ln); hp+=4+ln;
    }
    if(!done||!have_ske){ g_tls_why="server skipped the key exchange"; t12_alert(io,10); return -16; }
    if(!g_tls_insecure&&!ncert){ g_tls_why="server sent no certificate"; t12_alert(io,40); return -16; }
    u8 cpub[65]; int cpl=32; u8 pms[32]; u8 epriv[32];
    if(grp==0x0017){
        u8 seed[33]; for(int i=0;i<32;i++)seed[i]=seedr[i]; seed[32]=0x12; int ok=0;
        for(int r=0;r<8&&!ok;r++){ sha256_hash(seed,33,epriv); seed[32]++; ok=x509_p256_ecdh(epriv,0,0,cpub)==0; }
        if(!ok)return -3; cpl=65;
        if(x509_p256_ecdh(epriv,spub,65,pms)){ tls_wipe(epriv,32); g_tls_why="server key share is invalid"; t12_alert(io,47); return -3; }
        tls_wipe(epriv,32);
    } else {
        u8 base[32]; for(int i=0;i<32;i++)base[i]=0; base[0]=9; x25519(cpub,xpriv,base); x25519(pms,xpriv,spub);
        u8 z=0; for(int i=0;i<32;i++)z|=pms[i]; if(!z){ g_tls_why="server key share is invalid"; t12_alert(io,47); return -3; }
    }
    if(cert_req){ u8 cm[7]={0x0B,0,0,3,0,0,0}; sha256_update(tr,cm,7); if(!tls_write_plain(io,0x16,cm,7))return -8; }
    { u8 ck[4+1+65]; ck[0]=0x10; ck[1]=0; ck[2]=0; ck[3]=(u8)(1+cpl); ck[4]=(u8)cpl; for(int i=0;i<cpl;i++)ck[5+i]=cpub[i]; sha256_update(tr,ck,5+cpl); if(!tls_write_plain(io,0x16,ck,5+cpl))return -8; }
    u8 ms[48];
    if(sh->ems){ u8 sh_[32]; { sha256 t=*tr; sha256_final(&t,sh_); } prf12(pms,32,"extended master secret",sh_,32,ms,48); }
    else { u8 sd[64]; for(int i=0;i<32;i++){ sd[i]=crnd[i]; sd[32+i]=sh->srnd[i]; } prf12(pms,32,"master secret",sd,64,ms,48); }
    tls_wipe(pms,32);
    for(int i=0;i<48;i++)g_tls12_ms[i]=ms[i]; for(int i=0;i<32;i++){ g_tls12_crnd[i]=crnd[i]; g_tls12_srnd[i]=sh->srnd[i]; }
    int kl=t12_chacha(sh->cipher)?32:16, ivl=t12_chacha(sh->cipher)?12:4;
    u8 kb[2*32+2*12]; { u8 sd[64]; for(int i=0;i<32;i++){ sd[i]=sh->srnd[i]; sd[32+i]=crnd[i]; } prf12(ms,48,"key expansion",sd,64,kb,2*kl+2*ivl); }
    for(int i=0;i<32;i++){ ss->ckey[i]=0; ss->skey[i]=0; } for(int i=0;i<12;i++){ ss->civ[i]=0; ss->siv[i]=0; }
    for(int i=0;i<kl;i++){ ss->ckey[i]=kb[i]; ss->skey[i]=kb[kl+i]; } for(int i=0;i<ivl;i++){ ss->civ[i]=kb[2*kl+i]; ss->siv[i]=kb[2*kl+ivl+i]; }
    tls_wipe(kb,sizeof kb);
    ss->cipher=sh->cipher; ss->cseq=0; ss->sseq=0; ss->klen=kl; ss->v12=1; ss->ok=0;
    { u8 ccs=0x01; if(!tls_write_plain(io,0x14,&ccs,1)){ tls_wipe(ms,48); return -8; } }
    { u8 th[32]; { sha256 t=*tr; sha256_final(&t,th); } u8 fin[16]; fin[0]=0x14; fin[1]=0; fin[2]=0; fin[3]=12; prf12(ms,48,"client finished",th,32,fin+4,12);
      sha256_update(tr,fin,16); if(!t12_write(io,ss,0x16,fin,16)){ tls_wipe(ms,48); return -8; } }
    u8 want[12]; { u8 th[32]; { sha256 t=*tr; sha256_final(&t,th); } prf12(ms,48,"server finished",th,32,want,12); }
    tls_wipe(ms,48);
    int got_ccs=0;
    for(int g=0;g<16;g++){
        if(!tls_read_record(io,&rtype,rb,17000,&rl))return -7;
        if(rtype==0x14){ got_ccs=1; continue; }
        if(rtype==0x15&&!got_ccs){ g_tls_why="server refused the key exchange (alert)"; return -7; }
        if(rtype==0x16&&!got_ccs)continue;
        if(rtype!=0x16){ g_tls_why="server Finished message is missing"; return -15; }
        u8* inner=g_tls_inner; int il=t12_open(ss,0x16,rb,rl,inner);
        if(il!=16||inner[0]!=0x14||inner[3]!=12){ g_tls_why="server Finished message is invalid"; return -15; }
        u8 d=0; for(int i=0;i<12;i++)d|=inner[4+i]^want[i];
        if(d){ g_tls_why="server Finished message is invalid"; return -15; }
        ss->ok=1; g_tls_why=g_tls_insecure?"connected WITHOUT certificate checks (TLS 1.2)":"connected, certificate verified (TLS 1.2)";
        return 0;
    }
    return -7;
}

volatile int g_net_rx;
int tls_connect_sess(tls_io*io,const char*host,int hl,const u8 rnd[64],tls_sess*ss){
    u8* mch=g_tls_mch;
    ss->ok=0;
    g_tls_why="handshake failed"; tls_prepare();
    char hname[256]; int hn0=hl<255?hl:255; for(int i=0;i<hn0;i++)hname[i]=host[i]; hname[hn0]=0;
    u8 priv[32]; for(int i=0;i<32;i++)priv[i]=rnd[i];
    priv[0]&=248; priv[31]&=127; priv[31]|=64;
    u8 pub[65],base[32]; for(int i=0;i<32;i++)base[i]=0; base[0]=9; x25519(pub,priv,base);
    u8 sid[32]; { u8 t[33]; for(int i=0;i<32;i++)t[i]=rnd[32+i]; t[32]=0x53; sha256_hash(t,33,sid); }
    int chl=build_clienthello(host,hl,0x001d,pub,32,rnd+32,sid,0,0,mch);
    sha256 tr; sha256_init(&tr); sha256_update(&tr,mch,chl);
    if(!tls_write_plain(io,0x16,mch,chl))return -1;
    u8* rb=g_tls_rb; u8 rtype; int rl;
    tls_sh sh; for(unsigned i=0;i<sizeof(sh);i++)((u8*)&sh)[i]=0; u16 grp=0x001d, hrr_cipher=0; u8 epriv[32]; int tries=0, ccs_sent=0;
    for(;;){
        do { if(!tls_read_record(io,&rtype,rb,17000,&rl))return -2; } while(rtype==0x14);
        if(rtype==0x15){ g_tls_why="server refused the connection (alert)"; return -2; }
        if(rtype!=0x16||rl<4||rb[0]!=0x02)return -2;
        if(!parse_serverhello(rb,rl,&sh)){ g_tls_why="server does not support TLS 1.3 with X25519 or P-256"; return -3; }
        if(!sh.v12 && sh.cipher!=0x1301 && sh.cipher!=0x1303)return -4;
        if(!sh.hrr){ if(hrr_cipher && sh.cipher!=hrr_cipher){ g_tls_why="server changed the cipher after a HelloRetryRequest"; return -4; } break; }
        hrr_cipher=sh.cipher;
        if(tries++){ g_tls_why="server sent a second HelloRetryRequest"; return -3; }
        if(sh.group==grp && !sh.cklen){ g_tls_why="server sent an invalid HelloRetryRequest"; return -3; }
        u8 h1[32]; { sha256 t=tr; sha256_final(&t,h1); }
        sha256_init(&tr); { u8 mh[4]={0xFE,0,0,32}; sha256_update(&tr,mh,4); sha256_update(&tr,h1,32); }
        sha256_update(&tr,rb,rl);
        int publen=32;
        if(sh.group==0x0017){
            u8 seed[33]; for(int i=0;i<32;i++)seed[i]=rnd[i]; seed[32]=0x17;
            int ok=0; for(int r=0;r<8&&!ok;r++){ sha256_hash(seed,33,epriv); seed[32]++; ok=x509_p256_ecdh(epriv,0,0,pub)==0; }
            if(!ok)return -3; publen=65;
        } else x25519(pub,priv,base);
        grp=sh.group;
        chl=build_clienthello(host,hl,grp,pub,publen,rnd+32,sid,sh.cookie,sh.cklen,mch);
        sha256_update(&tr,mch,chl);
        { u8 ccs=0x01; tls_write_plain(io,0x14,&ccs,1); ccs_sent=1; }
        if(!tls_write_plain(io,0x16,mch,chl))return -1;
    }
    if(sh.v12){
        static const u8 dg[7]={'D','O','W','N','G','R','D'};
        int down=1; for(int i=0;i<7;i++) if(sh.srnd[24+i]!=dg[i]){ down=0; break; }
        if(down&&(sh.srnd[31]==1||sh.srnd[31]==0)){ g_tls_why="TLS downgrade detected"; t12_alert(io,47); return -3; }
        int shl=4+((rb[1]<<16)|(rb[2]<<8)|rb[3]); if(shl>rl)shl=rl;
        sha256_update(&tr,rb,shl);
        int r=tls12_handshake(io,hname,rnd+32,&sh,&tr,priv,rnd,rb+shl,rl-shl,ss);
        tls_wipe(priv,32);
        if(r){ ss->ok=0; tls_wipe(ss,sizeof(*ss)); }
        return r;
    }
    if(sh.group!=grp){ g_tls_why="server picked a key group it was not offered"; return -3; }
    u16 cipher=sh.cipher;
    sha256_update(&tr,rb,rl);
    u8 ecdhe[32];
    if(grp==0x0017){ if(x509_p256_ecdh(epriv,sh.key,sh.klen,ecdhe)){ tls_wipe(epriv,32); g_tls_why="server key share is invalid"; return -3; } tls_wipe(epriv,32); }
    else x25519(ecdhe,priv,sh.key);
    u8 thash[32]; { sha256 t=tr; sha256_final(&t,thash); }
    tls_ks k; tls_key_schedule(ecdhe,thash,cipher,&k);
    u64 rseq=0; int got_sfin=0,guard=0;
    u8* hs=g_tls_hs; int hn=0,hp=0;
    const u8* certp[10]; int certl[10]; int ncert=0, have_cv=0, cert_req=0; u8 req_ctx[256]; int req_ctxl=0;
    int fail=0;
    while(!got_sfin && !fail && guard++<128){
        if(!tls_read_record(io,&rtype,rb,17000,&rl))return -5;
        if(rtype==0x14)continue;
        if(rtype!=0x17)return -5;
        u8* inner=g_tls_inner; u8 it;
        int il=tls_decrypt(cipher,k.shs_key,k.shs_iv,rseq++,rb,rl,inner,&it);
        if(il<0)return -6;
        if(it==0x15){ g_tls_why="server sent an alert during the handshake"; return -5; }
        if(it!=0x16)continue;
        if(hn+il>(int)sizeof g_tls_hs){ g_tls_why="handshake too large"; return -5; }
        for(int i=0;i<il;i++)hs[hn+i]=inner[i]; hn+=il;
        while(hn-hp>=4){
            int mt=hs[hp]; int ln=(hs[hp+1]<<16)|(hs[hp+2]<<8)|hs[hp+3];
            if(hn-hp<4+ln)break;
            const u8* m=hs+hp+4;
            u8 th[32]; { sha256 t=tr; sha256_final(&t,th); }
            if(mt==11){
                if(ln<4){ fail=-10; break; }
                int o=1+m[0]; if(o+3>ln){ fail=-10; break; }
                int ll=(m[o]<<16)|(m[o+1]<<8)|m[o+2]; o+=3; int end=o+ll; if(end>ln){ fail=-10; break; }
                while(o+3<=end&&ncert<10){ int cl=(m[o]<<16)|(m[o+1]<<8)|m[o+2]; o+=3; if(o+cl+2>end)break;
                    certp[ncert]=m+o; certl[ncert]=cl; ncert++; o+=cl; int el=(m[o]<<8)|m[o+1]; o+=2+el; }
                if(!g_tls_insecure){
                    int r=x509_verify_chain(certp,certl,ncert,hname,tls_clock_now());
                    if(r){ g_tls_why=x509_reason(); fail=r; break; } } }
            else if(mt==13){ cert_req=1; req_ctxl=ln>0?m[0]:0; if(req_ctxl>ln-1)req_ctxl=0; for(int i=0;i<req_ctxl;i++)req_ctx[i]=m[1+i]; }
            else if(mt==15){
                if(ln<4){ fail=-14; break; }
                int scheme=(m[0]<<8)|m[1], sl=(m[2]<<8)|m[3]; if(4+sl>ln){ fail=-14; break; }
                if(!g_tls_insecure){
                    if(!ncert){ g_tls_why="server sent no certificate"; fail=-16; break; }
                    if(x509_leaf_cv(certp[0],certl[0],scheme,m+4,sl,th)){ g_tls_why="server signature (CertificateVerify) is invalid"; fail=-14; break; } }
                have_cv=1; }
            else if(mt==20){
                u8 v[32]; tls_finished(k.shs,th,v); u8 d=(u8)(ln!=32); for(int i=0;i<32&&ln==32;i++)d|=v[i]^m[i];
                if(d){ g_tls_why="server Finished message is invalid"; fail=-15; break; }
                if(!g_tls_insecure&&(!ncert||!have_cv)){ g_tls_why="server skipped certificate authentication"; fail=-16; break; }
                got_sfin=1; }
            sha256_update(&tr,hs+hp,4+ln); hp+=4+ln;
            if(got_sfin)break;
        }
    }
    if(fail){ u8 al[2]={2,fail==-11?48:fail==-12||fail==-10?42:fail==-13?45:51}; tls_write_enc(io,cipher,k.chs_key,k.chs_iv,0,0x15,al,2);
        tls_wipe(priv,32); tls_wipe(ecdhe,32); tls_wipe(&k,sizeof(k)); return fail; }
    if(!got_sfin)return -7;
    u8 thash2[32]; { sha256 t=tr; sha256_final(&t,thash2); }
    if(!ccs_sent){ u8 ccs=0x01; tls_write_plain(io,0x14,&ccs,1); }
    u64 cseq=0;
    if(cert_req){ u8 cm[4+1+255+3]; int o=0; cm[o++]=0x0B; o+=3; cm[o++]=(u8)req_ctxl; for(int i=0;i<req_ctxl;i++)cm[o++]=req_ctx[i]; cm[o++]=0; cm[o++]=0; cm[o++]=0;
        int bl=o-4; cm[1]=(u8)(bl>>16); cm[2]=(u8)(bl>>8); cm[3]=(u8)bl; sha256_update(&tr,cm,o);
        if(!tls_write_enc(io,cipher,k.chs_key,k.chs_iv,cseq++,0x16,cm,o))return -8; }
    u8 thash3[32]; { sha256 t=tr; sha256_final(&t,thash3); }
    { u8 fin[36]; fin[0]=0x14;fin[1]=0;fin[2]=0;fin[3]=0x20;
      u8 v[32]; tls_finished(k.chs,thash3,v); for(int i=0;i<32;i++)fin[4+i]=v[i];
      if(!tls_write_enc(io,cipher,k.chs_key,k.chs_iv,cseq,0x16,fin,36))return -8; }
    g_tls_why=g_tls_insecure?"connected WITHOUT certificate checks":"connected, certificate verified";
    u8 cap[32],sap[32],cap_key[32],cap_iv[12],sap_key[32],sap_iv[12];
    hkdf_label(k.master,"c ap traffic",thash2,32,cap,32);
    hkdf_label(k.master,"s ap traffic",thash2,32,sap,32);
    hkdf_label(cap,"key",0,0,cap_key,k.klen); hkdf_label(cap,"iv",0,0,cap_iv,12);
    hkdf_label(sap,"key",0,0,sap_key,k.klen); hkdf_label(sap,"iv",0,0,sap_iv,12);
    ss->cipher=cipher; ss->cseq=0; ss->sseq=0; ss->klen=k.klen; ss->v12=0;
    for(int i=0;i<32;i++){ ss->csec[i]=cap[i]; ss->ssec[i]=sap[i]; }
    for(int i=0;i<32;i++){ ss->ckey[i]=cap_key[i]; ss->skey[i]=sap_key[i]; }
    for(int i=0;i<12;i++){ ss->civ[i]=cap_iv[i]; ss->siv[i]=sap_iv[i]; }
    ss->ok=1;
    tls_wipe(priv,32); tls_wipe(ecdhe,32); tls_wipe(&k,sizeof(k));
    tls_wipe(cap,32); tls_wipe(sap,32);
    tls_wipe(cap_key,32); tls_wipe(cap_iv,12); tls_wipe(sap_key,32); tls_wipe(sap_iv,12);
    return 0;
}
int tls_sess_write(tls_io*io,tls_sess*ss,const u8*data,int len){
    if(!ss->ok)return -1;
    if(ss->v12){ if(!t12_write(io,ss,0x17,data,len)){ ss->ok=0; return -1; } return len; }
    if(!tls_write_enc(io,ss->cipher,ss->ckey,ss->civ,ss->cseq,0x17,data,len)){ ss->ok=0; return -1; }
    ss->cseq+=(u64)(len>0?(len+15999)/16000:1);
    return len;
}
int tls_sess_read(tls_io*io,tls_sess*ss,u8*out,int max){
    if(!ss->ok)return -1;
    for(int guard=0;guard<64;guard++){
        u8* rb=g_tls_rb; u8 rtype; int rl;
        if(!tls_read_record(io,&rtype,rb,17000,&rl))return 0;
        if(ss->v12){
            if(rtype!=0x17&&rtype!=0x15&&rtype!=0x16){ ss->ok=0; return -1; }
            u8* in2=g_tls_inner; int n=t12_open(ss,rtype,rb,rl,in2);
            if(n<0){ ss->ok=0; return -1; }
            if(rtype==0x15){ ss->ok=0; return (n>=2&&in2[1]==0)?0:-1; }
            if(rtype==0x16){ if(n>=4&&in2[0]==0){ u8 al[2]={1,100}; t12_write(io,ss,0x15,al,2); } continue; }
            if(n>max)n=max;
            for(int i=0;i<n;i++)out[i]=in2[i];
            if(n>0)return n;
            continue;
        }
        if(rtype==0x14)continue;
        if(rtype!=0x17){ ss->ok=0; return -1; }
        u8* inner=g_tls_inner; u8 it;
        int il=tls_decrypt(ss->cipher,ss->skey,ss->siv,ss->sseq++,rb,rl,inner,&it);
        if(il<0){ ss->ok=0; return -1; }
        if(it==0x15){ ss->ok=0; return -1; }
        if(it==0x16){
            int req=-1;
            for(int o=0;o+4<=il;){ int ml=(inner[o+1]<<16)|(inner[o+2]<<8)|inner[o+3]; if(o+4+ml>il)break; if(inner[o]==24&&ml==1)req=inner[o+4]; o+=4+ml; }
            if(req<0)continue;
            u8 ns[32]; hkdf_label(ss->ssec,"traffic upd",0,0,ns,32);
            for(int i=0;i<32;i++)ss->ssec[i]=ns[i];
            hkdf_label(ns,"key",0,0,ss->skey,ss->klen); hkdf_label(ns,"iv",0,0,ss->siv,12); ss->sseq=0; tls_wipe(ns,32);
            if(req==1){
                u8 ku[5]={24,0,0,1,0};
                if(!tls_write_enc(io,ss->cipher,ss->ckey,ss->civ,ss->cseq,0x16,ku,5)){ ss->ok=0; return -1; }
                u8 nc[32]; hkdf_label(ss->csec,"traffic upd",0,0,nc,32);
                for(int i=0;i<32;i++)ss->csec[i]=nc[i];
                hkdf_label(nc,"key",0,0,ss->ckey,ss->klen); hkdf_label(nc,"iv",0,0,ss->civ,12); ss->cseq=0; tls_wipe(nc,32);
            }
            continue;
        }
        if(it!=0x17)continue;
        if(il>max)il=max;
        for(int i=0;i<il;i++)out[i]=inner[i];
        if(il>0)return il;
    }
    return 0;
}
void tls_sess_wipe(tls_sess*ss){ tls_wipe(ss,sizeof(*ss)); }
int tls_get(tls_io*io,const char*host,int hl,const u8 rnd[64],
                   const char*req,int reqlen,u8*out,int maxout){
    tls_sess ss;
    int r=tls_connect_sess(io,host,hl,rnd,&ss);
    if(r<0)return r;
    if(r>0)return r;
    if(tls_sess_write(io,&ss,(const u8*)req,reqlen)<0){ tls_sess_wipe(&ss); return -9; }
    int outlen=0, guard=0; g_net_rx=0;
    while(outlen<maxout && guard++<maxout/256+512){
        int n=tls_sess_read(io,&ss,out+outlen,maxout-outlen);
        if(n<=0)break;
        outlen+=n; g_net_rx=outlen;
    }
    tls_sess_wipe(&ss);
    return outlen;
}


void tls12_prf(const u8*sec,int sl,const char*label,const u8*seed,int seedl,u8*out,int outl){ prf12(sec,sl,label,seed,seedl,out,outl); }
