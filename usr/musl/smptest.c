#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/syscall.h>

struct rep { int id; unsigned seen; int errors; unsigned long acc; long work; unsigned why; };

static int cur_cpu(void){ unsigned c=0; if(syscall(SYS_getcpu,&c,0,0)<0)return 0; return (int)(c&31); }
static long now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000L+t.tv_nsec/1000000L; }
static void fill(unsigned char* b,int n,unsigned s){ for(int i=0;i<n;i++){ s=s*1103515245u+12345u; b[i]=(unsigned char)(s>>16); } }

static void worker(int id,int rounds,long spin,int wfd){
    struct rep r; memset(&r,0,sizeof r); r.id=id;
    static unsigned char a[6000], b[6000];
    for(int round=0;round<rounds;round++){
        volatile unsigned long acc=r.acc;
        for(long i=0;i<spin;i++)acc+=(unsigned long)(i*i)^(unsigned long)id;
        r.acc=acc; r.work+=spin;
        r.seen|=1u<<cur_cpu();
        char p[64]; snprintf(p,sizeof p,"/tmp/smp_%d_%d",id,round);
        int n=1000+(id*37+round*91)%5000; fill(a,n,(unsigned)(id*1000+round));
        int fd=open(p,O_CREAT|O_RDWR|O_TRUNC,0644);
        if(fd<0){ r.errors++; r.why|=1; continue; }
        if(write(fd,a,n)!=n){ r.errors++; r.why|=2; }
        lseek(fd,0,SEEK_SET); memset(b,0,sizeof b);
        if(read(fd,b,n)!=n||memcmp(a,b,n)){ r.errors++; r.why|=4; }
        close(fd);
        if(unlink(p)){ r.errors++; r.why|=8; }
        if(round%4==0){
            int pp[2]; if(pipe(pp)){ r.errors++; r.why|=16; continue; }
            pid_t c=fork();
            if(c<0){ r.errors++; r.why|=1024; close(pp[0]); close(pp[1]); continue; }
            if(c==0){ close(pp[0]); char m=(char)('A'+id%26); if(write(pp[1],&m,1)!=1)_exit(1); _exit(7); }
            close(pp[1]); char ch=0; if(read(pp[0],&ch,1)!=1||ch!=(char)('A'+id%26)){ r.errors++; r.why|=32; } close(pp[0]);
            int st=0; if(waitpid(c,&st,0)!=c||!WIFEXITED(st)||WEXITSTATUS(st)!=7){ r.errors++; r.why|=64; }
        }
        if(round%5==1){
            size_t len=64*1024; unsigned char* m=mmap(0,len,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
            if(m==MAP_FAILED){ r.errors++; r.why|=128; continue; }
            for(size_t i=0;i<len;i+=4096)m[i]=(unsigned char)(id+i);
            pid_t c=fork();
            if(c<0){ r.errors++; r.why|=2048; munmap(m,len); continue; }
            if(c==0){ int bad=0; for(size_t i=0;i<len;i+=4096){ if(m[i]!=(unsigned char)(id+i))bad=1; m[i]=0xEE; } _exit(bad); }
            int st=0; waitpid(c,&st,0); if(!WIFEXITED(st)||WEXITSTATUS(st)!=0){ r.errors++; r.why|=256; }
            for(size_t i=0;i<len;i+=4096) if(m[i]!=(unsigned char)(id+i)){ r.errors++; r.why|=512; break; }
            munmap(m,len);
        }
    }
    if(write(wfd,&r,sizeof r)!=(ssize_t)sizeof r)_exit(2);
    _exit(0);
}
static void out(const char* s){
    fputs(s,stdout); fflush(stdout);
    int k=open("/dev/kmsg",O_WRONLY); if(k>=0){ if(write(k,s,strlen(s))<0){} close(k); }
}
int main(int argc,char** argv){
    int n=argc>1?atoi(argv[1]):8; if(n<1)n=1; if(n>12)n=12;
    int rounds=argc>2?atoi(argv[2]):24; if(rounds<1)rounds=1;
    long spin=argc>3?atol(argv[3]):3000000L;
    long cpus=sysconf(_SC_NPROCESSORS_ONLN);
    char line[200];
    snprintf(line,sizeof line,"smptest: %d workers x %d rounds on %ld online CPU(s)\n",n,rounds,cpus); out(line);
    int pp[2]; if(pipe(pp)){ out("smptest: FAIL pipe\n"); return 1; }
    long t0=now_ms();
    for(int i=0;i<n;i++){ pid_t c=fork(); if(c==0){ close(pp[0]); worker(i,rounds,spin,pp[1]); } if(c<0){ out("smptest: FAIL fork\n"); return 1; } }
    close(pp[1]);
    unsigned seen=0, why=0; int errors=0, got=0; long work=0;
    struct rep r; while(got<n&&read(pp[0],&r,sizeof r)==(ssize_t)sizeof r){ seen|=r.seen; errors+=r.errors; why|=r.why; work+=r.work; got++; }
    int bad=0, st; for(int i=0;i<n;i++){ if(wait(&st)<0||!WIFEXITED(st)||WEXITSTATUS(st)!=0)bad++; }
    long ms=now_ms()-t0; int used=0; for(int i=0;i<32;i++) if(seen&(1u<<i))used++;
    snprintf(line,sizeof line,"smptest: %d reports, %d CPUs used (mask %x), %d errors (kinds %x), %d bad exits, %ld ms\n",got,used,seen,errors,why,bad,ms); out(line);
    int ok=got==n&&!errors&&!bad&&(cpus<2||used>=2);
    out(ok?"smptest: PASS\n":"smptest: FAIL\n");
    return ok?0:1;
}
