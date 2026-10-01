#ifndef NVXCRYPTO2_H
#define NVXCRYPTO2_H
/* nvxcrypto2.h - crypto upgrades for NoovexOS.
   Include AFTER nvxcrypto.h and nvxtls.h (reuses chacha_block, SB[], xtime).
   Adds: SHA-512, SHA-384, HMAC-SHA512, AES-256, and a ChaCha20 CSPRNG. */

/* ===================== SHA-512 / SHA-384 ===================== */
static const u64 K512[80]={
0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,0xe9b5dba58189dbbcULL,
0x3956c25bf348b538ULL,0x59f111f1b605d019ULL,0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,
0xd807aa98a3030242ULL,0x12835b0145706fbeULL,0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,
0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,0xc19bf174cf692694ULL,
0xe49b69c19ef14ad2ULL,0xefbe4786384f25e3ULL,0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,
0x2de92c6f592b0275ULL,0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,
0x983e5152ee66dfabULL,0xa831c66d2db43210ULL,0xb00327c898fb213fULL,0xbf597fc7beef0ee4ULL,
0xc6e00bf33da88fc2ULL,0xd5a79147930aa725ULL,0x06ca6351e003826fULL,0x142929670a0e6e70ULL,
0x27b70a8546d22ffcULL,0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,0x53380d139d95b3dfULL,
0x650a73548baf63deULL,0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,0x92722c851482353bULL,
0xa2bfe8a14cf10364ULL,0xa81a664bbc423001ULL,0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,
0xd192e819d6ef5218ULL,0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,
0x19a4c116b8d2d0c8ULL,0x1e376c085141ab53ULL,0x2748774cdf8eeb99ULL,0x34b0bcb5e19b48a8ULL,
0x391c0cb3c5c95a63ULL,0x4ed8aa4ae3418acbULL,0x5b9cca4f7763e373ULL,0x682e6ff3d6b2b8a3ULL,
0x748f82ee5defb2fcULL,0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,
0x90befffa23631e28ULL,0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,0xc67178f2e372532bULL,
0xca273eceea26619cULL,0xd186b8c721c0c207ULL,0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,
0x06f067aa72176fbaULL,0x0a637dc5a2c898a6ULL,0x113f9804bef90daeULL,0x1b710b35131c471bULL,
0x28db77f523047d84ULL,0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,0x431d67c49c100d4cULL,
0x4cc5d4becb3e42b6ULL,0x597f299cfc657e2aULL,0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL};

static u64 ror64(u64 v,int n){ return (v>>n)|(v<<(64-n)); }

typedef struct { u64 h[8]; u64 len; u8 buf[128]; int n; } sha512_ctx;

static void sha512_compress(sha512_ctx* c,const u8* p){
    u64 w[80];
    for(int i=0;i<16;i++) w[i]=((u64)p[i*8]<<56)|((u64)p[i*8+1]<<48)|((u64)p[i*8+2]<<40)|((u64)p[i*8+3]<<32)
                              |((u64)p[i*8+4]<<24)|((u64)p[i*8+5]<<16)|((u64)p[i*8+6]<<8)|((u64)p[i*8+7]);
    for(int i=16;i<80;i++){ u64 s0=ror64(w[i-15],1)^ror64(w[i-15],8)^(w[i-15]>>7);
        u64 s1=ror64(w[i-2],19)^ror64(w[i-2],61)^(w[i-2]>>6); w[i]=w[i-16]+s0+w[i-7]+s1; }
    u64 a=c->h[0],b=c->h[1],cc=c->h[2],d=c->h[3],e=c->h[4],f=c->h[5],g=c->h[6],h=c->h[7];
    for(int i=0;i<80;i++){
        u64 S1=ror64(e,14)^ror64(e,18)^ror64(e,41);
        u64 ch=(e&f)^((~e)&g);
        u64 t1=h+S1+ch+K512[i]+w[i];
        u64 S0=ror64(a,28)^ror64(a,34)^ror64(a,39);
        u64 maj=(a&b)^(a&cc)^(b&cc);
        u64 t2=S0+maj;
        h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2;
    }
    c->h[0]+=a;c->h[1]+=b;c->h[2]+=cc;c->h[3]+=d;c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=h;
}
static void sha512_init(sha512_ctx* c){
    c->h[0]=0x6a09e667f3bcc908ULL;c->h[1]=0xbb67ae8584caa73bULL;c->h[2]=0x3c6ef372fe94f82bULL;c->h[3]=0xa54ff53a5f1d36f1ULL;
    c->h[4]=0x510e527fade682d1ULL;c->h[5]=0x9b05688c2b3e6c1fULL;c->h[6]=0x1f83d9abfb41bd6bULL;c->h[7]=0x5be0cd19137e2179ULL;
    c->len=0;c->n=0;
}
static void sha384_init(sha512_ctx* c){
    c->h[0]=0xcbbb9d5dc1059ed8ULL;c->h[1]=0x629a292a367cd507ULL;c->h[2]=0x9159015a3070dd17ULL;c->h[3]=0x152fecd8f70e5939ULL;
    c->h[4]=0x67332667ffc00b31ULL;c->h[5]=0x8eb44a8768581511ULL;c->h[6]=0xdb0c2e0d64f98fa7ULL;c->h[7]=0x47b5481dbefa4fa4ULL;
    c->len=0;c->n=0;
}
static void sha512_update(sha512_ctx* c,const u8* d,u64 len){
    c->len+=len;
    while(len){ int take=128-c->n; if((u64)take>len)take=(int)len; for(int i=0;i<take;i++)c->buf[c->n+i]=d[i];
        c->n+=take; d+=take; len-=take; if(c->n==128){ sha512_compress(c,c->buf); c->n=0; } }
}
static void sha512_final(sha512_ctx* c,u8* out,int outlen){
    u64 bits=c->len*8;
    u8 pad=0x80; sha512_update(c,&pad,1);
    u8 z=0; while(c->n!=112) sha512_update(c,&z,1);
    u8 lb[16]; for(int i=0;i<8;i++)lb[i]=0; for(int i=0;i<8;i++)lb[8+i]=(u8)(bits>>((7-i)*8));
    sha512_update(c,lb,16);
    for(int i=0;i<8 && i*8<outlen;i++) for(int j=0;j<8 && i*8+j<outlen;j++) out[i*8+j]=(u8)(c->h[i]>>((7-j)*8));
}
static void sha512_hash(const u8* d,int len,u8 out[64]){ sha512_ctx c; sha512_init(&c); sha512_update(&c,d,(u64)len); sha512_final(&c,out,64); }
static void sha384_hash(const u8* d,int len,u8 out[48]){ sha512_ctx c; sha384_init(&c); sha512_update(&c,d,(u64)len); sha512_final(&c,out,48); }

