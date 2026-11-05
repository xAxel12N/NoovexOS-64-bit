#include "include/nvx/base.h"
#include "include/nvx/lib/crypto/nvxwpa.h"
#include "include/nvx/lib/crypto/nvxtls.h"
#include "include/nvx/lib/crypto/nvxcrypto2.h"
#include "include/nvx/net/wifi/eap.h"

static u32 md4_f(u32 x,u32 y,u32 z){ return (x&y)|(~x&z); }
static u32 md4_g(u32 x,u32 y,u32 z){ return (x&y)|(x&z)|(y&z); }
static u32 md4_h(u32 x,u32 y,u32 z){ return x^y^z; }
static u32 rol(u32 v,int s){ return (v<<s)|(v>>(32-s)); }
static void md4_block(u32* h,const u8* b){
    u32 X[16]; for(int i=0;i<16;i++)X[i]=(u32)b[i*4]|((u32)b[i*4+1]<<8)|((u32)b[i*4+2]<<16)|((u32)b[i*4+3]<<24);
    u32 a=h[0],bb=h[1],c=h[2],d=h[3];
    static const int S1[4]={3,7,11,19}, S2[4]={3,5,9,13}, S3[4]={3,9,11,15}; static const int O3[16]={0,8,4,12,2,10,6,14,1,9,5,13,3,11,7,15};
    for(int i=0;i<16;i++){ u32 t=a+md4_f(bb,c,d)+X[i]; t=rol(t,S1[i&3]); a=d; d=c; c=bb; bb=t; }
    for(int i=0;i<16;i++){ int k=(i&3)*4+(i>>2); u32 t=a+md4_g(bb,c,d)+X[k]+0x5A827999u; t=rol(t,S2[i&3]); a=d; d=c; c=bb; bb=t; }
    for(int i=0;i<16;i++){ u32 t=a+md4_h(bb,c,d)+X[O3[i]]+0x6ED9EBA1u; t=rol(t,S3[i&3]); a=d; d=c; c=bb; bb=t; }
    h[0]+=a; h[1]+=bb; h[2]+=c; h[3]+=d; }
void md4_hash(const u8* m,int len,u8 out[16]){
    u32 h[4]={0x67452301u,0xEFCDAB89u,0x98BADCFEu,0x10325476u}; int i=0; for(;i+64<=len;i+=64)md4_block(h,m+i);
    u8 t[128]; int r=len-i; for(int k=0;k<r;k++)t[k]=m[i+k]; t[r]=0x80; int tl=(r<56)?64:128; for(int k=r+1;k<tl;k++)t[k]=0;
    u64 bits=(u64)len*8; for(int k=0;k<8;k++)t[tl-8+k]=(u8)(bits>>(8*k)); md4_block(h,t); if(tl==128)md4_block(h,t+64);
    for(int k=0;k<4;k++){ out[k*4]=(u8)h[k]; out[k*4+1]=(u8)(h[k]>>8); out[k*4+2]=(u8)(h[k]>>16); out[k*4+3]=(u8)(h[k]>>24); } }

