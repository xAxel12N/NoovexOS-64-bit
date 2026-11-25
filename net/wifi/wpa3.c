#include "include/nvx/base.h"
#include "include/nvx/lib/crypto/nvxwpa.h"
#include "include/nvx/lib/crypto/nvxcrypto.h"
#include "include/nvx/lib/crypto/nvxcrypto2.h"
#include "include/nvx/lib/crypto/nvxtls.h"
#include "include/nvx/lib/crypto/nvxx509.h"
#include "include/nvx/net/wifi/wpa3.h"

int g_wpa_akm=2;
extern u8 g_ptk[48];

void aes_cmac(const u8 key[16],const u8* m,int len,u8 out[16]){
    u8 rk[176]; aes128_expand(key,rk); u8 L[16]={0},K1[16],K2[16]; aes128_enc(rk,L,L);
    for(int i=0;i<16;i++)K1[i]=(u8)((L[i]<<1)|(i<15?L[i+1]>>7:0)); if(L[0]&0x80)K1[15]^=0x87;
    for(int i=0;i<16;i++)K2[i]=(u8)((K1[i]<<1)|(i<15?K1[i+1]>>7:0)); if(K1[0]&0x80)K2[15]^=0x87;
    int n=(len+15)/16; int full=len>0&&(len%16)==0; if(n==0)n=1;
    u8 X[16]={0},B[16];
    for(int b=0;b<n-1;b++){ for(int i=0;i<16;i++)X[i]^=m[b*16+i]; aes128_enc(rk,X,X); }
    int off=(n-1)*16, rem=len-off;
    for(int i=0;i<16;i++){ if(full)B[i]=m[off+i]^K1[i]; else B[i]=(u8)((i<rem?m[off+i]:(i==rem?0x80:0))^K2[i]); }
    for(int i=0;i<16;i++)X[i]^=B[i]; aes128_enc(rk,X,out); }

void wpa_mic(const u8* frame,int len,u8 out[32]){
    if(g_wpa_akm==8||g_wpa_akm==6){ aes_cmac(g_ptk,frame,len,out); return; }
    hmac_sha1(g_ptk,16,frame,(u32)len,out); }
int wpa_key_ver(void){ return (g_wpa_akm==8)?0:(g_wpa_akm==6)?3:2; }

void kdf_sha256(const u8* key,int klen,const char* label,const u8* ctx,int clen,u8* out,int bits){
    int L=0; while(label[L])L++; int need=(bits+7)/8; u8 buf[512]; if(4+L+clen>(int)sizeof(buf))return;
    for(int i=1,o=0;o<need;i++){ int p=0; buf[p++]=(u8)i; buf[p++]=(u8)(i>>8); for(int k=0;k<L;k++)buf[p++]=(u8)label[k]; for(int k=0;k<clen;k++)buf[p++]=ctx[k]; buf[p++]=(u8)bits; buf[p++]=(u8)(bits>>8);
        u8 h[32]; hmac_sha256(key,klen,buf,p,h); for(int k=0;k<32&&o<need;k++)out[o++]=h[k]; } }

void wpa_derive_ptk_akm(const u8* pmk,const u8* aa,const u8* spa,const u8* an,const u8* sn,u8* ptk){
    u8 d[76]; int p=0, c=0; const u8 *a1=aa,*a2=spa; for(int i=0;i<6;i++){ if(aa[i]!=spa[i]){ c=aa[i]<spa[i]?-1:1; break; } } if(c>0){ a1=spa; a2=aa; }
    for(int i=0;i<6;i++)d[p++]=a1[i]; for(int i=0;i<6;i++)d[p++]=a2[i];
    const u8 *n1=an,*n2=sn; c=0; for(int i=0;i<32;i++){ if(an[i]!=sn[i]){ c=an[i]<sn[i]?-1:1; break; } } if(c>0){ n1=sn; n2=an; }
    for(int i=0;i<32;i++)d[p++]=n1[i]; for(int i=0;i<32;i++)d[p++]=n2[i];
    if(g_wpa_akm==8||g_wpa_akm==6)kdf_sha256(pmk,32,"Pairwise key expansion",d,76,ptk,384);
    else wpa_prf(pmk,32,"Pairwise key expansion",d,76,ptk,48); }

static int be_lt(const u8* a,const u8* b,int n){ for(int i=0;i<n;i++){ if(a[i]!=b[i])return a[i]<b[i]; } return 0; }
static int be_zero(const u8* a,int n){ for(int i=0;i<n;i++) if(a[i])return 0; return 1; }

