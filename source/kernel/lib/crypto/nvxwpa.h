#ifndef NVXWPA_H
#define NVXWPA_H
static void (*g_wpa_pump)(void)=0;   /* optional UI-keepalive, set by kernel */

static u32 sha1_rol(u32 v,int n){ return (v<<n)|(v>>(32-n)); }
static void sha1_hash(const u8* msg,u32 len,u8 out[20]){
    u32 h0=0x67452301u,h1=0xEFCDAB89u,h2=0x98BADCFEu,h3=0x10325476u,h4=0xC3D2E1F0u;
    u64 ml=(u64)len*8;
    u8 blk[64]; u32 i;
    u32 full=len/64;
    for(u32 b=0;b<full;b++){
        const u8* p=msg+b*64; u32 w[80];
        for(i=0;i<16;i++) w[i]=((u32)p[i*4]<<24)|((u32)p[i*4+1]<<16)|((u32)p[i*4+2]<<8)|p[i*4+3];
        for(i=16;i<80;i++) w[i]=sha1_rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
        u32 a=h0,bb=h1,c=h2,d=h3,e=h4,f,k,t;
        for(i=0;i<80;i++){
            if(i<20){f=(bb&c)|((~bb)&d);k=0x5A827999u;}
            else if(i<40){f=bb^c^d;k=0x6ED9EBA1u;}
            else if(i<60){f=(bb&c)|(bb&d)|(c&d);k=0x8F1BBCDCu;}
            else {f=bb^c^d;k=0xCA62C1D6u;}
            t=sha1_rol(a,5)+f+e+k+w[i]; e=d;d=c;c=sha1_rol(bb,30);bb=a;a=t;
        }
        h0+=a;h1+=bb;h2+=c;h3+=d;h4+=e;
    }
    u32 rem=len%64; u8 last[128]; u32 lb=0;
    for(i=0;i<rem;i++) last[lb++]=msg[full*64+i];
    last[lb++]=0x80;
    u32 padto=(rem<56)?56:120;
    while(lb<padto) last[lb++]=0;
    for(int s=7;s>=0;s--) last[lb++]=(u8)(ml>>(s*8));
    for(u32 b=0;b<lb/64;b++){
        const u8* p=last+b*64; u32 w[80];
        for(i=0;i<16;i++) w[i]=((u32)p[i*4]<<24)|((u32)p[i*4+1]<<16)|((u32)p[i*4+2]<<8)|p[i*4+3];
        for(i=16;i<80;i++) w[i]=sha1_rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
        u32 a=h0,bb=h1,c=h2,d=h3,e=h4,f,k,t;
        for(i=0;i<80;i++){
            if(i<20){f=(bb&c)|((~bb)&d);k=0x5A827999u;}
            else if(i<40){f=bb^c^d;k=0x6ED9EBA1u;}
            else if(i<60){f=(bb&c)|(bb&d)|(c&d);k=0x8F1BBCDCu;}
            else {f=bb^c^d;k=0xCA62C1D6u;}
            t=sha1_rol(a,5)+f+e+k+w[i]; e=d;d=c;c=sha1_rol(bb,30);bb=a;a=t;
        }
        h0+=a;h1+=bb;h2+=c;h3+=d;h4+=e;
    }
    u32 hh[5]={h0,h1,h2,h3,h4};
    for(i=0;i<5;i++){ out[i*4]=(u8)(hh[i]>>24);out[i*4+1]=(u8)(hh[i]>>16);out[i*4+2]=(u8)(hh[i]>>8);out[i*4+3]=(u8)hh[i]; }
}

static void hmac_sha1(const u8* key,u32 klen,const u8* msg,u32 mlen,u8 out[20]){
    u8 k[64]; u32 i;
    u8 kh[20];
    if(klen>64){ sha1_hash(key,klen,kh); for(i=0;i<20;i++)k[i]=kh[i]; for(i=20;i<64;i++)k[i]=0; }
    else { for(i=0;i<klen;i++)k[i]=key[i]; for(i=klen;i<64;i++)k[i]=0; }
    u8 ip[64+256], op[64+20];
    for(i=0;i<64;i++) ip[i]=k[i]^0x36;
    for(i=0;i<mlen && i<256;i++) ip[64+i]=msg[i];
    u8 inner[20]; sha1_hash(ip,64+(mlen<256?mlen:256),inner);
    for(i=0;i<64;i++) op[i]=k[i]^0x5c;
    for(i=0;i<20;i++) op[64+i]=inner[i];
    sha1_hash(op,84,out);
}