static const u8 PC1[56]={57,49,41,33,25,17,9,1,58,50,42,34,26,18,10,2,59,51,43,35,27,19,11,3,60,52,44,36,63,55,47,39,31,23,15,7,62,54,46,38,30,22,14,6,61,53,45,37,29,21,13,5,28,20,12,4};
static const u8 PC2[48]={14,17,11,24,1,5,3,28,15,6,21,10,23,19,12,4,26,8,16,7,27,20,13,2,41,52,31,37,47,55,30,40,51,45,33,48,44,49,39,56,34,53,46,42,50,36,29,32};
static const u8 IP[64]={58,50,42,34,26,18,10,2,60,52,44,36,28,20,12,4,62,54,46,38,30,22,14,6,64,56,48,40,32,24,16,8,57,49,41,33,25,17,9,1,59,51,43,35,27,19,11,3,61,53,45,37,29,21,13,5,63,55,47,39,31,23,15,7};
static const u8 FP[64]={40,8,48,16,56,24,64,32,39,7,47,15,55,23,63,31,38,6,46,14,54,22,62,30,37,5,45,13,53,21,61,29,36,4,44,12,52,20,60,28,35,3,43,11,51,19,59,27,34,2,42,10,50,18,58,26,33,1,41,9,49,17,57,25};
static const u8 E[48]={32,1,2,3,4,5,4,5,6,7,8,9,8,9,10,11,12,13,12,13,14,15,16,17,16,17,18,19,20,21,20,21,22,23,24,25,24,25,26,27,28,29,28,29,30,31,32,1};
static const u8 P[32]={16,7,20,21,29,12,28,17,1,15,23,26,5,18,31,10,2,8,24,14,32,27,3,9,19,13,30,6,22,11,4,25};
static const u8 SH[16]={1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1};
static const u8 SBX[8][64]={
{14,4,13,1,2,15,11,8,3,10,6,12,5,9,0,7,0,15,7,4,14,2,13,1,10,6,12,11,9,5,3,8,4,1,14,8,13,6,2,11,15,12,9,7,3,10,5,0,15,12,8,2,4,9,1,7,5,11,3,14,10,0,6,13},
{15,1,8,14,6,11,3,4,9,7,2,13,12,0,5,10,3,13,4,7,15,2,8,14,12,0,1,10,6,9,11,5,0,14,7,11,10,4,13,1,5,8,12,6,9,3,2,15,13,8,10,1,3,15,4,2,11,6,7,12,0,5,14,9},
{10,0,9,14,6,3,15,5,1,13,12,7,11,4,2,8,13,7,0,9,3,4,6,10,2,8,5,14,12,11,15,1,13,6,4,9,8,15,3,0,11,1,2,12,5,10,14,7,1,10,13,0,6,9,8,7,4,15,14,3,11,5,2,12},
{7,13,14,3,0,6,9,10,1,2,8,5,11,12,4,15,13,8,11,5,6,15,0,3,4,7,2,12,1,10,14,9,10,6,9,0,12,11,7,13,15,1,3,14,5,2,8,4,3,15,0,6,10,1,13,8,9,4,5,11,12,7,2,14},
{2,12,4,1,7,10,11,6,8,5,3,15,13,0,14,9,14,11,2,12,4,7,13,1,5,0,15,10,3,9,8,6,4,2,1,11,10,13,7,8,15,9,12,5,6,3,0,14,11,8,12,7,1,14,2,13,6,15,0,9,10,4,5,3},
{12,1,10,15,9,2,6,8,0,13,3,4,14,7,5,11,10,15,4,2,7,12,9,5,6,1,13,14,0,11,3,8,9,14,15,5,2,8,12,3,7,0,4,10,1,13,11,6,4,3,2,12,9,5,15,10,11,14,1,7,6,0,8,13},
{4,11,2,14,15,0,8,13,3,12,9,7,5,10,6,1,13,0,11,7,4,9,1,10,14,3,5,12,2,15,8,6,1,4,11,13,12,3,7,14,10,15,6,8,0,5,9,2,6,11,13,8,1,4,10,7,9,5,0,15,14,2,3,12},
{13,2,8,4,6,15,11,1,10,9,3,14,5,0,12,7,1,15,13,8,10,3,7,4,12,5,6,11,0,14,9,2,7,11,4,1,9,12,14,2,0,6,10,13,15,3,5,8,2,1,14,7,4,10,8,13,15,12,9,0,3,5,6,11}};
static u64 perm(u64 in,const u8* t,int n,int inbits){ u64 o=0; for(int i=0;i<n;i++){ o<<=1; o|=(in>>(inbits-t[i]))&1; } return o; }
void des_encrypt(const u8 key8[8],const u8 in[8],u8 out[8]){
    u64 k=0,b=0; for(int i=0;i<8;i++){ k=(k<<8)|key8[i]; b=(b<<8)|in[i]; }
    u64 cd=perm(k,PC1,56,64); u32 c=(u32)(cd>>28)&0x0FFFFFFF, d=(u32)cd&0x0FFFFFFF; u64 ks[16];
    for(int r=0;r<16;r++){ for(int s=0;s<SH[r];s++){ c=((c<<1)|(c>>27))&0x0FFFFFFF; d=((d<<1)|(d>>27))&0x0FFFFFFF; } ks[r]=perm(((u64)c<<28)|d,PC2,48,56); }
    u64 ip=perm(b,IP,64,64); u32 L=(u32)(ip>>32), R=(u32)ip;
    for(int r=0;r<16;r++){ u64 e=perm(R,E,48,32)^ks[r]; u32 f=0; for(int s=0;s<8;s++){ int six=(int)((e>>(42-6*s))&0x3F); int row=((six&0x20)>>4)|(six&1), col=(six>>1)&0xF; f=(f<<4)|SBX[s][row*16+col]; } f=(u32)perm(f,P,32,32); u32 t=R; R=L^f; L=t; }
    u64 pre=((u64)R<<32)|L; u64 o=perm(pre,FP,64,64); for(int i=7;i>=0;i--){ out[i]=(u8)o; o>>=8; } }
