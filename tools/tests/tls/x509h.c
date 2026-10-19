#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lib/crypto/nvxcrypto.c"
#include "lib/crypto/nvxtls.c"
#include "lib/crypto/nvxcrypto2.c"
#include "lib/crypto/nvxx509.c"
#include "lib/crypto/nvxroots.c"
void* kmalloc(unsigned long n){ return malloc(n); }
void kfree(void* p){ free(p); }
void tls_prepare(void){}
i64 tls_clock_now(void){ return 0; }
static u8* rd(const char* p,int* n){ FILE* f=fopen(p,"rb"); if(!f){ perror(p); exit(2); } fseek(f,0,2); long l=ftell(f); fseek(f,0,0); u8* b=malloc(l+1); if(fread(b,1,l,f)!=(size_t)l)exit(2); fclose(f); *n=(int)l; return b; }
static u8* g_bufs[64]; static int g_nbuf;
static u8* keep(u8* b){ if(g_nbuf<64)g_bufs[g_nbuf++]=b; return b; }
static void release(void){ for(int i=0;i<g_nbuf;i++)free(g_bufs[i]); g_nbuf=0; }
int main(int argc,char** argv){
    setvbuf(stdout,0,_IONBF,0); atexit(release);
    if(argc<2)return 2;
    if(!strcmp(argv[1],"roots")){
        int ok=0,bad=0,skip=0;
        for(int i=0;i<NVX_NROOTS;i++){ x509_anchor a; nvx_root_get(i,&a); x509_key k; if(x509_parse_spki(a.spki,a.spkilen,&k)){ printf("spki parse fail %s\n",nvx_root_names[i]); bad++; } else ok++; }
        printf("anchors parsed %d bad %d skip %d\n",ok,bad,skip); return bad?1:0; }
    if(!strcmp(argv[1],"selfsig")){
        int ok=0,bad=0,skip=0;
        for(int i=2;i<argc;i++){ int n; u8* d=rd(argv[i],&n); x509_cert x; if(x509_parse(d,n,&x)){ printf("PARSEFAIL %s\n",argv[i]); bad++; free(d); continue; }
            if(!x.sigalg){ skip++; free(d); continue; }
            if(x509_verify_with(x.sigalg,x.tbs,x.tbslen,x.sig,x.siglen,x.spki,x.spkilen)){ printf("SIGFAIL %s alg %d\n",argv[i],x.sigalg); bad++; } else ok++; free(d); }
        printf("selfsig ok %d bad %d skipped %d\n",ok,bad,skip); return bad?1:0; }
    if(!strcmp(argv[1],"chain")){
        const char* host=argv[2]; i64 now=atoll(argv[3]); const u8* cs[10]; int ls[10]; int n=0;
        for(int i=4;i<argc&&n<10;i++){ if(!strcmp(argv[i],"-a")){ int l; u8* d=keep(rd(argv[++i],&l)); x509_add_anchor_der(d,l); continue; } int l; cs[n]=keep(rd(argv[i],&l)); ls[n++]=l; }
        int r=x509_verify_chain(cs,ls,n,host[0]?host:0,now); printf("%d %s\n",r,g_x509_why); return 0; }
    if(!strcmp(argv[1],"cv")){
        int sl,gl,hl; u8* spki=keep(rd(argv[2],&sl)); int scheme=(int)strtol(argv[3],0,16); u8* sig=keep(rd(argv[4],&gl)); u8* th=keep(rd(argv[5],&hl));
        int r=tls13_verify_cv(spki,sl,scheme,sig,gl,th,hl); printf("%d\n",r); return 0; }
    return 2;
}
