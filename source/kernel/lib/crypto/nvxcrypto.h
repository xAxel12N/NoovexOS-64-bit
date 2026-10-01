/* nvxcrypto.h - TLS 1.3 primitives for NoovexOS kernel.
   ALL functions verified bit-exact against RFC test vectors (SHA256/HMAC/HKDF/
   ChaCha20/Poly1305/AEAD/X25519). Freestanding -m32 clean, no libgcc deps (~8.7KB).
   Requires typedefs: u8, u32, u64, i64 (define u64/i64 in gmain.c if absent). */
#ifndef NVXCRYPTO_H
#define NVXCRYPTO_H
/* ===================== SHA-256 ===================== */
static const u32 K256[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

#define ROR(x,n) (((x)>>(n))|((x)<<(32-(n))))
typedef struct { u32 h[8]; u64 len; u8 buf[64]; int n; } sha256;

static void sha256_init(sha256*c){
    c->h[0]=0x6a09e667;c->h[1]=0xbb67ae85;c->h[2]=0x3c6ef372;c->h[3]=0xa54ff53a;
    c->h[4]=0x510e527f;c->h[5]=0x9b05688c;c->h[6]=0x1f83d9ab;c->h[7]=0x5be0cd19;
    c->len=0;c->n=0;
}
static void sha256_block(sha256*c,const u8*p){
    u32 w[64];
    for(int i=0;i<16;i++) w[i]=(p[i*4]<<24)|(p[i*4+1]<<16)|(p[i*4+2]<<8)|p[i*4+3];
    for(int i=16;i<64;i++){
        u32 s0=ROR(w[i-15],7)^ROR(w[i-15],18)^(w[i-15]>>3);
        u32 s1=ROR(w[i-2],17)^ROR(w[i-2],19)^(w[i-2]>>10);
        w[i]=w[i-16]+s0+w[i-7]+s1;
    }
    u32 a=c->h[0],b=c->h[1],cc=c->h[2],d=c->h[3],e=c->h[4],f=c->h[5],g=c->h[6],h=c->h[7];
    for(int i=0;i<64;i++){
        u32 S1=ROR(e,6)^ROR(e,11)^ROR(e,25);
        u32 ch=(e&f)^((~e)&g);
        u32 t1=h+S1+ch+K256[i]+w[i];
        u32 S0=ROR(a,2)^ROR(a,13)^ROR(a,22);
        u32 maj=(a&b)^(a&cc)^(b&cc);
        u32 t2=S0+maj;
        h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2;
    }
    c->h[0]+=a;c->h[1]+=b;c->h[2]+=cc;c->h[3]+=d;c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=h;
}
static void sha256_update(sha256*c,const u8*p,int len){
    c->len+=(u64)len;
    while(len>0){
        int k=64-c->n; if(k>len)k=len;
        for(int i=0;i<k;i++)c->buf[c->n+i]=p[i];
        c->n+=k;p+=k;len-=k;
        if(c->n==64){ sha256_block(c,c->buf); c->n=0; }
    }
}
static void sha256_final(sha256*c,u8*out){
    u64 bits=c->len*8; u8 pad=0x80;
    sha256_update(c,&pad,1);
    u8 z=0; while(c->n!=56) sha256_update(c,&z,1);
    u8 lb[8]; for(int i=0;i<8;i++)lb[i]=(u8)(bits>>(56-i*8));
    sha256_update(c,lb,8);
    for(int i=0;i<8;i++){ out[i*4]=c->h[i]>>24;out[i*4+1]=c->h[i]>>16;out[i*4+2]=c->h[i]>>8;out[i*4+3]=c->h[i]; }
}
static void sha256_hash(const u8*p,int len,u8*out){ sha256 c; sha256_init(&c); sha256_update(&c,p,len); sha256_final(&c,out); }

/* ===================== HMAC-SHA256 ===================== */
static void hmac_sha256(const u8*key,int klen,const u8*msg,int mlen,u8*out){
    u8 k[64]; u8 ki[64],ko[64]; u8 kh[32];
    if(klen>64){ sha256_hash(key,klen,kh); key=kh; klen=32; }
    for(int i=0;i<64;i++)k[i]=(i<klen)?key[i]:0;
    for(int i=0;i<64;i++){ ki[i]=k[i]^0x36; ko[i]=k[i]^0x5c; }
    sha256 c; u8 inner[32];
    sha256_init(&c); sha256_update(&c,ki,64); sha256_update(&c,msg,mlen); sha256_final(&c,inner);
    sha256_init(&c); sha256_update(&c,ko,64); sha256_update(&c,inner,32); sha256_final(&c,out);
}

/* ===================== HKDF (RFC 5869) ===================== */
static void hkdf_extract(const u8*salt,int slen,const u8*ikm,int ilen,u8*prk){
    u8 zero[32]; if(!salt){ for(int i=0;i<32;i++)zero[i]=0; salt=zero; slen=32; }
    hmac_sha256(salt,slen,ikm,ilen,prk);
}
static void hkdf_expand(const u8*prk,const u8*info,int ilen,u8*out,int L){
    u8 t[32]; int tlen=0; u8 ctr=1; int done=0;
    u8 buf[256+33];
    while(done<L){
        int p=0;
        for(int i=0;i<tlen;i++)buf[p++]=t[i];
        for(int i=0;i<ilen;i++)buf[p++]=info[i];
        buf[p++]=ctr;
        hmac_sha256(prk,32,buf,p,t); tlen=32;
        int k=L-done; if(k>32)k=32;
        for(int i=0;i<k;i++)out[done+i]=t[i];
        done+=k; ctr++;
    }
}
/* TLS1.3 HKDF-Expand-Label */
static void hkdf_label(const u8*secret,const char*label,const u8*ctx,int ctxlen,u8*out,int L){
    u8 info[300]; int p=0;
    info[p++]=(L>>8)&0xff; info[p++]=L&0xff;
    int ll=0; while(label[ll])ll++;
    info[p++]=(u8)(6+ll);
    const char*pfx="tls13 ";
    for(int i=0;i<6;i++)info[p++]=pfx[i];
    for(int i=0;i<ll;i++)info[p++]=label[i];
    info[p++]=(u8)ctxlen;
    for(int i=0;i<ctxlen;i++)info[p++]=ctx[i];
    hkdf_expand(secret,info,p,out,L);
}

/* ===================== ChaCha20 (RFC 8439) ===================== */
#define ROL(x,n) (((x)<<(n))|((x)>>(32-(n))))
static void chacha_block(const u32 in[16],u32 out[16]){
    u32 x[16]; for(int i=0;i<16;i++)x[i]=in[i];
    for(int r=0;r<10;r++){
        #define QR(a,b,c,d) x[a]+=x[b];x[d]^=x[a];x[d]=ROL(x[d],16); \
            x[c]+=x[d];x[b]^=x[c];x[b]=ROL(x[b],12); \
            x[a]+=x[b];x[d]^=x[a];x[d]=ROL(x[d],8); \
            x[c]+=x[d];x[b]^=x[c];x[b]=ROL(x[b],7);
        QR(0,4,8,12) QR(1,5,9,13) QR(2,6,10,14) QR(3,7,11,15)
        QR(0,5,10,15) QR(1,6,11,12) QR(2,7,8,13) QR(3,4,9,14)
        #undef QR
    }
    for(int i=0;i<16;i++)out[i]=x[i]+in[i];
}
static void chacha_state(u32 s[16],const u8 key[32],u32 counter,const u8 nonce[12]){
    s[0]=0x61707865;s[1]=0x3320646e;s[2]=0x79622d32;s[3]=0x6b206574;
    for(int i=0;i<8;i++)s[4+i]=key[i*4]|(key[i*4+1]<<8)|(key[i*4+2]<<16)|((u32)key[i*4+3]<<24);
    s[12]=counter;
    for(int i=0;i<3;i++)s[13+i]=nonce[i*4]|(nonce[i*4+1]<<8)|(nonce[i*4+2]<<16)|((u32)nonce[i*4+3]<<24);
}
static void chacha20(const u8 key[32],u32 counter,const u8 nonce[12],const u8*in,u8*out,int len){
    u32 s[16],blk[16]; chacha_state(s,key,counter,nonce);
    int off=0;
    while(off<len){
        chacha_block(s,blk); s[12]++;
        u8 ks[64]; for(int i=0;i<16;i++){ ks[i*4]=blk[i];ks[i*4+1]=blk[i]>>8;ks[i*4+2]=blk[i]>>16;ks[i*4+3]=blk[i]>>24; }
        int k=len-off; if(k>64)k=64;
        for(int i=0;i<k;i++)out[off+i]=in[off+i]^ks[i];
        off+=k;
    }
}

/* ===================== Poly1305 (RFC 8439) ===================== */
static void poly1305(const u8*msg,int len,const u8 key[32],u8 out[16]){
    u32 r0,r1,r2,r3,r4, s1,s2,s3,s4, h0=0,h1=0,h2=0,h3=0,h4=0;
    u32 t0=key[0]|(key[1]<<8)|(key[2]<<16)|((u32)key[3]<<24);
    u32 t1=key[4]|(key[5]<<8)|(key[6]<<16)|((u32)key[7]<<24);
    u32 t2=key[8]|(key[9]<<8)|(key[10]<<16)|((u32)key[11]<<24);
    u32 t3=key[12]|(key[13]<<8)|(key[14]<<16)|((u32)key[15]<<24);
    r0=t0&0x3ffffff; r1=((t0>>26)|(t1<<6))&0x3ffff03; r2=((t1>>20)|(t2<<12))&0x3ffc0ff;
    r3=((t2>>14)|(t3<<18))&0x3f03fff; r4=(t3>>8)&0x00fffff;
    s1=r1*5;s2=r2*5;s3=r3*5;s4=r4*5;
    int off=0;
    while(off<len){
        int k=len-off; if(k>16)k=16;
        u8 blk[17]; for(int i=0;i<17;i++)blk[i]=0;
        for(int i=0;i<k;i++)blk[i]=msg[off+i];
        blk[k]=1;  /* append 1 bit (block may be short: 1 right after data) */
        u32 c0=blk[0]|(blk[1]<<8)|(blk[2]<<16)|((u32)blk[3]<<24);
        u32 c1=blk[4]|(blk[5]<<8)|(blk[6]<<16)|((u32)blk[7]<<24);
        u32 c2=blk[8]|(blk[9]<<8)|(blk[10]<<16)|((u32)blk[11]<<24);
        u32 c3=blk[12]|(blk[13]<<8)|(blk[14]<<16)|((u32)blk[15]<<24);
        u32 hibit=blk[16]; /* the 1 bit, possibly in limb 4 region */
        h0+=c0&0x3ffffff;
        h1+=((c0>>26)|(c1<<6))&0x3ffffff;
        h2+=((c1>>20)|(c2<<12))&0x3ffffff;
        h3+=((c2>>14)|(c3<<18))&0x3ffffff;
        h4+=(c3>>8)|((u32)hibit<<24);
        u64 d0=(u64)h0*r0+(u64)h1*s4+(u64)h2*s3+(u64)h3*s2+(u64)h4*s1;
        u64 d1=(u64)h0*r1+(u64)h1*r0+(u64)h2*s4+(u64)h3*s3+(u64)h4*s2;
        u64 d2=(u64)h0*r2+(u64)h1*r1+(u64)h2*r0+(u64)h3*s4+(u64)h4*s3;
        u64 d3=(u64)h0*r3+(u64)h1*r2+(u64)h2*r1+(u64)h3*r0+(u64)h4*s4;
        u64 d4=(u64)h0*r4+(u64)h1*r3+(u64)h2*r2+(u64)h3*r1+(u64)h4*r0;
        u32 c;
        c=(u32)(d0>>26); h0=(u32)d0&0x3ffffff; d1+=c;
        c=(u32)(d1>>26); h1=(u32)d1&0x3ffffff; d2+=c;
        c=(u32)(d2>>26); h2=(u32)d2&0x3ffffff; d3+=c;
        c=(u32)(d3>>26); h3=(u32)d3&0x3ffffff; d4+=c;
        c=(u32)(d4>>26); h4=(u32)d4&0x3ffffff; h0+=c*5;
        c=h0>>26; h0&=0x3ffffff; h1+=c;
        off+=k;
    }
    /* final reduce */
    u32 c;
    c=h1>>26;h1&=0x3ffffff;h2+=c; c=h2>>26;h2&=0x3ffffff;h3+=c;
    c=h3>>26;h3&=0x3ffffff;h4+=c; c=h4>>26;h4&=0x3ffffff;h0+=c*5;
    c=h0>>26;h0&=0x3ffffff;h1+=c;
    /* compute h + -p (i.e. h - p), select if h>=p */
    u32 g0,g1,g2,g3,g4; u32 cc;
    g0=h0+5; cc=g0>>26; g0&=0x3ffffff;
    g1=h1+cc; cc=g1>>26; g1&=0x3ffffff;
    g2=h2+cc; cc=g2>>26; g2&=0x3ffffff;
    g3=h3+cc; cc=g3>>26; g3&=0x3ffffff;
    g4=h4+cc-(1<<26);
    u32 mask=(g4>>31)-1; /* if g4 negative (borrow) mask=0 -> keep h; else use g */
    g0&=mask;g1&=mask;g2&=mask;g3&=mask;g4&=mask;
    mask=~mask;
    h0=(h0&mask)|g0; h1=(h1&mask)|g1; h2=(h2&mask)|g2; h3=(h3&mask)|g3; h4=(h4&mask)|g4;
    /* serialize: combine 26-bit limbs into 128-bit little endian */
    u32 f0=(h0    |(h1<<26))&0xffffffff;
    u32 f1=((h1>>6)|(h2<<20))&0xffffffff;
    u32 f2=((h2>>12)|(h3<<14))&0xffffffff;
    u32 f3=((h3>>18)|(h4<<8))&0xffffffff;
    /* add s (key[16..31]) */
    u64 f; f=(u64)f0+ (key[16]|(key[17]<<8)|(key[18]<<16)|((u32)key[19]<<24));
    out[0]=f;out[1]=f>>8;out[2]=f>>16;out[3]=f>>24;
    f=(f>>32)+(u64)f1+(key[20]|(key[21]<<8)|(key[22]<<16)|((u32)key[23]<<24));
    out[4]=f;out[5]=f>>8;out[6]=f>>16;out[7]=f>>24;
    f=(f>>32)+(u64)f2+(key[24]|(key[25]<<8)|(key[26]<<16)|((u32)key[27]<<24));
    out[8]=f;out[9]=f>>8;out[10]=f>>16;out[11]=f>>24;
    f=(f>>32)+(u64)f3+(key[28]|(key[29]<<8)|(key[30]<<16)|((u32)key[31]<<24));
    out[12]=f;out[13]=f>>8;out[14]=f>>16;out[15]=f>>24;
}

/* ===================== ChaCha20-Poly1305 AEAD (RFC 8439) ===================== */
static void poly_key_gen(const u8 key[32],const u8 nonce[12],u8 polykey[32]){
    u32 s[16],blk[16]; chacha_state(s,key,0,nonce); chacha_block(s,blk);
    for(int i=0;i<8;i++){ polykey[i*4]=blk[i];polykey[i*4+1]=blk[i]>>8;polykey[i*4+2]=blk[i]>>16;polykey[i*4+3]=blk[i]>>24; }
}
static void aead_seal(const u8 key[32],const u8 nonce[12],const u8*aad,int aadlen,
                      const u8*pt,int ptlen,u8*ct,u8 tag[16]){
    u8 pk[32]; poly_key_gen(key,nonce,pk);
    chacha20(key,1,nonce,pt,ct,ptlen);
    /* mac over aad||pad16||ct||pad16||aadlen||ctlen */
    static u8* mac_data=(u8*)0x00900000u; int p=0;
    for(int i=0;i<aadlen;i++)mac_data[p++]=aad[i];
    while(p&15)mac_data[p++]=0;
    for(int i=0;i<ptlen;i++)mac_data[p++]=ct[i];
    while(p&15)mac_data[p++]=0;
    u64 al=aadlen, cl=ptlen;
    for(int i=0;i<8;i++)mac_data[p++]=(u8)(al>>(i*8));
    for(int i=0;i<8;i++)mac_data[p++]=(u8)(cl>>(i*8));
    poly1305(mac_data,p,pk,tag);
}
static int aead_open(const u8 key[32],const u8 nonce[12],const u8*aad,int aadlen,
                     const u8*ct,int ctlen,const u8 tag[16],u8*pt){
    u8 pk[32]; poly_key_gen(key,nonce,pk);
    static u8* mac_data=(u8*)0x00900000u; int p=0;
    for(int i=0;i<aadlen;i++)mac_data[p++]=aad[i];
    while(p&15)mac_data[p++]=0;
    for(int i=0;i<ctlen;i++)mac_data[p++]=ct[i];
    while(p&15)mac_data[p++]=0;
    u64 al=aadlen, cl=ctlen;
    for(int i=0;i<8;i++)mac_data[p++]=(u8)(al>>(i*8));
    for(int i=0;i<8;i++)mac_data[p++]=(u8)(cl>>(i*8));
    u8 t[16]; poly1305(mac_data,p,pk,t);
    int ok=0; for(int i=0;i<16;i++)ok|=t[i]^tag[i];
    if(ok)return 0;
    chacha20(key,1,nonce,ct,pt,ctlen);
    return 1;
}

/* ===================== X25519 (RFC 7748, tweetnacl-style gf) ===================== */
typedef i64 gf[16];
static const gf _121665={0xDB41,1};
static void car25519(gf o){ for(int i=0;i<16;i++){ o[i]+=(1LL<<16); i64 c=o[i]>>16; o[(i+1)*(i<15?1:0)]+=(i<15)?(c-1):0; if(i==15)o[0]+=38*(c-1); o[i]-=c<<16; } }
static void sel25519(gf p,gf q,int b){ i64 t,c=~(b-1); for(int i=0;i<16;i++){ t=c&(p[i]^q[i]); p[i]^=t; q[i]^=t; } }
static void gfA(gf o,const gf a,const gf b){ for(int i=0;i<16;i++)o[i]=a[i]+b[i]; }
static void gfZ(gf o,const gf a,const gf b){ for(int i=0;i<16;i++)o[i]=a[i]-b[i]; }
static void gfM(gf o,const gf a,const gf b){
    i64 t[31]; for(int i=0;i<31;i++)t[i]=0;
    for(int i=0;i<16;i++)for(int j=0;j<16;j++)t[i+j]+=a[i]*b[j];
    for(int i=0;i<15;i++)t[i]+=38*t[i+16];
    for(int i=0;i<16;i++)o[i]=t[i];
    car25519(o); car25519(o);
}
static void gfS(gf o,const gf a){ gfM(o,a,a); }
static void inv25519(gf o,const gf i){ gf c; for(int a=0;a<16;a++)c[a]=i[a];
    for(int a=253;a>=0;a--){ gfS(c,c); if(a!=2&&a!=4)gfM(c,c,i); }
    for(int a=0;a<16;a++)o[a]=c[a]; }
static void unpack25519(gf o,const u8*n){ for(int i=0;i<16;i++)o[i]=n[2*i]+((i64)n[2*i+1]<<8); o[15]&=0x7fff; }
static void pack25519(u8*o,const gf n){
    gf m,t; for(int i=0;i<16;i++)t[i]=n[i];
    car25519(t);car25519(t);car25519(t);
    for(int j=0;j<2;j++){ m[0]=t[0]-0xffed; for(int i=1;i<15;i++){ m[i]=t[i]-0xffff-((m[i-1]>>16)&1); m[i-1]&=0xffff; }
        m[15]=t[15]-0x7fff-((m[14]>>16)&1); int b=(m[15]>>16)&1; m[14]&=0xffff; sel25519(t,m,1-b); }
    for(int i=0;i<16;i++){ o[2*i]=t[i]&0xff; o[2*i+1]=t[i]>>8; }
}
static void x25519(u8*out,const u8*scalar,const u8*point){
    u8 z[32]; gf x,a,b,c,d,e,f; for(int i=0;i<32;i++)z[i]=scalar[i];
    z[31]&=127; z[31]|=64; z[0]&=248;
    unpack25519(x,point);
    for(int i=0;i<16;i++){ b[i]=x[i]; a[i]=c[i]=d[i]=0; } a[0]=d[0]=1;
    for(int i=254;i>=0;i--){ int bit=(z[i>>3]>>(i&7))&1;
        sel25519(a,b,bit); sel25519(c,d,bit);
        gfA(e,a,c); gfZ(a,a,c); gfA(c,b,d); gfZ(b,b,d);
        gfS(d,e); gfS(f,a); gfM(a,c,a); gfM(c,b,e);
        gfA(e,a,c); gfZ(a,a,c); gfS(b,a); gfZ(c,d,f);
        gfM(a,c,_121665); gfA(a,a,d); gfM(c,c,a); gfM(a,d,f);
        gfM(d,b,x); gfS(b,e);
        sel25519(a,b,bit); sel25519(c,d,bit); }
    inv25519(c,c); gfM(a,a,c); pack25519(out,a);
}

#endif
