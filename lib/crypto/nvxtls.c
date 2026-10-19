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
void aes128_enc(const u8 rk[176],const u8 in[16],u8 out[16]){
    u8 s[16]; for(int i=0;i<16;i++)s[i]=in[i]^rk[i];
    for(int round=1;round<=10;round++){
        u8 t[16];
        for(int i=0;i<16;i++)t[i]=SB[s[i]];
        /* ShiftRows */
        u8 r[16];
        r[0]=t[0]; r[4]=t[4]; r[8]=t[8]; r[12]=t[12];
        r[1]=t[5]; r[5]=t[9]; r[9]=t[13]; r[13]=t[1];
        r[2]=t[10];r[6]=t[14];r[10]=t[2]; r[14]=t[6];
        r[3]=t[15];r[7]=t[3]; r[11]=t[7]; r[15]=t[11];
        if(round<10){
            for(int c=0;c<4;c++){
                u8 *p=r+c*4; u8 a0=p[0],a1=p[1],a2=p[2],a3=p[3];
                p[0]=xtime(a0)^(xtime(a1)^a1)^a2^a3;
                p[1]=a0^xtime(a1)^(xtime(a2)^a2)^a3;
                p[2]=a0^a1^xtime(a2)^(xtime(a3)^a3);
                p[3]=(xtime(a0)^a0)^a1^a2^xtime(a3);
            }
        }
        for(int i=0;i<16;i++)s[i]=r[i]^rk[round*16+i];
    }
    for(int i=0;i<16;i++)out[i]=s[i];
}

