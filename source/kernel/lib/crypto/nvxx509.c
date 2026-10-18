#include "include/nvx/base.h"
#include "include/nvx/lib/crypto/nvxcrypto2.h"
#include "include/nvx/lib/crypto/nvxx509.h"
#include "include/nvx/lib/crypto/nvxroots.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/lib/js.h"
#include "include/nvx/lib/crypto/nvxcrypto.h"
#include "include/nvx/gui/wm.h"
#define BN_MAXL 130
typedef struct { int L; u32 n[BN_MAXL]; u32 n0; u32 r2[BN_MAXL]; } mont_t;

static int bn_cmp(const u32* a,const u32* b,int L){ for(int i=L-1;i>=0;i--){ if(a[i]!=b[i])return a[i]>b[i]?1:-1; } return 0; }
static u32 bn_sub(u32* r,const u32* a,const u32* b,int L){ u64 br=0; for(int i=0;i<L;i++){ u64 d=(u64)a[i]-b[i]-br; r[i]=(u32)d; br=(d>>32)&1; } return (u32)br; }
static u32 bn_add(u32* r,const u32* a,const u32* b,int L){ u64 c=0; for(int i=0;i<L;i++){ u64 s=(u64)a[i]+b[i]+c; r[i]=(u32)s; c=s>>32; } return (u32)c; }
static int bn_zero(const u32* a,int L){ for(int i=0;i<L;i++) if(a[i])return 0; return 1; }
static void bn_copy(u32* r,const u32* a,int L){ for(int i=0;i<L;i++)r[i]=a[i]; }
static void bn_set(u32* r,u32 v,int L){ for(int i=0;i<L;i++)r[i]=0; r[0]=v; }
static int bn_from_be(u32* r,int L,const u8* b,int n){
    while(n>0&&!*b){ b++; n--; }
    if(n>L*4)return -1;
    for(int i=0;i<L;i++)r[i]=0;
    for(int i=0;i<n;i++){ int k=n-1-i; r[k/4]|=(u32)b[i]<<((k%4)*8); }
    return 0;
}
static void bn_to_be(u8* o,int n,const u32* a,int L){ for(int i=0;i<n;i++){ int k=n-1-i; o[i]=(k/4<L)?(u8)(a[k/4]>>((k%4)*8)):0; } }
static int bn_bits(const u32* a,int L){ for(int i=L-1;i>=0;i--) if(a[i]){ int b=32; while(!(a[i]>>(b-1)))b--; return i*32+b; } return 0; }

static void mont_mul(const mont_t* m,u32* r,const u32* a,const u32* b){
    u32 t[BN_MAXL+2]; int L=m->L; for(int i=0;i<L+2;i++)t[i]=0;
    for(int i=0;i<L;i++){
        u64 c=0; u32 bi=b[i];
        for(int j=0;j<L;j++){ u64 x=(u64)t[j]+(u64)a[j]*bi+c; t[j]=(u32)x; c=x>>32; }
        u64 x=(u64)t[L]+c; t[L]=(u32)x; t[L+1]=(u32)(x>>32);
        u32 mm=t[0]*m->n0;
        c=((u64)t[0]+(u64)mm*m->n[0])>>32;
        for(int j=1;j<L;j++){ u64 y=(u64)t[j]+(u64)mm*m->n[j]+c; t[j-1]=(u32)y; c=y>>32; }
        x=(u64)t[L]+c; t[L-1]=(u32)x; t[L]=t[L+1]+(u32)(x>>32);
    }
    if(t[L]||bn_cmp(t,m->n,L)>=0)bn_sub(t,t,m->n,L);
    bn_copy(r,t,L);
}
static int mont_init(mont_t* m,const u8* nb,int nlen){
    while(nlen>0&&!*nb){ nb++; nlen--; }
    int L=(nlen+3)/4; if(L<1||L>BN_MAXL)return -1;
    m->L=L; if(bn_from_be(m->n,L,nb,nlen))return -1;
    if(!(m->n[0]&1))return -1;
    u32 inv=1; for(int i=0;i<5;i++)inv*=2-m->n[0]*inv; m->n0=(u32)(0-inv);
    u32* r=m->r2; bn_set(r,1,L);
    for(int i=0;i<64*L;i++){ u32 c=0; for(int j=0;j<L;j++){ u32 nc=r[j]>>31; r[j]=(r[j]<<1)|c; c=nc; } if(c||bn_cmp(r,m->n,L)>=0)bn_sub(r,r,m->n,L); }
    return 0;
}
static void mont_to(const mont_t* m,u32* r,const u32* a){ mont_mul(m,r,a,m->r2); }
static void mont_from(const mont_t* m,u32* r,const u32* a){ u32 one[BN_MAXL]; bn_set(one,1,m->L); mont_mul(m,r,a,one); }
static void mont_modmul(const mont_t* m,u32* r,const u32* a,const u32* b){ u32 t[BN_MAXL]; mont_mul(m,t,a,b); mont_mul(m,r,t,m->r2); }
static void mont_pow(const mont_t* m,u32* r,const u32* base,const u8* e,int elen){
    static u32 x[BN_MAXL], acc[BN_MAXL], one[BN_MAXL];
    int L=m->L; mont_to(m,x,base); bn_set(one,1,L); mont_to(m,acc,one);
    for(int i=0;i<elen;i++) for(int b=7;b>=0;b--){ mont_mul(m,acc,acc,acc); if((e[i]>>b)&1)mont_mul(m,acc,acc,x); }
    mont_from(m,r,acc);
}

