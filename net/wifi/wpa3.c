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

int sae_derive_pwe(const u8* a1,const u8* a2,const char* pw,u8 pwe[65]){
    u8 key[12]; const u8 *hi=a1,*lo=a2; if(be_lt(a1,a2,6)){ hi=a2; lo=a1; } for(int i=0;i<6;i++){ key[i]=hi[i]; key[6+i]=lo[i]; }
    int pl=0; while(pw[pl]&&pl<128)pl++; u8 base[140]; for(int i=0;i<pl;i++)base[i]=(u8)pw[i];
    u8 prime[32]; p256_prime(prime); int found=0;
    for(int ctr=1;ctr<=40;ctr++){ base[pl]=(u8)ctr; u8 seed[32]; hmac_sha256(key,12,base,pl+1,seed);
        u8 val[32]; kdf_sha256(seed,32,"SAE Hunting and Pecking",prime,32,val,256);
        if(!be_lt(val,prime,32))continue; u8 y[32]; if(!p256_y_from_x(val,y))continue;
        if(!found){ int want=seed[31]&1; if((y[31]&1)!=want){ u8 pt[65]; pt[0]=4; for(int i=0;i<32;i++){ pt[1+i]=val[i]; pt[33+i]=y[i]; } p256_neg(pt); for(int i=0;i<32;i++)y[i]=pt[33+i]; }
            pwe[0]=4; for(int i=0;i<32;i++){ pwe[1+i]=val[i]; pwe[33+i]=y[i]; } found=1; } }
    return found?0:-1; }

static void rand_scalar(u8 out[32]){ u8 n[32]; p256_order(n); for(;;){ csprng_bytes(out,32); p256_modn(out); int small=1; for(int i=0;i<31;i++) if(out[i])small=0; if(small&&out[31]<2)continue; if(be_lt(out,n,32))return; } }

int sae_commit(sae_ctx* s,const u8* own,const u8* peer,const char* pw){
    if(sae_derive_pwe(own,peer,pw,s->pwe)<0)return -1;
    for(int tries=0;tries<8;tries++){ rand_scalar(s->rnd); u8 mask[32]; rand_scalar(mask);
        p256_modn_add(s->rnd,mask,s->scalar); if(be_zero(s->scalar,32))continue; int small=1; for(int i=0;i<31;i++) if(s->scalar[i])small=0; if(small&&s->scalar[31]<2)continue;
        if(p256_mul(mask,s->pwe,s->elem)<0)continue; p256_neg(s->elem); for(int i=0;i<32;i++)mask[i]=0; return 0; }
    return -1; }

int sae_process_commit(sae_ctx* s,const u8* ps,const u8* pe){
    for(int i=0;i<32;i++)s->pscalar[i]=ps[i]; s->pelem[0]=4; for(int i=0;i<64;i++)s->pelem[1+i]=pe[i];
    u8 n[32]; p256_order(n); if(be_zero(ps,32)||!be_lt(ps,n,32))return -1;
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