static u8 ct_sel(u8 m,u8 a,u8 b){ return (u8)((a&m)|(b&(u8)~m)); }
int sae_derive_pwe(const u8* a1,const u8* a2,const char* pw,u8 pwe[65]){
    u8 key[12]; const u8 *hi=a1,*lo=a2; if(be_lt(a1,a2,6)){ hi=a2; lo=a1; } for(int i=0;i<6;i++){ key[i]=hi[i]; key[6+i]=lo[i]; }
    int pl=0; while(pw[pl]&&pl<128)pl++; u8 base[140]; for(int i=0;i<pl;i++)base[i]=(u8)pw[i];
    u8 prime[32], zero[32]={0}, seed[32], val[32], y[32], ny[32]; p256_prime(prime); u32 found=0; pwe[0]=4; for(int i=1;i<65;i++)pwe[i]=0;
    for(int ctr=1;ctr<=40;ctr++){ base[pl]=(u8)ctr; hmac_sha256(key,12,base,pl+1,seed);
        kdf_sha256(seed,32,"SAE Hunting and Pecking",prime,32,val,256);
        for(int i=0;i<32;i++)y[i]=0; u32 ok=(u32)be_lt(val,prime,32); ok&=(u32)(p256_y_from_x(val,y)!=0);
        p256_fp(1,zero,y,ny); u8 pm=(u8)(0u-(u32)((y[31]&1)==(seed[31]&1))), m=(u8)(0u-(ok&(found^1u)));
        for(int i=0;i<32;i++){ u8 yy=ct_sel(pm,y[i],ny[i]); pwe[1+i]=ct_sel(m,val[i],pwe[1+i]); pwe[33+i]=ct_sel(m,yy,pwe[33+i]); }
        found|=ok; }
    for(int i=0;i<32;i++){ seed[i]=0; val[i]=0; y[i]=0; ny[i]=0; } for(int i=0;i<(int)sizeof base;i++)base[i]=0;
    return found?0:-1; }

static void rand_scalar(u8 out[32]){ u8 n[32]; p256_order(n); for(;;){ csprng_bytes(out,32); p256_modn(out); int small=1; for(int i=0;i<31;i++) if(out[i])small=0; if(small&&out[31]<2)continue; if(be_lt(out,n,32))return; } }

static void hkdf_exp(const u8 prk[32],const char* info,u8* out,int n){
    int il=0; while(info[il])il++; u8 b[32+64+1], t[32]; int tl=0;
    for(int i=1,o=0;o<n;i++){ int p=0; for(int k=0;k<tl;k++)b[p++]=t[k]; for(int k=0;k<il;k++)b[p++]=(u8)info[k]; b[p++]=(u8)i; hmac_sha256(prk,32,b,p,t); tl=32; for(int k=0;k<32&&o<n;k++)out[o++]=t[k]; } }
static void fp_red48(const u8 v[48],u8 out[32]){
    static const u8 r256[32]={0,0,0,0,0xff,0xff,0xff,0xfe,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0,0,0,0,0,0,0,0,0,0,0,1};
    u8 hi[32]={0}, lo[32]; for(int i=0;i<16;i++)hi[16+i]=v[i]; for(int i=0;i<32;i++)lo[i]=v[16+i];
    p256_fp(2,hi,r256,hi); p256_fp(0,hi,lo,out); }
static void fp_const(int k,u8 out[32]){ u8 z[32]={0}, t[32]={0}; t[31]=(u8)k; p256_fp(1,z,t,out); }
static void fp_exp(int kind,u8 e[32]){ p256_prime(e); if(kind==0){ int i=31; while(e[i]<2){ e[i]=(u8)(e[i]+254); i--; } e[i]-=2; return; }
    if(kind==1){ e[31]&=0xFE; for(int i=31;i>0;i--)e[i]=(u8)((e[i]>>1)|(e[i-1]<<7)); e[0]>>=1; return; }
    int c=1; for(int i=31;i>=0&&c;i--){ int v=e[i]+c; e[i]=(u8)v; c=v>>8; } for(int i=31;i>0;i--)e[i]=(u8)((e[i]>>2)|(e[i-1]<<6)); e[0]>>=2; }