static void des7(const u8* k7,const u8* in,u8* out){ u8 k[8]; k[0]=k7[0]; k[1]=(u8)((k7[0]<<7)|(k7[1]>>1)); k[2]=(u8)((k7[1]<<6)|(k7[2]>>2)); k[3]=(u8)((k7[2]<<5)|(k7[3]>>3)); k[4]=(u8)((k7[3]<<4)|(k7[4]>>4)); k[5]=(u8)((k7[4]<<3)|(k7[5]>>5)); k[6]=(u8)((k7[5]<<2)|(k7[6]>>6)); k[7]=(u8)(k7[6]<<1); des_encrypt(k,in,out); }

void mschap_challenge_hash(const u8 peer[16],const u8 auth[16],const char* user,u8 out[8]){
    u8 b[16+16+256]; int n=0; for(int i=0;i<16;i++)b[n++]=peer[i]; for(int i=0;i<16;i++)b[n++]=auth[i];
    const char* u=user; for(int i=0;user[i];i++) if(user[i]=='\\')u=user+i+1; for(int i=0;u[i]&&n<(int)sizeof(b);i++)b[n++]=(u8)u[i];
    u8 h[20]; sha1_hash(b,(u32)n,h); for(int i=0;i<8;i++)out[i]=h[i]; }
void mschap_pw_hash(const char* pw,u8 out[16]){ u8 u[256]; int n=0; for(int i=0;pw[i]&&n<254;i++){ u[n++]=(u8)pw[i]; u[n++]=0; } md4_hash(u,n,out); for(int i=0;i<256;i++)((volatile u8*)u)[i]=0; }
void mschap_nt_response(const u8 auth[16],const u8 peer[16],const char* user,const char* pw,u8 out[24]){
    u8 ch[8]; mschap_challenge_hash(peer,auth,user,ch); u8 ph[21]; mschap_pw_hash(pw,ph); for(int i=16;i<21;i++)ph[i]=0;
    des7(ph,ch,out); des7(ph+7,ch,out+8); des7(ph+14,ch,out+16); for(int i=0;i<21;i++)((volatile u8*)ph)[i]=0; }
void mschap_auth_response(const char* pw,const u8 nt[24],const u8 peer[16],const u8 auth[16],const char* user,char out[43]){
    static const char M1[]="Magic server to client signing constant"; static const char M2[]="Pad to make it do more than one iteration";
    u8 ph[16],phh[16]; mschap_pw_hash(pw,ph); md4_hash(ph,16,phh);
    u8 b[16+24+64]; int n=0; for(int i=0;i<16;i++)b[n++]=phh[i]; for(int i=0;i<24;i++)b[n++]=nt[i]; for(int i=0;i<39;i++)b[n++]=(u8)M1[i];
    u8 d[20]; sha1_hash(b,(u32)n,d); u8 ch[8]; mschap_challenge_hash(peer,auth,user,ch);
    n=0; for(int i=0;i<20;i++)b[n++]=d[i]; for(int i=0;i<8;i++)b[n++]=ch[i]; for(int i=0;i<41;i++)b[n++]=(u8)M2[i]; sha1_hash(b,(u32)n,d);
    static const char* H="0123456789ABCDEF"; out[0]='S'; out[1]='='; for(int i=0;i<20;i++){ out[2+i*2]=H[d[i]>>4]; out[3+i*2]=H[d[i]&15]; } out[42]=0; }

