typedef struct { u32 h[8]; u8 buf[64]; u64 len; u32 n; } sv_sha;

static const u32 SV_K[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

static u32 sv_ror(u32 x,int n){ return (x>>n)|(x<<(32-n)); }
static void sv_sha_init(sv_sha* c){
    c->h[0]=0x6a09e667; c->h[1]=0xbb67ae85; c->h[2]=0x3c6ef372; c->h[3]=0xa54ff53a; c->h[4]=0x510e527f; c->h[5]=0x9b05688c; c->h[6]=0x1f83d9ab; c->h[7]=0x5be0cd19;
    c->len=0; c->n=0; }
static void sv_sha_block(sv_sha* c,const u8* p){
    u32 w[64]; for(int i=0;i<16;i++)w[i]=((u32)p[i*4]<<24)|((u32)p[i*4+1]<<16)|((u32)p[i*4+2]<<8)|p[i*4+3];
    for(int i=16;i<64;i++){ u32 s0=sv_ror(w[i-15],7)^sv_ror(w[i-15],18)^(w[i-15]>>3), s1=sv_ror(w[i-2],17)^sv_ror(w[i-2],19)^(w[i-2]>>10); w[i]=w[i-16]+s0+w[i-7]+s1; }
    u32 a=c->h[0],b=c->h[1],cc=c->h[2],d=c->h[3],e=c->h[4],f=c->h[5],g=c->h[6],h=c->h[7];
    for(int i=0;i<64;i++){ u32 t1=h+(sv_ror(e,6)^sv_ror(e,11)^sv_ror(e,25))+((e&f)^(~e&g))+SV_K[i]+w[i]; u32 t2=(sv_ror(a,2)^sv_ror(a,13)^sv_ror(a,22))+((a&b)^(a&cc)^(b&cc));
        h=g; g=f; f=e; e=d+t1; d=cc; cc=b; b=a; a=t1+t2; }
    c->h[0]+=a; c->h[1]+=b; c->h[2]+=cc; c->h[3]+=d; c->h[4]+=e; c->h[5]+=f; c->h[6]+=g; c->h[7]+=h; }
static void sv_sha_update(sv_sha* c,const u8* p,u64 n){
    c->len+=n;
    if(c->n){ while(n&&c->n<64){ c->buf[c->n++]=*p++; n--; } if(c->n==64){ sv_sha_block(c,c->buf); c->n=0; } }
    while(n>=64){ sv_sha_block(c,p); p+=64; n-=64; }
    while(n){ c->buf[c->n++]=*p++; n--; } }
static void sv_sha_zeros(sv_sha* c,u64 n){ u8 z[64]; for(int i=0;i<64;i++)z[i]=0; while(n){ u64 k=n>64?64:n; sv_sha_update(c,z,k); n-=k; } }
static void sv_sha_final(sv_sha* c,u8* out){
    u64 bits=c->len*8; u8 pad=0x80; sv_sha_update(c,&pad,1); u8 z=0; while(c->n!=56)sv_sha_update(c,&z,1);
    u8 l[8]; for(int i=0;i<8;i++)l[i]=(u8)(bits>>(56-8*i)); sv_sha_update(c,l,8);
    for(int i=0;i<8;i++){ out[i*4]=(u8)(c->h[i]>>24); out[i*4+1]=(u8)(c->h[i]>>16); out[i*4+2]=(u8)(c->h[i]>>8); out[i*4+3]=(u8)c->h[i]; } }

typedef struct { u32 n[8]; u32 n0; u32 r2[8]; } sv_mont;
typedef struct { u32 x[8], y[8], z[8]; } sv_pt;
typedef struct { sv_mont mp, mn; u32 bm[8], gx[8], gy[8], one[8]; u8 pm2[32], nm2[32]; } sv_curve;

static const u8 SV_P[32]={0xff,0xff,0xff,0xff,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
static const u8 SV_N[32]={0xff,0xff,0xff,0xff,0,0,0,0,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xbc,0xe6,0xfa,0xad,0xa7,0x17,0x9e,0x84,0xf3,0xb9,0xca,0xc2,0xfc,0x63,0x25,0x51};
static const u8 SV_B[32]={0x5a,0xc6,0x35,0xd8,0xaa,0x3a,0x93,0xe7,0xb3,0xeb,0xbd,0x55,0x76,0x98,0x86,0xbc,0x65,0x1d,0x06,0xb0,0xcc,0x53,0xb0,0xf6,0x3b,0xce,0x3c,0x3e,0x27,0xd2,0x60,0x4b};
static const u8 SV_GX[32]={0x6b,0x17,0xd1,0xf2,0xe1,0x2c,0x42,0x47,0xf8,0xbc,0xe6,0xe5,0x63,0xa4,0x40,0xf2,0x77,0x03,0x7d,0x81,0x2d,0xeb,0x33,0xa0,0xf4,0xa1,0x39,0x45,0xd8,0x98,0xc2,0x96};
static const u8 SV_GY[32]={0x4f,0xe3,0x42,0xe2,0xfe,0x1a,0x7f,0x9b,0x8e,0xe7,0xeb,0x4a,0x7c,0x0f,0x9e,0x16,0x2b,0xce,0x33,0x57,0x6b,0x31,0x5e,0xce,0xcb,0xb6,0x40,0x68,0x37,0xbf,0x51,0xf5};

static int sv_cmp(const u32* a,const u32* b){ for(int i=7;i>=0;i--) if(a[i]!=b[i])return a[i]>b[i]?1:-1; return 0; }
static u32 sv_sub(u32* r,const u32* a,const u32* b){ u64 br=0; for(int i=0;i<8;i++){ u64 d=(u64)a[i]-b[i]-br; r[i]=(u32)d; br=(d>>32)&1; } return (u32)br; }
static u32 sv_add(u32* r,const u32* a,const u32* b){ u64 c=0; for(int i=0;i<8;i++){ u64 s=(u64)a[i]+b[i]+c; r[i]=(u32)s; c=s>>32; } return (u32)c; }
static int sv_zero(const u32* a){ for(int i=0;i<8;i++) if(a[i])return 0; return 1; }
static void sv_cpy(u32* r,const u32* a){ for(int i=0;i<8;i++)r[i]=a[i]; }
static void sv_set1(u32* r){ for(int i=0;i<8;i++)r[i]=0; r[0]=1; }
static void sv_from_be(u32* r,const u8* b){ for(int i=0;i<8;i++)r[i]=0; for(int i=0;i<32;i++){ int k=31-i; r[k/4]|=(u32)b[i]<<((k%4)*8); } }

static void sv_mmul(const sv_mont* m,u32* r,const u32* a,const u32* b){
    u32 t[10]; for(int i=0;i<10;i++)t[i]=0;
    for(int i=0;i<8;i++){
        u64 c=0; u32 bi=b[i];
        for(int j=0;j<8;j++){ u64 x=(u64)t[j]+(u64)a[j]*bi+c; t[j]=(u32)x; c=x>>32; }
        u64 x=(u64)t[8]+c; t[8]=(u32)x; t[9]=(u32)(x>>32);
        u32 mm=t[0]*m->n0;
        c=((u64)t[0]+(u64)mm*m->n[0])>>32;
        for(int j=1;j<8;j++){ u64 y=(u64)t[j]+(u64)mm*m->n[j]+c; t[j-1]=(u32)y; c=y>>32; }
        x=(u64)t[8]+c; t[7]=(u32)x; t[8]=t[9]+(u32)(x>>32);
    }
    if(t[8]||sv_cmp(t,m->n)>=0)sv_sub(t,t,m->n);
    sv_cpy(r,t);
}
static void sv_minit(sv_mont* m,const u8* nb){
    sv_from_be(m->n,nb);
    u32 inv=1; for(int i=0;i<5;i++)inv*=2-m->n[0]*inv; m->n0=(u32)(0-inv);
    u32* r=m->r2; sv_set1(r);
    for(int i=0;i<512;i++){ u32 c=0; for(int j=0;j<8;j++){ u32 nc=r[j]>>31; r[j]=(r[j]<<1)|c; c=nc; } if(c||sv_cmp(r,m->n)>=0)sv_sub(r,r,m->n); }
}
static void sv_to(const sv_mont* m,u32* r,const u32* a){ sv_mmul(m,r,a,m->r2); }
static void sv_back(const sv_mont* m,u32* r,const u32* a){ u32 one[8]; sv_set1(one); sv_mmul(m,r,a,one); }
static void sv_pow(const sv_mont* m,u32* r,const u32* base,const u8* e){
    u32 x[8], acc[8], one[8]; sv_to(m,x,base); sv_set1(one); sv_to(m,acc,one);
    for(int i=0;i<32;i++) for(int b=7;b>=0;b--){ sv_mmul(m,acc,acc,acc); if((e[i]>>b)&1)sv_mmul(m,acc,acc,x); }
    sv_back(m,r,acc);
}
static void sv_minus2(u8* o,const u8* b){ for(int i=0;i<32;i++)o[i]=b[i]; int i=31; int v=o[i]-2; if(v>=0){ o[i]=(u8)v; return; } o[i]=(u8)(v+256); i--; while(i>=0&&o[i]==0){ o[i]=0xFF; i--; } if(i>=0)o[i]--; }
static void sv_setup(sv_curve* c){
    u32 x[8];
    sv_minit(&c->mp,SV_P); sv_minit(&c->mn,SV_N); sv_minus2(c->pm2,SV_P); sv_minus2(c->nm2,SV_N);
    sv_from_be(x,SV_B); sv_to(&c->mp,c->bm,x);
    sv_from_be(x,SV_GX); sv_to(&c->mp,c->gx,x);
    sv_from_be(x,SV_GY); sv_to(&c->mp,c->gy,x);
    sv_set1(x); sv_to(&c->mp,c->one,x);
}
static void sv_fadd(const sv_curve* c,u32* r,const u32* a,const u32* b){ u32 cy=sv_add(r,a,b); if(cy||sv_cmp(r,c->mp.n)>=0)sv_sub(r,r,c->mp.n); }
static void sv_fsub(const sv_curve* c,u32* r,const u32* a,const u32* b){ if(sv_sub(r,a,b))sv_add(r,r,c->mp.n); }
static void sv_fmul(const sv_curve* c,u32* r,const u32* a,const u32* b){ sv_mmul(&c->mp,r,a,b); }
static void sv_dbl(const sv_curve* c,sv_pt* r,const sv_pt* p){
    if(sv_zero(p->z)){ *r=*p; return; }
    u32 d[8],g[8],be[8],al[8],t1[8],t2[8],x3[8],y3[8],z3[8];
    sv_fmul(c,d,p->z,p->z); sv_fmul(c,g,p->y,p->y); sv_fmul(c,be,p->x,g);
    sv_fsub(c,t1,p->x,d); sv_fadd(c,t2,p->x,d); sv_fmul(c,al,t1,t2); sv_fadd(c,t1,al,al); sv_fadd(c,al,t1,al);
    sv_fmul(c,x3,al,al); sv_fadd(c,t1,be,be); sv_fadd(c,t1,t1,t1); sv_fadd(c,t2,t1,t1); sv_fsub(c,x3,x3,t2);
    sv_fadd(c,t2,p->y,p->z); sv_fmul(c,z3,t2,t2); sv_fsub(c,z3,z3,g); sv_fsub(c,z3,z3,d);
    sv_fsub(c,t1,t1,x3); sv_fmul(c,y3,al,t1); sv_fmul(c,t2,g,g); sv_fadd(c,t2,t2,t2); sv_fadd(c,t2,t2,t2); sv_fadd(c,t2,t2,t2); sv_fsub(c,y3,y3,t2);
    sv_cpy(r->x,x3); sv_cpy(r->y,y3); sv_cpy(r->z,z3);
}
static void sv_padd(const sv_curve* c,sv_pt* r,const sv_pt* p,const sv_pt* q){
    if(sv_zero(p->z)){ *r=*q; return; } if(sv_zero(q->z)){ *r=*p; return; }
    u32 z1z1[8],z2z2[8],u1[8],u2[8],s1[8],s2[8],h[8],i[8],j[8],rr[8],v[8],t[8],x3[8],y3[8],z3[8];
    sv_fmul(c,z1z1,p->z,p->z); sv_fmul(c,z2z2,q->z,q->z);
    sv_fmul(c,u1,p->x,z2z2); sv_fmul(c,u2,q->x,z1z1);
    sv_fmul(c,t,q->z,z2z2); sv_fmul(c,s1,p->y,t); sv_fmul(c,t,p->z,z1z1); sv_fmul(c,s2,q->y,t);
    sv_fsub(c,h,u2,u1); sv_fsub(c,rr,s2,s1);
    if(sv_zero(h)){ if(sv_zero(rr)){ sv_dbl(c,r,p); return; } for(int k=0;k<8;k++){ r->x[k]=0; r->y[k]=0; r->z[k]=0; } return; }
    sv_fadd(c,rr,rr,rr);
    sv_fadd(c,t,h,h); sv_fmul(c,i,t,t); sv_fmul(c,j,h,i); sv_fmul(c,v,u1,i);
    sv_fmul(c,x3,rr,rr); sv_fsub(c,x3,x3,j); sv_fsub(c,x3,x3,v); sv_fsub(c,x3,x3,v);
    sv_fsub(c,t,v,x3); sv_fmul(c,y3,rr,t); sv_fmul(c,t,s1,j); sv_fadd(c,t,t,t); sv_fsub(c,y3,y3,t);
    sv_fadd(c,t,p->z,q->z); sv_fmul(c,z3,t,t); sv_fsub(c,z3,z3,z1z1); sv_fsub(c,z3,z3,z2z2); sv_fmul(c,z3,z3,h);
    sv_cpy(r->x,x3); sv_cpy(r->y,y3); sv_cpy(r->z,z3);
}
static int sv_p256_verify(const u8* pub,const u8* hash,const u8* sig){
    sv_curve cv; sv_curve* c=&cv; sv_setup(c);
    if(pub[0]!=4)return -1;
    u32 qx[8],qy[8],r[8],s[8],e[8],w[8],u1[8],u2[8],t[8],t2[8];
    sv_from_be(qx,pub+1); sv_from_be(qy,pub+33);
    if(sv_cmp(qx,c->mp.n)>=0||sv_cmp(qy,c->mp.n)>=0)return -1;
    sv_from_be(r,sig); sv_from_be(s,sig+32);
    if(sv_zero(r)||sv_zero(s)||sv_cmp(r,c->mn.n)>=0||sv_cmp(s,c->mn.n)>=0)return -1;
    sv_pt Q,G,GQ,R; sv_to(&c->mp,Q.x,qx); sv_to(&c->mp,Q.y,qy); sv_cpy(Q.z,c->one);
    sv_fmul(c,t,Q.y,Q.y); sv_fmul(c,t2,Q.x,Q.x); sv_fmul(c,t2,t2,Q.x);
    { u32 x3[8]; sv_fadd(c,x3,Q.x,Q.x); sv_fadd(c,x3,x3,Q.x); sv_fsub(c,t2,t2,x3); sv_fadd(c,t2,t2,c->bm); }
    if(sv_cmp(t,t2))return -1;
    sv_from_be(e,hash); if(sv_cmp(e,c->mn.n)>=0)sv_sub(e,e,c->mn.n);
    sv_pow(&c->mn,w,s,c->nm2);
    { u32 a[8],b[8]; sv_to(&c->mn,a,e); sv_to(&c->mn,b,w); sv_mmul(&c->mn,a,a,b); sv_back(&c->mn,u1,a);
      sv_to(&c->mn,a,r); sv_mmul(&c->mn,a,a,b); sv_back(&c->mn,u2,a); }
    sv_cpy(G.x,c->gx); sv_cpy(G.y,c->gy); sv_cpy(G.z,c->one);
    sv_padd(c,&GQ,&G,&Q);
    for(int k=0;k<8;k++){ R.x[k]=0; R.y[k]=0; R.z[k]=0; }
    for(int b=255;b>=0;b--){
        sv_dbl(c,&R,&R);
        int b1=(u1[b/32]>>(b%32))&1, b2=(u2[b/32]>>(b%32))&1;
        if(b1&&b2)sv_padd(c,&R,&R,&GQ); else if(b1)sv_padd(c,&R,&R,&G); else if(b2)sv_padd(c,&R,&R,&Q);
    }
    if(sv_zero(R.z))return -1;
    u32 zi[8],zz[8],x[8],zn[8];
    sv_back(&c->mp,zn,R.z); sv_pow(&c->mp,zi,zn,c->pm2);
    sv_to(&c->mp,t,zi); sv_fmul(c,zz,t,t); sv_fmul(c,t,R.x,zz); sv_back(&c->mp,x,t);
    if(sv_cmp(x,c->mn.n)>=0)sv_sub(x,x,c->mn.n);
    return sv_cmp(x,r)?-1:0;
}

#define SV_O_HASH 32
#define SV_O_FULL 88
#define SV_O_SBVER 128
#define SV_O_SVN 132
#define SV_O_KEYID 136
#define SV_O_SRHASH 144
#define SV_O_SRLEN 176
#define SV_O_IMM 180
#define SV_O_SIG 184
#define SV_O_PUB 248
#define SV_BLOCK 320

static u32 sv_rd32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static u64 sv_find_block(const u8* k,u64 n){
    static const u8 mx[8]={0xEB,0xF3,0xFD,0x93,0x91,0xF6,0xEC,0xE2};
    for(u64 i=0;i+SV_BLOCK<=n;i++){ if((k[i]^0xA5)!=mx[0])continue; int ok=1; for(int j=1;j<8;j++) if((k[i+j]^0xA5)!=mx[j]){ ok=0; break; } if(ok)return i; }
    return (u64)-1;
}
static void sv_file_hash(const u8* k,u64 n,u64 off,u8* out){
    sv_sha c; sv_sha_init(&c);
    sv_sha_update(&c,k,off+SV_O_HASH); sv_sha_zeros(&c,32);
    sv_sha_update(&c,k+off+SV_O_HASH+32,SV_O_FULL-SV_O_HASH-32); sv_sha_zeros(&c,32);
    sv_sha_update(&c,k+off+SV_O_FULL+32,SV_O_SIG-SV_O_FULL-32); sv_sha_zeros(&c,64);
    sv_sha_update(&c,k+off+SV_O_SIG+64,n-(off+SV_O_SIG+64));
    sv_sha_final(&c,out);
}
static int sv_kernel_verify(const u8* k,u64 n,const u8* pub,u64* offp){
    u64 off=sv_find_block(k,n); if(offp)*offp=off;
    if(off==(u64)-1)return -2;
    if(sv_rd32(k+off+SV_O_SBVER)!=1)return -3;
    if(pub){ for(int i=0;i<65;i++) if(k[off+SV_O_PUB+i]!=pub[i])return -4; }
    u32 imm=sv_rd32(k+off+SV_O_IMM); if(imm>n||off+SV_BLOCK>imm)return -5;
    u8 d[32]; sv_file_hash(k,n,off,d);
    u8 bad=0; for(int i=0;i<32;i++)bad|=d[i]^k[off+SV_O_FULL+i]; if(bad)return -6;
    if(sv_p256_verify(k+off+SV_O_PUB,d,k+off+SV_O_SIG))return -7;
    return 0;
}
static const char* sv_why(int r){
    switch(r){ case -2: return "no signature block"; case -3: return "not signed"; case -4: return "signed with another key"; case -5: return "signature block damaged";
        case -6: return "file changed after signing"; case -7: return "signature is wrong"; case -8: return "Linux files do not match the signed hash"; case -9: return "older than the lowest allowed version (rollback)"; }
    return "unknown error"; }
