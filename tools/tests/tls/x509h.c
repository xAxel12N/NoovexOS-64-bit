#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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
static int hexb(const char* h,u8* o){ int n=0; while(h[0]&&h[1]){ unsigned v; sscanf(h,"%2x",&v); o[n++]=(u8)v; h+=2; } return n; }
static void phex(const u8* b,int n){ for(int i=0;i<n;i++)printf("%02x",b[i]); printf("\n"); }
static int s_wr(void* c,const u8* b,int n){ return (int)write(*(int*)c,b,n); }
static int s_rd(void* c,u8* b,int n){ return (int)read(*(int*)c,b,n); }
static tls_io g_hio;
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
    if(!strcmp(argv[1],"ecdh")){
        u8 k[32],peer[65],out[65]; hexb(argv[2],k);
        if(argc>3){ int pl=hexb(argv[3],peer); if(x509_p256_ecdh(k,peer,pl,out)){ printf("ERR\n"); return 0; } phex(out,32); }
        else { if(x509_p256_ecdh(k,0,0,out)){ printf("ERR\n"); return 0; } phex(out,65); }
        return 0; }
    if(!strcmp(argv[1],"get")){
        const char* host=argv[2]; int port=atoi(argv[3]); g_tls_insecure=1;
        for(int i=6;i<argc;i++){ int pl; u8* pem=keep(rd(argv[i],&pl)); u8* z=keep(malloc(pl+1)); memcpy(z,pem,pl); z[pl]=0; const char* pp=(const char*)z; for(;;){ u8* der=keep(malloc(8192)); int n=x509_pem_next(&pp,der,8192); if(n<=0)break; if(x509_add_anchor_der(der,n)){ printf("anchor fail\n"); return 1; } } g_tls_insecure=0; }
        int fd=socket(AF_INET,SOCK_STREAM,0); struct sockaddr_in a; memset(&a,0,sizeof a); a.sin_family=AF_INET; a.sin_port=htons(port); a.sin_addr.s_addr=inet_addr("127.0.0.1");
        if(connect(fd,(struct sockaddr*)&a,sizeof a)){ printf("noconn\n"); return 1; }
        g_hio.wr=s_wr; g_hio.rd=s_rd; g_hio.ctx=&fd;
        u8 rnd[64]; FILE* r=fopen("/dev/urandom","rb"); if(fread(rnd,1,64,r)!=64)return 2; fclose(r);
        static tls_sess ss; int rc=tls_connect_sess(&g_hio,host,(int)strlen(host),rnd,&ss);
        printf("connect %d group-cipher %04x %s\n",rc,ss.cipher,g_tls_why); if(rc)return 1;
        char req[256]; int rl=snprintf(req,sizeof req,"GET %s HTTP/1.0\r\nHost: %s\r\n\r\n",argc>4?argv[4]:"/",host);
        tls_sess_write(&g_hio,&ss,(const u8*)req,rl);
        static u8 buf[1<<20]; long tot=0; int n; while((n=tls_sess_read(&g_hio,&ss,buf+tot,(int)(sizeof buf-tot)))>0)tot+=n;
        if(argc>5){ FILE* o=fopen(argv[5],"wb"); fwrite(buf,1,tot,o); fclose(o); }
        printf("read %ld last %d\n",tot,n); close(fd); return 0; }
    return 2;
}
