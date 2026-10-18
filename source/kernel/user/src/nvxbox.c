#include "posix.h"
static int rc=0;
static void err(const char* a,const char* b,const char* c){ fprintf(stderr,"%s: %s: %s\n",a,b,c); rc=1; }
static const char* base(const char* p){ const char* b=p; for(;*p;p++) if(*p=='/')b=p+1; return b; }
static void join(char* o,const char* d,const char* n){ size_t l=strlen(d); strcpy(o,d); if(l&&o[l-1]!='/')strcat(o,"/"); strcat(o,n); }
static int lookup_id(const char* file,int id,char* out,int cap){ char buf[2048]; int fd=open(file,O_RDONLY); if(fd<0)return 0; long n=read(fd,buf,sizeof(buf)-1); close(fd); if(n<=0)return 0; buf[n]=0;
    for(char* l=buf;*l;){ char f[3][40]; int fi=0,o=0; f[0][0]=f[1][0]=f[2][0]=0; char* e=l; while(*e&&*e!='\n'){ if(*e==':'){ if(fi<3)f[fi][o]=0; fi++; o=0; } else if(fi<3&&o<39)f[fi][o++]=*e; e++; } if(fi<3)f[fi][o]=0;
        if(atoi(f[2])==id&&f[2][0]){ strncpy(out,f[0],(size_t)cap-1); out[cap-1]=0; return 1; } l=*e?e+1:e; }
    return 0; }
static int lookup_name(const char* file,const char* nm){ char buf[2048]; int fd=open(file,O_RDONLY); if(fd<0)return -1; long n=read(fd,buf,sizeof(buf)-1); close(fd); if(n<=0)return -1; buf[n]=0;
    for(char* l=buf;*l;){ char f[3][40]; int fi=0,o=0; f[0][0]=f[1][0]=f[2][0]=0; char* e=l; while(*e&&*e!='\n'){ if(*e==':'){ if(fi<3)f[fi][o]=0; fi++; o=0; } else if(fi<3&&o<39)f[fi][o++]=*e; e++; } if(fi<3)f[fi][o]=0;
        if(!strcmp(f[0],nm))return atoi(f[2]); l=*e?e+1:e; }
    return -1; }