enum { XH_SHA256=1, XH_SHA384=2, XH_SHA512=3 };
static int xh_len(int h){ return h==XH_SHA256?32:h==XH_SHA384?48:h==XH_SHA512?64:0; }
static int xh_hash(int h,const u8* d,int n,u8* out){
    if(h==XH_SHA256){ sha256_hash(d,n,out); return 32; }
    if(h==XH_SHA384){ sha384_hash(d,n,out); return 48; }
    if(h==XH_SHA512){ sha512_hash(d,n,out); return 64; }
    return 0;
}

static mont_t g_rsa_m;
static int rsa_public(const u8* n,int nlen,const u8* e,int elen,const u8* sig,int slen,u8* em,int* k){
    while(nlen>0&&!*n){ n++; nlen--; } while(elen>0&&!*e){ e++; elen--; }
    if(nlen<128||nlen>512||elen<1||elen>8)return -1;
    if(slen!=nlen)return -1;
    if(mont_init(&g_rsa_m,n,nlen))return -1;
    static u32 s[BN_MAXL], m[BN_MAXL];
    if(bn_from_be(s,g_rsa_m.L,sig,slen))return -1;
    if(bn_cmp(s,g_rsa_m.n,g_rsa_m.L)>=0)return -1;
    mont_pow(&g_rsa_m,m,s,e,elen);
    bn_to_be(em,nlen,m,g_rsa_m.L); *k=nlen; return 0;
}
static const u8 di_sha256[]={0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20};
static const u8 di_sha384[]={0x30,0x41,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x02,0x05,0x00,0x04,0x30};
static const u8 di_sha512[]={0x30,0x51,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x03,0x05,0x00,0x04,0x40};
static int rsa_pkcs1_verify(const u8* n,int nlen,const u8* e,int elen,int h,const u8* hash,const u8* sig,int slen){
    static u8 em[512]; int k;
    if(rsa_public(n,nlen,e,elen,sig,slen,em,&k))return -1;
    const u8* di=h==XH_SHA256?di_sha256:h==XH_SHA384?di_sha384:di_sha512; int dl=19, hl=xh_len(h);
    int tl=dl+hl; if(k<tl+11)return -1;
    if(em[0]!=0||em[1]!=1)return -1;
    int ps=k-tl-3; for(int i=0;i<ps;i++) if(em[2+i]!=0xFF)return -1;
    if(em[2+ps]!=0)return -1;
    u8 bad=0; for(int i=0;i<dl;i++)bad|=em[3+ps+i]^di[i]; for(int i=0;i<hl;i++)bad|=em[3+ps+dl+i]^hash[i];
    return bad?-1:0;
}
static void mgf1(int h,const u8* seed,int sl,u8* out,int olen){
    u8 buf[80], d[64]; int hl=xh_len(h); for(int i=0;i<sl;i++)buf[i]=seed[i];
    for(u32 c=0,o=0;(int)o<olen;c++){ buf[sl]=(u8)(c>>24); buf[sl+1]=(u8)(c>>16); buf[sl+2]=(u8)(c>>8); buf[sl+3]=(u8)c;
        xh_hash(h,buf,sl+4,d); for(int i=0;i<hl&&(int)o<olen;i++)out[o++]=d[i]; }
}
static int rsa_pss_verify(const u8* n,int nlen,const u8* e,int elen,int h,const u8* mhash,const u8* sig,int slen){
    static u8 em[512], db[512]; int k;
    if(rsa_public(n,nlen,e,elen,sig,slen,em,&k))return -1;
    int modbits=bn_bits(g_rsa_m.n,g_rsa_m.L); int embits=modbits-1; int emlen=(embits+7)/8;
    const u8* E=em; if(emlen<k){ if(em[0])return -1; E=em+1; }
    int hl=xh_len(h), sl=hl;
    if(emlen<hl+sl+2)return -1;
    if(E[emlen-1]!=0xBC)return -1;
    int dbl=emlen-hl-1; const u8* H=E+dbl;
    int zb=8*emlen-embits; if(zb&&(E[0]>>(8-zb)))return -1;
    mgf1(h,H,hl,db,dbl);
    for(int i=0;i<dbl;i++)db[i]^=E[i];
    if(zb)db[0]&=(u8)(0xFF>>zb);
    int ps=emlen-hl-sl-2; for(int i=0;i<ps;i++) if(db[i])return -1;
    if(db[ps]!=1)return -1;
    u8 mp[8+64+64]; for(int i=0;i<8;i++)mp[i]=0; for(int i=0;i<hl;i++)mp[8+i]=mhash[i]; for(int i=0;i<sl;i++)mp[8+hl+i]=db[dbl-sl+i];
    u8 hh[64]; xh_hash(h,mp,8+hl+sl,hh);
    u8 bad=0; for(int i=0;i<hl;i++)bad|=hh[i]^H[i]; return bad?-1:0;
}