static void pbkdf2_sha1(const u8* pw,u32 pwlen,const u8* salt,u32 saltlen,u32 iter,u8* out,u32 outlen){
    u32 blk=1; u32 done=0;
    while(done<outlen){
        u8 s2[64+4]; u32 i;
        for(i=0;i<saltlen && i<64;i++) s2[i]=salt[i];
        s2[saltlen]=(u8)(blk>>24);s2[saltlen+1]=(u8)(blk>>16);s2[saltlen+2]=(u8)(blk>>8);s2[saltlen+3]=(u8)blk;
        u8 u[20],t[20];
        hmac_sha1(pw,pwlen,s2,saltlen+4,u);
        for(i=0;i<20;i++)t[i]=u[i];
        for(u32 it=1;it<iter;it++){ hmac_sha1(pw,pwlen,u,20,u); for(i=0;i<20;i++)t[i]^=u[i]; if(g_wpa_pump && (it&511)==0)g_wpa_pump(); }
        u32 take=(outlen-done<20)?(outlen-done):20;
        for(i=0;i<take;i++) out[done+i]=t[i];
        done+=take; blk++;
    }
}

/* PRF-n (used to derive PTK): HMAC-SHA1 based, IEEE 802.11 */
static void wpa_prf(const u8* key,u32 klen,const char* label,const u8* data,u32 dlen,u8* out,u32 outlen){
    u8 buf[256]; u32 ll=0; while(label[ll])ll++;
    u32 done=0; u8 ctr=0;
    while(done<outlen){
        u32 i,n=0;
        for(i=0;i<ll;i++) buf[n++]=(u8)label[i];
        buf[n++]=0;
        for(i=0;i<dlen;i++) buf[n++]=data[i];
        buf[n++]=ctr;
        u8 dig[20]; hmac_sha1(key,klen,buf,n,dig);
        u32 take=(outlen-done<20)?(outlen-done):20;
        for(i=0;i<take;i++) out[done+i]=dig[i];
        done+=take; ctr++;
    }
}

/* ===== AES-CCM (CCMP) built on aes128_enc/aes128_expand from nvxtls.h ===== */
static void ccm_xor16(u8* a,const u8* b){ for(int i=0;i<16;i++)a[i]^=b[i]; }

/* CCM* per 802.11: L=2, M=8 (CCMP-128). nonce=13 bytes. */
static void aes_ccm_auth_encrypt(const u8 key[16],const u8 nonce[13],
                                 const u8* aad,int aadlen,
                                 u8* data,int dlen,u8 mic[8]){
    u8 rk[176]; aes128_expand(key,rk);
    u8 b0[16],x[16],s0[16],a[16];
    /* B0: flags | nonce | l(m) */
    b0[0]=(u8)( (1<<6) | (((8-2)/2)<<3) | (2-1) );  /* Adata=1, M=8, L=2 */
    for(int i=0;i<13;i++)b0[1+i]=nonce[i];
    b0[14]=(u8)(dlen>>8); b0[15]=(u8)dlen;
    aes128_enc(rk,b0,x);
    /* AAD block(s): 2-byte length prefix + aad, zero padded to 16 */
    { u8 ab[16]; int p=0; for(int i=0;i<16;i++)ab[i]=0;
      ab[p++]=(u8)(aadlen>>8); ab[p++]=(u8)aadlen;
      int off=0;
      while(off<aadlen){ if(p==16){ ccm_xor16(x,ab); aes128_enc(rk,x,x); p=0; for(int i=0;i<16;i++)ab[i]=0; }
          ab[p++]=aad[off++]; }
      ccm_xor16(x,ab); aes128_enc(rk,x,x);
    }
    /* payload CBC-MAC */
    { int off=0; u8 pb[16];
      while(off<dlen){ int n=0; for(int i=0;i<16;i++)pb[i]=0; while(n<16 && off<dlen){pb[n]=data[off];n++;off++;}
          ccm_xor16(x,pb); aes128_enc(rk,x,x); }
    }
    /* CTR: A0 for S0 (MIC), A1.. for data */
    a[0]=(u8)(2-1); for(int i=0;i<13;i++)a[1+i]=nonce[i]; a[14]=0;a[15]=0;
    aes128_enc(rk,a,s0);
    int off=0; u16 ctr=1;
    while(off<dlen){ a[14]=(u8)(ctr>>8); a[15]=(u8)ctr; u8 s[16]; aes128_enc(rk,a,s);
        for(int i=0;i<16 && off<dlen;i++){ data[off]^=s[i]; off++; } ctr++; }
    for(int i=0;i<8;i++) mic[i]=x[i]^s0[i];
}