static void modestr(unsigned m,char* s){ s[0]=S_ISDIR(m)?'d':S_ISLNK(m)?'l':((m&0170000)==0020000)?'c':'-'; const char* r="rwxrwxrwx"; for(int i=0;i<9;i++)s[1+i]=(m&(1u<<(8-i)))?r[i]:'-'; s[10]=0; }
static int cmpstrp(const void* a,const void* b){ return strcmp(*(char* const*)a,*(char* const*)b); }
static int cmpstr(const void* a,const void* b,void* c){ (void)c; return strcmp((const char*)a,(const char*)b); }
static void ls_one(const char* dir,const char* name,int lng){
    char p[256]; if(dir)join(p,dir,name); else strcpy(p,name);
    if(!lng){ printf("%s\n",name); return; }
    struct stat st; if(lstat(p,&st)<0){ printf("?????????? ? ?    ?        ? %s\n",name); return; }
    char m[12]; modestr(st.st_mode,m); char u[20],g[20]; if(!lookup_id("/etc/passwd",(int)st.st_uid,u,20))sprintf(u,"%u",st.st_uid); if(!lookup_id("/etc/group",(int)st.st_gid,g,20))sprintf(g,"%u",st.st_gid);
    char ds[24]="             "; if(st.st_mtime){ time_t t=(time_t)st.st_mtime; struct tm* tm=localtime(&t); strftime(ds,sizeof(ds),"%b %d %H:%M",tm); }
    printf("%s %2u %-6s %-6s %8lu %s %s",m,st.st_nlink,u,g,st.st_size,ds,name);
    if(S_ISLNK(st.st_mode)){ char t[200]; long n=readlink(p,t,sizeof(t)-1); if(n>0){ t[n]=0; printf(" -> %s",t); } }
    printf("\n");
}
static int do_ls(int argc,char** argv){
    int lng=0,all=0,np=0; char* paths[16];
    for(int i=1;i<argc;i++){ if(argv[i][0]=='-'){ for(int k=1;argv[i][k];k++){ if(argv[i][k]=='l')lng=1; if(argv[i][k]=='a')all=1; } } else if(np<16)paths[np++]=argv[i]; }
    if(!np)paths[np++]=".";
    for(int pi=0;pi<np;pi++){ struct stat st; char* p=paths[pi];
        if(stat(p,&st)<0){ err("ls",p,"No such file or directory"); continue; }
        if(!S_ISDIR(st.st_mode)){ ls_one(0,p,lng); continue; }
        if(np>1)printf("%s:\n",p);
        DIR* d=opendir(p); if(!d){ err("ls",p,"cannot open"); continue; }
        char** names=0; int n=0, cap=0; struct dirent* e; while((e=readdir(d))){ if(!all&&e->d_name[0]=='.')continue; if(n==cap){ cap=cap?cap*2:64; names=realloc(names,(size_t)cap*sizeof(char*)); } names[n++]=strdup(e->d_name); } closedir(d);
        qsort(names,(size_t)n,sizeof(char*),cmpstrp);
        if(lng) for(int i=0;i<n;i++)ls_one(p,names[i],1);
        else if(!isatty(1)) for(int i=0;i<n;i++)printf("%s\n",names[i]);
        else { int col=0; for(int i=0;i<n;i++){ int L=(int)strlen(names[i]); if(col&&col+L+2>88){ printf("\n"); col=0; } printf("%s  ",names[i]); col+=L+2; } if(n)printf("\n"); }
        for(int i=0;i<n;i++)free(names[i]); free(names);
        if(np>1&&pi<np-1)printf("\n"); }
    return rc;
}
static int each_input(int argc,char** argv,int first,void(*fn)(int,const char*)){
    int any=0; for(int i=first;i<argc;i++){ if(argv[i][0]=='-'&&argv[i][1])continue; any=1; if(!strcmp(argv[i],"-")){ fn(0,"-"); continue; } int fd=open(argv[i],O_RDONLY); if(fd<0){ err(base(argv[0]),argv[i],"No such file or directory"); continue; } fn(fd,argv[i]); close(fd); }
    if(!any)fn(0,"-"); return rc; }
static void f_cat(int fd,const char* n){ (void)n; char b[512]; long r; while((r=read(fd,b,sizeof(b)))>0)write(1,b,(size_t)r); }
static int readline_fd(int fd,char* out,int cap){ static char buf[1024]; static int bl=0,bp=0,lastfd=-1; if(fd!=lastfd){ bl=bp=0; lastfd=fd; } int n=0;
    for(;;){ if(bp>=bl){ long r=read(fd,buf,sizeof(buf)); if(r<=0){ out[n]=0; return n?n:-1; } bl=(int)r; bp=0; } char c=buf[bp++]; if(c=='\n'){ out[n]=0; return n; } if(n<cap-1)out[n++]=c; } }
static const char* g_pat; static int g_i,g_v,g_nflag,g_c,g_multi,g_hits;
static int has(const char* s,const char* p){ if(!*p)return 1; for(;*s;s++){ const char* a=s; const char* b=p; while(*a&&*b){ char x=*a,y=*b; if(g_i){ if(x>='A'&&x<='Z')x+=32; if(y>='A'&&y<='Z')y+=32; } if(x!=y)break; a++; b++; } if(!*b)return 1; } return 0; }
static void f_grep(int fd,const char* n){ char l[1024]; int ln=0,c=0; while(readline_fd(fd,l,sizeof(l))>=0){ ln++; int m=has(l,g_pat); if(g_v)m=!m; if(!m)continue; c++; g_hits++; if(g_c)continue; if(g_multi)printf("%s:",n); if(g_nflag)printf("%d:",ln); printf("%s\n",l); } if(g_c){ if(g_multi)printf("%s:",n); printf("%d\n",c); } }
static int do_grep(int argc,char** argv){ int i=1; for(;i<argc&&argv[i][0]=='-';i++){ for(int k=1;argv[i][k];k++){ char c=argv[i][k]; if(c=='i')g_i=1; if(c=='v')g_v=1; if(c=='n')g_nflag=1; if(c=='c')g_c=1; } }
    if(i>=argc){ fprintf(stderr,"usage: grep [-ivnc] pattern [file...]\n"); return 2; } g_pat=argv[i++]; g_multi=(argc-i)>1; each_input(argc,argv,i,f_grep); return g_hits?0:1; }