typedef struct { int L, bytes; const char *p,*n,*b,*gx,*gy; mont_t mp, mn; int ready; u32 bm[12], gxm[12], gym[12], pm2[12]; u8 pm2b[48], nm2b[48]; } ec_curve;
static ec_curve g_p256={8,32,"ffffffff00000001000000000000000000000000ffffffffffffffffffffffff","ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551","5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b","6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296","4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5"};
static ec_curve g_p384={12,48,"fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffeffffffff0000000000000000ffffffff","ffffffffffffffffffffffffffffffffffffffffffffffffc7634d81f4372ddf581a0db248b0a77aecec196accc52973","b3312fa7e23ee7e4988e056be3f82d19181d9c6efe8141120314088f5013875ac656398d8a2ed19d2a85c8edd3ec2aef","aa87ca22be8b05378eb1c71ef320ad746e1d3b628ba79b9859f741e082542a385502f25dbf55296c3a545e3872760ab7","3617de4a96262c6f5d9e98bf9292dc29f8f41dbd289a147ce9da3113b5f0b8c00a60b1ce1d7e819d7a431d7c90ea0e5f"};
static void hex2b(const char* h,u8* o,int n){ for(int i=0;i<n;i++){ int v=0; for(int k=0;k<2;k++){ char c=h[i*2+k]; v=v*16+(c<='9'?c-'0':(c|32)-'a'+10); } o[i]=(u8)v; } }
static void be_minus2(u8* b,int n){ int i=n-1; int v=b[i]-2; if(v>=0){ b[i]=(u8)v; return; } b[i]=(u8)(v+256); i--; while(i>=0&&b[i]==0){ b[i]=0xFF; i--; } if(i>=0)b[i]--; }
static void ec_setup(ec_curve* c){
    if(c->ready)return; u8 t[48]; u32 x[12]; int n=c->bytes;
    hex2b(c->p,t,n); mont_init(&c->mp,t,n); for(int i=0;i<n;i++)c->pm2b[i]=t[i]; be_minus2(c->pm2b,n);
    hex2b(c->n,t,n); mont_init(&c->mn,t,n); for(int i=0;i<n;i++)c->nm2b[i]=t[i]; be_minus2(c->nm2b,n);
    hex2b(c->b,t,n); bn_from_be(x,c->L,t,n); mont_to(&c->mp,c->bm,x);
    hex2b(c->gx,t,n); bn_from_be(x,c->L,t,n); mont_to(&c->mp,c->gxm,x);
    hex2b(c->gy,t,n); bn_from_be(x,c->L,t,n); mont_to(&c->mp,c->gym,x);
    c->ready=1;
}
typedef struct { u32 x[12], y[12], z[12]; } ecp;
static void fe_add(const ec_curve* c,u32* r,const u32* a,const u32* b){ u32 cy=bn_add(r,a,b,c->L); if(cy||bn_cmp(r,c->mp.n,c->L)>=0)bn_sub(r,r,c->mp.n,c->L); }
static void fe_sub(const ec_curve* c,u32* r,const u32* a,const u32* b){ if(bn_sub(r,a,b,c->L))bn_add(r,r,c->mp.n,c->L); }
static void fe_mul(const ec_curve* c,u32* r,const u32* a,const u32* b){ mont_mul(&c->mp,r,a,b); }
static void ec_dbl(const ec_curve* c,ecp* r,const ecp* p){
    int L=c->L; if(bn_zero(p->z,L)){ *r=*p; return; }
    u32 d[12],g[12],be[12],al[12],t1[12],t2[12],x3[12],y3[12],z3[12];
    fe_mul(c,d,p->z,p->z); fe_mul(c,g,p->y,p->y); fe_mul(c,be,p->x,g);
    fe_sub(c,t1,p->x,d); fe_add(c,t2,p->x,d); fe_mul(c,al,t1,t2); fe_add(c,t1,al,al); fe_add(c,al,t1,al);
    fe_mul(c,x3,al,al); fe_add(c,t1,be,be); fe_add(c,t1,t1,t1); fe_add(c,t2,t1,t1); fe_sub(c,x3,x3,t2);
    fe_add(c,t2,p->y,p->z); fe_mul(c,z3,t2,t2); fe_sub(c,z3,z3,g); fe_sub(c,z3,z3,d);
    fe_sub(c,t1,t1,x3); fe_mul(c,y3,al,t1); fe_mul(c,t2,g,g); fe_add(c,t2,t2,t2); fe_add(c,t2,t2,t2); fe_add(c,t2,t2,t2); fe_sub(c,y3,y3,t2);
    bn_copy(r->x,x3,L); bn_copy(r->y,y3,L); bn_copy(r->z,z3,L);
}
static void ec_add(const ec_curve* c,ecp* r,const ecp* p,const ecp* q){
    int L=c->L; if(bn_zero(p->z,L)){ *r=*q; return; } if(bn_zero(q->z,L)){ *r=*p; return; }
    u32 z1z1[12],z2z2[12],u1[12],u2[12],s1[12],s2[12],h[12],i[12],j[12],rr[12],v[12],t[12],x3[12],y3[12],z3[12];
    fe_mul(c,z1z1,p->z,p->z); fe_mul(c,z2z2,q->z,q->z);
    fe_mul(c,u1,p->x,z2z2); fe_mul(c,u2,q->x,z1z1);
    fe_mul(c,t,q->z,z2z2); fe_mul(c,s1,p->y,t); fe_mul(c,t,p->z,z1z1); fe_mul(c,s2,q->y,t);
    fe_sub(c,h,u2,u1); fe_sub(c,rr,s2,s1);
    if(bn_zero(h,L)){ if(bn_zero(rr,L)){ ec_dbl(c,r,p); return; } for(int k=0;k<L;k++){ r->x[k]=0; r->y[k]=0; r->z[k]=0; } return; }
    fe_add(c,rr,rr,rr);
    fe_add(c,t,h,h); fe_mul(c,i,t,t); fe_mul(c,j,h,i); fe_mul(c,v,u1,i);
    fe_mul(c,x3,rr,rr); fe_sub(c,x3,x3,j); fe_sub(c,x3,x3,v); fe_sub(c,x3,x3,v);
    fe_sub(c,t,v,x3); fe_mul(c,y3,rr,t); fe_mul(c,t,s1,j); fe_add(c,t,t,t); fe_sub(c,y3,y3,t);
    fe_add(c,t,p->z,q->z); fe_mul(c,z3,t,t); fe_sub(c,z3,z3,z1z1); fe_sub(c,z3,z3,z2z2); fe_mul(c,z3,z3,h);
    bn_copy(r->x,x3,L); bn_copy(r->y,y3,L); bn_copy(r->z,z3,L);
}
static int ecdsa_verify(ec_curve* c,const u8* pt,int ptlen,const u8* hash,int hlen,const u8* r_be,int rlen,const u8* s_be,int slen){
    ec_setup(c); int L=c->L, n=c->bytes;
    if(ptlen!=1+2*n||pt[0]!=4)return -1;
    u32 qx[12],qy[12],r[12],s[12],e[12],w[12],u1[12],u2[12],one[12],t[12],t2[12];
    if(bn_from_be(qx,L,pt+1,n)||bn_from_be(qy,L,pt+1+n,n))return -1;
    if(bn_cmp(qx,c->mp.n,L)>=0||bn_cmp(qy,c->mp.n,L)>=0)return -1;
    if(bn_from_be(r,L,r_be,rlen)||bn_from_be(s,L,s_be,slen))return -1;
    if(bn_zero(r,L)||bn_zero(s,L)||bn_cmp(r,c->mn.n,L)>=0||bn_cmp(s,c->mn.n,L)>=0)return -1;
    ecp Q,G,GQ,R; mont_to(&c->mp,Q.x,qx); mont_to(&c->mp,Q.y,qy); bn_set(one,1,L); mont_to(&c->mp,Q.z,one);
    fe_mul(c,t,Q.y,Q.y); fe_mul(c,t2,Q.x,Q.x); fe_mul(c,t2,t2,Q.x);
    { u32 x3[12]; fe_add(c,x3,Q.x,Q.x); fe_add(c,x3,x3,Q.x); fe_sub(c,t2,t2,x3); fe_add(c,t2,t2,c->bm); }
    if(bn_cmp(t,t2,L))return -1;
    u8 eb[48]; int el=hlen<n?hlen:n; for(int i=0;i<n;i++)eb[i]=0; for(int i=0;i<el;i++)eb[n-el+i]=hash[i];
    bn_from_be(e,L,eb,n); if(bn_cmp(e,c->mn.n,L)>=0)bn_sub(e,e,c->mn.n,L);
    mont_pow(&c->mn,w,s,c->nm2b,n);
    mont_modmul(&c->mn,u1,e,w); mont_modmul(&c->mn,u2,r,w);
    bn_copy(G.x,c->gxm,L); bn_copy(G.y,c->gym,L); mont_to(&c->mp,G.z,one);
    ec_add(c,&GQ,&G,&Q);
    for(int k=0;k<L;k++){ R.x[k]=0; R.y[k]=0; R.z[k]=0; }
    for(int b=L*32-1;b>=0;b--){
        ec_dbl(c,&R,&R);
        int b1=(u1[b/32]>>(b%32))&1, b2=(u2[b/32]>>(b%32))&1;
        if(b1&&b2)ec_add(c,&R,&R,&GQ); else if(b1)ec_add(c,&R,&R,&G); else if(b2)ec_add(c,&R,&R,&Q);
    }
    if(bn_zero(R.z,L))return -1;
    u32 zi[12],zz[12],x[12],zn[12];
    mont_from(&c->mp,zn,R.z); mont_pow(&c->mp,zi,zn,c->pm2b,n);
    mont_to(&c->mp,t,zi); fe_mul(c,zz,t,t); fe_mul(c,t,R.x,zz); mont_from(&c->mp,x,t);
    if(bn_cmp(x,c->mn.n,L)>=0)bn_sub(x,x,c->mn.n,L);
    return bn_cmp(x,r,L)?-1:0;
}