static int aes_ccm_decrypt_verify(const u8 key[16],const u8 nonce[13],
                                  const u8* aad,int aadlen,
                                  u8* data,int dlen,const u8 mic[8]){
    u8 rk[176]; aes128_expand(key,rk);
    u8 a[16],s0[16];
    a[0]=(u8)(2-1); for(int i=0;i<13;i++)a[1+i]=nonce[i]; a[14]=0;a[15]=0;
    aes128_enc(rk,a,s0);
    int off=0; u16 ctr=1;
    while(off<dlen){ a[14]=(u8)(ctr>>8); a[15]=(u8)ctr; u8 s[16]; aes128_enc(rk,a,s);
        for(int i=0;i<16 && off<dlen;i++){ data[off]^=s[i]; off++; } ctr++; }
    /* recompute MIC over now-plaintext */
    u8 x[16],b0[16];
    b0[0]=(u8)( (1<<6) | (((8-2)/2)<<3) | (2-1) );
    for(int i=0;i<13;i++)b0[1+i]=nonce[i];
    b0[14]=(u8)(dlen>>8); b0[15]=(u8)dlen;
    aes128_enc(rk,b0,x);
    { u8 ab[16]; int p=0; for(int i=0;i<16;i++)ab[i]=0;
      ab[p++]=(u8)(aadlen>>8); ab[p++]=(u8)aadlen; int o=0;
      while(o<aadlen){ if(p==16){ ccm_xor16(x,ab); aes128_enc(rk,x,x); p=0; for(int i=0;i<16;i++)ab[i]=0; } ab[p++]=aad[o++]; }
      ccm_xor16(x,ab); aes128_enc(rk,x,x); }
    { int o=0; u8 pb[16];
      while(o<dlen){ int n=0; for(int i=0;i<16;i++)pb[i]=0; while(n<16 && o<dlen){pb[n]=data[o];n++;o++;}
          ccm_xor16(x,pb); aes128_enc(rk,x,x); } }
    for(int i=0;i<8;i++){ if((u8)(x[i]^s0[i])!=mic[i]) return 0; }
    return 1;
}

#endif

static u8 g_isb[256]; static int g_isb_ok=0;
static u8 gmul(u8 a,u8 b){ u8 p=0; while(b){ if(b&1)p^=a; a=xtime(a); b>>=1; } return p; }
static void aes128_dec(const u8 rk[176],const u8 in[16],u8 out[16]){
    if(!g_isb_ok){ for(int i=0;i<256;i++)g_isb[SB[i]]=(u8)i; g_isb_ok=1; }
    u8 s[16]; for(int i=0;i<16;i++)s[i]=in[i]^rk[160+i];
    for(int round=9;round>=0;round--){
        u8 r[16];
        r[0]=s[0]; r[4]=s[4]; r[8]=s[8]; r[12]=s[12];
        r[5]=s[1]; r[9]=s[5]; r[13]=s[9]; r[1]=s[13];
        r[10]=s[2];r[14]=s[6];r[2]=s[10]; r[6]=s[14];
        r[15]=s[3];r[3]=s[7]; r[7]=s[11]; r[11]=s[15];
        for(int i=0;i<16;i++)s[i]=g_isb[r[i]]^rk[round*16+i];
        if(round>0) for(int c=0;c<4;c++){ u8* p=s+c*4; u8 a0=p[0],a1=p[1],a2=p[2],a3=p[3];
            p[0]=gmul(a0,14)^gmul(a1,11)^gmul(a2,13)^gmul(a3,9);
            p[1]=gmul(a0,9)^gmul(a1,14)^gmul(a2,11)^gmul(a3,13);
            p[2]=gmul(a0,13)^gmul(a1,9)^gmul(a2,14)^gmul(a3,11);
            p[3]=gmul(a0,11)^gmul(a1,13)^gmul(a2,9)^gmul(a3,14); }
    }
    for(int i=0;i<16;i++)out[i]=s[i];
}
static int aes_key_unwrap(const u8 kek[16],const u8* in,int inlen,u8* out){
    if(inlen<24||(inlen&7))return -1;
    int n=inlen/8-1; u8 rk[176]; aes128_expand(kek,rk);
    u8 A[8]; for(int i=0;i<8;i++)A[i]=in[i];
    for(int i=0;i<n*8;i++)out[i]=in[8+i];
    for(int j=5;j>=0;j--) for(int i=n;i>=1;i--){
        unsigned long long t=(unsigned long long)n*(unsigned)j+(unsigned)i;
        u8 B[16]; for(int k=0;k<8;k++)B[k]=A[k]^(u8)(t>>(8*(7-k)));
        for(int k=0;k<8;k++)B[8+k]=out[(i-1)*8+k];
        aes128_dec(rk,B,B);
        for(int k=0;k<8;k++){ A[k]=B[k]; out[(i-1)*8+k]=B[8+k]; } }
    for(int i=0;i<8;i++) if(A[i]!=0xA6)return -1;
    return n*8;
}