static int wl,ww,wc_,wf,wtot[3],wfiles;
static void f_wc(int fd,const char* n){ char b[512]; long r; int l=0,w=0,c=0,inw=0; while((r=read(fd,b,sizeof(b)))>0){ for(long i=0;i<r;i++){ c++; if(b[i]=='\n')l++; if(b[i]==' '||b[i]=='\n'||b[i]=='\t'){ inw=0; } else if(!inw){ inw=1; w++; } } }
    wtot[0]+=l; wtot[1]+=w; wtot[2]+=c; wfiles++; if(wf||!(wl||ww||wc_)){ printf("%7d %7d %7d",l,w,c); } else { if(wl)printf("%7d",l); if(ww)printf("%7d",w); if(wc_)printf("%7d",c); } if(strcmp(n,"-"))printf(" %s",n); printf("\n"); }
static int do_wc(int argc,char** argv){ for(int i=1;i<argc;i++) if(argv[i][0]=='-')for(int k=1;argv[i][k];k++){ if(argv[i][k]=='l')wl=1; if(argv[i][k]=='w')ww=1; if(argv[i][k]=='c')wc_=1; } each_input(argc,argv,1,f_wc); if(wfiles>1)printf("%7d %7d %7d total\n",wtot[0],wtot[1],wtot[2]); return rc; }
static int g_n=10;
static void f_head(int fd,const char* n){ (void)n; char l[1024]; for(int i=0;i<g_n&&readline_fd(fd,l,sizeof(l))>=0;i++)printf("%s\n",l); }
static void f_tail(int fd,const char* n){ (void)n; static char ring[64][256]; int cnt=0; char l[1024]; while(readline_fd(fd,l,sizeof(l))>=0){ strncpy(ring[cnt%64],l,255); ring[cnt%64][255]=0; cnt++; } int k=g_n>64?64:g_n; int s=cnt>k?cnt-k:0; for(int i=s;i<cnt;i++)printf("%s\n",ring[i%64]); }
static int nopt(int argc,char** argv){ for(int i=1;i<argc;i++){ if(!strcmp(argv[i],"-n")&&i+1<argc){ g_n=atoi(argv[i+1]); argv[i][0]='-'; argv[i+1]="-x"; } else if(argv[i][0]=='-'&&argv[i][1]>='0'&&argv[i][1]<='9')g_n=atoi(argv[i]+1); } return 0; }
static void f_sort(int fd,const char* n){ (void)n; static char lines[256][128]; static int cnt=0; char l[1024]; while(cnt<256&&readline_fd(fd,l,sizeof(l))>=0){ strncpy(lines[cnt],l,127); lines[cnt][127]=0; cnt++; } if(fd==0||1){ qsort_r(lines,(size_t)cnt,128,cmpstr,0); } }
static int do_mkdir(int argc,char** argv){ int p=0; for(int i=1;i<argc;i++) if(!strcmp(argv[i],"-p"))p=1;
    for(int i=1;i<argc;i++){ if(argv[i][0]=='-')continue; char cwd[256]; getcwd(cwd,sizeof(cwd)); char path[256]; strncpy(path,argv[i],255); path[255]=0;
        if(path[0]=='/')chdir("/"); char* s=path; int ok=1; while(*s){ while(*s=='/')s++; if(!*s)break; char* e=s; while(*e&&*e!='/')e++; char save=*e; *e=0; int last=!save||!e[1];
            struct stat st; if(stat(s,&st)==0){ if(last&&!p){ err("mkdir",argv[i],"File exists"); ok=0; break; } }
            else if(!last&&!p){ err("mkdir",argv[i],"No such file or directory"); ok=0; break; } else if(mkdir(s,0777)<0){ err("mkdir",argv[i],"cannot create"); ok=0; break; }
            if(chdir(s)<0){ ok=0; break; } *e=save; s=e; }
        (void)ok; chdir(cwd); }
    return rc; }
