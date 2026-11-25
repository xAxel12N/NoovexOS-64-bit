#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8;
#include "chunk_ext.c"
static int fails;
static void ok(int c,const char* m){ if(!c){ printf("FAIL %s\n",m); fails++; } }
static int cd(const char* s){ int n=(int)strlen(s); u8* b=malloc(n?n:1); memcpy(b,s,n); int p=0; int r=chunk_done(b,n,&p); free(b); return r; }
static int dc(const char* s,char* out){ int n=(int)strlen(s); u8* b=malloc(n?n:1); memcpy(b,s,n); int w=dechunk(b,n); memcpy(out,b,w); out[w]=0; free(b); return w; }
int main(int argc,char** argv){
    char o[4096];
    ok(cd("5\r\nhello\r\n0\r\n\r\n")==15,"complete body ends after the last chunk");
    ok(cd("5\r\nhel")==0,"partial chunk waits");
    ok(cd("80000000\r\nxx")<0,"chunk size 0x80000000 refused (was int overflow, negative index)");
    ok(cd("8000000\r\nxx")==0||cd("8000000\r\nxx")<0,"chunk size 2^27 does not wrap");
    ok(cd("ffffffff0\r\n")<0,"huge chunk size refused");
    ok(cd("zz\r\n")<0,"no hex digits refused");
    ok(dc("5\r\nhello\r\n3\r\nabc\r\n0\r\n\r\n",o)==8&&!strcmp(o,"helloabc"),"dechunk joins chunks");
    ok(dc("80000000\r\nab",o)<=2,"dechunk caps a huge size at the data present");
    long iters=argc>1?atol(argv[1]):200000; srand(1); u8 b[600];
    for(long it=0;it<iters;it++){ int n=rand()%sizeof b; for(int i=0;i<n;i++){ int r=rand()%8; b[i]=r<3?"0123456789abcdefF"[rand()%17]:r<5?"\r\n"[rand()%2]:(u8)rand(); }
        u8* h=malloc(n?n:1); memcpy(h,b,n); int p=0; int r=chunk_done(h,n,&p); if(r>n||p>n||p<0){ printf("FAIL chunk_done out of range r=%d p=%d n=%d\n",r,p,n); fails++; break; } memcpy(h,b,n); int w=dechunk(h,n); if(w<0||w>n){ printf("FAIL dechunk w=%d n=%d\n",w,n); fails++; break; } free(h); }
    if(!fails)puts("CHUNK all ok"); return fails!=0;
}