/* ===================== AES-128-GCM ===================== */
static void ghash_mul(u8 X[16],const u8 H[16]){
    u8 Z[16]; for(int i=0;i<16;i++)Z[i]=0;
    u8 V[16]; for(int i=0;i<16;i++)V[i]=H[i];
    for(int i=0;i<128;i++){
        if(X[i>>3]&(0x80>>(i&7))) for(int j=0;j<16;j++)Z[j]^=V[j];
        int lsb=V[15]&1;
        for(int j=15;j>0;j--)V[j]=(u8)((V[j]>>1)|((V[j-1]&1)<<7));
        V[0]>>=1;
        if(lsb)V[0]^=0xe1;
    }
    for(int i=0;i<16;i++)X[i]=Z[i];
}
static void ghash_blocks(u8 S[16],const u8 H[16],const u8*data,int len){
    int full=len/16;
    for(int b=0;b<full;b++){ for(int i=0;i<16;i++)S[i]^=data[b*16+i]; ghash_mul(S,H); }
    int rem=len-full*16;
    if(rem){ u8 blk[16]; for(int i=0;i<16;i++)blk[i]=0; for(int i=0;i<rem;i++)blk[i]=data[full*16+i];
        for(int i=0;i<16;i++)S[i]^=blk[i]; ghash_mul(S,H); }
}
static void inc32(u8 c[16]){ for(int i=15;i>=12;i--){ if(++c[i])break; } }
/* one-shot: encrypt=1 -> ct=enc(pt), tag out; encrypt=0 -> pt=dec(ct), returns 1 if tag ok */
static int aes_gcm(const u8 key[16],const u8 iv[12],const u8*aad,int aadlen,
                   const u8*in,int len,u8*out,u8 tag[16],int encrypt){
    u8 rk[176]; aes128_expand(key,rk);
    u8 H[16]; for(int i=0;i<16;i++)H[i]=0; aes128_enc(rk,H,H);
    u8 J0[16]; for(int i=0;i<12;i++)J0[i]=iv[i]; J0[12]=0;J0[13]=0;J0[14]=0;J0[15]=1;
    /* CTR */
    u8 ctr[16]; for(int i=0;i<16;i++)ctr[i]=J0[i];
    int off=0;
    while(off<len){ inc32(ctr); u8 ks[16]; aes128_enc(rk,ctr,ks);
        int k=len-off; if(k>16)k=16; for(int i=0;i<k;i++)out[off+i]=in[off+i]^ks[i]; off+=k; }
    /* GHASH over aad || ciphertext || lengths.  ciphertext = (encrypt? out : in) */
    const u8* ct = encrypt? out : in;
    u8 S[16]; for(int i=0;i<16;i++)S[i]=0;
    ghash_blocks(S,H,aad,aadlen);
    ghash_blocks(S,H,ct,len);
    u8 lb[16]; u64 ab=(u64)aadlen*8, cb=(u64)len*8;
    for(int i=0;i<8;i++)lb[i]=(u8)(ab>>(56-i*8));
    for(int i=0;i<8;i++)lb[8+i]=(u8)(cb>>(56-i*8));
    for(int i=0;i<16;i++)S[i]^=lb[i]; ghash_mul(S,H);
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
static int parse_serverhello(const u8*p,int len,u16*cipher,u8 pub[32]){
    if(len<6||p[0]!=0x02)return 0;
    int o=4;                 /* skip type(1)+length(3) */
    o+=2;                    /* legacy_version */
    o+=32;                   /* random */
    if(o>=len)return 0; int sid=p[o]; o+=1+sid;            /* session_id */
    if(o+2>len)return 0; *cipher=(p[o]<<8)|p[o+1]; o+=2; tls_last_cipher=*cipher;
    o+=1;                    /* compression */
    if(o+2>len)return 0; int extlen=(p[o]<<8)|p[o+1]; o+=2;
    int end=o+extlen; if(end>len)end=len; int found=0;
    while(o+4<=end){
        int et=(p[o]<<8)|p[o+1], el=(p[o+2]<<8)|p[o+3]; o+=4;
        if(et==0x0033 && el>=4){                            /* key_share */
            int grp=(p[o]<<8)|p[o+1], kl=(p[o+2]<<8)|p[o+3];
            if(grp==0x001d && kl==32){ for(int i=0;i<32;i++)pub[i]=p[o+4+i]; found=1; }
        }
        o+=el;
    }
    return found;
}
/* build a ClientHello handshake message for TLS1.3 (x25519, AES-GCM + ChaCha suites, SNI). */
static int build_clienthello(const char*host,int hl,const u8 pub[32],const u8 rnd[32],const u8 sid[32],u8*o){
    int p=0;
    o[p++]=0x01; int lp=p; p+=3;                 /* handshake header (len backpatched) */
    o[p++]=0x03; o[p++]=0x03;                     /* legacy_version */
    for(int i=0;i<32;i++)o[p++]=rnd[i];           /* random */
    o[p++]=32; for(int i=0;i<32;i++)o[p++]=sid[i]; /* legacy_session_id (compat mode) */
    p=put16(o,p,4); o[p++]=0x13;o[p++]=0x01; o[p++]=0x13;o[p++]=0x03; /* cipher suites */
    o[p++]=0x01; o[p++]=0x00;                      /* compression: null */
    int elp=p; p+=2; int exts=p;                   /* extensions (len backpatched) */
    /* server_name */
    p=put16(o,p,0x0000); p=put16(o,p,2+1+2+hl); p=put16(o,p,1+2+hl);
        o[p++]=0x00; p=put16(o,p,hl); for(int i=0;i<hl;i++)o[p++]=host[i];
    /* supported_groups: x25519 */
    p=put16(o,p,0x000a); p=put16(o,p,4); p=put16(o,p,2); p=put16(o,p,0x001d);
    /* signature_algorithms */
    p=put16(o,p,0x000d); p=put16(o,p,18); p=put16(o,p,16);
        p=put16(o,p,0x0403); p=put16(o,p,0x0503); p=put16(o,p,0x0804); p=put16(o,p,0x0805); p=put16(o,p,0x0806); p=put16(o,p,0x0401); p=put16(o,p,0x0501); p=put16(o,p,0x0601);
    /* supported_versions: TLS 1.3 */
    p=put16(o,p,0x002b); p=put16(o,p,3); o[p++]=0x02; p=put16(o,p,0x0304);
    /* key_share: x25519 */
    p=put16(o,p,0x0033); p=put16(o,p,2+2+2+32); p=put16(o,p,2+2+32);
        p=put16(o,p,0x001d); p=put16(o,p,32); for(int i=0;i<32;i++)o[p++]=pub[i];
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
static u8 g_tls_inner[0x8000] __attribute__((aligned(64)));
static u8 g_tls_rec[0x8000] __attribute__((aligned(64)));
static u8 g_tls_rb[0x8000] __attribute__((aligned(64)));
static u8 g_tls_mch[0x10000] __attribute__((aligned(64)));
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
static u8 g_tls_hs[65536];
int tls_get(tls_io*io,const char*host,int hl,const u8 rnd[64],
                   const char*req,int reqlen,u8*out,int maxout){
    u8* mch=g_tls_mch;
    g_tls_why="handshake failed"; tls_prepare();
    char hname[256]; int hn0=hl<255?hl:255; for(int i=0;i<hn0;i++)hname[i]=host[i]; hname[hn0]=0;
    u8 priv[32]; for(int i=0;i<32;i++)priv[i]=rnd[i];
    priv[0]&=248; priv[31]&=127; priv[31]|=64;
    u8 pub[32],base[32]; for(int i=0;i<32;i++)base[i]=0; base[0]=9; x25519(pub,priv,base);
    int chl=build_clienthello(host,hl,pub,rnd+32,rnd,mch);
    sha256 tr; sha256_init(&tr); sha256_update(&tr,mch,chl);
    if(!tls_write_plain(io,0x16,mch,chl))return -1;
    u8* rb=g_tls_rb; u8 rtype; int rl;
    if(!tls_read_record(io,&rtype,rb,17000,&rl))return -2;
    if(rtype==0x15){ g_tls_why="server refused the connection (alert)"; return -2; }
    if(rtype!=0x16||rl<4||rb[0]!=0x02)return -2;
    u16 cipher; u8 spub[32];
    if(!parse_serverhello(rb,rl,&cipher,spub)){ g_tls_why="server does not support TLS 1.3 with X25519"; return -3; }
    if(cipher!=0x1301 && cipher!=0x1303)return -4;
    sha256_update(&tr,rb,rl);
    u8 ecdhe[32]; x25519(ecdhe,priv,spub);
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
    u8 ccs=0x01; tls_write_plain(io,0x14,&ccs,1);
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
    if(!tls_write_enc(io,cipher,cap_key,cap_iv,0,0x17,(const u8*)req,reqlen))return -9;
    u64 sseq=0; int outlen=0; guard=0;
    while(outlen<maxout && guard++<512){
        if(!tls_read_record(io,&rtype,rb,17000,&rl))break;
        if(rtype==0x14)continue;
        if(rtype!=0x17)break;
        u8* inner=g_tls_inner; u8 it;
        int il=tls_decrypt(cipher,sap_key,sap_iv,sseq++,rb,rl,inner,&it);
        if(il<0)break;
        if(it==0x17){ for(int i=0;i<il && outlen<maxout;i++)out[outlen++]=inner[i]; }
        else if(it==0x15)break;
    }
    /* wipe all secret key material from RAM before returning */
    tls_wipe(priv,32); tls_wipe(ecdhe,32); tls_wipe(&k,sizeof(k));
    tls_wipe(cap,32); tls_wipe(sap,32);
    tls_wipe(cap_key,32); tls_wipe(cap_iv,12); tls_wipe(sap_key,32); tls_wipe(sap_iv,12);
    return outlen;
}