static int do_rm(int argc,char** argv){ int r=0,f=0; for(int i=1;i<argc;i++) if(argv[i][0]=='-'){ for(int k=1;argv[i][k];k++){ if(argv[i][k]=='r'||argv[i][k]=='R')r=1; if(argv[i][k]=='f')f=1; } }
    for(int i=1;i<argc;i++){ if(argv[i][0]=='-')continue; struct stat st; if(lstat(argv[i],&st)<0){ if(!f)err("rm",argv[i],"No such file or directory"); continue; }
        if(S_ISDIR(st.st_mode)){ if(!r){ err("rm",argv[i],"Is a directory"); continue; } if(rmtree(argv[i])<0)err("rm",argv[i],"could not remove everything"); }
        else if(unlink(argv[i])<0)err("rm",argv[i],"Permission denied"); }
    return rc; }
static int do_cp(int argc,char** argv){ if(argc<3){ fprintf(stderr,"usage: cp src dst\n"); return 1; } const char* d=argv[argc-1]; struct stat st; int dd=(stat(d,&st)==0&&S_ISDIR(st.st_mode));
    for(int i=1;i<argc-1;i++){ char to[256]; if(dd)join(to,d,base(argv[i])); else strcpy(to,d); if(copyfile(argv[i],to)<0)err("cp",argv[i],"cannot copy"); } return rc; }
static int do_mv(int argc,char** argv){ if(argc<3){ fprintf(stderr,"usage: mv src dst\n"); return 1; } for(int i=1;i<argc-1;i++) if(rename(argv[i],argv[argc-1])<0)err("mv",argv[i],"cannot move"); return rc; }
static int do_touch(int argc,char** argv){ for(int i=1;i<argc;i++){ struct stat st; if(stat(argv[i],&st)==0){ utime(argv[i],0); } else { int fd=open(argv[i],O_CREAT|O_WRONLY); if(fd<0)err("touch",argv[i],"cannot create"); else close(fd); } } return rc; }
static int do_ln(int argc,char** argv){ int s=0,i=1; if(i<argc&&!strcmp(argv[i],"-s")){ s=1; i++; } if(argc-i<2){ fprintf(stderr,"usage: ln [-s] target linkname\n"); return 1; }
    int r=s?symlink(argv[i],argv[i+1]):link(argv[i],argv[i+1]); if(r<0)err("ln",argv[i+1],"cannot create link"); return rc; }
static int do_chmod(int argc,char** argv){ if(argc<3){ fprintf(stderr,"usage: chmod MODE file...\n"); return 1; } unsigned m=(unsigned)strtol(argv[1],0,8); for(int i=2;i<argc;i++) if(chmod(argv[i],m)<0)err("chmod",argv[i],"Operation not permitted"); return rc; }
static int do_chown(int argc,char** argv){ if(argc<3){ fprintf(stderr,"usage: chown USER[:GROUP] file...\n"); return 1; } char u[40],g[40]; g[0]=0; strncpy(u,argv[1],39); u[39]=0; char* c=strchr(u,':'); if(c){ *c=0; strncpy(g,c+1,39); }
    int uid=u[0]?((u[0]>='0'&&u[0]<='9')?atoi(u):lookup_name("/etc/passwd",u)):-1; int gid=g[0]?((g[0]>='0'&&g[0]<='9')?atoi(g):lookup_name("/etc/group",g)):-1;
    if(u[0]&&uid<0){ err("chown",u,"invalid user"); return 1; } for(int i=2;i<argc;i++) if(chown(argv[i],uid,gid)<0)err("chown",argv[i],"Operation not permitted"); return rc; }