static eap_tx_fn e_tx; static eap_rx_fn e_rx; static void* e_ctx;
static u8 e_id=0; static u8 e_out[20000]; static int e_on=0; static u8 e_in[20000]; static int e_inl=0, e_inp=0; static int e_done=0, e_fail=0, e_need=0;
int g_eap_stage=0; const char* g_eap_why=""; u8 g_eap_pin[32]; int g_eap_pin_set=0; u8 g_eap_pin_new[32];
static u8 e_last[1500]; static int e_lastlen=0, e_lastid=-1;
static int e_send_resp(const u8* d,int n){ e_need=0; if(n>1490)return -1; int L=4+n; e_last[0]=2; e_last[1]=e_id; e_last[2]=(u8)(L>>8); e_last[3]=(u8)L; for(int i=0;i<n;i++)e_last[4+i]=d[i]; e_lastlen=L; e_lastid=e_id; return e_tx(e_ctx,e_last,L); }
static int e_recv_req(u8* typ,u8* data,int* dl){
    int retry=0;
    for(int t=0;t<12;t++){ static u8 f[1600]; int n=e_rx(e_ctx,f,(int)sizeof(f)); if(n<=0){ if(retry<2&&e_lastlen){ retry++; e_tx(e_ctx,e_last,e_lastlen); continue; } return -1; } if(n<4)continue;
        if(f[0]==3){ e_done=1; return 0; } if(f[0]==4){ e_fail=1; g_eap_why="the server refused the sign-in"; return -2; }
        if(f[0]!=1||n<5)continue; int L=(f[2]<<8)|f[3]; if(L<5)continue; if(L>n){ g_eap_why="a message from the server was cut off"; return -1; } if(!e_need&&e_lastlen&&f[1]==e_lastid){ e_tx(e_ctx,e_last,e_lastlen); continue; } e_id=f[1]; e_need=1; if(L>n)L=n; *typ=f[4]; *dl=L-5; for(int i=0;i<*dl;i++)data[i]=f[5+i]; return 1; }
    return -1; }
static int e_peap_flush(void){
    int off=0, tot=e_on; if(!tot){ if(!e_need)return 0; u8 a[2]={25,0}; return e_send_resp(a,2); }
    while(off<tot){ int chunk=tot-off; if(chunk>1000)chunk=1000; u8 b[1100]; int n=0; b[n++]=25; u8 fl=0; if(off==0&&chunk<tot)fl|=0x80; if(off+chunk<tot)fl|=0x40; b[n++]=fl;
        if(fl&0x80){ b[n++]=(u8)(tot>>24); b[n++]=(u8)(tot>>16); b[n++]=(u8)(tot>>8); b[n++]=(u8)tot; } for(int i=0;i<chunk;i++)b[n++]=e_out[off+i];
        if(e_send_resp(b,n)<0)return -1; off+=chunk;
        if(off<tot){ u8 ty; static u8 d[1600]; int dl; if(e_recv_req(&ty,d,&dl)!=1||ty!=25)return -1; } }
    e_on=0; return 0; }
static int e_pull(void){
    if(e_peap_flush()<0)return -1;
    for(int fr=0;fr<64;fr++){ u8 ty; static u8 d[1600]; int dl; int r=e_recv_req(&ty,d,&dl); if(r!=1)return -1; if(ty!=25||dl<1)return -1;
        u8 fl=d[0]; int o=1; if(fl&0x80)o+=4; if((fl&0x40)&&dl<=o)return -1; if(e_inl+dl-o>(int)sizeof(e_in)){ g_eap_why="the server sent too much data"; return -1; } for(int i=o;i<dl;i++)e_in[e_inl++]=d[i];
        if(fl&0x40){ u8 a[2]={25,0}; if(e_send_resp(a,2)<0)return -1; continue; } return 0; }
    g_eap_why="the server sent too many fragments"; return -1; }
static int tio_wr(void* c,const u8* b,int n){ (void)c; if(e_on+n>(int)sizeof(e_out))return -1; for(int i=0;i<n;i++)e_out[e_on++]=b[i]; return n; }
static int tio_rd(void* c,u8* b,int n){ (void)c; if(e_inp>=e_inl){ e_inl=0; e_inp=0; if(e_pull()<0)return -1; if(!e_inl)return -1; } int k=e_inl-e_inp; if(k>n)k=n; for(int i=0;i<k;i++)b[i]=e_in[e_inp+i]; e_inp+=k; return k; }

