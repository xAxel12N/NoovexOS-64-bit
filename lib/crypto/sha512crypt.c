#include "include/nvx/base.h"
#include "include/nvx/lib/crypto/nvxcrypto2.h"
#include "include/nvx/lib/crypto/sha512crypt.h"

static const char SC_B64[]="./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
static u8 sc_buf[4096];

static int sc_len(const char* s){ int n=0; while(s[n])n++; return n; }

static char* sc_b64(char* o,u32 b2,u32 b1,u32 b0,int n){
    u32 w=(b2<<16)|(b1<<8)|b0;
    while(n-->0){ *o++=SC_B64[w&63]; w>>=6; }
    return o;
}

static int sc_parse(const char* setting,const char** salt,int* sl,u32* rounds,int* custom){
    *rounds=5000; *custom=0;
    if(setting[0]!='$'||setting[1]!='6'||setting[2]!='$')return -1;
    const char* p=setting+3;
    const char* rp="rounds="; int i=0; while(rp[i]&&p[i]==rp[i])i++;
    if(!rp[i]){ u32 v=0; int d=0; p+=7; while(*p>='0'&&*p<='9'){ if(v<100000000u)v=v*10+(u32)(*p-'0'); else v=999999999u; p++; d++; }
        if(!d||*p!='$')return -1;
        p++;
        if(v<1000)v=1000;
        if(v>999999999u)v=999999999u;
        *rounds=v; *custom=1; }
    int n=0; while(p[n]&&p[n]!='$'&&n<16)n++;
    *salt=p; *sl=n; return 0;
}

int sha512_crypt(const char* key,const char* setting,char* out,int cap){
    const char* S; int sl; u32 R; int custom;
    if(cap<128||sc_parse(setting,&S,&sl,&R,&custom)<0)return -1;
    int kl=sc_len(key); if(kl>256)return -1;
    u8 A[64],B[64],DP[64],DS[64],C[64],P[256],Sx[16];
    int n=0;
    for(int i=0;i<kl;i++)sc_buf[n++]=(u8)key[i];
    for(int i=0;i<sl;i++)sc_buf[n++]=(u8)S[i];
    for(int i=0;i<kl;i++)sc_buf[n++]=(u8)key[i];
    sha512_hash(sc_buf,n,B);
    n=0;
    for(int i=0;i<kl;i++)sc_buf[n++]=(u8)key[i];
    for(int i=0;i<sl;i++)sc_buf[n++]=(u8)S[i];
    { int r=kl; while(r>64){ for(int i=0;i<64;i++)sc_buf[n++]=B[i]; r-=64; } for(int i=0;i<r;i++)sc_buf[n++]=B[i]; }
    for(int b=kl;b>0;b>>=1){ if(b&1){ for(int i=0;i<64;i++)sc_buf[n++]=B[i]; } else { for(int i=0;i<kl;i++)sc_buf[n++]=(u8)key[i]; } }
    sha512_hash(sc_buf,n,A);
    { sha512_ctx x; sha512_init(&x); for(int j=0;j<kl;j++)sha512_update(&x,(const u8*)key,(u64)kl); sha512_final(&x,DP,64); }
    for(int i=0;i<kl;i++)P[i]=DP[i%64];
    { sha512_ctx x; sha512_init(&x); for(int j=0;j<16+(int)A[0];j++)sha512_update(&x,(const u8*)S,(u64)sl); sha512_final(&x,DS,64); }
    for(int i=0;i<sl;i++)Sx[i]=DS[i];
    for(int i=0;i<64;i++)C[i]=A[i];
    for(u32 r=0;r<R;r++){
        n=0;
        if(r&1){ for(int i=0;i<kl;i++)sc_buf[n++]=P[i]; } else { for(int i=0;i<64;i++)sc_buf[n++]=C[i]; }
        if(r%3){ for(int i=0;i<sl;i++)sc_buf[n++]=Sx[i]; }
        if(r%7){ for(int i=0;i<kl;i++)sc_buf[n++]=P[i]; }
        if(r&1){ for(int i=0;i<64;i++)sc_buf[n++]=C[i]; } else { for(int i=0;i<kl;i++)sc_buf[n++]=P[i]; }
        sha512_hash(sc_buf,n,C);
    }
    char* o=out; *o++='$'; *o++='6'; *o++='$';
    if(custom){ const char* rp="rounds="; while(*rp)*o++=*rp++; char t[12]; int tn=0; u32 v=R; do{ t[tn++]=(char)('0'+v%10); v/=10; }while(v); while(tn)*o++=t[--tn]; *o++='$'; }
    for(int i=0;i<sl;i++)*o++=S[i];
    *o++='$';
    static const u8 T[21][3]={{0,21,42},{22,43,1},{44,2,23},{3,24,45},{25,46,4},{47,5,26},{6,27,48},{28,49,7},{50,8,29},{9,30,51},{31,52,10},{53,11,32},{12,33,54},{34,55,13},{56,14,35},{15,36,57},{37,58,16},{59,17,38},{18,39,60},{40,61,19},{62,20,41}};
    for(int i=0;i<21;i++)o=sc_b64(o,C[T[i][0]],C[T[i][1]],C[T[i][2]],4);
    o=sc_b64(o,0,0,C[63],2);
    *o=0;
    for(int i=0;i<n&&i<(int)sizeof(sc_buf);i++)sc_buf[i]=0;
    for(int i=0;i<64;i++){ A[i]=B[i]=DP[i]=DS[i]=C[i]=0; }
    for(int i=0;i<kl;i++)P[i]=0;
    return (int)(o-out);
}

int sha512_crypt_check(const char* key,const char* hash){
    char got[160];
    if(sha512_crypt(key,hash,got,(int)sizeof(got))<0)return 0;
    int hl=sc_len(hash), gl=sc_len(got); if(hl!=gl)return 0;
    u8 d=0; for(int i=0;i<gl;i++)d|=(u8)(got[i]^hash[i]);
    for(int i=0;i<gl;i++)got[i]=0;
    return d==0;
}

void sha512_crypt_salt(char* out,int n){
    u8 r[16]; if(n>16)n=16; csprng_bytes(r,n);
    for(int i=0;i<n;i++)out[i]=SC_B64[r[i]&63];
    out[n]=0;
}