static int do_stat(int argc,char** argv){ for(int i=1;i<argc;i++){ struct stat st; if(lstat(argv[i],&st)<0){ err("stat",argv[i],"No such file or directory"); continue; } char m[12]; modestr(st.st_mode,m);
        const char* ty=S_ISDIR(st.st_mode)?"directory":S_ISLNK(st.st_mode)?"symbolic link":((st.st_mode&0170000)==0020000)?"character device":"regular file";
        char u[20],g[20]; if(!lookup_id("/etc/passwd",(int)st.st_uid,u,20))strcpy(u,"?"); if(!lookup_id("/etc/group",(int)st.st_gid,g,20))strcpy(g,"?");
        printf("  File: %s\n  Size: %lu\tType: %s\n Inode: %lu\tLinks: %u\nAccess: (%04o/%s)  Uid: (%u/%s)  Gid: (%u/%s)\n",argv[i],st.st_size,ty,st.st_ino,st.st_nlink,st.st_mode&07777,m,st.st_uid,u,st.st_gid,g);
        if(st.st_mtime){ time_t t=(time_t)st.st_mtime; printf("Modify: %s\n",ctime(&t)); } } return rc; }
static int do_id(void){ int u=getuid(),g=getgid(); char un[20],gn[20]; if(!lookup_id("/etc/passwd",u,un,20))strcpy(un,"?"); if(!lookup_id("/etc/group",g,gn,20))strcpy(gn,"?"); printf("uid=%d(%s) gid=%d(%s)\n",u,un,g,gn); return 0; }
int coreutils_main(int argc,char** argv);
int main(int argc,char** argv){
    const char* c=base(argv[0]);
    if(strcmp(c,"nvxbox")){ int r=coreutils_main(argc,argv); if(r!=-1000)return r; }
    if(!strcmp(c,"nvxbox")){ if(argc>1){ return main(argc-1,argv+1); } printf("nvxbox: ls cat grep wc head tail echo mkdir rmdir rm mv cp touch ln chmod chown stat pwd hostname whoami id uname date sleep mount umount readlink tee sort true false\n"); return 0; }
    if(!strcmp(c,"ls"))return do_ls(argc,argv);
    if(!strcmp(c,"cat"))return each_input(argc,argv,1,f_cat);
    if(!strcmp(c,"grep"))return do_grep(argc,argv);
    if(!strcmp(c,"wc"))return do_wc(argc,argv);
    if(!strcmp(c,"head")){ nopt(argc,argv); return each_input(argc,argv,1,f_head); }
    if(!strcmp(c,"tail")){ nopt(argc,argv); return each_input(argc,argv,1,f_tail); }
    if(!strcmp(c,"echo")){ int i=1,nl=1; if(i<argc&&!strcmp(argv[i],"-n")){ nl=0; i++; } for(;i<argc;i++){ printf("%s",argv[i]); if(i<argc-1)printf(" "); } if(nl)printf("\n"); return 0; }
    if(!strcmp(c,"mkdir"))return do_mkdir(argc,argv);
    if(!strcmp(c,"rmdir")){ for(int i=1;i<argc;i++) if(rmdir(argv[i])<0)err("rmdir",argv[i],"not empty or not found"); return rc; }
    if(!strcmp(c,"rm"))return do_rm(argc,argv);
    if(!strcmp(c,"mv"))return do_mv(argc,argv);
    if(!strcmp(c,"cp"))return do_cp(argc,argv);
    if(!strcmp(c,"touch"))return do_touch(argc,argv);
    if(!strcmp(c,"ln"))return do_ln(argc,argv);
    if(!strcmp(c,"chmod"))return do_chmod(argc,argv);
    if(!strcmp(c,"chown"))return do_chown(argc,argv);
    if(!strcmp(c,"stat"))return do_stat(argc,argv);
    if(!strcmp(c,"pwd")){ char b[256]; printf("%s\n",getcwd(b,sizeof(b))?b:"?"); return 0; }
    if(!strcmp(c,"hostname")){ if(argc>1){ if(sethostname(argv[1],strlen(argv[1]))<0){ err("hostname",argv[1],"Operation not permitted"); return 1; } int fd=open("/etc/hostname",O_CREAT|O_WRONLY|O_TRUNC); if(fd>=0){ write(fd,argv[1],strlen(argv[1])); write(fd,"\n",1); close(fd); } return 0; } char b[64]; gethostname(b,sizeof(b)); printf("%s\n",b); return 0; }
    if(!strcmp(c,"whoami")){ char u[20]; if(!lookup_id("/etc/passwd",getuid(),u,20))sprintf(u,"%d",getuid()); printf("%s\n",u); return 0; }
    if(!strcmp(c,"id"))return do_id();
    if(!strcmp(c,"uname")){ char h[64]; gethostname(h,sizeof(h)); char r[32]="?"; int fd=open("/sys/kernel/osrelease",O_RDONLY); if(fd>=0){ int n=(int)read(fd,r,sizeof(r)-1); close(fd); if(n<0)n=0; r[n]=0; for(int i=0;r[i];i++)if(r[i]=='\n'){ r[i]=0; break; } } if(argc>1&&!strcmp(argv[1],"-a"))printf("NoovexOS %s %s nvx64 x86_64\n",h,r); else if(argc>1&&!strcmp(argv[1],"-r"))printf("%s\n",r); else printf("NoovexOS\n"); return 0; }
    if(!strcmp(c,"date")){ time_t t=time(0); printf("%s\n",ctime(&t)); return 0; }
    if(!strcmp(c,"sleep")){ if(argc<2)return 1; sleep((unsigned)atoi(argv[1])); return 0; }
    if(!strcmp(c,"mount")){ if(argc<3){ int fd=open("/proc/mounts",O_RDONLY); char b[512]; long n; while(fd>=0&&(n=read(fd,b,sizeof(b)))>0)write(1,b,(size_t)n); if(fd>=0)close(fd); return 0; }
        if(mount(argv[1],argv[2],0,0,0)<0){ err("mount",argv[2],"cannot mount (root only; USB drive with FAT at /mnt/usb)"); return 1; } return 0; }
    if(!strcmp(c,"umount")){ if(argc<2||umount(argv[1])<0){ err("umount",argc>1?argv[1]:"?","not mounted"); return 1; } return 0; }
    if(!strcmp(c,"readlink")){ for(int i=1;i<argc;i++){ char b[256]; long n=readlink(argv[i],b,sizeof(b)-1); if(n<0){ rc=1; continue; } b[n]=0; printf("%s\n",b); } return rc; }
    if(!strcmp(c,"tee")){ int ap=0,i=1; if(i<argc&&!strcmp(argv[i],"-a")){ ap=1; i++; } int fds[8],nf=0; for(;i<argc&&nf<8;i++){ int fd=open(argv[i],O_CREAT|O_WRONLY|(ap?O_APPEND:O_TRUNC)); if(fd>=0)fds[nf++]=fd; }
        char b[512]; long r; while((r=read(0,b,sizeof(b)))>0){ write(1,b,(size_t)r); for(int k=0;k<nf;k++)write(fds[k],b,(size_t)r); } for(int k=0;k<nf;k++)close(fds[k]); return 0; }
    if(!strcmp(c,"sort")){ static char lines[256][128]; int cnt=0,rev=0; char l[1024]; for(int i=1;i<argc;i++) if(!strcmp(argv[i],"-r"))rev=1;
        int fd=0; for(int i=1;i<argc;i++) if(argv[i][0]!='-'){ fd=open(argv[i],O_RDONLY); if(fd<0){ err("sort",argv[i],"No such file"); return 1; } }
        while(cnt<256&&readline_fd(fd,l,sizeof(l))>=0){ strncpy(lines[cnt],l,127); lines[cnt][127]=0; cnt++; } qsort_r(lines,(size_t)cnt,128,cmpstr,0);
        for(int i=0;i<cnt;i++)printf("%s\n",lines[rev?cnt-1-i:i]); return 0; }
    if(!strcmp(c,"true"))return 0;
    if(!strcmp(c,"false"))return 1;
    fprintf(stderr,"nvxbox: unknown command %s\n",c); return 127;
}