static void fp_gx(const u8 x[32],const u8 a[32],const u8 b[32],u8 out[32]){ u8 t[32],u[32]; p256_fp(2,x,x,t); p256_fp(2,t,x,t); p256_fp(2,a,x,u); p256_fp(0,t,u,t); p256_fp(0,t,b,out); }
int sae_sswu(const u8 u[32],u8 out[65]){
    u8 A[32],B[32],Z[32],pm2[32],ph[32],pq[32],one[32]={0},zero[32]={0},u2[32],t1[32],t2[32],m[32],t[32],x1a[32],x1b[32],x1[32],gx1[32],x2[32],gx2[32],q[32],v[32],x[32],y[32],ny[32];
    one[31]=1; fp_const(3,A);
    { static const u8 bb[32]={0x5a,0xc6,0x35,0xd8,0xaa,0x3a,0x93,0xe7,0xb3,0xeb,0xbd,0x55,0x76,0x98,0x86,0xbc,0x65,0x1d,0x06,0xb0,0xcc,0x53,0xb0,0xf6,0x3b,0xce,0x3c,0x3e,0x27,0xd2,0x60,0x4b}; for(int i=0;i<32;i++)B[i]=bb[i]; }
    fp_const(10,Z); fp_exp(0,pm2); fp_exp(1,ph); fp_exp(2,pq);
    p256_fp(2,u,u,u2); p256_fp(2,Z,u2,t1); p256_fp(2,t1,t1,t2); p256_fp(0,t1,t2,m); p256_fp(3,m,pm2,t);
    u8 mz=0; for(int i=0;i<32;i++)mz|=m[i]; u8 mzm=(u8)(0u-(u32)(mz==0));
    p256_fp(2,Z,A,x1a); p256_fp(3,x1a,pm2,x1a); p256_fp(2,B,x1a,x1a);
    p256_fp(1,zero,B,x1b); p256_fp(3,A,pm2,t2); p256_fp(2,x1b,t2,x1b); p256_fp(0,one,t,t2); p256_fp(2,x1b,t2,x1b);
    for(int i=0;i<32;i++)x1[i]=ct_sel(mzm,x1a[i],x1b[i]);
    fp_gx(x1,A,B,gx1); p256_fp(2,t1,x1,x2); fp_gx(x2,A,B,gx2);
    p256_fp(3,gx1,ph,q); u8 qa=0; for(int i=0;i<31;i++)qa|=q[i]; u8 qr=(u8)(0u-(u32)(qa==0&&q[31]<=1));
    for(int i=0;i<32;i++){ v[i]=ct_sel(qr,gx1[i],gx2[i]); x[i]=ct_sel(qr,x1[i],x2[i]); }
    p256_fp(3,v,pq,y); p256_fp(2,y,y,t2); u8 d=0; for(int i=0;i<32;i++)d|=t2[i]^v[i]; if(d)return -1;
    p256_fp(1,zero,y,ny); u8 eq=(u8)(0u-(u32)((u[31]&1)==(y[31]&1)));
    out[0]=4; for(int i=0;i<32;i++){ out[1+i]=x[i]; out[33+i]=ct_sel(eq,y[i],ny[i]); } return 0; }
int sae_derive_pt(const u8* ssid,int sl,const u8* pw,int pl,const u8* id,int il,u8 pt[65]){
    u8 ikm[256]; if(pl<0||il<0||pl+il>(int)sizeof(ikm)||sl<0||sl>32)return -1; for(int i=0;i<pl;i++)ikm[i]=pw[i]; for(int i=0;i<il;i++)ikm[pl+i]=id[i];
    u8 salt[32]={0}; for(int i=0;i<sl;i++)salt[i]=ssid[i]; u8 seed[32]; hmac_sha256(salt,sl?sl:32,ikm,pl+il,seed);
    u8 pv[48], u1[32], p1[65], p2[65]; int r=-1;
    hkdf_exp(seed,"SAE Hash to Element u1 P1",pv,48); fp_red48(pv,u1); if(sae_sswu(u1,p1)<0)goto out;
    hkdf_exp(seed,"SAE Hash to Element u2 P2",pv,48); fp_red48(pv,u1); if(sae_sswu(u1,p2)<0)goto out;
    r=p256_add(p1,p2,pt)<0?-1:0;
out: for(int i=0;i<(int)sizeof(ikm);i++)ikm[i]=0; for(int i=0;i<32;i++){ seed[i]=0; u1[i]=0; } for(int i=0;i<48;i++)pv[i]=0; return r; }
int sae_pwe_from_pt(const u8 pt[65],const u8* a1,const u8* a2,u8 pwe[65]){
    u8 ad[12]; const u8 *hi=a1,*lo=a2; if(be_lt(a1,a2,6)){ hi=a2; lo=a1; } for(int i=0;i<6;i++){ ad[i]=hi[i]; ad[6+i]=lo[i]; }
    u8 zero[32]={0}, val[32], nm1[32]; hmac_sha256(zero,32,ad,12,val); p256_order(nm1); nm1[31]--;
    if(!be_lt(val,nm1,32)){ int br=0; for(int i=31;i>=0;i--){ int d=val[i]-nm1[i]-br; br=d<0; val[i]=(u8)d; } }
    for(int i=31;i>=0;i--){ if(++val[i])break; }
    return p256_mul(val,pt,pwe)<0?-1:0; }