typedef struct { const u8* p; const u8* end; } der_cur;
static int der_get(der_cur* c,int* tag,const u8** v,int* len){
    if(c->p>=c->end||c->end-c->p<2)return -1;
    *tag=c->p[0]; int l=c->p[1]; const u8* q=c->p+2;
    if(l&0x80){ int nb=l&0x7F; if(nb<1||nb>3||c->end-q<nb)return -1; l=0; for(int i=0;i<nb;i++)l=(l<<8)|*q++; }
    if(l>c->end-q)return -1;
    *v=q; *len=l; c->p=q+l; return 0;
}
static int der_expect(der_cur* c,int tag,const u8** v,int* len){ int t; if(der_get(c,&t,v,len)||t!=tag)return -1; return 0; }
static int oid_is(const u8* v,int l,const u8* o,int ol){ if(l!=ol)return 0; for(int i=0;i<l;i++) if(v[i]!=o[i])return 0; return 1; }
static const u8 OID_RSA[]={0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x01};
static const u8 OID_RSA256[]={0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x0B};
static const u8 OID_RSA384[]={0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x0C};
static const u8 OID_RSA512[]={0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x0D};
static const u8 OID_ECPUB[]={0x2A,0x86,0x48,0xCE,0x3D,0x02,0x01};
static const u8 OID_P256[]={0x2A,0x86,0x48,0xCE,0x3D,0x03,0x01,0x07};
static const u8 OID_P384[]={0x2B,0x81,0x04,0x00,0x22};
static const u8 OID_ECDSA256[]={0x2A,0x86,0x48,0xCE,0x3D,0x04,0x03,0x02};
static const u8 OID_ECDSA384[]={0x2A,0x86,0x48,0xCE,0x3D,0x04,0x03,0x03};
static const u8 OID_ECDSA512[]={0x2A,0x86,0x48,0xCE,0x3D,0x04,0x03,0x04};
static const u8 OID_SAN[]={0x55,0x1D,0x11};
static const u8 OID_BC[]={0x55,0x1D,0x13};
static const u8 OID_CN[]={0x55,0x04,0x03};

