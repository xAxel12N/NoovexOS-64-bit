#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <wchar.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

static unsigned long g_rnd=0x2545F4914F6CDD1DUL;
long random(void){ g_rnd=g_rnd*6364136223846793005UL+1442695040888963407UL; return (long)((g_rnd>>33)&0x7FFFFFFF); }
void srandom(unsigned s){ g_rnd=(unsigned long)s*2654435761UL+1; }
char* strdup(const char* s){ size_t n=strlen(s)+1; char* d=malloc(n); if(d)memcpy(d,s,n); return d; }
int open(const char* p,int f,...){ (void)p;(void)f; errno=ENOENT; return -1; }
ssize_t read(int fd,void* b,size_t n){ (void)fd;(void)b;(void)n; errno=EBADF; return -1; }
int close(int fd){ (void)fd; return 0; }
uid_t getuid(void){ return 0; }
gid_t getgid(void){ return 0; }
pid_t getpid(void){ return 1; }
struct passwd* getpwnam(const char* n){ (void)n; return NULL; }
struct passwd* getpwuid(uid_t u){ (void)u; return NULL; }
struct group* getgrnam(const char* n){ (void)n; return NULL; }
struct group* getgrgid(gid_t g){ (void)g; return NULL; }

int mbsinit(const mbstate_t* ps){ return !ps||ps->count==0; }
size_t mbrtowc(wchar_t* pwc,const char* s,size_t n,mbstate_t* ps){
    static mbstate_t internal; if(!ps)ps=&internal;
    if(!s){ ps->count=0; return 0; }
    if(n==0)return (size_t)-2;
    const unsigned char* u=(const unsigned char*)s; size_t i=0;
    while(i<n){
        unsigned c=u[i];
        if(ps->count==0){
            if(c<0x80){ if(pwc)*pwc=(wchar_t)c; return c?1:0; }
            if(c>=0xC2&&c<=0xDF){ ps->value=c&0x1F; ps->count=1; }
            else if(c>=0xE0&&c<=0xEF){ ps->value=c&0x0F; ps->count=2; }
            else if(c>=0xF0&&c<=0xF4){ ps->value=c&0x07; ps->count=3; }
            else { errno=EILSEQ; return (size_t)-1; }
        } else {
            if((c&0xC0)!=0x80){ ps->count=0; errno=EILSEQ; return (size_t)-1; }
            ps->value=(ps->value<<6)|(c&0x3F); ps->count--;
            if(ps->count==0){
                unsigned v=ps->value;
                if((v>=0xD800&&v<=0xDFFF)||v>0x10FFFF){ errno=EILSEQ; return (size_t)-1; }
                if(pwc)*pwc=(wchar_t)v; return i+1;
            }
        }
        i++;
    }
    return (size_t)-2;
}
size_t wcrtomb(char* s,wchar_t wc,mbstate_t* ps){
    (void)ps; unsigned v=(unsigned)wc; char buf[4]; if(!s)s=buf;
    if(v<0x80){ s[0]=(char)v; return 1; }
    if(v<0x800){ s[0]=(char)(0xC0|(v>>6)); s[1]=(char)(0x80|(v&0x3F)); return 2; }
    if(v>=0xD800&&v<=0xDFFF){ errno=EILSEQ; return (size_t)-1; }
    if(v<0x10000){ s[0]=(char)(0xE0|(v>>12)); s[1]=(char)(0x80|((v>>6)&0x3F)); s[2]=(char)(0x80|(v&0x3F)); return 3; }
    if(v<=0x10FFFF){ s[0]=(char)(0xF0|(v>>18)); s[1]=(char)(0x80|((v>>12)&0x3F)); s[2]=(char)(0x80|((v>>6)&0x3F)); s[3]=(char)(0x80|(v&0x3F)); return 4; }
    errno=EILSEQ; return (size_t)-1;
}
size_t mbsrtowcs(wchar_t* dst,const char** src,size_t len,mbstate_t* ps){
    static mbstate_t internal; if(!ps)ps=&internal;
    const char* s=*src; size_t n=0;
    for(;;){
        if(dst&&n>=len)break;
        wchar_t wc; size_t r=mbrtowc(&wc,s,4,ps);
        if(r==(size_t)-1||r==(size_t)-2){ if(dst)*src=s; errno=EILSEQ; return (size_t)-1; }
        if(r==0){ if(dst){ dst[n]=0; *src=NULL; } return n; }
        if(dst)dst[n]=wc; n++; s+=r;
    }
    if(dst)*src=s; return n;
}

#include <getopt.h>
char* optarg; int optind=1, opterr=0, optopt=0; static int g_optpos=0;
int getopt_long(int argc,char* const* argv,const char* sh,const struct option* lo,int* li){
    optarg=0; if(optind<1){ optind=1; g_optpos=0; }
    if(optind>=argc)return -1;
    char* a=argv[optind]; int inorder=(sh&&sh[0]=='-');
    if(sh&&(sh[0]=='-'||sh[0]=='+'))sh++;
    if(g_optpos==0){
        if(a[0]!='-'||a[1]==0){ if(inorder){ optarg=a; optind++; return 1; } return -1; }
        if(a[1]=='-'&&a[2]==0){ optind++; return -1; }
        if(a[1]=='-'){
            const char* nm=a+2; int nl=0; while(nm[nl]&&nm[nl]!='=')nl++;
            for(int i=0;lo&&lo[i].name;i++){ if((int)strlen(lo[i].name)==nl&&!strncmp(lo[i].name,nm,(size_t)nl)){
                optind++; if(li)*li=i;
                if(lo[i].has_arg){ if(nm[nl]=='=')optarg=(char*)nm+nl+1; else if(lo[i].has_arg==required_argument){ if(optind<argc)optarg=argv[optind++]; else return '?'; } }
                if(lo[i].flag){ *lo[i].flag=lo[i].val; return 0; } return lo[i].val; } }
            optind++; return '?';
        }
        g_optpos=1;
    }
    char c=a[g_optpos++]; const char* p=sh; while(*p&&*p!=c)p++;
    if(!*p||c==':'){ optopt=c; if(!a[g_optpos]){ optind++; g_optpos=0; } return '?'; }
    if(p[1]==':'){ if(a[g_optpos])optarg=a+g_optpos; else if(optind+1<argc){ optind++; optarg=argv[optind]; } else { optind++; g_optpos=0; optopt=c; return '?'; }
        optind++; g_optpos=0; return c; }
    if(!a[g_optpos]){ optind++; g_optpos=0; }
    return c;
}
int getopt(int argc,char* const* argv,const char* sh){ return getopt_long(argc,argv,sh,0,0); }
void nvx_getopt_reset(void){ optind=1; g_optpos=0; optarg=0; }

#include <sys/stat.h>
#include <unistd.h>
char* basename(char* p){ char* s=p; for(char* q=p;*q;q++) if(*q=='/'&&q[1])s=q+1; return s; }
long sysconf(int n){ return n==_SC_PAGESIZE?4096:-1; }
int stat(const char* path,struct stat* st){ memset(st,0,sizeof(*st)); if(path&&!strcmp(path,"nvxdisk")){ st->st_mode=S_IFBLK; return 0; } errno=ENOENT; return -1; }