static void hmac_sha512(const u8* key,int klen,const u8* msg,int mlen,u8 out[64]){
    u8 k[128]; int i;
    if(klen>128){ u8 kh[64]; sha512_hash(key,klen,kh); for(i=0;i<64;i++)k[i]=kh[i]; for(i=64;i<128;i++)k[i]=0; }
    else { for(i=0;i<klen;i++)k[i]=key[i]; for(i=klen;i<128;i++)k[i]=0; }
    u8 ip[128],op[128]; for(i=0;i<128;i++){ ip[i]=k[i]^0x36; op[i]=k[i]^0x5c; }
    sha512_ctx c; u8 inner[64];
    sha512_init(&c); sha512_update(&c,ip,128); sha512_update(&c,msg,(u64)mlen); sha512_final(&c,inner,64);
    sha512_init(&c); sha512_update(&c,op,128); sha512_update(&c,inner,64); sha512_final(&c,out,64);
}

/* ===================== AES-256 (reuses SB[]/xtime from nvxtls.h) ===================== */
static void aes256_expand(const u8 key[32],u8 rk[240]){
    int i; for(i=0;i<32;i++)rk[i]=key[i];
    u8 rcon=1; int bytes=32;
    while(bytes<240){
        u8 t[4]; for(i=0;i<4;i++)t[i]=rk[bytes-4+i];
        if(bytes%32==0){ u8 tmp=t[0]; t[0]=SB[t[1]]^rcon; t[1]=SB[t[2]]; t[2]=SB[t[3]]; t[3]=SB[tmp];
            rcon=(u8)((rcon<<1)^((rcon>>7)*0x1b)); }
        else if(bytes%32==16){ for(i=0;i<4;i++)t[i]=SB[t[i]]; }
        for(i=0;i<4;i++){ rk[bytes]=rk[bytes-32]^t[i]; bytes++; }
    }
}
static void aes256_enc(const u8 rk[240],const u8 in[16],u8 out[16]){
    u8 s[16]; int i; for(i=0;i<16;i++)s[i]=in[i]^rk[i];
    for(int round=1;round<=14;round++){
        for(i=0;i<16;i++)s[i]=SB[s[i]];
        u8 t[16];
        t[0]=s[0];t[4]=s[4];t[8]=s[8];t[12]=s[12];
        t[1]=s[5];t[5]=s[9];t[9]=s[13];t[13]=s[1];
        t[2]=s[10];t[6]=s[14];t[10]=s[2];t[14]=s[6];
        t[3]=s[15];t[7]=s[3];t[11]=s[7];t[15]=s[11];
        for(i=0;i<16;i++)s[i]=t[i];
        if(round<14){
            for(int c=0;c<4;c++){ u8* col=s+c*4;
                u8 a0=col[0],a1=col[1],a2=col[2],a3=col[3];
                u8 h=a0^a1^a2^a3;
                u8 x0=(u8)(a0^h^((u8)((a0^a1)<<1)^(((a0^a1)&0x80)?0x1b:0)));
                u8 x1=(u8)(a1^h^((u8)((a1^a2)<<1)^(((a1^a2)&0x80)?0x1b:0)));
                u8 x2=(u8)(a2^h^((u8)((a2^a3)<<1)^(((a2^a3)&0x80)?0x1b:0)));
                u8 x3=(u8)(a3^h^((u8)((a3^a0)<<1)^(((a3^a0)&0x80)?0x1b:0)));
                col[0]=x0;col[1]=x1;col[2]=x2;col[3]=x3;
            }
        }
        for(i=0;i<16;i++)s[i]^=rk[round*16+i];
    }
    for(i=0;i<16;i++)out[i]=s[i];
}

