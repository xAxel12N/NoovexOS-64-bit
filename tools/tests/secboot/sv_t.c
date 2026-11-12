#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned int u32; typedef unsigned long long u64;
#include "../../../lib/crypto/nvxsigv.c"
static u8* rd(const char* p,u64* n){ FILE* f=fopen(p,"rb"); if(!f)return 0; fseek(f,0,2); *n=(u64)ftell(f); fseek(f,0,0); u8* b=malloc(*n+1); if(fread(b,1,*n,f)!=*n){} fclose(f); return b; }
int main(int argc,char** argv){
    if(argc>=2&&!strcmp(argv[1],"sha")){ u64 n; u8* b=rd(argv[2],&n); sv_sha c; sv_sha_init(&c); sv_sha_update(&c,b,n); u8 d[32]; sv_sha_final(&c,d); for(int i=0;i<32;i++)printf("%02x",d[i]); printf("\n"); free(b); return 0; }
    if(argc>=2&&!strcmp(argv[1],"ecdsa")){ u8 pub[65],h[32],s[64]; for(int i=0;i<65;i++)sscanf(argv[2]+2*i,"%2hhx",&pub[i]); for(int i=0;i<32;i++)sscanf(argv[3]+2*i,"%2hhx",&h[i]); for(int i=0;i<64;i++)sscanf(argv[4]+2*i,"%2hhx",&s[i]); printf("%d\n",sv_p256_verify(pub,h,s)); return 0; }
    if(argc>=2&&!strcmp(argv[1],"kernel")){ u64 n; u8* b=rd(argv[2],&n); u8 pub[65]; u8* pp=0; if(argc>3){ for(int i=0;i<65;i++)sscanf(argv[3]+2*i,"%2hhx",&pub[i]); pp=pub; } u64 off; int r=sv_kernel_verify(b,n,pp,&off); printf("%d %s\n",r,r?sv_why(r):"ok"); free(b); return r?1:0; }
    return 2;
}