static tls_io e_io; static tls_sess e_ss;
static int in_send(const u8* d,int n){ if(tls_sess_write(&e_io,&e_ss,d,n)<0)return -1; return e_peap_flush(); }
static int in_recv(u8* out,int cap){ int n=tls_sess_read(&e_io,&e_ss,out,cap); return n>0?n:-1; }

static int eap_run2(eap_tx_fn tx,eap_rx_fn rx,void* ctx,const char* user,const char* pass,u8 msk[64]){
    e_lastlen=0; e_lastid=-1; e_tx=tx; e_rx=rx; e_ctx=ctx; e_on=0; e_inl=0; e_inp=0; e_done=0; e_fail=0; e_need=0; g_eap_stage=1; g_eap_why="no answer from the network";
    u8 ty; static u8 d[1600]; int dl; char outer[96]; { int at=-1; for(int i=0;user[i];i++) if(user[i]=='@')at=i; int n=0; if(at>=0){ const char* a="anonymous"; for(int i=0;a[i];i++)outer[n++]=a[i]; for(int i=at;user[i]&&n<94;i++)outer[n++]=user[i]; } else { for(int i=0;user[i]&&n<94;i++)outer[n++]=user[i]; } outer[n]=0; }
    for(int t=0;t<3;t++){ int r=e_recv_req(&ty,d,&dl); if(r!=1)return -1; if(ty==1){ u8 b[100]; int n=0; b[n++]=1; for(int i=0;outer[i];i++)b[n++]=(u8)outer[i]; e_send_resp(b,n); continue; }
        if(ty==25){ if(dl<1||!(d[0]&0x20)){ g_eap_why="unexpected PEAP message"; return -1; } break; }
        { u8 nak[2]={3,25}; e_send_resp(nak,2); } }
    if(ty!=25){ g_eap_why="the network does not offer PEAP"; return -3; }
    g_eap_stage=2; e_io.wr=tio_wr; e_io.rd=tio_rd; e_io.ctx=0; e_io.rlen=0; e_io.rpos=0;
    u8 rnd[64]; csprng_bytes(rnd,64); int ins=g_tls_insecure, o12=g_tls_only12; g_tls_insecure=1; g_tls_only12=1; g_tls_pin=1; for(int i=0;i<32;i++)g_tls_leaf[i]=0;
    int tr=tls_connect_sess(&e_io,"",0,rnd,&e_ss); g_tls_insecure=ins; g_tls_only12=o12; g_tls_pin=0;
    if(tr<0||!e_ss.v12){ g_eap_why=g_tls_why?g_tls_why:"TLS tunnel failed"; return -4; }
    for(int i=0;i<32;i++)g_eap_pin_new[i]=g_tls_leaf[i];
    if(g_eap_pin_set){ u8 d0=0; for(int i=0;i<32;i++)d0|=g_eap_pin[i]^g_tls_leaf[i]; if(d0){ g_eap_why="the network's certificate changed - not sending your password (forget the network to trust the new one)"; return -12; } }
    g_eap_stage=3; static u8 buf[1600]; u8 peer[16]; csprng_bytes(peer,16); u8 authc[16]; u8 nt[24]; int have_ch=0, okmsg=0;
    for(int round=0;round<8;round++){
        int n=in_recv(buf,(int)sizeof(buf)); if(n<=0){ if(e_fail){ g_eap_why="the server refused the sign-in"; return -9; } if(e_done)break; g_eap_why="the tunnel closed early"; return -5; }
        u8* p=buf; int pl=n; int full=(n>=5&&(buf[0]==1||buf[0]==2)&&((buf[2]<<8)|buf[3])==n);
        u8 iid=e_id; if(full){ iid=buf[1]; p=buf+4; pl=n-4; }
        u8 t=p[0];
        if(t==1){ u8 r[100]; int k=0; r[k++]=1; for(int i=0;user[i]&&k<96;i++)r[k++]=(u8)user[i]; if(in_send(r,k)<0)return -6; continue; }
        if(t==26&&pl>=22&&p[1]==1){ int vs=p[5]; if(vs!=16||pl<6+16){ g_eap_why="bad MSCHAPv2 challenge"; return -7; } for(int i=0;i<16;i++)authc[i]=p[6+i];
            mschap_nt_response(authc,peer,user,pass,nt); have_ch=1;
            u8 r[200]; int k=0; int st=k; r[k++]=26; r[k++]=2; r[k++]=p[2]; int ml=k; r[k++]=0; r[k++]=0; r[k++]=49;
            for(int i=0;i<16;i++)r[k++]=peer[i]; for(int i=0;i<8;i++)r[k++]=0; for(int i=0;i<24;i++)r[k++]=nt[i]; r[k++]=0;
            const char* u=user; for(int i=0;user[i];i++) if(user[i]=='\\')u=user+i+1; for(int i=0;u[i]&&k<190;i++)r[k++]=(u8)u[i];
            int msl=k-st-1; r[ml]=(u8)(msl>>8); r[ml+1]=(u8)msl; if(in_send(r,k)<0)return -6; continue; }
        if(t==26&&pl>=5&&p[1]==3){ if(!have_ch)return -7; char ar[43]; mschap_auth_response(pass,nt,peer,authc,user,ar); int match=0;
            for(int i=5;i+42<=pl;i++){ int ok=1; for(int j=0;j<42;j++){ char a=(char)p[i+j], b=ar[j]; if(a>='a'&&a<='f')a-=32; if(a!=b){ ok=0; break; } } if(ok){ match=1; break; } }
            if(!match){ g_eap_why="the server could not prove it knows your password"; return -8; }
            u8 r[8]; int k=0; r[k++]=26; r[k++]=3; if(in_send(r,k)<0)return -6; okmsg=1; continue; }
        if(t==26&&pl>=2&&p[1]==4){ g_eap_why="wrong user name or password"; u8 r[2]={26,4}; in_send(r,2); return -9; }
        if(t==33){ int res=0; for(int i=1;i+5<pl;i++) if((p[i]&0x3F)==0&&p[i+1]==3){ res=(p[i+4]<<8)|p[i+5]; break; }
            u8 r[16]; int k=0; r[k++]=2; r[k++]=iid; r[k++]=0; r[k++]=11; r[k++]=33; r[k++]=0x80; r[k++]=0x03; r[k++]=0; r[k++]=2; r[k++]=0; r[k++]=(u8)(res==1&&okmsg?1:2);
            if(in_send(r,k)<0)return -6; if(res!=1||!okmsg){ g_eap_why="the server reported a sign-in failure"; return -9; }
            continue; }
        g_eap_why="unexpected message inside the tunnel"; return -10; }
    if(e_fail){ g_eap_why="the server refused the sign-in"; return -9; }
    if(!okmsg){ g_eap_why=e_fail?"the server refused the sign-in":"sign-in did not finish"; return -11; }
    u8 sd[64]; for(int i=0;i<32;i++){ sd[i]=g_tls12_crnd[i]; sd[32+i]=g_tls12_srnd[i]; }
    tls12_prf(g_tls12_ms,48,"client EAP encryption",sd,64,msk,64);
    for(int i=0;i<48;i++)g_tls12_ms[i]=0; tls_sess_wipe(&e_ss); g_eap_stage=4; g_eap_why="signed in"; return 0; }
int eap_peap_run(eap_tx_fn tx,eap_rx_fn rx,void* ctx,const char* user,const char* pass,u8 msk[64]){
    int r=eap_run2(tx,rx,ctx,user,pass,msk);
    for(int i=0;i<48;i++)g_tls12_ms[i]=0; tls_sess_wipe(&e_ss); for(int i=0;i<(int)sizeof(e_in);i++)e_in[i]=0; for(int i=0;i<(int)sizeof(e_out);i++)e_out[i]=0; for(int i=0;i<(int)sizeof(e_last);i++)e_last[i]=0; e_lastlen=0; e_inl=0; e_on=0;
    if(r<0)for(int i=0;i<64;i++)msk[i]=0; return r; }