enum { XK_RSA=1, XK_P256=2, XK_P384=3 };
typedef struct { int type; const u8 *n,*e,*pt; int nlen,elen,ptlen; } x509_key;
static int x509_parse_spki(const u8* spki,int len,x509_key* k){
    der_cur c={spki,spki+len}; const u8* v; int l;
    if(der_expect(&c,0x30,&v,&l))return -1; der_cur s={v,v+l};
    const u8* av; int al; if(der_expect(&s,0x30,&av,&al))return -1; der_cur a={av,av+al};
    const u8* oid; int ol; if(der_expect(&a,0x06,&oid,&ol))return -1;
    const u8* bs; int bl; if(der_expect(&s,0x03,&bs,&bl)||bl<2||bs[0])return -1; bs++; bl--;
    if(oid_is(oid,ol,OID_RSA,sizeof OID_RSA)){
        der_cur r={bs,bs+bl}; const u8* sv; int sl; if(der_expect(&r,0x30,&sv,&sl))return -1; der_cur q={sv,sv+sl};
        if(der_expect(&q,0x02,&k->n,&k->nlen)||der_expect(&q,0x02,&k->e,&k->elen))return -1;
        while(k->nlen>0&&!k->n[0]){ k->n++; k->nlen--; }
        k->type=XK_RSA; return 0; }
    if(oid_is(oid,ol,OID_ECPUB,sizeof OID_ECPUB)){
        const u8* cv; int cl; if(der_expect(&a,0x06,&cv,&cl))return -1;
        if(oid_is(cv,cl,OID_P256,sizeof OID_P256))k->type=XK_P256; else if(oid_is(cv,cl,OID_P384,sizeof OID_P384))k->type=XK_P384; else return -1;
        k->pt=bs; k->ptlen=bl; return 0; }
    return -1;
}
enum { XS_RSA256=1, XS_RSA384, XS_RSA512, XS_EC256, XS_EC384, XS_EC512 };
typedef struct {
    const u8 *tbs,*issuer,*subject,*spki,*sig,*san,*cn;
    int tbslen,issuerlen,subjectlen,spkilen,siglen,sanlen,cnlen,sigalg,is_ca,has_san;
    i64 nb, na;
} x509_cert;
static i64 x509_days(int y,int m,int d){ y-=m<=2; i64 era=(y>=0?y:y-399)/400; int yoe=y-(int)era*400; int doy=(153*(m+(m>2?-3:9))+2)/5+d-1; int doe=yoe*365+yoe/4-yoe/100+doy; return era*146097+doe-719468; }
static int x509_time(int tag,const u8* v,int l,i64* out){
    int y,i=0; for(int k=0;k<l;k++) if(k<l-1&&(v[k]<'0'||v[k]>'9'))return -1;
    if(tag==0x17){ if(l<13)return -1; y=(v[0]-'0')*10+(v[1]-'0'); y+=y<50?2000:1900; i=2; }
    else if(tag==0x18){ if(l<15)return -1; y=(v[0]-'0')*1000+(v[1]-'0')*100+(v[2]-'0')*10+(v[3]-'0'); i=4; }
    else return -1;
    int f[5]; for(int k=0;k<5;k++)f[k]=(v[i+2*k]-'0')*10+(v[i+2*k+1]-'0');
    *out=x509_days(y,f[0],f[1])*86400+f[2]*3600+f[3]*60+f[4]; return 0;
}
static int x509_name_cn(const u8* v,int l,const u8** cn,int* cnl){
    der_cur c={v,v+l}; const u8* sv; int sl; int t;
    while(der_get(&c,&t,&sv,&sl)==0){ if(t!=0x31)continue; der_cur s={sv,sv+sl}; const u8* av; int al;
        while(der_expect(&s,0x30,&av,&al)==0){ der_cur a={av,av+al}; const u8* o; int ol; const u8* sv2; int sl2; int t2;
            if(der_expect(&a,0x06,&o,&ol)||der_get(&a,&t2,&sv2,&sl2))continue;
            if(oid_is(o,ol,OID_CN,sizeof OID_CN)){ *cn=sv2; *cnl=sl2; } } }
    return 0;
}
static int x509_parse(const u8* d,int len,x509_cert* x){
    u8* z=(u8*)x; for(int i=0;i<(int)sizeof *x;i++)z[i]=0;
    der_cur c={d,d+len}; const u8* cv; int cl; if(der_expect(&c,0x30,&cv,&cl))return -1;
    der_cur C={cv,cv+cl}; const u8* tv; int tl; const u8* tstart=C.p;
    if(der_expect(&C,0x30,&tv,&tl))return -1; x->tbs=tstart; x->tbslen=(int)(C.p-tstart);
    const u8* av; int al; if(der_expect(&C,0x30,&av,&al))return -1;
    { der_cur a={av,av+al}; const u8* o; int ol; if(der_expect(&a,0x06,&o,&ol))return -1;
      if(oid_is(o,ol,OID_RSA256,sizeof OID_RSA256))x->sigalg=XS_RSA256; else if(oid_is(o,ol,OID_RSA384,sizeof OID_RSA384))x->sigalg=XS_RSA384;
      else if(oid_is(o,ol,OID_RSA512,sizeof OID_RSA512))x->sigalg=XS_RSA512; else if(oid_is(o,ol,OID_ECDSA256,sizeof OID_ECDSA256))x->sigalg=XS_EC256;
      else if(oid_is(o,ol,OID_ECDSA384,sizeof OID_ECDSA384))x->sigalg=XS_EC384; else if(oid_is(o,ol,OID_ECDSA512,sizeof OID_ECDSA512))x->sigalg=XS_EC512; }
    const u8* sv; int sl; if(der_expect(&C,0x03,&sv,&sl)||sl<1||sv[0])return -1; x->sig=sv+1; x->siglen=sl-1;
    der_cur T={tv,tv+tl}; int tag; const u8* v; int l;
    if(der_get(&T,&tag,&v,&l))return -1;
    if(tag==0xA0){ if(der_get(&T,&tag,&v,&l))return -1; }
    if(tag!=0x02)return -1;
    if(der_expect(&T,0x30,&v,&l))return -1;
    const u8* p0=T.p; if(der_expect(&T,0x30,&v,&l))return -1; x->issuer=p0; x->issuerlen=(int)(T.p-p0);
    if(der_expect(&T,0x30,&v,&l))return -1;
    { der_cur V={v,v+l}; const u8* tv2; int tl2; int tg;
      if(der_get(&V,&tg,&tv2,&tl2)||x509_time(tg,tv2,tl2,&x->nb))return -1;
      if(der_get(&V,&tg,&tv2,&tl2)||x509_time(tg,tv2,tl2,&x->na))return -1; }
    p0=T.p; if(der_expect(&T,0x30,&v,&l))return -1; x->subject=p0; x->subjectlen=(int)(T.p-p0);
    x509_name_cn(v,l,&x->cn,&x->cnlen);
    p0=T.p; if(der_expect(&T,0x30,&v,&l))return -1; x->spki=p0; x->spkilen=(int)(T.p-p0);
    while(der_get(&T,&tag,&v,&l)==0){
        if(tag!=0xA3)continue;
        der_cur E0={v,v+l}; const u8* ev; int el; if(der_expect(&E0,0x30,&ev,&el))return -1;
        der_cur E={ev,ev+el}; const u8* xv; int xl;
        while(der_expect(&E,0x30,&xv,&xl)==0){
            der_cur X={xv,xv+xl}; const u8* o; int ol; if(der_expect(&X,0x06,&o,&ol))return -1;
            const u8* ov; int ovl; int t2; if(der_get(&X,&t2,&ov,&ovl))return -1;
            if(t2==0x01){ if(der_get(&X,&t2,&ov,&ovl))return -1; }
            if(t2!=0x04)return -1;
            if(oid_is(o,ol,OID_SAN,sizeof OID_SAN)){ der_cur S={ov,ov+ovl}; const u8* gv; int gl; if(der_expect(&S,0x30,&gv,&gl))return -1; x->san=gv; x->sanlen=gl; x->has_san=1; }
            else if(oid_is(o,ol,OID_BC,sizeof OID_BC)){ der_cur S={ov,ov+ovl}; const u8* bv; int bl; if(der_expect(&S,0x30,&bv,&bl))return -1;
                der_cur B={bv,bv+bl}; const u8* cv2; int cl2; int t3; if(der_get(&B,&t3,&cv2,&cl2)==0&&t3==0x01&&cl2==1&&cv2[0])x->is_ca=1; } } }
    return 0;
}
static int x509_sig_parts(const u8* sig,int len,const u8** r,int* rl,const u8** s,int* sl){
    der_cur c={sig,sig+len}; const u8* v; int l; if(der_expect(&c,0x30,&v,&l))return -1;
    der_cur q={v,v+l}; if(der_expect(&q,0x02,r,rl)||der_expect(&q,0x02,s,sl))return -1; return 0;
}
static int x509_verify_with(int alg,const u8* tbs,int tbslen,const u8* sig,int siglen,const u8* spki,int spkilen){
    x509_key k; if(x509_parse_spki(spki,spkilen,&k))return -1;
    int h=(alg==XS_RSA256||alg==XS_EC256)?XH_SHA256:(alg==XS_RSA384||alg==XS_EC384)?XH_SHA384:(alg==XS_RSA512||alg==XS_EC512)?XH_SHA512:0;
    if(!h)return -1; u8 hash[64]; int hl=xh_hash(h,tbs,tbslen,hash);
    if(alg<=XS_RSA512){ if(k.type!=XK_RSA)return -1; return rsa_pkcs1_verify(k.n,k.nlen,k.e,k.elen,h,hash,sig,siglen); }
    if(k.type==XK_RSA)return -1;
    const u8 *r,*s; int rl,sl; if(x509_sig_parts(sig,siglen,&r,&rl,&s,&sl))return -1;
    return ecdsa_verify(k.type==XK_P256?&g_p256:&g_p384,k.pt,k.ptlen,hash,hl,r,rl,s,sl);
}
static int x509_lc(int c){ return (c>='A'&&c<='Z')?c+32:c; }
static int x509_host_eq(const u8* pat,int pl,const char* host){
    int hl=0; while(host[hl])hl++;
    if(pl>2&&pat[0]=='*'&&pat[1]=='.'){
        int dot=0; while(dot<hl&&host[dot]!='.')dot++;
        if(dot==0||dot>=hl)return 0;
        if(hl-dot!=pl-1)return 0;
        for(int i=0;i<pl-1;i++) if(x509_lc(pat[1+i])!=x509_lc(host[dot+i]))return 0;
        return 1; }
    if(pl!=hl)return 0;
    for(int i=0;i<pl;i++) if(x509_lc(pat[i])!=x509_lc(host[i]))return 0;
    return 1;
}
static int x509_parse_ip(const char* h,u8 ip[4]){
    int k=0,v=-1; for(int i=0;;i++){ char c=h[i];
        if(c>='0'&&c<='9'){ v=(v<0?0:v)*10+(c-'0'); if(v>255)return 0; }
        else if(c=='.'||!c){ if(v<0||k>=4)return 0; ip[k++]=(u8)v; v=-1; if(!c)break; }
        else return 0; }
    return k==4;
}
static int x509_match_host(const x509_cert* x,const char* host){
    u8 ip[4]; int isip=x509_parse_ip(host,ip);
    if(x->has_san){
        der_cur c={x->san,x->san+x->sanlen}; int t; const u8* v; int l;
        while(der_get(&c,&t,&v,&l)==0){
            if(t==0x82&&!isip&&x509_host_eq(v,l,host))return 1;
            if(t==0x87&&isip&&l==4&&v[0]==ip[0]&&v[1]==ip[1]&&v[2]==ip[2]&&v[3]==ip[3])return 1; }
        return 0; }
    if(x->cn&&!isip)return x509_host_eq(x->cn,x->cnlen,host);
    return 0;
}
static int x509_bytes_eq(const u8* a,int al,const u8* b,int bl){ if(al!=bl)return 0; for(int i=0;i<al;i++) if(a[i]!=b[i])return 0; return 1; }

