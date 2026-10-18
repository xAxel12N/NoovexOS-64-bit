#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

static int fails;
static void out(const char* s){
    fputs(s,stdout); fflush(stdout);
    int k=open("/dev/kmsg",O_WRONLY); if(k>=0){ if(write(k,s,strlen(s))<0){} close(k); }
}
static void res(const char* name,int ok,const char* detail){
    char b[256]; snprintf(b,sizeof b,"nettest: %s %s%s%s\n",ok?"ok  ":"FAIL",name,detail&&*detail?" - ":"",detail?detail:""); out(b);
    if(!ok)fails++;
}
static unsigned char pat(long i){ return (unsigned char)((i*7+(i>>8))&0xFF); }
static int tcp_to(struct in_addr a,int port){
    int s=socket(AF_INET,SOCK_STREAM,0); if(s<0)return -errno;
    struct sockaddr_in sa; memset(&sa,0,sizeof sa); sa.sin_family=AF_INET; sa.sin_port=htons(port); sa.sin_addr=a;
    if(connect(s,(struct sockaddr*)&sa,sizeof sa)<0){ int e=errno; close(s); return -e; }
    return s;
}
int main(int argc,char** argv){
    const char* host=argc>1?argv[1]:"noovex.test";
    char d[200];
    out("nettest: start\n");
    struct addrinfo hints,*ai=0; memset(&hints,0,sizeof hints); hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM;
    int g=getaddrinfo(host,"8080",&hints,&ai);
    struct in_addr srv; srv.s_addr=0;
    if(g==0&&ai){ srv=((struct sockaddr_in*)ai->ai_addr)->sin_addr; }
    snprintf(d,sizeof d,"%s -> %s",host,g==0?inet_ntoa(srv):gai_strerror(g)); res("dns lookup",g==0,d);
    if(ai)freeaddrinfo(ai);
    if(g!=0){ inet_aton("10.0.2.2",&srv); }

    int s=tcp_to(srv,8080);
    snprintf(d,sizeof d,"fd=%d",s); res("tcp connect",s>=0,d);
    if(s>=0){
        const char* rq="GET /big HTTP/1.0\r\nHost: noovex.test\r\n\r\n";
        res("tcp send request",write(s,rq,strlen(rq))==(ssize_t)strlen(rq),0);
        static char buf[400000]; long n=0; ssize_t r;
        while(n<(long)sizeof buf&&(r=read(s,buf+n,sizeof buf-n))>0)n+=r;
        close(s);
        char* body=strstr(buf,"\r\n\r\n"); long blen=body?n-(body+4-buf):-1; int good=body!=0;
        if(body){ body+=4; for(long i=0;i<blen;i++) if((unsigned char)body[i]!=pat(i)){ good=0; break; } }
        snprintf(d,sizeof d,"%ld bytes total, body %ld",n,blen); res("http download 300000 bytes",good&&blen==300000,d);
    }

    s=tcp_to(srv,7777);
    if(s>=0){
        static unsigned char tx[100000], rx[100000];
        for(int i=0;i<(int)sizeof tx;i++)tx[i]=pat(i*3+1);
        int one=1; setsockopt(s,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);
        long sent=0,got=0; int ok=1;
        while(got<(long)sizeof rx&&ok){
            if(sent<(long)sizeof tx){ ssize_t w=write(s,tx+sent,(sizeof tx-sent)>8192?8192:(sizeof tx-sent)); if(w<=0){ ok=0; break; } sent+=w; }
            ssize_t r=read(s,rx+got,sizeof rx-got); if(r<=0){ ok=0; break; } got+=r; }
        int same=ok&&got==(long)sizeof rx&&!memcmp(tx,rx,sizeof rx);
        shutdown(s,SHUT_WR); char c; ssize_t e=read(s,&c,1);
        close(s);
        snprintf(d,sizeof d,"%ld sent, %ld echoed, eof=%zd",sent,got,e); res("tcp echo 100000 bytes",same&&e==0,d);
    } else { snprintf(d,sizeof d,"errno %d",-s); res("tcp echo connect",0,d); }

    int u=socket(AF_INET,SOCK_DGRAM,0);
    if(u>=0){
        struct sockaddr_in to; memset(&to,0,sizeof to); to.sin_family=AF_INET; to.sin_port=htons(7778); to.sin_addr=srv;
        const char* m="ping over udp"; ssize_t w=sendto(u,m,strlen(m),0,(struct sockaddr*)&to,sizeof to);
        struct pollfd pf={u,POLLIN,0}; int pr=poll(&pf,1,3000);
        char rb[64]; struct sockaddr_in fr; socklen_t fl=sizeof fr; ssize_t r=pr>0?recvfrom(u,rb,sizeof rb-1,0,(struct sockaddr*)&fr,&fl):-1;
        if(r>0)rb[r]=0;
        snprintf(d,sizeof d,"sent %zd, poll %d, got %zd from port %d",w,pr,r,r>0?ntohs(fr.sin_port):0);
        res("udp echo",r>0&&!strcmp(rb,"PING OVER UDP")&&fr.sin_addr.s_addr==srv.s_addr&&ntohs(fr.sin_port)==7778,d);
        close(u);
    } else res("udp socket",0,0);

    s=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK,0);
    if(s>=0){
        struct sockaddr_in sa; memset(&sa,0,sizeof sa); sa.sin_family=AF_INET; sa.sin_port=htons(7777); sa.sin_addr=srv;
        int c=connect(s,(struct sockaddr*)&sa,sizeof sa); int e1=errno;
        struct pollfd pf={s,POLLOUT,0}; int pr=poll(&pf,1,5000);
        int err=-1; socklen_t el=sizeof err; getsockopt(s,SOL_SOCKET,SO_ERROR,&err,&el);
        char x; ssize_t r=read(s,&x,1); int e2=errno;
        snprintf(d,sizeof d,"connect=%d errno=%d poll=%d so_error=%d read=%zd errno=%d",c,e1,pr,err,r,e2);
        res("nonblocking connect",c<0&&e1==EINPROGRESS&&pr==1&&(pf.revents&POLLOUT)&&err==0&&r<0&&e2==EAGAIN,d);
        close(s);
    }

    s=tcp_to(srv,7779);
    snprintf(d,sizeof d,"result %d",s); res("refused port gives ECONNREFUSED",s==-ECONNREFUSED,d);

    int bad=socket(AF_INET6,SOCK_STREAM,0);
    res("AF_INET6 unsupported",bad<0&&errno==EAFNOSUPPORT,0);

    out(fails?"nettest: FAIL\n":"nettest: PASS\n");
    return fails?1:0;
}
