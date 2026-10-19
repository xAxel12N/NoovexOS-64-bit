#include "posix.h"
static volatile int g_chld=0;
static void on_chld(int s){ (void)s; g_chld=1; }
static void run_line(char* line){
    char* argv[12]; int n=0; char* p=line;
    while(*p&&n<11){ while(*p==' '||*p=='\t')p++; if(!*p)break; argv[n++]=p; while(*p&&*p!=' '&&*p!='\t')p++; if(*p)*p++=0; }
    argv[n]=0; if(!n||argv[0][0]=='#')return;
    int bg=0; if(!strcmp(argv[n-1],"&")){ bg=1; argv[--n]=0; if(!n)return; }
    pid_t c=fork();
    if(c==0){ execv(argv[0],argv); printf("init: cannot run %s\n",argv[0]); exit(127); }
    if(c<0){ printf("init: fork failed\n"); return; }
    if(!bg){ int st; waitpid(c,&st,0); }
}
int main(void){
    signal(SIGCHLD,on_chld);
    int fd=open("/etc/rc",O_RDONLY);
    if(fd>=0){ static char buf[4096]; long n=read(fd,buf,sizeof(buf)-1); close(fd); if(n<0)n=0; buf[n]=0;
        char* l=buf; while(*l){ char* e=l; while(*e&&*e!='\n')e++; char save=*e; *e=0; char line[256]; strncpy(line,l,255); line[255]=0; run_line(line); if(!save)break; l=e+1; } }
    int lf=open("/var/log/boot.log",O_CREAT|O_WRONLY|O_APPEND);
    if(lf>=0){ time_t t=time(0); char b[96]; int m=snprintf(b,sizeof(b),"boot: init started, /etc/rc done at %s\n",ctime(&t)); write(lf,b,(size_t)m); close(lf); }
    for(;;){ int st; while(waitpid(-1,&st,WNOHANG)>0){} pause(); }
    return 0;
}
