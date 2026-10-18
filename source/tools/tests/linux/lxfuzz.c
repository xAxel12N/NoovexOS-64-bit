#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>

static unsigned long long rs=0x9E3779B97F4A7C15ULL;
static unsigned long long rnd(void){ rs^=rs<<13; rs^=rs>>7; rs^=rs<<17; return rs; }
static const int DENY[]={0,7,9,11,12,15,22,23,34,35,56,57,58,59,60,61,62,130,158,200,230,231,234,247,270,271,293,24,165,166,169,105,106,113,114,117,119,170,202,43,288,37,38};
static int denied(long n){ for(unsigned i=0;i<sizeof DENY/sizeof DENY[0];i++) if(DENY[i]==n)return 1; return 0; }
static char* g_buf; static char* g_edge; static char* g_str;
static const char* PATHS[]={"/tmp/fz","/tmp/fzd","/","/tmp","/dev/null","/dev/zero","/proc/version","/bin/mhello","/nope/x","","..","/tmp/fz/../fz","/dev/urandom","/tmp/fzl"};
static long arg(void){
    switch(rnd()%16){
    case 0: return 0;
    case 1: return -1;
    case 2: return (long)(rnd()%8);
    case 3: return (long)(rnd()%64);
    case 4: return (long)g_buf+(long)(rnd()%60000);
    case 5: return (long)g_edge-(long)(rnd()%16);
    case 6: return 0x100000;
    case 7: return 0x0C000000;
    case 8: return 0x40070000000L;
    case 9: return (long)0x8000000000000000ULL;
    case 10: { const char* p=PATHS[rnd()%(sizeof PATHS/sizeof PATHS[0])]; strcpy(g_str,p); return (long)g_str; }
    case 11: return (long)(rnd()%0x1000000);
    case 12: return (long)rnd();
    case 13: return -100;
    case 14: return 0x7FFFFFFF;
    default: return (long)g_edge-4096;
    }
}
static int mem_phase(long iters){
    struct { char* p; size_t n; unsigned char tag; } sl[48]; memset(sl,0,sizeof sl);
    struct { char* p; size_t n; unsigned char tag; } ml[64]; memset(ml,0,sizeof ml);
    int bad=0; char* b0=(char*)syscall(SYS_brk,0); char* bcur=b0;
    for(int i=0;i<400;i++){ long d=(long)(rnd()%(256*1024))-(long)(96*1024); char* want=bcur+d; if(want<b0)want=b0;
        char* got=(char*)syscall(SYS_brk,want); if(got!=want)continue;
        if(want>bcur){ char* z=(char*)(((unsigned long)bcur+4095)&~4095UL); for(char* q=z;q<want;q+=331)if(*q)bad|=4; }
        for(char* q=b0;q<want;q+=1024)*q=1; bcur=want; }
    syscall(SYS_brk,b0); bcur=b0;
    for(long i=0;i<iters;i++){
        int op=(int)(rnd()%7);
        if(op==0){ int k=(int)(rnd()%48); if(sl[k].p)continue; size_t n=(size_t)(rnd()%(64*4096))+1; char* p=mmap(0,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
            if(p==MAP_FAILED)continue; for(size_t q=0;q<n;q+=509)if(p[q])bad|=1; sl[k].p=p; sl[k].n=n; sl[k].tag=(unsigned char)rnd(); memset(p,sl[k].tag,n); }
        else if(op==1){ int k=(int)(rnd()%48); if(!sl[k].p)continue; for(size_t q=0;q<sl[k].n;q+=97)if((unsigned char)sl[k].p[q]!=sl[k].tag)bad|=2; munmap(sl[k].p,sl[k].n); sl[k].p=0; }
        else if(op==2){ continue; }
        else if(op==3){ int k=(int)(rnd()%64); if(ml[k].p){ for(size_t q=0;q<ml[k].n;q+=13)if((unsigned char)ml[k].p[q]!=ml[k].tag)bad|=8; free(ml[k].p); ml[k].p=0; }
            else { size_t n=(size_t)(rnd()%((rnd()&7)?2048:300000))+1; ml[k].p=malloc(n); if(!ml[k].p)continue; ml[k].n=n; ml[k].tag=(unsigned char)rnd(); memset(ml[k].p,ml[k].tag,n); } }
        else if(op==4){ int k=(int)(rnd()%64); if(!ml[k].p)continue; size_t n=(size_t)(rnd()%200000)+1; char* q=realloc(ml[k].p,n); if(!q)continue;
            size_t keep=n<ml[k].n?n:ml[k].n; for(size_t z=0;z<keep;z+=17)if((unsigned char)q[z]!=ml[k].tag)bad|=16; ml[k].p=q; ml[k].n=n; memset(q,ml[k].tag,n); }
        else if(op==5){ int pp[2]; if(pipe2(pp,O_NONBLOCK))continue; char w[3000],r[3000]; int n=(int)(rnd()%3000)+1; for(int z=0;z<n;z++)w[z]=(char)rnd();
            int a=(int)write(pp[1],w,(size_t)n); int got=0,c; while((c=(int)read(pp[0],r+got,sizeof r-got))>0)got+=c; if(a!=got||memcmp(w,r,(size_t)got))bad|=32; close(pp[0]); close(pp[1]); }
        else { int k=(int)(rnd()%48); if(!sl[k].p||sl[k].n<8192)continue; size_t off=((size_t)(rnd()%(sl[k].n/4096)))*4096; size_t len=4096;
            if(off+len>sl[k].n)continue; munmap(sl[k].p+off,len); char* p2=mmap(sl[k].p+off,len,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
            if(p2!=sl[k].p+off){ bad|=64; continue; } for(size_t q=0;q<len;q+=64)if(p2[q])bad|=128; memset(p2,sl[k].tag,len); }
    }
    for(int k=0;k<48;k++) if(sl[k].p)munmap(sl[k].p,sl[k].n);
    for(int k=0;k<64;k++) free(ml[k].p);
    syscall(SYS_brk,b0);
    return bad;
}
int main(int argc,char** argv){
    long iters=argc>1?atol(argv[1]):20000; long trace=argc>2?atol(argv[2]):1L<<60;
    if(argc>3)rs^=(unsigned long long)atol(argv[3])*0x2545F4914F6CDD1DULL;
    close(0);
    g_buf=malloc(65536); memset(g_buf,0,65536);
    char* m=mmap(0,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    munmap(m+4096,4096); g_edge=m+4096; memset(m,'A',4096);
    g_str=malloc(512);
    long counts[3]={0,0,0};
    int cp[2]; pipe(cp);
    pid_t child=fork();
    if(child==0){ close(cp[0]);
    for(long i=0;i<iters;i++){
        long n=(long)(rnd()%336); if(n==335)n=(rnd()&1)?436:439; if(denied(n))continue;
        long a[6]; for(int k=0;k<6;k++)a[k]=arg();
        if(n==1&&(a[0]==1||a[0]==2))a[0]=3;
        if(n==3&&a[0]<3)continue;
        if(n==33||n==292){ if(a[1]<3)a[1]=3; }
        if(n==436){ if(a[0]<3)a[0]=3; }
        if(n==72&&a[0]<3)continue;
        if(n==16&&a[0]<3)continue;
        if(n==77&&a[0]<3)continue;
        if(n==13||n==14){ if(a[0]==9||a[0]==19)a[0]=10; if(n==13&&(a[0]==11||a[0]==7||a[0]==4||a[0]==8))a[1]=0; if(n==14)a[1]=0; }
        if(n==95)a[0]&=0777;
        if(n==80||n==81){ continue; }
        if(n==25&&a[0]!=0)a[0]=0;
        if(i>=trace){ char t[200]; int L=snprintf(t,sizeof t,"#%ld n=%ld %lx %lx %lx %lx %lx %lx\n",i,n,a[0],a[1],a[2],a[3],a[4],a[5]); write(1,t,L); }
        long r=syscall(n,a[0],a[1],a[2],a[3],a[4],a[5]);
        counts[r<0?0:r==0?1:2]++;
    }
    write(cp[1],counts,sizeof counts); _exit(0); }
    close(cp[1]); long got[3]={0,0,0}; int gr=(int)read(cp[0],got,sizeof got); close(cp[0]); int cst=0; waitpid(child,&cst,0);
    if(gr==(int)sizeof got){ counts[0]=got[0]; counts[1]=got[1]; counts[2]=got[2]; }
    int mb=mem_phase(iters/4);
    if(mb)printf("lxfuzz memory phase found corrupted data (mask %x)\n",mb);
    for(int fd=3;fd<64;fd++)close(fd);
    unlink("/tmp/fz"); unlink("/tmp/fzl"); rmdir("/tmp/fzd"); rmdir("/tmp/fz");
    if(!mb)printf("lxfuzz done: %ld errors, %ld zero, %ld positive\n",counts[0],counts[1],counts[2]);
    return mb?1:0;
}