static x509_anchor g_x509_extra[X509_MAX_EXTRA]; int g_x509_nextra;
static const char* g_x509_why="";

static int x509_find_anchor(const x509_cert* cur,int self){
    x509_anchor a;
    for(int i=0;;i++){
        if(i<g_x509_nextra)a=g_x509_extra[i]; else if(!nvx_root_get(i-g_x509_nextra,&a))break;
        if(self){ if(x509_bytes_eq(cur->subject,cur->subjectlen,a.subj,a.subjlen)&&x509_bytes_eq(cur->spki,cur->spkilen,a.spki,a.spkilen))return 1; continue; }
        if(!x509_bytes_eq(cur->issuer,cur->issuerlen,a.subj,a.subjlen))continue;
        if(x509_verify_with(cur->sigalg,cur->tbs,cur->tbslen,cur->sig,cur->siglen,a.spki,a.spkilen)==0)return 1; }
    return 0;
}
#define X509_ERR_PARSE  (-10)
#define X509_ERR_CHAIN  (-11)
#define X509_ERR_HOST   (-12)
#define X509_ERR_TIME   (-13)
int x509_verify_chain(const u8* const* certs,const int* lens,int n,const char* host,i64 now){
    if(n<1){ g_x509_why="server sent no certificate"; return X509_ERR_PARSE; }
    if(n>10)n=10;
    static x509_cert cs[10]; int used[10];
    for(int i=0;i<n;i++){ used[i]=0; if(x509_parse(certs[i],lens[i],&cs[i])){ if(i==0){ g_x509_why="could not parse the server certificate"; return X509_ERR_PARSE; } used[i]=1; } }
    if(now>0&&(now<cs[0].nb||now>cs[0].na)){ g_x509_why=now<cs[0].nb?"certificate is not valid yet (check the clock)":"certificate has expired"; return X509_ERR_TIME; }
    if(host&&!x509_match_host(&cs[0],host)){ g_x509_why="certificate does not match the host name"; return X509_ERR_HOST; }
    int cur=0; used[0]=1;
    for(int depth=0;depth<10;depth++){
        if(x509_find_anchor(&cs[cur],1)||x509_find_anchor(&cs[cur],0)){ g_x509_why="ok"; return 0; }
        int next=-1;
        for(int j=1;j<n;j++){ if(used[j])continue;
            if(!x509_bytes_eq(cs[cur].issuer,cs[cur].issuerlen,cs[j].subject,cs[j].subjectlen))continue;
            if(!cs[j].is_ca)continue;
            if(x509_verify_with(cs[cur].sigalg,cs[cur].tbs,cs[cur].tbslen,cs[cur].sig,cs[cur].siglen,cs[j].spki,cs[j].spkilen))continue;
            next=j; break; }
        if(next<0){ g_x509_why="certificate chain does not lead to a trusted root"; return X509_ERR_CHAIN; }
        if(now>0&&(now<cs[next].nb||now>cs[next].na)){ g_x509_why="an intermediate certificate has expired"; return X509_ERR_TIME; }
        used[next]=1; cur=next; }
    g_x509_why="certificate chain too long"; return X509_ERR_CHAIN;
}
static int tls13_verify_cv(const u8* spki,int spkilen,int scheme,const u8* sig,int siglen,const u8* thash,int thlen){
    u8 content[64+34+64]; int o=0; for(int i=0;i<64;i++)content[o++]=0x20;
    const char* ctx="TLS 1.3, server CertificateVerify"; for(int i=0;ctx[i];i++)content[o++]=(u8)ctx[i]; content[o++]=0;
    for(int i=0;i<thlen;i++)content[o++]=thash[i];
    x509_key k; if(x509_parse_spki(spki,spkilen,&k))return -1;
    u8 h[64];
    if(scheme==0x0403||scheme==0x0503){
        int want=scheme==0x0403?XK_P256:XK_P384; if(k.type!=want)return -1;
        int hl=xh_hash(scheme==0x0403?XH_SHA256:XH_SHA384,content,o,h);
        const u8 *r,*s; int rl,sl; if(x509_sig_parts(sig,siglen,&r,&rl,&s,&sl))return -1;
        return ecdsa_verify(want==XK_P256?&g_p256:&g_p384,k.pt,k.ptlen,h,hl,r,rl,s,sl); }
    if(scheme==0x0804||scheme==0x0805||scheme==0x0806){
        if(k.type!=XK_RSA)return -1; int hid=scheme==0x0804?XH_SHA256:scheme==0x0805?XH_SHA384:XH_SHA512;
        xh_hash(hid,content,o,h); return rsa_pss_verify(k.n,k.nlen,k.e,k.elen,hid,h,sig,siglen); }
    return -1;
}
int x509_pem_next(const char** pp,u8* out,int cap){
    const char* p=*pp; const char* b="-----BEGIN CERTIFICATE-----";
    for(;;){ if(!*p)return 0; int m=1; for(int i=0;b[i];i++) if(p[i]!=b[i]){ m=0; break; } if(m)break; p++; }
    for(int i=0;b[i];i++)p++;
    int n=0; u32 acc=0; int bits=0;
    for(;*p&&*p!='-';p++){ int c=*p,v;
        if(c>='A'&&c<='Z')v=c-'A'; else if(c>='a'&&c<='z')v=c-'a'+26; else if(c>='0'&&c<='9')v=c-'0'+52; else if(c=='+')v=62; else if(c=='/')v=63; else continue;
        acc=(acc<<6)|(u32)v; bits+=6; if(bits>=8){ bits-=8; if(n<cap)out[n++]=(u8)(acc>>bits); } }
    while(*p=='-')p++; while(*p&&*p!='-'&&*p!='\n')p++;
    *pp=p; return n;
}
int x509_add_anchor_der(const u8* d,int len){
    if(g_x509_nextra>=X509_MAX_EXTRA)return -1;
    x509_cert x; if(x509_parse(d,len,&x))return -1;
    g_x509_extra[g_x509_nextra].subj=x.subject; g_x509_extra[g_x509_nextra].subjlen=x.subjectlen;
    g_x509_extra[g_x509_nextra].spki=x.spki; g_x509_extra[g_x509_nextra].spkilen=x.spkilen; g_x509_nextra++;
    return 0;
}
int x509_leaf_cv(const u8* leaf,int len,int scheme,const u8* sig,int siglen,const u8* thash){
    x509_cert x; if(x509_parse(leaf,len,&x))return -1;
    return tls13_verify_cv(x.spki,x.spkilen,scheme,sig,siglen,thash,32);
}
const char* x509_reason(void){ return g_x509_why; }