static int sae_commit_pwe(sae_ctx* s){
    for(int tries=0;tries<8;tries++){ rand_scalar(s->rnd); u8 mask[32]; rand_scalar(mask);
        p256_modn_add(s->rnd,mask,s->scalar); if(be_zero(s->scalar,32))continue; int small=1; for(int i=0;i<31;i++) if(s->scalar[i])small=0; if(small&&s->scalar[31]<2)continue;
        if(p256_mul(mask,s->pwe,s->elem)<0)continue; p256_neg(s->elem); for(int i=0;i<32;i++)mask[i]=0; return 0; }
    return -1; }
int sae_commit(sae_ctx* s,const u8* own,const u8* peer,const char* pw){ if(sae_derive_pwe(own,peer,pw,s->pwe)<0)return -1; return sae_commit_pwe(s); }
int sae_commit_pt(sae_ctx* s,const u8* own,const u8* peer,const u8 pt[65]){ if(sae_pwe_from_pt(pt,own,peer,s->pwe)<0)return -1; return sae_commit_pwe(s); }

int sae_process_commit(sae_ctx* s,const u8* ps,const u8* pe){
    for(int i=0;i<32;i++)s->pscalar[i]=ps[i]; s->pelem[0]=4; for(int i=0;i<64;i++)s->pelem[1+i]=pe[i];
    u8 n[32]; p256_order(n); int le1=1; for(int i=0;i<31;i++) if(ps[i])le1=0; if((le1&&ps[31]<2)||!be_lt(ps,n,32))return -1;
    int same=1; for(int i=0;i<32;i++) if(ps[i]!=s->scalar[i])same=0; for(int i=0;i<65&&same;i++) if(s->pelem[i]!=s->elem[i])same=0; if(same)return -2;
    u8 t[65],t2[65],K[65]; if(p256_mul(ps,s->pwe,t)<0)return -1; if(p256_add(t,s->pelem,t2)<0)return -1; if(p256_mul(s->rnd,t2,K)<0)return -1;
    u8 zero[32]={0}; u8 keyseed[32]; hmac_sha256(zero,32,K+1,32,keyseed);
    u8 sum[32]; p256_modn_add(s->scalar,s->pscalar,sum); u8 kp[64]; kdf_sha256(keyseed,32,"SAE KCK and PMK",sum,32,kp,512);
    for(int i=0;i<32;i++){ s->kck[i]=kp[i]; s->pmk[i]=kp[32+i]; } for(int i=0;i<16;i++)s->pmkid[i]=sum[i];
    for(int i=0;i<32;i++){ s->rnd[i]=0; keyseed[i]=0; } for(int i=0;i<65;i++)K[i]=0; return 0; }

static void sae_cfm(const sae_ctx* s,u16 sc,const u8* s1,const u8* e1,const u8* s2,const u8* e2,u8 out[32]){
    u8 b[2+32+64+32+64]; int p=0; b[p++]=(u8)sc; b[p++]=(u8)(sc>>8);
    for(int i=0;i<32;i++)b[p++]=s1[i]; for(int i=0;i<64;i++)b[p++]=e1[1+i]; for(int i=0;i<32;i++)b[p++]=s2[i]; for(int i=0;i<64;i++)b[p++]=e2[1+i];
    hmac_sha256(s->kck,32,b,p,out); }
void sae_confirm(const sae_ctx* s,u16 sc,u8 out[32]){ sae_cfm(s,sc,s->scalar,s->elem,s->pscalar,s->pelem,out); }
int sae_check_confirm(const sae_ctx* s,u16 psc,const u8* c){ u8 x[32]; sae_cfm(s,psc,s->pscalar,s->pelem,s->scalar,s->elem,x); u8 d=0; for(int i=0;i<32;i++)d|=x[i]^c[i]; return d?-1:0; }