/* ===================== ChaCha20 CSPRNG (seeded from RDRAND/RDSEED + RDTSC) ===================== */
static u8  g_csprng_key[32];
static u32 g_csprng_ctr=0;
static int g_csprng_ready=0;
/* platform entropy: rdseed > rdrand > rdtsc mix */
static int nvx_rdrand32(u32* x){ unsigned char ok=0; __asm__ volatile("rdrand %0; setc %1":"=r"(*x),"=qm"(ok)::"cc"); return ok; }
static int nvx_rdseed32(u32* x){ unsigned char ok=0; __asm__ volatile("rdseed %0; setc %1":"=r"(*x),"=qm"(ok)::"cc"); return ok; }
static void csprng_seed(void){
    u8 seed[64]; int p=0;
    for(int i=0;i<16 && p<64;i++){ u32 r=0; int got=nvx_rdseed32(&r); if(!got)got=nvx_rdrand32(&r);
        u32 a,d; __asm__ volatile("rdtsc":"=a"(a),"=d"(d)); r^=a^(d<<1);
        seed[p++]=(u8)r;seed[p++]=(u8)(r>>8);seed[p++]=(u8)(r>>16);seed[p++]=(u8)(r>>24); }
    /* fold into key via SHA-512 (whitening) */
    u8 h[64]; sha512_hash(seed,64,h);
    for(int i=0;i<32;i++)g_csprng_key[i]=h[i];
    g_csprng_ctr=0; g_csprng_ready=1;
}
/* fill buf with cryptographically-strong random bytes */
static void csprng_bytes(u8* buf,int len){
    if(!g_csprng_ready)csprng_seed();
    u8 nonce[12]={0};
    while(len>0){
        u32 in[16],out[16];
        in[0]=0x61707865;in[1]=0x3320646e;in[2]=0x79622d32;in[3]=0x6b206574;
        for(int i=0;i<8;i++)in[4+i]=(u32)g_csprng_key[i*4]|((u32)g_csprng_key[i*4+1]<<8)|((u32)g_csprng_key[i*4+2]<<16)|((u32)g_csprng_key[i*4+3]<<24);
        in[12]=g_csprng_ctr++; in[13]=0;in[14]=0;in[15]=0;
        chacha_block(in,out);
        u8 ks[64]; for(int i=0;i<16;i++){ ks[i*4]=(u8)out[i];ks[i*4+1]=(u8)(out[i]>>8);ks[i*4+2]=(u8)(out[i]>>16);ks[i*4+3]=(u8)(out[i]>>24); }
        int take=len<64?len:64; for(int i=0;i<take;i++)buf[i]=ks[i]; buf+=take; len-=take;
        (void)nonce;
    }
    /* forward secrecy: derive a fresh key from the stream so past output can't be recovered */
    { u32 in[16],out[16];
      in[0]=0x61707865;in[1]=0x3320646e;in[2]=0x79622d32;in[3]=0x6b206574;
      for(int i=0;i<8;i++)in[4+i]=(u32)g_csprng_key[i*4]|((u32)g_csprng_key[i*4+1]<<8)|((u32)g_csprng_key[i*4+2]<<16)|((u32)g_csprng_key[i*4+3]<<24);
      in[12]=g_csprng_ctr++; in[13]=0;in[14]=0;in[15]=0; chacha_block(in,out);
      u8 nk[32]; for(int i=0;i<8;i++){ nk[i*4]=(u8)out[i];nk[i*4+1]=(u8)(out[i]>>8);nk[i*4+2]=(u8)(out[i]>>16);nk[i*4+3]=(u8)(out[i]>>24);} 
      for(int i=0;i<32;i++)g_csprng_key[i]=nk[i]; }
}
static u32 csprng_u32(void){ u32 r; csprng_bytes((u8*)&r,4); return r; }

#endif
